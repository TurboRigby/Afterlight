# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.24)
project(Afterlight VERSION 0.9.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan 1.1 REQUIRED COMPONENTS glslangValidator)
find_package(SDL3 3.4 CONFIG REQUIRED)

# Compile GLSL into embedded SPIR-V; no runtime paths or shader compiler needed.
set(afterlight_shader_headers)
foreach(shader mesh.vert mesh.frag shadow.vert ui.vert ui.frag fullscreen.vert post.frag sky.frag)
    string(REPLACE "." "_" shader_name "${shader}")
    set(shader_source "${CMAKE_CURRENT_SOURCE_DIR}/engine/shaders/${shader}")
    set(shader_header "${CMAKE_CURRENT_BINARY_DIR}/generated/${shader_name}.h")
    add_custom_command(
        OUTPUT "${shader_header}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
        COMMAND Vulkan::glslangValidator --target-env vulkan1.1 --spirv-val
            --vn af_${shader_name} -o "${shader_header}" "${shader_source}"
        DEPENDS "${shader_source}" Vulkan::glslangValidator
        COMMENT "Compiling Afterlight ${shader} shader"
        VERBATIM
    )
    list(APPEND afterlight_shader_headers "${shader_header}")
endforeach()

add_library(afterlight_json STATIC third_party/cjson/cJSON.c)
target_compile_definitions(afterlight_json PRIVATE ENABLE_LOCALES=1 CJSON_NESTING_LIMIT=32 CJSON_HIDE_SYMBOLS)

add_library(afterlight_gltf STATIC engine/src/cgltf.c)
target_include_directories(afterlight_gltf PRIVATE third_party/cgltf)

add_library(afterlight_image STATIC engine/src/image.c)
target_include_directories(afterlight_image PRIVATE third_party/stb)

add_library(afterlight STATIC
    ${afterlight_shader_headers}
    engine/src/animation.c
    engine/src/animation_player.c
    engine/src/environment_job.c
    engine/src/asset_watch.c
    engine/src/gltf.c
    engine/src/audio.c
    engine/src/gameplay.c
    engine/src/project.c
    engine/src/game_host.c
    engine/src/character.c
    engine/src/collision.c
    engine/src/simulation.c
    engine/src/mipmap.c
    engine/src/asset.c
    engine/src/shadow_math.c
    engine/src/shadow_vulkan.c
    engine/src/scene_io.c
    engine/src/scene.c
    engine/src/camera.c
    engine/src/math.c
    engine/src/engine.c
    engine/src/texture_vulkan.c
    engine/src/ui_vulkan.c
    engine/src/local_shadow.c
    engine/src/ibl.c
    engine/src/post_vulkan.c
    engine/src/renderer_vulkan.c
)
add_library(Afterlight::Engine ALIAS afterlight)
target_include_directories(afterlight PUBLIC engine/include PRIVATE
    engine/src third_party/cjson third_party/stb third_party/cgltf "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(afterlight PRIVATE SDL3::SDL3 Vulkan::Vulkan afterlight_json afterlight_image afterlight_gltf)
if(UNIX)
    target_link_libraries(afterlight PUBLIC m)
endif()
target_compile_definitions(afterlight PRIVATE
    $<$<CONFIG:Debug>:AF_DEBUG=1>
)

function(afterlight_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()
endfunction()
afterlight_warnings(afterlight)
add_executable(afterlight_profile sandbox/profile.c)
target_link_libraries(afterlight_profile PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_profile)

add_library(afterlight_game MODULE game/demo_game.c)
target_include_directories(afterlight_game PRIVATE engine/include)
set_target_properties(afterlight_game PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
if(UNIX)
    target_link_libraries(afterlight_game PRIVATE m)
endif()
afterlight_warnings(afterlight_game)
add_library(afterlight_editor_document STATIC editor/document.c editor/gizmo.c editor/runtime.c editor/asset_browser.c editor/collider_view.c editor/light_gizmo.c)
target_include_directories(afterlight_editor_document PUBLIC editor)
target_link_libraries(afterlight_editor_document PUBLIC Afterlight::Engine SDL3::SDL3)
afterlight_warnings(afterlight_editor_document)
add_library(afterlight_nuklear STATIC editor/nuklear.c)
target_include_directories(afterlight_nuklear PUBLIC third_party/nuklear editor)
add_library(afterlight_editor_ui STATIC editor/gui.c editor/dialog.c editor/play_hud.c editor/thumbnail.c)
target_include_directories(afterlight_editor_ui PRIVATE engine/src sandbox)
target_link_libraries(afterlight_editor_ui PUBLIC afterlight_nuklear Afterlight::Engine PRIVATE SDL3::SDL3)
afterlight_warnings(afterlight_editor_ui)

add_executable(afterlight_sandbox sandbox/main.c)
target_link_libraries(afterlight_sandbox PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_sandbox)
set(sandbox_scene "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/scenes/demo.afterlight.json")
if(APPLE)
    target_sources(afterlight_sandbox PRIVATE "${sandbox_scene}")
    set_source_files_properties("${sandbox_scene}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/scenes")
else()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/scenes/demo.afterlight.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sandbox_scene}" "${CMAKE_CURRENT_BINARY_DIR}/scenes/demo.afterlight.json"
        DEPENDS "${sandbox_scene}" VERBATIM)
    add_custom_target(afterlight_sandbox_assets DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/scenes/demo.afterlight.json")
    add_dependencies(afterlight_sandbox afterlight_sandbox_assets)
endif()
if(APPLE)
    set_target_properties(afterlight_sandbox PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Sandbox"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.sandbox"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
endif()
add_executable(afterlight_editor editor/main.c)
target_include_directories(afterlight_editor PRIVATE engine/src sandbox)
target_link_libraries(afterlight_editor PRIVATE afterlight_editor_document afterlight_editor_ui SDL3::SDL3)
afterlight_warnings(afterlight_editor)
add_dependencies(afterlight_editor afterlight_game)
target_compile_definitions(afterlight_editor PRIVATE AF_GAME_MODULE_PATH="$<TARGET_FILE:afterlight_game>")
if(APPLE)
    add_custom_command(TARGET afterlight_game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/afterlight_editor.app/Contents/Resources/game"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:afterlight_game>" "${CMAKE_CURRENT_BINARY_DIR}/afterlight_editor.app/Contents/Resources/game/$<TARGET_FILE_NAME:afterlight_game>")
endif()
if(APPLE)
    set_target_properties(afterlight_editor PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Editor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.editor"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    target_sources(afterlight_editor PRIVATE "${sandbox_scene}" editor/assets/FiraSans-Regular.ttf editor/assets/OFL.txt third_party/nuklear/LICENSE)
    set_source_files_properties(editor/assets/FiraSans-Regular.ttf PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
    set_source_files_properties(editor/assets/OFL.txt PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/firasans")
    set_source_files_properties(third_party/nuklear/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/nuklear")
else()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fonts/FiraSans-Regular.ttf"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/editor/assets/FiraSans-Regular.ttf" "${CMAKE_CURRENT_BINARY_DIR}/fonts/FiraSans-Regular.ttf"
        DEPENDS editor/assets/FiraSans-Regular.ttf VERBATIM)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/licenses/firasans/OFL.txt" "${CMAKE_CURRENT_BINARY_DIR}/licenses/nuklear/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/licenses/firasans" "${CMAKE_CURRENT_BINARY_DIR}/licenses/nuklear"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/editor/assets/OFL.txt" "${CMAKE_CURRENT_BINARY_DIR}/licenses/firasans/OFL.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nuklear/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/licenses/nuklear/LICENSE"
        DEPENDS editor/assets/OFL.txt third_party/nuklear/LICENSE VERBATIM)
    add_custom_target(afterlight_editor_assets DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/fonts/FiraSans-Regular.ttf"
        "${CMAKE_CURRENT_BINARY_DIR}/licenses/firasans/OFL.txt" "${CMAKE_CURRENT_BINARY_DIR}/licenses/nuklear/LICENSE")
    add_dependencies(afterlight_editor afterlight_editor_assets afterlight_sandbox_assets)
endif()
add_custom_target(run_editor COMMAND $<TARGET_FILE:afterlight_editor> DEPENDS afterlight_editor USES_TERMINAL)

set(afterlight_sample_assets "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets/pyramid.obj" "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets/checker.tga" "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets/sphere.obj" "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets/studio.hdr")
if(APPLE)
    foreach(target afterlight_sandbox afterlight_editor)
        target_sources(${target} PRIVATE ${afterlight_sample_assets})
    endforeach()
    set_source_files_properties(${afterlight_sample_assets} PROPERTIES HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION "Resources/assets")
else()
    add_custom_target(afterlight_sample_assets_copy
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets" "${CMAKE_CURRENT_BINARY_DIR}/assets")
    add_dependencies(afterlight_editor afterlight_sample_assets_copy)
    add_dependencies(afterlight_sandbox afterlight_sample_assets_copy)
endif()

# Keep dependency notices with runnable applications.
if(APPLE)
    foreach(target afterlight_sandbox afterlight_editor)
        target_sources(${target} PRIVATE third_party/stb/LICENSE third_party/cjson/LICENSE)
    endforeach()
    set_source_files_properties(third_party/stb/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/stb")
    set_source_files_properties(third_party/cjson/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/cjson")
else()
    add_custom_target(afterlight_license_copy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/licenses/stb" "${CMAKE_CURRENT_BINARY_DIR}/licenses/cjson" "${CMAKE_CURRENT_BINARY_DIR}/licenses/cgltf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/licenses/stb/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cjson/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/licenses/cjson/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cgltf/LICENSE" "${CMAKE_CURRENT_BINARY_DIR}/licenses/cgltf/LICENSE")
    add_dependencies(afterlight_editor afterlight_license_copy)
    add_dependencies(afterlight_sandbox afterlight_license_copy)
endif()

add_custom_target(run
    COMMAND $<TARGET_FILE:afterlight_sandbox>
    DEPENDS afterlight_sandbox
    USES_TERMINAL
)

# The obstacle course uses the same C gameplay as a reloadable module and standalone app.
add_library(afterlight_playground_game MODULE game/playground_game.c)
target_include_directories(afterlight_playground_game PRIVATE engine/include)
set_target_properties(afterlight_playground_game PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
afterlight_warnings(afterlight_playground_game)
add_library(afterlight_playground_code STATIC game/playground_game.c)
target_include_directories(afterlight_playground_code PRIVATE engine/include)
afterlight_warnings(afterlight_playground_code)
if(UNIX)
    target_link_libraries(afterlight_playground_game PRIVATE m)
    target_link_libraries(afterlight_playground_code PRIVATE m)
endif()
add_dependencies(afterlight_editor afterlight_playground_game)
target_compile_definitions(afterlight_editor PRIVATE AF_PLAYGROUND_MODULE_PATH="$<TARGET_FILE:afterlight_playground_game>")
add_executable(afterlight_playground playground/main.c)
target_include_directories(afterlight_playground PRIVATE sandbox)
target_link_libraries(afterlight_playground PRIVATE afterlight_playground_code afterlight_editor_ui)
afterlight_warnings(afterlight_playground)
set(obstacle_scene "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/scenes/obstacle.afterlight.json")
if(APPLE)
    set_target_properties(afterlight_playground PROPERTIES MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Playground" MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.playground"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}" MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set_source_files_properties("${obstacle_scene}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/scenes")
    target_sources(afterlight_editor PRIVATE "${obstacle_scene}")
    target_sources(afterlight_playground PRIVATE "${obstacle_scene}" third_party/stb/LICENSE third_party/cjson/LICENSE)
    add_custom_command(TARGET afterlight_playground_game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/afterlight_editor.app/Contents/Resources/game"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:afterlight_playground_game>" "${CMAKE_CURRENT_BINARY_DIR}/afterlight_editor.app/Contents/Resources/game/$<TARGET_FILE_NAME:afterlight_playground_game>")
else()
    add_custom_target(afterlight_obstacle_copy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${obstacle_scene}" "${CMAKE_CURRENT_BINARY_DIR}/scenes/obstacle.afterlight.json")
    add_dependencies(afterlight_editor afterlight_obstacle_copy)
    add_dependencies(afterlight_playground afterlight_obstacle_copy afterlight_license_copy)
endif()
add_custom_target(run_playground COMMAND $<TARGET_FILE:afterlight_playground> DEPENDS afterlight_playground USES_TERMINAL)
add_custom_target(run_playground_editor COMMAND $<TARGET_FILE:afterlight_editor> --playground DEPENDS afterlight_editor USES_TERMINAL)

set(afterlight_sound_assets)
foreach(sound jump land checkpoint finish)
    list(APPEND afterlight_sound_assets "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets/${sound}.wav")
endforeach()
list(APPEND afterlight_sound_assets "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets/beacon.glb")
if(APPLE)
    set_source_files_properties(${afterlight_sound_assets} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets")
    target_sources(afterlight_editor PRIVATE ${afterlight_sound_assets})
    set_source_files_properties(third_party/cgltf/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/cgltf")
    foreach(target afterlight_editor afterlight_sandbox afterlight_playground)
        target_sources(${target} PRIVATE third_party/cgltf/LICENSE)
    endforeach()
    target_sources(afterlight_playground PRIVATE ${afterlight_sound_assets} editor/assets/FiraSans-Regular.ttf editor/assets/OFL.txt third_party/nuklear/LICENSE)
else()
    add_dependencies(afterlight_playground afterlight_sample_assets_copy afterlight_editor_assets)
endif()

# Development project files keep the scene and watched C module together.
foreach(sample demo playground)
    if(sample STREQUAL "demo")
        set(project_scene "${sandbox_scene}")
        set(project_module afterlight_game)
    else()
        set(project_scene "${obstacle_scene}")
        set(project_module afterlight_playground_game)
    endif()
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${sample}.afterlight.project" CONTENT
        "{\"format\":\"afterlight.project\",\"version\":1,\"name\":\"Afterlight ${sample}\",\"startup_scene\":\"${project_scene}\",\"game_module\":\"$<TARGET_FILE_NAME:${project_module}>\"}\n")
    if(APPLE)
        get_filename_component(scene_filename "${project_scene}" NAME)
        set(bundle_project "${CMAKE_CURRENT_BINARY_DIR}/generated/${sample}.afterlight.project")
        file(GENERATE OUTPUT "${bundle_project}" CONTENT
            "{\"format\":\"afterlight.project\",\"version\":1,\"name\":\"Afterlight ${sample}\",\"startup_scene\":\"scenes/${scene_filename}\",\"game_module\":\"game/$<TARGET_FILE_NAME:${project_module}>\"}\n")
        set_source_files_properties("${bundle_project}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(afterlight_editor PRIVATE "${bundle_project}")
    endif()
endforeach()
target_compile_definitions(afterlight_editor PRIVATE
    AF_DEMO_PROJECT_PATH="${CMAKE_CURRENT_BINARY_DIR}/demo.afterlight.project"
    AF_PLAYGROUND_PROJECT_PATH="${CMAKE_CURRENT_BINARY_DIR}/playground.afterlight.project")

add_executable(afterlight_player player/main.c)
target_include_directories(afterlight_player PRIVATE sandbox)
target_link_libraries(afterlight_player PRIVATE afterlight_editor_document afterlight_editor_ui)
afterlight_warnings(afterlight_player)
if(APPLE)
    set_target_properties(afterlight_player PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Player" MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.player" MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}" MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    target_sources(afterlight_player PRIVATE editor/assets/FiraSans-Regular.ttf editor/assets/OFL.txt third_party/nuklear/LICENSE third_party/stb/LICENSE third_party/cjson/LICENSE third_party/cgltf/LICENSE)
else()
    add_dependencies(afterlight_player afterlight_editor_assets afterlight_license_copy)
endif()
set(afterlight_lighting_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/lighting.afterlight.json")
if(APPLE)
    set_source_files_properties("${afterlight_lighting_scene}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/scenes")
    target_sources(afterlight_editor PRIVATE "${afterlight_lighting_scene}")
endif()
add_custom_target(run_lighting_editor COMMAND $<TARGET_FILE:afterlight_editor> --scene "${afterlight_lighting_scene}" DEPENDS afterlight_editor USES_TERMINAL VERBATIM)

add_custom_target(run_player COMMAND $<TARGET_FILE:afterlight_player> --project "${CMAKE_CURRENT_BINARY_DIR}/playground.afterlight.project" DEPENDS afterlight_player afterlight_playground_game USES_TERMINAL)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(cmake/AfterlightLicensing.cmake)
if(APPLE)
    set(afterlight_player_package "$<TARGET_BUNDLE_DIR:afterlight_player>")
else()
    set(afterlight_player_package "$<TARGET_FILE:afterlight_player>")
endif()
target_compile_definitions(afterlight_editor PRIVATE AF_PYTHON_PATH="${Python3_EXECUTABLE}"
    AF_PROJECT_TOOL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/scripts/afterlight.py" AF_PLAYER_PACKAGE_PATH="${afterlight_player_package}")
add_dependencies(afterlight_editor afterlight_player)

# Keep authoring templates/tooling usable when the editor bundle is moved.
if(APPLE)
    set(afterlight_tool_root "$<TARGET_BUNDLE_CONTENT_DIR:afterlight_editor>/Resources/tools")
    target_sources(afterlight_editor PRIVATE scripts/afterlight.py game/starter_game.c)
    set_source_files_properties(scripts/afterlight.py game/starter_game.c PROPERTIES HEADER_FILE_ONLY TRUE)
    set_property(TARGET afterlight_editor APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/afterlight.py" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_source.py" "${CMAKE_CURRENT_SOURCE_DIR}/game/starter_game.c" "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE" "${afterlight_source_archive}")
    add_custom_command(TARGET afterlight_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${afterlight_tool_root}/scripts" "${afterlight_tool_root}/game" "${afterlight_tool_root}/engine/src" "${afterlight_tool_root}/sandbox/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/scripts/afterlight.py" "${afterlight_tool_root}/scripts/afterlight.py"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_source.py" "${afterlight_tool_root}/scripts/package_source.py"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${afterlight_tool_root}/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/game/starter_game.c" "${afterlight_tool_root}/game/starter_game.c"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/module_sanitizer.c" "${afterlight_tool_root}/engine/src/module_sanitizer.c"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/engine/include" "${afterlight_tool_root}/engine/include"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sandbox_scene}" "${afterlight_tool_root}/sandbox/scenes/demo.afterlight.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:afterlight_player>" "${afterlight_tool_root}/afterlight_player.app" VERBATIM)
endif()

set(AFTERLIGHT_EXPORT_PROJECT "${CMAKE_CURRENT_BINARY_DIR}/playground.afterlight.project" CACHE FILEPATH "Saved project to export")
set(AFTERLIGHT_EXPORT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/exported-game" CACHE PATH "New directory for export")
add_custom_target(export_game
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/afterlight.py" export "${AFTERLIGHT_EXPORT_PROJECT}"
        --output "${AFTERLIGHT_EXPORT_DIRECTORY}" --player "${afterlight_player_package}"
    DEPENDS afterlight_player afterlight_playground_game USES_TERMINAL VERBATIM)

include(CTest)
if(BUILD_TESTING)
    add_executable(afterlight_animation_player_test tests/animation_player.c)
    target_link_libraries(afterlight_animation_player_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_animation_player_test)
    add_test(NAME afterlight.animation_player COMMAND afterlight_animation_player_test "${PROJECT_SOURCE_DIR}/tests/assets/animated.gltf")
    add_executable(afterlight_animation_test tests/animation.c)
    target_link_libraries(afterlight_animation_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_animation_test)
    add_test(NAME afterlight.animation COMMAND afterlight_animation_test "${PROJECT_SOURCE_DIR}/tests/assets")
    add_test(NAME afterlight.animation_data COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/animation_data.py" "$<TARGET_FILE:afterlight_animation_test>" "${PROJECT_SOURCE_DIR}/tests/assets/animated.gltf")
    add_executable(afterlight_shadow_budget_test tests/shadow_budget.c)
    target_include_directories(afterlight_shadow_budget_test PRIVATE engine/src)
    target_link_libraries(afterlight_shadow_budget_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_shadow_budget_test)
    add_test(NAME afterlight.shadow_budget COMMAND afterlight_shadow_budget_test)
    add_executable(afterlight_environment_job_test tests/environment_jobs.c)
    target_include_directories(afterlight_environment_job_test PRIVATE engine/src)
    target_link_libraries(afterlight_environment_job_test PRIVATE Afterlight::Engine SDL3::SDL3)
    afterlight_warnings(afterlight_environment_job_test)
    add_test(NAME afterlight.environment_jobs COMMAND afterlight_environment_job_test "${PROJECT_SOURCE_DIR}/tests/assets/panorama.hdr")
    set_tests_properties(afterlight.environment_jobs PROPERTIES TIMEOUT 30)
    add_test(NAME afterlight.editor_light_tools COMMAND afterlight_editor --light-tools-test --validation)
    set_tests_properties(afterlight.editor_light_tools PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_executable(afterlight_environment_test tests/environments.c)
    target_include_directories(afterlight_environment_test PRIVATE engine/src)
    target_link_libraries(afterlight_environment_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_environment_test)
    add_test(NAME afterlight.environments COMMAND afterlight_environment_test "${PROJECT_SOURCE_DIR}/tests/assets/panorama.hdr")
    set_tests_properties(afterlight.environments PROPERTIES TIMEOUT 60 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_executable(afterlight_light_gizmo_test tests/light_gizmos.c)
    target_link_libraries(afterlight_light_gizmo_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_light_gizmo_test)
    add_test(NAME afterlight.light_gizmos COMMAND afterlight_light_gizmo_test)
    add_executable(afterlight_hdr_test tests/hdr.c)
    target_include_directories(afterlight_hdr_test PRIVATE engine/src)
    target_link_libraries(afterlight_hdr_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_hdr_test)
    add_test(NAME afterlight.hdr COMMAND afterlight_hdr_test "${PROJECT_SOURCE_DIR}/tests/assets")
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/hdr-tests")
    set_tests_properties(afterlight.hdr PROPERTIES TIMEOUT 30 WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/hdr-tests")
    add_executable(afterlight_thumbnail_test tests/thumbnails.c)
    target_link_libraries(afterlight_thumbnail_test PRIVATE afterlight_editor_ui)
    afterlight_warnings(afterlight_thumbnail_test)
    add_test(NAME afterlight.thumbnails COMMAND afterlight_thumbnail_test "${PROJECT_SOURCE_DIR}/tests/assets")
    add_executable(afterlight_lighting_math_test tests/lighting_math.c)
    target_include_directories(afterlight_lighting_math_test PRIVATE engine/src)
    target_link_libraries(afterlight_lighting_math_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_lighting_math_test)
    add_test(NAME afterlight.lighting_math COMMAND afterlight_lighting_math_test)
    set_tests_properties(afterlight.lighting_math PROPERTIES TIMEOUT 30)
    add_executable(afterlight_lighting_test tests/lighting.c)
    target_include_directories(afterlight_lighting_test PRIVATE engine/src sandbox)
    target_link_libraries(afterlight_lighting_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_lighting_test)
    add_test(NAME afterlight.lighting COMMAND afterlight_lighting_test)
    set_tests_properties(afterlight.lighting PROPERTIES TIMEOUT 60 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.editor_thumbnails COMMAND afterlight_editor --scene "${afterlight_lighting_scene}" --thumbnail-test --validation)
    set_tests_properties(afterlight.editor_thumbnails PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.licensing COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/licensing.py"
        --root "${CMAKE_CURRENT_SOURCE_DIR}" --archive "${afterlight_source_archive}" --player "${afterlight_player_package}")
    set_tests_properties(afterlight.licensing PROPERTIES LABELS "packaging")
    add_test(NAME afterlight.player COMMAND afterlight_player --project "${CMAKE_CURRENT_BINARY_DIR}/playground.afterlight.project" --frames 80 --validation)
    set_tests_properties(afterlight.player PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_executable(afterlight_asset_reload_test tests/asset_reload.c)
    target_include_directories(afterlight_asset_reload_test PRIVATE engine/src)
    target_link_libraries(afterlight_asset_reload_test PRIVATE Afterlight::Engine SDL3::SDL3)
    afterlight_warnings(afterlight_asset_reload_test)
    add_test(NAME afterlight.asset_reload COMMAND afterlight_asset_reload_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asset-reload-tests")
    set_tests_properties(afterlight.asset_reload PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asset-reload-tests" TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_executable(afterlight_model_test tests/model.c)
    target_link_libraries(afterlight_model_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_model_test)
    add_test(NAME afterlight.model COMMAND afterlight_model_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/model-tests")
    set_tests_properties(afterlight.model PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/model-tests")
    set(package_test_flags)
    if(CMAKE_C_FLAGS MATCHES "sanitize")
        list(APPEND package_test_flags --sanitized)
    endif()
    add_test(NAME afterlight.project_tools COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/project_tools.py"
            --root "${CMAKE_CURRENT_SOURCE_DIR}" --player "${afterlight_player_package}" --model-test "$<TARGET_FILE:afterlight_model_test>" --fixtures "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets" ${package_test_flags})
    set_tests_properties(afterlight.project_tools PROPERTIES TIMEOUT 120 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.editor_wizard COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/editor_wizard.py" "$<TARGET_FILE:afterlight_editor>")
    set_tests_properties(afterlight.editor_wizard PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_executable(afterlight_project_test tests/project.c)
    target_include_directories(afterlight_project_test PRIVATE third_party/cjson)
    target_link_libraries(afterlight_project_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_project_test)
    add_test(NAME afterlight.project COMMAND afterlight_project_test "${CMAKE_CURRENT_BINARY_DIR}/playground.afterlight.project" "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/legacy-v4.afterlight.json")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/project-tests")
    set_tests_properties(afterlight.project PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/project-tests")
    add_executable(afterlight_audio_test tests/audio.c)
    target_link_libraries(afterlight_audio_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_audio_test)
    add_test(NAME afterlight.audio COMMAND afterlight_audio_test "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets")
    set_tests_properties(afterlight.audio PROPERTIES ENVIRONMENT "SDL_AUDIO_DRIVER=dummy")
    add_executable(afterlight_gltf_test tests/gltf.c)
    target_include_directories(afterlight_gltf_test PRIVATE third_party/cjson)
    target_link_libraries(afterlight_gltf_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_gltf_test)
    add_test(NAME afterlight.gltf COMMAND afterlight_gltf_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets" "${CMAKE_CURRENT_SOURCE_DIR}/sandbox/assets/beacon.glb")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gltf-tests")
    set_tests_properties(afterlight.gltf PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/gltf-tests")
    add_executable(afterlight_collider_data_test tests/collider_data.c)
    target_include_directories(afterlight_collider_data_test PRIVATE third_party/cjson)
    target_link_libraries(afterlight_collider_data_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_collider_data_test)
    add_test(NAME afterlight.collider_data COMMAND afterlight_collider_data_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/legacy-v4.afterlight.json")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/collider-data-tests")
    set_tests_properties(afterlight.collider_data PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/collider-data-tests")
    add_executable(afterlight_playground_test tests/playground.c)
    target_link_libraries(afterlight_playground_test PRIVATE afterlight_playground_code Afterlight::Engine)
    afterlight_warnings(afterlight_playground_test)
    add_test(NAME afterlight.playground COMMAND afterlight_playground_test "${obstacle_scene}")
    add_executable(afterlight_fixed_loop_test tests/fixed_loop.c)
    target_include_directories(afterlight_fixed_loop_test PRIVATE sandbox)
    target_link_libraries(afterlight_fixed_loop_test PRIVATE Afterlight::Engine SDL3::SDL3)
    afterlight_warnings(afterlight_fixed_loop_test)
    add_test(NAME afterlight.fixed_loop COMMAND afterlight_fixed_loop_test)
    set_tests_properties(afterlight.fixed_loop PROPERTIES TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.playground_smoke COMMAND afterlight_playground --validation --frames 90)
    set_tests_properties(afterlight.playground_smoke PROPERTIES TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    foreach(test simulation collision character)
        add_executable(afterlight_${test}_test tests/${test}.c)
        target_link_libraries(afterlight_${test}_test PRIVATE Afterlight::Engine)
        afterlight_warnings(afterlight_${test}_test)
        add_test(NAME afterlight.${test} COMMAND afterlight_${test}_test)
    endforeach()
    add_executable(afterlight_render_data_test tests/render_data.c)
    target_include_directories(afterlight_render_data_test PRIVATE third_party/cjson)
    target_link_libraries(afterlight_render_data_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_render_data_test)
    add_test(NAME afterlight.render_data COMMAND afterlight_render_data_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/legacy-v2.afterlight.json" "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/legacy-v3.afterlight.json")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/render-data-tests")
    set_tests_properties(afterlight.render_data PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/render-data-tests")
    add_executable(afterlight_render_features_test tests/render_features.c)
    target_include_directories(afterlight_render_features_test PRIVATE engine/src)
    target_link_libraries(afterlight_render_features_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_render_features_test)
    add_test(NAME afterlight.render_features COMMAND afterlight_render_features_test)
    set_tests_properties(afterlight.render_features PROPERTIES TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_executable(afterlight_mipmap_test tests/mipmap.c)
    target_include_directories(afterlight_mipmap_test PRIVATE engine/src)
    target_link_libraries(afterlight_mipmap_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_mipmap_test)
    add_test(NAME afterlight.mipmap COMMAND afterlight_mipmap_test)
    add_executable(afterlight_shadow_math_test tests/shadow_math.c)
    target_include_directories(afterlight_shadow_math_test PRIVATE engine/src)
    target_link_libraries(afterlight_shadow_math_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_shadow_math_test)
    add_test(NAME afterlight.shadow_math COMMAND afterlight_shadow_math_test)
    add_executable(afterlight_shadow_test tests/shadows.c)
    target_include_directories(afterlight_shadow_test PRIVATE engine/src sandbox)
    target_link_libraries(afterlight_shadow_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_shadow_test)
    add_test(NAME afterlight.shadows COMMAND afterlight_shadow_test)
    set_tests_properties(afterlight.shadows PROPERTIES TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_library(afterlight_game_fast MODULE game/demo_game.c)
    target_include_directories(afterlight_game_fast PRIVATE engine/include)
    target_compile_definitions(afterlight_game_fast PRIVATE AF_DEMO_TURN_SPEED=2.0f)
    if(UNIX)
        target_link_libraries(afterlight_game_fast PRIVATE m)
    endif()
    add_library(afterlight_game_bad MODULE tests/module_variant.c)
    target_include_directories(afterlight_game_bad PRIVATE engine/include)
    target_compile_definitions(afterlight_game_bad PRIVATE AF_TEST_BAD_ABI=1)
    add_library(afterlight_game_init_fail MODULE tests/module_variant.c)
    target_include_directories(afterlight_game_init_fail PRIVATE engine/include)
    add_executable(afterlight_hot_reload_test tests/hot_reload.c)
    target_link_libraries(afterlight_hot_reload_test PRIVATE afterlight_editor_document SDL3::SDL3)
    afterlight_warnings(afterlight_hot_reload_test)
    add_dependencies(afterlight_hot_reload_test afterlight_game afterlight_game_fast afterlight_game_bad afterlight_game_init_fail)
    add_test(NAME afterlight.hot_reload COMMAND afterlight_hot_reload_test "$<TARGET_FILE:afterlight_game>" "$<TARGET_FILE:afterlight_game_fast>" "$<TARGET_FILE:afterlight_game_bad>" "$<TARGET_FILE:afterlight_game_init_fail>")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hot-reload-tests")
    set_tests_properties(afterlight.hot_reload PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hot-reload-tests")
    add_library(afterlight_demo_test STATIC game/demo_game.c)
    target_include_directories(afterlight_demo_test PRIVATE engine/include)
    add_executable(afterlight_runtime_test tests/runtime.c)
    target_link_libraries(afterlight_runtime_test PRIVATE afterlight_editor_document afterlight_demo_test)
    afterlight_warnings(afterlight_runtime_test)
    add_test(NAME afterlight.runtime COMMAND afterlight_runtime_test)
    add_executable(afterlight_hierarchy_test tests/hierarchy.c)
    target_link_libraries(afterlight_hierarchy_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_hierarchy_test)
    add_test(NAME afterlight.hierarchy COMMAND afterlight_hierarchy_test)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hierarchy-tests")
    set_tests_properties(afterlight.hierarchy PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/hierarchy-tests")
    add_executable(afterlight_asset_test tests/asset.c)
    target_link_libraries(afterlight_asset_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_asset_test)
    add_test(NAME afterlight.asset COMMAND afterlight_asset_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/quad.obj")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asset-tests")
    set_tests_properties(afterlight.asset PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/asset-tests")
    add_executable(afterlight_gizmo_test tests/gizmo.c)
    target_link_libraries(afterlight_gizmo_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_gizmo_test)
    add_test(NAME afterlight.gizmo COMMAND afterlight_gizmo_test)
    add_test(NAME afterlight.editor_smoke COMMAND afterlight_editor --validation --smoke-test "${CMAKE_CURRENT_BINARY_DIR}/editor-smoke.afterlight.json")
    set_tests_properties(afterlight.editor_smoke PROPERTIES TIMEOUT 30 LABELS "graphics"
        FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.editor_playground COMMAND afterlight_editor --playground --play-controls-test --validation)
    set_tests_properties(afterlight.editor_playground PROPERTIES TIMEOUT 30 LABELS "graphics"
        FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.editor_project COMMAND afterlight_editor --project "${CMAKE_CURRENT_BINARY_DIR}/playground.afterlight.project" --play-controls-test --validation)
    set_tests_properties(afterlight.editor_project PROPERTIES TIMEOUT 30 LABELS "graphics"
        FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_executable(afterlight_editor_document_test tests/editor_document.c)
    target_link_libraries(afterlight_editor_document_test PRIVATE afterlight_editor_document)
    afterlight_warnings(afterlight_editor_document_test)
    add_test(NAME afterlight.editor_document COMMAND afterlight_editor_document_test)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/editor-document-tests")
    set_tests_properties(afterlight.editor_document PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/editor-document-tests")
    add_executable(afterlight_scene_io_test tests/scene_io.c)
    target_link_libraries(afterlight_scene_io_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_scene_io_test)
    add_test(NAME afterlight.scene_io COMMAND afterlight_scene_io_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/assets/legacy-v2.afterlight.json")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/scene-io-tests")
    set_tests_properties(afterlight.scene_io PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/scene-io-tests" TIMEOUT 15)
    add_executable(afterlight_scene_test tests/scene.c)
    target_link_libraries(afterlight_scene_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_scene_test)
    add_test(NAME afterlight.scene COMMAND afterlight_scene_test)
    add_executable(afterlight_vulkan_policy_test tests/vulkan_policy.c)
    target_include_directories(afterlight_vulkan_policy_test PRIVATE engine/src)
    target_link_libraries(afterlight_vulkan_policy_test PRIVATE Vulkan::Vulkan)
    afterlight_warnings(afterlight_vulkan_policy_test)
    add_test(NAME afterlight.vulkan_policy COMMAND afterlight_vulkan_policy_test)
    add_executable(afterlight_math_test tests/math.c)
    target_link_libraries(afterlight_math_test PRIVATE Afterlight::Engine)
    afterlight_warnings(afterlight_math_test)
    add_test(NAME afterlight.math COMMAND afterlight_math_test)
    add_executable(afterlight_smoke tests/smoke.c)
    target_link_libraries(afterlight_smoke PRIVATE Afterlight::Engine SDL3::SDL3)
    afterlight_warnings(afterlight_smoke)
    add_test(NAME afterlight.smoke COMMAND afterlight_smoke)
    set_tests_properties(afterlight.smoke PROPERTIES TIMEOUT 30 LABELS "graphics"
        FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
endif()

# Preserve ASan global checking while replacing its broken Mach-O unload hook.
# The first linked object registers cleanup first, so it executes after user
# destructors. Disable only the compiler-generated destructor to avoid duplicates.
if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-fsanitize-address-destructor=none" AF_HAS_ASAN_DESTRUCTOR_OPTION)
    if(BUILD_TESTING)
        add_library(afterlight_unload_module MODULE tests/unload_module.c)
        set_target_properties(afterlight_unload_module PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
        afterlight_warnings(afterlight_unload_module)
        add_executable(afterlight_unload_test tests/unload.c)
        target_link_libraries(afterlight_unload_test PRIVATE SDL3::SDL3)
        afterlight_warnings(afterlight_unload_test)
        add_dependencies(afterlight_unload_test afterlight_unload_module)
        add_test(NAME afterlight.module_unload COMMAND afterlight_unload_test "$<TARGET_FILE:afterlight_unload_module>")
    endif()
    if(AF_HAS_ASAN_DESTRUCTOR_OPTION)
        foreach(module afterlight_game afterlight_playground_game afterlight_game_fast afterlight_game_bad afterlight_game_init_fail afterlight_unload_module)
            if(TARGET ${module})
                get_target_property(module_sources ${module} SOURCES)
                set_property(TARGET ${module} PROPERTY SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/module_sanitizer.c" ${module_sources})
                target_compile_options(${module} PRIVATE -Xclang -fsanitize-address-destructor=none)
            endif()
        endforeach()
    endif()
endif()

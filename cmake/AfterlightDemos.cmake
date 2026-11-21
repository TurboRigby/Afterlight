# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Developer utilities and sandbox builders.
add_executable(afterlight_profile sandbox/profile.c)
target_link_libraries(afterlight_profile PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_profile)

add_executable(afterlight_animation_builder sandbox/animation_demo.c)
target_link_libraries(afterlight_animation_builder PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_animation_builder)

add_executable(afterlight_character_builder sandbox/character_demo.c)
target_link_libraries(afterlight_character_builder PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_character_builder)

add_executable(afterlight_cluster_cook sandbox/cluster_cook.c)
target_include_directories(afterlight_cluster_cook PRIVATE engine/src)
target_link_libraries(afterlight_cluster_cook PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_cluster_cook)

add_executable(afterlight_cluster_demo sandbox/cluster_demo.c)
target_include_directories(afterlight_cluster_demo PRIVATE engine/src)
target_link_libraries(afterlight_cluster_demo PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_cluster_demo)

set(cluster_wall "${PROJECT_BINARY_DIR}/cluster-wall.afgeom")
add_custom_command(OUTPUT "${cluster_wall}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${cluster_wall}"
    COMMAND afterlight_cluster_cook --wall "${cluster_wall}"
    DEPENDS afterlight_cluster_cook VERBATIM)
set(cluster_wall_stream "${PROJECT_BINARY_DIR}/cluster-wall.afpages")
add_custom_command(OUTPUT "${cluster_wall_stream}"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${cluster_wall_stream}"
    COMMAND afterlight_cluster_cook --stream --wall "${cluster_wall_stream}"
    DEPENDS afterlight_cluster_cook VERBATIM)
add_custom_target(afterlight_cluster_wall DEPENDS "${cluster_wall}" "${cluster_wall_stream}")
add_dependencies(afterlight_cluster_demo afterlight_cluster_wall)

# Hot-reloadable C gameplay modules.
afterlight_add_game_module(afterlight_game game/demo_game.c)
afterlight_add_game_module(afterlight_playground_game game/playground_game.c)
afterlight_add_game_module(afterlight_ray_shadow_game game/ray_shadow_game.c)
afterlight_add_game_module(afterlight_water_course_game game/water_course_game.c)
afterlight_add_game_module(afterlight_water_game game/water_game.c)
afterlight_add_game_module(afterlight_character_game game/character_game.c)
afterlight_add_game_module(afterlight_animation_game game/animation_game.c)
afterlight_add_game_module(afterlight_toolbox_game game/character_game.c AF_TOOLBOX_GAME=1)
afterlight_add_game_module(afterlight_physics_game game/character_game.c AF_TOOLBOX_GAME=1 AF_PHYSICS_GAME=1)
afterlight_add_game_module(afterlight_mechanical_game game/character_game.c
    AF_TOOLBOX_GAME=1 AF_PHYSICS_GAME=1 AF_MECHANICAL_GAME=1)
afterlight_add_game_module(afterlight_machinery_game game/character_game.c
    AF_TOOLBOX_GAME=1 AF_PHYSICS_GAME=1 AF_MECHANICAL_GAME=1 AF_MACHINERY_GAME=1)
afterlight_add_game_module(afterlight_compound_game game/character_game.c
    AF_TOOLBOX_GAME=1 AF_PHYSICS_GAME=1 AF_MECHANICAL_GAME=1 AF_MACHINERY_GAME=1 AF_COMPOUND_GAME=1)

set(afterlight_editor_game_modules
    afterlight_game
    afterlight_playground_game
    afterlight_ray_shadow_game
    afterlight_water_course_game
    afterlight_water_game
    afterlight_compound_game
    afterlight_machinery_game
    afterlight_mechanical_game
    afterlight_physics_game
    afterlight_toolbox_game
    afterlight_character_game
    afterlight_animation_game
)
add_dependencies(afterlight_editor ${afterlight_editor_game_modules})
target_compile_definitions(afterlight_editor PRIVATE
    AF_GAME_MODULE_PATH="$<TARGET_FILE:afterlight_game>"
    AF_PLAYGROUND_MODULE_PATH="$<TARGET_FILE:afterlight_playground_game>"
)
if(APPLE)
    foreach(mod IN LISTS afterlight_editor_game_modules)
        add_custom_command(TARGET ${mod} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "${PROJECT_BINARY_DIR}/afterlight_editor.app/Contents/Resources/game"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:${mod}>"
                "${PROJECT_BINARY_DIR}/afterlight_editor.app/Contents/Resources/game/$<TARGET_FILE_NAME:${mod}>")
    endforeach()
endif()

# Sandbox application.
add_executable(afterlight_sandbox sandbox/main.c)
target_link_libraries(afterlight_sandbox PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_sandbox)

set(sandbox_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/demo.afterlight.json")
if(APPLE)
    target_sources(afterlight_sandbox PRIVATE "${sandbox_scene}")
    target_sources(afterlight_editor PRIVATE "${sandbox_scene}")
    set_source_files_properties("${sandbox_scene}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/scenes")
    set_target_properties(afterlight_sandbox PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Sandbox"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.sandbox"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
else()
    add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/scenes/demo.afterlight.json"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sandbox_scene}" "${PROJECT_BINARY_DIR}/scenes/demo.afterlight.json"
        DEPENDS "${sandbox_scene}" VERBATIM)
    add_custom_target(afterlight_sandbox_assets DEPENDS "${PROJECT_BINARY_DIR}/scenes/demo.afterlight.json")
    add_dependencies(afterlight_sandbox afterlight_sandbox_assets)
    add_dependencies(afterlight_editor afterlight_sandbox_assets)
endif()

set(afterlight_sample_assets
    "${PROJECT_SOURCE_DIR}/sandbox/assets/compound-l.obj"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/mesh-room.obj"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/pyramid.obj"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/checker.tga"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/sphere.obj"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/studio.hdr"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/mechanism.gltf"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/character.gltf"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/cutout.tga"
    "${PROJECT_SOURCE_DIR}/sandbox/assets/locomotion.gltf"
)
if(APPLE)
    foreach(target afterlight_sandbox afterlight_editor)
        target_sources(${target} PRIVATE ${afterlight_sample_assets})
    endforeach()
    set_source_files_properties(${afterlight_sample_assets} PROPERTIES HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION "Resources/assets")
else()
    add_custom_target(afterlight_sample_assets_copy
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/sandbox/assets" "${PROJECT_BINARY_DIR}/assets")
    add_dependencies(afterlight_editor afterlight_sample_assets_copy)
    add_dependencies(afterlight_sandbox afterlight_sample_assets_copy)
endif()

# Keep dependency notices with runnable applications.
if(APPLE)
    foreach(target afterlight_sandbox afterlight_editor)
        target_sources(${target} PRIVATE third_party/stb/LICENSE third_party/cjson/LICENSE third_party/smaa/LICENSE)
    endforeach()
    set_source_files_properties(third_party/smaa/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/smaa")
    set_source_files_properties(third_party/stb/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/stb")
    set_source_files_properties(third_party/cjson/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/cjson")
else()
    add_custom_target(afterlight_license_copy
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PROJECT_BINARY_DIR}/licenses/smaa" "${PROJECT_BINARY_DIR}/licenses/stb"
            "${PROJECT_BINARY_DIR}/licenses/cjson" "${PROJECT_BINARY_DIR}/licenses/cgltf"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/third_party/smaa/LICENSE" "${PROJECT_BINARY_DIR}/licenses/smaa/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/third_party/stb/LICENSE" "${PROJECT_BINARY_DIR}/licenses/stb/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/third_party/cjson/LICENSE" "${PROJECT_BINARY_DIR}/licenses/cjson/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/third_party/cgltf/LICENSE" "${PROJECT_BINARY_DIR}/licenses/cgltf/LICENSE")
    add_dependencies(afterlight_editor afterlight_license_copy)
    add_dependencies(afterlight_sandbox afterlight_license_copy)
    add_dependencies(afterlight_player afterlight_license_copy)
endif()

add_custom_target(run COMMAND $<TARGET_FILE:afterlight_sandbox> DEPENDS afterlight_sandbox USES_TERMINAL)

add_custom_target(afterlight_prefabs_copy
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/prefabs" "${PROJECT_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sandbox/prefabs/compound-l.afterlight.prefab" "${PROJECT_BINARY_DIR}/prefabs/compound-l.afterlight.prefab"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sandbox/prefabs/physics-crate.afterlight.prefab" "${PROJECT_BINARY_DIR}/prefabs/physics-crate.afterlight.prefab"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sandbox/prefabs/crate.afterlight.prefab" "${PROJECT_BINARY_DIR}/prefabs/crate.afterlight.prefab"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sandbox/input.json" "${PROJECT_BINARY_DIR}/input.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sandbox/assets/compound-l.obj" "${PROJECT_BINARY_DIR}/assets/compound-l.obj"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/sandbox/assets/cutout.tga" "${PROJECT_BINARY_DIR}/assets/cutout.tga")
add_dependencies(afterlight_editor afterlight_prefabs_copy)

# Standalone obstacle-course playground.
add_library(afterlight_playground_code STATIC game/playground_game.c)
target_include_directories(afterlight_playground_code PRIVATE engine/include)
afterlight_warnings(afterlight_playground_code)
if(UNIX)
    target_link_libraries(afterlight_playground_code PRIVATE m)
endif()

add_executable(afterlight_playground playground/main.c)
target_include_directories(afterlight_playground PRIVATE sandbox)
target_link_libraries(afterlight_playground PRIVATE afterlight_playground_code afterlight_editor_ui)
afterlight_warnings(afterlight_playground)

set(obstacle_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/obstacle.afterlight.json")
set(afterlight_sound_assets)
foreach(sound jump land checkpoint finish)
    list(APPEND afterlight_sound_assets "${PROJECT_SOURCE_DIR}/sandbox/assets/${sound}.wav")
endforeach()
list(APPEND afterlight_sound_assets "${PROJECT_SOURCE_DIR}/sandbox/assets/beacon.glb")

if(APPLE)
    set_target_properties(afterlight_playground PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Playground"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.playground"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
    set_source_files_properties("${obstacle_scene}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/scenes")
    set_source_files_properties(${afterlight_sound_assets} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets")
    set_source_files_properties(third_party/cgltf/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/cgltf")
    target_sources(afterlight_editor PRIVATE "${obstacle_scene}" ${afterlight_sound_assets})
    foreach(target afterlight_editor afterlight_sandbox afterlight_playground)
        target_sources(${target} PRIVATE third_party/cgltf/LICENSE)
    endforeach()
    target_sources(afterlight_playground PRIVATE
        "${obstacle_scene}"
        ${afterlight_sound_assets}
        editor/assets/FiraSans-Regular.ttf
        editor/assets/OFL.txt
        third_party/nuklear/LICENSE
        third_party/stb/LICENSE
        third_party/cjson/LICENSE
        third_party/smaa/LICENSE)
else()
    add_custom_target(afterlight_obstacle_copy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${obstacle_scene}" "${PROJECT_BINARY_DIR}/scenes/obstacle.afterlight.json")
    add_dependencies(afterlight_editor afterlight_obstacle_copy)
    add_dependencies(afterlight_playground
        afterlight_obstacle_copy afterlight_license_copy afterlight_sample_assets_copy afterlight_editor_assets)
endif()
add_custom_target(run_playground COMMAND $<TARGET_FILE:afterlight_playground> DEPENDS afterlight_playground USES_TERMINAL)
add_custom_target(run_playground_editor COMMAND $<TARGET_FILE:afterlight_editor> --playground DEPENDS afterlight_editor USES_TERMINAL)

# Sample scenes, project files, and launch targets.
set(mechanism_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/mechanism.afterlight.json")
set(animations_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/animations.afterlight.json")
set(afterlight_lighting_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/lighting.afterlight.json")

if(APPLE)
    set_source_files_properties("${mechanism_scene}" "${animations_scene}" "${afterlight_lighting_scene}"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/scenes")
    target_sources(afterlight_editor PRIVATE "${mechanism_scene}" "${animations_scene}" "${afterlight_lighting_scene}")
    set_source_files_properties(
        "${PROJECT_SOURCE_DIR}/sandbox/prefabs/crate.afterlight.prefab"
        "${PROJECT_SOURCE_DIR}/sandbox/prefabs/physics-crate.afterlight.prefab"
        "${PROJECT_SOURCE_DIR}/sandbox/prefabs/compound-l.afterlight.prefab"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/prefabs")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/sandbox/input.json" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(afterlight_editor PRIVATE
        "${PROJECT_SOURCE_DIR}/sandbox/input.json"
        "${PROJECT_SOURCE_DIR}/sandbox/prefabs/crate.afterlight.prefab"
        "${PROJECT_SOURCE_DIR}/sandbox/prefabs/physics-crate.afterlight.prefab"
        "${PROJECT_SOURCE_DIR}/sandbox/prefabs/compound-l.afterlight.prefab")
else()
    add_custom_target(afterlight_animation_scene_copy
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/scenes"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${mechanism_scene}" "${PROJECT_BINARY_DIR}/scenes/mechanism.afterlight.json"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${animations_scene}" "${PROJECT_BINARY_DIR}/scenes/animations.afterlight.json")
    add_dependencies(afterlight_editor afterlight_animation_scene_copy)
endif()

set(afterlight_all_samples
    demo playground mechanism animations
    character character-course character-stress
    toolbox toolbox-course
    physics-drop physics-playground
    mesh-room mechanical-playground motor-room machinery-playground
    prop-room prop-workshop
    water-pool water-lake water-stream water-river-bend
    water-pour water-pour-lab water-course water-expedition
    ray-shadow-lab
)
foreach(sample IN LISTS afterlight_all_samples)
    if(sample STREQUAL "demo")
        set(project_scene "${sandbox_scene}")
        set(project_module afterlight_game)
    elseif(sample STREQUAL "playground")
        set(project_scene "${obstacle_scene}")
        set(project_module afterlight_playground_game)
    elseif(sample STREQUAL "mechanism")
        set(project_scene "${mechanism_scene}")
        set(project_module afterlight_animation_game)
    elseif(sample STREQUAL "animations")
        set(project_scene "${animations_scene}")
        set(project_module afterlight_animation_game)
    elseif(sample STREQUAL "ray-shadow-lab")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_ray_shadow_game)
    elseif(sample STREQUAL "water-course" OR sample STREQUAL "water-expedition")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_water_course_game)
    elseif(sample MATCHES "^water-")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_water_game)
    elseif(sample STREQUAL "prop-room" OR sample STREQUAL "prop-workshop")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_compound_game)
    elseif(sample STREQUAL "motor-room" OR sample STREQUAL "machinery-playground")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_machinery_game)
    elseif(sample STREQUAL "mesh-room" OR sample STREQUAL "mechanical-playground")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_mechanical_game)
    elseif(sample MATCHES "^physics")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_physics_game)
    elseif(sample MATCHES "^toolbox")
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_toolbox_game)
    else()
        set(project_scene "${PROJECT_SOURCE_DIR}/sandbox/scenes/${sample}.afterlight.json")
        set(project_module afterlight_character_game)
    endif()

    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/${sample}.afterlight.project" CONTENT
        "{\"format\":\"afterlight.project\",\"version\":1,\"name\":\"Afterlight ${sample}\",\"startup_scene\":\"${project_scene}\",\"game_module\":\"$<TARGET_FILE_NAME:${project_module}>\"}\n")
    if(APPLE)
        get_filename_component(scene_filename "${project_scene}" NAME)
        set(bundle_project "${PROJECT_BINARY_DIR}/generated/${sample}.afterlight.project")
        file(GENERATE OUTPUT "${bundle_project}" CONTENT
            "{\"format\":\"afterlight.project\",\"version\":1,\"name\":\"Afterlight ${sample}\",\"startup_scene\":\"scenes/${scene_filename}\",\"game_module\":\"game/$<TARGET_FILE_NAME:${project_module}>\"}\n")
        set_source_files_properties("${bundle_project}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(afterlight_editor PRIVATE "${bundle_project}")
    endif()

    if(NOT sample MATCHES "^(demo|playground|mechanism|animations)$")
        add_custom_target(run_${sample}_editor
            COMMAND $<TARGET_FILE:afterlight_editor> --project "${PROJECT_BINARY_DIR}/${sample}.afterlight.project"
            DEPENDS afterlight_editor USES_TERMINAL VERBATIM)
        add_custom_target(run_${sample}_player
            COMMAND $<TARGET_FILE:afterlight_player> --project "${PROJECT_BINARY_DIR}/${sample}.afterlight.project"
            DEPENDS afterlight_player ${project_module} afterlight_prefabs_copy USES_TERMINAL VERBATIM)
        if(APPLE)
            set_source_files_properties("${project_scene}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/scenes")
            target_sources(afterlight_editor PRIVATE "${project_scene}")
        else()
            add_custom_command(TARGET afterlight_editor POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${project_scene}" "${PROJECT_BINARY_DIR}/scenes/${sample}.afterlight.json")
        endif()
    endif()
endforeach()

target_compile_definitions(afterlight_editor PRIVATE
    AF_DEMO_PROJECT_PATH="${PROJECT_BINARY_DIR}/demo.afterlight.project"
    AF_PLAYGROUND_PROJECT_PATH="${PROJECT_BINARY_DIR}/playground.afterlight.project")

add_custom_target(run_lighting_editor
    COMMAND $<TARGET_FILE:afterlight_editor> --scene "${afterlight_lighting_scene}"
    DEPENDS afterlight_editor USES_TERMINAL VERBATIM)
add_custom_target(run_mechanism_editor
    COMMAND $<TARGET_FILE:afterlight_editor> --project "${PROJECT_BINARY_DIR}/mechanism.afterlight.project"
    DEPENDS afterlight_editor USES_TERMINAL VERBATIM)
add_custom_target(run_animation_editor
    COMMAND $<TARGET_FILE:afterlight_editor> --project "${PROJECT_BINARY_DIR}/animations.afterlight.project"
    DEPENDS afterlight_editor USES_TERMINAL VERBATIM)
add_custom_target(run_animation_player
    COMMAND $<TARGET_FILE:afterlight_player> --project "${PROJECT_BINARY_DIR}/animations.afterlight.project"
    DEPENDS afterlight_player afterlight_animation_game USES_TERMINAL VERBATIM)
add_custom_target(run_player
    COMMAND $<TARGET_FILE:afterlight_player> --project "${PROJECT_BINARY_DIR}/playground.afterlight.project"
    DEPENDS afterlight_player afterlight_playground_game USES_TERMINAL)

# Packaging, source archival, and export targets.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(cmake/AfterlightLicensing.cmake)
if(APPLE)
    set(afterlight_player_package "$<TARGET_BUNDLE_DIR:afterlight_player>")
else()
    set(afterlight_player_package "$<TARGET_FILE:afterlight_player>")
endif()
target_compile_definitions(afterlight_editor PRIVATE
    AF_PYTHON_PATH="${Python3_EXECUTABLE}"
    AF_PROJECT_TOOL_PATH="${PROJECT_SOURCE_DIR}/scripts/afterlight.py"
    AF_PLAYER_PACKAGE_PATH="${afterlight_player_package}")
add_dependencies(afterlight_editor afterlight_player)

# Keep authoring templates/tooling usable when the editor bundle is moved.
if(APPLE)
    set(afterlight_tool_root "$<TARGET_BUNDLE_CONTENT_DIR:afterlight_editor>/Resources/tools")
    target_sources(afterlight_editor PRIVATE scripts/afterlight.py game/starter_game.c)
    set_source_files_properties(scripts/afterlight.py game/starter_game.c PROPERTIES HEADER_FILE_ONLY TRUE)
    set_property(TARGET afterlight_editor APPEND PROPERTY LINK_DEPENDS
        "${PROJECT_SOURCE_DIR}/scripts/afterlight.py"
        "${PROJECT_SOURCE_DIR}/scripts/package_source.py"
        "${PROJECT_SOURCE_DIR}/game/starter_game.c"
        "${PROJECT_SOURCE_DIR}/LICENSE"
        "${PROJECT_SOURCE_DIR}/NOTICE"
        "${afterlight_source_archive}")
    add_custom_command(TARGET afterlight_editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${afterlight_tool_root}/scripts" "${afterlight_tool_root}/game"
            "${afterlight_tool_root}/engine/src/core" "${afterlight_tool_root}/sandbox/scenes"
            "$<TARGET_BUNDLE_CONTENT_DIR:afterlight_editor>/Resources/extensions"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/scripts/afterlight.py" "${afterlight_tool_root}/scripts/afterlight.py"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/scripts/package_source.py" "${afterlight_tool_root}/scripts/package_source.py"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/LICENSE" "${afterlight_tool_root}/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/game/starter_game.c" "${afterlight_tool_root}/game/starter_game.c"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/engine/src/core/module_sanitizer.c" "${afterlight_tool_root}/engine/src/core/module_sanitizer.c"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${PROJECT_SOURCE_DIR}/engine/include" "${afterlight_tool_root}/engine/include"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${sandbox_scene}" "${afterlight_tool_root}/sandbox/scenes/demo.afterlight.json"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:afterlight_player>" "${afterlight_tool_root}/afterlight_player.app" VERBATIM)
endif()

set(AFTERLIGHT_EXPORT_PROJECT "${PROJECT_BINARY_DIR}/playground.afterlight.project" CACHE FILEPATH "Saved project to export")
set(AFTERLIGHT_EXPORT_DIRECTORY "${PROJECT_BINARY_DIR}/exported-game" CACHE PATH "New directory for export")
add_custom_target(export_game
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/afterlight.py" export "${AFTERLIGHT_EXPORT_PROJECT}"
        --output "${AFTERLIGHT_EXPORT_DIRECTORY}" --player "${afterlight_player_package}"
    DEPENDS afterlight_player afterlight_playground_game USES_TERMINAL VERBATIM)

set(AFTERLIGHT_BUNDLE_OUTPUT "${PROJECT_BINARY_DIR}/afterlight_game_standalone" CACHE FILEPATH "Destination path for standalone single binary game")
add_custom_target(bundle_game
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/afterlight.py" bundle "${AFTERLIGHT_EXPORT_PROJECT}"
        --output "${AFTERLIGHT_BUNDLE_OUTPUT}" --player "${afterlight_player_package}"
    DEPENDS afterlight_player afterlight_playground_game USES_TERMINAL VERBATIM)

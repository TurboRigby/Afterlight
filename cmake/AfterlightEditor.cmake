# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_library(afterlight_editor_document STATIC
    editor/document/document.c
    editor/document/environment.c
    editor/tools/joint_gizmo.c
    editor/tools/gizmo.c
    editor/tools/animation_editor.c
    editor/runtime/runtime.c
    editor/runtime/save_game.c
    editor/content/asset_browser.c
    editor/tools/collider_view.c
    editor/tools/light_gizmo.c
)
target_include_directories(afterlight_editor_document PUBLIC editor)
target_link_libraries(afterlight_editor_document PUBLIC Afterlight::Engine SDL3::SDL3)
afterlight_warnings(afterlight_editor_document)

add_library(afterlight_nuklear STATIC editor/ui/nuklear.c)
target_include_directories(afterlight_nuklear PUBLIC third_party/nuklear editor)

add_library(afterlight_editor_ui STATIC
    editor/ui/gui.c
    editor/ui/dialog.c
    editor/ui/play_hud.c
    editor/content/thumbnail.c
    editor/tools/blueprint_editor.c
)
target_include_directories(afterlight_editor_ui PRIVATE engine/src sandbox)
target_link_libraries(afterlight_editor_ui PUBLIC afterlight_nuklear Afterlight::Engine PRIVATE SDL3::SDL3)
afterlight_warnings(afterlight_editor_ui)

add_executable(afterlight_editor
    editor/core/main.c
    editor/core/editor_state.c
    editor/ui/hierarchy.c
    editor/ui/inspector.c
    editor/tools/landscape_editor.c
    editor/ui/panels.c
    editor/ui/animation_authoring.c
    editor/content/project_tools.c
    editor/testing/smoke_tests.c
    editor/tools/viewport_overlays.c
)
target_include_directories(afterlight_editor PRIVATE engine/src sandbox)
target_link_libraries(afterlight_editor PRIVATE afterlight_editor_document afterlight_editor_ui SDL3::SDL3)
afterlight_warnings(afterlight_editor)

add_library(afterlight_player_preferences STATIC player/preferences.c)
target_include_directories(afterlight_player_preferences PUBLIC player PRIVATE third_party/cjson)
target_link_libraries(afterlight_player_preferences PUBLIC Afterlight::Engine PRIVATE afterlight_json SDL3::SDL3)
afterlight_warnings(afterlight_player_preferences)

add_executable(afterlight_player player/main.c player/bundle.c)
target_include_directories(afterlight_player PRIVATE sandbox)
target_link_libraries(afterlight_player PRIVATE afterlight_editor_document afterlight_editor_ui afterlight_player_preferences)
afterlight_warnings(afterlight_player)

if(APPLE)
    set_target_properties(afterlight_editor PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Editor"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.editor"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    target_sources(afterlight_editor PRIVATE
        editor/assets/FiraSans-Regular.ttf
        editor/assets/OFL.txt
        third_party/nuklear/LICENSE
    )
    set_source_files_properties(editor/assets/FiraSans-Regular.ttf PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/fonts")
    set_source_files_properties(editor/assets/OFL.txt PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/firasans")
    set_source_files_properties(third_party/nuklear/LICENSE PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/nuklear")

    set_target_properties(afterlight_player PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Afterlight Player"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.player"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    )
    target_sources(afterlight_player PRIVATE
        editor/assets/FiraSans-Regular.ttf
        editor/assets/OFL.txt
        third_party/nuklear/LICENSE
        third_party/stb/LICENSE
        third_party/cjson/LICENSE
        third_party/cgltf/LICENSE
        third_party/smaa/LICENSE
    )
else()
    add_custom_command(OUTPUT "${PROJECT_BINARY_DIR}/fonts/FiraSans-Regular.ttf"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/editor/assets/FiraSans-Regular.ttf"
            "${PROJECT_BINARY_DIR}/fonts/FiraSans-Regular.ttf"
        DEPENDS editor/assets/FiraSans-Regular.ttf VERBATIM)
    add_custom_command(
        OUTPUT "${PROJECT_BINARY_DIR}/licenses/firasans/OFL.txt" "${PROJECT_BINARY_DIR}/licenses/nuklear/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "${PROJECT_BINARY_DIR}/licenses/firasans" "${PROJECT_BINARY_DIR}/licenses/nuklear"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/editor/assets/OFL.txt" "${PROJECT_BINARY_DIR}/licenses/firasans/OFL.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PROJECT_SOURCE_DIR}/third_party/nuklear/LICENSE" "${PROJECT_BINARY_DIR}/licenses/nuklear/LICENSE"
        DEPENDS editor/assets/OFL.txt third_party/nuklear/LICENSE VERBATIM)
    add_custom_target(afterlight_editor_assets DEPENDS
        "${PROJECT_BINARY_DIR}/fonts/FiraSans-Regular.ttf"
        "${PROJECT_BINARY_DIR}/licenses/firasans/OFL.txt"
        "${PROJECT_BINARY_DIR}/licenses/nuklear/LICENSE")
    add_dependencies(afterlight_editor afterlight_editor_assets)
    add_dependencies(afterlight_player afterlight_editor_assets)
endif()

add_custom_target(run_editor COMMAND $<TARGET_FILE:afterlight_editor> DEPENDS afterlight_editor USES_TERMINAL)

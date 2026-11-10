# SPDX-License-Identifier: MPL-2.0
add_executable(afterlight_low_water main.c world.c)
target_include_directories(afterlight_low_water PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/src" "${PROJECT_SOURCE_DIR}/sandbox")
target_link_libraries(afterlight_low_water PRIVATE afterlight_editor_ui)
afterlight_warnings(afterlight_low_water)

set(low_water_assets
    assets/valve.glb assets/keypad.glb assets/calibration.glb assets/archive.glb assets/crown.glb
    assets/room.wav assets/relay.wav assets/carrier.wav)
set(low_water_font "${PROJECT_SOURCE_DIR}/editor/assets/FiraSans-Regular.ttf")
set(low_water_font_license "${PROJECT_SOURCE_DIR}/editor/assets/OFL.txt")
set(low_water_gui_license "${PROJECT_SOURCE_DIR}/third_party/nuklear/LICENSE")
if(APPLE)
    set_target_properties(afterlight_low_water PROPERTIES
        MACOSX_BUNDLE TRUE MACOSX_BUNDLE_BUNDLE_NAME "Low Water"
        MACOSX_BUNDLE_GUI_IDENTIFIER "dev.afterlight.low-water"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    set_source_files_properties(${low_water_assets} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/assets")
    set_source_files_properties("${low_water_font}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties("${low_water_font_license}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/firasans")
    set_source_files_properties("${low_water_gui_license}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses/nuklear")
    target_sources(afterlight_low_water PRIVATE ${low_water_assets} "${low_water_font}"
        "${low_water_font_license}" "${low_water_gui_license}")
else()
    foreach(asset IN LISTS low_water_assets)
        configure_file("${asset}" "${asset}" COPYONLY)
    endforeach()
    configure_file("${low_water_font}" "FiraSans-Regular.ttf" COPYONLY)
    configure_file("${low_water_font_license}" "licenses/firasans/OFL.txt" COPYONLY)
    configure_file("${low_water_gui_license}" "licenses/nuklear/LICENSE" COPYONLY)
endif()
add_custom_target(run_low_water COMMAND $<TARGET_FILE:afterlight_low_water>
    DEPENDS afterlight_low_water USES_TERMINAL VERBATIM)

# The Annex ships as authored scenes on the shared first-person controller.
add_library(afterlight_story_game MODULE "${PROJECT_SOURCE_DIR}/game/starter_game.c")
target_include_directories(afterlight_story_game PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")
set_target_properties(afterlight_story_game PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
afterlight_warnings(afterlight_story_game)
if(UNIX)
    target_link_libraries(afterlight_story_game PRIVATE m)
endif()
set(annex_project "${CMAKE_CURRENT_BINARY_DIR}/signal-annex.afterlight.project")
file(GENERATE OUTPUT "${annex_project}" CONTENT
    "{\"format\":\"afterlight.project\",\"version\":1,\"name\":\"Low Water - Signal Annex\",\"startup_scene\":\"${CMAKE_CURRENT_SOURCE_DIR}/annex/relay.afterlight.json\",\"game_module\":\"$<TARGET_FILE_NAME:afterlight_story_game>\"}\n")
add_custom_target(run_low_water_annex COMMAND $<TARGET_FILE:afterlight_player> --project "${annex_project}"
    DEPENDS afterlight_story_game afterlight_player USES_TERMINAL VERBATIM)
add_custom_target(run_low_water_annex_editor COMMAND $<TARGET_FILE:afterlight_editor> --project "${annex_project}"
    DEPENDS afterlight_story_game afterlight_editor USES_TERMINAL VERBATIM)

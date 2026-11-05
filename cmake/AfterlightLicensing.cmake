# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Bundle the build's corresponding covered source, not the export-time checkout.
file(GLOB_RECURSE afterlight_source_inputs CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/engine/*" "${PROJECT_SOURCE_DIR}/editor/*"
    "${PROJECT_SOURCE_DIR}/game/*" "${PROJECT_SOURCE_DIR}/player/*"
    "${PROJECT_SOURCE_DIR}/playground/*" "${PROJECT_SOURCE_DIR}/sandbox/*"
    "${PROJECT_SOURCE_DIR}/scripts/*" "${PROJECT_SOURCE_DIR}/tests/*"
    "${PROJECT_SOURCE_DIR}/docs/*" "${PROJECT_SOURCE_DIR}/cmake/*"
    "${PROJECT_SOURCE_DIR}/third_party/*")
list(FILTER afterlight_source_inputs EXCLUDE REGEX "/(__pycache__|build|[.]git|[.]cache)/")
set(afterlight_source_archive "${PROJECT_BINARY_DIR}/source/afterlight-source.zip")
add_custom_command(OUTPUT "${afterlight_source_archive}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/package_source.py"
        --root "${PROJECT_SOURCE_DIR}" --output "${afterlight_source_archive}"
    DEPENDS ${afterlight_source_inputs} CMakeLists.txt CMakePresets.json README.md
        LICENSE NOTICE THIRD_PARTY_NOTICES.md .gitignore .gitattributes
    COMMENT "Archiving Afterlight source for MPL distribution" VERBATIM)
add_custom_target(afterlight_source DEPENDS "${afterlight_source_archive}")

if(APPLE)
    set_source_files_properties(LICENSE NOTICE THIRD_PARTY_NOTICES.md
        PROPERTIES HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION "Resources")
    set_source_files_properties("${afterlight_source_archive}" PROPERTIES
        GENERATED TRUE HEADER_FILE_ONLY TRUE MACOSX_PACKAGE_LOCATION "Resources/source")
    foreach(target afterlight_sandbox afterlight_editor afterlight_playground afterlight_player)
        add_dependencies(${target} afterlight_source)
        target_sources(${target} PRIVATE LICENSE NOTICE THIRD_PARTY_NOTICES.md "${afterlight_source_archive}")
    endforeach()
else()
    add_custom_target(afterlight_source_notices
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/LICENSE" "${PROJECT_BINARY_DIR}/LICENSE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/NOTICE" "${PROJECT_BINARY_DIR}/NOTICE"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${PROJECT_BINARY_DIR}/THIRD_PARTY_NOTICES.md"
        DEPENDS afterlight_source VERBATIM)
    foreach(target afterlight_sandbox afterlight_editor afterlight_playground afterlight_player)
        add_dependencies(${target} afterlight_source_notices)
    endforeach()
endif()

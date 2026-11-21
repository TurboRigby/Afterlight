# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.24)

set(afterlight_version_header "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/afterlight/version.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${afterlight_version_header}")
foreach(component MAJOR MINOR PATCH)
    file(STRINGS "${afterlight_version_header}" version_line REGEX "^#define AF_VERSION_${component} [0-9]+$")
    string(REGEX REPLACE ".* " "" afterlight_version_${component} "${version_line}")
endforeach()

project(Afterlight
    VERSION "${afterlight_version_MAJOR}.${afterlight_version_MINOR}.${afterlight_version_PATCH}"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan 1.1 REQUIRED COMPONENTS glslangValidator)
find_package(SDL3 3.4 CONFIG REQUIRED)

include(cmake/AfterlightEngine.cmake)
include(cmake/AfterlightEditor.cmake)
add_subdirectory(games/low-water)
include(cmake/AfterlightDemos.cmake)

include(CTest)
include(cmake/AfterlightTests.cmake)

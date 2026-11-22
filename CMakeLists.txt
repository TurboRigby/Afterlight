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

option(AFTERLIGHT_ENABLE_VULKAN "Enable Vulkan rendering backend" ON)
if(APPLE)
    option(AFTERLIGHT_ENABLE_METAL "Enable native Metal rendering backend" ON)
else()
    option(AFTERLIGHT_ENABLE_METAL "Enable native Metal rendering backend" OFF)
endif()

if(NOT AFTERLIGHT_ENABLE_VULKAN AND NOT AFTERLIGHT_ENABLE_METAL)
    message(FATAL_ERROR "At least one rendering backend (AFTERLIGHT_ENABLE_VULKAN or AFTERLIGHT_ENABLE_METAL) must be enabled.")
endif()
if(AFTERLIGHT_ENABLE_METAL AND NOT APPLE)
    message(FATAL_ERROR "AFTERLIGHT_ENABLE_METAL is only supported on Apple platforms.")
endif()

if(AFTERLIGHT_ENABLE_METAL)
    enable_language(OBJC)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(AFTERLIGHT_ENABLE_VULKAN)
    find_package(Vulkan 1.1 REQUIRED COMPONENTS glslangValidator)
    set(AFTERLIGHT_GLSLANG Vulkan::glslangValidator)
else()
    find_package(Vulkan QUIET COMPONENTS glslangValidator)
    if(TARGET Vulkan::glslangValidator)
        set(AFTERLIGHT_GLSLANG Vulkan::glslangValidator)
    else()
        find_program(AFTERLIGHT_GLSLANG glslangValidator REQUIRED)
    endif()
endif()
if(AFTERLIGHT_ENABLE_METAL)
    find_program(AFTERLIGHT_XCRUN xcrun REQUIRED)
    find_path(SPIRV_CROSS_INCLUDE_DIR
        NAMES spirv_cross/spirv_cross_c.h
        PATHS /opt/homebrew/include /usr/local/include "$ENV{VULKAN_SDK}/include"
        REQUIRED
    )
    find_library(SPIRV_CROSS_C_LIBRARY
        NAMES spirv-cross-c-shared spirv-cross-c
        PATHS /opt/homebrew/lib /usr/local/lib "$ENV{VULKAN_SDK}/lib"
        REQUIRED
    )
endif()
find_package(SDL3 3.4 CONFIG REQUIRED)

include(cmake/AfterlightEngine.cmake)
include(cmake/AfterlightEditor.cmake)
add_subdirectory(games/low-water)
include(cmake/AfterlightDemos.cmake)

include(CTest)
include(cmake/AfterlightTests.cmake)

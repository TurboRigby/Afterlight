# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.24)
project(Lumina VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Locate Afterlight headers
if(NOT AFTERLIGHT_INCLUDE_DIR)
    find_path(AFTERLIGHT_INCLUDE_DIR afterlight/game_module.h
        HINTS
            "${CMAKE_CURRENT_SOURCE_DIR}/../../engine/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../afterlight-engine/engine/include"
            "/usr/local/include"
            "/opt/homebrew/include")
endif()

add_library(game MODULE src/game.c)
target_include_directories(game PRIVATE "${AFTERLIGHT_INCLUDE_DIR}")
set_target_properties(game PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)

if(NOT MSVC)
    target_compile_options(game PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIX)
    target_link_libraries(game PRIVATE m)
endif()

file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/game.afterlight.project" CONTENT
    "{\"format\":\"afterlight.project\",\"version\":1,\"name\":\"LUMINA: Deep Core\",\"startup_scene\":\"../scenes/lumina_core.afterlight.json\",\"game_module\":\"$<TARGET_FILE_NAME:game>\"}\n")

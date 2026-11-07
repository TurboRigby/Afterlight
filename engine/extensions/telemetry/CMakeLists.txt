# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.24)
project(AfterlightTelemetry LANGUAGES CXX)

add_library(afterlight_telemetry MODULE telemetry.cpp)
target_compile_features(afterlight_telemetry PRIVATE cxx_std_17)
target_compile_definitions(afterlight_telemetry PRIVATE AF_EXTENSION_BUILD)
target_include_directories(afterlight_telemetry PRIVATE ../../include)
set_target_properties(afterlight_telemetry PROPERTIES
    PREFIX ""
    CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES)

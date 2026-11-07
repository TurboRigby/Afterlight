# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.24)
project(AfterlightNGX LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    message(FATAL_ERROR "The Afterlight NGX adapter targets native Linux x86-64.")
endif()

find_package(Vulkan 1.2 REQUIRED)
find_package(SDL3 3.4 CONFIG REQUIRED)
find_package(Threads REQUIRED)
set(AFTERLIGHT_NGX_SDK "" CACHE PATH "Root of the official NVIDIA DLSS SDK")
set(ngx_linux "${AFTERLIGHT_NGX_SDK}/lib/Linux_x86_64")
file(GLOB ngx_models CONFIGURE_DEPENDS "${ngx_linux}/rel/libnvidia-ngx-dlss.so.*")
if(NOT EXISTS "${AFTERLIGHT_NGX_SDK}/include/nvsdk_ngx_helpers_vk.h" OR
   NOT EXISTS "${ngx_linux}/libnvsdk_ngx.a" OR NOT ngx_models)
    message(FATAL_ERROR "Set AFTERLIGHT_NGX_SDK to the NVIDIA DLSS SDK with its Linux release libraries.")
endif()

add_library(afterlight_ngx MODULE ngx.c)
target_compile_features(afterlight_ngx PRIVATE c_std_17)
target_compile_definitions(afterlight_ngx PRIVATE AF_EXTENSION_BUILD
    AF_NGX_RUNTIME_DIR="${ngx_linux}/rel")
target_include_directories(afterlight_ngx PRIVATE ../../include)
target_include_directories(afterlight_ngx SYSTEM PRIVATE "${AFTERLIGHT_NGX_SDK}/include")
target_link_libraries(afterlight_ngx PRIVATE "${ngx_linux}/libnvsdk_ngx.a"
    Vulkan::Vulkan SDL3::SDL3 stdc++ Threads::Threads ${CMAKE_DL_LIBS})
set_target_properties(afterlight_ngx PROPERTIES PREFIX "" C_EXTENSIONS OFF C_VISIBILITY_PRESET hidden)

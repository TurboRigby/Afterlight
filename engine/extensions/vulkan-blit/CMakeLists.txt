# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.24)
project(AfterlightVulkanBlit LANGUAGES C)

find_package(Vulkan 1.1 REQUIRED)
add_library(afterlight_vulkan_blit MODULE blit.c)
target_compile_features(afterlight_vulkan_blit PRIVATE c_std_17)
target_compile_definitions(afterlight_vulkan_blit PRIVATE AF_EXTENSION_BUILD)
target_include_directories(afterlight_vulkan_blit PRIVATE ../../include)
target_link_libraries(afterlight_vulkan_blit PRIVATE Vulkan::Vulkan)
set_target_properties(afterlight_vulkan_blit PROPERTIES
    PREFIX ""
    C_EXTENSIONS OFF
    C_VISIBILITY_PRESET hidden)

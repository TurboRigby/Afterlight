# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.24)
project(AfterlightFSR1 LANGUAGES CXX)

find_package(Vulkan 1.1 REQUIRED COMPONENTS glslangValidator)
set(AFTERLIGHT_FSR1_ROOT "" CACHE PATH "Root of the AMD FidelityFX-FSR source tree")
set(fsr_headers "${AFTERLIGHT_FSR1_ROOT}/ffx-fsr")
if(NOT EXISTS "${fsr_headers}/ffx_a.h" OR NOT EXISTS "${fsr_headers}/ffx_fsr1.h" OR
   NOT EXISTS "${AFTERLIGHT_FSR1_ROOT}/license.txt")
    message(FATAL_ERROR "Set AFTERLIGHT_FSR1_ROOT to the official AMD FidelityFX-FSR checkout.")
endif()

set(shader "${CMAKE_CURRENT_SOURCE_DIR}/fsr1.comp")
set(passes encode easu rcas)
set(shader_headers)
foreach(pass IN LISTS passes)
    list(FIND passes "${pass}" pass_index)
    set(header "${CMAKE_CURRENT_BINARY_DIR}/${pass}_spv.h")
    add_custom_command(OUTPUT "${header}"
        COMMAND Vulkan::glslangValidator --target-env vulkan1.1 --spirv-val -S comp
            "-I${fsr_headers}" "-DAF_FSR_PASS=${pass_index}" --vn "af_fsr_${pass}"
            -o "${header}" "${shader}"
        DEPENDS "${shader}" "${fsr_headers}/ffx_a.h" "${fsr_headers}/ffx_fsr1.h"
        VERBATIM)
    list(APPEND shader_headers "${header}")
endforeach()

add_library(afterlight_fsr1 MODULE fsr1.cpp ${shader_headers})
target_compile_features(afterlight_fsr1 PRIVATE cxx_std_17)
target_compile_definitions(afterlight_fsr1 PRIVATE AF_EXTENSION_BUILD)
target_include_directories(afterlight_fsr1 PRIVATE ../../include "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(afterlight_fsr1 SYSTEM PRIVATE "${fsr_headers}")
target_link_libraries(afterlight_fsr1 PRIVATE Vulkan::Vulkan)
set_target_properties(afterlight_fsr1 PROPERTIES PREFIX "" CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden)
add_custom_command(TARGET afterlight_fsr1 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AFTERLIGHT_FSR1_ROOT}/license.txt"
        "$<TARGET_FILE_DIR:afterlight_fsr1>/AMD-FSR1-LICENSE.txt"
    VERBATIM)

# SPDX-License-Identifier: MPL-2.0
cmake_minimum_required(VERSION 3.24)
project(AfterlightTemporalFSR LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "These Afterlight FSR adapters target native Linux Vulkan.")
endif()

find_package(Vulkan 1.2 REQUIRED COMPONENTS glslangValidator)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(SPIRV_DIS spirv-dis REQUIRED)

set(AFTERLIGHT_FIDELITYFX_SDK "" CACHE PATH "Root of AMD FidelityFX SDK v1.1.4")
set(sdk "${AFTERLIGHT_FIDELITYFX_SDK}/sdk")
if(NOT EXISTS "${sdk}/src/backends/vk/ffx_vk.cpp" OR
   NOT EXISTS "${sdk}/include/FidelityFX/host/ffx_fsr3upscaler.h")
    message(FATAL_ERROR "Set AFTERLIGHT_FIDELITYFX_SDK to the FidelityFX SDK v1.1.4 checkout.")
endif()

# The upstream scratch allocator can place its 32-byte-aligned context array
# at an unaligned offset. Reserve padding and align that array explicitly.
set(vulkan_backend "${sdk}/src/backends/vk/ffx_vk.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${vulkan_backend}")
file(READ "${vulkan_backend}" backend)
string(REPLACE "resourceArraySize + contextArraySize,"
    "resourceArraySize + contextArraySize + alignof(BackendContext_VK::EffectContext) - 1,"
    backend "${backend}")
string(REPLACE "backendContext->pEffectContexts = (BackendContext_VK::EffectContext*)pMem;"
    "pMem = reinterpret_cast<uint8_t*>(FFX_ALIGN_UP(reinterpret_cast<uintptr_t>(pMem), alignof(BackendContext_VK::EffectContext)));\n        backendContext->pEffectContexts = (BackendContext_VK::EffectContext*)pMem;"
    backend "${backend}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ffx_vk_linux.cpp" CONTENT "${backend}" @ONLY)

add_library(afterlight_ffx_vk OBJECT
    "${CMAKE_CURRENT_BINARY_DIR}/ffx_vk_linux.cpp"
    "${sdk}/src/shared/ffx_assert.cpp"
    "${sdk}/src/shared/ffx_breadcrumbs_list.cpp"
    "${sdk}/src/shared/ffx_message.cpp"
    "${sdk}/src/shared/ffx_object_management.cpp")
target_include_directories(afterlight_ffx_vk PUBLIC "${sdk}/include" PRIVATE
    "${sdk}/src/shared" "${sdk}/src/backends/shared")
target_link_libraries(afterlight_ffx_vk PUBLIC Vulkan::Vulkan)
target_compile_features(afterlight_ffx_vk PUBLIC cxx_std_20)
target_compile_options(afterlight_ffx_vk PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/sdk_linux.h")
set_target_properties(afterlight_ffx_vk PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)

function(add_fsr_extension version component)
    set(gpu "${sdk}/include/FidelityFX/gpu")
    set(passes "${sdk}/src/backends/vk/shaders/${component}")
    file(GLOB_RECURSE shader_sources CONFIGURE_DEPENDS "${gpu}/*.h" "${passes}/*.glsl")
    set(shader_source "${CMAKE_CURRENT_BINARY_DIR}/${component}_shaders.cpp")
    add_custom_command(OUTPUT "${shader_source}"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/compile_shaders.py"
            "${component}" "${sdk}" "${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}"
            "${SPIRV_DIS}" "${shader_source}"
        DEPENDS compile_shaders.py ${shader_sources}
        VERBATIM)

    set(target afterlight_fsr${version})
    # Linux wchar_t is wider than the Windows type used to size the SDK's
    # opaque context. Build the adapter and SDK component with the same size.
    set(host_header "${sdk}/include/FidelityFX/host/ffx_${component}.h")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${host_header}")
    file(READ "${host_header}" header)
    string(REPLACE "(FFX_SDK_DEFAULT_CONTEXT_SIZE)" "(2 * FFX_SDK_DEFAULT_CONTEXT_SIZE)" header "${header}")
    file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/FidelityFX/host/ffx_${component}.h"
        CONTENT "${header}" @ONLY)
    add_library(${target} MODULE fsr.cpp "${shader_source}"
        "${sdk}/src/components/${component}/ffx_${component}.cpp")
    target_compile_definitions(${target} PRIVATE AF_EXTENSION_BUILD AF_FSR_VERSION=${version})
    target_include_directories(${target} PRIVATE ../../include "${CMAKE_CURRENT_BINARY_DIR}/include" "${sdk}/src/shared"
        "${sdk}/src/backends/shared" "${sdk}/include/FidelityFX/gpu")
    target_compile_options(${target} PRIVATE -include "${CMAKE_CURRENT_SOURCE_DIR}/sdk_linux.h")
    target_link_libraries(${target} PRIVATE afterlight_ffx_vk)
    target_link_options(${target} PRIVATE "LINKER:--no-undefined")
    set_target_properties(${target} PROPERTIES PREFIX "" CXX_EXTENSIONS OFF CXX_VISIBILITY_PRESET hidden)
endfunction()

add_fsr_extension(2 fsr2)
add_fsr_extension(3 fsr3upscaler)
configure_file("${sdk}/LICENSE.txt" AMD-FIDELITYFX-LICENSE.txt COPYONLY)

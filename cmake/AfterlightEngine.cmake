# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Compile GLSL into embedded SPIR-V (Vulkan) and/or precompiled .metallib (Metal).
set(afterlight_shader_deps
    "${PROJECT_SOURCE_DIR}/engine/shaders/water_fog.glsl"
    "${PROJECT_SOURCE_DIR}/engine/shaders/water_ripples.glsl"
    "${PROJECT_SOURCE_DIR}/engine/shaders/water_reflection.glsl"
    "${PROJECT_SOURCE_DIR}/engine/shaders/fxaa.glsl"
    "${PROJECT_SOURCE_DIR}/engine/shaders/smaa.glsl"
    "${PROJECT_SOURCE_DIR}/engine/shaders/taa.glsl"
    "${PROJECT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl"
)

set(afterlight_shader_headers)
if(AFTERLIGHT_ENABLE_VULKAN)
    foreach(shader
        mesh.vert mesh.frag cluster.comp shadow.vert shadow.frag
        ui.vert ui.frag fullscreen.vert post.frag sky.frag gi_denoise.frag
    )
        string(REPLACE "." "_" shader_name "${shader}")
        set(shader_source "${PROJECT_SOURCE_DIR}/engine/shaders/${shader}")
        set(shader_header "${PROJECT_BINARY_DIR}/generated/${shader_name}.h")
        add_custom_command(
            OUTPUT "${shader_header}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/generated"
            COMMAND ${AFTERLIGHT_GLSLANG} --target-env vulkan1.1 --spirv-val
                --vn af_${shader_name} -o "${shader_header}" "${shader_source}"
            DEPENDS "${shader_source}" ${afterlight_shader_deps}
            COMMENT "Compiling Afterlight ${shader} shader"
            VERBATIM
        )
        list(APPEND afterlight_shader_headers "${shader_header}")
    endforeach()

    set(ray_shader_header "${PROJECT_BINARY_DIR}/generated/mesh_ray_frag.h")
    add_custom_command(
        OUTPUT "${ray_shader_header}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/generated"
        COMMAND ${AFTERLIGHT_GLSLANG} --target-env vulkan1.2 --spirv-val -DAF_RAY_QUERY=1
            --vn af_mesh_ray_frag -o "${ray_shader_header}" "${PROJECT_SOURCE_DIR}/engine/shaders/mesh.frag"
        DEPENDS "${PROJECT_SOURCE_DIR}/engine/shaders/mesh.frag" ${afterlight_shader_deps}
        COMMENT "Compiling optional ray-query material shader"
        VERBATIM
    )
    list(APPEND afterlight_shader_headers "${ray_shader_header}")
endif()

if(AFTERLIGHT_ENABLE_METAL)
    # Host SPIR-V -> MSL translator tool (reuses the runtime spv_to_msl.c module).
    add_executable(afterlight_spv2msl
        engine/src/render/metal/spv_to_msl.c
        engine/src/render/metal/spv2msl_cli.c
    )
    target_include_directories(afterlight_spv2msl PRIVATE
        engine/src/render/metal
        "${SPIRV_CROSS_INCLUDE_DIR}/spirv_cross"
    )
    target_link_libraries(afterlight_spv2msl PRIVATE "${SPIRV_CROSS_C_LIBRARY}")
    if(NOT MSVC)
        target_compile_options(afterlight_spv2msl PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    endif()

    set(afterlight_metal_dir "${PROJECT_BINARY_DIR}/generated/metal")
    set(afterlight_metal_airs)

    set(afterlight_metal_shader_specs
        "mesh.vert|mesh_vert|vert|scene|af_mesh_vert_main|vulkan1.1|"
        "mesh.frag|mesh_frag|frag|scene|af_mesh_frag_main|vulkan1.1|"
        "mesh.frag|mesh_ray_frag|frag|scene|af_mesh_ray_frag_main|vulkan1.2|-DAF_RAY_QUERY=1"
        "shadow.vert|shadow_vert|vert|scene|af_shadow_vert_main|vulkan1.1|"
        "shadow.frag|shadow_frag|frag|scene|af_shadow_frag_main|vulkan1.1|"
        "fullscreen.vert|fullscreen_vert|vert|scene|af_fullscreen_vert_main|vulkan1.1|"
        "sky.frag|sky_frag|frag|scene|af_sky_frag_main|vulkan1.1|"
        "post.frag|post_frag|frag|post|af_post_frag_main|vulkan1.1|"
        "gi_denoise.frag|gi_denoise_frag|frag|post|af_gi_denoise_frag_main|vulkan1.1|"
        "ui.vert|ui_vert|vert|post|af_ui_vert_main|vulkan1.1|"
        "ui.frag|ui_frag|frag|post|af_ui_frag_main|vulkan1.1|"
    )

    foreach(spec IN LISTS afterlight_metal_shader_specs)
        string(REPLACE "|" ";" parts "${spec}")
        list(GET parts 0 src_file)
        list(GET parts 1 out_stem)
        list(GET parts 2 stage)
        list(GET parts 3 profile)
        list(GET parts 4 entry_name)
        list(GET parts 5 target_env)
        list(GET parts 6 extra_def)

        set(glsl_src "${PROJECT_SOURCE_DIR}/engine/shaders/${src_file}")
        set(spv_out "${afterlight_metal_dir}/${out_stem}.spv")
        set(msl_out "${afterlight_metal_dir}/${out_stem}.metal")
        set(air_out "${afterlight_metal_dir}/${out_stem}.air")

        set(glsl_cmd ${AFTERLIGHT_GLSLANG} --target-env ${target_env} --spirv-val -V)
        if(NOT extra_def STREQUAL "")
            list(APPEND glsl_cmd ${extra_def})
        endif()
        list(APPEND glsl_cmd -o "${spv_out}" "${glsl_src}")

        add_custom_command(
            OUTPUT "${air_out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${afterlight_metal_dir}"
            COMMAND ${glsl_cmd}
            COMMAND $<TARGET_FILE:afterlight_spv2msl> "${spv_out}" "${msl_out}" "${stage}" "${profile}" "${entry_name}"
            COMMAND "${AFTERLIGHT_XCRUN}" -sdk macosx metal -std=metal3.0 -O2 -c "${msl_out}" -o "${air_out}"
            DEPENDS "${glsl_src}" ${afterlight_shader_deps} afterlight_spv2msl
            COMMENT "Compiling Metal shader ${out_stem}"
            VERBATIM
        )
        list(APPEND afterlight_metal_airs "${air_out}")
    endforeach()

    # Native MSL GPU cluster-culling ICB compute kernel.
    set(cluster_icb_src "${PROJECT_SOURCE_DIR}/engine/shaders/metal/cluster_icb.metal")
    set(cluster_icb_air "${afterlight_metal_dir}/cluster_icb.air")
    add_custom_command(
        OUTPUT "${cluster_icb_air}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${afterlight_metal_dir}"
        COMMAND "${AFTERLIGHT_XCRUN}" -sdk macosx metal -std=metal3.0 -O2 -c "${cluster_icb_src}" -o "${cluster_icb_air}"
        DEPENDS "${cluster_icb_src}"
        COMMENT "Compiling native Metal cluster ICB compute shader"
        VERBATIM
    )
    list(APPEND afterlight_metal_airs "${cluster_icb_air}")

    set(metal_lib_bin "${afterlight_metal_dir}/af_shaders.metallib")
    set(metal_lib_header "${PROJECT_BINARY_DIR}/generated/metal_shaders_lib.h")
    add_custom_command(
        OUTPUT "${metal_lib_header}"
        COMMAND "${AFTERLIGHT_XCRUN}" -sdk macosx metallib ${afterlight_metal_airs} -o "${metal_lib_bin}"
        COMMAND Python3::Interpreter "${PROJECT_SOURCE_DIR}/scripts/bin_to_c_header.py"
            --input "${metal_lib_bin}" --output "${metal_lib_header}" --symbol af_metal_shaders_metallib
        DEPENDS ${afterlight_metal_airs} "${PROJECT_SOURCE_DIR}/scripts/bin_to_c_header.py"
        COMMENT "Linking Afterlight Metal shader library"
        VERBATIM
    )
    list(APPEND afterlight_shader_headers "${metal_lib_header}")
endif()

# Vendored third-party C libraries.
add_library(afterlight_json STATIC third_party/cjson/cJSON.c)
target_compile_definitions(afterlight_json PRIVATE ENABLE_LOCALES=1 CJSON_NESTING_LIMIT=32 CJSON_HIDE_SYMBOLS)

add_library(afterlight_gltf STATIC engine/src/assets/cgltf.c)
target_include_directories(afterlight_gltf PRIVATE third_party/cgltf)

add_library(afterlight_image STATIC engine/src/assets/image.c)
target_include_directories(afterlight_image PRIVATE third_party/stb)

function(afterlight_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wpedantic -Wshadow -Wconversion>
            $<$<COMPILE_LANGUAGE:OBJC>:-Wall -Wextra -Wshadow -Wconversion -fno-objc-arc>
        )
    endif()
endfunction()

function(afterlight_add_game_module target source)
    add_library(${target} MODULE "${source}")
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/engine/include")
    set_target_properties(${target} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
    afterlight_warnings(${target})
    if(UNIX)
        target_link_libraries(${target} PRIVATE m)
    endif()
    if(ARGN)
        target_compile_definitions(${target} PRIVATE ${ARGN})
    endif()
endfunction()

add_library(afterlight STATIC
    ${afterlight_shader_headers}
    # Core
    engine/src/core/camera.c
    engine/src/core/engine.c
    engine/src/core/extension_runtime.c
    engine/src/core/input.c
    engine/src/core/jobs.c
    engine/src/core/math.c
    engine/src/core/simulation.c
    # Assets
    engine/src/assets/asset.c
    engine/src/assets/asset_watch.c
    engine/src/assets/gltf.c
    engine/src/assets/mipmap.c
    # Audio
    engine/src/audio/audio.c
    engine/src/audio/audio_codec.c
    # Animation
    engine/src/animation/animation.c
    engine/src/animation/animation_asset.c
    engine/src/animation/animation_edit.c
    engine/src/animation/animation_events.c
    engine/src/animation/animation_player.c
    engine/src/animation/animation_runtime.c
    engine/src/animation/rig.c
    # Geometry
    engine/src/geometry/cluster.c
    engine/src/geometry/cluster_lod.c
    engine/src/geometry/cluster_stream.c
    # Scene
    engine/src/scene/landscape.c
    engine/src/scene/landscape_io.c
    engine/src/scene/prefab.c
    engine/src/scene/project.c
    engine/src/scene/scene.c
    engine/src/scene/scene_io.c
    engine/src/scene/wind.c
    # Scripting & Story
    engine/src/scripting/blueprint.c
    engine/src/scripting/blueprint_io.c
    engine/src/scripting/blueprint_vm.c
    engine/src/scripting/story.c
    engine/src/scripting/timeline.c
    # Gameplay
    engine/src/gameplay/character.c
    engine/src/gameplay/game_host.c
    engine/src/gameplay/gameplay.c
    engine/src/gameplay/locomotion.c
    engine/src/gameplay/locomotion_player.c
    engine/src/gameplay/swim.c
    engine/src/gameplay/third_person.c
    # Physics
    engine/src/physics/body.c
    engine/src/physics/collision.c
    engine/src/physics/collision_mesh.c
    engine/src/physics/compound.c
    engine/src/physics/joint.c
    engine/src/physics/physics.c
    engine/src/physics/physics_bounds.c
    engine/src/physics/physics_ccd.c
    engine/src/physics/physics_compound.c
    engine/src/physics/physics_contacts.c
    engine/src/physics/physics_interaction.c
    engine/src/physics/physics_joints.c
    engine/src/physics/physics_mesh.c
    engine/src/physics/physics_sleep.c
    engine/src/physics/physics_solver.c
    engine/src/physics/physics_water.c
    engine/src/physics/physics_wind.c
    engine/src/physics/triangle_collision.c
    # Water
    engine/src/water/water.c
    engine/src/water/water_io.c
    engine/src/water/water_particle_mesh.c
    engine/src/water/water_particles.c
    engine/src/water/water_river.c
    engine/src/water/water_runtime.c
    # Renderer
    engine/src/render/environment_job.c
    engine/src/render/ibl.c
    engine/src/render/local_shadow.c
    engine/src/render/renderer_common.c
    engine/src/render/renderer_dispatch.c
    engine/src/render/shadow_math.c
)
add_library(Afterlight::Engine ALIAS afterlight)
target_include_directories(afterlight
    PUBLIC engine/include
    PRIVATE engine/src third_party/cjson third_party/stb third_party/cgltf third_party/smaa "${PROJECT_BINARY_DIR}/generated"
)
target_link_libraries(afterlight PRIVATE SDL3::SDL3 afterlight_json afterlight_image afterlight_gltf)
if(AFTERLIGHT_ENABLE_VULKAN)
    target_sources(afterlight PRIVATE
        engine/src/render/post_vulkan.c
        engine/src/render/ray_query_vulkan.c
        engine/src/render/refraction_vulkan.c
        engine/src/render/renderer_assets_vulkan.c
        engine/src/render/renderer_cluster_vulkan.c
        engine/src/render/renderer_device_vulkan.c
        engine/src/render/renderer_mesh_vulkan.c
        engine/src/render/renderer_vulkan.c
        engine/src/render/shadow_vulkan.c
        engine/src/render/texture_vulkan.c
        engine/src/render/ui_vulkan.c
        engine/src/render/vk_memory.c
    )
    target_link_libraries(afterlight PRIVATE Vulkan::Vulkan)
    target_compile_definitions(afterlight PRIVATE AF_ENABLE_VULKAN=1)
endif()
if(AFTERLIGHT_ENABLE_METAL)
    target_sources(afterlight PRIVATE
        engine/src/render/metal/spv_to_msl.c
        engine/src/render/metal/texture_metal.m
        engine/src/render/metal/shadow_metal.m
        engine/src/render/metal/refraction_metal.m
        engine/src/render/metal/ui_metal.m
        engine/src/render/metal/post_metal.m
        engine/src/render/metal/ray_query_metal.m
        engine/src/render/metal/upscaler_metal.m
        engine/src/render/metal/renderer_mesh_metal.m
        engine/src/render/metal/renderer_cluster_metal.m
        engine/src/render/metal/renderer_assets_metal.m
        engine/src/render/metal/renderer_device_metal.m
        engine/src/render/metal/renderer_metal.m
    )
    target_include_directories(afterlight PRIVATE "${SPIRV_CROSS_INCLUDE_DIR}/spirv_cross")
    target_link_libraries(afterlight PRIVATE "${SPIRV_CROSS_C_LIBRARY}")
    target_link_options(afterlight PUBLIC
        "SHELL:-framework Metal"
        "SHELL:-framework QuartzCore"
        "SHELL:-framework Foundation"
        "SHELL:-weak_framework MetalFX"
    )
    target_compile_definitions(afterlight PRIVATE AF_ENABLE_METAL=1)
endif()
if(UNIX)
    target_link_libraries(afterlight PUBLIC m)
endif()
target_compile_definitions(afterlight PRIVATE $<$<CONFIG:Debug>:AF_DEBUG=1>)
afterlight_warnings(afterlight)

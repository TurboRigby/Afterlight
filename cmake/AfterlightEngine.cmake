# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Compile GLSL into embedded SPIR-V; no runtime paths or shader compiler needed.
set(afterlight_shader_headers)
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
        COMMAND Vulkan::glslangValidator --target-env vulkan1.1 --spirv-val
            --vn af_${shader_name} -o "${shader_header}" "${shader_source}"
        DEPENDS
            "${shader_source}"
            "${PROJECT_SOURCE_DIR}/engine/shaders/water_fog.glsl"
            "${PROJECT_SOURCE_DIR}/engine/shaders/water_ripples.glsl"
            "${PROJECT_SOURCE_DIR}/engine/shaders/fxaa.glsl"
            "${PROJECT_SOURCE_DIR}/engine/shaders/smaa.glsl"
            "${PROJECT_SOURCE_DIR}/engine/shaders/taa.glsl"
            "${PROJECT_SOURCE_DIR}/third_party/smaa/SMAA.hlsl"
            Vulkan::glslangValidator
        COMMENT "Compiling Afterlight ${shader} shader"
        VERBATIM
    )
    list(APPEND afterlight_shader_headers "${shader_header}")
endforeach()

set(ray_shader_header "${PROJECT_BINARY_DIR}/generated/mesh_ray_frag.h")
add_custom_command(
    OUTPUT "${ray_shader_header}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/generated"
    COMMAND Vulkan::glslangValidator --target-env vulkan1.2 --spirv-val -DAF_RAY_QUERY=1
        --vn af_mesh_ray_frag -o "${ray_shader_header}" "${PROJECT_SOURCE_DIR}/engine/shaders/mesh.frag"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/engine/shaders/mesh.frag"
        "${PROJECT_SOURCE_DIR}/engine/shaders/water_fog.glsl"
        "${PROJECT_SOURCE_DIR}/engine/shaders/water_ripples.glsl"
        "${PROJECT_SOURCE_DIR}/engine/shaders/water_reflection.glsl"
        Vulkan::glslangValidator
    COMMENT "Compiling optional ray-query material shader"
    VERBATIM
)
list(APPEND afterlight_shader_headers "${ray_shader_header}")

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
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
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
    engine/src/render/post_vulkan.c
    engine/src/render/ray_query_vulkan.c
    engine/src/render/refraction_vulkan.c
    engine/src/render/renderer_assets_vulkan.c
    engine/src/render/renderer_cluster_vulkan.c
    engine/src/render/renderer_device_vulkan.c
    engine/src/render/renderer_mesh_vulkan.c
    engine/src/render/renderer_vulkan.c
    engine/src/render/shadow_math.c
    engine/src/render/shadow_vulkan.c
    engine/src/render/texture_vulkan.c
    engine/src/render/ui_vulkan.c
    engine/src/render/vk_memory.c
)
add_library(Afterlight::Engine ALIAS afterlight)
target_include_directories(afterlight
    PUBLIC engine/include
    PRIVATE engine/src third_party/cjson third_party/stb third_party/cgltf "${PROJECT_BINARY_DIR}/generated"
)
target_link_libraries(afterlight PRIVATE SDL3::SDL3 Vulkan::Vulkan afterlight_json afterlight_image afterlight_gltf)
if(UNIX)
    target_link_libraries(afterlight PUBLIC m)
endif()
target_compile_definitions(afterlight PRIVATE $<$<CONFIG:Debug>:AF_DEBUG=1>)
afterlight_warnings(afterlight)

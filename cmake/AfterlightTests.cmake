# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Standalone benchmarks (always built).
add_executable(afterlight_water_render_benchmark tests/water/water_render_benchmark.c)
target_include_directories(afterlight_water_render_benchmark PRIVATE sandbox)
target_link_libraries(afterlight_water_render_benchmark PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_water_render_benchmark)

add_executable(afterlight_water_benchmark tests/water/water_benchmark.c)
target_include_directories(afterlight_water_benchmark PRIVATE engine/src)
target_link_libraries(afterlight_water_benchmark PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_water_benchmark)

add_executable(afterlight_physics_benchmark tests/physics/physics_stress.c)
target_compile_definitions(afterlight_physics_benchmark PRIVATE AF_PHYSICS_BENCHMARK=1)
target_link_libraries(afterlight_physics_benchmark PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_physics_benchmark)

add_executable(afterlight_renderer_benchmark tests/render/renderer_benchmark.c)
target_include_directories(afterlight_renderer_benchmark PRIVATE engine/src sandbox)
target_link_libraries(afterlight_renderer_benchmark PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_renderer_benchmark)

add_executable(afterlight_ray_shadow_benchmark tests/render/lighting/ray_shadows.c)
target_include_directories(afterlight_ray_shadow_benchmark PRIVATE engine/src sandbox)
target_link_libraries(afterlight_ray_shadow_benchmark PRIVATE Afterlight::Engine)
afterlight_warnings(afterlight_ray_shadow_benchmark)

# Helper to register a C test executable with standard warnings and optional CTest properties.
function(afterlight_add_test target_name test_name)
    cmake_parse_arguments(AF_TEST
        "GRAPHICS;CHECK_VALIDATION;DUMMY_AUDIO;NO_CTEST"
        "TIMEOUT;WORKDIR"
        "SOURCES;INCLUDES;LIBS;DEPENDS;ARGS;ENV"
        ${ARGN}
    )
    add_executable(${target_name} ${AF_TEST_SOURCES})
    if(AF_TEST_INCLUDES)
        target_include_directories(${target_name} PRIVATE ${AF_TEST_INCLUDES})
    endif()
    if(AF_TEST_LIBS)
        target_link_libraries(${target_name} PRIVATE ${AF_TEST_LIBS})
    else()
        target_link_libraries(${target_name} PRIVATE Afterlight::Engine)
    endif()
    afterlight_warnings(${target_name})
    if(AF_TEST_DEPENDS)
        add_dependencies(${target_name} ${AF_TEST_DEPENDS})
    endif()
    if(AF_TEST_WORKDIR)
        file(MAKE_DIRECTORY "${AF_TEST_WORKDIR}")
    endif()
    if(NOT AF_TEST_NO_CTEST)
        add_test(NAME ${test_name} COMMAND ${target_name} ${AF_TEST_ARGS})
        set(env_list ${AF_TEST_ENV})
        if(AF_TEST_GRAPHICS)
            set_tests_properties(${test_name} PROPERTIES LABELS "graphics")
            list(APPEND env_list "VK_LAYER_VALIDATE_SYNC=1")
        endif()
        if(AF_TEST_DUMMY_AUDIO)
            list(APPEND env_list "SDL_AUDIO_DRIVER=dummy")
        endif()
        if(AF_TEST_CHECK_VALIDATION)
            set_tests_properties(${test_name} PROPERTIES FAIL_REGULAR_EXPRESSION "\\[validation error\\]")
        endif()
        if(AF_TEST_TIMEOUT)
            set_tests_properties(${test_name} PROPERTIES TIMEOUT ${AF_TEST_TIMEOUT})
        endif()
        if(AF_TEST_WORKDIR)
            set_tests_properties(${test_name} PROPERTIES WORKING_DIRECTORY "${AF_TEST_WORKDIR}")
        endif()
        if(env_list)
            set_tests_properties(${test_name} PROPERTIES ENVIRONMENT "${env_list}")
        endif()
    endif()
endfunction()

if(BUILD_TESTING)
    # Story & Low Water
    afterlight_add_test(afterlight_story_runtime_test afterlight.story_runtime
        SOURCES tests/scripting/story_runtime.c
        INCLUDES sandbox
        LIBS afterlight_editor_document afterlight_editor_ui SDL3::SDL3
        DEPENDS afterlight_story_game
        ARGS "${PROJECT_BINARY_DIR}/games/low-water/signal-annex.afterlight.project"
             "${PROJECT_BINARY_DIR}/signal-annex-test.save"
        GRAPHICS DUMMY_AUDIO TIMEOUT 60)
    add_test(NAME afterlight.low_water_story COMMAND afterlight_low_water --raster --verify)
    set_tests_properties(afterlight.low_water_story PROPERTIES
        TIMEOUT 90 LABELS graphics ENVIRONMENT "SDL_AUDIO_DRIVER=dummy;VK_LAYER_VALIDATE_SYNC=1")

    # Clustered geometry & streaming
    afterlight_add_test(afterlight_cluster_lod_test afterlight.cluster_lod_boundaries
        SOURCES tests/render/cluster_lod.c INCLUDES engine/src
        DEPENDS afterlight_cluster_wall ARGS "${cluster_wall}")
    add_test(NAME afterlight.cluster_geometry COMMAND afterlight_cluster_demo "${cluster_wall}" --verify)
    add_test(NAME afterlight.cluster_streaming COMMAND afterlight_cluster_demo "${cluster_wall_stream}" --verify-stream)
    set_tests_properties(afterlight.cluster_geometry afterlight.cluster_streaming PROPERTIES
        TIMEOUT 45 LABELS graphics ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")

    # Vulkan extensions & ray tracing
    add_library(afterlight_upscaler_fixture MODULE tests/render/vulkan/vulkan_upscaler_module.c)
    target_include_directories(afterlight_upscaler_fixture PRIVATE engine/include)
    target_compile_definitions(afterlight_upscaler_fixture PRIVATE AF_EXTENSION_BUILD)
    target_link_libraries(afterlight_upscaler_fixture PRIVATE Vulkan::Vulkan)
    set_target_properties(afterlight_upscaler_fixture PROPERTIES C_VISIBILITY_PRESET hidden)
    afterlight_warnings(afterlight_upscaler_fixture)

    afterlight_add_test(afterlight_vulkan_upscaler_test afterlight.vulkan_upscaler
        SOURCES tests/render/vulkan/vulkan_upscaler.c INCLUDES engine/src
        LIBS Afterlight::Engine Vulkan::Vulkan DEPENDS afterlight_upscaler_fixture
        ARGS $<TARGET_FILE:afterlight_upscaler_fixture> GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    set_tests_properties(afterlight.vulkan_upscaler PROPERTIES SKIP_RETURN_CODE 77)

    set(test_custom_material_header "${PROJECT_BINARY_DIR}/generated/test_custom_material_frag.h")
    add_custom_command(
        OUTPUT "${test_custom_material_header}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/generated"
        COMMAND Vulkan::glslangValidator --target-env vulkan1.1 --spirv-val
            --vn af_test_custom_material_frag -o "${test_custom_material_header}"
            "${PROJECT_SOURCE_DIR}/tests/render/vulkan/custom_material.frag"
        DEPENDS "${PROJECT_SOURCE_DIR}/tests/render/vulkan/custom_material.frag" Vulkan::glslangValidator
        COMMENT "Compiling custom material test shader"
        VERBATIM
    )
    afterlight_add_test(afterlight_vk_memory_and_materials_test afterlight.vk_memory_and_materials
        SOURCES tests/render/vulkan/vk_memory_and_materials.c "${test_custom_material_header}"
        INCLUDES "${PROJECT_BINARY_DIR}/generated" engine/src tests
        GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    add_test(NAME afterlight.vk_memory_staged COMMAND afterlight_vk_memory_and_materials_test)
    set_tests_properties(afterlight.vk_memory_staged PROPERTIES
        TIMEOUT 45 LABELS graphics FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;AFTERLIGHT_FORCE_STAGING_UPLOAD=1")

    afterlight_add_test(afterlight_ray_shadows_test afterlight.ray_shadows
        SOURCES tests/render/lighting/ray_shadows.c INCLUDES engine/src sandbox NO_CTEST)
    foreach(variant ray_shadows ray_fallback ray_allocation_failure ray_pipeline_fallback)
        add_test(NAME afterlight.${variant} COMMAND afterlight_ray_shadows_test)
        set_tests_properties(afterlight.${variant} PROPERTIES
            TIMEOUT 90 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    endforeach()
    set_property(TEST afterlight.ray_fallback APPEND PROPERTY ENVIRONMENT "AFTERLIGHT_DISABLE_RAY_QUERY=1")
    set_property(TEST afterlight.ray_allocation_failure APPEND PROPERTY ENVIRONMENT "AFTERLIGHT_TEST_RAY_FAIL_ALLOC=2")
    set_property(TEST afterlight.ray_pipeline_fallback APPEND PROPERTY ENVIRONMENT "AFTERLIGHT_TEST_RAY_PIPELINE_FAIL=1")

    afterlight_add_test(afterlight_water_reflection_test afterlight.water_reflection
        SOURCES tests/water/water_reflection.c INCLUDES engine/src GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    afterlight_add_test(afterlight_ray_gi_test afterlight.ray_gi
        SOURCES tests/render/lighting/ray_gi.c INCLUDES engine/src GRAPHICS CHECK_VALIDATION TIMEOUT 45)

    afterlight_add_test(afterlight_water_shadows_test afterlight.water_shadows
        SOURCES tests/water/water_shadows.c INCLUDES engine/src sandbox GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    add_test(NAME afterlight.water_shadows_ray COMMAND afterlight_water_shadows_test)
    set_tests_properties(afterlight.water_shadows_ray PROPERTIES
        TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;AFTERLIGHT_TEST_RAY_WATER=1")

    add_test(NAME afterlight.ray_lab COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/ray-shadow-lab.afterlight.project" --frames 120 --validation)
    set_tests_properties(afterlight.ray_lab PROPERTIES
        TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")

    # Rendering foundations, AA, refraction, lighting & allocation failure
    afterlight_add_test(afterlight_allocation_failure_test afterlight.allocation_failure_1
        SOURCES tests/render/allocation_failure.c LIBS Afterlight::Engine SDL3::SDL3 NO_CTEST)
    foreach(at 1 2)
        add_test(NAME afterlight.allocation_failure_${at} COMMAND afterlight_allocation_failure_test)
        set_tests_properties(afterlight.allocation_failure_${at} PROPERTIES
            TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
            ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;AFTERLIGHT_TEST_POST_FAIL_AT=${at}")
    endforeach()

    afterlight_add_test(afterlight_render_foundations_test afterlight.render_foundations
        SOURCES tests/render/render_foundations.c INCLUDES engine/src GRAPHICS TIMEOUT 60)
    add_test(NAME afterlight.visibility_culling COMMAND afterlight_renderer_benchmark 1000)
    add_test(NAME afterlight.prop_instancing COMMAND afterlight_renderer_benchmark 1023 --props)
    set_tests_properties(afterlight.visibility_culling afterlight.prop_instancing PROPERTIES
        TIMEOUT 60 LABELS "graphics" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")

    afterlight_add_test(afterlight_motion_vectors_test afterlight.motion_vectors
        SOURCES tests/render/motion_vectors.c INCLUDES engine/src GRAPHICS TIMEOUT 30)
    afterlight_add_test(afterlight_temporal_antialiasing_test afterlight.temporal_antialiasing
        SOURCES tests/render/temporal_antialiasing.c INCLUDES engine/src GRAPHICS TIMEOUT 60)
    afterlight_add_test(afterlight_antialiasing_test afterlight.antialiasing
        SOURCES tests/render/antialiasing.c INCLUDES engine/src GRAPHICS TIMEOUT 60)
    afterlight_add_test(afterlight_refraction_test afterlight.refraction
        SOURCES tests/render/refraction.c INCLUDES engine/src GRAPHICS TIMEOUT 40)
    afterlight_add_test(afterlight_underwater_test afterlight.underwater
        SOURCES tests/render/underwater.c INCLUDES engine/src GRAPHICS TIMEOUT 40)
    afterlight_add_test(afterlight_alpha_test afterlight.alpha
        SOURCES tests/render/alpha.c INCLUDES engine/src GRAPHICS CHECK_VALIDATION TIMEOUT 60)
    afterlight_add_test(afterlight_render_features_test afterlight.render_features
        SOURCES tests/render/render_features.c INCLUDES engine/src GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    afterlight_add_test(afterlight_material_facing_test afterlight.material_facing
        SOURCES tests/render/material_facing.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/material-facing.gltf" GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    afterlight_add_test(afterlight_render_data_test afterlight.render_data
        SOURCES tests/render/render_data.c INCLUDES third_party/cjson LIBS afterlight_editor_document
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/legacy-v2.afterlight.json" "${PROJECT_SOURCE_DIR}/tests/assets/legacy-v3.afterlight.json"
        WORKDIR "${PROJECT_BINARY_DIR}/render-data-tests")
    afterlight_add_test(afterlight_mipmap_test afterlight.mipmap
        SOURCES tests/render/lighting/mipmap.c INCLUDES engine/src)
    afterlight_add_test(afterlight_shadow_math_test afterlight.shadow_math
        SOURCES tests/render/lighting/shadow_math.c INCLUDES engine/src)
    afterlight_add_test(afterlight_shadow_test afterlight.shadows
        SOURCES tests/render/lighting/shadows.c INCLUDES engine/src sandbox GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    afterlight_add_test(afterlight_shadow_budget_test afterlight.shadow_budget
        SOURCES tests/render/lighting/shadow_budget.c INCLUDES engine/src)
    afterlight_add_test(afterlight_environment_job_test afterlight.environment_jobs
        SOURCES tests/render/lighting/environment_jobs.c INCLUDES engine/src LIBS Afterlight::Engine SDL3::SDL3
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/panorama.hdr" TIMEOUT 30)
    afterlight_add_test(afterlight_environment_test afterlight.environments
        SOURCES tests/render/lighting/environments.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/panorama.hdr" GRAPHICS CHECK_VALIDATION TIMEOUT 60)
    afterlight_add_test(afterlight_hdr_test afterlight.hdr
        SOURCES tests/render/lighting/hdr.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets" WORKDIR "${PROJECT_BINARY_DIR}/hdr-tests" TIMEOUT 30)
    afterlight_add_test(afterlight_lighting_math_test afterlight.lighting_math
        SOURCES tests/render/lighting/lighting_math.c INCLUDES engine/src TIMEOUT 30)
    afterlight_add_test(afterlight_lighting_test afterlight.lighting
        SOURCES tests/render/lighting/lighting.c INCLUDES engine/src sandbox GRAPHICS CHECK_VALIDATION TIMEOUT 60)
    afterlight_add_test(afterlight_vulkan_policy_test afterlight.vulkan_policy
        SOURCES tests/render/vulkan/vulkan_policy.c INCLUDES engine/src LIBS Vulkan::Vulkan)

    # Water tests
    afterlight_add_test(afterlight_water_render_test afterlight.water_render
        SOURCES tests/water/water_render.c INCLUDES engine/src GRAPHICS TIMEOUT 40)
    add_test(NAME afterlight.water_ray_shadows COMMAND afterlight_water_render_test)
    set_tests_properties(afterlight.water_ray_shadows PROPERTIES
        TIMEOUT 60 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;AFTERLIGHT_TEST_RAY_WATER=1;AFTERLIGHT_TEST_AA=taa")
    foreach(optics water_render refraction underwater)
        foreach(aa fxaa smaa taa)
            add_test(NAME afterlight.${optics}_${aa} COMMAND afterlight_${optics}_test)
            set_tests_properties(afterlight.${optics}_${aa} PROPERTIES
                TIMEOUT 60 LABELS "graphics" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;AFTERLIGHT_TEST_AA=${aa}")
        endforeach()
    endforeach()

    afterlight_add_test(afterlight_water_contact_render_test afterlight.water_contact_render
        SOURCES tests/water/water_contact_render.c INCLUDES engine/src sandbox GRAPHICS TIMEOUT 40)
    add_test(NAME afterlight.water_contact_render_taa COMMAND afterlight_water_contact_render_test)
    set_tests_properties(afterlight.water_contact_render_taa PROPERTIES
        TIMEOUT 60 LABELS "graphics" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;AFTERLIGHT_TEST_AA=taa")

    afterlight_add_test(afterlight_water_volume_test afterlight.water_volume
        SOURCES tests/water/water_volume.c INCLUDES engine/src)
    afterlight_add_test(afterlight_water_course_scene_test afterlight.water_course_scene
        SOURCES tests/water/water_course_scene.c
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-course.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-expedition.afterlight.json")
    afterlight_add_test(afterlight_water_course_test afterlight.water_course
        SOURCES tests/water/water_course.c editor/runtime/runtime.c
        INCLUDES editor sandbox LIBS Afterlight::Engine SDL3::SDL3 DEPENDS afterlight_water_course_game
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-course.afterlight.json"
             "$<TARGET_FILE:afterlight_water_course_game>" "${PROJECT_BINARY_DIR}/water-course.afterlight.project"
        GRAPHICS TIMEOUT 180)
    afterlight_add_test(afterlight_swimming_test afterlight.swimming SOURCES tests/water/swimming.c)
    afterlight_add_test(afterlight_water_boundaries_test afterlight.water_boundaries SOURCES tests/water/water_boundaries.c)
    afterlight_add_test(afterlight_water_sleep_test afterlight.water_sleep
        SOURCES tests/water/water_sleep.c INCLUDES engine/src)
    afterlight_add_test(afterlight_water_coupling_test afterlight.water_coupling
        SOURCES tests/water/water_coupling.c INCLUDES engine/src)
    afterlight_add_test(afterlight_water_demo_test afterlight.water_demo
        SOURCES tests/water/water_demo.c editor/runtime/runtime.c
        INCLUDES editor sandbox LIBS Afterlight::Engine SDL3::SDL3 DEPENDS afterlight_water_game
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes" "$<TARGET_FILE:afterlight_water_game>" "${PROJECT_BINARY_DIR}/water-pool.afterlight.project"
        GRAPHICS TIMEOUT 90)
    afterlight_add_test(afterlight_water_particle_limits_test afterlight.water_particle_limits
        SOURCES tests/water/water_particle_limits.c INCLUDES engine/src)
    afterlight_add_test(afterlight_water_particles_test afterlight.water_particles
        SOURCES tests/water/water_particles.c INCLUDES engine/src)
    afterlight_add_test(afterlight_water_channel_test afterlight.water_channel
        SOURCES tests/water/water_channel.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-stream.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-river-bend.afterlight.json")
    afterlight_add_test(afterlight_water_river_test afterlight.water_river
        SOURCES tests/water/water_river.c INCLUDES engine/src)
    afterlight_add_test(afterlight_water_scenes_test afterlight.water_scenes
        SOURCES tests/water/water_scenes.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-pool.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-lake.afterlight.json")
    afterlight_add_test(afterlight_water_data_test afterlight.water_data
        SOURCES tests/water/water_data.c editor/document/document.c INCLUDES editor engine/src)
    afterlight_add_test(afterlight_water_pool_test afterlight.water_pool
        SOURCES tests/water/water_pool.c INCLUDES engine/src)
    afterlight_add_test(afterlight_water_progress_test afterlight.water_progress
        SOURCES tests/water/water_progress.c LIBS afterlight_editor_document DEPENDS afterlight_water_course_game
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/water-expedition.afterlight.json"
             "$<TARGET_FILE:afterlight_water_course_game>")

    # Physics, compound shapes, and joints
    afterlight_add_test(afterlight_compound_scene_test afterlight.compound_scene
        SOURCES tests/physics/compound/compound_scene.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/prop-room.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/prop-workshop.afterlight.json")
    afterlight_add_test(afterlight_compound_prefab_test afterlight.compound_prefab
        SOURCES tests/physics/compound/compound_prefab.c LIBS afterlight_editor_document)
    afterlight_add_test(afterlight_compound_editor_test afterlight.compound_editor
        SOURCES tests/physics/compound/compound_editor.c LIBS afterlight_editor_document)
    foreach(t compound_ccd compound_contacts compound_mass compound_queries compound_data)
        afterlight_add_test(afterlight_${t}_test afterlight.${t}
            SOURCES tests/physics/compound/${t}.c INCLUDES engine/src)
    endforeach()

    afterlight_add_test(afterlight_motor_scene_test afterlight.motor_scene
        SOURCES tests/physics/joints/motor_scene.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/motor-room.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/machinery-playground.afterlight.json")
    afterlight_add_test(afterlight_joint_gizmo_test afterlight.joint_gizmo
        SOURCES tests/physics/joints/joint_gizmo.c LIBS afterlight_editor_document)
    afterlight_add_test(afterlight_joint_editor_test afterlight.joint_editor
        SOURCES tests/physics/joints/joint_editor.c LIBS afterlight_editor_document)
    afterlight_add_test(afterlight_mechanical_scene_test afterlight.mechanical_scene
        SOURCES tests/physics/joints/mechanical_scene.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/mesh-room.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/mechanical-playground.afterlight.json")
    foreach(t hinge_motor joint_diagnostics joint_stress physics_hinge physics_joints)
        afterlight_add_test(afterlight_${t}_test afterlight.${t}
            SOURCES tests/physics/joints/${t}.c INCLUDES engine/src)
    endforeach()

    foreach(t physics_islands physics_broadphase physics_landing physics_ccd physics_mesh
              physics_stress physics_timing physics_interaction physics_sleep
              physics_solver physics_contacts physics_motion physics_body)
        afterlight_add_test(afterlight_${t}_test afterlight.${t}
            SOURCES tests/physics/${t}.c INCLUDES engine/src)
    endforeach()
    afterlight_add_test(afterlight_physics_editor_test afterlight.physics_editor
        SOURCES tests/physics/physics_editor.c LIBS afterlight_editor_document)
    afterlight_add_test(afterlight_mesh_queries_test afterlight.mesh_queries
        SOURCES tests/physics/collision/mesh_queries.c INCLUDES engine/src)
    afterlight_add_test(afterlight_collision_mesh_test afterlight.collision_mesh
        SOURCES tests/physics/collision/collision_mesh.c INCLUDES engine/src)
    afterlight_add_test(afterlight_collider_data_test afterlight.collider_data
        SOURCES tests/physics/collision/collider_data.c INCLUDES third_party/cjson LIBS afterlight_editor_document
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/legacy-v4.afterlight.json"
        WORKDIR "${PROJECT_BINARY_DIR}/collider-data-tests")

    afterlight_add_test(afterlight_compound_demo_test afterlight.compound_demo
        SOURCES tests/physics/compound/compound_demo.c INCLUDES engine/src sandbox
        LIBS afterlight_editor_document SDL3::SDL3 DEPENDS afterlight_compound_game afterlight_prefabs_copy
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/prop-room.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/prop-workshop.afterlight.json"
             "$<TARGET_FILE:afterlight_compound_game>" "${PROJECT_BINARY_DIR}/prop-room.afterlight.project"
        GRAPHICS CHECK_VALIDATION DUMMY_AUDIO TIMEOUT 150)
    afterlight_add_test(afterlight_machinery_demo_test afterlight.machinery_demo
        SOURCES tests/physics/joints/machinery_demo.c INCLUDES engine/src sandbox
        LIBS afterlight_editor_document SDL3::SDL3 DEPENDS afterlight_machinery_game afterlight_prefabs_copy
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/motor-room.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/machinery-playground.afterlight.json"
             "$<TARGET_FILE:afterlight_machinery_game>" "${PROJECT_BINARY_DIR}/motor-room.afterlight.project"
        GRAPHICS CHECK_VALIDATION DUMMY_AUDIO TIMEOUT 150)
    afterlight_add_test(afterlight_mechanical_demo_test afterlight.mechanical_demo
        SOURCES tests/physics/joints/mechanical_demo.c INCLUDES engine/src sandbox
        LIBS afterlight_editor_document SDL3::SDL3 DEPENDS afterlight_mechanical_game afterlight_prefabs_copy
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/mesh-room.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/mechanical-playground.afterlight.json"
             "$<TARGET_FILE:afterlight_mechanical_game>" "${PROJECT_BINARY_DIR}/mesh-room.afterlight.project"
        GRAPHICS CHECK_VALIDATION DUMMY_AUDIO TIMEOUT 120)
    afterlight_add_test(afterlight_physics_demo_test afterlight.physics_demo
        SOURCES tests/physics/physics_demo.c INCLUDES engine/src sandbox
        LIBS afterlight_editor_document SDL3::SDL3 DEPENDS afterlight_physics_game afterlight_prefabs_copy
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/physics-drop.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/physics-playground.afterlight.json"
             "$<TARGET_FILE:afterlight_physics_game>" "${PROJECT_BINARY_DIR}/physics-drop.afterlight.project"
        GRAPHICS CHECK_VALIDATION DUMMY_AUDIO TIMEOUT 90)

    # Gameplay & character
    afterlight_add_test(afterlight_toolbox_test afterlight.toolbox
        SOURCES tests/gameplay/toolbox.c INCLUDES engine/src sandbox
        LIBS afterlight_editor_document SDL3::SDL3 DEPENDS afterlight_toolbox_game afterlight_prefabs_copy
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/toolbox.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/toolbox-course.afterlight.json"
             "$<TARGET_FILE:afterlight_toolbox_game>" "${PROJECT_BINARY_DIR}/toolbox.afterlight.project"
        GRAPHICS CHECK_VALIDATION DUMMY_AUDIO TIMEOUT 90)
    add_library(afterlight_character_code STATIC game/character_game.c)
    target_include_directories(afterlight_character_code PRIVATE engine/include)
    afterlight_warnings(afterlight_character_code)
    if(UNIX)
        target_link_libraries(afterlight_character_code PRIVATE m)
    endif()
    afterlight_add_test(afterlight_character_course_test afterlight.character_game
        SOURCES tests/gameplay/character_course.c INCLUDES engine/src
        LIBS Afterlight::Engine afterlight_character_code SDL3::SDL3
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/character-course.afterlight.json")
    afterlight_add_test(afterlight_character_demo_play_test afterlight.character_demo_play
        SOURCES tests/gameplay/character_demo_play.c INCLUDES engine/src sandbox
        LIBS afterlight_editor_document SDL3::SDL3 DEPENDS afterlight_character_game
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/scenes/character.afterlight.json"
             "${PROJECT_SOURCE_DIR}/sandbox/scenes/character-course.afterlight.json"
             "$<TARGET_FILE:afterlight_character_game>"
        GRAPHICS CHECK_VALIDATION DUMMY_AUDIO TIMEOUT 60)
    add_test(NAME afterlight.character_simple COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/character.afterlight.project" --frames 60 --validation)
    add_test(NAME afterlight.character_course COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/character-course.afterlight.project" --frames 90 --validation)
    add_test(NAME afterlight.character_crowd COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/character-stress.afterlight.project" --frames 60 --validation)
    set_tests_properties(afterlight.character_simple afterlight.character_course afterlight.character_crowd PROPERTIES
        TIMEOUT 60 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    set_tests_properties(afterlight.character_course PROPERTIES DEPENDS afterlight.character_simple)

    afterlight_add_test(afterlight_playground_test afterlight.playground
        SOURCES tests/gameplay/playground.c LIBS afterlight_playground_code Afterlight::Engine
        ARGS "${obstacle_scene}")
    add_test(NAME afterlight.playground_smoke COMMAND afterlight_playground --validation --frames 90)
    set_tests_properties(afterlight.playground_smoke PROPERTIES
        TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")

    afterlight_add_test(afterlight_locomotion_runtime_test afterlight.locomotion_runtime
        SOURCES tests/gameplay/locomotion_runtime.c INCLUDES engine/src LIBS Afterlight::Engine SDL3::SDL3
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/assets/locomotion.gltf")
    afterlight_add_test(afterlight_locomotion_test afterlight.locomotion SOURCES tests/gameplay/locomotion.c)
    afterlight_add_test(afterlight_locomotion_player_test afterlight.locomotion_player
        SOURCES tests/gameplay/locomotion_player.c ARGS "${PROJECT_SOURCE_DIR}/sandbox/assets/locomotion.gltf")
    afterlight_add_test(afterlight_third_person_test afterlight.third_person SOURCES tests/gameplay/third_person.c)

    # Animation
    afterlight_add_test(afterlight_animation_stress afterlight.animation_stress
        SOURCES sandbox/animation_stress.c INCLUDES engine/src LIBS Afterlight::Engine SDL3::SDL3
        ARGS "${PROJECT_SOURCE_DIR}/sandbox/assets/locomotion.gltf" TIMEOUT 60)
    afterlight_add_test(afterlight_root_motion_test afterlight.root_motion
        SOURCES tests/animation/root_motion.c ARGS "${PROJECT_SOURCE_DIR}/sandbox/assets/locomotion.gltf")
    afterlight_add_test(afterlight_animation_events_test afterlight.animation_events
        SOURCES tests/animation/animation_events.c ARGS "${PROJECT_SOURCE_DIR}/sandbox/assets/locomotion.gltf")
    afterlight_add_test(afterlight_animation_demo_play_test afterlight.animation_demo_play
        SOURCES tests/animation/animation_demo_play.c INCLUDES engine/src sandbox
        LIBS afterlight_editor_document SDL3::SDL3 DEPENDS afterlight_animation_game
        ARGS "${mechanism_scene}" "${animations_scene}" "$<TARGET_FILE:afterlight_animation_game>"
        GRAPHICS CHECK_VALIDATION TIMEOUT 60)
    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/demo-tests")
    add_test(NAME afterlight.animation_demos COMMAND afterlight_animation_builder "${PROJECT_SOURCE_DIR}/sandbox/assets" "${PROJECT_BINARY_DIR}/demo-tests")
    add_test(NAME afterlight.animation_demo_simple COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/mechanism.afterlight.project" --frames 90 --validation)
    add_test(NAME afterlight.animation_demo_complex COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/animations.afterlight.project" --frames 150 --validation)
    set_tests_properties(afterlight.animation_demo_simple afterlight.animation_demo_complex PROPERTIES
        TIMEOUT 60 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]"
        ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    set_tests_properties(afterlight.animation_demo_complex PROPERTIES DEPENDS afterlight.animation_demo_simple)

    file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/animation-tests")
    afterlight_add_test(afterlight_animation_scene_test afterlight.animation_scene
        SOURCES tests/animation/animation_scene.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/animated.gltf" "${PROJECT_SOURCE_DIR}/tests/assets/skinned.gltf"
        GRAPHICS CHECK_VALIDATION TIMEOUT 60)
    afterlight_add_test(afterlight_animation_authoring_test afterlight.animation_authoring
        SOURCES tests/animation/animation_authoring.c INCLUDES engine/src LIBS afterlight_editor_document
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/skinned.gltf" "${PROJECT_SOURCE_DIR}/tests/assets/animated.gltf"
             "${PROJECT_SOURCE_DIR}/tests/assets/morph.gltf"
        WORKDIR "${PROJECT_BINARY_DIR}/animation-tests")
    afterlight_add_test(afterlight_animation_runtime_test afterlight.animation_runtime
        SOURCES tests/animation/animation_runtime.c INCLUDES engine/src LIBS afterlight_editor_document SDL3::SDL3
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/skinned.gltf"
        WORKDIR "${PROJECT_BINARY_DIR}/animation-tests")
    afterlight_add_test(afterlight_morph_test afterlight.morph_data
        SOURCES tests/animation/morph.c INCLUDES engine/src
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets/morph.gltf" --cpu)
    add_test(NAME afterlight.morph COMMAND afterlight_morph_test "${PROJECT_SOURCE_DIR}/tests/assets/morph.gltf")
    set_tests_properties(afterlight.morph PROPERTIES TIMEOUT 45 LABELS "graphics" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    afterlight_add_test(afterlight_skeleton_test afterlight.skeleton
        SOURCES tests/animation/skeleton.c ARGS "${PROJECT_SOURCE_DIR}/tests/assets/skinned.gltf")
    add_test(NAME afterlight.skeleton_normalized COMMAND afterlight_skeleton_test "${PROJECT_SOURCE_DIR}/tests/assets/skinned-normalized.gltf")
    add_test(NAME afterlight.skeleton_data COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/animation/skeleton_data.py"
        "$<TARGET_FILE:afterlight_skeleton_test>" "${PROJECT_SOURCE_DIR}/tests/assets/skinned.gltf")
    afterlight_add_test(afterlight_skinning_test afterlight.skinning
        SOURCES tests/animation/skinning.c INCLUDES engine/src sandbox GRAPHICS CHECK_VALIDATION TIMEOUT 60)
    afterlight_add_test(afterlight_animation_player_test afterlight.animation_player
        SOURCES tests/animation/animation_player.c ARGS "${PROJECT_SOURCE_DIR}/tests/assets/animated.gltf")
    afterlight_add_test(afterlight_animation_test afterlight.animation
        SOURCES tests/animation/animation.c ARGS "${PROJECT_SOURCE_DIR}/tests/assets")
    add_test(NAME afterlight.animation_data COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/animation/animation_data.py"
        "$<TARGET_FILE:afterlight_animation_test>" "${PROJECT_SOURCE_DIR}/tests/assets/animated.gltf")

    # Core, scene, scripting, import, runtime & editor
    afterlight_add_test(afterlight_wind_test afterlight.wind
        SOURCES tests/scene/wind.c editor/document/document.c INCLUDES editor third_party/cjson)
    afterlight_add_test(afterlight_scene_index_test afterlight.scene_index
        SOURCES tests/scene/scene_index.c INCLUDES engine/src)
    afterlight_add_test(afterlight_prefab_test afterlight.prefab SOURCES tests/scene/prefab.c)
    afterlight_add_test(afterlight_input_test afterlight.input SOURCES tests/core/input.c)
    afterlight_add_test(afterlight_jobs_test afterlight.jobs SOURCES tests/core/jobs.c INCLUDES engine/src)
    afterlight_add_test(afterlight_audio_test afterlight.audio
        SOURCES tests/core/audio.c INCLUDES engine/src ARGS "${PROJECT_SOURCE_DIR}/sandbox/assets" DUMMY_AUDIO)
    afterlight_add_test(afterlight_fixed_loop_test afterlight.fixed_loop
        SOURCES tests/core/fixed_loop.c INCLUDES sandbox LIBS Afterlight::Engine SDL3::SDL3
        GRAPHICS CHECK_VALIDATION TIMEOUT 45)
    foreach(test_source tests/core/simulation.c tests/physics/collision/collision.c tests/gameplay/character.c)
        get_filename_component(test "${test_source}" NAME_WE)
        afterlight_add_test(afterlight_${test}_test afterlight.${test} SOURCES "${test_source}")
    endforeach()
    afterlight_add_test(afterlight_math_test afterlight.math SOURCES tests/core/math.c)
    afterlight_add_test(afterlight_scene_test afterlight.scene SOURCES tests/scene/scene.c)
    afterlight_add_test(afterlight_landscape_test afterlight.landscape SOURCES tests/scene/landscape.c)
    afterlight_add_test(afterlight_dynamic_pools_and_cow_test afterlight.dynamic_pools_and_cow
        SOURCES tests/physics/dynamic_pools_and_cow.c)
    afterlight_add_test(afterlight_blueprint_test afterlight.blueprint SOURCES tests/scripting/blueprint.c)
    afterlight_add_test(afterlight_scene_io_test afterlight.scene_io
        SOURCES tests/scene/scene_io.c ARGS "${PROJECT_SOURCE_DIR}/tests/assets/legacy-v2.afterlight.json"
        WORKDIR "${PROJECT_BINARY_DIR}/scene-io-tests" TIMEOUT 15)
    afterlight_add_test(afterlight_hierarchy_test afterlight.hierarchy
        SOURCES tests/scene/hierarchy.c LIBS afterlight_editor_document WORKDIR "${PROJECT_BINARY_DIR}/hierarchy-tests")
    afterlight_add_test(afterlight_project_test afterlight.project
        SOURCES tests/scene/project.c INCLUDES third_party/cjson LIBS afterlight_editor_document
        ARGS "${PROJECT_BINARY_DIR}/playground.afterlight.project" "${PROJECT_SOURCE_DIR}/tests/assets/legacy-v4.afterlight.json"
        WORKDIR "${PROJECT_BINARY_DIR}/project-tests")

    afterlight_add_test(afterlight_asset_test afterlight.asset
        SOURCES tests/import/asset.c ARGS "${PROJECT_SOURCE_DIR}/tests/assets/quad.obj"
        WORKDIR "${PROJECT_BINARY_DIR}/asset-tests")
    afterlight_add_test(afterlight_gltf_test afterlight.gltf
        SOURCES tests/import/gltf.c INCLUDES third_party/cjson
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets" "${PROJECT_SOURCE_DIR}/sandbox/assets/beacon.glb"
        WORKDIR "${PROJECT_BINARY_DIR}/gltf-tests")
    afterlight_add_test(afterlight_model_test afterlight.model
        SOURCES tests/import/model.c LIBS afterlight_editor_document
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets" WORKDIR "${PROJECT_BINARY_DIR}/model-tests")
    afterlight_add_test(afterlight_asset_reload_test afterlight.asset_reload
        SOURCES tests/import/asset_reload.c INCLUDES engine/src LIBS Afterlight::Engine SDL3::SDL3
        ARGS "${PROJECT_SOURCE_DIR}/tests/assets" WORKDIR "${PROJECT_BINARY_DIR}/asset-reload-tests"
        GRAPHICS CHECK_VALIDATION TIMEOUT 30)
    afterlight_add_test(afterlight_mesh_reload_test afterlight.mesh_reload
        SOURCES tests/import/mesh_reload.c INCLUDES engine/src
        WORKDIR "${PROJECT_BINARY_DIR}/mesh-reload-tests" GRAPHICS TIMEOUT 60)

    # Runtime, hot reload, save/preferences
    foreach(module afterlight_state_game afterlight_state_reject_game)
        add_library(${module} MODULE tests/runtime/state_module.c)
        target_include_directories(${module} PRIVATE engine/include)
        set_target_properties(${module} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
        afterlight_warnings(${module})
    endforeach()
    target_compile_definitions(afterlight_state_reject_game PRIVATE REJECT_STATE=1)

    afterlight_add_test(afterlight_scene_transition_test afterlight.scene_transition
        SOURCES tests/runtime/scene_transition.c LIBS afterlight_editor_document SDL3::SDL3
        DEPENDS afterlight_state_game ARGS "$<TARGET_FILE:afterlight_state_game>")
    afterlight_add_test(afterlight_player_preferences_test afterlight.player_preferences
        SOURCES tests/runtime/player_preferences.c LIBS afterlight_player_preferences)
    afterlight_add_test(afterlight_save_game_test afterlight.save_game
        SOURCES tests/runtime/save_game.c LIBS afterlight_editor_document
        DEPENDS afterlight_state_game afterlight_state_reject_game
        ARGS "$<TARGET_FILE:afterlight_state_game>" "$<TARGET_FILE:afterlight_state_reject_game>")
    afterlight_add_test(afterlight_state_reload_test afterlight.state_reload
        SOURCES tests/runtime/state_reload.c LIBS afterlight_editor_document SDL3::SDL3
        DEPENDS afterlight_state_game afterlight_state_reject_game
        ARGS "$<TARGET_FILE:afterlight_state_game>" "$<TARGET_FILE:afterlight_state_reject_game>")

    add_library(afterlight_game_fast MODULE game/demo_game.c)
    target_include_directories(afterlight_game_fast PRIVATE engine/include)
    target_compile_definitions(afterlight_game_fast PRIVATE AF_DEMO_TURN_SPEED=2.0f)
    if(UNIX)
        target_link_libraries(afterlight_game_fast PRIVATE m)
    endif()
    add_library(afterlight_game_bad MODULE tests/runtime/module_variant.c)
    target_include_directories(afterlight_game_bad PRIVATE engine/include)
    target_compile_definitions(afterlight_game_bad PRIVATE AF_TEST_BAD_ABI=1)
    add_library(afterlight_game_init_fail MODULE tests/runtime/module_variant.c)
    target_include_directories(afterlight_game_init_fail PRIVATE engine/include)

    afterlight_add_test(afterlight_hot_reload_test afterlight.hot_reload
        SOURCES tests/runtime/hot_reload.c LIBS afterlight_editor_document SDL3::SDL3
        DEPENDS afterlight_game afterlight_game_fast afterlight_game_bad afterlight_game_init_fail
        ARGS "$<TARGET_FILE:afterlight_game>" "$<TARGET_FILE:afterlight_game_fast>"
             "$<TARGET_FILE:afterlight_game_bad>" "$<TARGET_FILE:afterlight_game_init_fail>"
        WORKDIR "${PROJECT_BINARY_DIR}/hot-reload-tests")

    add_library(afterlight_demo_test STATIC game/demo_game.c)
    target_include_directories(afterlight_demo_test PRIVATE engine/include)
    afterlight_add_test(afterlight_runtime_test afterlight.runtime
        SOURCES tests/runtime/runtime.c LIBS afterlight_editor_document afterlight_demo_test)
    afterlight_add_test(afterlight_smoke afterlight.smoke
        SOURCES tests/runtime/smoke.c LIBS Afterlight::Engine SDL3::SDL3
        GRAPHICS CHECK_VALIDATION TIMEOUT 30)

    # Editor tests
    afterlight_add_test(afterlight_environment_editor_test afterlight.environment_editor
        SOURCES tests/editor/environment_editor.c INCLUDES engine/src sandbox LIBS afterlight_editor_document
        GRAPHICS TIMEOUT 40)
    afterlight_add_test(afterlight_light_gizmo_test afterlight.light_gizmos
        SOURCES tests/editor/light_gizmos.c LIBS afterlight_editor_document)
    afterlight_add_test(afterlight_thumbnail_test afterlight.thumbnails
        SOURCES tests/editor/thumbnails.c LIBS afterlight_editor_ui ARGS "${PROJECT_SOURCE_DIR}/tests/assets")
    afterlight_add_test(afterlight_gizmo_test afterlight.gizmo
        SOURCES tests/editor/gizmo.c LIBS afterlight_editor_document)
    afterlight_add_test(afterlight_editor_document_test afterlight.editor_document
        SOURCES tests/editor/editor_document.c LIBS afterlight_editor_document
        WORKDIR "${PROJECT_BINARY_DIR}/editor-document-tests")

    add_test(NAME afterlight.editor_animation COMMAND afterlight_editor --animation-tools-test "${PROJECT_SOURCE_DIR}/tests/assets/animated.gltf" --validation)
    set_tests_properties(afterlight.editor_animation PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.editor_animation_authoring COMMAND afterlight_editor --animation-authoring-test "${PROJECT_SOURCE_DIR}/tests/assets/skinned.gltf" --validation)
    set_tests_properties(afterlight.editor_animation_authoring PROPERTIES TIMEOUT 45 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1" WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/animation-tests")
    add_test(NAME afterlight.editor_light_tools COMMAND afterlight_editor --light-tools-test --validation)
    set_tests_properties(afterlight.editor_light_tools PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.editor_thumbnails COMMAND afterlight_editor --scene "${afterlight_lighting_scene}" --thumbnail-test --validation)
    set_tests_properties(afterlight.editor_thumbnails PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.editor_wizard COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/editor/editor_wizard.py" "$<TARGET_FILE:afterlight_editor>")
    set_tests_properties(afterlight.editor_wizard PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.editor_smoke COMMAND afterlight_editor --validation --smoke-test "${PROJECT_BINARY_DIR}/editor-smoke.afterlight.json")
    set_tests_properties(afterlight.editor_smoke PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1")
    add_test(NAME afterlight.editor_playground COMMAND afterlight_editor --playground --play-controls-test --validation)
    set_tests_properties(afterlight.editor_playground PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.editor_project COMMAND afterlight_editor --project "${PROJECT_BINARY_DIR}/playground.afterlight.project" --play-controls-test --validation)
    set_tests_properties(afterlight.editor_project PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    set_property(TEST afterlight.editor_smoke afterlight.editor_playground afterlight.editor_project
                      afterlight.editor_animation afterlight.editor_light_tools afterlight.editor_thumbnails
                 PROPERTY RESOURCE_LOCK "editor_interactive")

    # Player, tooling, packaging & licensing tests
    add_test(NAME afterlight.player_menu COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/water-expedition.afterlight.project" --menu-test --validation)
    set_tests_properties(afterlight.player_menu PROPERTIES TIMEOUT 40 LABELS "graphics" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.player COMMAND afterlight_player --project "${PROJECT_BINARY_DIR}/playground.afterlight.project" --frames 80 --validation)
    set_tests_properties(afterlight.player PROPERTIES TIMEOUT 30 LABELS "graphics" FAIL_REGULAR_EXPRESSION "\\[validation error\\]" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.asset_preflight COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/import/asset_preflight.py" --root "${PROJECT_SOURCE_DIR}" --player "$<TARGET_FILE:afterlight_player>" --module "$<TARGET_FILE:afterlight_game>")
    add_test(NAME afterlight.dco COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/dco.py")
    add_test(NAME afterlight.licensing COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/licensing.py"
        --root "${PROJECT_SOURCE_DIR}" --archive "${afterlight_source_archive}" --player "${afterlight_player_package}")
    set_tests_properties(afterlight.licensing PROPERTIES LABELS "packaging")

    set(package_test_flags)
    if(CMAKE_C_FLAGS MATCHES "sanitize")
        list(APPEND package_test_flags --sanitized)
    endif()
    add_test(NAME afterlight.ray_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/ray_export.py" --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_ray_shadow_game>" ${package_test_flags})
    set_tests_properties(afterlight.ray_export PROPERTIES TIMEOUT 180 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.bundle_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/bundle_export.py" --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_playground_game>" --extension "$<TARGET_FILE:afterlight_upscaler_fixture>" ${package_test_flags})
    set_tests_properties(afterlight.bundle_export PROPERTIES TIMEOUT 180 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.water_course_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/water_export.py" --courses --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_water_course_game>" --play-test "$<TARGET_FILE:afterlight_water_course_test>" ${package_test_flags})
    set_tests_properties(afterlight.water_course_export PROPERTIES TIMEOUT 300 LABELS "graphics;packaging")
    add_test(NAME afterlight.water_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/water_export.py" --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_water_game>" --play-test "$<TARGET_FILE:afterlight_water_demo_test>" ${package_test_flags})
    set_tests_properties(afterlight.water_export PROPERTIES TIMEOUT 180 LABELS "graphics;packaging")
    add_test(NAME afterlight.compound_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/physics_export.py" --compound --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_compound_game>" --play-test "$<TARGET_FILE:afterlight_compound_demo_test>" ${package_test_flags})
    set_tests_properties(afterlight.compound_export PROPERTIES TIMEOUT 300 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.machinery_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/physics_export.py" --machinery --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_machinery_game>" --play-test "$<TARGET_FILE:afterlight_machinery_demo_test>" ${package_test_flags})
    set_tests_properties(afterlight.machinery_export PROPERTIES TIMEOUT 300 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.mechanical_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/physics_export.py" --mechanical --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_mechanical_game>" --play-test "$<TARGET_FILE:afterlight_mechanical_demo_test>" ${package_test_flags})
    set_tests_properties(afterlight.mechanical_export PROPERTIES TIMEOUT 300 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.physics_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/physics_export.py" --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_physics_game>" --play-test "$<TARGET_FILE:afterlight_physics_demo_test>" ${package_test_flags})
    set_tests_properties(afterlight.physics_export PROPERTIES TIMEOUT 240 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.toolbox_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/toolbox_export.py" --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_toolbox_game>" --play-test "$<TARGET_FILE:afterlight_toolbox_test>" ${package_test_flags})
    set_tests_properties(afterlight.toolbox_export PROPERTIES TIMEOUT 180 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.character_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/character_export.py" --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_character_game>" ${package_test_flags})
    set_tests_properties(afterlight.character_export PROPERTIES TIMEOUT 180 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.animation_export COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/animation_export.py" --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --module "$<TARGET_FILE:afterlight_animation_game>" ${package_test_flags})
    set_tests_properties(afterlight.animation_export PROPERTIES TIMEOUT 180 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
    add_test(NAME afterlight.project_tools COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/tooling/project_tools.py"
        --root "${PROJECT_SOURCE_DIR}" --player "${afterlight_player_package}" --model-test "$<TARGET_FILE:afterlight_model_test>" --fixtures "${PROJECT_SOURCE_DIR}/tests/assets" ${package_test_flags})
    set_tests_properties(afterlight.project_tools PROPERTIES TIMEOUT 120 LABELS "graphics;packaging" ENVIRONMENT "VK_LAYER_VALIDATE_SYNC=1;SDL_AUDIO_DRIVER=dummy")
endif()

# Preserve ASan global checking while replacing its broken Mach-O unload hook.
# The first linked object registers cleanup first, so it executes after user
# destructors. Disable only the compiler-generated destructor to avoid duplicates.
if(APPLE AND CMAKE_C_COMPILER_ID MATCHES "Clang")
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-fsanitize-address-destructor=none" AF_HAS_ASAN_DESTRUCTOR_OPTION)
    if(BUILD_TESTING)
        add_library(afterlight_unload_module MODULE tests/runtime/unload_module.c)
        set_target_properties(afterlight_unload_module PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
        afterlight_warnings(afterlight_unload_module)
        afterlight_add_test(afterlight_unload_test afterlight.module_unload
            SOURCES tests/runtime/unload.c LIBS SDL3::SDL3 DEPENDS afterlight_unload_module
            ARGS "$<TARGET_FILE:afterlight_unload_module>")
    endif()
    if(AF_HAS_ASAN_DESTRUCTOR_OPTION)
        foreach(module
            ${afterlight_editor_game_modules}
            afterlight_state_game afterlight_state_reject_game
            afterlight_game_fast afterlight_game_bad afterlight_game_init_fail
            afterlight_unload_module
        )
            if(TARGET ${module})
                get_target_property(module_sources ${module} SOURCES)
                set_property(TARGET ${module} PROPERTY SOURCES "${PROJECT_SOURCE_DIR}/engine/src/core/module_sanitizer.c" ${module_sources})
                target_compile_options(${module} PRIVATE -Xclang -fsanitize-address-destructor=none)
            endif()
        endforeach()
    endif()
endif()

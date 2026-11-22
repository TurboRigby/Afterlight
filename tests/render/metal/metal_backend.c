/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include <afterlight/extension_vulkan.h>
#include "render/render_test.h"
#include "test_custom_material_frag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct state {
    af_scene *scene;
    af_object_id receiver;
    af_object_id occluder;
    unsigned ticks, phase;
    bool failed;
    unsigned char lit_pixel[4];
    unsigned char shadowed_pixel[4];
} state;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "metal_backend phase %u line %d: %s\n", s->phase, __LINE__, #c);                           \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

static const af_vertex quad_vertices[] = {
    {.position = {-3, -3, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 0}},
    {.position = {3, -3, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 0}},
    {.position = {3, 3, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 1}},
    {.position = {-3, 3, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 1}},
};
static const uint32_t quad_indices[] = {0, 1, 2, 0, 2, 3};

static bool init(af_engine *engine, void *data) {
    state *s = data;
    af_engine_stats stats = af_engine_get_stats(engine);
    if (stats.active_backend != AF_RENDER_BACKEND_METAL) {
        fprintf(stderr, "Expected AF_RENDER_BACKEND_METAL (%d), got %d\n", (int)AF_RENDER_BACKEND_METAL,
                (int)stats.active_backend);
        return false;
    }

    s->scene = af_scene_create();
    if (!s->scene)
        return false;

    af_mesh_data quad = {quad_vertices, 4, quad_indices, 6};
    if (!af_engine_upload_mesh(engine, "surface", &quad))
        return false;

    s->receiver = af_scene_add_object(s->scene, "Receiver", "surface");
    af_material material = af_material_default();
    material.base_color = (af_color){0.8f, 0.8f, 0.8f, 1.0f};
    material.roughness = 0.9f;
    af_scene_set_object_material(s->scene, s->receiver, material);

    /* Add an occluder initially hidden; used in hardware ray-traced shadow phases. */
    s->occluder = af_scene_add_object(s->scene, "Occluder", "surface");
    af_transform occ_t = af_transform_identity();
    occ_t.position = (af_vec3){0.0f, 0.0f, 2.0f};
    occ_t.scale = (af_vec3){0.6f, 0.6f, 1.0f};
    af_scene_set_object_transform(s->scene, s->occluder, occ_t);
    af_scene_set_object_visible(s->scene, s->occluder, false);

    /* Disable sun/ambient so phase 0 exercises 100 screen-tiled local lights. */
    af_directional_light sun = af_directional_light_default();
    sun.intensity = 0.0f;
    sun.ambient = 0.0f;
    af_scene_set_light(s->scene, sun);

    for (unsigned i = 0; i < 100; ++i) {
        af_local_light light = af_local_light_default(AF_LIGHT_POINT);
        float gx = (float)(i % 10u) * 0.4f - 1.8f;
        float gy = (float)(i / 10u) * 0.4f - 1.8f;
        light.position = (af_vec3){gx, gy, 1.2f};
        light.color = (af_vec3){1.0f, 1.0f, 1.0f};
        light.intensity = 0.35f;
        light.range = 4.0f;
        if (!af_scene_add_local_light(s->scene, light))
            return false;
    }

    af_render_settings settings = af_render_settings_default();
    settings.tone_mapping = false;
    settings.environment_enabled = false;
    settings.skybox = false;
    settings.bloom = false;
    settings.anti_aliasing = AF_AA_OFF;
    af_scene_set_render_settings(s->scene, settings);

    /* Verify invalid custom SPIR-V is cleanly rejected on Metal. */
    uint32_t bad_spirv[8] = {0x12345678u, 0, 0, 0, 0, 0, 0, 0};
    if (af_engine_upload_material_shader(engine, "bad", bad_spirv, 8) ||
        af_engine_upload_material_shader(engine, "", af_test_custom_material_frag,
                                         sizeof(af_test_custom_material_frag) / sizeof(uint32_t)))
        return false;

    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0.0f, 0.0f, 5.0f};
    camera.yaw = -AF_PI * 0.5f;
    camera.pitch = 0.0f;
    af_engine_set_camera(engine, &camera);
    af_engine_set_clear_color(engine, (af_color){0, 0, 0, 1});
    af_engine_set_scene(engine, s->scene);
    return true;
}

static void update(af_engine *engine, double dt, void *data) {
    (void)dt;
    state *s = data;
    ++s->ticks;
    if (s->ticks < 8)
        return;

    int w = 0, h = 0;
    af_engine_get_window_size(engine, &w, &h);
    af_vec2 center = {(float)w * 0.5f, (float)h * 0.5f};

    if (s->ticks == 8) {
        CHECK(af_engine_test_capture_pixel(engine, center));
        return;
    }

    unsigned char p[4] = {0};
    CHECK(af_engine_test_read_pixel(engine, p));
    af_engine_stats stats = af_engine_get_stats(engine);
    CHECK(stats.active_backend == AF_RENDER_BACKEND_METAL);
    printf("Metal backend phase %u: pixel=%u/%u/%u ray_active=%d blas=%llu tlas=%llu aa=%d\n", s->phase, p[0], p[1],
           p[2], (int)stats.ray_traced_shadows_active, (unsigned long long)stats.ray_blas_builds,
           (unsigned long long)stats.ray_tlas_builds, (int)stats.effective_aa);

    switch (s->phase) {
    case 0: {
        /* 100 tiled local lights illuminate receiver brightly. */
        CHECK(p[0] > 180 && p[1] > 180 && p[2] > 180);
        memcpy(s->lit_pixel, p, 4);

        /* Upload runtime custom SPIR-V material shader (translated via SPIRV-Cross -> MSL). */
        size_t words = sizeof(af_test_custom_material_frag) / sizeof(uint32_t);
        CHECK(af_engine_upload_material_shader(engine, "test_magenta", af_test_custom_material_frag, words));
        af_material mat = af_scene_get_object(s->scene, s->receiver)->material;
        snprintf(mat.shader, sizeof(mat.shader), "%s", "test_magenta");
        CHECK(af_scene_set_object_material(s->scene, s->receiver, mat));
        break;
    }
    case 1: {
        /* Custom SPIR-V fragment shader renders magenta on Metal. */
        CHECK(p[0] > 200 && p[1] < 20 && p[2] > 200);
        /* Trigger swapchain rebuild (resize + bloom) to verify custom Metal pipeline rebuild. */
        CHECK(af_engine_set_window_size(engine, 480, 320));
        af_render_settings settings = af_scene_get_render_settings(s->scene);
        settings.bloom = true;
        CHECK(af_scene_set_render_settings(s->scene, settings));
        break;
    }
    case 2: {
        /* Custom shader survived resize & bloom rebuild. */
        CHECK(p[0] > 200 && p[1] < 40 && p[2] > 200);
        CHECK(af_engine_remove_material_shader(engine, "test_magenta"));
        af_render_settings settings = af_scene_get_render_settings(s->scene);
        settings.bloom = false;
        CHECK(af_scene_set_render_settings(s->scene, settings));
        break;
    }
    case 3: {
        /* Removed custom shader cleanly fell back to built-in PBR shader. */
        CHECK(abs((int)p[0] - (int)s->lit_pixel[0]) <= 10);
        CHECK(abs((int)p[1] - (int)s->lit_pixel[1]) <= 10);
        CHECK(abs((int)p[2] - (int)s->lit_pixel[2]) <= 10);

        /* Exercise native MetalFX upscaler when available on this macOS version/GPU. */
        if (af_engine_upscaler_quality_count(engine) > 0) {
            CHECK(af_engine_upscaler_quality_count(engine) >= 3);
            CHECK(af_engine_upscaler_quality_name(engine, 0) != NULL);
            CHECK(af_engine_set_upscaler_quality(engine, 1));
            CHECK(af_engine_upscaler_quality(engine) == 1);
            CHECK(af_engine_set_upscaler_enabled(engine, true));
        }
        break;
    }
    case 4: {
        /* MetalFX produced a clean bright image (or unchanged if unavailable). */
        CHECK(p[0] > 160 && p[1] > 160 && p[2] > 160);
        if (af_engine_upscaler_quality_count(engine) > 0) {
            CHECK(stats.effective_aa == AF_AA_OFF);
            CHECK(af_engine_set_upscaler_enabled(engine, false));
        }

        /* Switch to directional sun + occluder + Metal 3 hardware ray-traced shadows. */
        while (af_scene_local_light_count(s->scene) > 0)
            CHECK(af_scene_remove_local_light(s->scene, 0));

        af_directional_light sun = af_directional_light_default();
        sun.direction = (af_vec3){0.0f, -0.5f, -0.866f};
        sun.color = (af_vec3){1.0f, 1.0f, 1.0f};
        sun.intensity = 2.0f;
        sun.ambient = 0.05f;
        af_scene_set_light(s->scene, sun);

        af_shadow_settings shadows = af_shadow_settings_default();
        shadows.enabled = true;
        af_scene_set_shadows(s->scene, shadows);

        af_render_settings settings = af_scene_get_render_settings(s->scene);
        settings.ray_traced_shadows = true;
        CHECK(af_scene_set_render_settings(s->scene, settings));

        /* Place occluder along sun ray from (0,0,0) toward (0, +0.5, +0.866), above the center camera ray. */
        af_transform occ_t = af_transform_identity();
        occ_t.position = (af_vec3){0.0f, 1.2f, 2.08f};
        occ_t.scale = (af_vec3){0.35f, 0.35f, 1.0f};
        CHECK(af_scene_set_object_transform(s->scene, s->occluder, occ_t));
        CHECK(af_scene_set_object_visible(s->scene, s->occluder, true));
        break;
    }
    case 5: {
        if (stats.ray_query_supported) {
            CHECK(stats.ray_traced_shadows_active);
            CHECK(stats.ray_blas_builds > 0);
            CHECK(stats.ray_tlas_builds > 0);
        }
        memcpy(s->shadowed_pixel, p, 4);
        CHECK(af_scene_set_object_visible(s->scene, s->occluder, false));
        break;
    }
    case 6: {
        /* Removing occluder brightens the receiver under hardware ray shadows. */
        CHECK(p[0] > s->shadowed_pixel[0] + 40);
        printf("Metal backend test: 100 tiled lights, runtime SPIR-V->MSL custom shader, MetalFX upscaler, and "
               "hardware ray-traced shadows passed.\n");
        af_engine_request_quit(engine);
        return;
    }
    }

    ++s->phase;
    s->ticks = 0;
}

static void shutdown(af_engine *engine, void *data) {
    state *s = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(s->scene);
}

int main(void) {
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight Metal backend test";
    config.width = 400;
    config.height = 300;
    config.enable_validation = true;
    config.backend = AF_RENDER_BACKEND_METAL;

    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;

    state s = {0};
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    return result || s.failed || s.phase != 6 || stats.validation_errors;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include "render_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
    af_scene *scene;
    af_object_id object;
    unsigned step;
    bool failed;
    unsigned char legacy[4], rough[4], metal[4], flat[4], exposed[4];
} state;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Rendering step %u line %d: %s\n", s->step, __LINE__, #c);                                 \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static const af_vertex vertices[] = {
    {.position = {-2, -2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 0}},
    {.position = {2, -2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {128, 0}},
    {.position = {2, 2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {128, 128}},
    {.position = {-2, 2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 128}}};
static const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
static bool init(af_engine *engine, void *data) {
    state *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    s->object = af_scene_add_object(s->scene, "Surface", "quad");
    af_mesh_data mesh = {vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "quad", &mesh))
        return false;
    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 4};
    camera.yaw = -1.57079632679f;
    camera.pitch = 0;
    if (!af_engine_set_camera(engine, &camera))
        return false;
    af_directional_light light = af_directional_light_default();
    light.direction = (af_vec3){0, 0, -1};
    light.color = (af_vec3){1, 1, 1};
    light.intensity = 1;
    light.ambient = 0;
    af_scene_set_light(s->scene, light);
    af_shadow_settings shadow = af_shadow_settings_default();
    shadow.enabled = false;
    shadow.resolution = 512;
    af_scene_set_shadows(s->scene, shadow);
    af_material material = af_material_default();
    material.base_color = (af_color){0.25f, 0.12f, 0.06f, 1};
    af_scene_set_object_material(s->scene, s->object, material);
    unsigned char pixels[3 * 5 * 4];
    memset(pixels, 255, sizeof(pixels));
    if (!af_engine_upload_texture(engine, "npot", pixels, 3, 5))
        return false;
    const unsigned char normal[] = {255, 128, 128, 255};
    if (!af_engine_upload_normal_map(engine, "tilted", normal, 1, 1))
        return false;
    unsigned char checker[64 * 64 * 4];
    for (unsigned y = 0; y < 64; ++y)
        for (unsigned x = 0; x < 64; ++x) {
            size_t i = ((size_t)y * 64 + x) * 4;
            checker[i] = checker[i + 1] = checker[i + 2] = (x + y) % 2 ? 255 : 0;
            checker[i + 3] = 255;
        }
    if (!af_engine_upload_texture(engine, "checker", checker, 64, 64))
        return false;
    af_engine_set_scene(engine, s->scene);
    return true;
}
static void capture(af_engine *engine, state *s) {
    int w, h;
    af_engine_get_window_size(engine, &w, &h);
    if (!af_engine_test_capture_pixel(engine, (af_vec2){(float)w / 2, (float)h / 2})) {
        s->failed = true;
        af_engine_request_quit(engine);
    }
}
static void update(af_engine *engine, double delta, void *data) {
    (void)delta;
    state *s = data;
    ++s->step;
    unsigned char p[4];
    af_material material = af_scene_get_object(s->scene, s->object)->material;
    if (s->step == 3) {
        af_texture_info info;
        CHECK(af_engine_texture_info(engine, "npot", &info) && info.width == 3 && info.height == 5 &&
              info.mip_levels == 3 && info.srgb && info.allocation_bytes >= 72 && info.anisotropy >= 1);
        CHECK(af_engine_texture_info(engine, "tilted", &info) && !info.srgb && info.mip_levels == 1);
        CHECK(!af_engine_texture_info(engine, "missing", &info));
        capture(engine, s);
    }
    if (s->step == 4) {
        CHECK(af_engine_test_read_pixel(engine, s->legacy));
        material.physically_based = true;
        material.roughness = 0.9f;
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 5) {
        CHECK(af_engine_test_read_pixel(engine, s->rough));
        CHECK(s->rough[0] + 20 < s->legacy[0] && s->rough[0] > 30);
        material.metallic = 1;
        material.roughness = 0.8f;
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 6) {
        CHECK(af_engine_test_read_pixel(engine, s->metal));
        material.roughness = 0.1f;
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 7) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] > s->metal[0] + 30);
        af_directional_light light = af_scene_get_light(s->scene);
        light.intensity = 0;
        CHECK(af_scene_set_light(s->scene, light));
        material.emission = (af_vec3){0, 1, 0};
        material.emission_strength = 2;
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 8) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[1] > 240 && p[0] < 5 && p[2] < 5);
        material = af_material_default();
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        af_directional_light light = af_scene_get_light(s->scene);
        light.intensity = 1;
        CHECK(af_scene_set_light(s->scene, light));
        capture(engine, s);
    }
    if (s->step == 9) {
        CHECK(af_engine_test_read_pixel(engine, s->flat));
        snprintf(material.normal_map, sizeof(material.normal_map), "tilted");
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 10) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] + 80 < s->flat[0]);
        material.normal_strength = 0;
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 11) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(abs((int)p[0] - (int)s->flat[0]) < 4);
        snprintf(material.normal_map, sizeof(material.normal_map), "missing");
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 12) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] > 240 && af_engine_get_stats(engine).missing_textures == 1);
    }
    if (s->step == 12) {
        material = af_material_default();
        material.unlit = true;
        material.base_color = (af_color){0.25f, 0.25f, 0.25f, 1};
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        CHECK(af_scene_set_render_settings(s->scene, (af_render_settings){.exposure = 1}));
        capture(engine, s);
    }
    if (s->step == 13) {
        CHECK(af_engine_test_read_pixel(engine, s->exposed));
        CHECK(s->exposed[0] > 170 && s->exposed[0] < 200);
        CHECK(af_scene_set_render_settings(s->scene, (af_render_settings){.exposure = 1, .tone_mapping = true}));
        capture(engine, s);
    }
    if (s->step == 14) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] + 20 < s->exposed[0] && p[0] > 140);
        CHECK(af_scene_set_render_settings(s->scene, (af_render_settings){.exposure = -1}));
        capture(engine, s);
    }
    if (s->step == 15) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] < 115 && p[0] > 90);
    }
    if (s->step == 15) {
        material = af_material_default();
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        CHECK(af_scene_set_render_settings(s->scene, af_render_settings_default()));
        af_directional_light sun = af_scene_get_light(s->scene);
        sun.intensity = 0;
        CHECK(af_scene_set_light(s->scene, sun));
        af_local_light local = af_local_light_default(AF_LIGHT_POINT);
        local.position = (af_vec3){0, 0, 2};
        local.intensity = 4;
        CHECK(af_scene_add_local_light(s->scene, local));
        capture(engine, s);
    }
    if (s->step == 16) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] > 230);
        af_local_light local = *af_scene_local_light_at(s->scene, 0);
        local.range = 1;
        CHECK(af_scene_set_local_light(s->scene, 0, local));
        capture(engine, s);
    }
    if (s->step == 17) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] < 5);
        af_local_light local = *af_scene_local_light_at(s->scene, 0);
        local.range = 10;
        local.enabled = false;
        CHECK(af_scene_set_local_light(s->scene, 0, local));
        capture(engine, s);
    }
    if (s->step == 18) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] < 5);
        af_local_light local = *af_scene_local_light_at(s->scene, 0);
        local.type = AF_LIGHT_SPOT;
        local.enabled = true;
        local.direction = (af_vec3){0, 0, -1};
        CHECK(af_scene_set_local_light(s->scene, 0, local));
        capture(engine, s);
    }
    if (s->step == 19) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] > 230);
        af_local_light local = *af_scene_local_light_at(s->scene, 0);
        local.direction = (af_vec3){1, 0, 0};
        CHECK(af_scene_set_local_light(s->scene, 0, local));
        capture(engine, s);
    }
    if (s->step == 20) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] < 5);
        af_local_light local = *af_scene_local_light_at(s->scene, 0);
        float angle = 22.5f * AF_PI / 180;
        local.direction = (af_vec3){sinf(angle), 0, -cosf(angle)};
        CHECK(af_scene_set_local_light(s->scene, 0, local));
        capture(engine, s);
    }
    if (s->step == 21) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] > 80 && p[0] < 220);
        CHECK(af_scene_remove_local_light(s->scene, 0));
        capture(engine, s);
    }
    if (s->step == 22) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] < 5);
    }
    if (s->step == 23) {
        material = af_material_default();
        material.unlit = true;
        snprintf(material.texture, sizeof(material.texture), "checker");
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        capture(engine, s);
    }
    if (s->step == 24) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] >= 180 && p[0] <= 195 && abs((int)p[0] - (int)p[1]) < 3);
        af_engine_stats stats = af_engine_get_stats(engine);
        CHECK(stats.cpu_events_ms > 0 && stats.cpu_update_ms > 0 && stats.cpu_render_ms > 0);
        CHECK(!stats.gpu_timing_supported ||
              (stats.gpu_timing_valid && isfinite(stats.gpu_frame_ms) && stats.gpu_frame_ms > 0 &&
               stats.gpu_shadow_ms >= 0 && stats.gpu_scene_ms >= 0 && stats.gpu_overlay_ms >= 0));
        printf("GPU frame %.3f ms; shadows %.3f / scene %.3f / overlay %.3f\n", stats.gpu_frame_ms, stats.gpu_shadow_ms,
               stats.gpu_scene_ms, stats.gpu_overlay_ms);
        material = af_material_default();
        material.physically_based = true;
        material.metallic = 1;
        material.roughness = .9f;
        material.base_color = (af_color){.25f, .12f, .06f, 1};
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        af_directional_light sun = af_scene_get_light(s->scene);
        sun.intensity = 1;
        CHECK(af_scene_set_light(s->scene, sun));
        capture(engine, s);
    }
    if (s->step == 25) {
        CHECK(af_engine_test_read_pixel(engine, s->metal));
        const unsigned char surface[] = {255, 32, 255, 255};
        CHECK(af_engine_upload_normal_map(engine, "surface", surface, 1, 1));
        snprintf(material.metallic_roughness_map, sizeof(material.metallic_roughness_map), "surface");
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        af_texture_info info;
        CHECK(af_engine_texture_info(engine, "surface", &info) && !info.srgb);
        capture(engine, s);
    }
    if (s->step == 26) {
        CHECK(af_engine_test_read_pixel(engine, p));
        CHECK(p[0] > s->metal[0] + 30);
        af_engine_request_quit(engine);
    }
}
static void shutdown(af_engine *engine, void *data) {
    state *s = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(s->scene);
}
int main(void) {
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight material regression";
    config.width = 640;
    config.height = 480;
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    state s = {0};
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    const char *required = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
    if (required && !strcmp(required, "1") && !stats.validation_enabled)
        return 1;
    if (result || s.failed || stats.validation_errors)
        return 1;
    puts("Rendering: NPOT texture diagnostics, PBR roughness/metallic, emission, normal maps/fallback and CPU/GPU "
         "profiling passed.");
    return 0;
}

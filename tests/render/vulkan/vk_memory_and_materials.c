/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include "render/render_test.h"
#include "test_custom_material_frag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct state {
    af_scene *scene;
    af_object_id receiver;
    unsigned ticks, phase;
    bool failed;
    bool expect_staged;
    unsigned char lit_pixel[4];
} state;

#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "vk_memory_and_materials phase %u line %d: %s\n", s->phase, __LINE__, #c);                 \
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
    const char *forced = getenv("AFTERLIGHT_FORCE_STAGING_UPLOAD");
    s->expect_staged = forced && forced[0] && strcmp(forced, "0") != 0;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;

    /* 1. Upload 64 distinct meshes + 16 distinct 64x64 textures to exercise Vulkan memory suballocation. */
    af_mesh_data quad = {quad_vertices, 4, quad_indices, 6};
    if (!af_engine_upload_mesh(engine, "surface", &quad))
        return false;
    for (unsigned i = 0; i < 64; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "pool_mesh_%u", i);
        if (!af_engine_upload_mesh(engine, name, &quad))
            return false;
    }
    unsigned char pixels[64 * 64 * 4];
    memset(pixels, 200, sizeof(pixels));
    for (unsigned i = 0; i < 16; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "pool_tex_%u", i);
        if (!af_engine_upload_texture(engine, name, pixels, 64, 64))
            return false;
    }

    af_engine_stats stats = af_engine_get_stats(engine);
    printf("Suballocator stats after 65 meshes + 16 textures: vk_allocations=%llu staged_uploads=%llu\n",
           (unsigned long long)stats.vk_allocations, (unsigned long long)stats.vk_staged_uploads);
    if (stats.vk_allocations == 0 || stats.vk_allocations > 32) {
        fprintf(stderr, "Expected suballocated vk_allocations in [1..32], got %llu\n",
                (unsigned long long)stats.vk_allocations);
        return false;
    }
    if (s->expect_staged && stats.vk_staged_uploads < 64) {
        fprintf(stderr, "Expected >=64 staged uploads when AFTERLIGHT_FORCE_STAGING_UPLOAD=1, got %llu\n",
                (unsigned long long)stats.vk_staged_uploads);
        return false;
    }

    s->receiver = af_scene_add_object(s->scene, "Receiver", "surface");
    af_material material = af_material_default();
    material.base_color = (af_color){0.8f, 0.8f, 0.8f, 1.0f};
    material.roughness = 0.9f;
    af_scene_set_object_material(s->scene, s->receiver, material);

    /* Turn off directional sun and ambient IBL so only local lights illuminate the receiver. */
    af_directional_light sun = af_directional_light_default();
    sun.intensity = 0;
    sun.ambient = 0;
    af_scene_set_light(s->scene, sun);
    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.enabled = false;
    af_scene_set_shadows(s->scene, shadows);
    af_render_settings rs = af_scene_get_render_settings(s->scene);
    rs.environment_enabled = false;
    rs.tone_mapping = false;
    af_scene_set_render_settings(s->scene, rs);

    /* 2. Add 100 local lights (> 16 legacy limit). Lights 0..95 are far away; light #96 (uvec4 word[3])
          illuminates the receiver quad brightly in green! */
    for (unsigned i = 0; i < 100; ++i) {
        af_local_light light = af_local_light_default(AF_LIGHT_POINT);
        snprintf(light.name, sizeof(light.name), "Light_%u", i);
        if (i == 96) {
            light.position = (af_vec3){0.0f, 0.0f, 2.0f};
            light.color = (af_vec3){0.0f, 1.0f, 0.0f};
            light.intensity = 12.0f;
            light.range = 8.0f;
        } else {
            light.position = (af_vec3){40.0f + (float)(i % 10) * 5.0f, 0.0f, -20.0f - (float)(i / 10) * 5.0f};
            light.color = (af_vec3){1.0f, 0.2f, 0.2f};
            light.intensity = 10.0f;
            light.range = 3.0f;
        }
        if (!af_scene_add_local_light(s->scene, light))
            return false;
    }

    /* Verify custom shader validation rejects invalid SPIR-V. */
    uint32_t bad_spirv[8] = {0x12345678u, 0, 0, 0, 0, 0, 0, 0};
    if (af_engine_upload_material_shader(engine, "bad", bad_spirv, 8) ||
        af_engine_upload_material_shader(engine, "", af_test_custom_material_frag,
                                         sizeof(af_test_custom_material_frag) / sizeof(uint32_t)))
        return false;

    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 5};
    camera.yaw = -AF_PI * 0.5f;
    camera.pitch = 0;
    if (!af_engine_set_camera(engine, &camera))
        return false;
    af_engine_set_clear_color(engine, (af_color){0, 0, 0, 1});
    af_engine_set_scene(engine, s->scene);
    return true;
}

static void update(af_engine *engine, double delta, void *data) {
    (void)delta;
    state *s = data;
    if (++s->ticks < 4)
        return;
    unsigned char p[4] = {0};
    if (s->phase > 0)
        CHECK(af_engine_test_read_pixel(engine, p));

    af_vec2 center = {320, 240};
    switch (s->phase) {
    case 0:
        /* Capture pixel lit by local light #96 (uvec4 tile mask word 3). */
        break;
    case 1:
        memcpy(s->lit_pixel, p, 4);
        printf("Light #96 lit pixel: RGBA=(%u, %u, %u, %u)\n", p[0], p[1], p[2], p[3]);
        CHECK(p[1] > 150 && p[1] > p[0] + 60 && p[1] > p[2] + 60);
        /* Disable light #96; receiver should go dark. */
        {
            af_local_light l96 = *af_scene_local_light_at(s->scene, 96);
            l96.enabled = false;
            CHECK(af_scene_set_local_light(s->scene, 96, l96));
        }
        break;
    case 2:
        printf("Light #96 disabled pixel: RGBA=(%u, %u, %u, %u)\n", p[0], p[1], p[2], p[3]);
        CHECK(p[1] < 20 && p[0] < 20 && p[2] < 20);
        /* Upload custom magenta SPIR-V material shader and assign to receiver. */
        CHECK(af_engine_upload_material_shader(engine, "test_magenta", af_test_custom_material_frag,
                                               sizeof(af_test_custom_material_frag) / sizeof(uint32_t)));
        {
            af_material mat = af_scene_get_object(s->scene, s->receiver)->material;
            snprintf(mat.shader, sizeof(mat.shader), "%s", "test_magenta");
            CHECK(af_scene_set_object_material(s->scene, s->receiver, mat));
        }
        break;
    case 3:
        printf("Custom shader pixel: RGBA=(%u, %u, %u, %u)\n", p[0], p[1], p[2], p[3]);
        CHECK(p[0] > 200 && p[1] < 30 && p[2] > 200);
        /* Trigger swapchain rebuild by toggling bloom on; custom pipeline must survive. */
        {
            af_render_settings rs = af_scene_get_render_settings(s->scene);
            rs.bloom = !rs.bloom;
            af_scene_set_render_settings(s->scene, rs);
        }
        break;
    case 4:
        printf("Custom shader post-rebuild pixel: RGBA=(%u, %u, %u, %u)\n", p[0], p[1], p[2], p[3]);
        CHECK(p[0] > 200 && p[1] < 40 && p[2] > 200);
        /* Remove custom shader; renderer must cleanly fall back to built-in pipeline (dark). */
        CHECK(af_engine_remove_material_shader(engine, "test_magenta"));
        break;
    case 5:
        printf("Fallback after shader removal pixel: RGBA=(%u, %u, %u, %u)\n", p[0], p[1], p[2], p[3]);
        CHECK(p[0] < 30 && p[1] < 30 && p[2] < 30);
        CHECK(af_engine_get_stats(engine).validation_errors == 0);
        af_engine_request_quit(engine);
        return;
    default:
        break;
    }

    CHECK(af_engine_test_capture_pixel(engine, center));
    s->ticks = 0;
    ++s->phase;
}

static void shutdown_test(af_engine *engine, void *data) {
    state *s = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(s->scene);
}

int main(void) {
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight Memory & Custom Materials Test";
    config.width = 640;
    config.height = 480;
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    state s = {0};
    af_game game = {.init = init, .update = update, .shutdown = shutdown_test, .user_data = &s};
    int code = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    if (code != 0 || s.failed || stats.validation_errors != 0 || s.phase != 5)
        return 1;
    puts("vk_memory_and_materials: suballocation, staged uploads, 100+ tiled local lights, and custom SPIR-V "
         "material shaders passed.");
    return 0;
}

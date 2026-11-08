/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include "render_test.h"
#include "cube.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
    af_scene *scene;
    af_object_id receiver, caster;
    unsigned ticks, phase;
    bool failed;
    unsigned char lit[4], environment[4], halo[4];
} state;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Lighting phase %u line %d: %s\n", s->phase, __LINE__, #c);                                \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static const af_vertex vertices[] = {{.position = {-2, -2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}},
                                     {.position = {2, -2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}},
                                     {.position = {2, 2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}},
                                     {.position = {-2, 2, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}}};
static const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
static const af_overlay_vertex ui_vertices[] = {{{300, 220}, {0, 0}, {255, 255, 255, 255}},
                                                {{340, 220}, {1, 0}, {255, 255, 255, 255}},
                                                {{340, 260}, {1, 1}, {255, 255, 255, 255}},
                                                {{300, 260}, {0, 1}, {255, 255, 255, 255}}};
static af_overlay_command ui_command = {.clip = {0, 0, 720, 500}, .index_count = 6, .texture = 2};
static bool init(af_engine *engine, void *data) {
    state *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    af_mesh_data quad = {vertices, 4, indices, 6}, cube = cube_mesh_data();
    if (!af_engine_upload_mesh(engine, "surface", &quad) || !af_engine_upload_mesh(engine, "cube", &cube))
        return false;
    s->receiver = af_scene_add_object(s->scene, "Receiver", "surface");
    s->caster = af_scene_add_object(s->scene, "Caster", "cube");
    af_transform t = af_transform_identity();
    t.position = (af_vec3){1, 0, 1.5f};
    t.scale = (af_vec3){.7f, .7f, .7f};
    af_scene_set_object_transform(s->scene, s->caster, t);
    af_material material = af_material_default();
    material.base_color = (af_color){.6f, .6f, .6f, 1};
    af_scene_set_object_material(s->scene, s->receiver, material);
    af_directional_light sun = af_directional_light_default();
    sun.intensity = 0;
    sun.ambient = 0;
    af_scene_set_light(s->scene, sun);
    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.enabled = false;
    shadows.resolution = 512;
    af_scene_set_shadows(s->scene, shadows);
    af_local_light light = af_local_light_default(AF_LIGHT_POINT);
    light.position = (af_vec3){2, 0, 3};
    light.intensity = 30;
    light.range = 12;
    light.shadow_bias = .0002f;
    af_scene_add_local_light(s->scene, light);
    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 6};
    camera.yaw = -AF_PI * .5f;
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
    if (s->phase)
        CHECK(af_engine_test_read_pixel(engine, p));
    af_local_light light = *af_scene_local_light_at(s->scene, 0);
    af_material material = af_scene_get_object(s->scene, s->receiver)->material;
    af_render_settings render = af_scene_get_render_settings(s->scene);
    af_vec2 sample = {320, 240};
    switch (s->phase) {
    case 0:
        break;
    case 1:
        memcpy(s->lit, p, 4);
        CHECK(p[0] > 150);
        light.cast_shadows = true;
        break;
    case 2:
        printf("Point receiver lit %u / shadow %u\n", s->lit[0], p[0]);
        CHECK(p[0] + 70 < s->lit[0]);
        CHECK(af_engine_get_stats(engine).shadow_draw_calls > 0);
        {
            af_material caster = af_scene_get_object(s->scene, s->caster)->material;
            caster.cast_shadows = false;
            CHECK(af_scene_set_object_material(s->scene, s->caster, caster));
        }
        break;
    case 3:
        CHECK(abs((int)p[0] - s->lit[0]) < 8);
        {
            af_material caster = af_scene_get_object(s->scene, s->caster)->material;
            caster.cast_shadows = true;
            CHECK(af_scene_set_object_material(s->scene, s->caster, caster));
        }
        CHECK(af_scene_set_object_visible(s->scene, s->caster, false));
        break;
    case 4:
        CHECK(abs((int)p[0] - s->lit[0]) < 8);
        CHECK(af_scene_set_object_visible(s->scene, s->caster, true));
        material.receive_shadows = false;
        break;
    case 5:
        CHECK(abs((int)p[0] - s->lit[0]) < 8);
        material.receive_shadows = true;
        light.type = AF_LIGHT_SPOT;
        light.direction = (af_vec3){-2, 0, -3};
        light.inner_angle = .4f;
        light.outer_angle = .65f;
        break;
    case 6:
        printf("Spot receiver shadow %u\n", p[0]);
        CHECK(p[0] + 70 < s->lit[0]);
        light.cast_shadows = false;
        break;
    case 7:
        CHECK(p[0] > 150);
        light.cast_shadows = true;
        render.local_shadow_resolution = 1024;
        break;
    case 8:
        CHECK(p[0] < 30);
        light.enabled = false;
        material.physically_based = true;
        material.metallic = 1;
        material.roughness = .08f;
        break;
    case 9:
        CHECK(p[0] < 5);
        render.environment_enabled = true;
        render.environment_intensity = 1;
        break;
    case 10:
        memcpy(s->environment, p, 4);
        CHECK(p[0] > 30 && p[2] > 30);
        render.environment_rotation = 2.35f;
        break;
    case 11:
        printf("IBL blue %u / rotated %u\n", s->environment[2], p[2]);
        CHECK(abs((int)p[2] - s->environment[2]) > 15);
        render.environment_profile = 1;
        break;
    case 12:
        CHECK(p[0] > p[2] + 15);
        render.environment_enabled = false;
        material = af_material_default();
        material.unlit = true;
        material.base_color = (af_color){0, 0, 0, 1};
        material.emission = (af_vec3){1, .2f, .05f};
        material.emission_strength = 16;
        {
            af_transform t = af_transform_identity();
            t.scale = (af_vec3){.2f, .2f, .2f};
            CHECK(af_scene_set_object_transform(s->scene, s->receiver, t));
        }
        CHECK(af_scene_set_object_visible(s->scene, s->caster, false));
        render.tone_mapping = true;
        render.bloom = false;
        sample.x = 352;
        break;
    case 13:
        memcpy(s->halo, p, 4);
        CHECK(p[0] < 5);
        render.bloom = true;
        render.bloom_strength = .5f;
        render.bloom_threshold = 1;
        render.bloom_radius = 2;
        sample.x = 352;
        break;
    case 14:
        printf("Bloom halo %u / off %u\n", p[0], s->halo[0]);
        CHECK(p[0] > s->halo[0] + 20 && p[0] > p[2] + 10);
        render.bloom_threshold = 100;
        sample.x = 352;
        break;
    case 15:
        CHECK(p[0] < 5);
        render.bloom = false;
        render.exposure = -4;
        break;
    case 16:
        CHECK(p[0] > 175 && p[0] < 200);
        render.exposure = -5;
        break;
    case 17:
        CHECK(p[0] > 145 && p[0] < 165);
        CHECK(af_engine_set_window_size(engine, 720, 500));
        break;
    case 18: {
        const unsigned char white[] = {255, 255, 255, 255}, image[] = {80, 140, 210, 255};
        CHECK(af_engine_upload_overlay_atlas(engine, white, 1, 1));
        CHECK(!af_engine_upload_overlay_texture(engine, 1, image, 1, 1));
        CHECK(af_engine_upload_overlay_texture(engine, 2, image, 1, 1));
        af_overlay_frame ui = {.vertices = ui_vertices,
                               .indices = indices,
                               .commands = &ui_command,
                               .vertex_count = 4,
                               .index_count = 6,
                               .command_count = 1,
                               .width = 720,
                               .height = 500};
        CHECK(af_engine_set_overlay(engine, &ui));
        render.exposure = 4;
        render.bloom = true;
        render.bloom_threshold = 0;
        break;
    }
    case 19:
        CHECK(abs((int)p[0] - 80) < 3 && abs((int)p[1] - 140) < 3 && abs((int)p[2] - 210) < 3);
        {
            const unsigned char image[] = {16, 230, 40, 255};
            CHECK(af_engine_upload_overlay_texture(engine, 3, image, 1, 1));
            ui_command.texture = 3;
        }
        break;
    case 20:
        CHECK(p[0] < 30 && p[1] > 220 && p[2] < 50);
        CHECK(af_engine_remove_overlay_texture(engine, 2));
        {
            const unsigned char image[] = {220, 95, 10, 255};
            CHECK(af_engine_upload_overlay_texture(engine, 3, image, 1, 1));
        }
        break;
    case 21:
        CHECK(p[0] > 210 && p[1] > 85 && p[1] < 105 && p[2] < 20);
        CHECK(af_engine_remove_overlay_texture(engine, 3));
        CHECK(af_engine_set_overlay(engine, NULL));
        render.local_shadow_resolution = 256;
        light.enabled = true;
        light.type = AF_LIGHT_POINT;
        light.cast_shadows = true;
        for (unsigned i = 0; i < 4; ++i) {
            af_local_light extra = light;
            extra.position.x += (float)i + 1;
            CHECK(af_scene_add_local_light(s->scene, extra));
        }
        break;
    case 22: {
        af_engine_stats stats = af_engine_get_stats(engine);
        CHECK(stats.hdr_enabled && stats.local_shadow_lights == 4 && stats.local_shadow_maps == 24 &&
              stats.local_shadow_overflow == 1);
        light.type = AF_LIGHT_SPOT;
        const unsigned char image[] = {25, 75, 180, 255};
        CHECK(af_engine_upload_overlay_texture(engine, 4, image, 1, 1));
        ui_command.texture = 4;
        break;
    }
    case 23: {
        CHECK(af_engine_get_stats(engine).local_shadow_maps == 19);
        CHECK(p[0] > 20 && p[0] < 30 && p[1] > 70 && p[1] < 80 && p[2] > 175 && p[2] < 185);
        const unsigned char image[] = {160, 45, 90, 255};
        CHECK(af_engine_upload_overlay_texture(engine, 4, image, 1, 1));
        break;
    }
    case 24:
        CHECK(p[0] > 155 && p[0] < 165 && p[1] > 40 && p[1] < 50 && p[2] > 85 && p[2] < 95);
        CHECK(af_engine_remove_overlay_texture(engine, 4));
        CHECK(af_engine_set_overlay(engine, NULL));
        break;
    case 25:
        CHECK(af_engine_get_stats(engine).shadow_cached_passes > 0 && af_engine_get_stats(engine).shadow_passes == 0);
        af_engine_request_quit(engine);
        return;
    default:
        CHECK(false);
    }
    if ((s->phase >= 18 && s->phase < 21) || (s->phase >= 22 && s->phase < 24)) {
        af_overlay_frame ui = {.vertices = ui_vertices,
                               .indices = indices,
                               .commands = &ui_command,
                               .vertex_count = 4,
                               .index_count = 6,
                               .command_count = 1,
                               .width = 720,
                               .height = 500};
        CHECK(af_engine_set_overlay(engine, &ui));
    }
    CHECK(af_scene_set_local_light(s->scene, 0, light));
    CHECK(af_scene_set_object_material(s->scene, s->receiver, material));
    CHECK(af_scene_set_render_settings(s->scene, render));
    CHECK(af_engine_test_capture_pixel(engine, sample));
    ++s->phase;
}
static void shutdown(af_engine *engine, void *data) {
    state *s = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(s->scene);
}
int main(void) {
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight HDR / IBL / local shadow regression";
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
    if (result || s.failed || stats.validation_errors || s.phase != 25)
        return 1;
    puts("Lighting: HDR highlight recovery, rotating/profiled IBL, point/spot cast/receive/visibility, shadow resize, "
         "bloom halo/threshold and swapchain resize passed.");
    return 0;
}

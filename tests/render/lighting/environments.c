/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include "render/render_test.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
    const char *path;
    af_scene *scene;
    unsigned phase, ticks, waiting;
    bool failed, loaded;
    unsigned char sky[4], dark[4], surface[4];
    af_object_id object;
} state;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Environment phase %u line %d: %s\n", s->phase, __LINE__, #c);                             \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static bool init(af_engine *engine, void *data) {
    state *s = data;
    s->scene = af_scene_create();
    af_asset_error error;
    if (!s->scene || !af_scene_set_asset(s->scene, "panorama", AF_ASSET_ENVIRONMENT, s->path) ||
        !af_engine_start_environment(engine, "panorama", s->path, &error))
        return false;
    af_render_settings render = af_render_settings_default();
    strcpy(render.environment_asset, "panorama");
    render.skybox = true;
    render.environment_enabled = true;
    render.tone_mapping = true;
    if (!af_scene_set_render_settings(s->scene, render))
        return false;
    af_shadow_settings shadow = af_shadow_settings_default();
    shadow.enabled = false;
    shadow.resolution = 512;
    af_scene_set_shadows(s->scene, shadow);
    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 4};
    camera.pitch = 0;
    camera.yaw = -AF_PI / 2;
    af_engine_set_camera(engine, &camera);
    af_engine_set_scene(engine, s->scene);
    af_engine_set_clear_color(engine, (af_color){0, 0, 0, 1});
    return true;
}
static void update(af_engine *engine, double delta, void *data) {
    (void)delta;
    state *s = data;
    if (!s->loaded) {
        af_environment_job_status job = af_engine_environment_status(engine);
        CHECK(++s->waiting < 1200 && job.state != AF_ENV_FAILED && job.state != AF_ENV_CANCELED);
        if (job.state != AF_ENV_COMPLETE)
            return;
        s->loaded = true;
    }
    if (++s->ticks < 3)
        return;
    unsigned char p[4] = {0};
    if (s->phase)
        CHECK(af_engine_test_read_pixel(engine, p));
    af_render_settings render = af_scene_get_render_settings(s->scene);
    af_camera camera = af_engine_get_camera(engine);
    af_vec2 point = {200, 150};
    af_asset_error error;
    switch (s->phase) {
    case 0:
        break;
    case 1:
        memcpy(s->sky, p, 4);
        CHECK(p[2] > 100);
        camera.position = (af_vec3){100, 20, 100};
        break;
    case 2:
        CHECK(abs((int)p[2] - s->sky[2]) <= 2);
        render.environment_rotation = AF_PI;
        break;
    case 3:
        CHECK(abs((int)p[0] - s->sky[0]) + abs((int)p[2] - s->sky[2]) > 10);
        render.environment_rotation = 0;
        render.skybox_intensity = .1f;
        break;
    case 4:
        CHECK(p[2] + 50 < s->sky[2]);
        memcpy(s->dark, p, 4);
        render.environment_intensity = 10;
        break;
    case 5:
        CHECK(abs((int)p[2] - s->dark[2]) <= 2);
        render.skybox = false;
        break;
    case 6: {
        CHECK(p[0] < 3 && p[2] < 3);
        camera.position = (af_vec3){0, 0, 4};
        const af_vertex vertices[] = {{{-2, -2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {0, 0}},
                                      {{2, -2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {1, 0}},
                                      {{2, 2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {1, 1}},
                                      {{-2, 2, 0}, {1, 1, 1, 1}, {0, 0, 1}, {0, 1}}};
        const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
        af_mesh_data mesh = {vertices, 4, indices, 6};
        CHECK(af_engine_upload_mesh(engine, "quad", &mesh));
        s->object = af_scene_add_object(s->scene, "Metal", "quad");
        af_material material = af_material_default();
        material.physically_based = true;
        material.metallic = 1;
        material.roughness = .3f;
        CHECK(af_scene_set_object_material(s->scene, s->object, material));
        af_directional_light sun = af_directional_light_default();
        sun.intensity = 0;
        sun.ambient = 0;
        CHECK(af_scene_set_light(s->scene, sun));
        render.environment_intensity = 1;
        break;
    }
    case 7:
        CHECK(p[2] > 30);
        memcpy(s->surface, p, 4);
        CHECK(!af_engine_load_environment(engine, "panorama", "missing-panorama.hdr", &error));
        break;
    case 8:
        CHECK(abs((int)p[2] - s->surface[2]) <= 2);
        CHECK(af_engine_reload_asset_source(engine, s->scene, s->path, &error));
        break;
    case 9:
        CHECK(abs((int)p[2] - s->surface[2]) <= 2);
        render.environment_enabled = false;
        break;
    case 10:
        CHECK(p[0] < 3 && p[2] < 3);
        render.skybox = true;
        render.skybox_intensity = 1;
        CHECK(af_scene_set_object_visible(s->scene, s->object, false));
        af_rect viewport = {40, 30, 320, 240};
        af_engine_set_viewport(engine, &viewport);
        point = (af_vec2){10, 10};
        break;
    case 11:
        CHECK(p[0] < 3 && p[2] < 3);
        break;
    case 12:
        CHECK(p[2] > 100);
        CHECK(af_engine_set_window_size(engine, 480, 320));
        break;
    case 13:
        CHECK(af_engine_remove_texture(engine, "panorama"));
        render.environment_asset[0] = 0;
        render.environment_profile = 1;
        break;
    case 14:
        CHECK(p[0] > p[2]);
        af_engine_request_quit(engine);
        return;
    default:
        CHECK(false);
    }
    CHECK(af_scene_set_render_settings(s->scene, render));
    CHECK(af_engine_set_camera(engine, &camera));
    CHECK(af_engine_test_capture_pixel(engine, point));
    ++s->phase;
}
static void shutdown(af_engine *engine, void *data) {
    state *s = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(s->scene);
}
int main(int argc, char **argv) {
    if (argc != 2)
        return 1;
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight HDR import and skybox regression";
    config.width = 400;
    config.height = 300;
    config.enable_validation = true;
    state s = {.path = argv[1]};
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    if (result || s.failed || s.phase != 14 || stats.validation_errors)
        return 1;
    puts("Environment: imported HDR sky/IBL, independent brightness, translation invariance, rotation, transactional "
         "reload, viewport clipping, resize and procedural fallback passed.");
    return 0;
}

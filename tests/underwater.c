/* SPDX-License-Identifier: MPL-2.0 */
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct test {
    af_scene *scene;
    unsigned phase, ticks;
    bool failed;
    af_object_id plane;
    unsigned char deep[4], near_surface[4];
} test;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Underwater phase %u line %d: %s (%u,%u,%u)\n", s->phase, __LINE__, #x, p[0], p[1], p[2]); \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static bool init(af_engine *e, void *data) {
    test *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    const char *aa = getenv("AFTERLIGHT_TEST_AA");
    if (aa) {
        af_render_settings r = af_scene_get_render_settings(s->scene);
        r.anti_aliasing = !strcmp(aa, "taa") ? AF_AA_TAA : !strcmp(aa, "smaa") ? AF_AA_SMAA : AF_AA_FXAA;
        if (!af_scene_set_render_settings(s->scene, r))
            return false;
    }
    af_object_id id = af_scene_add_object(s->scene, "Pool", "__node");
    af_water w = af_water_default();
    w.half_size = (af_vec2){10, 10};
    w.depth = 6;
    w.wind = 0;
    w.absorption = (af_vec3){.8f, .1f, .01f};
    w.scattering = (af_vec3){0, 0, 0};
    if (!af_scene_set_water(s->scene, id, w))
        return false;
    af_vertex v[] = {{.position = {-20, -20, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}},
                     {.position = {20, -20, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}},
                     {.position = {20, 20, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}},
                     {.position = {-20, 20, 0}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}}};
    uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    af_mesh_data mesh = {v, 4, indices, 6};
    if (!af_engine_upload_mesh(e, "quad", &mesh))
        return false;
    s->plane = af_scene_add_object(s->scene, "White wall", "quad");
    af_material m = af_material_default();
    m.unlit = true;
    if (!af_scene_set_object_material(s->scene, s->plane, m))
        return false;
    af_transform t = af_transform_identity();
    t.position.z = -4;
    af_scene_set_object_transform(s->scene, s->plane, t);
    af_camera c = af_camera_default();
    c.position = (af_vec3){0, 1, 4};
    c.yaw = -AF_PI / 2;
    c.pitch = 0;
    af_engine_set_camera(e, &c);
    af_engine_set_scene(e, s->scene);
    af_engine_set_simulation_paused(e, true);
    return true;
}
static void update(af_engine *e, double dt, void *data) {
    (void)dt;
    test *s = data;
    if (++s->ticks < 4)
        return;
    unsigned char p[4] = {0};
    if (s->phase)
        CHECK(af_engine_test_read_pixel(e, p));
    af_camera c = af_engine_get_camera(e);
    af_render_settings render = af_scene_get_render_settings(s->scene);
    switch (s->phase) {
    case 0:
        break;
    case 1:
        CHECK(p[0] > 240 && p[2] > 240);
        c.position.y = -1;
        break;
    case 2:
        CHECK(p[0] + 100 < p[2] && p[2] > 200);
        memcpy(s->deep, p, 4);
        render.underwater = false;
        break;
    case 3:
        CHECK(p[0] > 240 && p[2] > 240);
        render.underwater = true;
        c.position.y = -.01f;
        break;
    case 4:
        memcpy(s->near_surface, p, 4);
        CHECK(p[0] > s->deep[0] + 50);
        c.position.y = .01f;
        break;
    case 5:
        CHECK(abs((int)p[0] - s->near_surface[0]) < 50);
        c.position.y = .2f;
        break;
    case 6:
        CHECK(p[0] > 240 && p[2] > 240);
        c.position = (af_vec3){12, -1, 4};
        break;
    case 7:
        CHECK(p[0] > 240 && p[2] > 240);
        CHECK(af_scene_save(s->scene, "underwater.json", NULL));
        {
            af_scene *copy = af_scene_load("underwater.json", NULL);
            CHECK(copy && af_scene_get_render_settings(copy).underwater);
            af_scene_destroy(copy);
            remove("underwater.json");
        }
        puts("Underwater: absorption, disable, surface continuity, region bounds and saved settings "
             "passed.");
        af_engine_request_quit(e);
        return;
    default:
        CHECK(false);
    }
    CHECK(af_scene_set_render_settings(s->scene, render) && af_engine_set_camera(e, &c) &&
          af_engine_test_capture_pixel(e, (af_vec2){200, 150}));
    ++s->phase;
}
static void shutdown(af_engine *e, void *data) {
    test *s = data;
    af_engine_set_scene(e, NULL);
    af_scene_destroy(s->scene);
}
int main(void) {
    test s = {0};
    af_engine_config c = af_engine_config_default();
    c.width = 400;
    c.height = 300;
    c.enable_validation = true;
    c.title = "Afterlight underwater optics";
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game g = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int r = af_engine_run(e, &g);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return r || s.failed || s.phase != 7 || stats.validation_errors ? 1 : 0;
}

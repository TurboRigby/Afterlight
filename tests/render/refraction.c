/* SPDX-License-Identifier: MPL-2.0 */
#include "render/render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct test {
    af_scene *scene;
    af_object_id owner, guard;
    unsigned phase, ticks;
    bool failed;
    unsigned char straight[4], bent[4];
} test;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Refraction phase %u line %d: %s (%u,%u,%u)\n", s->phase, __LINE__, #x, p[0], p[1], p[2]); \
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
    s->owner = af_scene_add_object(s->scene, "Pool", "__node");
    af_water w = af_water_default();
    w.half_size = (af_vec2){10, 10};
    w.depth = 2.4f;
    w.wind = 0;
    w.absorption = w.scattering = (af_vec3){0};
    w.ior = 1.5f;
    if (!af_scene_set_water(s->scene, s->owner, w))
        return false;
    af_vertex vertices[80] = {0};
    uint32_t indices[120];
    for (unsigned i = 0; i < 20; ++i) {
        float z = -10 + (float)i;
        af_color color = i >= 8 ? (af_color){.02f, .9f, .02f, 1} : (af_color){.9f, .02f, .02f, 1};
        af_vec3 p[] = {{-10, -2, z}, {10, -2, z}, {10, -2, z + 1}, {-10, -2, z + 1}};
        for (unsigned k = 0; k < 4; ++k)
            vertices[i * 4 + k] = (af_vertex){.position = p[k], .color = color, .normal = {0, 1, 0}};
        unsigned base = i * 4, j = i * 6;
        indices[j] = base;
        indices[j + 1] = base + 2;
        indices[j + 2] = base + 1;
        indices[j + 3] = base;
        indices[j + 4] = base + 3;
        indices[j + 5] = base + 2;
    }
    af_mesh_data mesh = {vertices, 80, indices, 120};
    if (!af_engine_upload_mesh(e, "stripes", &mesh))
        return false;
    af_vertex guard_v[4] = {{.position = {-.5f, 1, 2.1f}, .normal = {0, 1, 0}, .color = {.02f, .02f, .9f, 1}},
                            {.position = {.5f, 1, 2.1f}, .normal = {0, 1, 0}, .color = {.02f, .02f, .9f, 1}},
                            {.position = {.5f, 1, 2.8f}, .normal = {0, 1, 0}, .color = {.02f, .02f, .9f, 1}},
                            {.position = {-.5f, 1, 2.8f}, .normal = {0, 1, 0}, .color = {.02f, .02f, .9f, 1}}};
    uint32_t guard_i[] = {0, 2, 1, 0, 3, 2};
    af_mesh_data guard_mesh = {guard_v, 4, guard_i, 6};
    if (!af_engine_upload_mesh(e, "guard", &guard_mesh))
        return false;
    s->guard = af_scene_add_object(s->scene, "Foreground guard", "guard");
    af_material guard_m = af_material_default();
    guard_m.unlit = true;
    af_scene_set_object_material(s->scene, s->guard, guard_m);
    af_scene_set_object_visible(s->scene, s->guard, false);
    af_object_id floor = af_scene_add_object(s->scene, "Striped floor", "stripes");
    af_material m = af_material_default();
    m.unlit = true;
    if (!af_scene_set_object_material(s->scene, floor, m))
        return false;
    af_directional_light l = af_directional_light_default();
    l.intensity = l.ambient = 0;
    af_scene_set_light(s->scene, l);
    af_render_settings r = af_render_settings_default();
    const char *aa = getenv("AFTERLIGHT_TEST_AA");
    if (aa)
        r.anti_aliasing = !strcmp(aa, "taa") ? AF_AA_TAA : !strcmp(aa, "smaa") ? AF_AA_SMAA : AF_AA_FXAA;
    r.refraction = false;
    r.underwater = false;
    af_scene_set_render_settings(s->scene, r);
    af_camera c = af_camera_default();
    c.position = (af_vec3){0, 3, 5};
    c.pitch = -atan2f(3, 5);
    c.yaw = -AF_PI / 2;
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
    af_render_settings r = af_scene_get_render_settings(s->scene);
    af_water w = *af_scene_get_water(s->scene, s->owner);
    printf("Refraction pixel %u: %u %u %u\n", s->phase, p[0], p[1], p[2]);
    switch (s->phase) {
    case 0:
        break;
    case 1:
        memcpy(s->straight, p, 4);
        CHECK(p[0] > 150 && p[1] < 80);
        r.refraction = true;
        break;
    case 2:
        memcpy(s->bent, p, 4);
        CHECK(p[1] > 150 && p[0] < 80);
        CHECK(af_scene_set_object_visible(s->scene, s->guard, true));
        break;
    case 3:
        CHECK(p[0] > 150 && p[2] < 80);
        CHECK(af_scene_set_object_visible(s->scene, s->guard, false));
        w.ior = 1;
        break;
    case 4:
        CHECK(p[0] > 150 && p[1] < 80);
        w.ior = 1.5f;
        r.refraction_strength = 0;
        break;
    case 5:
        CHECK(p[0] > 150 && p[1] < 80);
        r.refraction_strength = 1;
        CHECK(af_engine_set_window_size(e, 480, 320));
        break;
    case 6:
        CHECK(af_engine_get_stats(e).swapchain_rebuilds >= 2);
        CHECK(af_scene_save(s->scene, "refraction.json", NULL));
        {
            af_scene *copy = af_scene_load("refraction.json", NULL);
            CHECK(copy && af_scene_get_render_settings(copy).refraction &&
                  af_scene_get_render_settings(copy).refraction_strength == 1);
            af_scene_destroy(copy);
            remove("refraction.json");
        }
        {
            const af_rect viewport = {20, 20, 240, 180};
            af_engine_set_viewport(e, &viewport);
        }
        break;
    case 7:
        CHECK(p[1] > 150 && p[0] < 80);
        w.ior = 1;
        break;
    case 8:
        CHECK(p[0] > 150 && p[1] < 80);
        w.ior = 1.5f;
        r.refraction = false;
        break;
    case 9:
        CHECK(p[0] > 150 && p[1] < 80);
        r.refraction = true;
        {
            const af_rect viewport = {210, 120, 240, 180};
            af_engine_set_viewport(e, &viewport);
        }
        break;
    case 10:
        CHECK(p[1] > 150 && p[0] < 80);
        w.ior = 1;
        break;
    case 11:
        CHECK(p[0] > 150 && p[1] < 80);
        puts("Refraction: submerged stripes shift with IOR, foreground rejection, toggle/strength, "
             "resize, inset/moved viewports and persistence pass.");
        af_engine_request_quit(e);
        return;
    default:
        CHECK(false);
    }
    CHECK(af_scene_set_water(s->scene, s->owner, w) && af_scene_set_render_settings(s->scene, r));
    af_vec2 point = s->phase >= 9   ? (af_vec2){330, 210}
                    : s->phase >= 6 ? (af_vec2){140, 110}
                    : s->phase >= 5 ? (af_vec2){240, 160}
                                    : (af_vec2){200, 150};
    CHECK(af_engine_test_capture_pixel(e, point));
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
    c.title = "Afterlight scene refraction";
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game g = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &g);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || s.failed || s.phase != 11 || stats.validation_errors ? 1 : 0;
}

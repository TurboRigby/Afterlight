/* SPDX-License-Identifier: MPL-2.0 */
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct state {
    af_scene *scene;
    unsigned phase, ticks;
    bool pending, failed;
    uint64_t bytes[6];
} state;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Render foundations phase %u line %d: %s\n", s->phase, __LINE__, #x);                      \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static bool init(af_engine *e, void *data) {
    state *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    af_vertex v[] = {{.position = {-2, -2, 0}, .color = {1, 0, 0, 1}},
                     {.position = {2, -2, 0}, .color = {1, 0, 0, 1}},
                     {.position = {0, 2, 0}, .color = {1, 0, 0, 1}}};
    uint32_t ix[] = {0, 1, 2};
    af_mesh_data mesh = {v, 3, ix, 3};
    if (!af_engine_upload_mesh(e, "triangle", &mesh))
        return false;
    af_object_id id = af_scene_add_object(s->scene, "Surface", "triangle");
    af_material m = af_material_default();
    m.unlit = true;
    if (!af_scene_set_object_material(s->scene, id, m))
        return false;
    af_engine_set_scene(e, s->scene);
    af_engine_set_simulation_paused(e, true);
    return true;
}
static void update(af_engine *e, double dt, void *data) {
    (void)dt;
    state *s = data;
    if (++s->ticks < 10)
        return;
    if (!s->pending) {
        int w, h;
        af_engine_get_window_size(e, &w, &h);
        CHECK(af_engine_test_capture_pixel(e, (af_vec2){(float)w / 2, (float)h / 2}));
        s->pending = true;
        return;
    }
    unsigned char p[4];
    CHECK(af_engine_test_read_pixel(e, p));
    af_engine_stats stats = af_engine_get_stats(e);
    CHECK(stats.post_bytes > 0 &&
          stats.tracked_gpu_bytes >= stats.post_bytes + stats.shadow_bytes + stats.refraction_bytes + stats.mesh_bytes);
    printf("Resources phase %u: post %llu tracked %llu pixel %u %u %u\n", s->phase,
           (unsigned long long)stats.post_bytes, (unsigned long long)stats.tracked_gpu_bytes, p[0], p[1], p[2]);
    if (s->phase < 6) {
        s->bytes[s->phase] = stats.post_bytes;
        CHECK(p[0] > 240 && p[1] < 5 && p[2] < 5);
        if (s->phase > 0 && s->phase < 5)
            CHECK(s->bytes[s->phase] > s->bytes[s->phase - 1]);
        if (s->phase == 5)
            CHECK(stats.post_bytes == s->bytes[0]);
    } else if (s->phase == 6)
        CHECK(p[0] > 50 && abs((int)p[0] - p[1]) < 3 && abs((int)p[0] - p[2]) < 3);
    else if (s->phase == 7)
        CHECK(p[0] > 100 && abs((int)p[0] - p[1]) < 3 && abs((int)p[0] - p[2]) < 3);
    else if (s->phase == 8)
        CHECK(p[0] > 150 && p[1] < 5 && p[2] < 5);
    else if (s->phase == 9)
        CHECK(p[1] > 100 && p[0] < 5 && p[2] < 5);
    else if (s->phase == 10) {
        CHECK(stats.gpu_memory_over_budget);
        CHECK(!af_engine_set_render_budget(e, NAN, 1024u * 1024u));
        CHECK(!af_engine_set_debug_view(e, AF_DEBUG_COUNT));
        CHECK(af_engine_set_render_budget(e, 1000, UINT64_C(1024) * 1024 * 1024));
    } else {
        CHECK(!stats.gpu_memory_over_budget);
        CHECK(af_engine_set_debug_view(e, AF_DEBUG_SHADED));
        puts("Renderer foundations: disabled allocations, AA/bloom switching, debug "
             "depth/motion/history/rejection, resize and memory budgets passed.");
        af_engine_request_quit(e);
        s->phase = 12;
        return;
    }
    ++s->phase;
    af_render_settings r = af_scene_get_render_settings(s->scene);
    r.anti_aliasing = s->phase <= 3 ? (af_anti_aliasing)s->phase : s->phase == 4 ? AF_AA_TAA : AF_AA_OFF;
    r.bloom = s->phase == 4;
    r.bloom_strength = 0;
    CHECK(af_scene_set_render_settings(s->scene, r));
    if (s->phase >= 6 && s->phase <= 9)
        CHECK(af_engine_set_debug_view(e, (af_debug_view)(s->phase - 5)));
    if (s->phase == 10) {
        CHECK(af_engine_set_window_size(e, 640, 480));
        CHECK(af_engine_set_render_budget(e, 1000, 1024u * 1024u));
    }
    s->ticks = 0;
    s->pending = false;
}
static void shutdown(af_engine *e, void *data) {
    state *s = data;
    af_engine_set_scene(e, NULL);
    af_scene_destroy(s->scene);
}
int main(void) {
    state s = {0};
    af_engine_config c = af_engine_config_default();
    c.width = 400;
    c.height = 300;
    c.enable_validation = true;
    c.title = "Afterlight renderer foundations";
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int r = af_engine_run(e, &game);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return r || s.failed || s.phase != 12 || stats.validation_errors ? 1 : 0;
}

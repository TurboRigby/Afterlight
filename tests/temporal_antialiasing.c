/* SPDX-License-Identifier: MPL-2.0 */
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIDE 64u
#define WIDTH 200u
#define HEIGHT 160u
typedef struct state {
    af_scene *scene;
    af_object_id edge, back, prop, skin;
    unsigned phase, ticks, samples;
    bool pending, failed;
    unsigned char previous[SIDE * SIDE * 4];
    double variation[2];
    float old_prop, old_skin, old_camera;
    uint64_t reset_count;
} state;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "TAA phase %u tick %u line %d: %s\n", s->phase, s->ticks, __LINE__, #x);                   \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static bool colored(af_engine *e, state *s, const char *name, const af_vec3 *positions, unsigned count,
                    const uint32_t *indices, unsigned icount, af_color color, bool skin, af_object_id *out) {
    af_vertex v[4] = {0};
    for (unsigned i = 0; i < count; ++i) {
        v[i].position = positions[i];
        v[i].color = color;
        if (skin)
            v[i].weights.x = 1;
    }
    af_mesh_data mesh = {v, count, indices, icount};
    if (!af_engine_upload_mesh(e, name, &mesh))
        return false;
    *out = af_scene_add_object(s->scene, name, name);
    af_material m = af_material_default();
    m.unlit = true;
    return af_scene_set_object_material(s->scene, *out, m);
}
static bool init(af_engine *e, void *data) {
    state *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    af_vec3 edge[] = {{-2, -1, 0}, {2, 1, 0}, {-2, 2, 0}};
    uint32_t triangle[] = {0, 1, 2}, quad[] = {0, 1, 2, 0, 2, 3};
    af_vec3 background[] = {{-20, -20, -1}, {20, -20, -1}, {20, 20, -1}, {-20, 20, -1}};
    af_vec3 box[] = {{-.14f, -.2f, 0}, {.14f, -.2f, 0}, {.14f, .2f, 0}, {-.14f, .2f, 0}};
    if (!colored(e, s, "edge", edge, 3, triangle, 3, (af_color){1, 1, 1, 1}, false, &s->edge) ||
        !colored(e, s, "background", background, 4, quad, 6, (af_color){0, 1, 0, 1}, false, &s->back) ||
        !colored(e, s, "prop", box, 4, quad, 6, (af_color){1, 0, 0, 1}, false, &s->prop) ||
        !colored(e, s, "skin", box, 4, quad, 6, (af_color){0, 0, 1, 1}, true, &s->skin))
        return false;
    af_scene_set_object_visible(s->scene, s->back, false);
    af_scene_set_object_visible(s->scene, s->prop, false);
    af_scene_set_object_visible(s->scene, s->skin, false);
    af_render_settings r = af_render_settings_default();
    r.anti_aliasing = AF_AA_TAA;
    r.aa_sharpness = 0;
    r.taa_history_weight = 0;
    if (!af_scene_set_render_settings(s->scene, r))
        return false;
    af_engine_set_scene(e, s->scene);
    af_engine_set_simulation_paused(e, true);
    af_engine_set_clear_color(e, (af_color){0, 0, 0, 1});
    return true;
}
static void update(af_engine *e, double dt, void *data) {
    (void)dt;
    state *s = data;
    ++s->ticks;
    af_temporal_test_stats stats = af_engine_test_temporal(e);
    if (s->ticks > 3)
        CHECK(stats.supported);
    if (s->phase < 2) {
        if (s->pending) {
            unsigned char pixels[SIDE * SIDE * 4];
            CHECK(af_engine_test_read_pixels(e, pixels, sizeof(pixels), NULL, NULL));
            if (s->samples)
                for (unsigned i = 0; i < sizeof(pixels); i += 4)
                    s->variation[s->phase] += abs((int)pixels[i] - s->previous[i]);
            memcpy(s->previous, pixels, sizeof(pixels));
            ++s->samples;
            s->pending = false;
            if (s->samples == 32) {
                printf("TAA weight %s edge variation: %.0f\n", s->phase ? ".9" : "0", s->variation[s->phase]);
                if (!s->phase) {
                    af_render_settings r = af_scene_get_render_settings(s->scene);
                    r.taa_history_weight = .9f;
                    CHECK(af_scene_set_render_settings(s->scene, r));
                } else {
                    CHECK(s->variation[0] > 100);
                    CHECK(s->variation[1] < s->variation[0] * .85);
                    CHECK(af_scene_set_object_visible(s->scene, s->edge, false));
                    CHECK(af_scene_set_object_visible(s->scene, s->back, true));
                    CHECK(af_scene_set_object_visible(s->scene, s->prop, true));
                    CHECK(af_scene_set_object_visible(s->scene, s->skin, true));
                }
                ++s->phase;
                s->ticks = s->samples = 0;
                return;
            }
        }
        if (s->ticks > 32) {
            CHECK(af_engine_test_capture_region(e, (af_vec2){184, 134}, SIDE, SIDE));
            s->pending = true;
        }
        return;
    }
    if (s->pending) {
        unsigned char pixels[WIDTH * HEIGHT * 4];
        CHECK(af_engine_test_read_pixels(e, pixels, sizeof(pixels), NULL, NULL));
        CHECK(stats.valid_objects >= 3 && stats.valid_skins == 1);
        float sx = (float)stats.width / 400.f, sy = (float)stats.height / 300.f;
        unsigned checked = 0, bad = 0;
        float tan_half = tanf(af_camera_default().vertical_fov * .5f), margin = 4 * 6 * tan_half / (float)stats.height;
        for (unsigned y = 0; y < HEIGHT; ++y)
            for (unsigned x = 0; x < WIDTH; ++x) {
                float nx = (2 * (150 * sx + (float)x + .5f) / (float)stats.width - 1) * tan_half * (4.f / 3.f),
                      ny = (1 - 2 * (110 * sy + (float)y + .5f) / (float)stats.height) * tan_half;
                float wx = nx * 3 + s->old_camera, wy = ny * 3;
                bool red = fabsf(wx - s->old_prop) < .14f && fabsf(wy + .25f) < .2f,
                     blue = fabsf(wx - s->old_skin) < .14f && fabsf(wy - .25f) < .2f;
                bool near_red = fabsf(wx - s->old_prop) < .14f + margin && fabsf(wy + .25f) < .2f + margin;
                bool near_blue = fabsf(wx - s->old_skin) < .14f + margin && fabsf(wy - .25f) < .2f + margin;
                if ((near_red &&
                     (!red || fabsf(wx - s->old_prop) > .14f - margin || fabsf(wy + .25f) > .2f - margin)) ||
                    (near_blue &&
                     (!blue || fabsf(wx - s->old_skin) > .14f - margin || fabsf(wy - .25f) > .2f - margin)))
                    continue;
                const unsigned char *p = &pixels[(y * WIDTH + x) * 4];
                unsigned channel = blue ? 2u : red ? 0u : 1u;
                bool good = p[channel] > 242 && p[(channel + 1) % 3] < 12 && p[(channel + 2) % 3] < 12;
                bad += !good;
                ++checked;
            }
        if (bad)
            fprintf(stderr, "TAA moving flat pixels: %u bad of %u\n", bad, checked);
        CHECK(checked > 2000 && bad < 4);
        ++s->samples;
        if (s->samples == 48) {
            s->phase = 3;
            s->ticks = 0;
            s->pending = false;
            s->reset_count = stats.resets;
            af_engine_reset_interpolation(e);
            return;
        }
    }
    if (s->phase == 3) {
        if (s->ticks == 2) {
            CHECK(stats.resets > s->reset_count);
            s->reset_count = stats.resets;
            af_camera c = af_camera_default();
            c.position.x = 10;
            CHECK(af_engine_set_camera(e, &c));
        }
        if (s->ticks == 4) {
            CHECK(stats.resets > s->reset_count);
            s->reset_count = stats.resets;
            af_camera c = af_camera_default();
            CHECK(af_engine_set_camera(e, &c));
            af_rect v = {40, 30, 320, 240};
            af_engine_set_viewport(e, &v);
        }
        if (s->ticks == 6) {
            CHECK(stats.resets > s->reset_count);
            s->reset_count = stats.resets;
            CHECK(af_engine_set_window_size(e, 480, 360));
        }
        if (s->ticks == 10) {
            CHECK(stats.resets > s->reset_count);
            af_render_settings r = af_scene_get_render_settings(s->scene);
            r.anti_aliasing = AF_AA_FXAA;
            CHECK(af_scene_set_render_settings(s->scene, r));
        }
        if (s->ticks == 12) {
            CHECK(!stats.valid);
            af_render_settings r = af_scene_get_render_settings(s->scene);
            r.anti_aliasing = AF_AA_SMAA;
            CHECK(af_scene_set_render_settings(s->scene, r));
        }
        if (s->ticks == 14) {
            af_render_settings r = af_scene_get_render_settings(s->scene);
            r.anti_aliasing = AF_AA_TAA;
            r.bloom = true;
            CHECK(af_scene_set_render_settings(s->scene, r));
        }
        if (s->ticks == 18) {
            CHECK(stats.valid && stats.frames > 1 && stats.frames < 8);
            CHECK(af_scene_save(s->scene, "taa.json", NULL));
            af_scene *copy = af_scene_load("taa.json", NULL);
            CHECK(copy && af_scene_get_render_settings(copy).anti_aliasing == AF_AA_TAA);
            af_scene_destroy(copy);
            remove("taa.json");
            s->phase = 4;
            puts("TAA: static convergence, camera/object/skinned motion, "
                 "disocclusion, camera cuts, interpolation reset, viewport, resize, "
                 "mode switching, bloom and persistence passed.");
            af_engine_request_quit(e);
        }
        return;
    }
    s->old_prop = .32f * sinf((float)s->ticks * .19f);
    s->old_skin = .32f * cosf((float)s->ticks * .17f);
    s->old_camera = .04f * sinf((float)s->ticks * .11f);
    af_transform t = af_transform_identity();
    t.position = (af_vec3){s->old_prop, -.25f, 0};
    CHECK(af_scene_set_object_transform(s->scene, s->prop, t));
    t.position = (af_vec3){0, .25f, 0};
    CHECK(af_scene_set_object_transform(s->scene, s->skin, t));
    af_mat4 bone = af_mat4_identity();
    bone.elements[12] = s->old_skin;
    CHECK(af_engine_set_skin_palette(e, s->skin, &bone, 1));
    af_camera camera = af_camera_default();
    camera.position.x = s->old_camera;
    CHECK(af_engine_set_camera(e, &camera));
    if (s->ticks > 4) {
        CHECK(af_engine_test_capture_region(e, (af_vec2){150, 110}, WIDTH, HEIGHT));
        s->pending = true;
    }
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
    c.title = "Afterlight temporal AA motion regression";
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game g = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &g);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || s.failed || s.phase != 4 || stats.validation_errors || !stats.validation_enabled ? 1 : 0;
}

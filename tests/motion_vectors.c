/* SPDX-License-Identifier: MPL-2.0 */
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct state {
    af_scene *scene;
    af_object_id id;
    unsigned stage, ticks;
    bool pending, failed;
} state;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Motion stage %u line %d: %s\n", s->stage, __LINE__, #x);                                  \
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
    af_vertex v[4] = {0};
    af_vec3 p[] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    uint32_t ix[] = {0, 1, 2, 0, 2, 3};
    for (unsigned i = 0; i < 4; ++i) {
        v[i].position = p[i];
        v[i].color = (af_color){1, 1, 1, 1};
        v[i].weights.x = 1;
    }
    af_mesh_data mesh = {v, 4, ix, 6};
    if (!af_engine_upload_mesh(e, "skin", &mesh))
        return false;
    s->id = af_scene_add_object(s->scene, "moving skin", "skin");
    af_material m = af_material_default();
    m.unlit = true;
    if (!af_scene_set_object_material(s->scene, s->id, m))
        return false;
    af_render_settings settings = af_render_settings_default();
    settings.anti_aliasing = AF_AA_TAA;
    if (!af_scene_set_render_settings(s->scene, settings))
        return false;
    af_engine_set_scene(e, s->scene);
    af_engine_set_simulation_paused(e, true);
    af_rect viewport = {80, 60, 240, 180};
    af_engine_set_viewport(e, &viewport);
    af_mat4 identity = af_mat4_identity();
    return af_engine_set_skin_palette(e, s->id, &identity, 1);
}
static void update(af_engine *e, double dt, void *data) {
    (void)dt;
    state *s = data;
    if (++s->ticks < 5)
        return;
    if (s->pending) {
        af_vec4 pixel;
        CHECK(af_engine_test_read_motion(e, &pixel));
        float expected[] = {0, -.3f, -.2f, .1f, -.2f, 0};
        printf("Motion %u: %.6f %.6f %.3f %.3f\n", s->stage, pixel.x, pixel.y, pixel.z, pixel.w);
        if (s->stage < 6) {
            float velocity =
                expected[s->stage] * .6f / (2 * tanf(af_camera_default().vertical_fov * .5f) * (4.f / 3.f) * 3);
            float vertical = 0;
            if (s->stage == 5) {
                // Dolly motion expands the actual sampled ray, including its pixel
                // center and raster jitter; the logical center is between pixels.
                af_temporal_test_stats stats = af_engine_test_temporal(e);
                velocity = (.5f / (float)stats.width - stats.jitter.x) / 3;
                vertical = (.5f / (float)stats.height - stats.jitter.y) / 3;
            }
            CHECK(fabsf(pixel.x - velocity) < .00015f && fabsf(pixel.y - vertical) < .0001f);
            CHECK(fabsf(pixel.z - 3) < .005f);
            CHECK(fabsf(pixel.w - (s->stage == 5 ? 4.f : 3.f)) < .005f);
        } else if (s->stage == 6)
            CHECK(pixel.z < 0 && fabsf(pixel.w - 4) < .005f);
        else if (s->stage == 7)
            CHECK(pixel.z < 0 && pixel.w > 65000);
        else if (s->stage == 11)
            CHECK(fabsf(pixel.z - 4) < .005f && fabsf(pixel.w - 4) < .005f && fabsf(pixel.x) < .0001f &&
                  fabsf(pixel.y) < .0001f);
        else
            CHECK(pixel.z < 0 && fabsf(pixel.w - 4) < .005f);
        if (++s->stage == 12) {
            puts("GPU motion: static, object, bone, camera, combined motion, linear "
                 "previous/current depth, removed palette, hidden object, AA Off and "
                 "reactive alpha "
                 "passed in an inset viewport.");
            af_engine_request_quit(e);
            return;
        }
    }
    af_transform model = af_transform_identity();
    model.position.x = s->stage >= 4 ? .5f : s->stage >= 1 ? .3f : 0;
    CHECK(af_scene_set_object_transform(s->scene, s->id, model));
    af_mat4 bone = af_mat4_identity();
    bone.elements[12] = s->stage >= 4 ? .3f : s->stage >= 2 ? .2f : 0;
    CHECK(af_engine_set_skin_palette(e, s->id, &bone, s->stage >= 6 ? 0 : 1));
    af_camera camera = af_camera_default();
    camera.position.x = s->stage >= 4 ? .2f : s->stage >= 3 ? .1f : 0;
    camera.position.z = s->stage >= 5 ? 4 : 3;
    CHECK(af_engine_set_camera(e, &camera));
    CHECK(af_scene_set_object_visible(s->scene, s->id, s->stage != 7));
    af_material material = af_material_default();
    material.unlit = true;
    material.alpha_mode = s->stage == 9 ? AF_ALPHA_BLEND : AF_ALPHA_OPAQUE;
    CHECK(af_scene_set_object_material(s->scene, s->id, material));
    if (s->stage == 9) {
        af_render_settings settings = af_scene_get_render_settings(s->scene);
        settings.anti_aliasing = AF_AA_TAA;
        CHECK(af_scene_set_render_settings(s->scene, settings));
    }
    if (s->stage == 8) {
        af_render_settings settings = af_scene_get_render_settings(s->scene);
        settings.anti_aliasing = AF_AA_OFF;
        CHECK(af_scene_set_render_settings(s->scene, settings));
    }
    CHECK(af_engine_test_capture_motion(e, (af_vec2){200, 150}));
    s->pending = true;
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
    c.title = "Afterlight GPU motion vectors";
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game g = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &g);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || s.failed || s.stage != 12 || stats.validation_errors || !stats.validation_enabled ? 1 : 0;
}

/* SPDX-License-Identifier: MPL-2.0 */
#include "render/render_test.h"
#include <afterlight/afterlight.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SIDE 64u
typedef struct state {
    af_scene *scene;
    unsigned mode, view, ticks;
    bool pending, failed;
    unsigned char off[SIDE * SIDE * 4];
} state;
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "AA mode %u view %u line %d: %s\n", s->mode, s->view, __LINE__, #x);                       \
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
    af_vertex v[] = {{.position = {-2, -1, 0}, .color = {1, 1, 1, 1}},
                     {.position = {2, 1, 0}, .color = {1, 1, 1, 1}},
                     {.position = {-2, 2, 0}, .color = {1, 1, 1, 1}}};
    uint32_t ix[] = {0, 1, 2};
    af_mesh_data mesh = {v, 3, ix, 3};
    if (!af_engine_upload_mesh(e, "diagonal", &mesh))
        return false;
    af_object_id id = af_scene_add_object(s->scene, "Diagonal edge", "diagonal");
    af_material material = af_material_default();
    material.unlit = true;
    if (!af_scene_set_object_material(s->scene, id, material))
        return false;
    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 3};
    camera.yaw = -AF_PI / 2;
    af_engine_set_camera(e, &camera);
    af_engine_set_scene(e, s->scene);
    af_engine_set_clear_color(e, (af_color){0, 0, 0, 1});
    const unsigned char white[] = {255, 255, 255, 255};
    if (!af_engine_upload_overlay_atlas(e, white, 1, 1))
        return false;
    af_engine_set_simulation_paused(e, true);
    return true;
}
static unsigned mixed(const unsigned char *pixels) {
    unsigned count = 0;
    for (unsigned i = 0; i < SIDE * SIDE; ++i)
        count += pixels[4 * i] > 8 && pixels[4 * i] < 247;
    return count;
}
static void update(af_engine *e, double dt, void *data) {
    (void)dt;
    state *s = data;
    if (s->view == 3) {
        static const af_overlay_vertex vertices[] = {{{4, 4}, {0, 0}, {255, 255, 255, 255}},
                                                     {{28, 16}, {0, 0}, {255, 255, 255, 255}},
                                                     {{4, 28}, {0, 0}, {255, 255, 255, 255}}};
        static const uint32_t indices[] = {0, 1, 2};
        static const af_overlay_command command = {.clip = {0, 0, 640, 480}, .index_count = 3};
        af_overlay_frame overlay = {vertices, indices, &command, 3, 3, 1, 640, 480};
        CHECK(af_engine_set_overlay(e, &overlay));
    }
    if (!s->pending) {
        if (++s->ticks < 24)
            return;
        af_vec2 point = s->view == 0 ? (af_vec2){184, 134} : s->view == 1 ? (af_vec2){224, 164} : (af_vec2){0, 0};
        CHECK(af_engine_test_capture_region(e, point, SIDE, SIDE));
        s->pending = true;
        return;
    }
    unsigned char pixels[SIDE * SIDE * 4];
    uint32_t width, height;
    CHECK(af_engine_test_read_pixels(e, pixels, sizeof(pixels), &width, &height));
    CHECK(width == SIDE && height == SIDE);
    unsigned intermediates = mixed(pixels);
    printf("AA %u view %u: %u intermediate edge pixels\n", s->mode, s->view, intermediates);
    if (s->view < 2) {
        if (!s->mode) {
            CHECK(intermediates == 0);
            memcpy(s->off, pixels, sizeof(pixels));
        } else {
            CHECK(intermediates >= 16);
            for (unsigned y = 2; y < SIDE - 2; ++y)
                for (unsigned x = 2; x < SIDE - 2; ++x) {
                    unsigned at = (y * SIDE + x) * 4;
                    bool flat = true;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            unsigned other = (unsigned)((int)y + dy) * SIDE + (unsigned)((int)x + dx);
                            flat &= s->off[other * 4] == s->off[at];
                        }
                    if (flat)
                        CHECK(abs((int)pixels[at] - s->off[at]) < 4);
                }
        }
    } else if (s->view == 3) {
        if (!s->mode) {
            CHECK(intermediates == 0);
            memcpy(s->off, pixels, sizeof(pixels));
        } else
            CHECK(!memcmp(s->off, pixels, sizeof(pixels)));
    } else
        for (unsigned i = 0; i < sizeof(pixels); i += 4)
            CHECK(pixels[i] < 3 && pixels[i + 1] < 3 && pixels[i + 2] < 3);
    if (++s->mode > (unsigned)AF_AA_TAA) {
        s->mode = 0;
        if (++s->view == 1) {
            CHECK(af_engine_set_window_size(e, 640, 480));
            af_rect area = {80, 60, 320, 240};
            af_engine_set_viewport(e, &area);
        }
        if (s->view == 4) {
            af_render_settings settings = af_scene_get_render_settings(s->scene);
            settings.anti_aliasing = AF_AA_FXAA;
            settings.aa_sharpness = .3f;
            CHECK(af_scene_set_render_settings(s->scene, settings));
            CHECK(af_scene_save(s->scene, "antialiasing.json", NULL));
            af_scene *copy = af_scene_load("antialiasing.json", NULL);
            CHECK(copy && af_scene_get_render_settings(copy).anti_aliasing == AF_AA_FXAA &&
                  af_scene_get_render_settings(copy).aa_sharpness == .3f);
            af_scene_destroy(copy);
            remove("antialiasing.json");
            puts("AA: real diagonal-edge coverage, flat areas, inset viewport, "
                 "outside clipping, crisp overlay, resize and persistence passed.");
            af_engine_request_quit(e);
            return;
        }
    }
    af_render_settings settings = af_scene_get_render_settings(s->scene);
    settings.anti_aliasing = (af_anti_aliasing)s->mode;
    settings.aa_sharpness = 0;
    CHECK(af_scene_set_render_settings(s->scene, settings));
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
    c.title = "Afterlight selectable anti-aliasing";
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &game);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || s.failed || s.view != 4 || stats.validation_errors ? 1 : 0;
}

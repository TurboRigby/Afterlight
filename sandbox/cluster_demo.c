/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include "render/render_test.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef struct demo {
    const char *path;
    af_scene *scene;
    af_camera camera;
    unsigned frame, phase;
    bool verify, failed, colors, enabled, gpu, gpu_expected, lod;
    af_cluster_stats cpu_clusters;
    uint64_t far_triangles;
    unsigned char reference[64 * 64 * 4];
} demo;

static bool init(af_engine *engine, void *user) {
    demo *d = user;
    af_asset_error error;
    if (!af_engine_load_cluster_mesh(engine, "wall", d->path, &error)) {
        fprintf(stderr, "%s\n", error.message);
        return false;
    }
    d->scene = af_scene_create();
    if (!d->scene || !af_scene_add_object(d->scene, "Rock wall", "wall"))
        return false;
    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.enabled = false;
    af_scene_set_shadows(d->scene, shadows);
    af_engine_set_scene(engine, d->scene);
    d->camera = af_camera_default();
    d->camera.position.z = 5;
    d->camera.far_clip = 400;
    d->gpu_expected = af_engine_cluster_stats(engine).gpu_supported;
    af_engine_set_cluster_rendering(engine, !d->verify, false);
    af_engine_set_cluster_lod(engine, !d->verify);
    return af_engine_set_camera(engine, &d->camera);
}

static void verify_frame(af_engine *engine, demo *d) {
    if (++d->frame == 12) {
        af_cluster_stats c = af_engine_cluster_stats(engine);
        af_engine_stats s = af_engine_get_stats(engine);
        const char *modes[] = {"whole", "CPU clusters", "GPU clusters", "colors", "far fine", "far LOD", "far GPU LOD"};
        printf("%s: %zu draws, %" PRIu64 "/%" PRIu64 " clusters, %" PRIu64 " triangles, %" PRIu64
               " coarse regions, %.3f ms GPU scene\n",
               modes[d->phase], s.draw_calls, c.visible, c.candidates, c.triangles, c.coarse_regions, s.gpu_scene_ms);
        if (d->phase == 1 || d->phase == 5)
            d->cpu_clusters = c;
        if (d->phase == 4)
            d->far_triangles = c.triangles;
        bool gpu_phase = d->phase == 2 || d->phase == 6;
        bool good = d->phase ? c.visible > 0 : c.candidates == 0;
        if (d->phase >= 1 && d->phase <= 3)
            good &= c.visible < c.candidates;
        if (d->phase >= 5)
            good &= c.coarse_regions > 0 && c.triangles < d->far_triangles * 9 / 10;
        if (gpu_phase)
            good &= c.visible == d->cpu_clusters.visible && c.triangles == d->cpu_clusters.triangles;
        if (!good || c.gpu_active != (gpu_phase && d->gpu_expected) ||
            !af_engine_test_capture_region(engine, (af_vec2){168, 118}, 64, 64)) {
            d->failed = true;
            af_engine_request_quit(engine);
        }
        return;
    }
    if (d->frame < 13)
        return;
    unsigned char pixels[sizeof(d->reference)];
    bool compare = d->phase == 1 || d->phase == 2 || d->phase == 3 || d->phase == 6;
    if (!af_engine_test_read_pixels(engine, pixels, sizeof(pixels), NULL, NULL) ||
        (compare && (!memcmp(d->reference, pixels, sizeof(pixels))) != (d->phase != 3))) {
        fprintf(stderr, "Unexpected cluster capture in phase %u.\n", d->phase);
        d->failed = true;
        af_engine_request_quit(engine);
        return;
    }
    if (!d->phase || d->phase == 5)
        memcpy(d->reference, pixels, sizeof(pixels));
    if (++d->phase == 7) {
        af_engine_request_quit(engine);
        return;
    }
    if (d->phase == 4) {
        d->camera.position.z = 240;
        (void)af_engine_set_camera(engine, &d->camera);
    }
    af_engine_set_cluster_rendering(engine, true, d->phase == 3);
    af_engine_set_cluster_gpu_culling(engine, d->phase == 2 || d->phase == 6);
    af_engine_set_cluster_lod(engine, d->phase >= 5);
    d->frame = 0;
}

static void update(af_engine *engine, double delta, void *user) {
    demo *d = user;
    if (d->verify) {
        verify_frame(engine, d);
        return;
    }
    if (af_engine_key_pressed(engine, AF_KEY_ESCAPE))
        af_engine_request_quit(engine);
    if (af_engine_key_pressed(engine, AF_KEY_C))
        d->colors = !d->colors;
    if (af_engine_key_pressed(engine, AF_KEY_X))
        d->gpu = !d->gpu;
    if (af_engine_key_pressed(engine, AF_KEY_N))
        d->lod = !d->lod;
    if (af_engine_key_pressed(engine, AF_KEY_V))
        d->enabled = !d->enabled;
    if (af_engine_key_pressed(engine, AF_KEY_R)) {
        d->camera = af_camera_default();
        d->camera.position.z = 5;
        d->camera.far_clip = 400;
    }
    if (af_engine_mouse_pressed(engine, AF_MOUSE_RIGHT))
        (void)af_engine_set_mouse_capture(engine, !af_engine_mouse_captured(engine));
    af_vec2 mouse = af_engine_mouse_delta(engine);
    if (af_engine_mouse_captured(engine))
        af_camera_rotate(&d->camera, mouse.x * .002f, mouse.y * -.002f);
    af_vec3 move = {(float)af_engine_key_down(engine, AF_KEY_D) - (float)af_engine_key_down(engine, AF_KEY_A),
                    (float)af_engine_key_down(engine, AF_KEY_E) - (float)af_engine_key_down(engine, AF_KEY_Q),
                    (float)af_engine_key_down(engine, AF_KEY_W) - (float)af_engine_key_down(engine, AF_KEY_S)};
    af_camera_move(&d->camera, af_vec3_scale(move, (float)delta * 6));
    (void)af_engine_set_camera(engine, &d->camera);
    af_engine_set_cluster_rendering(engine, d->enabled, d->colors);
    af_engine_set_cluster_gpu_culling(engine, d->gpu);
    af_engine_set_cluster_lod(engine, d->lod);
    if (af_engine_key_pressed(engine, AF_KEY_SPACE)) {
        af_cluster_stats s = af_engine_cluster_stats(engine);
        printf("%" PRIu64 "/%" PRIu64 " clusters, %" PRIu64 " triangles, %" PRIu64 " coarse regions\n", s.visible,
               s.candidates, s.triangles, s.coarse_regions);
    }
}

static void shutdown(af_engine *engine, void *user) {
    demo *d = user;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(d->scene);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3 || (argc == 3 && strcmp(argv[2], "--verify"))) {
        fprintf(stderr, "usage: afterlight_cluster_demo MESH.afgeom [--verify]\n");
        return 1;
    }
    demo d = {.path = argv[1], .enabled = true, .gpu = true, .lod = true, .verify = argc == 3};
    af_engine_config cfg = af_engine_config_default();
    cfg.title = "Afterlight cluster wall";
    cfg.width = d.verify ? 400 : 1000;
    cfg.height = d.verify ? 300 : 750;
    cfg.enable_validation = d.verify;
    af_engine *engine = af_engine_create(&cfg);
    if (!engine)
        return 1;
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &d};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    return result || d.failed || (d.verify && (d.phase != 7 || stats.validation_errors)) ? 1 : 0;
}

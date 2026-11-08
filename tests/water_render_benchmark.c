/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include <afterlight/afterlight.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct benchmark {
    const char *directory;
    af_scene *scene;
    unsigned case_index, frames, measured;
    bool failed;
    double physics, render, gpu, fence, acquire, upload;
    uint64_t idle;
} benchmark;
static const char *names[] = {"water-pool",       "water-lake", "water-stream",
                              "water-river-bend", "water-pour", "water-pour-lab"};
static bool load(af_engine *e, benchmark *b) {
    af_engine_set_scene(e, NULL);
    af_scene_destroy(b->scene);
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s.afterlight.json", b->directory, names[b->case_index / 3]);
    b->scene = af_scene_load(path, NULL);
    if (!b->scene)
        return false;
    af_water w = *af_scene_water_at(b->scene, 0);
    w.quality = (af_water_quality)(b->case_index % 3);
    if (!af_scene_set_water(b->scene, w.owner, w))
        return false;
    af_engine_set_scene(e, b->scene);
    b->frames = 0;
    b->physics = b->render = b->gpu = b->fence = b->acquire = b->upload = 0;
    b->idle = af_engine_get_stats(e).mesh_idle_waits;
    return true;
}
static bool init(af_engine *e, void *data) {
    benchmark *b = data;
    af_mesh_data mesh = cube_mesh_data();
    af_camera c = af_camera_default();
    c.position = (af_vec3){0, 5, 9};
    c.yaw = -AF_PI / 2;
    c.pitch = -.48f;
    af_engine_set_camera(e, &c);
    puts("scene,quality,frames,cpu_physics_ms,cpu_render_ms,gpu_frame_ms,cpu_fence_ms,cpu_acquire_ms,"
         "cpu_water_upload_ms,mesh_idle_waits");
    return af_engine_upload_mesh(e, "cube", &mesh) && load(e, b);
}
static void update(af_engine *e, double dt, void *data) {
    (void)dt;
    benchmark *b = data;
    af_engine_stats s = af_engine_get_stats(e);
    if (s.validation_errors || s.missing_meshes) {
        b->failed = true;
        af_engine_request_quit(e);
        return;
    }
    if (b->frames >= 30) {
        b->physics += s.cpu_physics_ms;
        b->render += s.cpu_render_ms;
        b->gpu += s.gpu_frame_ms;
        b->fence += s.cpu_fence_ms;
        b->acquire += s.cpu_acquire_ms;
        b->upload += s.water_upload_ms;
    }
    if (++b->frames < 30 + b->measured)
        return;
    double n = b->measured;
    printf("%s,%u,%u,%.5f,%.5f,%.5f,%.5f,%.5f,%.5f,%llu\n", names[b->case_index / 3], b->case_index % 3, b->measured,
           b->physics / n, b->render / n, b->gpu / n, b->fence / n, b->acquire / n, b->upload / n,
           (unsigned long long)(s.mesh_idle_waits - b->idle));
    fflush(stdout);
    if (++b->case_index == 18) {
        af_engine_request_quit(e);
        return;
    }
    if (!load(e, b)) {
        b->failed = true;
        af_engine_request_quit(e);
    }
}
static void shutdown(af_engine *e, void *data) {
    benchmark *b = data;
    af_engine_set_scene(e, NULL);
    af_scene_destroy(b->scene);
}
int main(int argc, char **argv) {
    if (argc < 2 || argc > 3)
        return 2;
    benchmark b = {.directory = argv[1], .measured = argc == 3 ? (unsigned)strtoul(argv[2], NULL, 10) : 90};
    if (b.measured < 30 || b.measured > 3600)
        return 2;
    af_engine_config c = af_engine_config_default();
    c.title = "Afterlight water Release render benchmark";
    c.width = 640;
    c.height = 480;
    c.enable_validation = true;
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game g = {.init = init, .update = update, .shutdown = shutdown, .user_data = &b};
    int r = af_engine_run(e, &g);
    af_engine_destroy(e);
    return r || b.failed || b.case_index != 18 ? 1 : 0;
}

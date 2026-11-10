/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "runtime/runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static const char *names[] = {"water-pool",       "water-lake", "water-stream",
                              "water-river-bend", "water-pour", "water-pour-lab"};
typedef struct test {
    const char *directory, *module, *project;
    af_scene *source;
    ed_runtime runtime;
    unsigned scene, phase, frames;
    uint64_t start, paused, mesh_waits;
    float source_y, particle_y;
    bool failed, kick, manual;
    af_object_id source_body, owner;
} test;
static bool tilt;
static bool key(const af_game_host *h, af_key k) {
    (void)h;
    return tilt && k == AF_KEY_F;
}
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Water demo %u phase %u line %d: %s\n", s->scene, s->phase, __LINE__, #x);                 \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static bool load(af_engine *e, test *s) {
    af_engine_set_scene(e, NULL);
    ed_runtime_stop(&s->runtime);
    af_scene_destroy(s->source);
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s.afterlight.json", s->directory, names[s->scene]);
    af_scene_error error;
    s->source = af_scene_load(path, &error);
    if (!s->source) {
        fprintf(stderr, "%s\n", error.message);
        return false;
    }
    if (!ed_runtime_set_project(&s->runtime, s->project, path) ||
        !ed_runtime_start_file(&s->runtime, s->source, e, s->module))
        return false;
    s->runtime.host.key_pressed = key;
    s->owner = af_scene_water_at(s->source, 0)->owner;
    s->source_body = 0;
    for (size_t i = 0; i < af_scene_object_count(s->source); ++i) {
        const af_scene_object *o = af_scene_object_at(s->source, i);
        if (o->body.enabled && o->body.type == AF_BODY_DYNAMIC) {
            s->source_body = o->id;
            s->source_y = o->transform.position.y;
            break;
        }
    }
    s->start = af_engine_get_stats(e).simulation_ticks;
    s->phase = 0;
    s->kick = false;
    af_engine_set_simulation_paused(e, false);
    return true;
}
static bool init(af_engine *e, void *data) {
    test *s = data;
    af_mesh_data mesh = cube_mesh_data();
    if (!af_engine_upload_mesh(e, "cube", &mesh))
        return false;
    s->mesh_waits = af_engine_get_stats(e).mesh_idle_waits;
    return load(e, s);
}
static void update(af_engine *e, double dt, void *data) {
    test *s = data;
    if (s->failed)
        return;
    if (s->scene == 6) {
        CHECK(af_engine_get_stats(e).loaded_meshes == 1);
        puts("Water C Play: six ordered demos, generated meshes, buoyancy/currents, filling/tilting, "
             "pause/single-step and Edit isolation passed.");
        af_engine_request_quit(e);
        return;
    }
    ed_runtime_frame_update(&s->runtime, dt, true);
    af_engine_set_scene(e, s->runtime.scene);
    uint64_t ticks = af_engine_get_stats(e).simulation_ticks - s->start;
    af_water_stats water = af_engine_water_stats(e);
    if (s->phase == 0 && ticks >= 90) {
        CHECK(water.surfaces == 1 && af_engine_get_stats(e).validation_errors == 0);
        CHECK(af_engine_get_stats(e).mesh_idle_waits == s->mesh_waits &&
              af_engine_get_stats(e).water_stream_uploads > 0 && af_engine_get_stats(e).water_upload_bytes > 0);
        if (s->scene < 4)
            CHECK(water.wet_bodies > 0);
        else
            CHECK(water.particles >= 90 && water.emitted_volume > 0);
        s->runtime.paused = true;
        af_engine_set_simulation_paused(e, true);
        s->paused = af_engine_get_stats(e).simulation_ticks;
        s->frames = 0;
        s->phase = 1;
        return;
    }
    if (s->phase == 1 && ++s->frames >= 5) {
        CHECK(af_engine_get_stats(e).simulation_ticks == s->paused);
        s->manual = true;
        CHECK(af_engine_step_simulation(e));
        s->phase = 2;
        return;
    }
    if (s->phase == 2 && af_engine_get_stats(e).simulation_ticks > s->paused) {
        CHECK(af_engine_get_stats(e).simulation_ticks == s->paused + 1);
        s->runtime.paused = false;
        af_engine_set_simulation_paused(e, false);
        s->phase = 3;
        if (s->scene == 5) {
            tilt = true;
            s->kick = true;
        }
        return;
    }
    if (s->phase == 3 && ticks >= (s->scene == 5 ? 270u : 120u)) {
        if (s->source_body)
            CHECK(af_scene_get_object(s->source, s->source_body)->transform.position.y == s->source_y);
        if (s->scene == 5) {
            const af_scene_object *cup = NULL;
            for (size_t i = 0; i < af_scene_object_count(s->runtime.scene); ++i) {
                const af_scene_object *o = af_scene_object_at(s->runtime.scene, i);
                if (!strcmp(o->name, "Pouring cup"))
                    cup = o;
            }
            CHECK(cup && cup->transform.rotation.z < -1.5f);
        }
        printf("Water C scene %s: %zu cells / %zu particles, %.3f ms physics, %.3f ms fluid.\n", names[s->scene],
               water.cells, water.particles, af_engine_get_stats(e).cpu_physics_ms, water.milliseconds);
        ++s->scene;
        if (s->scene < 6) {
            CHECK(load(e, s));
        } else {
            af_engine_set_scene(e, NULL);
            ed_runtime_stop(&s->runtime);
            af_scene_destroy(s->source);
            s->source = NULL;
        }
        return;
    }
}
static void fixed(af_engine *e, double dt, void *data) {
    (void)e;
    test *s = data;
    if (s->scene == 6)
        return;
    if (s->manual) {
        ed_runtime_step(&s->runtime, dt);
        s->manual = false;
    } else
        ed_runtime_update(&s->runtime, dt, true);
    if (s->kick) {
        tilt = false;
        s->kick = false;
    }
}
static void shutdown(af_engine *e, void *data) {
    test *s = data;
    af_engine_set_scene(e, NULL);
    ed_runtime_stop(&s->runtime);
    af_scene_destroy(s->source);
}
int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    test s = {.directory = argv[1], .module = argv[2], .project = argv[3]};
    af_engine_config c = af_engine_config_default();
    c.width = 640;
    c.height = 480;
    c.title = "Afterlight ordered water labs";
    c.enable_validation = true;
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game g = {.init = init, .update = update, .fixed_update = fixed, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &g);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || s.failed || s.scene != 6 || stats.validation_errors ? 1 : 0;
}

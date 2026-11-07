/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "runtime.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct test {
    const char *scene_path, *module, *project;
    af_scene *source;
    ed_runtime runtime;
    unsigned phase, visit, frames;
    uint64_t start;
    float initial_z, river_z, max_received;
    bool failed, manual;
} test;
static bool down[AF_KEY_COUNT], pressed[AF_KEY_COUNT];
static bool held(const af_game_host *h, af_key k) {
    (void)h;
    return down[k];
}
static bool edge(const af_game_host *h, af_key k) {
    (void)h;
    return pressed[k];
}
static bool captured(const af_engine *e) {
    (void)e;
    return true;
}
static af_vec2 motion(const af_engine *e) {
    (void)e;
    return (af_vec2){0};
}
static af_object_id named(const af_scene *s, const char *name) {
    for (size_t i = 0; i < af_scene_object_count(s); ++i)
        if (!strcmp(af_scene_object_at(s, i)->name, name))
            return af_scene_object_at(s, i)->id;
    return 0;
}
static af_object_id player(const af_scene *s) {
    for (size_t i = 0; i < af_scene_object_count(s); ++i)
        if (af_scene_object_at(s, i)->gameplay.player.enabled)
            return af_scene_object_at(s, i)->id;
    return 0;
}
static bool teleport(test *s, af_vec3 position) {
    af_object_id id = player(s->runtime.scene);
    const af_scene_object *o = af_scene_get_object(s->runtime.scene, id);
    if (!o)
        return false;
    af_transform t = o->transform;
    t.position = position;
    return af_scene_set_object_transform(s->runtime.scene, id, t);
}
static bool visit(test *s, unsigned order, bool goal) {
    for (size_t i = 0; i < af_scene_object_count(s->runtime.scene); ++i) {
        const af_scene_object *o = af_scene_object_at(s->runtime.scene, i);
        if ((goal && o->gameplay.role == AF_ROLE_GOAL) ||
            (!goal && o->gameplay.role == AF_ROLE_CHECKPOINT && o->gameplay.checkpoint_order == order)) {
            af_mat4 m;
            if (!af_scene_world_matrix(s->runtime.scene, o->id, &m))
                return false;
            return teleport(s, (af_vec3){m.elements[12], m.elements[13], m.elements[14]});
        }
    }
    return false;
}
static float received(af_engine *e, const af_scene *s) {
    af_object_id cup = named(s, "Receiving cup"), fluid = named(s, "Course pour");
    const af_scene_object *o = af_scene_get_object(s, cup);
    if (!o)
        return 0;
    af_water_particle particles[4096];
    size_t n = af_engine_water_particles(e, fluid, particles, 4096);
    float volume = 0;
    for (size_t i = 0; i < n && i < 4096; ++i) {
        af_vec3 p = particles[i].position, c = o->transform.position;
        if (fabsf(p.x - c.x) < .64f && fabsf(p.z - c.z) < .64f && p.y > c.y && p.y < c.y + 1.2f)
            volume += particles[i].volume;
    }
    return volume;
}
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Course phase %u line %d: %s (received %.4f)\n", s->phase, __LINE__, #x,    \
                    s->max_received);                                                                   \
            s->failed = true;                                                                           \
            af_engine_request_quit(e);                                                                  \
            return;                                                                                     \
        }                                                                                               \
    } while (0)
static bool init(af_engine *e, void *data) {
    test *s = data;
    s->source = af_scene_load(s->scene_path, NULL);
    af_mesh_data mesh = cube_mesh_data();
    if (!s->source || !af_engine_upload_mesh(e, "cube", &mesh) ||
        !af_engine_load_scene_assets(e,s->source,NULL) ||
        !ed_runtime_set_project(&s->runtime, s->project, s->scene_path) ||
        !ed_runtime_start_file(&s->runtime, s->source, e, s->module))
        return false;
    s->initial_z = af_scene_get_object(s->source, player(s->source))->transform.position.z;
    s->start = af_engine_get_stats(e).simulation_ticks;
    down[AF_KEY_W] = true;
    return true;
}
static void update(af_engine *e, double dt, void *data) {
    test *s = data;
    static unsigned last_phase = 999;
    if (s->phase != last_phase) {
        fprintf(stderr, "Water course phase %u\n", s->phase);
        last_phase = s->phase;
    }
    s->runtime.host.key_down = held;
    s->runtime.host.key_pressed = edge;
    s->runtime.host.mouse_captured = captured;
    s->runtime.host.mouse_delta = motion;
    ed_runtime_frame_update(&s->runtime, dt, true);
    af_engine_set_scene(e, s->runtime.scene);
    uint64_t ticks = af_engine_get_stats(e).simulation_ticks - s->start;
    af_gameplay_status status = af_engine_gameplay_status(e);
    const af_scene_object *body = af_scene_get_object(s->runtime.scene, player(s->runtime.scene));
    CHECK(body && isfinite(body->transform.position.y));
    switch (s->phase) {
    case 0:
        if (ticks < 120)
            return;
        CHECK(body->transform.position.z < 5 && body->transform.position.y < .7f && status.running);
        down[AF_KEY_W] = false;
        CHECK(teleport(s, (af_vec3){-2, -.6f, 2}));
        down[AF_KEY_LEFT_CONTROL] = true;
        s->phase = 1;
        s->start = af_engine_get_stats(e).simulation_ticks;
        break;
    case 1:
        if (ticks < 60)
            return;
        CHECK(body->transform.position.y < -.9f && strstr(status.hint, "Diving"));
        down[AF_KEY_LEFT_CONTROL] = false;
        s->phase = 2;
        s->start = af_engine_get_stats(e).simulation_ticks;
        break;
    case 2:
        if (ticks < 150)
            return;
        CHECK(body->transform.position.y > -.5f);
        s->visit = 1;
        CHECK(visit(s, 1, false));
        s->phase = 3;
        break;
    case 3:
        if (status.checkpoint < s->visit)
            return;
        if (s->visit < status.checkpoint_count) {
            CHECK(visit(s, ++s->visit, false));
            return;
        }
        CHECK(visit(s, 0, true));
        s->phase = 4;
        break;
    case 4:
        if (!status.finished)
            return;
        CHECK(af_scene_get_object(s->source, player(s->source))->transform.position.z == s->initial_z);
        CHECK(ed_runtime_reload(&s->runtime));
        CHECK(af_engine_gameplay_status(e).finished);
        pressed[AF_KEY_R] = true;
        s->phase = 5;
        break;
    case 5:
        if (status.finished || status.checkpoint)
            return;
        CHECK(body->transform.position.z > 7);
        pressed[AF_KEY_N] = true;
        s->phase = 6;
        break;
    case 6:
        if (!named(s->runtime.scene, "Receiving cup"))
            return;
        CHECK(status.checkpoint_count == 3);
        CHECK(teleport(s, (af_vec3){0, -.3f, -7}));
        s->river_z = -7;
        s->phase = 7;
        s->start = af_engine_get_stats(e).simulation_ticks;
        break;
    case 7:
        if (ticks < 60)
            return;
        CHECK(body->transform.position.z < s->river_z - .25f);
        s->visit = 1;
        CHECK(visit(s, 1, false));
        s->phase = 8;
        break;
    case 8:
        if (status.checkpoint < s->visit)
            return;
        if (s->visit < status.checkpoint_count) {
            CHECK(visit(s, ++s->visit, false));
            return;
        }
        CHECK(visit(s, 0, true));
        s->phase = 9;
        s->start = af_engine_get_stats(e).simulation_ticks;
        break;
    case 9:
        if (ticks < 5)
            return;
        CHECK(!status.finished && strstr(status.hint, "Fill receiving cup"));
        CHECK(teleport(s, (af_vec3){2, 1.35f, -16}));
        pressed[AF_KEY_E] = true;
        s->phase = 10;
        s->start = af_engine_get_stats(e).simulation_ticks;
        break;
    case 10:
        s->max_received = fmaxf(s->max_received, received(e, s->runtime.scene));
        if (s->max_received < .05f) {
            if (ticks % 120 == 0) {
                const af_scene_object *cup =
                    af_scene_get_object(s->runtime.scene, named(s->runtime.scene, "Source cup"));
                fprintf(stderr, "Native pour tick %llu tilt %.3f player %.2f %.2f %.2f\n",
                        (unsigned long long)ticks, cup ? cup->transform.rotation.z : 0,
                        body->transform.position.x, body->transform.position.y,
                        body->transform.position.z);
            }
            CHECK(ticks < 900);
            return;
        }
        printf("Expedition received %.4f m3 after %llu ticks.\n", s->max_received,
               (unsigned long long)ticks);
        CHECK(visit(s, 0, true));
        s->phase = 11;
        break;
    case 11:
        if (!status.finished)
            return;
        puts("Water courses: actual walk/swim/dive/current movement, ordered checkpoints, finish gate, "
             "reset/scene transition, hot reload, cup transfer and Edit isolation passed.");
        af_engine_request_quit(e);
        return;
    default:
        CHECK(false);
    }
}
static void fixed(af_engine *e, double dt, void *data) {
    (void)e;
    test *s = data;
    s->runtime.host.key_down = held;
    s->runtime.host.key_pressed = edge;
    s->runtime.host.mouse_captured = captured;
    s->runtime.host.mouse_delta = motion;
    ed_runtime_update(&s->runtime, dt, true);
    memset(pressed, 0, sizeof(pressed));
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
    test s = {.scene_path = argv[1], .module = argv[2], .project = argv[3]};
    af_engine_config c = af_engine_config_default();
    c.width = 640;
    c.height = 480;
    c.enable_validation = true;
    c.title = "Afterlight playable water courses regression";
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game g = {
        .init = init, .update = update, .fixed_update = fixed, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &g);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || s.failed || s.phase != 11 || stats.validation_errors ? 1 : 0;
}

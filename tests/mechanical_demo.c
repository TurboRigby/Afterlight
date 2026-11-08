/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "runtime.h"
#include <afterlight/joint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct test {
    const char *simple, *complex, *module, *project;
    ed_runtime runtime;
    af_scene *source;
    af_object_id ball, crate, spawned;
    unsigned phase, ticks, frames;
    float source_z, old_z, paused_y;
    uint64_t paused_ticks;
    bool failed;
} test;
static unsigned phase;
static bool grab_edge, throw_edge;
static af_object_id pick;
static float action(const af_game_host *h, const char *name) {
    (void)h;
    return phase == 2 && !strcmp(name, "MoveY") ? 1.f : 0;
}
static bool pressed(const af_game_host *h, const char *name) {
    (void)h;
    return (!strcmp(name, "Grab") && grab_edge) || (!strcmp(name, "Throw") && throw_edge);
}
static bool pick_body(const af_scene *scene, af_vec3 origin, af_vec3 direction, float distance, af_query_filter filter,
                      af_collision_hit *out) {
    (void)origin;
    (void)direction;
    (void)distance;
    (void)filter;
    const af_scene_object *o = af_scene_get_object(scene, pick);
    if (!o)
        return false;
    *out = (af_collision_hit){.object = pick, .point = o->transform.position, .normal = {0, 1, 0}};
    return true;
}
static bool captured(const af_engine *e) {
    (void)e;
    return true;
}
static af_object_id named(const af_scene *s, const char *name) {
    for (size_t i = 0; i < af_scene_object_count(s); ++i)
        if (!strcmp(af_scene_object_at(s, i)->name, name))
            return af_scene_object_at(s, i)->id;
    return 0;
}
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Physics demo phase %u line %d: %s\n", s->phase, __LINE__, #x);                            \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static bool load(af_engine *e, test *s, const char *path) {
    af_engine_set_scene(e, NULL);
    ed_runtime_stop(&s->runtime);
    af_scene_destroy(s->source);
    af_scene_error error;
    af_asset_error ae;
    s->source = af_scene_load(path, &error);
    if (!s->source) {
        fprintf(stderr, "%s\n", error.message);
        return false;
    }
    if (!ed_runtime_set_project(&s->runtime, s->project, path) || !af_engine_load_scene_assets(e, s->source, &ae) ||
        !ed_runtime_start_file(&s->runtime, s->source, e, s->module)) {
        fprintf(stderr, "Physics module: %s\n", s->runtime.status);
        return false;
    }
    s->runtime.auto_reload = false;
    s->ball = named(s->runtime.scene, "Drop ball");
    s->crate = named(s->runtime.scene, "Push crate");
    s->source_z = af_scene_get_object(s->source, s->crate)->transform.position.z;
    return s->ball && s->crate;
}
static bool init(af_engine *e, void *data) {
    test *s = data;
    af_mesh_data mesh = cube_mesh_data();
    return af_engine_upload_mesh(e, "cube", &mesh) && load(e, s, s->simple);
}
static void update(af_engine *e, double dt, void *data) {
    test *s = data;
    s->runtime.host.action_value = action;
    s->runtime.host.action_pressed = pressed;
    s->runtime.host.mouse_captured = captured;
    s->runtime.host.raycast = pick_body;
    ed_runtime_frame_update(&s->runtime, dt, true);
    ++s->frames;
    af_physics_stats stats = af_engine_physics_stats(e);
    if (s->phase == 3) {
        CHECK(af_engine_body_state(e, s->ball).position.y == s->paused_y && stats.ticks == s->paused_ticks);
        if (s->frames < 8)
            return;
        af_engine_step_simulation(e);
        s->phase = 4;
        phase = 4;
        s->frames = 0;
        return;
    }
    if (s->phase == 4) {
        if (stats.ticks == s->paused_ticks)
            return;
        CHECK(stats.ticks == s->paused_ticks + 1 && af_engine_body_state(e, s->ball).position.y > s->paused_y);
        s->runtime.paused = false;
        af_engine_set_simulation_paused(e, false);
        CHECK(ed_runtime_reload(&s->runtime) && strstr(s->runtime.status, "restored"));
        s->phase = 5;
        phase = 5;
        s->ticks = 0;
        return;
    }
    unsigned wait = s->phase == 0 || s->phase == 2 ? 120 : s->phase == 6 ? 240 : s->phase == 5 ? 15 : 60;
    if (s->ticks < wait)
        return;
    s->ticks = 0;
    CHECK(!af_engine_get_stats(e).validation_errors && stats.dynamic_bodies >= 3);
    af_physics_state ball = af_engine_body_state(e, s->ball);
    CHECK(ball.available && isfinite(ball.position.y));
    fprintf(stderr,
            "Physics phase %u: ball %llu held %llu pos %.3f %.3f %.3f v %.3f %.3f %.3f, contacts %zu "
            "asleep %zu\n",
            s->phase, (unsigned long long)s->ball, (unsigned long long)af_engine_held_body(e), ball.position.x,
            ball.position.y, ball.position.z, ball.velocity.x, ball.velocity.y, ball.velocity.z, stats.contacts,
            stats.sleeping);
    switch (s->phase) {
    case 0:
        CHECK(stats.contacts > 0 && stats.sleeping >= 1 && ball.position.y < 1);
        char prefab[4096];
        CHECK(snprintf(prefab, sizeof(prefab), "%sprefabs/physics-crate.afterlight.prefab",
                       s->runtime.host.project_directory) < (int)sizeof(prefab));
        af_transform placement = af_transform_identity();
        placement.position = (af_vec3){4, 2, 3};
        af_scene_error error;
        CHECK(af_engine_spawn_prefab(e, s->runtime.scene, prefab, 0, placement, &s->spawned, &error));
        pick = s->ball;
        grab_edge = true;
        break;
    case 1:
        CHECK(af_engine_held_body(e) == s->ball && ball.position.y > .6f);
        CHECK(af_engine_body_state(e, s->spawned).available && stats.dynamic_bodies == 5);
        throw_edge = true;
        s->old_z = af_engine_body_state(e, s->crate).position.z;
        break;
    case 2:
        CHECK(af_engine_body_state(e, s->crate).position.z < s->old_z - .15f);
        CHECK(af_scene_get_object(s->source, s->crate)->transform.position.z == s->source_z);
        CHECK(af_engine_apply_impulse(e, s->ball, (af_vec3){0, (3 - ball.velocity.y) * ball.mass, 0}, ball.position));
        s->paused_y = ball.position.y;
        s->paused_ticks = stats.ticks;
        s->runtime.paused = true;
        af_engine_set_simulation_paused(e, true);
        s->frames = 0;
        break;
    case 5:
        CHECK(load(e, s, s->complex));
        break;
    case 6: {
        CHECK(stats.dynamic_bodies >= 17 && stats.contacts > 0);
        for (size_t i = 0; i < af_scene_object_count(s->runtime.scene); ++i) {
            const af_scene_object *o = af_scene_object_at(s->runtime.scene, i);
            if (o->body.enabled && o->body.type == AF_BODY_DYNAMIC) {
                af_physics_state body = af_engine_body_state(e, o->id);
                CHECK(isfinite(body.position.x) && isfinite(body.position.y) && body.position.y > -2);
            }
        }

        CHECK(af_scene_joint_count(s->runtime.scene) >= 10);
        for (size_t i = 0; i < af_scene_joint_count(s->runtime.scene); ++i) {
            af_joint j = *af_scene_joint_at(s->runtime.scene, i);
            af_joint_frame f;
            CHECK(af_scene_joint_frame(s->runtime.scene, j, &f));
            float joint_error = fabsf(af_vec3_length(af_vec3_sub(f.anchor_a, f.anchor_b)) -
                                      (j.type == AF_JOINT_DISTANCE ? j.distance : 0));
            CHECK(joint_error < .15f);
        }
        CHECK(af_engine_get_stats(e).shadow_draw_calls > 0);
        CHECK(af_engine_set_window_size(e, 800, 600));
        break;
    }
    case 7:
        af_engine_request_quit(e);
        return;
    default:
        CHECK(false);
    }
    ++s->phase;
    phase = s->phase;
}
static void fixed(af_engine *e, double dt, void *data) {
    test *s = data;
    if (s->runtime.paused)
        ed_runtime_step(&s->runtime, dt);
    else
        ed_runtime_update(&s->runtime, dt, true);
    grab_edge = throw_edge = false;
    ++s->ticks;
    af_engine_set_scene(e, s->runtime.scene);
}
static void shutdown(af_engine *e, void *data) {
    test *s = data;
    af_engine_set_scene(e, NULL);
    ed_runtime_stop(&s->runtime);
    af_scene_destroy(s->source);
}
int main(int argc, char **argv) {
    if (argc != 5)
        return 2;
    af_engine_config c = af_engine_config_default();
    c.title = "Afterlight physics simple-to-playground";
    c.width = 640;
    c.height = 480;
    c.enable_validation = true;
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    test s = {.simple = argv[1], .complex = argv[2], .module = argv[3], .project = argv[4]};
    af_game game = {.init = init, .update = update, .fixed_update = fixed, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &game);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    if (result || s.failed || s.phase != 7 || stats.validation_errors)
        return 1;
    puts("Mechanical Play: drop/rest/sleep, push/grab/throw, pause/single-step, reload, "
         "imported ramps/stairs, chains, hinges and mesh CCD, resize and edit isolation passed.");
    return 0;
}

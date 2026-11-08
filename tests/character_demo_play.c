/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include "cube.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct test_state {
    const char *simple, *course, *module;
    af_scene *source;
    ed_runtime runtime;
    af_object_id actor, body;
    unsigned phase, ticks;
    bool failed, saw_jump, saw_fall;
    float start_z;
} test_state;
static unsigned input_phase;
static bool edge;
static bool down(const af_game_host *host, af_key key) {
    (void)host;
    return (key == AF_KEY_W && (input_phase == 1 || input_phase == 2 || input_phase == 5)) ||
           (key == AF_KEY_LEFT_SHIFT && input_phase == 2);
}
static bool pressed(const af_game_host *host, af_key key) {
    (void)host;
    return edge && ((key == AF_KEY_SPACE && input_phase == 3) || (key == AF_KEY_E && input_phase == 5));
}
static af_vec2 still(const af_engine *engine) {
    (void)engine;
    return (af_vec2){0};
}
static bool no_mouse(const af_engine *engine, af_mouse_button button) {
    (void)engine;
    (void)button;
    return false;
}
static float action_value(const af_game_host *h, const char *name) {
    if (!strcmp(name, "MoveY"))
        return down(h, AF_KEY_W) ? 1.f : 0;
    if (!strcmp(name, "Run"))
        return down(h, AF_KEY_LEFT_SHIFT) ? 1.f : 0;
    return 0;
}
static bool action_pressed(const af_game_host *h, const char *name) {
    return (!strcmp(name, "Jump") && pressed(h, AF_KEY_SPACE)) || (!strcmp(name, "Restart") && pressed(h, AF_KEY_R)) ||
           (!strcmp(name, "Interact") && pressed(h, AF_KEY_E));
}
static bool captured(const af_engine *engine) {
    (void)engine;
    return true;
}
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Character Play phase %u line %d: %s\n", s->phase, __LINE__, #x);                          \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static bool load(af_engine *engine, test_state *s, const char *path) {
    af_engine_set_scene(engine, NULL);
    ed_runtime_stop(&s->runtime);
    af_scene_destroy(s->source);
    af_scene_error se;
    af_asset_error ae;
    s->source = af_scene_load(path, &se);
    s->actor = s->body = 0;
    if (!s->source || !af_engine_load_scene_assets(engine, s->source, &ae) ||
        !ed_runtime_start_file(&s->runtime, s->source, engine, s->module))
        return false;
    s->runtime.auto_reload = false;
    s->runtime.host.action_value = action_value;
    s->runtime.host.action_pressed = action_pressed;
    s->runtime.host.key_down = down;
    s->runtime.host.key_pressed = pressed;
    s->runtime.host.mouse_captured = captured;
    s->runtime.host.mouse_delta = still;
    s->runtime.host.mouse_pressed = no_mouse;
    for (size_t i = 0; i < af_scene_object_count(s->runtime.scene); ++i) {
        const af_scene_object *o = af_scene_object_at(s->runtime.scene, i);
        if (o->animation.asset[0] && !s->actor)
            s->actor = o->id;
        if (o->gameplay.player.enabled)
            s->body = o->id;
    }
    return s->actor && s->body;
}
static bool init(af_engine *engine, void *data) {
    test_state *s = data;
    af_mesh_data cube = cube_mesh_data();
    return af_engine_upload_mesh(engine, "cube", &cube) && load(engine, s, s->simple);
}
static void update(af_engine *engine, double dt, void *data) {
    test_state *s = data;
    ed_runtime_frame_update(&s->runtime, dt, true);
    if (s->ticks < 60)
        return;
    s->ticks = 0;
    af_animation_debug d = af_engine_animation_debug(engine, s->actor);
    const af_scene_object *body = af_scene_get_object(s->runtime.scene, s->body);
    CHECK(body && d.driven);
    af_engine_stats stats = af_engine_get_stats(engine);
    CHECK(!stats.missing_meshes && !stats.missing_textures && !stats.validation_errors);
    switch (s->phase) {
    case 0:
        CHECK(d.motion.state == AF_LOCOMOTION_IDLE);
        CHECK(af_engine_get_camera(engine).position.z > body->transform.position.z + 3);
        s->start_z = body->transform.position.z;
        break;
    case 1:
        CHECK(d.motion.state == AF_LOCOMOTION_WALK && body->transform.position.z < s->start_z - 1 &&
              d.emitted_events > 0);
        break;
    case 2:
        CHECK(d.motion.state == AF_LOCOMOTION_RUN && d.weights[2] > .9f);
        break;
    case 3:
        CHECK(s->saw_jump && s->saw_fall);
        CHECK(d.motion.state == AF_LOCOMOTION_FALL || d.motion.state == AF_LOCOMOTION_IDLE);
        break;
    case 4:
        CHECK(d.motion.state == AF_LOCOMOTION_IDLE);
        break;
    case 5: {
        CHECK(d.root_motion && body->transform.position.z < s->start_z - 4);
        af_animation_bone bones[9];
        CHECK(af_engine_animation_bones(engine, s->actor, bones, 9) == 9 && stats.shadow_draw_calls > 0);
        CHECK(af_scene_get_object(s->source, s->body)->transform.position.z == s->start_z);
        CHECK(load(engine, s, s->course));
        break;
    }
    case 6:
        CHECK(d.motion.state == AF_LOCOMOTION_IDLE && af_engine_gameplay_status(engine).checkpoint_count == 2);
        CHECK(af_engine_get_camera(engine).position.z > body->transform.position.z + 3);
        CHECK(af_engine_set_window_size(engine, 800, 600));
        break;
    case 7:
        CHECK(stats.shadow_draw_calls > 0);
        af_engine_request_quit(engine);
        return;
    default:
        CHECK(false);
    }
    ++s->phase;
    input_phase = s->phase;
    edge = true;
}
static void fixed(af_engine *engine, double dt, void *data) {
    test_state *s = data;
    ed_runtime_update(&s->runtime, dt, true);
    af_animation_debug d = af_engine_animation_debug(engine, s->actor);
    if (s->phase == 3) {
        s->saw_jump |= d.motion.state == AF_LOCOMOTION_JUMP;
        s->saw_fall |= d.motion.state == AF_LOCOMOTION_FALL;
    }
    edge = false;
    ++s->ticks;
    af_engine_set_scene(engine, s->runtime.scene);
}
static void shutdown(af_engine *engine, void *data) {
    test_state *s = data;
    af_engine_set_scene(engine, NULL);
    ed_runtime_stop(&s->runtime);
    af_scene_destroy(s->source);
}
int main(int argc, char **argv) {
    if (argc != 4)
        return 2;
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight simple-to-course character verification";
    config.width = 640;
    config.height = 480;
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    test_state s = {.simple = argv[1], .course = argv[2], .module = argv[3]};
    af_game game = {.init = init, .update = update, .fixed_update = fixed, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    if (result || s.failed || s.phase != 7 || stats.validation_errors)
        return 1;
    puts("Character Play: idle/walk/run/jump/fall, footsteps, root motion, third-person startup, course, "
         "skinning/shadows, resize and scene isolation passed.");
    return 0;
}

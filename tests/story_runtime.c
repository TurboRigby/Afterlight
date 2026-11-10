/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include "cube.h"
#include <afterlight/blueprint.h>
#include <afterlight/project.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct story_test {
    af_project project;
    const char *project_path, *save_path;
    ed_runtime runtime;
    unsigned phase, frames;
    bool failed, passed;
} story_test;

#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Story phase %u line %d: %s (%s)\n", t->phase, __LINE__, #x, t->runtime.status);           \
            t->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

static af_object_id object_named(const af_scene *s, const char *name) {
    for (size_t i = 0; i < af_scene_object_count(s); ++i) {
        const af_scene_object *o = af_scene_object_at(s, i);
        if (!strcmp(o->name, name))
            return o->id;
    }
    return AF_OBJECT_NONE;
}

static bool start(story_test *t, af_engine *engine) {
    af_scene_error error = {0};
    af_scene *source = af_scene_load(t->project.startup_scene, &error);
    if (!source) {
        fprintf(stderr, "%s\n", error.message);
        return false;
    }
    bool ok = af_engine_load_scene_assets(engine, source, NULL) &&
              ed_runtime_set_project(&t->runtime, t->project_path, t->project.startup_scene) &&
              ed_runtime_start_file(&t->runtime, source, engine, t->project.game_module);
    af_scene_destroy(source);
    t->runtime.auto_reload = false;
    return ok;
}

static bool init(af_engine *engine, void *data) {
    story_test *t = data;
    af_mesh_data cube = cube_mesh_data();
    af_action_binding binding = {.name = "Interact",
                                 .negative_key = -1,
                                 .positive_key = AF_KEY_F,
                                 .pad_button = -1,
                                 .pad_axis = -1,
                                 .mouse_axis = -1,
                                 .scale = 1};
    return af_engine_upload_mesh(engine, "cube", &cube) && af_engine_bind_action(engine, binding) && start(t, engine);
}

static bool press(SDL_Scancode scancode) {
    SDL_Event event = {.type = SDL_EVENT_KEY_DOWN};
    event.key.scancode = scancode;
    event.key.down = true;
    if (!SDL_PushEvent(&event))
        return false;
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    return SDL_PushEvent(&event);
}

static bool look_at(ed_runtime *r, af_vec3 body, af_vec3 target) {
    af_vec3 eye = af_vec3_add(body, (af_vec3){0, .67f, 0});
    af_vec3 d = af_vec3_sub(target, eye);
    float state[] = {body.x, body.y, body.z, atan2f(d.z, d.x), atan2f(d.y, hypotf(d.x, d.z))};
    return r->api->load_state(r->state, &r->host, 1, state, sizeof(state));
}

static float gate_height(const af_scene *s) {
    const af_scene_object *o = af_scene_get_object(s, object_named(s, "Lift gate"));
    return o ? o->transform.position.y : 0;
}

static void frame(af_engine *engine, double dt, void *data) {
    story_test *t = data;
    ed_runtime *r = &t->runtime;
    ed_runtime_frame_update(r, dt, true);
    if (++t->frames < 4)
        return;
    if (t->phase == 0 && !SDL_GetKeyboardFocus()) {
        CHECK(t->frames < 60);
        if (t->frames == 4) {
            int count = 0;
            SDL_Window **windows = SDL_GetWindows(&count);
            if (windows && count)
                SDL_RaiseWindow(windows[0]);
            SDL_free(windows);
        }
        return;
    }
    const af_story_dialogue *d = af_scene_story_dialogue(r->scene);
    switch (t->phase) {
    case 0: {
        CHECK(af_scene_blueprint_count(r->scene) == 2 && gate_height(r->scene) < 2);
        af_camera c = af_camera_default();
        c.position = (af_vec3){0, 1.55f, -3.1f};
        af_blueprint *terminal = af_scene_get_blueprint(r->scene, 1);
        CHECK(terminal && terminal->bound_object == object_named(r->scene, "Dispatch keypad focus"));
        float range = terminal->interact_range;
        terminal->interact_range = 8.f;
        CHECK(!af_scene_interaction_focus(r->scene, &c, 1).object);
        terminal->interact_range = range;
        CHECK(look_at(r, (af_vec3){-2.4f, .903f, .15f}, (af_vec3){-2.4f, 1.08f, -1.55f}));
        c = af_engine_get_camera(engine);
        af_interaction_focus hit = af_scene_interaction_focus(r->scene, &c, 1);
        CHECK(hit.object == object_named(r->scene, "Relay valve") && hit.prompt && strstr(hit.prompt, "valve"));
        r->paused = true;
        CHECK(press(SDL_SCANCODE_F));
        break;
    }
    case 1:
        CHECK(!af_scene_story_flag(r->scene, "relay_open"));
        r->paused = false;
        CHECK(press(SDL_SCANCODE_F));
        break;
    case 2:
        CHECK(af_scene_story_flag(r->scene, "relay_open"));
        CHECK(gate_height(r->scene) > 4);
        CHECK(af_scene_get_light(r->scene).intensity > 2);
        CHECK(af_engine_audio_stats(engine).plays > 0);
        CHECK(ed_runtime_save_game(r, t->save_path));
        CHECK(af_scene_set_story_flag(r->scene, "relay_open", false) && ed_runtime_load_game(r, t->save_path));
        CHECK(af_scene_story_flag(r->scene, "relay_open"));
        break;
    case 3:
        CHECK(gate_height(r->scene) > 4);
        CHECK(look_at(r, (af_vec3){0, .903f, -6.4f}, (af_vec3){0, 1.18f, -8.1f}));
        CHECK(press(SDL_SCANCODE_F));
        break;
    case 4:
        CHECK(d && !strcmp(d->speaker, "IONA") && d->choices[1][0] && !r->host.input_enabled);
        CHECK(press(SDL_SCANCODE_DOWN));
        break;
    case 5:
        CHECK(d && r->story_choice == 1 && !r->host.input_enabled && press(SDL_SCANCODE_RETURN));
        break;
    case 6:
        CHECK(!strcmp(r->current_scene, "landing") && af_scene_story_flag(r->scene, "sealed_dispatch") &&
              af_scene_story_flag(r->scene, "relay_open") && ed_runtime_save_game(r, t->save_path));
        CHECK(af_scene_set_story_flag(r->scene, "sealed_dispatch", false) && ed_runtime_load_game(r, t->save_path));
        CHECK(af_scene_story_flag(r->scene, "sealed_dispatch"));
        CHECK(look_at(r, (af_vec3){0, .903f, .6f}, (af_vec3){0, 1.17f, -1}));
        CHECK(press(SDL_SCANCODE_F));
        break;
    case 7:
        CHECK(d && strstr(d->speaker, "SEALED"));
        ed_runtime_stop(r);
        CHECK(start(t, engine));
        CHECK(!af_scene_story_flag(r->scene, "sealed_dispatch"));
        CHECK(af_scene_set_story_flag(r->scene, "relay_open", true));
        CHECK(look_at(r, (af_vec3){0, .903f, -6.4f}, (af_vec3){0, 1.18f, -8.1f}));
        CHECK(press(SDL_SCANCODE_F));
        break;
    case 8:
        CHECK(d && !strcmp(d->speaker, "IONA") && r->story_choice == 0 && press(SDL_SCANCODE_RETURN));
        break;
    case 9:
        CHECK(!strcmp(r->current_scene, "landing") && !af_scene_story_flag(r->scene, "sealed_dispatch"));
        CHECK(look_at(r, (af_vec3){0, .903f, .6f}, (af_vec3){0, 1.17f, -1}));
        CHECK(press(SDL_SCANCODE_F));
        break;
    case 10: {
        CHECK(d && strstr(d->speaker, "OPEN BAND"));
        af_engine_stats stats = af_engine_get_stats(engine);
        CHECK(stats.validation_enabled && !stats.validation_errors && !stats.missing_meshes && !stats.missing_textures);
        if (stats.ray_query_supported) {
            CHECK(stats.ray_traced_gi_active && stats.ray_traced_shadows_active && !stats.ray_tracing_failed);
            puts("Signal Annex: hardware ray-traced shadows and GI active.");
        }
        t->passed = true;
        af_engine_request_quit(engine);
        break;
    }
    }
    ++t->phase;
}

int main(int argc, char **argv) {
    if (argc != 3)
        return 1;
    story_test t = {.project_path = argv[1], .save_path = argv[2]};
    af_asset_error error = {0};
    if (!af_project_load(t.project_path, &t.project, &error)) {
        fprintf(stderr, "%s\n", error.message);
        return 1;
    }
    af_engine_config config = af_engine_config_default();
    config.width = 448;
    config.height = 288;
    config.title = "Signal Annex story verification";
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    af_game game = {.init = init, .update = frame, .user_data = &t};
    int result = af_engine_run(engine, &game);
    ed_runtime_stop(&t.runtime);
    af_engine_destroy(engine);
    remove(t.save_path);
    if (t.passed)
        puts("Signal Annex: input remap, paused interaction, valve, both dialogue branches, scene changes and "
             "save/load passed.");
    return result || t.failed || !t.passed;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include "cube.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
    af_scene *scene;
    unsigned frames, ticks, space, r;
    bool failed;
} state;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Fixed loop frame %u: %s\n", s->frames, #c);                                               \
            s->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static void key(SDL_Scancode scancode) {
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = scancode;
    e.key.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    SDL_PushEvent(&e);
}
static bool init(af_engine *engine, void *data) {
    state *s = data;
    s->scene = af_scene_create();
    if (!s->scene)
        return false;
    af_scene_add_object(s->scene, "Tick cube", "cube");
    af_shadow_settings shadow = af_shadow_settings_default();
    shadow.enabled = false;
    shadow.resolution = 512;
    af_scene_set_shadows(s->scene, shadow);
    af_mesh_data cube = cube_mesh_data();
    if (!af_engine_upload_mesh(engine, "cube", &cube))
        return false;
    af_engine_set_scene(engine, s->scene);
    af_engine_set_simulation_paused(engine, true);
    return true;
}
static void update(af_engine *engine, double delta, void *data) {
    (void)delta;
    state *s = data;
    ++s->frames;
    if (s->frames == 1)
        key(SDL_SCANCODE_SPACE);
    if (s->frames == 2)
        CHECK(af_engine_step_simulation(engine));
    if (s->frames == 3) {
        CHECK(s->ticks == 1 && s->space == 1);
        CHECK(af_engine_step_simulation(engine));
    }
    if (s->frames == 4)
        CHECK(s->ticks == 2 && s->space == 1);
    if (s->frames == 6)
        af_engine_set_simulation_paused(engine, false);
    if (s->frames == 7)
        key(SDL_SCANCODE_R);
    if (s->frames == 12)
        SDL_Delay(200);
    if (s->frames == 15) {
        af_engine_stats stats = af_engine_get_stats(engine);
        CHECK(stats.simulation_dropped_seconds > .02 && stats.simulation_steps <= 8);
    }
    if (s->frames == 24) {
        CHECK(s->r == 1 && s->space == 1 && s->ticks > 2);
        CHECK(af_engine_get_stats(engine).simulation_ticks == s->ticks);
        af_engine_request_quit(engine);
    }
}
static void fixed(af_engine *engine, double delta, void *data) {
    state *s = data;
    CHECK(fabs(delta - AF_FIXED_DELTA) < 1e-12);
    ++s->ticks;
    s->space += (unsigned)af_engine_key_pressed(engine, AF_KEY_SPACE);
    s->r += (unsigned)af_engine_key_pressed(engine, AF_KEY_R);
    af_transform t = af_scene_object_at(s->scene, 0)->transform;
    t.rotation.y += .1f;
    CHECK(af_scene_set_object_transform(s->scene, 1, t));
}
static void shutdown(af_engine *engine, void *data) {
    state *s = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(s->scene);
}
int main(void) {
    af_engine_config c = af_engine_config_default();
    c.width = 640;
    c.height = 480;
    c.enable_validation = true;
    c.title = "Afterlight fixed-loop regression";
    af_engine *engine = af_engine_create(&c);
    if (!engine)
        return 1;
    state s = {0};
    af_game game = {.init = init, .update = update, .fixed_update = fixed, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    const char *required = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
    if (required && !strcmp(required, "1") && !stats.validation_enabled)
        return 1;
    if (result || s.failed || stats.validation_errors)
        return 1;
    puts("Fixed loop: real paused steps, edge-trigger input buffering, catch-up limit, interpolation rendering and "
         "tick accounting passed.");
    return 0;
}

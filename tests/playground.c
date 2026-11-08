/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/game_module.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Playground %d: %s\n", __LINE__, #c);                                                      \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static bool forward, jump, restart_key, mouse_capture, teleport;
static unsigned resets, finished;
static af_vec3 target;
static af_gameplay_status status;
static bool down(const af_game_host *host, af_key key) {
    (void)host;
    return key == AF_KEY_W && forward;
}
static bool pressed(const af_game_host *host, af_key key) {
    (void)host;
    return (key == AF_KEY_SPACE && jump) || (key == AF_KEY_R && restart_key);
}
static bool captured(const af_engine *engine) {
    (void)engine;
    return mouse_capture;
}
static bool capture(af_engine *engine, bool enabled) {
    (void)engine;
    mouse_capture = enabled;
    return true;
}
static void reset(af_engine *engine) {
    (void)engine;
    ++resets;
}
static void report(af_engine *engine, af_gameplay_status value) {
    (void)engine;
    status = value;
}
static bool move(const af_scene *scene, af_character *player, af_character_config config, af_character_input input,
                 float delta, af_query_filter filter) {
    if (teleport) {
        *player = (af_character){.position = target};
        teleport = false;
        return true;
    }
    return af_character_step(scene, player, config, input, delta, filter);
}
static void log_message(const char *message) {
    if (strstr(message, "Course complete"))
        ++finished;
}
static af_object_id find(const af_scene *scene, const char *name) {
    for (size_t i = 0; i < af_scene_object_count(scene); ++i)
        if (!strcmp(af_scene_object_at(scene, i)->name, name))
            return af_scene_object_at(scene, i)->id;
    return 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_scene_error error;
    af_scene *scene = af_scene_load(argv[1], &error);
    CHECK(scene);
    af_object_id player = find(scene, "Player"), platform = find(scene, "Moving platform");
    CHECK(player && platform);
    /* Arbitrary names prove gameplay uses components/IDs, not display strings. */
    for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
        char name[64];
        snprintf(name, sizeof(name), "Object %zu", i);
        CHECK(af_scene_set_object_name(scene, af_scene_object_at(scene, i)->id, name));
    }
    af_game_host host = af_game_host_create(scene, NULL);
    host.input_enabled = true;
    host.camera_enabled = false;
    host.key_down = down;
    host.key_pressed = pressed;
    host.mouse_captured = captured;
    host.capture_mouse = capture;
    host.reset_interpolation = reset;
    host.log = log_message;
    host.report_gameplay = report;
    host.move_character = move;
    const af_game_module *api = af_game_get_api(AF_GAME_ABI_VERSION);
    CHECK(api && !af_game_get_api(AF_GAME_ABI_VERSION - 1));
    void *state = calloc(1, api->state_size);
    CHECK(state && api->init(state, &host));
    CHECK(mouse_capture && status.checkpoint_count == 2 && !status.running);
    for (unsigned i = 0; i < 60; ++i)
        api->update(state, &host, AF_FIXED_DELTA);
    CHECK(fabsf(host.get_object(scene, player)->transform.position.y - .902f) < .01f && status.elapsed == 0);
    forward = true;
    for (unsigned i = 0; i < 110; ++i)
        api->update(state, &host, AF_FIXED_DELTA);
    af_vec3 climbed = host.get_object(scene, player)->transform.position;
    CHECK(climbed.y > 2 && climbed.z < 0 && status.elapsed > 1.8);
    jump = true;
    api->update(state, &host, AF_FIXED_DELTA);
    jump = false;
    for (unsigned i = 0; i < 20; ++i)
        api->update(state, &host, AF_FIXED_DELTA);
    CHECK(host.get_object(scene, player)->transform.position.y > climbed.y + .5f);
    CHECK(fabsf(host.get_object(scene, platform)->transform.position.x) > .1f);
    forward = false;
    restart_key = true;
    api->update(state, &host, AF_FIXED_DELTA);
    restart_key = false;
    CHECK(resets >= 2 && fabsf(host.get_object(scene, player)->transform.position.z - 7) < .01f &&
          status.elapsed == 0 && status.checkpoint == 0);
    teleport = true;
    target = (af_vec3){0, 3.1f, -12};
    api->update(state, &host, AF_FIXED_DELTA);
    CHECK(!status.finished && finished == 0);
    teleport = true;
    target = (af_vec3){0, 2.552f, -3};
    api->update(state, &host, AF_FIXED_DELTA);
    CHECK(status.checkpoint == 1);
    teleport = true;
    target = (af_vec3){0, -4, -6};
    api->update(state, &host, AF_FIXED_DELTA);
    CHECK(status.deaths == 1 && fabsf(host.get_object(scene, player)->transform.position.z + 3) < .01f);
    teleport = true;
    target = (af_vec3){0, 2.952f, -6};
    api->update(state, &host, AF_FIXED_DELTA);
    CHECK(status.checkpoint == 2);
    forward = true;
    teleport = true;
    target = (af_vec3){0, 3.1f, -12};
    api->update(state, &host, AF_FIXED_DELTA);
    forward = false;
    CHECK(status.finished && finished == 1 && status.finish_time > 0);
    double finish_time = status.finish_time;
    for (unsigned i = 0; i < 30; ++i)
        api->update(state, &host, AF_FIXED_DELTA);
    CHECK(status.elapsed == finish_time && status.finish_time == finish_time);
    restart_key = true;
    api->update(state, &host, AF_FIXED_DELTA);
    restart_key = false;
    CHECK(!status.finished && status.checkpoint == 0 && status.deaths == 0 && status.elapsed == 0);
    api->shutdown(state, &host);
    CHECK(!mouse_capture);
    free(state);
    af_scene_destroy(scene);
    puts("Playground: component discovery after renaming, spawn, walking, jumping, platform, ordered checkpoints, "
         "respawn, locked goal, timer freeze and new run passed.");
    return 0;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/game_module.h>
#include <math.h>
#ifndef AF_DEMO_TURN_SPEED
#define AF_DEMO_TURN_SPEED 0.7f
#endif
typedef struct game_state {
    double time;
    af_object_id objects[3];
    af_transform base[3];
    unsigned count;
} game_state;
static bool init(void *memory, const af_game_host *host) {
    game_state *state = memory;
    for (size_t i = 0; i < host->object_count(host->scene) && state->count < 3; ++i)
    {
        const af_scene_object *o = host->object_at(host->scene, i);
        if (o->parent || !o->visible)
            continue;
        state->objects[state->count] = o->id;
        state->base[state->count] = o->transform;
        ++state->count;
    }
    host->log("C game initialized.");
    return true;
}
static void update(void *memory, const af_game_host *host, double delta) {
    game_state *state = memory;
    state->time += delta;
    for (unsigned i = 0; i < state->count; ++i)
    {
        if (!host->get_object(host->scene, state->objects[i]))
            continue;
        af_transform t = state->base[i];
        t.rotation.y += (float)fmod(state->time * AF_DEMO_TURN_SPEED, 6.283185307179586);
        if (i == 0)
        {
            state->base[i].position.x += ((float)host->key_down(host, AF_KEY_D) - (float)host->key_down(host, AF_KEY_A)) * (float)delta * 2;
            state->base[i].position.z += ((float)host->key_down(host, AF_KEY_S) - (float)host->key_down(host, AF_KEY_W)) * (float)delta * 2;
            t.position = state->base[i].position;
        }
        host->set_transform(host->scene, state->objects[i], t);
    }
}
static void shutdown(void *memory, const af_game_host *host) {
    (void)memory;
    host->log("C game stopped.");
}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t version) {
    static const af_game_module module = {AF_GAME_ABI_VERSION, sizeof(af_game_module), sizeof(af_game_host), sizeof(af_scene_object), "Afterlight C demo", sizeof(game_state), init, update, shutdown, NULL, 0, NULL, NULL};
    return version == AF_GAME_ABI_VERSION ? &module : NULL;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/* Generated projects use only this C file and the host API; no engine linking. */
#include <afterlight/game_module.h>
#include <math.h>
#include <string.h>
typedef struct game_state {
    af_object_id body;
    af_character player;
    af_character_config config;
    af_camera camera;
    af_vec3 spawn;
    float eye, sensitivity;
} game_state;
static bool init(void *memory, const af_game_host *host) {
    game_state *s = memory;
    for (size_t i = 0; i < host->object_count(host->scene); ++i) {
        const af_scene_object *o = host->object_at(host->scene, i);
        if (o->gameplay.player.enabled) {
            if (s->body)
                return false;
            s->body = o->id;
        }
    }
    const af_scene_object *o = host->get_object(host->scene, s->body);
    if (!o || o->parent || !host->game_camera(host->scene, &s->camera))
        return false;
    s->config = o->gameplay.player.controller;
    s->eye = o->gameplay.camera.eye_height;
    s->sensitivity = o->gameplay.player.mouse_sensitivity;
    s->spawn = o->transform.position;
    const af_scene_object *spawn = host->get_object(host->scene, o->gameplay.player.spawn);
    af_mat4 matrix;
    if (spawn && host->world_matrix(host->scene, spawn->id, &matrix))
        s->spawn = (af_vec3){matrix.elements[12], matrix.elements[13], matrix.elements[14]};
    s->player.position = s->spawn;
    host->capture_mouse(host->engine, true);
    return true;
}
static void frame(void *memory, const af_game_host *host, double delta) {
    (void)delta;
    game_state *s = memory;
    if (host->input_enabled) {
        if (host->key_pressed(host, AF_KEY_ESCAPE))
            host->capture_mouse(host->engine, false);
        else if (host->mouse_pressed(host->engine, AF_MOUSE_LEFT) && host->mouse_in_viewport(host->engine))
            host->capture_mouse(host->engine, true);
        if (host->mouse_captured(host->engine)) {
            af_vec2 m = host->mouse_delta(host->engine);
            s->camera.yaw += m.x * s->sensitivity;
            s->camera.pitch = fmaxf(-1.5f, fminf(1.5f, s->camera.pitch - m.y * s->sensitivity));
        }
    }
    if (host->camera_enabled) {
        s->camera.position = s->player.position;
        s->camera.position.y += s->eye;
        host->set_camera(host->engine, &s->camera);
    }
}
static void tick(void *memory, const af_game_host *host, double delta) {
    game_state *s = memory;
    af_character_input input = {0};
    if (host->input_enabled && host->mouse_captured(host->engine)) {
        float f = (float)host->key_down(host, AF_KEY_W) - (float)host->key_down(host, AF_KEY_S),
              r = (float)host->key_down(host, AF_KEY_D) - (float)host->key_down(host, AF_KEY_A);
        input.move = (af_vec3){cosf(s->camera.yaw) * f - sinf(s->camera.yaw) * r, 0,
                               sinf(s->camera.yaw) * f + cosf(s->camera.yaw) * r};
        input.jump = host->key_pressed(host, AF_KEY_SPACE);
        if (host->key_pressed(host, AF_KEY_R))
            s->player = (af_character){.position = s->spawn};
    }
    host->move_character(host->scene, &s->player, s->config, input, (float)delta,
                         (af_query_filter){.ignore = s->body, .mask = UINT32_MAX});
    if (s->player.position.y < -20) {
        s->player = (af_character){.position = s->spawn};
        host->reset_interpolation(host->engine);
    }
    const af_scene_object *body = host->get_object(host->scene, s->body);
    if (body) {
        af_transform t = body->transform;
        t.position = s->player.position;
        host->set_transform(host->scene, s->body, t);
    }
    if (host->camera_enabled) {
        s->camera.position = s->player.position;
        s->camera.position.y += s->eye;
        host->set_camera(host->engine, &s->camera);
    }
}
static void shutdown(void *memory, const af_game_host *host) {
    (void)memory;
    host->capture_mouse(host->engine, false);
}
static size_t save_state(const void *memory, void *bytes, size_t capacity) {
    const game_state *s = memory;
    float state[] = {s->player.position.x, s->player.position.y, s->player.position.z, s->camera.yaw, s->camera.pitch};
    if (!bytes)
        return sizeof(state);
    if (capacity < sizeof(state))
        return 0;
    memcpy(bytes, state, sizeof(state));
    return sizeof(state);
}

static bool load_state(void *memory, const af_game_host *host, uint32_t version, const void *bytes, size_t size) {
    float state[5];
    if (version != 1 || !bytes || size != sizeof(state))
        return false;
    memcpy(state, bytes, sizeof(state));
    for (size_t i = 0; i < sizeof(state) / sizeof(state[0]); ++i)
        if (!isfinite(state[i]) || fabsf(state[i]) > 1000000.f)
            return false;
    if (fabsf(state[4]) > 1.5f)
        return false;
    game_state *s = memory;
    const af_scene_object *body = host->get_object(host->scene, s->body);
    if (!body)
        return false;
    af_transform transform = body->transform;
    transform.position = (af_vec3){state[0], state[1], state[2]};
    if (!host->set_transform(host->scene, s->body, transform))
        return false;
    s->player = (af_character){.position = transform.position};
    s->camera.position = transform.position;
    s->camera.position.y += s->eye;
    s->camera.yaw = state[3];
    s->camera.pitch = state[4];
    return host->set_camera(host->engine, &s->camera);
}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t version) {
    static const af_game_module api = {AF_GAME_ABI_VERSION,
                                       sizeof(af_game_module),
                                       sizeof(af_game_host),
                                       sizeof(af_scene_object),
                                       "My Afterlight game",
                                       sizeof(game_state),
                                       init,
                                       tick,
                                       shutdown,
                                       frame,
                                       1,
                                       save_state,
                                       load_state};
    return version == AF_GAME_ABI_VERSION ? &api : NULL;
}

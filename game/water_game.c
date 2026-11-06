/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/game_module.h>
#include <math.h>
#include <string.h>
typedef struct water_game {
    af_camera camera;
    bool hard;
    char scene[32];
} water_game;
static bool init(void *memory, const af_game_host *h) {
    water_game *s = memory;
    s->hard = h->object_count(h->scene) > 15;
    strcpy(s->scene, s->hard ? "water-lake" : "water-pool");
    s->camera = (af_camera){.position = {0, 5, 9},
                            .yaw = -AF_PI / 2,
                            .pitch = -.48f,
                            .vertical_fov = AF_PI / 3,
                            .near_clip = .08f,
                            .far_clip = 200};
    h->set_camera(h->engine, &s->camera);
    h->log("Water: right-click captures camera; WASD/QE fly, Space makes ripples, R resets, N switches "
           "pools. F6 pauses.");
    return true;
}
static void update(void *memory, const af_game_host *h, double dt) {
    (void)dt;
    water_game *s = memory;
    if (!h->input_enabled)
        return;
    if (h->key_pressed(h, AF_KEY_SPACE))
        for (size_t i = 0; i < h->object_count(h->scene); ++i) {
            const af_scene_object *o = h->object_at(h->scene, i);
            if (h->get_water(h->scene, o->id))
                h->disturb_water(h->engine, o->id, o->transform.position, 1, 2);
        }
    if (h->request_scene) {
        if (h->key_pressed(h, AF_KEY_R))
            h->request_scene(h, s->scene);
        if (h->key_pressed(h, AF_KEY_N))
            h->request_scene(h, s->hard ? "water-pool" : "water-lake");
    }
}
static void frame(void *memory, const af_game_host *h, double dt) {
    water_game *s = memory;
    if (!h->camera_enabled)
        return;
    if (h->mouse_pressed(h->engine, AF_MOUSE_RIGHT))
        h->capture_mouse(h->engine, !h->mouse_captured(h->engine));
    if (h->key_pressed(h, AF_KEY_ESCAPE))
        h->capture_mouse(h->engine, false);
    if (h->mouse_captured(h->engine)) {
        af_vec2 m = h->mouse_delta(h->engine);
        s->camera.yaw += m.x * .0025f;
        s->camera.pitch = fmaxf(-1.5f, fminf(1.5f, s->camera.pitch - m.y * .0025f));
        float speed = (h->key_down(h, AF_KEY_LEFT_SHIFT) ? 12 : 4) * (float)dt;
        float f = (h->key_down(h, AF_KEY_W) ? 1.f : 0) - (h->key_down(h, AF_KEY_S) ? 1.f : 0),
              r = (h->key_down(h, AF_KEY_D) ? 1.f : 0) - (h->key_down(h, AF_KEY_A) ? 1.f : 0);
        s->camera.position.x += speed * (cosf(s->camera.yaw) * f - sinf(s->camera.yaw) * r);
        s->camera.position.z += speed * (sinf(s->camera.yaw) * f + cosf(s->camera.yaw) * r);
        s->camera.position.y +=
            speed * ((h->key_down(h, AF_KEY_E) ? 1.f : 0) - (h->key_down(h, AF_KEY_Q) ? 1.f : 0));
    }
    h->set_camera(h->engine, &s->camera);
}
static void shutdown(void *s, const af_game_host *h) {
    (void)s;
    (void)h;
}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t abi) {
    static const af_game_module api = {.abi_version = AF_GAME_ABI_VERSION,
                                       .struct_size = sizeof(api),
                                       .host_size = sizeof(af_game_host),
                                       .object_size = sizeof(af_scene_object),
                                       .name = "Water laboratory",
                                       .state_size = sizeof(water_game),
                                       .init = init,
                                       .update = update,
                                       .frame_update = frame,
                                       .shutdown = shutdown};
    return abi == AF_GAME_ABI_VERSION ? &api : NULL;
}

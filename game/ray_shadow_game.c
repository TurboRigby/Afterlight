/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/game_module.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct lab {
    double time;
    af_object_id arm;
    af_transform base;
    bool frozen;
} lab;
static bool init(void *memory, const af_game_host *h) {
    lab *s = memory;
    for (size_t i = 0; i < h->object_count(h->scene); ++i) {
        const af_scene_object *o = h->object_at(h->scene, i);
        if (!strcmp(o->name, "Moving shadow arm")) {
            s->arm = o->id;
            s->base = o->transform;
        }
    }
    af_camera camera;
    if (h->game_camera(h->scene, &camera))
        h->set_camera(h->engine, &camera);
    h->capture_mouse(h->engine, false);
    return s->arm != 0;
}
static void frame(void *memory, const af_game_host *h, double dt) {
    (void)dt;
    lab *s = memory;
    if (h->input_enabled) {
        if (h->key_pressed(h, AF_KEY_Q)) {
            af_render_settings r = h->get_render_settings(h->scene);
            r.ray_traced_shadows = !r.ray_traced_shadows;
            h->set_render_settings(h->scene, r);
        }
        if (h->key_pressed(h, AF_KEY_SPACE))
            s->frozen = !s->frozen;
        if (h->key_pressed(h, AF_KEY_R))
            s->time = 0;
    }
    af_engine_stats stats = h->get_stats(h->engine);
    af_gameplay_status status = {0};
    snprintf(status.hint, sizeof(status.hint), "Sun: %s | Q compare | Space freeze | R reset",
             stats.ray_traced_shadows_active ? "hardware rays"
             : stats.ray_query_supported     ? "raster maps"
                                             : "raster (ray queries unavailable)");
    h->report_gameplay(h->engine, status);
}
static void update(void *memory, const af_game_host *h, double dt) {
    lab *s = memory;
    if (!s->frozen)
        s->time += dt;
    af_transform t = s->base;
    t.rotation.y += (float)sin(s->time * .65) * 1.2f;
    t.position.x += (float)sin(s->time * .4) * .8f;
    h->set_transform(h->scene, s->arm, t);
}
static void shutdown(void *memory, const af_game_host *h) {
    (void)memory;
    h->capture_mouse(h->engine, false);
}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t abi) {
    static const af_game_module api = {.abi_version = AF_GAME_ABI_VERSION,
                                       .struct_size = sizeof(api),
                                       .host_size = sizeof(af_game_host),
                                       .object_size = sizeof(af_scene_object),
                                       .name = "Ray shadow comparison lab",
                                       .state_size = sizeof(lab),
                                       .init = init,
                                       .update = update,
                                       .shutdown = shutdown,
                                       .frame_update = frame};
    return abi == AF_GAME_ABI_VERSION ? &api : NULL;
}

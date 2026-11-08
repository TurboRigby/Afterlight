/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/game_module.h>
#include <string.h>
typedef struct state {
    uint32_t ticks;
} state;
static bool init(void *data, const af_game_host *host) {
    (void)host;
    *(state *)data = (state){0};
    return true;
}
static void update(void *data, const af_game_host *h, double dt) {
    (void)dt;
    state *s = data;
    ++s->ticks;
    const af_scene_object *o = h->object_at(h->scene, 0);
    af_transform t = o->transform;
    t.position.x = (float)s->ticks;
    h->set_transform(h->scene, o->id, t);
}
static void shutdown(void *data, const af_game_host *host) {
    (void)data;
    (void)host;
}
static size_t save(const void *data, void *out, size_t capacity) {
    if (!out)
        return sizeof(state);
    if (capacity < sizeof(state))
        return 0;
    memcpy(out, data, sizeof(state));
    return sizeof(state);
}
static bool load(void *data, const af_game_host *h, uint32_t version, const void *bytes, size_t size) {
    (void)h;
#ifdef REJECT_STATE
    (void)data;
    (void)version;
    (void)bytes;
    (void)size;
    return false;
#else
    if (version != 1 || size != sizeof(state))
        return false;
    memcpy(data, bytes, size);
    return true;
#endif
}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t version) {
    static const af_game_module api = {.abi_version = AF_GAME_ABI_VERSION,
                                       .struct_size = sizeof(af_game_module),
                                       .host_size = sizeof(af_game_host),
                                       .object_size = sizeof(af_scene_object),
                                       .name = "State migration fixture",
                                       .state_size = sizeof(state),
                                       .init = init,
                                       .update = update,
                                       .shutdown = shutdown,
                                       .state_version = 1,
                                       .save_state = save,
                                       .load_state = load};
    return version == AF_GAME_ABI_VERSION ? &api : NULL;
}

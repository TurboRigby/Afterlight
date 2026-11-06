/* SPDX-License-Identifier: MPL-2.0 */
#include "scene_internal.h"
#include "water_internal.h"
#include <afterlight/water.h>
#include <math.h>
#include <string.h>
af_water af_water_default(void) {
    return (af_water){.enabled = true,
                      .quality = AF_WATER_MEDIUM,
                      .half_size = {4, 4},
                      .depth = 2,
                      .density = 1000,
                      .drag = 1.5f,
                      .wave_speed = 3,
                      .damping = .8f,
                      .wind = .015f,
                      .absorption = {.7f, .12f, .055f},
                      .scattering = {.03f, .18f, .22f},
                      .roughness = .16f,
                      .ior = 1.333f};
}
static bool range(float v, float a, float b) { return isfinite(v) && v >= a && v <= b; }
bool af_water_valid(af_water w) {
    return w.mode == AF_WATER_POOL && w.quality >= AF_WATER_LOW && w.quality <= AF_WATER_HIGH &&
           range(w.half_size.x, .25f, 1000) && range(w.half_size.y, .25f, 1000) &&
           range(w.depth, .05f, 100) && range(w.density, .01f, 20000) && range(w.drag, 0, 20) &&
           range(w.wave_speed, .01f, 20) && range(w.damping, 0, 20) && range(w.wind, 0, 1) &&
           range(w.roughness, .04f, 1) && range(w.ior, 1, 2.5f) && range(w.absorption.x, 0, 100) &&
           range(w.absorption.y, 0, 100) && range(w.absorption.z, 0, 100) &&
           range(w.scattering.x, 0, 1) && range(w.scattering.y, 0, 1) && range(w.scattering.z, 0, 1);
}
size_t af_scene_water_count(const af_scene *s) { return s ? s->water_count : 0; }
const af_water *af_scene_water_at(const af_scene *s, size_t i) {
    return s && i < s->water_count ? &s->waters[i] : NULL;
}
const af_water *af_scene_get_water(const af_scene *s, af_object_id id) {
    for (size_t i = 0; s && i < s->water_count; ++i)
        if (s->waters[i].owner == id)
            return &s->waters[i];
    return NULL;
}
bool wa_owner_valid(const af_scene *s, af_object_id id, af_water w) {
    const af_scene_object *o = af_scene_get_object(s, id);
    if (!o || !af_water_valid(w) || o->parent || o->body.enabled ||
        (o->collider.enabled && o->collider.type != AF_COLLIDER_NONE) || strcmp(o->mesh, "__node") ||
        fabsf(o->transform.rotation.x) > 1e-6f || fabsf(o->transform.rotation.z) > 1e-6f ||
        fabsf(o->transform.scale.x - 1) > 1e-6f || fabsf(o->transform.scale.y - 1) > 1e-6f ||
        fabsf(o->transform.scale.z - 1) > 1e-6f)
        return false;
    return true;
}
bool af_scene_set_water(af_scene *s, af_object_id id, af_water w) {
    if (!wa_owner_valid(s, id, w))
        return false;
    size_t i = 0;
    for (; i < s->water_count && s->waters[i].owner != id; ++i) {
    }
    if (i == s->water_count) {
        if (i == AF_SCENE_MAX_WATERS)
            return false;
        ++s->water_count;
    }
    w.owner = id;
    s->waters[i] = w;
    return true;
}
bool af_scene_remove_water(af_scene *s, af_object_id id) {
    for (size_t i = 0; s && i < s->water_count; ++i)
        if (s->waters[i].owner == id) {
            memmove(&s->waters[i], &s->waters[i + 1], (--s->water_count - i) * sizeof(*s->waters));
            return true;
        }
    return false;
}

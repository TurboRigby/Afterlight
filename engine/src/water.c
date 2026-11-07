/* SPDX-License-Identifier: MPL-2.0 */
#include "scene_internal.h"
#include "water_internal.h"
#include <afterlight/water.h>
#include <math.h>
#include <string.h>
af_water af_water_default(void) {
    return (af_water){.enabled = true,
                      .ripple_scale=1,.ripple_strength=.18f,
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
                      .ior = 1.333f,
                      .flow_speed = 1.5f,
                      .path_count = 2,
                      .path = {{0, 0, -4}, {0, 0, 4}},
                      .particle_spacing = .16f,
                      .initial_volume = .25f,
                      .emission_rate = 0,
                      .viscosity = 3,
                      .emission_velocity = {0, -1, 0}};
}
static bool range(float v, float a, float b) { return isfinite(v) && v >= a && v <= b; }
bool af_water_valid(af_water w) {
    if (w.quality < AF_WATER_LOW || w.quality > AF_WATER_HIGH)
        return false;
    if (w.mode == AF_WATER_PARTICLES && range(w.particle_spacing, .06f, .5f)) {
        float max_volume = (float)(256u << (2u * (unsigned)w.quality)) * w.particle_spacing *
                           w.particle_spacing * w.particle_spacing;
        if (w.initial_volume > max_volume + .00001f ||
            w.initial_volume > 4 * w.half_size.x * w.half_size.y * w.depth)
            return false;
    }
    if (w.path_count < 2 || w.path_count > AF_WATER_MAX_PATH)
        return false;
    for (unsigned i = 0; i < w.path_count; ++i)
        if (!range(w.path[i].x, -1000, 1000) || !range(w.path[i].y, -100, 100) ||
            !range(w.path[i].z, -1000, 1000))
            return false;
    if (w.mode == AF_WATER_PARTICLES && range(w.particle_spacing, .06f, .5f) &&
        range(w.half_size.x, .25f, 1000) && range(w.half_size.y, .25f, 1000) &&
        range(w.depth, .05f, 100)) {
        float s = w.particle_spacing;
        float nx = fminf(64, floorf(2 * w.half_size.x / s)),
              nz = fminf(64, floorf(2 * w.half_size.y / s)), ny = floorf(w.depth / s);
        if (w.initial_volume > nx * nz * ny * s * s * s + .00001f)
            return false;
    }
    if (w.mode == AF_WATER_RIVER) {
        if (w.path_count < 2 || w.path_count > AF_WATER_MAX_PATH)
            return false;
        for (unsigned i = 0; i < w.path_count; ++i) {
            af_vec3 p = w.path[i];
            if (!range(p.x, -1000, 1000) || !range(p.y, -100, 100) || !range(p.z, -1000, 1000))
                return false;
            if (i) {
                af_vec3 d = af_vec3_sub(p, w.path[i - 1]);
                float length = hypotf(d.x, d.z);
                if (length < .5f || fabsf(d.y) > length * .5f)
                    return false;
                if (i > 1) {
                    af_vec3 old = af_vec3_sub(w.path[i - 1], w.path[i - 2]);
                    float dot = (d.x * old.x + d.z * old.z) / (length * hypotf(old.x, old.z));
                    if (dot < .5f)
                        return false;
                }
            }
        }
    }
    return range(w.ripple_scale,.1f,20) && range(w.ripple_strength,0,1) && range(w.particle_spacing, .06f, .5f) && range(w.initial_volume, 0, 100) &&
           range(w.emission_rate, 0, 2) && range(w.viscosity, 0, 20) &&
           range(w.emission_velocity.x, -20, 20) && range(w.emission_velocity.y, -20, 20) &&
           range(w.emission_velocity.z, -20, 20) &&
           (w.mode == AF_WATER_POOL || w.mode == AF_WATER_RIVER || w.mode == AF_WATER_PARTICLES) &&
           range(w.flow_speed, 0, 20) && w.quality >= AF_WATER_LOW && w.quality <= AF_WATER_HIGH &&
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
        fabsf(o->transform.position.x) > 1000000 || fabsf(o->transform.position.y) > 1000000 ||
        fabsf(o->transform.position.z) > 1000000 || fabsf(o->transform.rotation.x) > 1e-6f ||
        fabsf(o->transform.rotation.z) > 1e-6f || fabsf(o->transform.scale.x - 1) > 1e-6f ||
        fabsf(o->transform.scale.y - 1) > 1e-6f || fabsf(o->transform.scale.z - 1) > 1e-6f)
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

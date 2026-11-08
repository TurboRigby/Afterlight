/* SPDX-License-Identifier: MPL-2.0 */
#include "physics_internal.h"
#include "water_internal.h"
#include "water_particles_internal.h"
#include <stdlib.h>
#include <string.h>
const af_water_runtime *ph_water_runtime(const af_physics *p) {
    return p ? p->water : NULL;
}
af_water_sample af_physics_sample_water(const af_physics *p, const af_scene *s, af_vec3 point) {
    return wa_sample(p ? p->water : NULL, s, point, 1);
}
af_water_stats af_physics_water_stats(const af_physics *p) {
    af_water_stats stats = p && p->water ? p->water->stats : (af_water_stats){0};
    for (size_t k = 0; p && p->water && k < p->water->count; ++k) {
        const wa_slot *slot = &p->water->slots[k];
        const wa_particles *f = slot->particles;
        stats.sleeping_surfaces += slot->sleeping ? 1u : 0u;
        if (f) {
            stats.particles += f->count;
            stats.sleeping_particles += f->sleeping ? f->count : 0;
            stats.emitted_volume += f->emitted;
            stats.escaped_volume += f->escaped;
            stats.particle_volume += (float)f->count * f->volume;
        }
    }
    for (size_t i = 0; p && i < p->count; ++i)
        stats.wet_bodies += p->bodies[i].wet ? 1u : 0u;
    return stats;
}
bool af_physics_disturb_water(af_physics *p, const af_scene *s, af_object_id id, af_vec3 point, float radius,
                              float velocity) {
    if (!p || !s)
        return false;
    if (!p->water)
        p->water = calloc(1, sizeof(*p->water));
    if (!p->water)
        return false;
    if (!wa_sync(p->water, s))
        return false;
    p->water->time = p->simulation_time;
    return wa_disturb(p->water, s, id, point, radius, velocity);
}
bool ph_water_sync(af_physics *p, const af_scene *s) {
    if (!af_scene_water_count(s) && !p->water)
        return true;
    if (!p->water)
        p->water = calloc(1, sizeof(*p->water));
    if (!p->water)
        return false;
    if (!wa_sync(p->water, s))
        return false;
    p->water->time = p->simulation_time;
    for (size_t i = 0; i < p->count; ++i) {
        ph_body *b = &p->bodies[i];
        if (!b->sleeping)
            continue;
        af_water_sample sample = wa_sample(p->water, s, b->p, 1);
        bool calm = false;
        for (size_t k = 0; k < p->water->count; ++k)
            if (p->water->slots[k].config.owner == sample.owner)
                calm = p->water->slots[k].sleeping && p->water->slots[k].config.mode == AF_WATER_POOL;
        if ((b->wet && !calm) || b->water_key != p->water->key)
            ph_wake_island(p, i);
    }
    for (size_t i = 0; i < p->water->count; ++i) {
        wa_slot *a = &p->water->slots[i];
        memcpy(a->previous, a->h, sizeof(a->h));
        if (a->particles)
            for (size_t k = 0; k < a->particles->count; ++k)
                a->particles->values[k].previous = a->particles->values[k].p;
    }
    return true;
}
static float cap(float r, float y, float *moment) {
    y = fmaxf(-r, fminf(r, y));
    float h = y + r;
    *moment = -AF_PI * .25f * (r * r - y * y) * (r * r - y * y);
    return AF_PI * h * h * (r - h / 3);
}
static void wet_point(af_physics *p, ph_body *b, af_water_sample s, af_vec3 point, float volume, float dt) {
    if (volume <= 1e-10f)
        return;
    const af_water *w = af_scene_get_water(p->scene, s.owner);
    if (!w)
        return;
    b->wet = true;
    b->sleeping = false;
    af_vec3 r = af_vec3_sub(point, b->p);
    float displaced = w->density * volume;
    ph_impulse(b, (af_vec3){0, 9.81f * displaced * dt, 0}, point);
    af_vec3 velocity = af_vec3_sub(af_vec3_add(b->v, af_vec3_cross(b->w, r)), s.velocity);
    float speed = af_vec3_length(velocity);
    if (speed > 1e-7f && w->drag > 0) {
        af_vec3 n = af_vec3_scale(velocity, 1 / speed);
        float inverse = b->inv_mass + af_vec3_dot(n, af_vec3_cross(ph_inverse_inertia(b, af_vec3_cross(r, n)), r));
        float drag = w->drag * displaced * (1 + .2f * speed);
        if (inverse > 0)
            ph_impulse(b, af_vec3_scale(velocity, -(-expm1f(-drag * inverse * dt)) / inverse), point);
    }
    p->water->stats.displaced_volume += volume / (float)p->stats.substeps;
}
void ph_water_step(af_physics *p, float dt) {
    if (!p->water)
        return;
    double stamp = ph_clock(p);
    wa_step(p->water, dt);
    for (size_t i = 0; i < p->count; ++i) {
        ph_body *b = &p->bodies[i];
        if (b->inv_mass == 0)
            continue;
        bool calm = true;
        for (size_t k = 0; k < p->water->count; ++k) {
            const wa_slot *slot = &p->water->slots[k];
            if (slot->config.enabled && slot->config.mode != AF_WATER_PARTICLES)
                calm &= slot->sleeping && slot->config.mode == AF_WATER_POOL;
        }
        if (b->sleeping && calm && b->water_key == p->water->key) {
            b->wet = b->water_support;
            continue;
        }
        b->water_calm = calm;
        b->water_support = false;
        b->water_key = p->water->key;
        float displaced = 0;
        af_water_sample touched = {0};
        for (size_t part = 0; part < (b->part_count ? b->part_count : 1); ++part) {
            af_world_collider w = ph_part_world(b, part);
            if (w.type == AF_COLLIDER_SPHERE) {
                af_water_sample s = wa_sample(p->water, p->scene, w.center, 1);
                if (!s.inside)
                    continue;
                float m1, m0, v1 = cap(w.radius, s.height - w.center.y, &m1),
                              v0 = cap(w.radius, s.bottom - w.center.y, &m0), volume = v1 - v0;
                if (volume > 1e-10f) {
                    af_vec3 point = w.center;
                    point.y += (m1 - m0) / volume;
                    wet_point(p, b, s, point, volume, dt);
                    displaced += volume;
                    touched = s;
                }
            } else if (w.type == AF_COLLIDER_BOX) {
                /* Quality controls actual submerged-volume integration, including torque. */
                af_water_sample at = wa_sample(p->water, p->scene, w.center, 1);
                unsigned n = at.inside ? 2u + (unsigned)af_scene_get_water(p->scene, at.owner)->quality : 2;
                af_vec3 half = af_vec3_scale(w.half_extents, 1 / (float)n);
                float tile = 8 * half.x * half.y * half.z;
                float hy = fabsf(w.axes[0].y) * half.x + fabsf(w.axes[1].y) * half.y + fabsf(w.axes[2].y) * half.z;
                for (unsigned z = 0; z < n; ++z)
                    for (unsigned y = 0; y < n; ++y)
                        for (unsigned x = 0; x < n; ++x) {
                            af_vec3 q = {((float)x * 2 + 1 - (float)n) * half.x, ((float)y * 2 + 1 - (float)n) * half.y,
                                         ((float)z * 2 + 1 - (float)n) * half.z};
                            af_vec3 point = af_vec3_add(
                                w.center,
                                af_vec3_add(af_vec3_scale(w.axes[0], q.x),
                                            af_vec3_add(af_vec3_scale(w.axes[1], q.y), af_vec3_scale(w.axes[2], q.z))));
                            af_water_sample s = wa_sample(p->water, p->scene, point, 1);
                            if (!s.inside)
                                continue;
                            float lo = fmaxf(point.y - hy, s.bottom), hi = fminf(point.y + hy, s.height);
                            float volume = tile * fmaxf(0, hi - lo) / (2 * hy);
                            if (volume > 1e-10f) {
                                point.y = (lo + hi) * .5f;
                                wet_point(p, b, s, point, volume, dt);
                                displaced += volume;
                                touched = s;
                            }
                        }
            }
        }
        if (displaced > 0 && touched.inside && fabsf(b->p.y - touched.height) < 1 && fabsf(b->v.y) > .2f)
            wa_disturb(p->water, p->scene, touched.owner, b->p, fmaxf(.3f, cbrtf(displaced)),
                       fmaxf(-.1f, fminf(.1f, b->v.y * displaced * dt * .3f)));
        if (touched.inside && calm)
            b->water_support =
                fabsf(displaced * touched.density - b->config.mass * b->config.gravity_scale) < b->config.mass * .08f;
        b->v = ph_limit(b->v, 200);
        b->w = ph_limit(b->w, 100);
    }
    for (size_t i = 0; i < p->water->count; ++i)
        if (p->water->slots[i].config.enabled && p->water->slots[i].config.mode == AF_WATER_PARTICLES)
            ph_particles_step(p, &p->water->slots[i], dt);
    p->water->stats.milliseconds += ph_clock(p) - stamp;
}

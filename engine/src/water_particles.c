/* SPDX-License-Identifier: MPL-2.0 */
/* Bounded CPU position-based fluid. Equations follow Macklin/Mueller's PBF
   density projection with non-negative pressure, Poly6 and Spiky kernels.
   https://matthias-research.github.io/pages/publications/pbf_sig_preprint.pdf */
#include "collision_internal.h"
#include "physics_internal.h"
#include "water_particles_internal.h"
#include <stdlib.h>
#include <string.h>
static af_vec3 rotate(af_transform t, af_vec3 p) {
    float c = cosf(t.rotation.y), s = sinf(t.rotation.y);
    return (af_vec3){c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
}
static uint32_t hash(int x, int y, int z) {
    return ((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u) &
           (WA_HASH_SIZE - 1);
}
static void grid(wa_particles *f) {
    for (unsigned i = 0; i < WA_HASH_SIZE; ++i)
        f->heads[i] = UINT32_MAX;
    float h = 2 * f->spacing;
    for (size_t i = 0; i < f->count; ++i) {
        wa_particle *p = &f->values[i];
        p->cell[0] = (int)floorf(p->q.x / h);
        p->cell[1] = (int)floorf(p->q.y / h);
        p->cell[2] = (int)floorf(p->q.z / h);
        uint32_t k = hash(p->cell[0], p->cell[1], p->cell[2]);
        p->next = f->heads[k];
        f->heads[k] = (uint32_t)i;
    }
}
static bool cell(const wa_particle *p, int x, int y, int z) {
    return p->cell[0] == x && p->cell[1] == y && p->cell[2] == z;
}
static float kernel(float d2, float h) {
    float x = h * h - d2;
    if (x <= 0)
        return 0;
    float h3 = h * h * h;
    return 315.f / (64 * AF_PI * h3 * h3 * h3) * x * x * x;
}
static af_vec3 gradient(af_vec3 d, float h, size_t i, size_t j) {
    float r = af_vec3_length(d);
    if (r >= h)
        return (af_vec3){0};
    if (r < 1e-6f) {
        d = ph_axis((unsigned)((i + j) % 3));
        if (i < j)
            d = af_vec3_scale(d, -1);
        r = 1e-4f * h;
        d = af_vec3_scale(d, r);
    }
    float h3 = h * h * h;
    return af_vec3_scale(d, -45 / (AF_PI * h3 * h3) * (h - r) * (h - r) / r);
}
static void birth(wa_slot *slot, size_t count, bool initial) {
    wa_particles *f = slot->particles;
    af_water w = slot->config;
    unsigned nx = (unsigned)fmaxf(1, floorf(2 * w.half_size.x / f->spacing)),
             nz = (unsigned)fmaxf(1, floorf(2 * w.half_size.y / f->spacing));
    if (nx > 64)
        nx = 64;
    if (nz > 64)
        nz = 64;
    for (size_t n = 0; n < count && f->count < f->capacity; ++n) {
        uint64_t serial = f->births++;
        unsigned x = (unsigned)(serial % nx), z = (unsigned)((serial / nx) % nz),
                 layer = (unsigned)(serial / (nx * nz));
        af_vec3 local = {((float)x + .5f - (float)nx * .5f) * f->spacing,
                         initial ? -(float)layer * f->spacing : 0,
                         ((float)z + .5f - (float)nz * .5f) * f->spacing};
        af_vec3 position = af_vec3_add(slot->transform.position, rotate(slot->transform, local));
        if (!initial) {
            bool blocked = false;
            for (size_t i = 0; i < f->count; ++i)
                if (af_vec3_length(af_vec3_sub(f->values[i].p, position)) < f->spacing * .8f) {
                    blocked = true;
                    break;
                }
            if (blocked) {
                --f->births;
                break;
            }
        }
        wa_particle *p = &f->values[f->count++];
        *p = (wa_particle){.p = position,
                           .previous = position,
                           .q = position,
                           .v = rotate(slot->transform, w.emission_velocity),
                           .hit_body = SIZE_MAX};
        f->emitted += f->volume;
    }
}
void wa_particles_free(wa_particles *p) { free(p); }
bool wa_particles_sync(wa_slot *a) {
    size_t capacity = 256u << (2u * (unsigned)a->config.quality);
    if (a->particles &&
        (a->particles->capacity != capacity || a->particles->spacing != a->config.particle_spacing)) {
        wa_particles_free(a->particles);
        a->particles = NULL;
    }
    if (!a->particles) {
        a->particles = calloc(1, sizeof(*a->particles));
        if (!a->particles)
            return false;
        wa_particles *f = a->particles;
        f->capacity = capacity;
        f->spacing = a->config.particle_spacing;
        f->volume = f->spacing * f->spacing * f->spacing;
        f->initial_volume = a->config.initial_volume;
        size_t count =
            (size_t)fminf((float)capacity, floorf(a->config.initial_volume / f->volume + .0001f));
        birth(a, count, true);
    }
    return true;
}
size_t af_physics_water_particles(const af_physics *p, af_object_id id, af_water_particle *out,
                                  size_t capacity) {
    const af_water_runtime *r = ph_water_runtime(p);
    for (size_t k = 0; r && k < r->count; ++k)
        if (r->slots[k].config.owner == id) {
            const wa_particles *f = r->slots[k].particles;
            if (!f)
                return 0;
            for (size_t i = 0; out && i < f->count && i < capacity; ++i)
                out[i] = (af_water_particle){f->values[i].p, f->values[i].v, f->volume};
            return f->count;
        }
    return 0;
}
static bool near_shape(af_world_collider w, af_vec3 a, af_vec3 b, float radius) {
    af_vec3 h = {w.radius, w.radius, w.radius};
    if (w.type == AF_COLLIDER_MESH)
        return true;
    if (w.type == AF_COLLIDER_BOX) {
        h = (af_vec3){0};
        for (unsigned k = 0; k < 3; ++k) {
            float e = ph_get(w.half_extents, k);
            h.x += fabsf(w.axes[k].x) * e;
            h.y += fabsf(w.axes[k].y) * e;
            h.z += fabsf(w.axes[k].z) * e;
        }
    } else if (w.type == AF_COLLIDER_CAPSULE) {
        h.x += fabsf(w.a.x - w.b.x) * .5f;
        h.y += fabsf(w.a.y - w.b.y) * .5f;
        h.z += fabsf(w.a.z - w.b.z) * .5f;
    }
    return fminf(a.x, b.x) <= w.center.x + h.x + radius &&
           fmaxf(a.x, b.x) >= w.center.x - h.x - radius &&
           fminf(a.y, b.y) <= w.center.y + h.y + radius &&
           fmaxf(a.y, b.y) >= w.center.y - h.y - radius &&
           fminf(a.z, b.z) <= w.center.z + h.z + radius && fmaxf(a.z, b.z) >= w.center.z - h.z - radius;
}
static af_world_collider moving_shape(const ph_body *body, size_t part, float fraction) {
    if (!body->water_motion)
        return ph_part_world(body, part);
    ph_body at = *body;
    at.p = af_vec3_add(body->water_previous_p,
                       af_vec3_scale(af_vec3_sub(body->p, body->water_previous_p), fraction));
    for (unsigned k = 0; k < 2; ++k) {
        at.axes[k] = af_vec3_add(
            body->water_previous_axes[k],
            af_vec3_scale(af_vec3_sub(body->axes[k], body->water_previous_axes[k]), fraction));
        if (af_vec3_length(at.axes[k]) < 1e-5f)
            at.axes[k] = body->axes[k];
    }
    at.axes[0] = af_vec3_normalize(at.axes[0]);
    at.axes[1] = af_vec3_sub(at.axes[1], af_vec3_scale(at.axes[0], af_vec3_dot(at.axes[0], at.axes[1])));
    if (af_vec3_length(at.axes[1]) < 1e-5f)
        at.axes[1] = body->axes[1];
    at.axes[1] = af_vec3_normalize(at.axes[1]);
    at.axes[2] = af_vec3_cross(at.axes[0], at.axes[1]);
    at.shape.center = at.shape.a = at.shape.b = at.p;
    for (unsigned k = 0; k < 3; ++k)
        at.shape.axes[k] = at.axes[k];
    return ph_part_world(&at, part);
}
static void contact(wa_particle *p, size_t body, af_collision_hit hit) {
    p->normal = hit.normal;
    p->point = hit.point;
    p->hit_body = body;
    for (unsigned i = 0; i < p->contact_count; ++i)
        if (p->contacts[i].body == body && af_vec3_dot(p->contacts[i].normal, hit.normal) > .97f) {
            p->contacts[i] = (wa_fluid_contact){body, hit.normal, hit.point};
            return;
        }
    if (p->contact_count < 4)
        p->contacts[p->contact_count++] = (wa_fluid_contact){body, hit.normal, hit.point};
}
static void collide(af_physics *world, wa_particle *p, float radius, bool sweep) {
    for (size_t b = 0; b < world->count; ++b) {
        ph_body *body = &world->bodies[b];
        if (body->shape.trigger || body->character_proxy)
            continue;
        for (size_t k = 0; k < (body->part_count ? body->part_count : 1); ++k) {
            float start = (float)world->water_substep / (float)world->stats.substeps,
                  end = (float)(world->water_substep + 1) / (float)world->stats.substeps;
            af_world_collider w = moving_shape(body, k, end);
            af_vec3 origin = p->p;
            if (body->water_motion) {
                af_world_collider old = moving_shape(body, k, start);
                af_vec3 d = af_vec3_sub(p->p, old.center);
                origin = w.center;
                for (unsigned axis = 0; axis < 3; ++axis)
                    origin =
                        af_vec3_add(origin, af_vec3_scale(w.axes[axis], af_vec3_dot(d, old.axes[axis])));
            }
            if (!near_shape(w, origin, p->q, radius))
                continue;
            af_collision_hit hit;
            if (sweep &&
                af_collider_sweep_sphere(w, origin, radius, af_vec3_sub(p->q, origin), .0002f, &hit)) {
                af_vec3 displacement = af_vec3_sub(p->q, origin),
                        remaining = af_vec3_scale(displacement, 1 - hit.fraction);
                float into = af_vec3_dot(remaining, hit.normal);
                if (into < 0)
                    remaining = af_vec3_sub(remaining, af_vec3_scale(hit.normal, into));
                p->q = af_vec3_add(af_vec3_add(origin, af_vec3_scale(displacement, hit.fraction)),
                                   af_vec3_add(remaining, af_vec3_scale(hit.normal, .0003f)));
                contact(p, b, hit);
            }
            float separation = af_collision_capsule_distance(w, p->q, radius, 0, &hit);
            if (separation < .0002f) {
                p->q =
                    af_vec3_add(p->q, af_vec3_scale(hit.normal, fminf(radius * 4, .0002f - separation)));
                contact(p, b, hit);
            }
        }
    }
}
static void couple(af_physics *world, wa_particle *p, af_vec3 incoming, float mass) {
    for (unsigned k = 0; k < p->contact_count; ++k) {
        wa_fluid_contact c = p->contacts[k];
        ph_body *b = &world->bodies[c.body];
        af_vec3 r = af_vec3_sub(c.point, b->p), surface = af_vec3_add(b->v, af_vec3_cross(b->w, r));
        float vn = af_vec3_dot(af_vec3_sub(incoming, surface), c.normal);
        float projected = af_vec3_dot(af_vec3_sub(p->v, surface), c.normal);
        if (vn >= 0) {
            if (projected < 0)
                p->v = af_vec3_sub(p->v, af_vec3_scale(c.normal, projected));
            continue;
        }
        if (b->inv_mass > 0) {
            b->sleeping = false;
            b->sleep_time = 0;
            b->wet = true;
            b->water_calm = false;
        }
        float inverse =
            b->inv_mass +
            af_vec3_dot(c.normal, af_vec3_cross(ph_inverse_inertia(b, af_vec3_cross(r, c.normal)), r));
        float impulse = fminf(-vn, 30) / (1 / mass + fmaxf(0, inverse));
        af_vec3 reaction = af_vec3_scale(c.normal, impulse);
        ph_impulse(b, af_vec3_scale(reaction, -1), c.point);
        incoming = af_vec3_add(incoming, af_vec3_scale(reaction, 1 / mass));
        float target = af_vec3_dot(af_vec3_sub(incoming, surface), c.normal);
        p->v = af_vec3_add(p->v, af_vec3_scale(c.normal, target - projected));
        surface = af_vec3_add(b->v, af_vec3_cross(b->w, r));
        af_vec3 tangent = af_vec3_sub(p->v, surface);
        tangent = af_vec3_sub(tangent, af_vec3_scale(c.normal, af_vec3_dot(tangent, c.normal)));
        float speed = af_vec3_length(tangent);
        if (speed > 1e-7f) {
            af_vec3 n = af_vec3_scale(tangent, 1 / speed);
            float effective =
                1 / mass + b->inv_mass +
                af_vec3_dot(n, af_vec3_cross(ph_inverse_inertia(b, af_vec3_cross(r, n)), r));
            float friction =
                fminf(speed / fmaxf(effective, 1e-7f), impulse * fminf(.3f, b->config.friction * .2f));
            af_vec3 change = af_vec3_scale(n, -friction);
            p->v = af_vec3_add(p->v, af_vec3_scale(change, 1 / mass));
            incoming = af_vec3_add(incoming, af_vec3_scale(change, 1 / mass));
            ph_impulse(b, af_vec3_scale(change, -1), c.point);
        }
    }
    p->v = ph_limit(p->v, 30);
}
void ph_particles_step(af_physics *world, wa_slot *slot, float dt) {
    wa_particles *f = slot->particles;
    if (!f)
        return;
    uint64_t key = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < world->count; ++i) {
        ph_body *b = &world->bodies[i];
        key = (key ^ b->id ^ b->geometry_key) * UINT64_C(1099511628211);
        const unsigned char *data = (const unsigned char *)&b->p;
        for (size_t k = 0; k < sizeof(b->p); ++k)
            key = (key ^ data[k]) * UINT64_C(1099511628211);
        data = (const unsigned char *)b->axes;
        for (size_t k = 0; k < sizeof(b->axes); ++k)
            key = (key ^ data[k]) * UINT64_C(1099511628211);
    }
    if (f->sleeping && key == f->geometry_key && slot->config.emission_rate == 0)
        return;
    f->sleeping = false;
    f->geometry_key = key;
    af_water w = slot->config;
    float spacing = f->spacing, h = spacing * 2, radius = spacing * .42f, volume = f->volume;
    size_t before = f->count;
    f->pending = w.emission_rate > 0 ? fminf(f->pending + w.emission_rate * dt, volume * 64) : 0;
    size_t want = (size_t)floorf(f->pending / volume);
    birth(slot, want, false);
    f->pending -= volume * (float)(f->count - before);
    for (size_t i = 0; i < f->count; ++i) {
        wa_particle *p = &f->values[i];
        p->v.y -= 9.81f * dt;
        p->q = af_vec3_add(p->p, af_vec3_scale(p->v, dt));
        p->normal = (af_vec3){0};
        p->hit_body = SIZE_MAX;
        p->contact_count = 0;
        collide(world, p, radius, true);
    }
    unsigned iterations = 3u + 2u * (unsigned)w.quality;
    for (unsigned iter = 0; iter < iterations; ++iter) {
        grid(f);
        for (size_t i = 0; i < f->count; ++i) {
            wa_particle *p = &f->values[i];
            float rho = volume * kernel(0, h), sum = 0;
            af_vec3 own = {0};
            for (int z = p->cell[2] - 1; z <= p->cell[2] + 1; ++z)
                for (int y = p->cell[1] - 1; y <= p->cell[1] + 1; ++y)
                    for (int x = p->cell[0] - 1; x <= p->cell[0] + 1; ++x)
                        for (uint32_t j = f->heads[hash(x, y, z)]; j != UINT32_MAX;
                             j = f->values[j].next) {
                            wa_particle *q = &f->values[j];
                            if (j == i || !cell(q, x, y, z))
                                continue;
                            af_vec3 d = af_vec3_sub(p->q, q->q);
                            rho += volume * kernel(af_vec3_dot(d, d), h);
                            af_vec3 grad = af_vec3_scale(gradient(d, h, i, j), volume);
                            own = af_vec3_add(own, grad);
                            sum += af_vec3_dot(grad, grad);
                        }
            p->lambda = -fmaxf(0, rho - 1) / (sum + af_vec3_dot(own, own) + .001f / (spacing * spacing));
        }
        for (size_t i = 0; i < f->count; ++i) {
            wa_particle *p = &f->values[i];
            af_vec3 correction = {0};
            for (int z = p->cell[2] - 1; z <= p->cell[2] + 1; ++z)
                for (int y = p->cell[1] - 1; y <= p->cell[1] + 1; ++y)
                    for (int x = p->cell[0] - 1; x <= p->cell[0] + 1; ++x)
                        for (uint32_t j = f->heads[hash(x, y, z)]; j != UINT32_MAX;
                             j = f->values[j].next) {
                            wa_particle *q = &f->values[j];
                            if (j == i || !cell(q, x, y, z))
                                continue;
                            af_vec3 d = af_vec3_sub(p->q, q->q);
                            float ratio = kernel(af_vec3_dot(d, d), h) / kernel(.09f * h * h, h);
                            float scorr = -.0001f * spacing * spacing * ratio * ratio * ratio * ratio;
                            correction = af_vec3_add(
                                correction, af_vec3_scale(gradient(d, h, i, j),
                                                          volume * (p->lambda + q->lambda + scorr)));
                        }
            p->delta = ph_limit(correction, spacing * .15f);
        }
        for (size_t i = 0; i < f->count; ++i) {
            wa_particle *p = &f->values[i];
            p->q = af_vec3_add(p->q, p->delta);
            collide(world, p, radius, false);
        }
    }
    for (size_t i = 0; i < f->count; ++i) {
        wa_particle *p = &f->values[i];
        af_vec3 old = p->v;
        p->v = ph_limit(af_vec3_scale(af_vec3_sub(p->q, p->p), 1 / dt), 30);
        couple(world, p, old, w.density * volume);
        p->p = p->q;
    }
    grid(f);
    float viscosity = -expm1f(-w.viscosity * dt);
    for (size_t i = 0; i < f->count; ++i) {
        wa_particle *p = &f->values[i];
        af_vec3 change = {0};
        float weight = 0;
        for (int z = p->cell[2] - 1; z <= p->cell[2] + 1; ++z)
            for (int y = p->cell[1] - 1; y <= p->cell[1] + 1; ++y)
                for (int x = p->cell[0] - 1; x <= p->cell[0] + 1; ++x)
                    for (uint32_t j = f->heads[hash(x, y, z)]; j != UINT32_MAX; j = f->values[j].next) {
                        wa_particle *q = &f->values[j];
                        if (j == i || !cell(q, x, y, z))
                            continue;
                        af_vec3 d = af_vec3_sub(p->p, q->p);
                        float k = volume * kernel(af_vec3_dot(d, d), h);
                        weight += k;
                        change = af_vec3_add(change, af_vec3_scale(af_vec3_sub(q->v, p->v), k));
                    }
        p->delta = af_vec3_scale(change, viscosity / fmaxf(1, weight));
    }
    size_t write = 0;
    for (size_t i = 0; i < f->count; ++i) {
        wa_particle p = f->values[i];
        p.v = af_vec3_add(p.v, p.delta);
        af_vec3 relative = af_vec3_sub(p.p, slot->transform.position);
        if (!ph_finite(p.p) || fabsf(relative.x) > 50 || fabsf(relative.z) > 50 || relative.y < -30 ||
            relative.y > 50) {
            f->escaped += volume;
            continue;
        }
        f->values[write++] = p;
    }
    f->count = write;
    float speed = 0;
    bool supported = false;
    for (size_t i = 0; i < f->count; ++i) {
        speed = fmaxf(speed, af_vec3_length(f->values[i].v));
        supported |= f->values[i].hit_body < world->count;
    }
    f->quiet_time = w.emission_rate == 0 && supported && speed < .12f ? f->quiet_time + dt : 0;
    if (f->quiet_time > 1) {
        f->sleeping = true;
        for (size_t i = 0; i < f->count; ++i)
            f->values[i].v = (af_vec3){0};
    }
    slot->time += dt;
}

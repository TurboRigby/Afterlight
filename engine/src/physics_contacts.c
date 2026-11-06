/* SPDX-License-Identifier: MPL-2.0 */
#include "collision_internal.h"
#include "physics_internal.h"
#include <stdlib.h>
#include <string.h>
#define SKIN .006f
static af_vec3 add(af_vec3 a, af_vec3 b) {
    return af_vec3_add(a, b);
}
static af_vec3 sub(af_vec3 a, af_vec3 b) {
    return af_vec3_sub(a, b);
}
static af_vec3 mul(af_vec3 a, float b) {
    return af_vec3_scale(a, b);
}
static float dot(af_vec3 a, af_vec3 b) {
    return af_vec3_dot(a, b);
}
static float radius(const ph_body *b, af_vec3 n) {
    float r = 0;
    for (unsigned i = 0; i < 3; ++i)
        r += ph_get(b->shape.half_extents, i) * fabsf(dot(n, b->shape.axes[i]));
    return r;
}
bool ph_emit(af_physics *p, size_t a, size_t b, af_vec3 point, af_vec3 n, float penetration) {
    if (p->contact_count == AF_PHYSICS_MAX_CONTACTS)
        return false;
    ph_body *one = &p->bodies[a], *two = &p->bodies[b];
    ph_contact c = {.a = a,
                    .b = b,
                    .view = {.a = one->id,
                             .b = two->id,
                             .part_a = one->shape.part,
                             .part_b = two->shape.part,
                             .point = point,
                             .normal = n,
                             .penetration = penetration},
                    .local_a = ph_local(one, sub(point, one->p)),
                    .local_b = ph_local(two, sub(point, two->p))};
    p->contacts[p->contact_count++] = c;
    return true;
}
static size_t clip(af_vec3 *polygon, size_t count, af_vec3 center, af_vec3 normal, float extent) {
    af_vec3 result[16];
    size_t used = 0;
    if (!count)
        return 0;
    af_vec3 previous = polygon[count - 1];
    float before = dot(sub(previous, center), normal) - extent;
    for (size_t i = 0; i < count; ++i) {
        af_vec3 current = polygon[i];
        float now = dot(sub(current, center), normal) - extent;
        if ((before <= 0) != (now <= 0)) {
            float fraction = before / (before - now);
            if (used == 16)
                return 0;
            result[used++] = add(previous, mul(sub(current, previous), fraction));
        }
        if (now <= 0) {
            if (used == 16)
                return 0;
            result[used++] = current;
        }
        previous = current;
        before = now;
    }
    memcpy(polygon, result, used * sizeof(*polygon));
    return used;
}
static af_vec3 support(const ph_body *b, af_vec3 n) {
    af_vec3 p = b->shape.center;
    for (unsigned i = 0; i < 3; ++i) {
        float d = dot(b->shape.axes[i], n);
        if (fabsf(d) > 1e-6f)
            p = add(p, mul(b->shape.axes[i], copysignf(ph_get(b->shape.half_extents, i), d)));
    }
    return p;
}
static void closest_segments(af_vec3 p, af_vec3 q, af_vec3 a, af_vec3 b, af_vec3 *one, af_vec3 *two) {
    af_vec3 u = sub(q, p), v = sub(b, a), r = sub(p, a);
    float aa = dot(u, u), bb = dot(v, v), uv = dot(u, v), ur = dot(u, r), vr = dot(v, r), s = 0, t = 0,
          den = aa * bb - uv * uv;
    if (aa < 1e-12f)
        t = bb > 1e-12f ? fminf(1, fmaxf(0, vr / bb)) : 0;
    else if (bb < 1e-12f)
        s = fminf(1, fmaxf(0, -ur / aa));
    else {
        s = den > 1e-12f ? fminf(1, fmaxf(0, (uv * vr - ur * bb) / den)) : 0;
        t = (uv * s + vr) / bb;
        if (t < 0) {
            t = 0;
            s = fminf(1, fmaxf(0, -ur / aa));
        } else if (t > 1) {
            t = 1;
            s = fminf(1, fmaxf(0, (uv - ur) / aa));
        }
    }
    *one = add(p, mul(u, s));
    *two = add(a, mul(v, t));
}
static bool boxes(af_physics *p, size_t ai, size_t bi) {
    ph_body *a = &p->bodies[ai], *b = &p->bodies[bi];
    af_vec3 delta = sub(b->shape.center, a->shape.center), normal = {0, 1, 0};
    float best = 1e30f;
    unsigned feature = 0;
    for (unsigned i = 0; i < 15; ++i) {
        af_vec3 axis = i < 3   ? a->shape.axes[i]
                       : i < 6 ? b->shape.axes[i - 3]
                               : af_vec3_cross(a->shape.axes[(i - 6) / 3], b->shape.axes[(i - 6) % 3]);
        float length = af_vec3_length(axis);
        if (length < 1e-5f)
            continue;
        axis = mul(axis, 1 / length);
        float d = dot(delta, axis), overlap = radius(a, axis) + radius(b, axis) - fabsf(d);
        if (overlap < -SKIN)
            return true;
        if (overlap < best - 1e-5f) {
            best = overlap;
            normal = mul(axis, d < 0 ? -1.f : 1.f);
            feature = i;
        }
    }
    if (feature < 6) {
        ph_body *ref = feature < 3 ? a : b, *incident = feature < 3 ? b : a;
        unsigned face = feature % 3;
        af_vec3 n = feature < 3 ? normal : mul(normal, -1),
                center = add(ref->shape.center, mul(n, ph_get(ref->shape.half_extents, face)));
        unsigned incident_face = 0;
        for (unsigned i = 1; i < 3; ++i)
            if (fabsf(dot(incident->shape.axes[i], n)) >
                fabsf(dot(incident->shape.axes[incident_face], n)))
                incident_face = i;
        af_vec3 ic = add(incident->shape.center,
                         mul(incident->shape.axes[incident_face],
                             -copysignf(ph_get(incident->shape.half_extents, incident_face),
                                        dot(incident->shape.axes[incident_face], n)))),
                u = mul(incident->shape.axes[(incident_face + 1) % 3],
                        ph_get(incident->shape.half_extents, (incident_face + 1) % 3)),
                v = mul(incident->shape.axes[(incident_face + 2) % 3],
                        ph_get(incident->shape.half_extents, (incident_face + 2) % 3));
        af_vec3 poly[16] = {sub(sub(ic, u), v), add(sub(ic, v), u), add(add(ic, u), v),
                            add(sub(ic, u), v)};
        size_t count = 4;
        for (unsigned axis = 0; axis < 3; ++axis)
            if (axis != face) {
                count = clip(poly, count, center, ref->shape.axes[axis],
                             ph_get(ref->shape.half_extents, axis));
                count = clip(poly, count, center, mul(ref->shape.axes[axis], -1),
                             ph_get(ref->shape.half_extents, axis));
            }
        /* At most four well-spread points per face manifold. */
        af_vec3 chosen[4];
        float depths[4];
        size_t used = 0;
        for (size_t i = 0; i < count; ++i) {
            float separation = dot(sub(poly[i], center), n);
            if (separation > SKIN)
                continue;
            af_vec3 point = sub(poly[i], mul(n, separation * .5f));
            if (used < 4) {
                chosen[used] = point;
                depths[used++] = -separation;
            } else {
                size_t replace = 0;
                float nearest = 1e30f;
                for (size_t j = 0; j < 4; ++j) {
                    float distance = dot(sub(chosen[j], point), sub(chosen[j], point));
                    if (distance < nearest) {
                        nearest = distance;
                        replace = j;
                    }
                }
                if (nearest > .0001f) {
                    chosen[replace] = point;
                    depths[replace] = -separation;
                }
            }
        }
        for (size_t i = 0; i < used; ++i)
            if (!ph_emit(p, ai, bi, chosen[i], normal, depths[i]))
                return false;
        if (used)
            return true;
    }
    af_vec3 one = support(a, normal), two = support(b, mul(normal, -1));
    if (feature >= 6) {
        unsigned ia = (feature - 6) / 3, ib = (feature - 6) % 3;
        af_vec3 ea = mul(a->shape.axes[ia], ph_get(a->shape.half_extents, ia)),
                eb = mul(b->shape.axes[ib], ph_get(b->shape.half_extents, ib));
        one = sub(one, mul(a->shape.axes[ia], dot(sub(one, a->shape.center), a->shape.axes[ia])));
        two = sub(two, mul(b->shape.axes[ib], dot(sub(two, b->shape.center), b->shape.axes[ib])));
        closest_segments(sub(one, ea), add(one, ea), sub(two, eb), add(two, eb), &one, &two);
    }
    return ph_emit(p, ai, bi, mul(add(one, two), .5f), normal, best);
}
static bool sphere_box(af_physics *p, size_t sphere, size_t box, bool reverse) {
    ph_body *s = &p->bodies[sphere], *b = &p->bodies[box];
    af_vec3 local = ph_shape_local(b, sub(s->shape.center, b->shape.center)), h = b->shape.half_extents,
            closest = {fminf(h.x, fmaxf(-h.x, local.x)), fminf(h.y, fmaxf(-h.y, local.y)),
                       fminf(h.z, fmaxf(-h.z, local.z))};
    af_vec3 difference = sub(local, closest);
    float distance = af_vec3_length(difference), penetration = s->shape.radius - distance;
    af_vec3 outward;
    if (distance > 1e-7f)
        outward = ph_shape_vector(b, mul(difference, 1 / distance));
    else {
        unsigned axis = 0;
        float depth = h.x - fabsf(local.x);
        for (unsigned i = 1; i < 3; ++i) {
            float d = ph_get(h, i) - fabsf(ph_get(local, i));
            if (d < depth) {
                depth = d;
                axis = i;
            }
        }
        outward = mul(b->shape.axes[axis], ph_get(local, axis) < 0 ? -1.f : 1.f);
        penetration = s->shape.radius + depth;
        closest = add(local, ph_shape_local(b, mul(outward, depth)));
    }
    if (penetration < -SKIN)
        return true;
    af_vec3 point = mul(add(add(b->shape.center, ph_shape_vector(b, closest)),
                            sub(s->shape.center, mul(outward, s->shape.radius))),
                        .5f);
    return ph_emit(p, reverse ? box : sphere, reverse ? sphere : box, point,
                   reverse ? outward : mul(outward, -1), penetration);
}
static bool shapes(af_physics *p, size_t ai, size_t bi) {
    ph_body *a = &p->bodies[ai], *b = &p->bodies[bi];
    bool ok;
    if (a->character_proxy || b->character_proxy) {
        ph_body *player = a->character_proxy ? a : b, *other = player == a ? b : a;
        af_collision_hit hit;
        float separation =
            af_collision_capsule_distance(other->shape, player->p, player->shape.radius,
                                          player->shape.half_extents.y - player->shape.radius, &hit);
        ok = separation > SKIN ||
             ph_emit(p, ai, bi, hit.point, player == a ? mul(hit.normal, -1) : hit.normal, -separation);
    } else if (a->shape.type == AF_COLLIDER_MESH || b->shape.type == AF_COLLIDER_MESH)
        ok = ph_mesh_contacts(p, ai, bi);
    else if (a->shape.type == AF_COLLIDER_BOX && b->shape.type == AF_COLLIDER_BOX)
        ok = boxes(p, ai, bi);
    else if (a->shape.type != b->shape.type)
        ok = a->shape.type == AF_COLLIDER_SPHERE ? sphere_box(p, ai, bi, false)
                                                 : sphere_box(p, bi, ai, true);
    else {
        af_vec3 d = sub(b->shape.center, a->shape.center);
        float length = af_vec3_length(d), penetration = a->shape.radius + b->shape.radius - length;
        if (penetration < -SKIN)
            return true;
        af_vec3 n = length > 1e-7f ? mul(d, 1 / length) : (af_vec3){0, 1, 0};
        ok = ph_emit(p, ai, bi, add(a->shape.center, mul(n, a->shape.radius - penetration * .5f)), n,
                     penetration);
    }
    return ok;
}
bool ph_detect(af_physics *p) {
    p->cache_count = p->contact_count;
    memcpy(p->cache, p->contacts, p->cache_count * sizeof(ph_contact));
    p->contact_count = 0;
    double start = ph_clock(p), narrow = 0;
    ph_bounds(p, p->bounds, 0, false);
    for (size_t i = 0; i < p->count; ++i)
        for (size_t j = i + 1; j < p->count && p->bounds[j].key_low <= p->bounds[i].key_high; ++j) {
            ph_bound one = p->bounds[i], two = p->bounds[j];
            size_t ai = one.body, bi = two.body;
            if (ai > bi) {
                size_t swap = ai;
                ai = bi;
                bi = swap;
            }
            ph_body *a = &p->bodies[ai], *b = &p->bodies[bi];
            if ((a->inv_mass == 0 && b->inv_mass == 0) || !(a->shape.layer & b->shape.layer) ||
                !ph_collision_allowed(p, a->id, b->id) || !ph_bounds_overlap(one, two))
                continue;
            ++p->stats.broadphase_pairs;
            double stamp = ph_clock(p);
            af_world_collider original_a = a->shape, original_b = b->shape;
            bool ok = true;
            size_t na = a->part_count ? a->part_count : 1, nb = b->part_count ? b->part_count : 1;
            for (size_t pa = 0; pa < na && ok; ++pa)
                for (size_t pb = 0; pb < nb && ok; ++pb) {
                    a->shape = ph_part_world(a, pa);
                    b->shape = ph_part_world(b, pb);
                    ok = shapes(p, ai, bi);
                }
            a->shape = original_a;
            b->shape = original_b;
            narrow += ph_clock(p) - stamp;
            if (!ok)
                return false;
        }
    p->stats.narrowphase_ms += narrow;
    p->stats.broadphase_ms += fmax(0, ph_clock(p) - start - narrow);
    p->stats.contacts = p->contact_count;
    return true;
}

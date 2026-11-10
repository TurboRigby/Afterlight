/* SPDX-License-Identifier: MPL-2.0 */
#include "physics_internal.h"
#include "triangle_collision.h"
#include <stdlib.h>
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
static size_t clip(af_vec3 *poly, size_t count, unsigned axis, float side, float extent) {
    if (!count)
        return 0;
    af_vec3 out[16], previous = poly[count - 1];
    float before = side * ph_get(previous, axis) - extent;
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        af_vec3 now = poly[i];
        float d = side * ph_get(now, axis) - extent;
        if ((d <= 0) != (before <= 0) && used < 16)
            out[used++] = add(previous, mul(sub(now, previous), before / (before - d)));
        if (d <= 0 && used < 16)
            out[used++] = now;
        previous = now;
        before = d;
    }
    for (size_t i = 0; i < used; ++i)
        poly[i] = out[i];
    return used;
}
static bool emit(af_physics *p, size_t a, size_t b, af_vec3 point, af_vec3 normal, float depth, size_t first) {
    size_t same[4], count = 0;
    for (size_t i = first; i < p->contact_count; ++i) {
        ph_contact *c = &p->contacts[i];
        if (dot(c->view.normal, normal) < .98f)
            continue;
        af_vec3 delta = sub(c->view.point, point);
        if (dot(delta, delta) < 1e-6f) {
            if (depth > c->view.penetration)
                c->view.penetration = depth;
            return true;
        }
        if (count < 4)
            same[count] = i;
        ++count;
    }
    if (count >= 4) { /* Retain a wide contact footprint within each normal patch. */
        size_t nearest = same[0];
        float old_distance = 1e30f;
        for (size_t k = 0; k < 4; ++k) {
            float d = dot(sub(p->contacts[same[k]].view.point, point), sub(p->contacts[same[k]].view.point, point));
            if (d < old_distance) {
                old_distance = d;
                nearest = same[k];
            }
        }
        float old_spread = 0, new_spread = 0;
        for (size_t k = 0; k < 4; ++k)
            if (same[k] != nearest) {
                af_vec3 other = p->contacts[same[k]].view.point;
                af_vec3 delta = sub(other, p->contacts[nearest].view.point);
                old_spread += dot(delta, delta);
                delta = sub(other, point);
                new_spread += dot(delta, delta);
            }
        if (new_spread <= old_spread + .00001f && depth <= p->contacts[nearest].view.penetration + .01f)
            return true;
        ph_body *one = &p->bodies[a], *two = &p->bodies[b];
        p->contacts[nearest] = (ph_contact){.a = a,
                                            .b = b,
                                            .view = {.a = one->id,
                                                     .b = two->id,
                                                     .part_a = one->shape.part,
                                                     .part_b = two->shape.part,
                                                     .point = point,
                                                     .normal = normal,
                                                     .penetration = depth},
                                            .local_a = ph_local(one, sub(point, one->p)),
                                            .local_b = ph_local(two, sub(point, two->p))};
        return true;
    }
    return ph_emit(p, a, b, point, normal, depth);
}
static bool box_triangle(af_physics *p, size_t a, size_t b, size_t box, af_triangle t, size_t first) {
    ph_body *body = &p->bodies[box];
    af_vec3 v[3] = {sub(t.a, body->shape.center), sub(t.b, body->shape.center), sub(t.c, body->shape.center)},
            edges[3] = {sub(t.b, t.a), sub(t.c, t.b), sub(t.a, t.c)}, face = af_vec3_cross(edges[0], edges[1]),
            normal = {0};
    float best = 1e30f;
    for (unsigned i = 0; i < 13; ++i) {
        af_vec3 axis = i < 3    ? body->shape.axes[i]
                       : i == 3 ? face
                                : af_vec3_cross(body->shape.axes[(i - 4) / 3], edges[(i - 4) % 3]);
        float length = af_vec3_length(axis);
        if (length < 1e-7f)
            continue;
        axis = mul(axis, 1 / length);
        float low = dot(v[0], axis), high = low;
        for (unsigned k = 1; k < 3; ++k) {
            float d = dot(v[k], axis);
            low = fminf(low, d);
            high = fmaxf(high, d);
        }
        float r = radius(body, axis);
        if (low > r + .006f || high < -r - .006f)
            return true;
        float positive = r - low, negative = high + r, depth = fminf(positive, negative);
        if (depth < best) {
            best = depth;
            normal = mul(axis, positive < negative ? 1.f : -1.f);
        }
    }
    af_vec3 local[16] = {ph_shape_local(body, v[0]), ph_shape_local(body, v[1]), ph_shape_local(body, v[2])};
    size_t count = 3;
    for (unsigned axis = 0; axis < 3; ++axis) {
        count = clip(local, count, axis, 1, ph_get(body->shape.half_extents, axis) + .006f);
        count = clip(local, count, axis, -1, ph_get(body->shape.half_extents, axis) + .006f);
    }
    af_vec3 n = box == a ? normal : mul(normal, -1);
    for (size_t i = 0; i < count; ++i)
        if (!emit(p, a, b, add(body->shape.center, ph_shape_vector(body, local[i])), n, best, first))
            return false;
    if (!count) {
        af_vec3 point = tc_closest_triangle(t, body->shape.center);
        return emit(p, a, b, point, n, best, first);
    }
    return true;
}
bool ph_mesh_contacts(af_physics *p, size_t a, size_t b) {
    size_t mesh = p->bodies[a].shape.type == AF_COLLIDER_MESH ? a : b, other = mesh == a ? b : a;
    ph_body *body = &p->bodies[other];
    if (body->inv_mass == 0)
        return true;
    af_world_collider target = p->bodies[mesh].shape;
    af_vec3 extent = {body->shape.radius, body->shape.radius, body->shape.radius};
    if (body->shape.type == AF_COLLIDER_BOX) {
        extent = (af_vec3){0};
        for (unsigned i = 0; i < 3; ++i) {
            af_vec3 axis = body->shape.axes[i];
            float h = ph_get(body->shape.half_extents, i);
            extent = add(extent, (af_vec3){fabsf(axis.x) * h, fabsf(axis.y) * h, fabsf(axis.z) * h});
        }
    }
    extent = add(extent, (af_vec3){.006f, .006f, .006f});
    uint32_t scratch[128], *indices;
    size_t count;
    if (!tc_candidates(target, sub(body->shape.center, extent), add(body->shape.center, extent), &indices, &count,
                       scratch, 128))
        return false;
    size_t first = p->contact_count;
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        af_triangle t = tc_world_triangle(target, indices[i]);
        if (body->shape.type == AF_COLLIDER_SPHERE) {
            af_collision_hit hit;
            float distance = tc_capsule_triangle(t, body->shape.center, body->shape.radius, 0, &hit);
            if (distance > .006f)
                continue;
            af_vec3 n = other == a ? mul(hit.normal, -1) : hit.normal;
            ok = emit(p, a, b, hit.point, n, -distance, first);
        } else
            ok = box_triangle(p, a, b, other, t, first);
    }
    if (indices != scratch)
        free(indices);
    return ok;
}

/* SPDX-License-Identifier: MPL-2.0 */
#include "triangle_collision.h"
#include <float.h>
#include <math.h>
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
static float clamp(float v, float lo, float hi) {
    return fminf(hi, fmaxf(lo, v));
}
static af_vec3 point(af_mat4 m, af_vec3 p) {
    af_vec4 v = af_mat4_transform(m, (af_vec4){p.x, p.y, p.z, 1});
    return (af_vec3){v.x, v.y, v.z};
}
static af_vec3 minimum(af_vec3 a, af_vec3 b) {
    return (af_vec3){fminf(a.x, b.x), fminf(a.y, b.y), fminf(a.z, b.z)};
}
static af_vec3 maximum(af_vec3 a, af_vec3 b) {
    return (af_vec3){fmaxf(a.x, b.x), fmaxf(a.y, b.y), fmaxf(a.z, b.z)};
}
af_vec3 tc_closest_triangle(af_triangle t, af_vec3 p) {
    af_vec3 ab = sub(t.b, t.a), ac = sub(t.c, t.a), ap = sub(p, t.a);
    float d1 = dot(ab, ap), d2 = dot(ac, ap);
    if (d1 <= 0 && d2 <= 0)
        return t.a;
    af_vec3 bp = sub(p, t.b);
    float d3 = dot(ab, bp), d4 = dot(ac, bp);
    if (d3 >= 0 && d4 <= d3)
        return t.b;
    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0 && d1 >= 0 && d3 <= 0)
        return add(t.a, mul(ab, d1 / (d1 - d3)));
    af_vec3 cp = sub(p, t.c);
    float d5 = dot(ab, cp), d6 = dot(ac, cp);
    if (d6 >= 0 && d5 <= d6)
        return t.c;
    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0 && d2 >= 0 && d6 <= 0)
        return add(t.a, mul(ac, d2 / (d2 - d6)));
    float va = d3 * d6 - d5 * d4;
    if (va <= 0 && d4 - d3 >= 0 && d5 - d6 >= 0)
        return add(t.b, mul(sub(t.c, t.b), (d4 - d3) / ((d4 - d3) + (d5 - d6))));
    float inverse = 1 / (va + vb + vc);
    return add(t.a, add(mul(ab, vb * inverse), mul(ac, vc * inverse)));
}
static void segments(af_vec3 p, af_vec3 q, af_vec3 a, af_vec3 b, af_vec3 *one, af_vec3 *two) {
    af_vec3 u = sub(q, p), v = sub(b, a), r = sub(p, a);
    float aa = dot(u, u), bb = dot(v, v), uv = dot(u, v), ur = dot(u, r), vr = dot(v, r), s = 0, t = 0,
          den = aa * bb - uv * uv;
    if (aa < 1e-12f)
        t = bb > 1e-12f ? clamp(vr / bb, 0, 1) : 0;
    else if (bb < 1e-12f)
        s = clamp(-ur / aa, 0, 1);
    else {
        s = den > 1e-12f ? clamp((uv * vr - ur * bb) / den, 0, 1) : 0;
        t = (uv * s + vr) / bb;
        if (t < 0) {
            t = 0;
            s = clamp(-ur / aa, 0, 1);
        } else if (t > 1) {
            t = 1;
            s = clamp((uv - ur) / aa, 0, 1);
        }
    }
    *one = add(p, mul(u, s));
    *two = add(a, mul(v, t));
}
float tc_capsule_triangle(af_triangle t, af_vec3 center, float radius, float half,
                          af_collision_hit *out) {
    af_vec3 a = add(center, (af_vec3){0, -half, 0}), b = add(center, (af_vec3){0, half, 0}), one = a,
            two = tc_closest_triangle(t, a);
    float best = dot(sub(one, two), sub(one, two));
    af_vec3 v = tc_closest_triangle(t, b);
    float distance = dot(sub(b, v), sub(b, v));
    if (distance < best) {
        best = distance;
        one = b;
        two = v;
    }
    af_vec3 edges[3] = {t.a, t.b, t.c};
    for (unsigned i = 0; i < 3; ++i) {
        af_vec3 x, y;
        segments(a, b, edges[i], edges[(i + 1) % 3], &x, &y);
        float d = dot(sub(x, y), sub(x, y));
        if (d < best) {
            best = d;
            one = x;
            two = y;
        }
    }
    af_vec3 normal = af_vec3_normalize(af_vec3_cross(sub(t.b, t.a), sub(t.c, t.a)));
    float da = dot(sub(a, t.a), normal), db = dot(sub(b, t.a), normal);
    if (da * db <= 0 && fabsf(da - db) > 1e-8f) {
        af_vec3 cross = add(a, mul(sub(b, a), da / (da - db))), closest = tc_closest_triangle(t, cross);
        if (af_vec3_length(sub(cross, closest)) < 1e-5f) {
            one = two = cross;
            best = 0;
        }
    }
    distance = sqrtf(fmaxf(0, best));
    float separation = distance - radius;
    if (distance > 1e-6f)
        normal = mul(sub(one, two), 1 / distance);
    else {
        if (dot(sub(center, t.a), normal) < 0)
            normal = mul(normal, -1);
        separation = fminf(dot(sub(a, t.a), normal), dot(sub(b, t.a), normal)) - radius;
    }
    *out = (af_collision_hit){.point = two, .normal = normal, .penetration = fmaxf(0, -separation)};
    return separation;
}
bool tc_candidates(af_world_collider target, af_vec3 low, af_vec3 high, uint32_t **list, size_t *count,
                   uint32_t *scratch, size_t capacity) {
    af_vec3 lo = point(target.inverse, low), hi = lo;
    for (unsigned i = 0; i < 8; ++i) {
        af_vec3 p = point(target.inverse, (af_vec3){i & 1 ? high.x : low.x, i & 2 ? high.y : low.y,
                                                    i & 4 ? high.z : low.z});
        lo = minimum(lo, p);
        hi = maximum(hi, p);
    }
    lo = (af_vec3){fmaxf(-1000000, lo.x), fmaxf(-1000000, lo.y), fmaxf(-1000000, lo.z)};
    hi = (af_vec3){fminf(1000000, hi.x), fminf(1000000, hi.y), fminf(1000000, hi.z)};
    *count = af_collision_mesh_query(target.mesh, lo, hi, scratch, capacity);
    *list = scratch;
    if (*count > capacity) {
        if (*count > AF_COLLISION_MESH_MAX_TRIANGLES)
            return false;
        *list = malloc(*count * sizeof(uint32_t));
        if (!*list)
            return false;
        af_collision_mesh_query(target.mesh, lo, hi, *list, *count);
    }
    return true;
}
af_triangle tc_world_triangle(af_world_collider target, uint32_t index) {
    af_triangle t = {0};
    af_collision_mesh_triangle(target.mesh, index, &t);
    return (af_triangle){point(target.matrix, t.a), point(target.matrix, t.b),
                         point(target.matrix, t.c)};
}
float tc_mesh_distance(af_world_collider target, af_vec3 center, float radius, float half,
                       af_collision_hit *out) {
    uint32_t scratch[128], *list;
    size_t count;
    af_vec3 ext = {radius + .00001f, radius + half + .00001f, radius + .00001f};
    if (!tc_candidates(target, sub(center, ext), add(center, ext), &list, &count, scratch, 128))
        return FLT_MAX;
    float best = FLT_MAX;
    for (size_t i = 0; i < count; ++i) {
        af_collision_hit hit;
        float d = tc_capsule_triangle(tc_world_triangle(target, list[i]), center, radius, half, &hit);
        if (d < best) {
            best = d;
            *out = hit;
            out->object = target.object;
            out->trigger = target.trigger;
        }
    }
    if (list != scratch)
        free(list);
    return best;
}
bool tc_mesh_sweep(af_world_collider target, af_vec3 center, float radius, float half, af_vec3 move,
                   float skin, af_collision_hit *out) {
    uint32_t scratch[128], *list;
    size_t count;
    af_vec3 ext = {radius + skin, half + radius + skin, radius + skin}, end = add(center, move);
    if (!tc_candidates(target, sub(minimum(center, end), ext), add(maximum(center, end), ext), &list,
                       &count, scratch, 128))
        return false;
    float closest = 1.00001f;
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        af_triangle triangle = tc_world_triangle(target, list[i]);
        float t = 0;
        for (unsigned step = 0; step < 64 && t <= 1 && t < closest; ++step) {
            af_collision_hit hit;
            float distance =
                      tc_capsule_triangle(triangle, add(center, mul(move, t)), radius, half, &hit),
                  approach = -dot(move, hit.normal);
            if (distance <= skin + .00001f) {
                if (t == 0 && approach <= 1e-7f)
                    break;
                hit.object = target.object;
                hit.trigger = target.trigger;
                hit.fraction = t;
                hit.distance = t * af_vec3_length(move);
                *out = hit;
                closest = t;
                found = true;
                break;
            }
            if (approach <= 1e-8f)
                break;
            t += fmaxf((distance - skin) / approach, .000001f);
        }
    }
    if (list != scratch)
        free(list);
    return found;
}
bool tc_mesh_ray(af_world_collider target, af_vec3 origin, af_vec3 direction, float maximum_distance,
                 af_collision_hit *out) {
    uint32_t scratch[128], *list;
    size_t count;
    af_vec3 end = add(origin, mul(direction, maximum_distance)), padding = {.00001f, .00001f, .00001f};
    if (!tc_candidates(target, sub(minimum(origin, end), padding), add(maximum(origin, end), padding),
                       &list, &count, scratch, 128))
        return false;
    bool found = false;
    for (size_t i = 0; i < count; ++i) {
        af_triangle triangle = tc_world_triangle(target, list[i]);
        af_vec3 e1 = sub(triangle.b, triangle.a), e2 = sub(triangle.c, triangle.a),
                p = af_vec3_cross(direction, e2);
        float det = dot(e1, p);
        if (fabsf(det) < 1e-9f)
            continue;
        float inv = 1 / det;
        af_vec3 t = sub(origin, triangle.a);
        float u = dot(t, p) * inv;
        if (u < -.00001f || u > 1.00001f)
            continue;
        af_vec3 q = af_vec3_cross(t, e1);
        float v = dot(direction, q) * inv;
        if (v < -.00001f || u + v > 1.00001f)
            continue;
        float distance = dot(e2, q) * inv;
        if (distance < 0 || distance > maximum_distance)
            continue;
        maximum_distance = distance;
        af_vec3 n = af_vec3_normalize(af_vec3_cross(e1, e2));
        if (dot(n, direction) > 0)
            n = mul(n, -1);
        *out = (af_collision_hit){.object = target.object,
                                  .point = add(origin, mul(direction, distance)),
                                  .normal = n,
                                  .distance = distance,
                                  .trigger = target.trigger};
        found = true;
    }
    if (list != scratch)
        free(list);
    return found;
}

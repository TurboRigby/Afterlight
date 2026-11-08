/* SPDX-License-Identifier: MPL-2.0 */
#include "water_particles_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static af_vec3 position(const wa_particle *p, float alpha) {
    return af_vec3_add(p->previous, af_vec3_scale(af_vec3_sub(p->p, p->previous), alpha));
}
static af_vec3 local(af_transform t, af_vec3 p) {
    p = af_vec3_sub(p, t.position);
    float c = cosf(t.rotation.y), s = sinf(t.rotation.y);
    return (af_vec3){c * p.x - s * p.z, p.y, s * p.x + c * p.z};
}
static af_vec3 normal_local(af_transform t, af_vec3 p) {
    t.position = (af_vec3){0};
    return local(t, p);
}
static bool drops(const wa_slot *a, float alpha, af_vertex **vertices, uint32_t **indices, af_mesh_data *mesh) {
    const wa_particles *f = a->particles;
    size_t count = f ? f->count : 0;
    if (!count)
        count = 1;
    af_vertex *v = calloc(count * 24, sizeof(*v));
    uint32_t *ix = malloc(count * 24 * sizeof(*ix));
    if (!v || !ix) {
        free(v);
        free(ix);
        return false;
    }
    static const af_vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    static const unsigned triangles[] = {2, 4, 0, 2, 1, 4, 2, 5, 1, 2, 0, 5, 3, 0, 4, 3, 4, 1, 3, 1, 5, 3, 5, 0};
    for (size_t i = 0; i < count; ++i) {
        af_vec3 p = f && i < f->count ? local(a->transform, position(&f->values[i], alpha)) : (af_vec3){0};
        float radius = f && f->count ? f->spacing * .62f : 0;
        for (unsigned j = 0; j < 24; ++j) {
            af_vec3 n = directions[triangles[j]];
            size_t k = i * 24 + j;
            v[k] =
                (af_vertex){.position = af_vec3_add(p, af_vec3_scale(n, radius)), .normal = n, .color = {1, 1, 1, 1}};
            ix[k] = (uint32_t)k;
        }
    }
    *vertices = v;
    *indices = ix;
    *mesh = (af_mesh_data){v, (uint32_t)(count * 24), ix, (uint32_t)(count * 24)};
    return true;
}
typedef struct node {
    float value;
    af_vec3 gradient;
} node;
typedef struct builder {
    af_vertex *v;
    uint32_t *ix;
    uint32_t count, cap;
    af_transform transform;
} builder;
static void triangle(builder *b, af_vec3 *p, af_vec3 *n) {
    if (b->count + 3 > b->cap)
        return;
    af_vec3 face = af_vec3_cross(af_vec3_sub(p[1], p[0]), af_vec3_sub(p[2], p[0]));
    if (af_vec3_length(face) < 1e-10f)
        return;
    af_vec3 sum = af_vec3_add(n[0], af_vec3_add(n[1], n[2]));
    unsigned order[] = {0, 1, 2};
    if (af_vec3_dot(face, sum) < 0) {
        order[1] = 2;
        order[2] = 1;
    }
    for (unsigned i = 0; i < 3; ++i) {
        unsigned k = order[i];
        b->v[b->count] = (af_vertex){
            .position = local(b->transform, p[k]), .normal = normal_local(b->transform, n[k]), .color = {1, 1, 1, 1}};
        b->ix[b->count] = b->count;
        ++b->count;
    }
}
static void tetra(builder *b, const af_vec3 *p, const node *v) {
    unsigned in[4], out[4], ni = 0, no = 0;
    for (unsigned k = 0; k < 4; ++k)
        if (v[k].value >= .5f)
            in[ni++] = k;
        else
            out[no++] = k;
    if (!ni || !no)
        return;
    af_vec3 points[4], normals[4];
    unsigned pairs[4][2], edges = 0;
    if (ni == 1 || no == 1) {
        unsigned one = ni == 1 ? in[0] : out[0];
        unsigned *other = ni == 1 ? out : in;
        for (unsigned i = 0; i < 3; ++i) {
            pairs[edges][0] = one;
            pairs[edges++][1] = other[i];
        }
    } else {
        pairs[0][0] = in[0];
        pairs[0][1] = out[0];
        pairs[1][0] = in[0];
        pairs[1][1] = out[1];
        pairs[2][0] = in[1];
        pairs[2][1] = out[0];
        pairs[3][0] = in[1];
        pairs[3][1] = out[1];
        edges = 4;
    }
    for (unsigned k = 0; k < edges; ++k) {
        unsigned x = pairs[k][0], y = pairs[k][1];
        float t = (.5f - v[x].value) / (v[y].value - v[x].value);
        points[k] = af_vec3_add(p[x], af_vec3_scale(af_vec3_sub(p[y], p[x]), t));
        af_vec3 g = af_vec3_add(v[x].gradient, af_vec3_scale(af_vec3_sub(v[y].gradient, v[x].gradient), t));
        normals[k] = af_vec3_normalize(af_vec3_scale(g, -1));
    }
    triangle(b, points, normals);
    if (edges == 4) {
        af_vec3 q[] = {points[1], points[3], points[2]}, n[] = {normals[1], normals[3], normals[2]};
        triangle(b, q, n);
    }
}
bool wa_particles_mesh(const wa_slot *a, float alpha, af_vertex **vertices, uint32_t **indices, af_mesh_data *mesh) {
    const wa_particles *f = a->particles;
    if (!f || f->count < 2 || a->config.quality == AF_WATER_LOW)
        return drops(a, alpha, vertices, indices, mesh);
    float radius = 1.2f * f->spacing;
    af_vec3 lo = position(&f->values[0], alpha), hi = lo;
    for (size_t i = 1; i < f->count; ++i) {
        af_vec3 p = position(&f->values[i], alpha);
        lo = (af_vec3){fminf(lo.x, p.x), fminf(lo.y, p.y), fminf(lo.z, p.z)};
        hi = (af_vec3){fmaxf(hi.x, p.x), fmaxf(hi.y, p.y), fmaxf(hi.z, p.z)};
    }
    lo = af_vec3_sub(lo, (af_vec3){radius, radius, radius});
    hi = af_vec3_add(hi, (af_vec3){radius, radius, radius});
    af_vec3 extent = af_vec3_sub(hi, lo);
    unsigned limit = a->config.quality == AF_WATER_HIGH ? 64u : 48u;
    float step = fmaxf(f->spacing * (a->config.quality == AF_WATER_HIGH ? .5f : .7f),
                       fmaxf(extent.x, fmaxf(extent.y, extent.z)) / (float)(limit - 1));
    unsigned nx = (unsigned)ceilf(extent.x / step) + 1, ny = (unsigned)ceilf(extent.y / step) + 1,
             nz = (unsigned)ceilf(extent.z / step) + 1;
    if (nx > limit + 1 || ny > limit + 1 || nz > limit + 1)
        return drops(a, alpha, vertices, indices, mesh);
    node *field = calloc((size_t)nx * ny * nz, sizeof(*field));
    if (!field)
        return false;
    for (size_t i = 0; i < f->count; ++i) {
        af_vec3 p = position(&f->values[i], alpha);
        int x0 = (int)fmaxf(0, floorf((p.x - radius - lo.x) / step)),
            x1 = (int)fminf((float)(nx - 1), ceilf((p.x + radius - lo.x) / step));
        int y0 = (int)fmaxf(0, floorf((p.y - radius - lo.y) / step)),
            y1 = (int)fminf((float)(ny - 1), ceilf((p.y + radius - lo.y) / step));
        int z0 = (int)fmaxf(0, floorf((p.z - radius - lo.z) / step)),
            z1 = (int)fminf((float)(nz - 1), ceilf((p.z + radius - lo.z) / step));
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    af_vec3 d = af_vec3_sub(
                        (af_vec3){lo.x + (float)x * step, lo.y + (float)y * step, lo.z + (float)z * step}, p);
                    float q = 1 - af_vec3_dot(d, d) / (radius * radius);
                    if (q <= 0)
                        continue;
                    node *n = &field[((size_t)z * ny + (size_t)y) * nx + (size_t)x];
                    n->value += q * q * q;
                    n->gradient = af_vec3_add(n->gradient, af_vec3_scale(d, -6 * q * q / (radius * radius)));
                }
    }
    builder b = {.cap = 180000, .transform = a->transform};
    b.v = calloc(b.cap, sizeof(*b.v));
    b.ix = malloc(b.cap * sizeof(*b.ix));
    if (!b.v || !b.ix) {
        free(field);
        free(b.v);
        free(b.ix);
        return false;
    }
    static const unsigned tet[6][4] = {{0, 1, 3, 7}, {0, 3, 2, 7}, {0, 2, 6, 7},
                                       {0, 6, 4, 7}, {0, 4, 5, 7}, {0, 5, 1, 7}};
    for (unsigned z = 0; z + 1 < nz; ++z)
        for (unsigned y = 0; y + 1 < ny; ++y)
            for (unsigned x = 0; x + 1 < nx; ++x) {
                af_vec3 p[8];
                node v[8];
                float low = 100000, high = 0;
                for (unsigned k = 0; k < 8; ++k) {
                    unsigned xx = x + (k & 1u), yy = y + ((k >> 1u) & 1u), zz = z + ((k >> 2u) & 1u);
                    p[k] = (af_vec3){lo.x + (float)xx * step, lo.y + (float)yy * step, lo.z + (float)zz * step};
                    v[k] = field[((size_t)zz * ny + yy) * nx + xx];
                    low = fminf(low, v[k].value);
                    high = fmaxf(high, v[k].value);
                }
                if (low > .5f || high < .5f)
                    continue;
                for (unsigned t = 0; t < 6; ++t) {
                    af_vec3 points[4];
                    node nodes[4];
                    for (unsigned k = 0; k < 4; ++k) {
                        points[k] = p[tet[t][k]];
                        nodes[k] = v[tet[t][k]];
                    }
                    tetra(&b, points, nodes);
                }
            }
    free(field);
    if (!b.count || b.count + 3 > b.cap) {
        free(b.v);
        free(b.ix);
        return drops(a, alpha, vertices, indices, mesh);
    }
    *vertices = b.v;
    *indices = b.ix;
    *mesh = (af_mesh_data){b.v, b.count, b.ix, b.count};
    return true;
}

/* SPDX-License-Identifier: MPL-2.0 */
#include "water_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static float clamp(float x, float a, float b) { return fmaxf(a, fminf(b, x)); }
static af_vec3 local(af_transform t, af_vec3 p) {
    p = af_vec3_sub(p, t.position);
    float c = cosf(t.rotation.y), s = sinf(t.rotation.y);
    return (af_vec3){c * p.x - s * p.z, p.y, s * p.x + c * p.z};
}
static af_vec3 rotate(af_transform t, af_vec3 p) {
    float c = cosf(t.rotation.y), s = sinf(t.rotation.y);
    return (af_vec3){c * p.x + s * p.z, p.y, -s * p.x + c * p.z};
}
static const wa_slot *slot(const af_water_runtime *r, af_object_id id) {
    for (size_t i = 0; r && i < r->count; ++i)
        if (r->slots[i].config.owner == id)
            return &r->slots[i];
    return NULL;
}
bool wa_sync(af_water_runtime *r, const af_scene *s) {
    size_t n = af_scene_water_count(s);
    /* Preserve by owner even when an earlier definition is removed. */
    for (size_t i = 0; i < n; ++i) {
        af_water w = *af_scene_water_at(s, i);
        const af_scene_object *o = af_scene_get_object(s, w.owner);
        if (!o || !wa_owner_valid(s, w.owner, w))
            return false;
        size_t k = i;
        for (; k < r->count && r->slots[k].config.owner != w.owner; ++k) {
        }
        if (k < r->count && k != i) {
            wa_slot *temp = malloc(sizeof(*temp));
            if (!temp)
                return false;
            *temp = r->slots[i];
            r->slots[i] = r->slots[k];
            r->slots[k] = *temp;
            free(temp);
        }
        wa_slot *a = &r->slots[i];
        if (k == r->count || a->config.owner != w.owner || a->config.quality != w.quality ||
            a->config.half_size.x != w.half_size.x || a->config.half_size.y != w.half_size.y ||
            a->config.mode != w.mode || a->config.path_count != w.path_count ||
            memcmp(a->config.path, w.path, sizeof(w.path)) ||
            memcmp(&a->transform, &o->transform, sizeof(o->transform))) {
            memset(a, 0, sizeof(*a));
            a->config = w;
            a->transform = o->transform;
            a->n = 16u << (unsigned)w.quality;
        }
        a->config = w;
    }
    for (size_t i = n; i < r->count; ++i)
        memset(&r->slots[i], 0, sizeof(r->slots[i]));
    r->count = n;
    r->stats = (af_water_stats){.surfaces = n};
    for (size_t i = 0; i < n; ++i)
        if (r->slots[i].config.enabled)
            r->stats.cells += (size_t)r->slots[i].n * r->slots[i].n;
    return true;
}
static float length(const af_water *w) {
    return w->mode == AF_WATER_RIVER ? wa_river_length(w) * .5f : w->half_size.y;
}
static float grid(const wa_slot *a, float x, float z, float alpha) {
    float gx = clamp((x / a->config.half_size.x + 1) * .5f * (float)(a->n - 1), 0, (float)a->n - 1);
    float gz = clamp((z / length(&a->config) + 1) * .5f * (float)(a->n - 1), 0, (float)a->n - 1);
    unsigned ix = (unsigned)gx, iz = (unsigned)gz, jx = ix + 1 < a->n ? ix + 1 : ix,
             jz = iz + 1 < a->n ? iz + 1 : iz;
    unsigned ids[] = {iz * a->n + ix, iz * a->n + jx, jz * a->n + ix, jz * a->n + jx};
    float h[4];
    for (unsigned i = 0; i < 4; ++i)
        h[i] = a->previous[ids[i]] + (a->h[ids[i]] - a->previous[ids[i]]) * alpha;
    float u = gx - (float)ix, v = gz - (float)iz;
    return (h[0] * (1 - u) + h[1] * u) * (1 - v) + (h[2] * (1 - u) + h[3] * u) * v;
}
void wa_step(af_water_runtime *r, float dt) {
    for (size_t i = 0; i < r->count; ++i) {
        wa_slot *a = &r->slots[i];
        af_water w = a->config;
        if (!w.enabled)
            continue;
        unsigned n = a->n;
        float dx = 2 * w.half_size.x / (float)(n - 1), dz = 2 * length(&w) / (float)(n - 1);
        float c = fminf(w.wave_speed, sqrtf(9.81f * w.depth)), c2 = c * c;
        unsigned steps = (unsigned)ceilf(
            dt * (c / fminf(dx, dz) + (w.mode == AF_WATER_RIVER ? w.flow_speed / dz : 0)) / .35f);
        if (!steps)
            steps = 1;
        float h = dt / (float)steps;
        for (unsigned sub = 0; sub < steps; ++sub) {
            for (unsigned z = 0; z < n; ++z)
                for (unsigned x = 0; x < n; ++x) {
                    unsigned k = z * n + x;
                    float v = a->h[k];
                    float lap =
                        (a->h[z * n + (x ? x - 1 : x)] - 2 * v + a->h[z * n + (x + 1 < n ? x + 1 : x)]) /
                            (dx * dx) +
                        (a->h[(z ? z - 1 : z) * n + x] - 2 * v + a->h[(z + 1 < n ? z + 1 : z) * n + x]) /
                            (dz * dz);
                    float wind = w.wind * sinf((float)x * .37f + (float)z * .19f + a->time * 1.7f);
                    float advect = w.mode == AF_WATER_RIVER
                                       ? w.flow_speed * (a->v[k] - a->v[(z ? z - 1 : z) * n + x]) / dz
                                       : 0;
                    a->v[k] = (a->v[k] + (c2 * lap + wind - advect) * h) * expf(-w.damping * h);
                }
            float next[AF_WATER_GRID_MAX * AF_WATER_GRID_MAX];
            float mean = 0;
            for (unsigned k = 0; k < n * n; ++k) {
                unsigned z = k / n;
                float advect = w.mode == AF_WATER_RIVER
                                   ? w.flow_speed * (a->h[k] - a->h[(z ? z - 1 : z) * n + k % n]) / dz
                                   : 0;
                next[k] =
                    clamp(a->h[k] + (a->v[k] - advect) * h, -w.depth * .25f, fminf(.5f, w.depth * .25f));
                if (w.mode == AF_WATER_RIVER) {
                    float edge = fminf((float)z, (float)(n - 1 - z));
                    if (edge < 3) {
                        float damping = expf(-(3 - edge) * 2 * h);
                        next[k] *= damping;
                        a->v[k] *= damping;
                    }
                }
                mean += next[k];
            }
            mean /= (float)(n * n);
            for (unsigned k = 0; k < n * n; ++k)
                a->h[k] = next[k] - (w.mode == AF_WATER_POOL ? mean : 0);
            a->time += h;
        }
        for (unsigned k = 0; k < n * n; ++k)
            r->stats.max_wave = fmaxf(r->stats.max_wave, fabsf(a->h[k]));
    }
}
static af_water_sample sample_id(const af_water_runtime *r, const af_scene *s, af_vec3 p, float alpha,
                                 af_object_id id) {
    for (size_t i = 0; i < af_scene_water_count(s); ++i) {
        af_water w = *af_scene_water_at(s, i);
        if (!w.enabled || (id && w.owner != id))
            continue;
        const af_scene_object *o = af_scene_get_object(s, w.owner);
        if (!o)
            continue;
        af_vec3 q = local(o->transform, p);
        float base = 0;
        af_vec3 tangent = {0, 0, 1};
        if (w.mode == AF_WATER_RIVER) {
            float side, along;
            if (!wa_river_coordinates(&w, q, &side, &along, &base, &tangent))
                continue;
            q.x = side;
            q.z = along - length(&w);
        } else if (fabsf(q.x) > w.half_size.x + 1e-4f || fabsf(q.z) > w.half_size.y + 1e-4f)
            continue;
        const wa_slot *a = slot(r, w.owner);
        float height = 0, v = 0;
        af_vec3 normal = {0, 1, 0};
        if (a && a->n) {
            height = grid(a, q.x, q.z, alpha);
            float dx = 2 * w.half_size.x / (float)(a->n - 1), dz = 2 * length(&w) / (float)(a->n - 1);
            normal = af_vec3_normalize(
                (af_vec3){-(grid(a, q.x + dx, q.z, alpha) - grid(a, q.x - dx, q.z, alpha)) / (2 * dx), 1,
                          -(grid(a, q.x, q.z + dz, alpha) - grid(a, q.x, q.z - dz, alpha)) / (2 * dz)});
            unsigned x = (unsigned)clamp((q.x / w.half_size.x + 1) * .5f * (float)(a->n - 1), 0,
                                         (float)a->n - 1),
                     z = (unsigned)clamp((q.z / length(&w) + 1) * .5f * (float)(a->n - 1), 0,
                                         (float)a->n - 1);
            v = a->v[z * a->n + x];
        }
        if (w.mode == AF_WATER_RIVER) {
            float across = -normal.x, down = -normal.z + tangent.y;
            normal = af_vec3_normalize((af_vec3){-across * tangent.z - down * tangent.x, 1,
                                                 across * tangent.x - down * tangent.z});
        }
        return (af_water_sample){
            true,
            w.owner,
            o->transform.position.y + base + height,
            o->transform.position.y + base - w.depth,
            w.density,
            rotate(o->transform, (af_vec3){w.mode == AF_WATER_RIVER ? tangent.x * w.flow_speed : 0,
                                           v + (w.mode == AF_WATER_RIVER ? tangent.y * w.flow_speed : 0),
                                           w.mode == AF_WATER_RIVER ? tangent.z * w.flow_speed : 0}),
            rotate(o->transform, normal)};
    }
    return (af_water_sample){0};
}
af_water_sample wa_sample(const af_water_runtime *r, const af_scene *s, af_vec3 p, float alpha) {
    return sample_id(r, s, p, alpha, 0);
}
bool wa_disturb(af_water_runtime *r, const af_scene *s, af_object_id id, af_vec3 p, float radius,
                float velocity) {
    if (!r || !isfinite(radius) || radius <= 0 || radius > 1000 || !isfinite(velocity) ||
        fabsf(velocity) > 20)
        return false;
    wa_slot *a = NULL;
    for (size_t i = 0; i < r->count; ++i)
        if (r->slots[i].config.owner == id)
            a = &r->slots[i];
    const af_scene_object *o = af_scene_get_object(s, id);
    if (!a || !o || !a->config.enabled)
        return false;
    af_vec3 q = local(o->transform, p);
    if (a->config.mode == AF_WATER_RIVER) {
        float side, along, height;
        af_vec3 tangent;
        if (!wa_river_coordinates(&a->config, q, &side, &along, &height, &tangent))
            return false;
        q.x = side;
        q.z = along - length(&a->config);
    } else if (fabsf(q.x) > a->config.half_size.x || fabsf(q.z) > a->config.half_size.y)
        return false;
    for (unsigned z = 0; z < a->n; ++z)
        for (unsigned x = 0; x < a->n; ++x) {
            float dx = ((float)x / (float)(a->n - 1) * 2 - 1) * a->config.half_size.x - q.x,
                  dz = ((float)z / (float)(a->n - 1) * 2 - 1) * length(&a->config) - q.z;
            float f = fmaxf(0, 1 - (dx * dx + dz * dz) / (radius * radius));
            a->v[z * a->n + x] = clamp(a->v[z * a->n + x] + velocity * f * f, -10, 10);
        }
    return true;
}
bool wa_mesh(const af_water_runtime *r, const af_scene *s, af_object_id id, float alpha,
             af_vertex **vertices, uint32_t **indices, af_mesh_data *mesh) {
    const af_water *w = af_scene_get_water(s, id);
    const af_scene_object *o = af_scene_get_object(s, id);
    if (!w || !o)
        return false;
    unsigned n = 16u << (unsigned)w->quality;
    uint32_t vc = n * n, ic = (n - 1) * (n - 1) * 6;
    af_vertex *v = calloc(vc, sizeof(*v));
    uint32_t *ix = malloc(ic * sizeof(*ix));
    if (!v || !ix) {
        free(v);
        free(ix);
        return false;
    }
    af_transform inverse = o->transform;
    inverse.rotation.y = -inverse.rotation.y;
    for (unsigned z = 0; z < n; ++z)
        for (unsigned x = 0; x < n; ++x) {
            af_vec3 p = {((float)x / (float)(n - 1) * 2 - 1) * w->half_size.x, 0,
                         ((float)z / (float)(n - 1) * 2 - 1) * length(w)};
            if (w->mode == AF_WATER_RIVER) {
                af_vec3 tangent, center = wa_river_point(w, p.z + length(w), &tangent);
                p = af_vec3_add(center, (af_vec3){p.x * tangent.z, 0, -p.x * tangent.x});
            }
            af_vec3 wp = af_vec3_add(o->transform.position, rotate(o->transform, p));
            af_water_sample sample = sample_id(r, s, wp, alpha, id);
            p.y = sample.height - o->transform.position.y;
            v[z * n + x] = (af_vertex){.position = p,
                                       .normal = rotate(inverse, sample.normal),
                                       .color = {1, 1, 1, 1},
                                       .uv = {(float)x / (float)(n - 1), (float)z / (float)(n - 1)}};
        }
    unsigned k = 0;
    for (unsigned z = 0; z < n - 1; ++z)
        for (unsigned x = 0; x < n - 1; ++x) {
            unsigned a = z * n + x, b = a + 1, c = a + n, d = c + 1;
            ix[k++] = a;
            ix[k++] = c;
            ix[k++] = b;
            ix[k++] = b;
            ix[k++] = c;
            ix[k++] = d;
        }
    *vertices = v;
    *indices = ix;
    *mesh = (af_mesh_data){v, vc, ix, ic};
    return true;
}

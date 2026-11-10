/* SPDX-License-Identifier: MPL-2.0 */
#include "water_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static float clamp(float x, float a, float b) {
    return fmaxf(a, fminf(b, x));
}
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
    r->wind = af_scene_get_wind(s);
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
            a->config.mode != w.mode || a->config.particle_spacing != w.particle_spacing ||
            a->config.initial_volume != w.initial_volume || a->config.path_count != w.path_count ||
            memcmp(a->config.path, w.path, sizeof(w.path)) ||
            memcmp(&a->transform, &o->transform, sizeof(o->transform))) {
            wa_particles_free(a->particles);
            memset(a, 0, sizeof(*a));
            a->config = w;
            a->transform = o->transform;
            a->n = 16u << (unsigned)w.quality;
        }
        a->config = w;
        if (w.mode == AF_WATER_PARTICLES && !wa_particles_sync(a))
            return false;
    }
    for (size_t i = n; i < r->count; ++i) {
        wa_particles_free(r->slots[i].particles);
        memset(&r->slots[i], 0, sizeof(r->slots[i]));
    }
    r->count = n;
    uint64_t key = UINT64_C(1469598103934665603) ^ r->disturbance;
    float wind_values[] = {r->wind.direction.x,   r->wind.direction.z,    r->wind.speed,
                           r->wind.gust_strength, r->wind.gust_frequency, r->wind.air_density};
    key ^= r->wind.enabled ? 1u : 0u;
    for (size_t k = 0; k < sizeof(wind_values) / sizeof(*wind_values); ++k) {
        uint32_t bits = 0;
        memcpy(&bits, &wind_values[k], sizeof(bits));
        for (unsigned byte = 0; byte < 4; ++byte)
            key = (key ^ ((bits >> (byte * 8)) & 255u)) * UINT64_C(1099511628211);
    }
    for (size_t i = 0; i < n; ++i) {
        const unsigned char *bytes = (const unsigned char *)&r->slots[i].config;
        for (size_t k = 0; k < sizeof(af_water); ++k)
            key = (key ^ bytes[k]) * UINT64_C(1099511628211);
        bytes = (const unsigned char *)&r->slots[i].transform;
        for (size_t k = 0; k < sizeof(af_transform); ++k)
            key = (key ^ bytes[k]) * UINT64_C(1099511628211);
    }
    r->key = key;
    r->stats = (af_water_stats){.surfaces = n};
    for (size_t i = 0; i < n; ++i)
        if (r->slots[i].config.enabled)
            r->stats.cells += r->slots[i].config.mode == AF_WATER_PARTICLES ? 0 : (size_t)r->slots[i].n * r->slots[i].n;
    return true;
}
static float length(const af_water *w) {
    return w->mode == AF_WATER_RIVER ? wa_river_length(w) * .5f : w->half_size.y;
}
static float grid(const wa_slot *a, float x, float z, float alpha) {
    float gx = clamp((x / a->config.half_size.x + 1) * .5f * (float)(a->n - 1), 0, (float)a->n - 1);
    float gz = clamp((z / length(&a->config) + 1) * .5f * (float)(a->n - 1), 0, (float)a->n - 1);
    unsigned ix = (unsigned)gx, iz = (unsigned)gz, jx = ix + 1 < a->n ? ix + 1 : ix, jz = iz + 1 < a->n ? iz + 1 : iz;
    unsigned ids[] = {iz * a->n + ix, iz * a->n + jx, jz * a->n + ix, jz * a->n + jx};
    float h[4];
    for (unsigned i = 0; i < 4; ++i)
        h[i] = a->previous[ids[i]] + (a->h[ids[i]] - a->previous[ids[i]]) * alpha;
    float u = gx - (float)ix, v = gz - (float)iz;
    return (h[0] * (1 - u) + h[1] * u) * (1 - v) + (h[2] * (1 - u) + h[3] * u) * v;
}
void wa_step(af_water_runtime *r, float dt) {
    af_vec3 air = af_wind_velocity(r->wind, r->time);
    float air_speed = af_vec3_length(air);
    r->time += dt;
    for (size_t i = 0; i < r->count; ++i) {
        wa_slot *a = &r->slots[i];
        af_water w = a->config;
        if (!w.enabled || w.mode == AF_WATER_PARTICLES)
            continue;
        float drive = w.wind + fminf(.25f, .0015f * air_speed * air_speed);
        float yaw = a->transform.rotation.y, cosa = cosf(yaw), sina = sinf(yaw);
        af_vec3 direction = air_speed > .001f ? af_vec3_scale(air, 1 / air_speed) : (af_vec3){.8f, 0, .6f};
        float wind_x = cosa * direction.x - sina * direction.z, wind_z = sina * direction.x + cosa * direction.z;
        if (a->sleeping && drive == 0)
            continue;
        a->sleeping = false;
        unsigned n = a->n;
        float dx = 2 * w.half_size.x / (float)(n - 1), dz = 2 * length(&w) / (float)(n - 1);
        float c = fminf(w.wave_speed, sqrtf(9.81f * w.depth)), c2 = c * c;
        unsigned steps =
            (unsigned)ceilf(dt * (c / fminf(dx, dz) + (w.mode == AF_WATER_RIVER ? w.flow_speed / dz : 0)) / .35f);
        if (!steps)
            steps = 1;
        float h = dt / (float)steps;
        for (unsigned sub = 0; sub < steps; ++sub) {
            float old_v[AF_WATER_GRID_MAX * AF_WATER_GRID_MAX];
            memcpy(old_v, a->v, n * n * sizeof(float));
            for (unsigned z = 0; z < n; ++z)
                for (unsigned x = 0; x < n; ++x) {
                    unsigned k = z * n + x;
                    float v = a->h[k];
                    float lap =
                        (a->h[z * n + (x ? x - 1 : x)] - 2 * v + a->h[z * n + (x + 1 < n ? x + 1 : x)]) / (dx * dx) +
                        (a->h[(z ? z - 1 : z) * n + x] - 2 * v + a->h[(z + 1 < n ? z + 1 : z) * n + x]) / (dz * dz);
                    float along = (2 * (float)x / (float)(n - 1) - 1) * w.half_size.x,
                          across = (2 * (float)z / (float)(n - 1) - 1) * length(&w);
                    float phase = along * wind_x + across * wind_z;
                    float wind = drive * sinf(phase * 1.6f - a->time * (1.7f + air_speed * .1f));
                    float advect =
                        w.mode == AF_WATER_RIVER ? w.flow_speed * (old_v[k] - old_v[(z ? z - 1 : z) * n + x]) / dz : 0;
                    a->v[k] = (a->v[k] + (c2 * lap + wind - advect) * h) * expf(-w.damping * h);
                }
            float next[AF_WATER_GRID_MAX * AF_WATER_GRID_MAX];
            float mean = 0;
            for (unsigned k = 0; k < n * n; ++k) {
                unsigned z = k / n;
                float advect =
                    w.mode == AF_WATER_RIVER ? w.flow_speed * (a->h[k] - a->h[(z ? z - 1 : z) * n + k % n]) / dz : 0;
                next[k] = clamp(a->h[k] + (a->v[k] - advect) * h, -w.depth * .25f, fminf(.5f, w.depth * .25f));
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
        float amplitude = 0, speed = 0;
        for (unsigned k = 0; k < n * n; ++k) {
            amplitude = fmaxf(amplitude, fabsf(a->h[k]));
            speed = fmaxf(speed, fabsf(a->v[k]));
        }
        a->quiet_time = drive == 0 && amplitude < .0002f && speed < .0005f ? a->quiet_time + dt : 0;
        if (a->quiet_time > .5f) {
            memset(a->h, 0, sizeof(a->h));
            memset(a->v, 0, sizeof(a->v));
            a->sleeping = true;
        }
        r->stats.max_wave = fmaxf(r->stats.max_wave, amplitude);
    }
}
static af_water_sample sample_id(const af_water_runtime *r, const af_scene *s, af_vec3 p, float alpha,
                                 af_object_id id) {
    for (size_t i = 0; i < af_scene_water_count(s); ++i) {
        af_water w = *af_scene_water_at(s, i);
        if (!w.enabled || w.mode == AF_WATER_PARTICLES || (id && w.owner != id))
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
            unsigned x = (unsigned)clamp((q.x / w.half_size.x + 1) * .5f * (float)(a->n - 1), 0, (float)a->n - 1),
                     z = (unsigned)clamp((q.z / length(&w) + 1) * .5f * (float)(a->n - 1), 0, (float)a->n - 1);
            v = a->v[z * a->n + x];
        }
        if (w.mode == AF_WATER_RIVER) {
            float across = -normal.x, down = -normal.z + tangent.y;
            normal = af_vec3_normalize(
                (af_vec3){-across * tangent.z - down * tangent.x, 1, across * tangent.x - down * tangent.z});
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
    if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z))
        return (af_water_sample){0};
    return sample_id(r, s, p, alpha, 0);
}
bool wa_contains(const af_water_runtime *r, const af_scene *s, af_object_id id, af_vec3 p) {
    af_water_sample sample = sample_id(r, s, p, 1, id);
    return sample.inside && p.y >= sample.bottom && p.y < sample.height;
}
bool wa_disturb(af_water_runtime *r, const af_scene *s, af_object_id id, af_vec3 p, float radius, float velocity) {
    if (!r || !isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) || !isfinite(radius) || radius <= 0 || radius > 1000 ||
        !isfinite(velocity) || fabsf(velocity) > 20)
        return false;
    wa_slot *a = NULL;
    for (size_t i = 0; i < r->count; ++i)
        if (r->slots[i].config.owner == id)
            a = &r->slots[i];
    const af_scene_object *o = af_scene_get_object(s, id);
    if (!a || !o || !a->config.enabled || a->config.mode == AF_WATER_PARTICLES)
        return false;
    a->sleeping = false;
    a->quiet_time = 0;
    ++r->disturbance;
    ++r->key;
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
static af_vec3 river_bank(const af_water *w, unsigned node) {
    unsigned before = node ? node - 1 : node, after = node + 1 < w->path_count ? node + 1 : node;
    af_vec3 a = af_vec3_sub(w->path[node ? node : after], w->path[before]);
    af_vec3 b = af_vec3_sub(w->path[after], w->path[node + 1 < w->path_count ? node : before]);
    a.y = b.y = 0;
    a = af_vec3_normalize(a);
    b = af_vec3_normalize(b);
    float scale = 1 / (1 + af_vec3_dot(a, b));
    return (af_vec3){(a.z + b.z) * scale, 0, -(a.x + b.x) * scale};
}
static af_vec3 river_mesh_point(const af_water *w, float along, float side) {
    for (unsigned i = 1; i < w->path_count; ++i) {
        af_vec3 delta = af_vec3_sub(w->path[i], w->path[i - 1]);
        float distance = hypotf(delta.x, delta.z);
        if (along <= distance || i + 1 == w->path_count) {
            float t = clamp(along / distance, 0, 1);
            af_vec3 a = river_bank(w, i - 1), b = river_bank(w, i);
            return af_vec3_add(af_vec3_add(w->path[i - 1], af_vec3_scale(delta, t)),
                               af_vec3_scale(af_vec3_add(af_vec3_scale(a, 1 - t), af_vec3_scale(b, t)), side));
        }
        along -= distance;
    }
    return (af_vec3){0};
}
bool wa_mesh(const af_water_runtime *r, const af_scene *s, af_object_id id, float alpha, af_vertex **vertices,
             uint32_t **indices, af_mesh_data *mesh) {
    const af_water *w = af_scene_get_water(s, id);
    const af_scene_object *o = af_scene_get_object(s, id);
    if (!w || !o)
        return false;
    if (w->mode == AF_WATER_PARTICLES) {
        const wa_slot *a = slot(r, id);
        wa_slot preview = {.config = *w, .transform = o->transform};
        if (!a) {
            if (!wa_particles_sync(&preview))
                return false;
            a = &preview;
        }
        bool ok = wa_particles_mesh(a, alpha, vertices, indices, mesh);
        wa_particles_free(preview.particles);
        return ok;
    }
    unsigned n = 16u << (unsigned)w->quality;
    uint32_t surface_vertices = n * n;
    uint32_t vc = 2 * surface_vertices + 16 * (n - 1), ic = (n - 1) * (n - 1) * 12 + 24 * (n - 1);
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
            float grid_x = p.x, grid_z = p.z;
            if (w->mode == AF_WATER_RIVER)
                p = river_mesh_point(w, p.z + length(w), p.x);
            float base = p.y;
            af_vec3 wp = af_vec3_add(o->transform.position, rotate(o->transform, p));
            af_water_sample sample = sample_id(r, s, wp, alpha, id);
            p.y = sample.height - o->transform.position.y;
            if (w->mode == AF_WATER_RIVER) {
                const wa_slot *a = slot(r, id);
                p.y = base + (a ? grid(a, grid_x, grid_z, alpha) : 0);
            }
            v[z * n + x] = (af_vertex){.position = p,
                                       .normal = rotate(inverse, sample.normal),
                                       .color = {1, 1, 1, 1},
                                       .uv = {(float)x / (float)(n - 1), (float)z / (float)(n - 1)}};
            v[surface_vertices + z * n + x] = v[z * n + x];
            v[surface_vertices + z * n + x].position.y = base - w->depth;
            v[surface_vertices + z * n + x].normal = (af_vec3){0, -1, 0};
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
            ix[k++] = surface_vertices + a;
            ix[k++] = surface_vertices + b;
            ix[k++] = surface_vertices + c;
            ix[k++] = surface_vertices + b;
            ix[k++] = surface_vertices + d;
            ix[k++] = surface_vertices + c;
        }
    for (unsigned z = 0; z < n; ++z)
        for (unsigned x = 0; x < n; ++x) {
            af_vec3 dx = af_vec3_sub(v[surface_vertices + z * n + (x + 1 < n ? x + 1 : x)].position,
                                     v[surface_vertices + z * n + (x ? x - 1 : x)].position);
            af_vec3 dz = af_vec3_sub(v[surface_vertices + (z + 1 < n ? z + 1 : z) * n + x].position,
                                     v[surface_vertices + (z ? z - 1 : z) * n + x].position);
            v[surface_vertices + z * n + x].normal = af_vec3_normalize(af_vec3_cross(dx, dz));
            dx = af_vec3_sub(v[z * n + (x + 1 < n ? x + 1 : x)].position, v[z * n + (x ? x - 1 : x)].position);
            dz = af_vec3_sub(v[(z + 1 < n ? z + 1 : z) * n + x].position, v[(z ? z - 1 : z) * n + x].position);
            v[z * n + x].normal = af_vec3_normalize(af_vec3_cross(dz, dx));
        }
    unsigned next = 2 * surface_vertices;
    for (unsigned edge = 0; edge < 4; ++edge)
        for (unsigned j = 0; j < n - 1; ++j) {
            unsigned a = edge == 0 ? j : edge == 1 ? j * n + n - 1 : edge == 2 ? n * n - 1 - j : (n - 1 - j) * n;
            unsigned b = edge == 0 ? a + 1 : edge == 1 ? a + n : edge == 2 ? a - 1 : a - n;
            af_vec3 normal =
                af_vec3_normalize(af_vec3_cross(af_vec3_sub(v[b].position, v[a].position), (af_vec3){0, -1, 0}));
            unsigned source[] = {a, b, surface_vertices + b, surface_vertices + a};
            for (unsigned q = 0; q < 4; ++q) {
                v[next + q] = v[source[q]];
                v[next + q].normal = normal;
            }
            ix[k++] = next;
            ix[k++] = next + 1;
            ix[k++] = next + 2;
            ix[k++] = next;
            ix[k++] = next + 2;
            ix[k++] = next + 3;
            next += 4;
        }
    *vertices = v;
    *indices = ix;
    *mesh = (af_mesh_data){v, vc, ix, ic};
    return true;
}

void wa_destroy(af_water_runtime *r) {
    if (r)
        for (size_t i = 0; i < AF_SCENE_MAX_WATERS; ++i) {
            wa_particles_free(r->slots[i].particles);
            r->slots[i].particles = NULL;
        }
}

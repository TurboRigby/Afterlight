/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/landscape.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHUNK_V AF_LANDSCAPE_CHUNK_VERTICES
#define CHUNK_Q AF_LANDSCAPE_CHUNK_QUADS

static inline float clamp_f(float val, float min_v, float max_v) {
    return val < min_v ? min_v : (val > max_v ? max_v : val);
}

/* Simple coherent hash / noise for procedural generation */
static uint32_t hash_u32(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

static float pseudo_rand_float(uint32_t *state) {
    *state = hash_u32(*state + 0x9e3779b9u);
    return (float)(*state & 0x00ffffffu) / 16777215.0f;
}

static inline af_vec2 grad2d(uint32_t hash) {
    static const float g[8][2] = {
        { 1.0f,  0.0f},
        {-1.0f,  0.0f},
        { 0.0f,  1.0f},
        { 0.0f, -1.0f},
        { 0.70710678f,  0.70710678f},
        {-0.70710678f,  0.70710678f},
        { 0.70710678f, -0.70710678f},
        {-0.70710678f, -0.70710678f}
    };
    return (af_vec2){g[hash & 7u][0], g[hash & 7u][1]};
}

static float noise2d(float x, float z) {
    int32_t ix = (int32_t)floorf(x);
    int32_t iz = (int32_t)floorf(z);
    float fx = x - (float)ix;
    float fz = z - (float)iz;

    /* Quintic Hermite interpolant: 6t^5 - 15t^4 + 10t^3 (C2 continuous) */
    float wx = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
    float wz = fz * fz * fz * (fz * (fz * 6.0f - 15.0f) + 10.0f);

    uint32_t uix = (uint32_t)(ix + 0x100000);
    uint32_t uiz = (uint32_t)(iz + 0x100000);

    uint32_t h00 = hash_u32(uix * 374761393u + uiz * 668265263u);
    uint32_t h10 = hash_u32((uix + 1u) * 374761393u + uiz * 668265263u);
    uint32_t h01 = hash_u32(uix * 374761393u + (uiz + 1u) * 668265263u);
    uint32_t h11 = hash_u32((uix + 1u) * 374761393u + (uiz + 1u) * 668265263u);

    af_vec2 g00 = grad2d(h00);
    af_vec2 g10 = grad2d(h10);
    af_vec2 g01 = grad2d(h01);
    af_vec2 g11 = grad2d(h11);

    float d00 = g00.x * fx + g00.y * fz;
    float d10 = g10.x * (fx - 1.0f) + g10.y * fz;
    float d01 = g01.x * fx + g01.y * (fz - 1.0f);
    float d11 = g11.x * (fx - 1.0f) + g11.y * (fz - 1.0f);

    float nx0 = d00 + wx * (d10 - d00);
    float nx1 = d01 + wx * (d11 - d01);
    return nx0 + wz * (nx1 - nx0);
}

static float fbm2d(float x, float z, int octaves) {
    float val = 0.0f;
    float amp = 1.0f;
    float freq = 1.0f;
    float max_amp = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        val += noise2d(x * freq, z * freq) * amp;
        max_amp += amp;
        amp *= 0.45f;
        freq *= 2.0f;
    }
    return val / max_amp;
}

af_terrain_layer af_terrain_layer_default(size_t index) {
    af_terrain_layer layer = {0};
    layer.roughness = 0.8f;
    layer.metalness = 0.0f;
    layer.uv_scale = 4.0f;
    layer.color = (af_color){1.0f, 1.0f, 1.0f, 1.0f};

    switch (index) {
        case 0:
            snprintf(layer.name, sizeof(layer.name), "Grass");
            layer.color = (af_color){0.34f, 0.52f, 0.22f, 1.0f};
            layer.roughness = 0.85f;
            break;
        case 1:
            snprintf(layer.name, sizeof(layer.name), "Dirt");
            layer.color = (af_color){0.45f, 0.33f, 0.22f, 1.0f};
            layer.roughness = 0.90f;
            break;
        case 2:
            snprintf(layer.name, sizeof(layer.name), "Rock");
            layer.color = (af_color){0.50f, 0.52f, 0.55f, 1.0f};
            layer.roughness = 0.65f;
            break;
        case 3:
            snprintf(layer.name, sizeof(layer.name), "Sand");
            layer.color = (af_color){0.82f, 0.76f, 0.58f, 1.0f};
            layer.roughness = 0.75f;
            break;
        default:
            snprintf(layer.name, sizeof(layer.name), "Layer %zu", index + 1);
            break;
    }
    return layer;
}

af_placement_rules af_placement_rules_default(void) {
    af_placement_rules rules = {0};
    rules.random_yaw = true;
    rules.yaw_min = 0.0f;
    rules.yaw_max = AF_PI * 2.0f;
    rules.random_pitch_roll = false;
    rules.max_tilt = 0.15f;
    rules.scale_min = (af_vec3){0.8f, 0.8f, 0.8f};
    rules.scale_max = (af_vec3){1.2f, 1.2f, 1.2f};
    rules.uniform_scale = true;
    rules.slope_min = 0.0f;
    rules.slope_max = AF_PI * 0.35f; /* Up to ~63 degrees */
    rules.height_min = -1000.0f;
    rules.height_max = 10000.0f;
    rules.density = 1.0f;
    rules.min_spacing = 1.5f;
    rules.align_to_normal = 0.0f;    /* Keep trees upright by default */
    rules.offset_y = 0.0f;
    rules.paint_mask_layer = -1;    /* Any layer */
    rules.collision_avoidance = true;
    return rules;
}

af_foliage_layer af_foliage_layer_default(const char *name, const char *mesh) {
    af_foliage_layer layer = {0};
    if (name) snprintf(layer.name, sizeof(layer.name), "%s", name);
    else snprintf(layer.name, sizeof(layer.name), "Foliage Layer");
    if (mesh) snprintf(layer.mesh, sizeof(layer.mesh), "%s", mesh);
    layer.material = af_material_default();
    layer.visible = true;
    layer.cast_shadows = true;
    layer.max_distance = 150.0f;
    layer.rules = af_placement_rules_default();
    return layer;
}

static void init_chunk(af_landscape *landscape, size_t chunk_idx, int32_t cx, int32_t cz) {
    af_terrain_chunk *c = &landscape->chunks[chunk_idx];
    memset(c, 0, sizeof(*c));
    c->chunk_x = cx;
    c->chunk_z = cz;

    float chunk_size = (float)CHUNK_Q * landscape->vertex_spacing;
    c->bounds_min.x = landscape->origin.x + (float)cx * chunk_size;
    c->bounds_min.z = landscape->origin.z + (float)cz * chunk_size;
    c->bounds_min.y = landscape->origin.y;

    c->bounds_max.x = c->bounds_min.x + chunk_size;
    c->bounds_max.z = c->bounds_min.z + chunk_size;
    c->bounds_max.y = landscape->origin.y;

    for (size_t i = 0; i < CHUNK_V * CHUNK_V; ++i) {
        c->heights[i] = 0.0f;
        c->normals[i] = (af_vec3){0.0f, 1.0f, 0.0f};
        /* 100% layer 0 (Grass) */
        c->weights[i * 4 + 0] = 255;
        c->weights[i * 4 + 1] = 0;
        c->weights[i * 4 + 2] = 0;
        c->weights[i * 4 + 3] = 0;
    }
    c->dirty_mesh = true;
    snprintf(c->mesh_name, sizeof(c->mesh_name), "__terrain_c_%d_%d", cx, cz);
}

af_landscape *af_landscape_create(uint32_t chunks_x, uint32_t chunks_z, float spacing) {
    if (chunks_x == 0) chunks_x = 4;
    if (chunks_z == 0) chunks_z = 4;
    if (spacing <= 0.01f) spacing = 1.0f;

    af_landscape *ls = calloc(1, sizeof(*ls));
    if (!ls) return NULL;

    ls->enabled = true;
    ls->chunk_count_x = chunks_x;
    ls->chunk_count_z = chunks_z;
    ls->vertex_spacing = spacing;
    ls->chunk_count = (size_t)chunks_x * chunks_z;

    float total_w = (float)(chunks_x * CHUNK_Q) * spacing;
    float total_d = (float)(chunks_z * CHUNK_Q) * spacing;
    ls->origin = (af_vec3){-total_w * 0.5f, 0.0f, -total_d * 0.5f};

    ls->chunks = calloc(ls->chunk_count, sizeof(af_terrain_chunk));
    if (!ls->chunks) {
        free(ls);
        return NULL;
    }

    for (uint32_t cz = 0; cz < chunks_z; ++cz) {
        for (uint32_t cx = 0; cx < chunks_x; ++cx) {
            size_t idx = (size_t)cz * chunks_x + cx;
            init_chunk(ls, idx, (int32_t)cx, (int32_t)cz);
        }
    }

    ls->layer_count = AF_LANDSCAPE_MAX_LAYERS;
    for (size_t i = 0; i < AF_LANDSCAPE_MAX_LAYERS; ++i) {
        ls->layers[i] = af_terrain_layer_default(i);
    }

    ls->foliage_layer_count = 0;
    ls->total_foliage_instances = 0;
    ls->modification_version = 1;
    return ls;
}

af_landscape *af_landscape_clone(const af_landscape *src) {
    if (!src) return NULL;
    af_landscape *dst = calloc(1, sizeof(*dst));
    if (!dst) return NULL;

    memcpy(dst, src, sizeof(*dst));
    dst->chunks = calloc(dst->chunk_count, sizeof(af_terrain_chunk));
    if (!dst->chunks) {
        free(dst);
        return NULL;
    }

    for (size_t c = 0; c < src->chunk_count; ++c) {
        memcpy(&dst->chunks[c], &src->chunks[c], sizeof(af_terrain_chunk));
        /* Deep clone foliage chunks */
        for (size_t fl = 0; fl < AF_LANDSCAPE_MAX_FOLIAGE_LAYERS; ++fl) {
            const af_foliage_chunk *sc = &src->chunks[c].foliage[fl];
            af_foliage_chunk *dc = &dst->chunks[c].foliage[fl];
            dc->instances = NULL;
            dc->capacity = 0;
            dc->count = 0;
            if (sc->count > 0 && sc->instances) {
                dc->instances = malloc(sc->count * sizeof(af_foliage_instance));
                if (dc->instances) {
                    memcpy(dc->instances, sc->instances, sc->count * sizeof(af_foliage_instance));
                    dc->count = sc->count;
                    dc->capacity = sc->count;
                }
            }
        }
    }
    return dst;
}

void af_landscape_destroy(af_landscape *ls) {
    if (!ls) return;
    if (ls->chunks) {
        for (size_t c = 0; c < ls->chunk_count; ++c) {
            for (size_t fl = 0; fl < AF_LANDSCAPE_MAX_FOLIAGE_LAYERS; ++fl) {
                free(ls->chunks[c].foliage[fl].instances);
            }
        }
        free(ls->chunks);
    }
    free(ls);
}

/* Internal helper: find chunk and local vertex grid coordinates */
static bool get_local_coords(const af_landscape *ls, float world_x, float world_z,
                            int32_t *out_cx, int32_t *out_cz,
                            float *out_lx, float *out_lz) {
    if (!ls || !ls->chunks) return false;
    float rx = (world_x - ls->origin.x) / ls->vertex_spacing;
    float rz = (world_z - ls->origin.z) / ls->vertex_spacing;

    float total_verts_x = (float)(ls->chunk_count_x * CHUNK_Q);
    float total_verts_z = (float)(ls->chunk_count_z * CHUNK_Q);

    if (rx < 0.0f || rz < 0.0f || rx > total_verts_x || rz > total_verts_z)
        return false;

    int32_t cx = (int32_t)floorf(rx / (float)CHUNK_Q);
    int32_t cz = (int32_t)floorf(rz / (float)CHUNK_Q);

    if (cx >= (int32_t)ls->chunk_count_x) cx = (int32_t)ls->chunk_count_x - 1;
    if (cz >= (int32_t)ls->chunk_count_z) cz = (int32_t)ls->chunk_count_z - 1;
    if (cx < 0) cx = 0;
    if (cz < 0) cz = 0;

    float lx = rx - (float)((int64_t)cx * (int64_t)CHUNK_Q);
    float lz = rz - (float)((int64_t)cz * (int64_t)CHUNK_Q);

    if (out_cx) *out_cx = cx;
    if (out_cz) *out_cz = cz;
    if (out_lx) *out_lx = lx;
    if (out_lz) *out_lz = lz;
    return true;
}

static inline size_t chunk_index(const af_landscape *ls, int32_t cx, int32_t cz) {
    return (size_t)(cz >= 0 ? (uint32_t)cz : 0u) * ls->chunk_count_x + (size_t)(cx >= 0 ? (uint32_t)cx : 0u);
}

float af_landscape_get_height(const af_landscape *ls, float world_x, float world_z) {
    int32_t cx, cz;
    float lx, lz;
    if (!get_local_coords(ls, world_x, world_z, &cx, &cz, &lx, &lz))
        return ls ? ls->origin.y : 0.0f;

    const af_terrain_chunk *c = &ls->chunks[chunk_index(ls, cx, cz)];
    uint32_t qx = (uint32_t)clamp_f(floorf(lx), 0.0f, (float)(CHUNK_Q - 1));
    uint32_t qz = (uint32_t)clamp_f(floorf(lz), 0.0f, (float)(CHUNK_Q - 1));

    float tx = lx - (float)qx;
    float tz = lz - (float)qz;

    float h00 = c->heights[qz * CHUNK_V + qx];
    float h10 = c->heights[qz * CHUNK_V + (qx + 1)];
    float h01 = c->heights[(qz + 1) * CHUNK_V + qx];
    float h11 = c->heights[(qz + 1) * CHUNK_V + (qx + 1)];

    float h0 = h00 + tx * (h10 - h00);
    float h1 = h01 + tx * (h11 - h01);
    return h0 + tz * (h1 - h0);
}

bool af_landscape_get_normal(const af_landscape *ls, float world_x, float world_z, af_vec3 *out_normal) {
    if (!out_normal) return false;
    int32_t cx, cz;
    float lx, lz;
    if (!get_local_coords(ls, world_x, world_z, &cx, &cz, &lx, &lz)) {
        *out_normal = (af_vec3){0.0f, 1.0f, 0.0f};
        return false;
    }

    const af_terrain_chunk *c = &ls->chunks[chunk_index(ls, cx, cz)];
    uint32_t qx = (uint32_t)clamp_f(floorf(lx), 0.0f, (float)(CHUNK_Q - 1));
    uint32_t qz = (uint32_t)clamp_f(floorf(lz), 0.0f, (float)(CHUNK_Q - 1));

    float tx = lx - (float)qx;
    float tz = lz - (float)qz;

    af_vec3 n00 = c->normals[qz * CHUNK_V + qx];
    af_vec3 n10 = c->normals[qz * CHUNK_V + (qx + 1)];
    af_vec3 n01 = c->normals[(qz + 1) * CHUNK_V + qx];
    af_vec3 n11 = c->normals[(qz + 1) * CHUNK_V + (qx + 1)];

    af_vec3 n0 = af_vec3_add(n00, af_vec3_scale(af_vec3_sub(n10, n00), tx));
    af_vec3 n1 = af_vec3_add(n01, af_vec3_scale(af_vec3_sub(n11, n01), tx));
    af_vec3 n = af_vec3_add(n0, af_vec3_scale(af_vec3_sub(n1, n0), tz));
    *out_normal = af_vec3_normalize(n);
    return true;
}

bool af_landscape_get_layer_weights(const af_landscape *ls, float world_x, float world_z, float out_weights[4]) {
    if (!out_weights) return false;
    int32_t cx, cz;
    float lx, lz;
    if (!get_local_coords(ls, world_x, world_z, &cx, &cz, &lx, &lz)) {
        out_weights[0] = 1.0f;
        out_weights[1] = out_weights[2] = out_weights[3] = 0.0f;
        return false;
    }

    const af_terrain_chunk *c = &ls->chunks[chunk_index(ls, cx, cz)];
    uint32_t qx = (uint32_t)clamp_f(floorf(lx), 0.0f, (float)(CHUNK_Q - 1));
    uint32_t qz = (uint32_t)clamp_f(floorf(lz), 0.0f, (float)(CHUNK_Q - 1));

    float tx = lx - (float)qx;
    float tz = lz - (float)qz;

    size_t i00 = (size_t)(qz * CHUNK_V + qx) * 4;
    size_t i10 = (size_t)(qz * CHUNK_V + (qx + 1)) * 4;
    size_t i01 = (size_t)((qz + 1) * CHUNK_V + qx) * 4;
    size_t i11 = (size_t)((qz + 1) * CHUNK_V + (qx + 1)) * 4;

    float total = 0.0f;
    for (size_t k = 0; k < 4; ++k) {
        float w00 = (float)c->weights[i00 + k];
        float w10 = (float)c->weights[i10 + k];
        float w01 = (float)c->weights[i01 + k];
        float w11 = (float)c->weights[i11 + k];
        float w0 = w00 + tx * (w10 - w00);
        float w1 = w01 + tx * (w11 - w01);
        float w = w0 + tz * (w1 - w0);
        out_weights[k] = w;
        total += w;
    }
    if (total > 0.001f) {
        for (size_t k = 0; k < 4; ++k) out_weights[k] /= total;
    } else {
        out_weights[0] = 1.0f;
    }
    return true;
}

static float get_vertex_height_global(const af_landscape *ls, int32_t gx, int32_t gz) {
    int32_t total_x = (int32_t)(ls->chunk_count_x * CHUNK_Q);
    int32_t total_z = (int32_t)(ls->chunk_count_z * CHUNK_Q);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > total_x) gx = total_x;
    if (gz > total_z) gz = total_z;

    int32_t cx = gx / (int32_t)CHUNK_Q;
    int32_t cz = gz / (int32_t)CHUNK_Q;
    if (cx >= (int32_t)ls->chunk_count_x) cx = (int32_t)ls->chunk_count_x - 1;
    if (cz >= (int32_t)ls->chunk_count_z) cz = (int32_t)ls->chunk_count_z - 1;

    int32_t lx = gx - cx * (int32_t)CHUNK_Q;
    int32_t lz = gz - cz * (int32_t)CHUNK_Q;

    const af_terrain_chunk *c = &ls->chunks[chunk_index(ls, cx, cz)];
    return c->heights[(size_t)lz * CHUNK_V + (size_t)lx];
}

void af_landscape_recompute_normals(af_landscape *ls, int32_t min_cx, int32_t min_cz, int32_t max_cx, int32_t max_cz) {
    if (!ls || !ls->chunks) return;
    if (min_cx < 0) min_cx = 0;
    if (min_cz < 0) min_cz = 0;
    if (max_cx >= (int32_t)ls->chunk_count_x) max_cx = (int32_t)ls->chunk_count_x - 1;
    if (max_cz >= (int32_t)ls->chunk_count_z) max_cz = (int32_t)ls->chunk_count_z - 1;

    float two_dx = 2.0f * ls->vertex_spacing;

    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
            af_terrain_chunk *c = &ls->chunks[chunk_index(ls, cx, cz)];
            for (int32_t vz = 0; vz < (int32_t)CHUNK_V; ++vz) {
                int32_t gz = cz * (int32_t)CHUNK_Q + vz;
                for (int32_t vx = 0; vx < (int32_t)CHUNK_V; ++vx) {
                    int32_t gx = cx * (int32_t)CHUNK_Q + vx;

                    float h_left = get_vertex_height_global(ls, gx - 1, gz);
                    float h_right = get_vertex_height_global(ls, gx + 1, gz);
                    float h_down = get_vertex_height_global(ls, gx, gz - 1);
                    float h_up = get_vertex_height_global(ls, gx, gz + 1);

                    float dx = h_right - h_left;
                    float dz = h_up - h_down;

                    af_vec3 n = {-dx, two_dx, -dz};
                    c->normals[(size_t)vz * CHUNK_V + (size_t)vx] = af_vec3_normalize(n);
                }
            }
        }
    }
}

static float compute_falloff(float dist, float radius, af_brush_falloff falloff) {
    float t = clamp_f(dist / radius, 0.0f, 1.0f);
    switch (falloff) {
        case AF_FALLOFF_FLAT:
            return 1.0f;
        case AF_FALLOFF_LINEAR:
            return 1.0f - t;
        case AF_FALLOFF_SPHERICAL:
            return sqrtf(fmaxf(0.0f, 1.0f - t * t));
        case AF_FALLOFF_SMOOTH:
        default:
            return (1.0f - t) * (1.0f - t) * (3.0f - 2.0f * (1.0f - t));
    }
}

static void update_chunk_bounds(af_landscape *ls, size_t chunk_idx) {
    af_terrain_chunk *c = &ls->chunks[chunk_idx];
    float min_y = c->heights[0];
    float max_y = c->heights[0];
    for (size_t i = 1; i < CHUNK_V * CHUNK_V; ++i) {
        if (c->heights[i] < min_y) min_y = c->heights[i];
        if (c->heights[i] > max_y) max_y = c->heights[i];
    }
    c->bounds_min.y = min_y;
    c->bounds_max.y = max_y;
    c->dirty_mesh = true;
    ls->modification_version++;
}

bool af_landscape_sculpt_raise_lower(af_landscape *ls, af_vec3 center, float radius, float delta, af_brush_falloff falloff) {
    if (!ls || radius <= 0.001f || fabsf(delta) <= 1e-6f) return false;

    int32_t min_cx = (int32_t)floorf(((center.x - radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cx = (int32_t)ceilf(((center.x + radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t min_cz = (int32_t)floorf(((center.z - radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cz = (int32_t)ceilf(((center.z + radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));

    if (min_cx < 0) min_cx = 0;
    if (min_cz < 0) min_cz = 0;
    if (max_cx >= (int32_t)ls->chunk_count_x) max_cx = (int32_t)ls->chunk_count_x - 1;
    if (max_cz >= (int32_t)ls->chunk_count_z) max_cz = (int32_t)ls->chunk_count_z - 1;

    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
            size_t c_idx = chunk_index(ls, (int32_t)cx, (int32_t)cz);
            af_terrain_chunk *c = &ls->chunks[c_idx];
            bool changed = false;

            for (int32_t vz = 0; vz < (int32_t)CHUNK_V; ++vz) {
                float wz = ls->origin.z + (float)(cz * (int32_t)CHUNK_Q + vz) * ls->vertex_spacing;
                float dz = wz - center.z;
                for (int32_t vx = 0; vx < (int32_t)CHUNK_V; ++vx) {
                    float wx = ls->origin.x + (float)(cx * (int32_t)CHUNK_Q + vx) * ls->vertex_spacing;
                    float dx = wx - center.x;
                    float dist = sqrtf(dx * dx + dz * dz);
                    if (dist <= radius) {
                        float w = compute_falloff(dist, radius, falloff);
                        c->heights[(size_t)vz * CHUNK_V + (size_t)vx] += delta * w;
                        changed = true;
                    }
                }
            }
            if (changed) update_chunk_bounds(ls, c_idx);
        }
    }
    af_landscape_recompute_normals(ls, min_cx - 1, min_cz - 1, max_cx + 1, max_cz + 1);
    return true;
}

bool af_landscape_sculpt_smooth(af_landscape *ls, af_vec3 center, float radius, float strength, af_brush_falloff falloff) {
    if (!ls || radius <= 0.001f || strength <= 0.0f) return false;
    strength = clamp_f(strength, 0.0f, 1.0f);

    int32_t min_cx = (int32_t)floorf(((center.x - radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cx = (int32_t)ceilf(((center.x + radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t min_cz = (int32_t)floorf(((center.z - radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cz = (int32_t)ceilf(((center.z + radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));

    if (min_cx < 0) min_cx = 0;
    if (min_cz < 0) min_cz = 0;
    if (max_cx >= (int32_t)ls->chunk_count_x) max_cx = (int32_t)ls->chunk_count_x - 1;
    if (max_cz >= (int32_t)ls->chunk_count_z) max_cz = (int32_t)ls->chunk_count_z - 1;

    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
            size_t c_idx = chunk_index(ls, (int32_t)cx, (int32_t)cz);
            af_terrain_chunk *c = &ls->chunks[c_idx];
            bool changed = false;

            for (int32_t vz = 0; vz < (int32_t)CHUNK_V; ++vz) {
                int32_t gz = cz * (int32_t)CHUNK_Q + vz;
                float wz = ls->origin.z + (float)gz * ls->vertex_spacing;
                float dz = wz - center.z;

                for (int32_t vx = 0; vx < (int32_t)CHUNK_V; ++vx) {
                    int32_t gx = cx * (int32_t)CHUNK_Q + vx;
                    float wx = ls->origin.x + (float)gx * ls->vertex_spacing;
                    float dx = wx - center.x;
                    float dist = sqrtf(dx * dx + dz * dz);
                    if (dist <= radius) {
                        float w = compute_falloff(dist, radius, falloff) * strength;
                        float avg = (get_vertex_height_global(ls, gx - 1, gz) +
                                     get_vertex_height_global(ls, gx + 1, gz) +
                                     get_vertex_height_global(ls, gx, gz - 1) +
                                     get_vertex_height_global(ls, gx, gz + 1)) * 0.25f;
                        c->heights[(size_t)vz * CHUNK_V + (size_t)vx] += (avg - c->heights[(size_t)vz * CHUNK_V + (size_t)vx]) * w;
                        changed = true;
                    }
                }
            }
            if (changed) update_chunk_bounds(ls, c_idx);
        }
    }
    af_landscape_recompute_normals(ls, min_cx - 1, min_cz - 1, max_cx + 1, max_cz + 1);
    return true;
}

bool af_landscape_sculpt_flatten(af_landscape *ls, af_vec3 center, float radius, float target_height, float strength, af_brush_falloff falloff) {
    if (!ls || radius <= 0.001f) return false;
    strength = clamp_f(strength, 0.01f, 1.0f);

    int32_t min_cx = (int32_t)floorf(((center.x - radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cx = (int32_t)ceilf(((center.x + radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t min_cz = (int32_t)floorf(((center.z - radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cz = (int32_t)ceilf(((center.z + radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));

    if (min_cx < 0) min_cx = 0;
    if (min_cz < 0) min_cz = 0;
    if (max_cx >= (int32_t)ls->chunk_count_x) max_cx = (int32_t)ls->chunk_count_x - 1;
    if (max_cz >= (int32_t)ls->chunk_count_z) max_cz = (int32_t)ls->chunk_count_z - 1;

    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
            size_t c_idx = chunk_index(ls, (int32_t)cx, (int32_t)cz);
            af_terrain_chunk *c = &ls->chunks[c_idx];
            bool changed = false;

            for (int32_t vz = 0; vz < (int32_t)CHUNK_V; ++vz) {
                float wz = ls->origin.z + (float)(cz * (int32_t)CHUNK_Q + vz) * ls->vertex_spacing;
                float dz = wz - center.z;
                for (int32_t vx = 0; vx < (int32_t)CHUNK_V; ++vx) {
                    float wx = ls->origin.x + (float)(cx * (int32_t)CHUNK_Q + vx) * ls->vertex_spacing;
                    float dx = wx - center.x;
                    float dist = sqrtf(dx * dx + dz * dz);
                    if (dist <= radius) {
                        float w = compute_falloff(dist, radius, falloff) * strength;
                        c->heights[(size_t)vz * CHUNK_V + (size_t)vx] += (target_height - c->heights[(size_t)vz * CHUNK_V + (size_t)vx]) * w;
                        changed = true;
                    }
                }
            }
            if (changed) update_chunk_bounds(ls, c_idx);
        }
    }
    af_landscape_recompute_normals(ls, min_cx - 1, min_cz - 1, max_cx + 1, max_cz + 1);
    return true;
}

bool af_landscape_paint_layer(af_landscape *ls, af_vec3 center, float radius, size_t layer_idx, float strength, af_brush_falloff falloff) {
    if (!ls || radius <= 0.001f || layer_idx >= AF_LANDSCAPE_MAX_LAYERS) return false;
    strength = clamp_f(strength, 0.01f, 1.0f);

    int32_t min_cx = (int32_t)floorf(((center.x - radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cx = (int32_t)ceilf(((center.x + radius) - ls->origin.x) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t min_cz = (int32_t)floorf(((center.z - radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));
    int32_t max_cz = (int32_t)ceilf(((center.z + radius) - ls->origin.z) / (ls->vertex_spacing * (float)CHUNK_Q));

    if (min_cx < 0) min_cx = 0;
    if (min_cz < 0) min_cz = 0;
    if (max_cx >= (int32_t)ls->chunk_count_x) max_cx = (int32_t)ls->chunk_count_x - 1;
    if (max_cz >= (int32_t)ls->chunk_count_z) max_cz = (int32_t)ls->chunk_count_z - 1;

    for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
        for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
            size_t c_idx = chunk_index(ls, (int32_t)cx, (int32_t)cz);
            af_terrain_chunk *c = &ls->chunks[c_idx];
            bool changed = false;

            for (int32_t vz = 0; vz < (int32_t)CHUNK_V; ++vz) {
                float wz = ls->origin.z + (float)(cz * (int32_t)CHUNK_Q + vz) * ls->vertex_spacing;
                float dz = wz - center.z;
                for (int32_t vx = 0; vx < (int32_t)CHUNK_V; ++vx) {
                    float wx = ls->origin.x + (float)(cx * (int32_t)CHUNK_Q + vx) * ls->vertex_spacing;
                    float dx = wx - center.x;
                    float dist = sqrtf(dx * dx + dz * dz);
                    if (dist <= radius) {
                        float w = compute_falloff(dist, radius, falloff) * strength;
                        size_t b_idx = ((size_t)vz * CHUNK_V + (size_t)vx) * 4;

                        float cur[4];
                        for (size_t k = 0; k < 4; ++k) cur[k] = (float)c->weights[b_idx + (size_t)k] / 255.0f;

                        /* Add to target layer */
                        cur[layer_idx] += w;

                        /* Renormalize */
                        float sum = 0.0f;
                        for (size_t k = 0; k < 4; ++k) sum += cur[k];
                        if (sum > 0.001f) {
                            for (size_t k = 0; k < 4; ++k) {
                                c->weights[b_idx + (size_t)k] = (uint8_t)clamp_f(roundf((cur[k] / sum) * 255.0f), 0.0f, 255.0f);
                            }
                        }
                        changed = true;
                    }
                }
            }
            if (changed) {
                c->dirty_mesh = true;
                ls->modification_version++;
            }
        }
    }
    return true;
}

bool af_landscape_auto_paint_slopes(af_landscape *ls, size_t rock_layer, float min_slope_rad, size_t sand_layer, float max_height) {
    if (!ls || !ls->chunks) return false;
    if (min_slope_rad <= 0.01f) min_slope_rad = 0.5f;

    for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
        af_terrain_chunk *c = &ls->chunks[c_idx];
        for (size_t v = 0; v < CHUNK_V * CHUNK_V; ++v) {
            af_vec3 n = c->normals[v];
            float slope = acosf(clamp_f(n.y, -1.0f, 1.0f));
            float h = c->heights[v];
            size_t b_idx = v * 4;

            float cur[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            if (slope > min_slope_rad && rock_layer < AF_LANDSCAPE_MAX_LAYERS) {
                float rock_blend = clamp_f((slope - min_slope_rad) / 0.35f, 0.0f, 1.0f);
                cur[0] = 1.0f - rock_blend;
                cur[rock_layer] = rock_blend;
            }
            if (h <= max_height && sand_layer < AF_LANDSCAPE_MAX_LAYERS) {
                float sand_blend = clamp_f((max_height - h) / 2.0f, 0.0f, 1.0f);
                cur[0] *= (1.0f - sand_blend);
                cur[sand_layer] = fmaxf(cur[sand_layer], sand_blend);
            }

            float sum = cur[0] + cur[1] + cur[2] + cur[3];
            if (sum > 0.001f) {
                for (size_t k = 0; k < 4; ++k)
                    c->weights[b_idx + (size_t)k] = (uint8_t)clamp_f(roundf((cur[k] / sum) * 255.0f), 0.0f, 255.0f);
            }
        }
        c->dirty_mesh = true;
    }
    ls->modification_version++;
    return true;
}

bool af_landscape_generate_procedural_hills(af_landscape *ls, float scale, float amplitude, uint32_t seed) {
    if (!ls || !ls->chunks) return false;
    if (amplitude <= 0.001f) amplitude = 8.0f;

    /* Support both wavelength in meters (e.g. 45.0m) and frequency (e.g. 0.02) */
    float inv_scale;
    if (scale <= 0.0001f) {
        inv_scale = 1.0f / 45.0f;
    } else if (scale < 0.2f) {
        inv_scale = scale;
    } else {
        inv_scale = 1.0f / scale;
    }

    float seed_offset_x = (float)(hash_u32(seed) % 10000);
    float seed_offset_z = (float)(hash_u32(seed + 1337u) % 10000);

    for (uint32_t cz = 0; cz < ls->chunk_count_z; ++cz) {
        for (uint32_t cx = 0; cx < ls->chunk_count_x; ++cx) {
            size_t c_idx = chunk_index(ls, (int32_t)cx, (int32_t)cz);
            af_terrain_chunk *c = &ls->chunks[c_idx];

            for (uint32_t vz = 0; vz < CHUNK_V; ++vz) {
                float wz = ls->origin.z + (float)(cz * CHUNK_Q + vz) * ls->vertex_spacing;
                for (uint32_t vx = 0; vx < CHUNK_V; ++vx) {
                    float wx = ls->origin.x + (float)(cx * CHUNK_Q + vx) * ls->vertex_spacing;

                    float nx = (wx + seed_offset_x) * inv_scale;
                    float nz = (wz + seed_offset_z) * inv_scale;

                    /* 4-octave fractal noise normalized smoothly to [0, amplitude] */
                    float n = fbm2d(nx, nz, 4);
                    float elevation = (n * 0.5f + 0.5f) * amplitude;
                    c->heights[(size_t)vz * CHUNK_V + (size_t)vx] = elevation;
                }
            }
            update_chunk_bounds(ls, c_idx);
        }
    }
    af_landscape_recompute_normals(ls, 0, 0, (int32_t)ls->chunk_count_x - 1, (int32_t)ls->chunk_count_z - 1);
    af_landscape_auto_paint_slopes(ls, 2, 0.65f, 3, 0.8f);
    return true;
}

bool af_landscape_import_heightmap(af_landscape *ls, const float *heights, uint32_t width, uint32_t height, float min_h, float max_h) {
    if (!ls || !heights || width < 2 || height < 2) return false;

    uint32_t total_x = ls->chunk_count_x * CHUNK_Q + 1;
    uint32_t total_z = ls->chunk_count_z * CHUNK_Q + 1;

    for (uint32_t cz = 0; cz < ls->chunk_count_z; ++cz) {
        for (uint32_t cx = 0; cx < ls->chunk_count_x; ++cx) {
            size_t c_idx = chunk_index(ls, (int32_t)cx, (int32_t)cz);
            af_terrain_chunk *c = &ls->chunks[c_idx];

            for (uint32_t vz = 0; vz < CHUNK_V; ++vz) {
                uint32_t gz = cz * CHUNK_Q + vz;
                float v_norm = (float)gz / (float)(total_z - 1);
                float img_y = v_norm * (float)(height - 1);
                uint32_t iy0 = (uint32_t)floorf(img_y);
                uint32_t iy1 = iy0 < height - 1 ? iy0 + 1 : iy0;
                float ty = img_y - (float)iy0;

                for (uint32_t vx = 0; vx < CHUNK_V; ++vx) {
                    uint32_t gx = cx * CHUNK_Q + vx;
                    float u_norm = (float)gx / (float)(total_x - 1);
                    float img_x = u_norm * (float)(width - 1);
                    uint32_t ix0 = (uint32_t)floorf(img_x);
                    uint32_t ix1 = ix0 < width - 1 ? ix0 + 1 : ix0;
                    float tx = img_x - (float)ix0;

                    float h00 = heights[iy0 * width + ix0];
                    float h10 = heights[iy0 * width + ix1];
                    float h01 = heights[iy1 * width + ix0];
                    float h11 = heights[iy1 * width + ix1];

                    float h0 = h00 + tx * (h10 - h00);
                    float h1 = h01 + tx * (h11 - h01);
                    float h_raw = h0 + ty * (h1 - h0);

                    c->heights[(size_t)vz * CHUNK_V + (size_t)vx] = min_h + h_raw * (max_h - min_h);
                }
            }
            update_chunk_bounds(ls, c_idx);
        }
    }
    af_landscape_recompute_normals(ls, 0, 0, (int32_t)ls->chunk_count_x - 1, (int32_t)ls->chunk_count_z - 1);
    return true;
}

bool af_landscape_export_heightmap(const af_landscape *ls, float *out_heights, uint32_t width, uint32_t height) {
    if (!ls || !out_heights || width == 0 || height == 0) return false;

    float total_w = (float)(ls->chunk_count_x * CHUNK_Q) * ls->vertex_spacing;
    float total_d = (float)(ls->chunk_count_z * CHUNK_Q) * ls->vertex_spacing;

    for (uint32_t y = 0; y < height; ++y) {
        float v = (float)y / (float)(height > 1 ? height - 1 : 1);
        float wz = ls->origin.z + v * total_d;
        for (uint32_t x = 0; x < width; ++x) {
            float u = (float)x / (float)(width > 1 ? width - 1 : 1);
            float wx = ls->origin.x + u * total_w;
            out_heights[y * width + x] = af_landscape_get_height(ls, wx, wz);
        }
    }
    return true;
}

/* Terrain raycast */
bool af_landscape_raycast(const af_landscape *ls, af_vec3 ray_origin, af_vec3 ray_dir, af_vec3 *hit_point, af_vec3 *hit_normal) {
    if (!ls || !ls->chunks) return false;
    float dir_len = af_vec3_length(ray_dir);
    if (dir_len <= 1e-6f) return false;
    af_vec3 dir = af_vec3_scale(ray_dir, 1.0f / dir_len);

    float total_w = (float)(ls->chunk_count_x * CHUNK_Q) * ls->vertex_spacing;
    float total_d = (float)(ls->chunk_count_z * CHUNK_Q) * ls->vertex_spacing;

    /* AABB check against full terrain bounding box */
    float t_min = 0.0f;
    float t_max = 2000.0f;
    float step = ls->vertex_spacing * 0.5f;

    /* If ray is looking away or too high, advance to terrain bounds */
    float t = t_min;
    float prev_t = t;
    float prev_diff = ray_origin.y + dir.y * t - af_landscape_get_height(ls, ray_origin.x + dir.x * t, ray_origin.z + dir.z * t);

    (void)total_w; (void)total_d;
    for (int i = 0; i < 1000 && t < t_max; ++i) {
        t += step;
        af_vec3 p = af_vec3_add(ray_origin, af_vec3_scale(dir, t));
        if (p.x < ls->origin.x || p.x > ls->origin.x + total_w ||
            p.z < ls->origin.z || p.z > ls->origin.z + total_d) {
            /* Outside horizontal bounds */
            if (t > 10.0f && (p.y < -100.0f || p.y > 1000.0f)) break;
            prev_t = t;
            continue;
        }

        float th = af_landscape_get_height(ls, p.x, p.z);
        float diff = p.y - th;

        if (diff <= 0.0f && prev_diff > 0.0f) {
            /* Ray crossed terrain surface between prev_t and t! Refine with bisection */
            float t0 = prev_t;
            float t1 = t;
            for (int b = 0; b < 12; ++b) {
                float tm = (t0 + t1) * 0.5f;
                af_vec3 pm = af_vec3_add(ray_origin, af_vec3_scale(dir, tm));
                float hm = af_landscape_get_height(ls, pm.x, pm.z);
                if (pm.y <= hm) t1 = tm;
                else t0 = tm;
            }
            float hit_t = (t0 + t1) * 0.5f;
            af_vec3 hit = af_vec3_add(ray_origin, af_vec3_scale(dir, hit_t));
            if (hit_point) *hit_point = hit;
            if (hit_normal) af_landscape_get_normal(ls, hit.x, hit.z, hit_normal);
            return true;
        }
        prev_t = t;
        prev_diff = diff;
    }
    return false;
}

size_t af_landscape_chunk_vertex_count(void) {
    return CHUNK_V * CHUNK_V;
}

size_t af_landscape_chunk_index_count(void) {
    return CHUNK_Q * CHUNK_Q * 6;
}

bool af_landscape_build_chunk_mesh(const af_landscape *ls, size_t chunk_idx, af_vertex *out_vertices, uint32_t *out_indices) {
    if (!ls || !ls->chunks || chunk_idx >= ls->chunk_count || !out_vertices || !out_indices)
        return false;

    const af_terrain_chunk *c = &ls->chunks[chunk_idx];
    float uv_scale = 0.25f; /* 1 UV tile per 4 meters */

    /* Build vertices */
    for (uint32_t vz = 0; vz < CHUNK_V; ++vz) {
        float wz = ls->origin.z + (float)(c->chunk_z * (int32_t)CHUNK_Q + (int32_t)vz) * ls->vertex_spacing;
        for (uint32_t vx = 0; vx < CHUNK_V; ++vx) {
            float wx = ls->origin.x + (float)(c->chunk_x * (int32_t)CHUNK_Q + (int32_t)vx) * ls->vertex_spacing;
            size_t idx = vz * CHUNK_V + vx;

            af_vertex *v = &out_vertices[idx];
            v->position = (af_vec3){wx, c->heights[idx], wz};
            v->normal = c->normals[idx];
            v->uv = (af_vec2){wx * uv_scale, wz * uv_scale};

            /* Multi-layer blended color */
            float w0 = (float)c->weights[idx * 4 + 0] / 255.0f;
            float w1 = (float)c->weights[idx * 4 + 1] / 255.0f;
            float w2 = (float)c->weights[idx * 4 + 2] / 255.0f;
            float w3 = (float)c->weights[idx * 4 + 3] / 255.0f;

            af_color c0 = ls->layers[0].color;
            af_color c1 = ls->layers[1].color;
            af_color c2 = ls->layers[2].color;
            af_color c3 = ls->layers[3].color;

            v->color = (af_color){
                c0.r * w0 + c1.r * w1 + c2.r * w2 + c3.r * w3,
                c0.g * w0 + c1.g * w1 + c2.g * w2 + c3.g * w3,
                c0.b * w0 + c1.b * w1 + c2.b * w2 + c3.b * w3,
                1.0f
            };
            memset(v->joints, 0, sizeof(v->joints));
            v->weights = (af_vec4){0, 0, 0, 0};
        }
    }

    /* Build indices (two triangles per quad) */
    size_t i_out = 0;
    for (uint32_t qz = 0; qz < CHUNK_Q; ++qz) {
        for (uint32_t qx = 0; qx < CHUNK_Q; ++qx) {
            uint32_t i00 = qz * CHUNK_V + qx;
            uint32_t i10 = qz * CHUNK_V + (qx + 1);
            uint32_t i01 = (qz + 1) * CHUNK_V + qx;
            uint32_t i11 = (qz + 1) * CHUNK_V + (qx + 1);

            out_indices[i_out++] = i00;
            out_indices[i_out++] = i01;
            out_indices[i_out++] = i10;

            out_indices[i_out++] = i10;
            out_indices[i_out++] = i01;
            out_indices[i_out++] = i11;
        }
    }
    return true;
}

/* Foliage Layer Management */
int af_landscape_add_foliage_layer(af_landscape *ls, const char *name, const char *mesh) {
    if (!ls || ls->foliage_layer_count >= AF_LANDSCAPE_MAX_FOLIAGE_LAYERS)
        return -1;
    size_t idx = ls->foliage_layer_count++;
    ls->foliage_layers[idx] = af_foliage_layer_default(name, mesh);
    ls->modification_version++;
    return (int)idx;
}

bool af_landscape_remove_foliage_layer(af_landscape *ls, size_t layer_idx) {
    if (!ls || layer_idx >= ls->foliage_layer_count) return false;

    /* Free all instances in this layer across all chunks */
    for (size_t c = 0; c < ls->chunk_count; ++c) {
        free(ls->chunks[c].foliage[layer_idx].instances);
        for (size_t l = layer_idx; l + 1 < ls->foliage_layer_count; ++l) {
            ls->chunks[c].foliage[l] = ls->chunks[c].foliage[l + 1];
        }
        memset(&ls->chunks[c].foliage[ls->foliage_layer_count - 1], 0, sizeof(af_foliage_chunk));
    }

    for (size_t l = layer_idx; l + 1 < ls->foliage_layer_count; ++l) {
        ls->foliage_layers[l] = ls->foliage_layers[l + 1];
    }
    ls->foliage_layer_count--;
    ls->modification_version++;
    return true;
}

bool af_landscape_set_foliage_visible(af_landscape *ls, size_t layer_idx, bool visible) {
    if (!ls || layer_idx >= ls->foliage_layer_count) return false;
    ls->foliage_layers[layer_idx].visible = visible;
    ls->modification_version++;
    return true;
}

static bool test_instance_rules(const af_landscape *ls, const af_foliage_layer *fl,
                                af_vec3 pos, af_vec3 normal, float *weights) {
    (void)ls;
    const af_placement_rules *r = &fl->rules;

    /* Height restriction */
    if (pos.y < r->height_min || pos.y > r->height_max) return false;

    /* Slope restriction */
    float slope = acosf(clamp_f(normal.y, -1.0f, 1.0f));
    if (slope < r->slope_min || slope > r->slope_max) return false;

    /* Paint mask check */
    if (r->paint_mask_layer >= 0 && r->paint_mask_layer < (int)AF_LANDSCAPE_MAX_LAYERS) {
        if (weights[r->paint_mask_layer] < 0.25f) return false;
    }
    return true;
}

static bool add_instance_to_chunk(af_terrain_chunk *c, size_t layer_idx, af_foliage_instance inst, float min_dist) {
    af_foliage_chunk *fc = &c->foliage[layer_idx];

    /* Collision avoidance spacing check */
    if (min_dist > 0.05f && fc->count > 0) {
        float min_dist_sq = min_dist * min_dist;
        for (size_t i = 0; i < fc->count; ++i) {
            af_vec3 diff = af_vec3_sub(fc->instances[i].transform.position, inst.transform.position);
            if (af_vec3_dot(diff, diff) < min_dist_sq) return false;
        }
    }

    if (fc->count >= fc->capacity) {
        size_t new_cap = fc->capacity == 0 ? 32 : fc->capacity * 2;
        af_foliage_instance *next = realloc(fc->instances, new_cap * sizeof(*next));
        if (!next) return false;
        fc->instances = next;
        fc->capacity = new_cap;
    }
    fc->instances[fc->count++] = inst;

    /* Update foliage chunk bounds */
    if (fc->count == 1) {
        fc->bounds_min = inst.transform.position;
        fc->bounds_max = inst.transform.position;
    } else {
        fc->bounds_min.x = fminf(fc->bounds_min.x, inst.transform.position.x);
        fc->bounds_min.y = fminf(fc->bounds_min.y, inst.transform.position.y);
        fc->bounds_min.z = fminf(fc->bounds_min.z, inst.transform.position.z);
        fc->bounds_max.x = fmaxf(fc->bounds_max.x, inst.transform.position.x);
        fc->bounds_max.y = fmaxf(fc->bounds_max.y, inst.transform.position.y);
        fc->bounds_max.z = fmaxf(fc->bounds_max.z, inst.transform.position.z);
    }
    return true;
}

bool af_landscape_paint_foliage(af_landscape *ls, size_t layer_idx, af_vec3 center, float radius, float strength, uint32_t seed) {
    if (!ls || layer_idx >= ls->foliage_layer_count || radius <= 0.001f || strength <= 0.0f)
        return false;

    af_foliage_layer *fl = &ls->foliage_layers[layer_idx];
    const af_placement_rules *r = &fl->rules;

    uint32_t rng = seed ^ 0x85ebca6bu;
    float area = AF_PI * radius * radius;
    int samples = (int)ceilf(area * 0.1f * r->density * strength);
    if (samples < 1) samples = 1;
    if (samples > 200) samples = 200;

    size_t added = 0;
    for (int s = 0; s < samples; ++s) {
        float u1 = pseudo_rand_float(&rng);
        float u2 = pseudo_rand_float(&rng);
        float r_dist = radius * sqrtf(u1);
        float angle = u2 * AF_PI * 2.0f;

        float wx = center.x + r_dist * cosf(angle);
        float wz = center.z + r_dist * sinf(angle);

        int32_t cx, cz;
        if (!get_local_coords(ls, wx, wz, &cx, &cz, NULL, NULL)) continue;

        float wy = af_landscape_get_height(ls, wx, wz);
        af_vec3 norm;
        af_landscape_get_normal(ls, wx, wz, &norm);

        float weights[4];
        af_landscape_get_layer_weights(ls, wx, wz, weights);

        af_vec3 pos = {wx, wy + r->offset_y, wz};
        if (!test_instance_rules(ls, fl, pos, norm, weights)) continue;

        /* Rotation */
        float yaw = 0.0f;
        if (r->random_yaw) {
            float y_span = r->yaw_max - r->yaw_min;
            yaw = r->yaw_min + pseudo_rand_float(&rng) * y_span;
        }

        float pitch = 0.0f, roll = 0.0f;
        if (r->random_pitch_roll && r->max_tilt > 0.001f) {
            pitch = (pseudo_rand_float(&rng) * 2.0f - 1.0f) * r->max_tilt;
            roll = (pseudo_rand_float(&rng) * 2.0f - 1.0f) * r->max_tilt;
        }

        /* Surface normal alignment */
        if (r->align_to_normal > 0.01f) {
            float tilt_x = -norm.z * r->align_to_normal;
            float tilt_z = norm.x * r->align_to_normal;
            pitch += tilt_x;
            roll += tilt_z;
        }

        /* Scale */
        af_vec3 scale;
        if (r->uniform_scale) {
            float s_val = r->scale_min.y + pseudo_rand_float(&rng) * (r->scale_max.y - r->scale_min.y);
            scale = (af_vec3){s_val, s_val, s_val};
        } else {
            scale.x = r->scale_min.x + pseudo_rand_float(&rng) * (r->scale_max.x - r->scale_min.x);
            scale.y = r->scale_min.y + pseudo_rand_float(&rng) * (r->scale_max.y - r->scale_min.y);
            scale.z = r->scale_min.z + pseudo_rand_float(&rng) * (r->scale_max.z - r->scale_min.z);
        }

        af_foliage_instance inst;
        inst.transform.position = pos;
        inst.transform.rotation = (af_vec3){pitch, yaw, roll};
        inst.transform.scale = scale;

        float min_dist = r->collision_avoidance ? r->min_spacing : 0.0f;
        af_terrain_chunk *c = &ls->chunks[chunk_index(ls, cx, cz)];
        if (add_instance_to_chunk(c, layer_idx, inst, min_dist)) {
            added++;
        }
    }

    fl->total_instances += added;
    ls->total_foliage_instances += added;
    ls->modification_version++;
    return added > 0;
}

bool af_landscape_erase_foliage(af_landscape *ls, int layer_idx, af_vec3 center, float radius) {
    if (!ls || radius <= 0.001f) return false;
    float r_sq = radius * radius;
    size_t removed = 0;

    for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
        af_terrain_chunk *c = &ls->chunks[c_idx];

        size_t l_start = layer_idx >= 0 ? (size_t)layer_idx : 0;
        size_t l_end = layer_idx >= 0 ? (size_t)layer_idx + 1 : ls->foliage_layer_count;

        for (size_t l = l_start; l < l_end && l < AF_LANDSCAPE_MAX_FOLIAGE_LAYERS; ++l) {
            af_foliage_chunk *fc = &c->foliage[l];
            size_t write_idx = 0;
            for (size_t i = 0; i < fc->count; ++i) {
                af_vec3 diff = af_vec3_sub(fc->instances[i].transform.position, center);
                diff.y = 0.0f; /* 2D radius test */
                if (af_vec3_dot(diff, diff) <= r_sq) {
                    removed++;
                    if (ls->foliage_layers[l].total_instances > 0)
                        ls->foliage_layers[l].total_instances--;
                } else {
                    if (write_idx != i) fc->instances[write_idx] = fc->instances[i];
                    write_idx++;
                }
            }
            fc->count = write_idx;
        }
    }
    if (removed > 0) {
        ls->total_foliage_instances -= (ls->total_foliage_instances >= removed ? removed : ls->total_foliage_instances);
        ls->modification_version++;
    }
    return removed > 0;
}

void af_landscape_clear_foliage(af_landscape *ls, int layer_idx) {
    if (!ls) return;
    size_t l_start = layer_idx >= 0 ? (size_t)layer_idx : 0;
    size_t l_end = layer_idx >= 0 ? (size_t)layer_idx + 1 : ls->foliage_layer_count;

    for (size_t l = l_start; l < l_end && l < ls->foliage_layer_count; ++l) {
        for (size_t c = 0; c < ls->chunk_count; ++c) {
            ls->chunks[c].foliage[l].count = 0;
        }
        ls->total_foliage_instances -= (ls->total_foliage_instances >= ls->foliage_layers[l].total_instances
                                        ? ls->foliage_layers[l].total_instances : ls->total_foliage_instances);
        ls->foliage_layers[l].total_instances = 0;
    }
    ls->modification_version++;
}

bool af_landscape_populate_procedural_foliage(af_landscape *ls, size_t layer_idx, uint32_t seed) {
    if (!ls || layer_idx >= ls->foliage_layer_count) return false;

    af_landscape_clear_foliage(ls, (int)layer_idx);
    af_foliage_layer *fl = &ls->foliage_layers[layer_idx];
    const af_placement_rules *r = &fl->rules;

    uint32_t rng = seed ^ 0x27d4eb2du;
    float chunk_size = (float)CHUNK_Q * ls->vertex_spacing;
    float chunk_area = chunk_size * chunk_size;
    int target_chunk_samples = (int)ceilf(chunk_area * 0.05f * r->density);
    if (target_chunk_samples < 2) target_chunk_samples = 2;
    if (target_chunk_samples > 300) target_chunk_samples = 300;

    size_t total_added = 0;
    for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
        af_terrain_chunk *c = &ls->chunks[c_idx];

        for (int s = 0; s < target_chunk_samples; ++s) {
            float u = pseudo_rand_float(&rng);
            float v = pseudo_rand_float(&rng);

            float wx = c->bounds_min.x + u * chunk_size;
            float wz = c->bounds_min.z + v * chunk_size;
            float wy = af_landscape_get_height(ls, wx, wz);

            af_vec3 norm;
            af_landscape_get_normal(ls, wx, wz, &norm);

            float weights[4];
            af_landscape_get_layer_weights(ls, wx, wz, weights);

            af_vec3 pos = {wx, wy + r->offset_y, wz};
            if (!test_instance_rules(ls, fl, pos, norm, weights)) continue;

            float yaw = 0.0f;
            if (r->random_yaw) {
                yaw = r->yaw_min + pseudo_rand_float(&rng) * (r->yaw_max - r->yaw_min);
            }

            float pitch = 0.0f, roll = 0.0f;
            if (r->random_pitch_roll && r->max_tilt > 0.001f) {
                pitch = (pseudo_rand_float(&rng) * 2.0f - 1.0f) * r->max_tilt;
                roll = (pseudo_rand_float(&rng) * 2.0f - 1.0f) * r->max_tilt;
            }

            if (r->align_to_normal > 0.01f) {
                pitch += -norm.z * r->align_to_normal;
                roll += norm.x * r->align_to_normal;
            }

            af_vec3 scale;
            if (r->uniform_scale) {
                float s_val = r->scale_min.y + pseudo_rand_float(&rng) * (r->scale_max.y - r->scale_min.y);
                scale = (af_vec3){s_val, s_val, s_val};
            } else {
                scale.x = r->scale_min.x + pseudo_rand_float(&rng) * (r->scale_max.x - r->scale_min.x);
                scale.y = r->scale_min.y + pseudo_rand_float(&rng) * (r->scale_max.y - r->scale_min.y);
                scale.z = r->scale_min.z + pseudo_rand_float(&rng) * (r->scale_max.z - r->scale_min.z);
            }

            af_foliage_instance inst;
            inst.transform.position = pos;
            inst.transform.rotation = (af_vec3){pitch, yaw, roll};
            inst.transform.scale = scale;

            float min_dist = r->collision_avoidance ? r->min_spacing : 0.0f;
            if (add_instance_to_chunk(c, layer_idx, inst, min_dist)) {
                total_added++;
            }
        }
    }

    fl->total_instances = total_added;
    ls->total_foliage_instances += total_added;
    ls->modification_version++;
    return total_added > 0;
}

/* Frustum & Distance Culling query */
size_t af_landscape_query_foliage_batches(const af_landscape *ls, const af_camera *camera, af_rect viewport,
                                          af_foliage_batch *out_batches, size_t max_batches,
                                          af_foliage_instance *instance_pool, size_t pool_capacity,
                                          size_t *out_total_instances) {
    if (!ls || !ls->chunks || !out_batches || max_batches == 0) return 0;
    (void)viewport;

    size_t batch_count = 0;
    size_t total_instances = 0;
    size_t pool_used = 0;

    af_vec3 cam_pos = camera ? camera->position : (af_vec3){0, 0, 0};

    for (size_t l = 0; l < ls->foliage_layer_count && batch_count < max_batches; ++l) {
        const af_foliage_layer *fl = &ls->foliage_layers[l];
        if (!fl->visible || fl->total_instances == 0 || fl->mesh[0] == '\0')
            continue;

        float max_dist_sq = fl->max_distance * fl->max_distance;
        size_t batch_start_pool = pool_used;
        size_t layer_instance_count = 0;

        for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
            const af_terrain_chunk *c = &ls->chunks[c_idx];
            const af_foliage_chunk *fc = &c->foliage[l];
            if (fc->count == 0) continue;

            /* Distance culling based on chunk center */
            af_vec3 chunk_center = {
                (c->bounds_min.x + c->bounds_max.x) * 0.5f,
                (c->bounds_min.y + c->bounds_max.y) * 0.5f,
                (c->bounds_min.z + c->bounds_max.z) * 0.5f
            };
            af_vec3 to_cam = af_vec3_sub(chunk_center, cam_pos);
            if (af_vec3_dot(to_cam, to_cam) > max_dist_sq) continue;

            /* Copy visible instances into instance pool */
            for (size_t i = 0; i < fc->count && pool_used < pool_capacity; ++i) {
                if (instance_pool) instance_pool[pool_used++] = fc->instances[i];
                layer_instance_count++;
            }
        }

        if (layer_instance_count > 0) {
            af_foliage_batch *b = &out_batches[batch_count++];
            b->layer_index = l;
            b->mesh_name = fl->mesh;
            b->material = fl->material;
            b->cast_shadows = fl->cast_shadows;
            b->instance_count = layer_instance_count;
            b->instances = instance_pool ? &instance_pool[batch_start_pool] : NULL;
            total_instances += layer_instance_count;
        }
    }

    if (out_total_instances) *out_total_instances = total_instances;
    return batch_count;
}

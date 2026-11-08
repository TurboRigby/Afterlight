/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_LANDSCAPE_H
#define AFTERLIGHT_LANDSCAPE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <afterlight/math.h>
#include <afterlight/mesh.h>
#include <afterlight/material.h>
#include <afterlight/camera.h>
#include <afterlight/overlay.h>
#include <afterlight/asset.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_LANDSCAPE_CHUNK_QUADS 16u
#define AF_LANDSCAPE_CHUNK_VERTICES (AF_LANDSCAPE_CHUNK_QUADS + 1u)
#define AF_LANDSCAPE_MAX_LAYERS 4u
#define AF_LANDSCAPE_MAX_FOLIAGE_LAYERS 16u
#define AF_LANDSCAPE_NAME_CAPACITY 32u

typedef enum af_brush_mode {
    AF_BRUSH_SCULPT_RAISE,
    AF_BRUSH_SCULPT_LOWER,
    AF_BRUSH_SCULPT_SMOOTH,
    AF_BRUSH_SCULPT_FLATTEN,
    AF_BRUSH_PAINT_LAYER,
    AF_BRUSH_PAINT_FOLIAGE,
    AF_BRUSH_ERASE_FOLIAGE
} af_brush_mode;

typedef enum af_brush_falloff {
    AF_FALLOFF_SMOOTH,
    AF_FALLOFF_LINEAR,
    AF_FALLOFF_SPHERICAL,
    AF_FALLOFF_FLAT
} af_brush_falloff;

typedef struct af_terrain_layer {
    char name[AF_LANDSCAPE_NAME_CAPACITY];
    char diffuse_texture[AF_ASSET_PATH_CAPACITY];
    char normal_texture[AF_ASSET_PATH_CAPACITY];
    af_color color;
    float roughness;
    float metalness;
    float uv_scale;
} af_terrain_layer;

typedef struct af_placement_rules {
    bool random_yaw;
    float yaw_min;
    float yaw_max;
    bool random_pitch_roll;
    float max_tilt; /* Radians */
    af_vec3 scale_min;
    af_vec3 scale_max;
    bool uniform_scale;
    float slope_min; /* Radians (0 = horizontal, PI/2 = vertical) */
    float slope_max;
    float height_min;
    float height_max;
    float density;         /* Target instances per unit area / brush stamp */
    float min_spacing;     /* Collision avoidance radius */
    float align_to_normal; /* 0.0 = upright (0,1,0), 1.0 = surface normal */
    float offset_y;        /* Vertical offset in meters */
    int paint_mask_layer;  /* -1 = any layer, 0..3 = restricted to terrain layer */
    bool collision_avoidance;
} af_placement_rules;

typedef struct af_foliage_instance {
    af_transform transform;
} af_foliage_instance;

typedef struct af_foliage_chunk {
    size_t count;
    size_t capacity;
    af_foliage_instance *instances;
    af_vec3 bounds_min;
    af_vec3 bounds_max;
} af_foliage_chunk;

typedef struct af_foliage_layer {
    char name[AF_LANDSCAPE_NAME_CAPACITY];
    char mesh[AF_MESH_NAME_CAPACITY];
    af_material material;
    bool visible;
    bool cast_shadows;
    float max_distance; /* Distance culling limit in meters */
    af_placement_rules rules;
    size_t total_instances;
} af_foliage_layer;

typedef struct af_terrain_chunk {
    int32_t chunk_x;
    int32_t chunk_z;
    af_vec3 bounds_min;
    af_vec3 bounds_max;
    float heights[AF_LANDSCAPE_CHUNK_VERTICES * AF_LANDSCAPE_CHUNK_VERTICES];
    af_vec3 normals[AF_LANDSCAPE_CHUNK_VERTICES * AF_LANDSCAPE_CHUNK_VERTICES];
    /* RGBA weights for up to 4 splat layers, normalized to sum to 255 */
    uint8_t weights[AF_LANDSCAPE_CHUNK_VERTICES * AF_LANDSCAPE_CHUNK_VERTICES * 4];
    bool dirty_mesh;
    char mesh_name[64];
    af_foliage_chunk foliage[AF_LANDSCAPE_MAX_FOLIAGE_LAYERS];
} af_terrain_chunk;

typedef struct af_landscape {
    bool enabled;
    uint32_t chunk_count_x;
    uint32_t chunk_count_z;
    float vertex_spacing; /* Distance between adjacent vertices in meters */
    af_vec3 origin;       /* World origin corner */
    size_t chunk_count;
    af_terrain_chunk *chunks;
    size_t layer_count;
    af_terrain_layer layers[AF_LANDSCAPE_MAX_LAYERS];
    size_t foliage_layer_count;
    af_foliage_layer foliage_layers[AF_LANDSCAPE_MAX_FOLIAGE_LAYERS];
    size_t total_foliage_instances;
    uint64_t modification_version;
} af_landscape;

af_terrain_layer af_terrain_layer_default(size_t index);
af_placement_rules af_placement_rules_default(void);
af_foliage_layer af_foliage_layer_default(const char *name, const char *mesh);

af_landscape *af_landscape_create(uint32_t chunks_x, uint32_t chunks_z, float spacing);
af_landscape *af_landscape_clone(const af_landscape *landscape);
void af_landscape_destroy(af_landscape *landscape);

/* Sampling & Spatial Queries */
float af_landscape_get_height(const af_landscape *landscape, float world_x, float world_z);
bool af_landscape_get_normal(const af_landscape *landscape, float world_x, float world_z, af_vec3 *out_normal);
bool af_landscape_get_layer_weights(const af_landscape *landscape, float world_x, float world_z, float out_weights[4]);
bool af_landscape_raycast(const af_landscape *landscape, af_vec3 ray_origin, af_vec3 ray_dir, af_vec3 *hit_point,
                          af_vec3 *hit_normal);
void af_landscape_recompute_normals(af_landscape *landscape, int32_t min_chunk_x, int32_t min_chunk_z,
                                    int32_t max_chunk_x, int32_t max_chunk_z);

/* Terrain Sculpting */
bool af_landscape_sculpt_raise_lower(af_landscape *landscape, af_vec3 center, float radius, float delta,
                                     af_brush_falloff falloff);
bool af_landscape_sculpt_smooth(af_landscape *landscape, af_vec3 center, float radius, float strength,
                                af_brush_falloff falloff);
bool af_landscape_sculpt_flatten(af_landscape *landscape, af_vec3 center, float radius, float target_height,
                                 float strength, af_brush_falloff falloff);
bool af_landscape_paint_layer(af_landscape *landscape, af_vec3 center, float radius, size_t layer_idx, float strength,
                              af_brush_falloff falloff);
bool af_landscape_auto_paint_slopes(af_landscape *landscape, size_t rock_layer, float min_slope_rad, size_t sand_layer,
                                    float max_height);

/* Heightmaps */
bool af_landscape_generate_procedural_hills(af_landscape *landscape, float scale, float amplitude, uint32_t seed);
bool af_landscape_import_heightmap(af_landscape *landscape, const float *heights, uint32_t width, uint32_t height,
                                   float min_h, float max_h);
bool af_landscape_export_heightmap(const af_landscape *landscape, float *out_heights, uint32_t width, uint32_t height);

/* Chunk Mesh Generation */
size_t af_landscape_chunk_vertex_count(void);
size_t af_landscape_chunk_index_count(void);
bool af_landscape_build_chunk_mesh(const af_landscape *landscape, size_t chunk_idx, af_vertex *out_vertices,
                                   uint32_t *out_indices);

/* Foliage & Mesh Painting */
int af_landscape_add_foliage_layer(af_landscape *landscape, const char *name, const char *mesh);
bool af_landscape_remove_foliage_layer(af_landscape *landscape, size_t layer_idx);
bool af_landscape_set_foliage_visible(af_landscape *landscape, size_t layer_idx, bool visible);
bool af_landscape_paint_foliage(af_landscape *landscape, size_t layer_idx, af_vec3 center, float radius, float strength,
                                uint32_t seed);
bool af_landscape_erase_foliage(af_landscape *landscape, int layer_idx, af_vec3 center, float radius);
bool af_landscape_populate_procedural_foliage(af_landscape *landscape, size_t layer_idx, uint32_t seed);
void af_landscape_clear_foliage(af_landscape *landscape, int layer_idx);

/* Spatially Partitioned Instanced Query */
typedef struct af_foliage_batch {
    size_t layer_index;
    const char *mesh_name;
    af_material material;
    bool cast_shadows;
    size_t instance_count;
    const af_foliage_instance *instances;
} af_foliage_batch;

size_t af_landscape_query_foliage_batches(const af_landscape *landscape, const af_camera *camera, af_rect viewport,
                                          af_foliage_batch *out_batches, size_t max_batches,
                                          af_foliage_instance *instance_pool, size_t pool_capacity,
                                          size_t *out_total_instances);

#ifdef __cplusplus
}
#endif

#endif /* AFTERLIGHT_LANDSCAPE_H */

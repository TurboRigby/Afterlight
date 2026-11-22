/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_RENDERER_COMMON_H
#define AF_RENDERER_COMMON_H

#include <afterlight/afterlight.h>
#include <SDL3/SDL.h>
#include "assets/gltf.h"
#include "geometry/cluster_stream.h"
#include "local_shadow.h"
#include "shadow_math.h"
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AF_FRAMES_IN_FLIGHT 2u

enum {
    AF_RESOURCE_AA_MASK = 3u,
    AF_RESOURCE_BLOOM = 4u,
    AF_RESOURCE_MOTION = 8u,
    AF_RESOURCE_REFRACTION = 16u,
    AF_RESOURCE_RAY_SHADOWS = 32u,
    AF_RESOURCE_RAY_REFLECTIONS = 64u,
    AF_RESOURCE_UPSCALER = 128u,
    AF_RESOURCE_RAY_GI = 256u,
    AF_RESOURCE_RAY_MATERIALS = 512u,
    AF_RESOURCE_RAY_QUERY =
        AF_RESOURCE_RAY_SHADOWS | AF_RESOURCE_RAY_REFLECTIONS | AF_RESOURCE_RAY_GI | AF_RESOURCE_RAY_MATERIALS
};

#define AF_SKIN_INSTANCES 128u
#define AF_SKIN_MATRICES (AF_SKIN_INSTANCES * AF_ANIMATION_MAX_JOINTS + 1u)
#define AF_CLUSTER_INDEX_RANGES (2 * AF_CLUSTER_RESIDENT_PAGES + 1)
#define AF_LIGHT_TILE_SIZE 32u

enum { AF_RAY_SHADOW_MASK = 1, AF_RAY_REFLECTION_MASK = 2, AF_RAY_GI_MASK = 4 };

typedef struct af_ray_stats {
    uint64_t bytes, blas_builds, tlas_builds;
    size_t instances, meshes;
    double gpu_build_ms;
} af_ray_stats;

typedef struct af_light_tile_mask {
    uint32_t words[4];
} af_light_tile_mask;
_Static_assert(sizeof(af_light_tile_mask) == 16, "std430 uvec4 tile mask");
_Static_assert(AF_SCENE_MAX_LOCAL_LIGHTS == 128, "Tile bitmask covers 128 local lights");

/* Mesh Draw.params.y flags. Shadow draws put the face flags above their alpha mode. */
enum {
    AF_DRAW_RECEIVE_SHADOWS = 1u << 0,
    AF_DRAW_CULL_BACK = 1u << 1,
    AF_DRAW_FLIP_BACK_NORMAL = 1u << 2,
    AF_DRAW_MIRRORED = 1u << 3,
    AF_DRAW_BLEND_OPAQUE = 1u << 4,
    AF_DRAW_BLEND_PARTIAL = 1u << 5,
    AF_DRAW_INSTANCED = 1u << 6,
    AF_DRAW_CLUSTER_DEBUG = 1u << 7
};

typedef struct af_gpu_light {
    af_vec4 position_range, color_intensity, direction_inner, params;
} af_gpu_light;

typedef struct af_frame_uniform {
    af_mat4 view_projection;
    af_vec4 direction_intensity, color_ambient;
    af_mat4 shadow_matrices[4];
    af_vec4 cascade_splits, camera_position, camera_forward, shadow_params, cascade_params, render_params;
    af_vec4 light_count;
    af_mat4 local_shadow_matrices[AF_LOCAL_SHADOW_LAYERS];
    af_mat4 inverse_view_projection;
    af_vec4 sky_params;
    af_vec4 water_absorption, water_scattering, water_bounds, water_axes, water_screen, water_viewport, wind_time;
    af_vec4 rtgi_params;
    af_mat4 unjittered_view_projection, previous_view_projection;
    af_vec4 ray_params, rtgi_flags, light_tile_params;
} af_frame_uniform;
_Static_assert(sizeof(af_frame_uniform) == 2384, "Shader frame layout");

typedef struct af_blend_draw {
    size_t index;
    float depth;
} af_blend_draw;

typedef struct af_skin_instance {
    af_object_id object;
    af_mat4 matrices[AF_ANIMATION_MAX_JOINTS];
    size_t count;
} af_skin_instance;

typedef struct af_motion_record {
    af_mat4 model;
    uint32_t info[4];
} af_motion_record;
_Static_assert(sizeof(af_motion_record) == 80, "std430 temporal record");

typedef struct af_motion_snapshot {
    af_object_id id;
    af_mat4 model;
    uint64_t signature;
    bool visible;
} af_motion_snapshot;

typedef struct af_cluster_candidate {
    af_cluster bounds;
    uint32_t object[4];
} af_cluster_candidate;
_Static_assert(sizeof(af_cluster_candidate) == 48, "std430 cluster candidate");

typedef struct af_cluster_span {
    uint32_t first, count;
} af_cluster_span;

typedef struct af_cluster_view {
    af_mat4 projection;
    float pixel_scale;
} af_cluster_view;

typedef struct af_draw_material {
    af_color tint;
    af_vec4 params, surface, emission;
} af_draw_material;

typedef struct af_mesh_draw {
    af_mat4 model;
    af_draw_material material;
} af_mesh_draw;
_Static_assert(sizeof(af_mesh_draw) == 128, "Portable draw push constant limit");

typedef struct af_ray_surface_record {
    af_color color;
    af_vec4 emission_unlit;
    uint32_t addresses[4];
} af_ray_surface_record;
_Static_assert(sizeof(af_ray_surface_record) == 48 && offsetof(af_ray_surface_record, addresses) == 32,
               "Reflection hit record matches std430");
_Static_assert(sizeof(af_vertex) == 72 && offsetof(af_vertex, position) == 0 && offsetof(af_vertex, color) == 12 &&
                   offsetof(af_vertex, normal) == 28,
               "Reflection geometry matches the shader's packed vertex layout");

static inline uint64_t af_r_hash(uint64_t h, const void *data, size_t size) {
    const unsigned char *p = data;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static inline void af_r_water_name(af_object_id id, char name[AF_MESH_NAME_CAPACITY]) {
    snprintf(name, AF_MESH_NAME_CAPACITY, "__water_%llu", (unsigned long long)id);
}

int af_rc_blend_compare(const void *a, const void *b);
int af_rc_motion_compare(const void *a, const void *b);
uint32_t af_rc_mesh_face_flags(af_mesh_sidedness sidedness, af_mat4 model);
uint32_t af_rc_shadow_material_flags(af_alpha_mode alpha_mode, af_mesh_sidedness sidedness, af_mat4 model);
float af_rc_halton(unsigned n, unsigned base);
uint64_t af_rc_temporal_settings_hash(uint64_t h, af_render_settings value);
uint64_t af_rc_temporal_material_hash(uint64_t h, af_material value);
uint64_t af_rc_temporal_light_hash(uint64_t h, af_local_light value);
float af_rc_cluster_error_scale(af_mat4 world, float pixel_scale);
bool af_rc_cluster_wants_coarse(af_mat4 clip, af_vec3 minimum, af_vec3 maximum, float pixels, bool lod);
af_cluster_view af_rc_cluster_view(const af_frame_uniform *uniform, const af_camera *camera);
af_rect af_rc_resolve_viewport(SDL_Window *window, af_rect rect);
bool af_rc_validate_mesh_data(const af_mesh_data *mesh, af_vec3 *out_min, af_vec3 *out_max, bool *out_skinned,
                              uint32_t *out_max_joint);
bool af_rc_rgba32f_to_rgba16f(const float *src, uint16_t *dst, size_t float_count);
void af_rc_bin_local_lights(const af_gpu_light *lights, uint32_t count, const af_camera *camera,
                            const af_frame_uniform *uniform, uint32_t render_w, uint32_t render_h, uint32_t cols,
                            uint32_t rows, af_light_tile_mask *tiles);
bool af_rc_ray_aabb_hit(const float origin[3], const float dir[3], const float lo[3], const float hi[3], float near_t,
                        float far_t, float *out_t);
float af_rc_f16_to_f32(uint16_t h);

#endif

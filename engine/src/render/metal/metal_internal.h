/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_METAL_INTERNAL_H
#define AF_METAL_INTERNAL_H

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL_metal.h>

#include "render/renderer_backend.h"
#include "render/renderer_common.h"
#include "render/metal/metal_bindings.h"
#include "render/metal/spv_to_msl.h"
#include "animation/morph.h"
#include "core/extension_runtime.h"
#include "core/log.h"
#include "water/water_internal.h"
#include "water/water_particles_internal.h"

#include <dispatch/dispatch.h>
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AF_MTL_RELEASE(obj)                                                                                            \
    do {                                                                                                               \
        if ((obj) != nil) {                                                                                            \
            [(obj) release];                                                                                           \
            (obj) = nil;                                                                                               \
        }                                                                                                              \
    } while (0)

typedef struct af_mtl_renderer af_mtl_renderer;
typedef struct af_mtl_texture af_mtl_texture;
typedef struct af_mtl_shadows af_mtl_shadows;
typedef struct af_mtl_refraction af_mtl_refraction;
typedef struct af_mtl_ui af_mtl_ui;
typedef struct af_mtl_post af_mtl_post;
typedef struct af_mtl_ray_query af_mtl_ray_query;
typedef struct af_mtl_upscaler af_mtl_upscaler;

typedef struct af_mtl_buffer {
    id<MTLBuffer> handle;
    uint64_t allocation_bytes;
    uint64_t capacity;
} af_mtl_buffer;

typedef struct af_mtl_shadow_draw {
    af_mat4 model;
    size_t mesh;
    uint32_t skin_base;
    af_mtl_texture *texture;
    float alpha, cutoff;
    uint32_t material_flags;
    bool ray_caster;
    size_t object_index;
} af_mtl_shadow_draw;

typedef struct af_mtl_environment_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_mtl_texture *specular, *diffuse, *sky;
} af_mtl_environment_asset;

typedef struct af_mtl_texture_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_mtl_texture *texture;
    bool normal;
} af_mtl_texture_asset;

typedef struct af_mtl_material_shader_asset {
    char name[AF_MESH_NAME_CAPACITY];
    uint32_t *spirv;
    size_t word_count;
    id<MTLFunction> fragment_fn;
    id<MTLRenderPipelineState> opaque_pipeline;
    id<MTLRenderPipelineState> blend_pipeline;
} af_mtl_material_shader_asset;

typedef struct af_mtl_cluster_residency af_mtl_cluster_residency;

typedef struct af_mtl_gpu_mesh {
    af_mtl_buffer vertices;
    af_mtl_buffer indices;
    uint32_t vertex_count, index_count, max_joint;
    bool skinned;
    af_mesh_sidedness sidedness;
    af_vec3 minimum, maximum;
    af_mesh_morph *morph;
    af_vertex *morph_base;
    af_cluster *clusters;
    uint32_t cluster_count, cluster_fine_count;
    af_cluster_region *cluster_regions;
    uint32_t cluster_region_count;
    af_mtl_cluster_residency *residency;
} af_mtl_gpu_mesh;

typedef struct af_mtl_cached_cluster_page {
    af_mtl_gpu_mesh gpu;
    uint32_t region, frame_mask;
    uint64_t last_used, generation;
} af_mtl_cached_cluster_page;

typedef struct af_mtl_pending_cluster_page {
    af_cluster_mesh mesh;
    uint64_t vertex_bytes, index_bytes;
    uint32_t region;
    uint64_t last_wanted;
} af_mtl_pending_cluster_page;

struct af_mtl_cluster_residency {
    af_cluster_stream *stream;
    uint64_t *page_requirements;
    af_mtl_pending_cluster_page pending;
    af_mtl_cached_cluster_page pages[AF_CLUSTER_RESIDENT_PAGES];
};

typedef struct af_mtl_cluster_index_range {
    uint32_t first, count;
    af_mtl_cached_cluster_page *page;
} af_mtl_cluster_index_range;

typedef struct af_mtl_cluster_stream_batch {
    const af_mtl_gpu_mesh *mesh;
    af_cluster_span commands;
} af_mtl_cluster_stream_batch;

typedef struct af_mtl_morph_draw {
    uint64_t offset;
    af_vec3 minimum, maximum;
    af_object_id object;
    uint64_t signature;
} af_mtl_morph_draw;

typedef struct af_mtl_stream_frame {
    af_mtl_gpu_mesh gpu;
    uint64_t revision;
} af_mtl_stream_frame;

typedef struct af_mtl_mesh_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_mtl_gpu_mesh gpu;
    bool dynamic, retired;
    uint64_t revision;
    af_vertex *cpu_vertices;
    uint32_t *cpu_indices;
    uint32_t vertex_count;
    af_mtl_stream_frame streams[AF_FRAMES_IN_FLIGHT];
} af_mtl_mesh_asset;

typedef struct af_mtl_prop_batch {
    const af_mtl_gpu_mesh *mesh;
    const af_material *material;
    af_mesh_draw draw;
    uint32_t first_object, first_record, count;
} af_mtl_prop_batch;

typedef struct af_mtl_ray_instance {
    uint64_t mesh_key, page_generation;
    id<MTLBuffer> vertices;
    id<MTLBuffer> indices;
    uint32_t vertex_count, first_index, index_count;
    af_mat4 world;
    uint8_t mask;
    bool single_sided, transient;
    af_color color;
    af_vec4 emission_unlit;
} af_mtl_ray_instance;

typedef struct af_mtl_frame {
    af_mtl_buffer uniform;
    af_mtl_buffer skin_buffer;
    af_mtl_buffer motion_buffer;
    af_mtl_buffer morph_buffer;
    af_mtl_buffer light_buffer;
    af_mtl_buffer light_tile_buffer;
    af_mtl_buffer cluster_input;
    af_mtl_buffer cluster_transforms;
    af_mtl_buffer cluster_stats_buf;
    af_mtl_buffer cluster_indirect;
    uint32_t cluster_submitted, cluster_coarse_regions;
    uint64_t shadow_signatures[4], local_shadow_signatures[24];
    double gpu_total_ms;
    bool timing_ready;
    id<MTLCommandBuffer> last_command;
} af_mtl_frame;

typedef struct af_mtl_swapchain {
    uint32_t width, height;
    uint32_t render_width, render_height;
    uint32_t resource_config;
    af_mtl_post *post;
    af_mtl_refraction *refraction;
    id<MTLTexture> depths[AF_FRAMES_IN_FLIGHT];
    uint64_t depth_bytes;
    id<MTLRenderPipelineState> pipeline;
    id<MTLRenderPipelineState> blend_pipeline;
    id<MTLRenderPipelineState> sky_pipeline;
    id<MTLRenderPipelineState> water_attenuation;
    id<MTLRenderPipelineState> water_scattering;
    bool upscaler_created;
} af_mtl_swapchain;

struct af_mtl_renderer {
    af_renderer base;
    SDL_Window *window;
    SDL_MetalView metal_view;
    CAMetalLayer *layer;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLLibrary> shader_lib;
    id<MTLFunction> mesh_vert_fn;
    id<MTLFunction> mesh_frag_fn;
    id<MTLFunction> mesh_ray_frag_fn;
    id<MTLFunction> sky_vert_fn;
    id<MTLFunction> sky_frag_fn;
    id<MTLFunction> shadow_vert_fn;
    id<MTLFunction> shadow_frag_fn;
    id<MTLFunction> post_frag_fn;
    id<MTLFunction> gi_denoise_frag_fn;
    id<MTLFunction> ui_vert_fn;
    id<MTLFunction> ui_frag_fn;
    id<MTLFunction> cluster_icb_fn;
    id<MTLComputePipelineState> cluster_pipeline;
    id<MTLDepthStencilState> depth_rw;
    id<MTLDepthStencilState> depth_ro;
    id<MTLDepthStencilState> depth_none;
    dispatch_semaphore_t frame_semaphore;
    bool frame_semaphore_held;

    const af_physics *water_physics;
    af_water water_configs[AF_SCENE_MAX_WATERS];
    af_transform water_transforms[AF_SCENE_MAX_WATERS];
    float water_times[AF_SCENE_MAX_WATERS], water_alphas[AF_SCENE_MAX_WATERS];
    const af_scene *previous_scene;
    float interpolation_alpha;

    af_mtl_ui *ui;
    af_mtl_shadows *shadows, *local_shadows;
    af_mtl_ray_query *rays;
    bool ray_supported, ray_failed;
    uint64_t ray_revision, gi_emitter_signature;
    af_mtl_ray_instance *ray_inputs;
    size_t ray_input_capacity, ray_raster_casters;

    uint32_t local_shadow_layers, local_shadow_resolution, local_shadow_lights, local_shadow_overflow;
    uint32_t shadow_resolution, shadow_layers;
    af_mtl_shadow_draw *shadow_draws;
    af_shadow_bounds *shadow_bounds;
    size_t shadow_capacity, shadow_count, shadow_draw_calls;

    const af_scene *posed_scene;
    const af_mat4 *posed_worlds;
    const af_animation_pose *const *posed_morphs;
    size_t posed_count;

    const af_scene *morph_scene;
    af_mtl_morph_draw *morph_draws;
    af_vertex *morph_vertices;
    af_morph_delta *morph_deltas;
    size_t morph_draw_count, morph_draw_capacity, morph_vertex_count, morph_vertex_capacity, morph_delta_capacity;

    af_skin_instance skins[AF_SKIN_INSTANCES];
    size_t skin_count;
    uint64_t skin_revision;
    af_skin_instance previous_skins[AF_SKIN_INSTANCES];
    size_t previous_skin_count;

    af_motion_snapshot *motion_previous, *motion_next;
    size_t motion_count, motion_next_count, motion_previous_capacity, motion_next_capacity;
    af_motion_record *motion_records;
    size_t motion_records_capacity;

    af_mtl_upscaler *upscaler;
    bool upscaler_enabled, upscaler_failed;
    uint32_t upscaler_quality;

    af_mat4 previous_view_projection;
    af_camera temporal_camera;
    af_rect temporal_viewport;
    const af_scene *temporal_scene;
    uint64_t temporal_signature, temporal_local_light_signature, temporal_frame, temporal_resets;
    uint32_t temporal_objects, temporal_skins;
    bool temporal_valid, temporal_supported;
    af_vec2 temporal_jitter, previous_temporal_jitter;

    uint64_t texture_revision, mesh_revision;
    uint32_t shadow_passes, shadow_cached_passes;

    af_mtl_buffer capture_buffer;
    af_vec2 capture_position;
    uint32_t capture_width, capture_height;
    bool capture_supported, capture_pending, capture_ready, capture_motion;

    af_mtl_texture *white, *flat_normal;
    af_mtl_texture *environment, *irradiance, *brdf;
    uint32_t environment_profile;
    af_mtl_environment_asset environments[16];
    size_t environment_count;
    char active_environment[AF_MESH_NAME_CAPACITY];
    af_mtl_texture_asset *textures;
    size_t texture_count, texture_capacity, missing_textures;
    af_mtl_material_shader_asset *custom_shaders;
    size_t custom_shader_count, custom_shader_capacity;

    af_gpu_light local_light_staging[AF_SCENE_MAX_LOCAL_LIGHTS];
    uint32_t local_light_staging_count;
    af_light_tile_mask *light_tile_staging;
    size_t light_tile_capacity;
    uint32_t light_tile_cols, light_tile_rows;

    float anisotropy;
    bool timing_valid;
    double gpu_ms[5];
    double cpu_fence_ms, cpu_acquire_ms, cpu_record_ms;

    af_mtl_frame frames[AF_FRAMES_IN_FLIGHT];
    uint32_t frame_index;
    af_mtl_swapchain swapchain;

    af_mtl_mesh_asset *meshes;
    uint64_t water_upload_bytes, water_stream_uploads, mesh_idle_waits;
    double water_upload_ms;
    size_t mesh_count, mesh_capacity;
    size_t draw_calls, missing_meshes;

    af_debug_view debug_view;
    bool culling_disabled, prop_instancing_disabled;
    bool clusters_disabled, cluster_colors, cluster_lod_disabled;
    af_cluster_stats cluster_stats;
    bool cluster_gpu_supported, cluster_gpu_disabled;
    uint32_t cluster_gpu_candidates, cluster_gpu_visible, cluster_gpu_triangles, cluster_gpu_coarse_regions;
    af_cluster_candidate *cluster_candidates;
    af_mat4 *cluster_matrices;
    af_cluster_span *cluster_spans;
    af_mtl_cluster_stream_batch *cluster_stream_batches;
    uint64_t page_budget, page_uploads, page_evictions, page_failures;
    size_t cluster_object_capacity;
    size_t visible_objects, culled_objects;

    double gpu_budget_ms;
    uint64_t gpu_budget_bytes;
    bool resize_pending;
    bool swapchain_invalid;
    bool validation_enabled;
    atomic_uint validation_errors;
    uint64_t rendered_frames;
    uint64_t swapchain_rebuilds;
    const af_terrain_chunk *landscape_chunks;
    uint64_t landscape_version;
};

/* Texture subsystem (texture_metal.m) */
af_mtl_texture *af_mtl_texture_create(id<MTLDevice> device, const unsigned char *rgba, uint32_t width, uint32_t height,
                                      bool srgb, bool repeat, float anisotropy);
af_mtl_texture *af_mtl_texture_create_hdr(id<MTLDevice> device, const float *rgba, uint32_t width, uint32_t height,
                                          uint32_t levels, bool wrap_u);
void af_mtl_texture_destroy(af_mtl_texture *texture);
af_texture_info af_mtl_texture_info(const af_mtl_texture *texture);
id<MTLTexture> af_mtl_texture_handle(const af_mtl_texture *texture);
id<MTLSamplerState> af_mtl_texture_sampler(const af_mtl_texture *texture);

/* Shadow subsystem (shadow_metal.m) */
af_mtl_shadows *af_mtl_shadows_create(id<MTLDevice> device, id<MTLFunction> vert_fn, id<MTLFunction> frag_fn,
                                      uint32_t resolution, uint32_t layers);
void af_mtl_shadows_destroy(af_mtl_shadows *shadows);
uint64_t af_mtl_shadows_allocation_bytes(const af_mtl_shadows *shadows);
id<MTLTexture> af_mtl_shadows_texture(const af_mtl_shadows *shadows, uint32_t frame);
id<MTLSamplerState> af_mtl_shadows_sampler(const af_mtl_shadows *shadows);
id<MTLRenderCommandEncoder> af_mtl_shadows_begin(af_mtl_shadows *shadows, id<MTLCommandBuffer> cmd, uint32_t frame,
                                                 uint32_t layer, af_shadow_settings settings,
                                                 id<MTLBuffer> skin_buffer);
void af_mtl_shadows_push(id<MTLRenderCommandEncoder> enc, const float *mvp, uint32_t skin_base, uint32_t material_flags,
                         float alpha, float cutoff, const af_mtl_texture *texture);

/* Refraction subsystem (refraction_metal.m) */
af_mtl_refraction *af_mtl_refraction_create(id<MTLDevice> device, uint32_t width, uint32_t height, uint32_t count);
void af_mtl_refraction_destroy(af_mtl_refraction *r);
uint64_t af_mtl_refraction_allocation_bytes(const af_mtl_refraction *r);
id<MTLTexture> af_mtl_refraction_color(const af_mtl_refraction *r, uint32_t frame);
id<MTLTexture> af_mtl_refraction_depth(const af_mtl_refraction *r, uint32_t frame);
id<MTLSamplerState> af_mtl_refraction_color_sampler(const af_mtl_refraction *r);
id<MTLSamplerState> af_mtl_refraction_depth_sampler(const af_mtl_refraction *r);
void af_mtl_refraction_copy(af_mtl_refraction *r, id<MTLCommandBuffer> cmd, uint32_t frame, id<MTLTexture> color,
                            id<MTLTexture> depth);

/* UI overlay subsystem (ui_metal.m) */
af_mtl_ui *af_mtl_ui_create(id<MTLDevice> device, id<MTLFunction> vert_fn, id<MTLFunction> frag_fn,
                            const unsigned char *rgba, uint32_t width, uint32_t height);
void af_mtl_ui_destroy(af_mtl_ui *ui);
bool af_mtl_ui_upload_texture(af_mtl_ui *ui, uint32_t id, const unsigned char *rgba, uint32_t width, uint32_t height);
bool af_mtl_ui_remove_texture(af_mtl_ui *ui, uint32_t id);
void af_mtl_ui_process_uploads(af_mtl_ui *ui, uint32_t frame_index);
bool af_mtl_ui_prepare(af_mtl_ui *ui, uint32_t frame_index, const af_overlay_frame *frame);
void af_mtl_ui_draw(af_mtl_ui *ui, id<MTLRenderCommandEncoder> enc, uint32_t frame_index, const af_overlay_frame *frame,
                    uint32_t extent_w, uint32_t extent_h);

/* Post-processing & GI denoise subsystem (post_metal.m) */
af_mtl_post *af_mtl_post_create(id<MTLDevice> device, id<MTLFunction> vert_fn, id<MTLFunction> post_fn,
                                id<MTLFunction> gi_fn, uint32_t out_w, uint32_t out_h, uint32_t render_w,
                                uint32_t render_h, uint32_t count, af_render_settings settings, bool motion, bool gi,
                                bool upscale);
void af_mtl_post_destroy(af_mtl_post *post);
uint64_t af_mtl_post_allocation_bytes(const af_mtl_post *post);
id<MTLTexture> af_mtl_post_scene_texture(const af_mtl_post *post, uint32_t frame);
id<MTLTexture> af_mtl_post_motion_texture(const af_mtl_post *post, uint32_t frame);
id<MTLTexture> af_mtl_post_gi_texture(const af_mtl_post *post, uint32_t frame);
id<MTLTexture> af_mtl_post_upscaled_texture(const af_mtl_post *post, uint32_t frame);
void af_mtl_post_reset_temporal(af_mtl_post *post);
void af_mtl_post_invalidate_lighting_history(af_mtl_post *post, bool gi);
void af_mtl_post_denoise_gi(af_mtl_post *post, id<MTLCommandBuffer> cmd, uint32_t frame, af_render_settings settings,
                            af_rect normalized_viewport, af_vec2 previous_minus_current_jitter);
/* Encodes all post passes and returns the open output render encoder targeting drawable_tex for UI drawing. */
id<MTLRenderCommandEncoder> af_mtl_post_draw(af_mtl_post *post, id<MTLCommandBuffer> cmd, id<MTLTexture> drawable_tex,
                                             uint32_t frame, af_render_settings settings, af_rect normalized_viewport,
                                             af_vec2 normalized_jitter, af_debug_view debug_view, bool upscaled,
                                             float time_seconds, float delta_time);

/* Hardware ray query subsystem (ray_query_metal.m) */
af_mtl_ray_query *af_mtl_ray_query_create(id<MTLDevice> device, unsigned frames);
void af_mtl_ray_query_destroy(af_mtl_ray_query *r);
size_t af_mtl_ray_query_instance_limit(const af_mtl_ray_query *r);
bool af_mtl_ray_query_prepare(af_mtl_ray_query *r, unsigned frame, const af_mtl_ray_instance *instances, size_t count,
                              bool reflections);
void af_mtl_ray_query_record(af_mtl_ray_query *r, unsigned frame, id<MTLCommandBuffer> cmd);
id<MTLAccelerationStructure> af_mtl_ray_query_tlas(const af_mtl_ray_query *r, unsigned frame);
id<MTLBuffer> af_mtl_ray_query_surfaces(const af_mtl_ray_query *r, unsigned frame);
void af_mtl_ray_query_use_resources(const af_mtl_ray_query *r, unsigned frame, id<MTLRenderCommandEncoder> enc,
                                    bool reflections);
af_ray_stats af_mtl_ray_query_stats(const af_mtl_ray_query *r);

/* Native MetalFX upscaler subsystem (upscaler_metal.m) */
af_mtl_upscaler *af_mtl_upscaler_create(id<MTLDevice> device);
void af_mtl_upscaler_destroy(af_mtl_upscaler *u);
bool af_mtl_upscaler_configure(af_mtl_upscaler *u, uint32_t render_w, uint32_t render_h, uint32_t out_w, uint32_t out_h,
                               bool temporal);
void af_mtl_upscaler_reset(af_mtl_upscaler *u);
bool af_mtl_upscaler_encode(af_mtl_upscaler *u, id<MTLCommandBuffer> cmd, id<MTLTexture> color, id<MTLTexture> depth,
                            id<MTLTexture> motion, id<MTLTexture> output, float jitter_px_x, float jitter_px_y,
                            bool reset_history);
uint32_t af_mtl_upscaler_quality_count(const af_mtl_upscaler *u);
const char *af_mtl_upscaler_quality_name(const af_mtl_upscaler *u, uint32_t index);
float af_mtl_upscaler_quality_scale(const af_mtl_upscaler *u, uint32_t index);
bool af_mtl_upscaler_is_spatial(const af_mtl_upscaler *u);

/* Device, swapchain & pipeline internals (renderer_device_metal.m) */
bool af_mtl_build_mesh_pipelines(af_mtl_renderer *r, id<MTLFunction> frag_fn, id<MTLRenderPipelineState> *out_opaque,
                                 id<MTLRenderPipelineState> *out_blend);
void af_mtl_destroy_swapchain(af_mtl_renderer *r);
bool af_mtl_create_swapchain(af_mtl_renderer *r, af_render_settings settings, uint32_t config);
bool af_mtl_r_wait_idle(af_mtl_renderer *r);

/* Buffer, mesh, streaming, skinning, morph & picking internals (renderer_mesh_metal.m) */
void af_mtl_destroy_buffer(af_mtl_buffer *buf);
bool af_mtl_create_buffer(af_mtl_renderer *r, const void *data, uint64_t size, af_mtl_buffer *out);
void af_mtl_destroy_mesh(af_mtl_gpu_mesh *mesh);
size_t af_mtl_find_mesh_index(const af_mtl_renderer *r, const char *name);
void af_mtl_stream_destroy(af_mtl_mesh_asset *a);
bool af_mtl_stream_buffer(af_mtl_renderer *r, af_mtl_buffer *b, const void *data, uint64_t bytes);
bool af_mtl_stream_prepare(af_mtl_renderer *r);
void af_mtl_bind_object_mesh(id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame, const af_mtl_gpu_mesh *mesh,
                             const af_mtl_morph_draw *morph);
bool af_mtl_object_world(af_mtl_renderer *r, const af_scene *scene, size_t index, af_mat4 *out);
const af_mtl_morph_draw *af_mtl_morph_draw_for(const af_mtl_renderer *r, const af_scene *scene, size_t index);
uint32_t af_mtl_skin_base(const af_mtl_renderer *r, af_object_id object, const af_mtl_gpu_mesh *mesh);
af_shadow_bounds af_mtl_object_bounds(af_mtl_renderer *r, af_object_id object, af_mat4 world,
                                      const af_mtl_gpu_mesh *mesh, const af_mtl_morph_draw *morph);
bool af_mtl_prepare_morphs(af_mtl_renderer *r, const af_scene *scene);

/* Cluster LOD, streaming residency & ICB compute culling (renderer_cluster_metal.m) */
void af_mtl_destroy_cluster_compute(af_mtl_renderer *r);
void af_mtl_update_cluster_pages(af_mtl_renderer *r);
void af_mtl_use_cluster_page(af_mtl_renderer *r, af_mtl_cached_cluster_page *page);
size_t af_mtl_cluster_index_ranges(const af_mtl_renderer *r, const af_mtl_gpu_mesh *base, af_mat4 model,
                                   af_cluster_view view, af_mtl_cluster_index_range *ranges);
bool af_mtl_prepare_cluster_commands(af_mtl_renderer *r, af_mtl_frame *frame, id<MTLCommandBuffer> cmd,
                                     const af_scene *scene, af_mat4 projection, float pixel_scale, size_t objects);
size_t af_mtl_draw_clusters(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame,
                            const af_mtl_gpu_mesh *mesh, af_mesh_draw draw, af_mat4 visibility, float pixel_scale,
                            uint32_t object);
size_t af_mtl_draw_indirect_clusters(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame,
                                     const af_mtl_gpu_mesh *mesh, af_cluster_span span);

/* Assets, environments, custom shaders & readback (renderer_assets_metal.m) */
bool af_mtl_create_environment(af_mtl_renderer *r, uint32_t profile);
void af_mtl_active_environment_textures(const af_mtl_renderer *r, af_mtl_texture **out_spec, af_mtl_texture **out_diff,
                                        af_mtl_texture **out_sky);
id<MTLRenderPipelineState> af_mtl_material_pipeline(const af_mtl_renderer *r, const char *shader_name, bool blend);

/* Main Metal ops declarations (registered in renderer_device_metal.m) */
void af_mtl_r_destroy(af_mtl_renderer *r);
void af_mtl_r_set_upscaler(af_mtl_renderer *r, const struct af_vulkan_upscaler_api *api, void *state);
bool af_mtl_r_set_upscaler_enabled(af_mtl_renderer *r, bool enabled);
bool af_mtl_r_upscaler_active(const af_mtl_renderer *r);
uint32_t af_mtl_r_upscaler_quality_count(const af_mtl_renderer *r);
const char *af_mtl_r_upscaler_quality_name(const af_mtl_renderer *r, uint32_t index);
uint32_t af_mtl_r_upscaler_quality(const af_mtl_renderer *r);
bool af_mtl_r_set_upscaler_quality(af_mtl_renderer *r, uint32_t index);
void af_mtl_r_release_upscaler(af_mtl_renderer *r);
af_temporal_test_stats af_mtl_r_test_temporal(const af_mtl_renderer *r);
void af_mtl_r_reset_temporal(af_mtl_renderer *r);
void af_mtl_r_resize(af_mtl_renderer *r);
bool af_mtl_r_upload_mesh(af_mtl_renderer *r, const char *name, const af_mesh_data *mesh);
bool af_mtl_r_upload_mesh_file(af_mtl_renderer *r, const char *name, af_mesh_file *mesh, af_mesh_sidedness sidedness);
bool af_mtl_r_set_skin_palette(af_mtl_renderer *r, af_object_id object, const af_mat4 *matrices, size_t count);
void af_mtl_r_clear_skin_palettes(af_mtl_renderer *r);
void af_mtl_r_set_object_worlds(af_mtl_renderer *r, const af_scene *scene, const af_mat4 *world,
                                const af_animation_pose *const *morph_poses, size_t count);
bool af_mtl_r_remove_texture(af_mtl_renderer *r, const char *name);
bool af_mtl_r_upload_texture(af_mtl_renderer *r, const char *name, const unsigned char *rgba, uint32_t width,
                             uint32_t height);
bool af_mtl_r_upload_material_shader(af_mtl_renderer *r, const char *name, const uint32_t *spirv, size_t word_count);
bool af_mtl_r_remove_material_shader(af_mtl_renderer *r, const char *name);
bool af_mtl_r_remove_mesh(af_mtl_renderer *r, const char *name);
bool af_mtl_r_upload_overlay_atlas(af_mtl_renderer *r, const unsigned char *rgba, uint32_t width, uint32_t height);
af_object_id af_mtl_r_pick_object(af_mtl_renderer *r, const af_scene *scene, const af_camera *camera, af_rect viewport,
                                  af_vec2 point);
bool af_mtl_r_update_water(af_mtl_renderer *r, const af_physics *physics, const af_scene *scene, float alpha);
bool af_mtl_r_update_landscape(af_mtl_renderer *r, const af_scene *scene);
bool af_mtl_r_draw(af_mtl_renderer *r, af_color color, const af_scene *scene, const af_camera *camera, af_rect viewport,
                   af_object_id highlighted, const af_overlay_frame *overlay, float frame_time_ms, bool *rendered);
bool af_mtl_r_upload_overlay_texture(af_mtl_renderer *r, uint32_t id, const unsigned char *rgba, uint32_t width,
                                     uint32_t height);
bool af_mtl_r_remove_overlay_texture(af_mtl_renderer *r, uint32_t id);
bool af_mtl_r_test_capture_motion(af_mtl_renderer *r, af_vec2 position);
bool af_mtl_r_test_read_motion(af_mtl_renderer *r, af_vec4 *out);
bool af_mtl_r_test_capture_region(af_mtl_renderer *r, af_vec2 position, uint32_t width, uint32_t height);
bool af_mtl_r_test_read_pixels(af_mtl_renderer *r, unsigned char *rgba, size_t capacity, uint32_t *width,
                               uint32_t *height);
bool af_mtl_r_test_capture_pixel(af_mtl_renderer *r, af_vec2 position);
bool af_mtl_r_test_read_pixel(af_mtl_renderer *r, unsigned char rgba[4]);
bool af_mtl_r_set_debug_view(af_mtl_renderer *r, af_debug_view view);
void af_mtl_r_set_visibility_culling(af_mtl_renderer *r, bool enabled);
void af_mtl_r_set_prop_instancing(af_mtl_renderer *r, bool enabled);
bool af_mtl_r_upload_cluster_stream(af_mtl_renderer *r, const char *name, af_cluster_mesh *mesh,
                                    af_cluster_stream *stream);
bool af_mtl_r_upload_clusters(af_mtl_renderer *r, const char *name, af_cluster_mesh *mesh);
void af_mtl_r_set_cluster_lod(af_mtl_renderer *r, bool enabled);
void af_mtl_r_set_cluster_gpu_culling(af_mtl_renderer *r, bool enabled);
void af_mtl_r_set_cluster_rendering(af_mtl_renderer *r, bool enabled, bool debug_colors);
bool af_mtl_r_set_cluster_cache_budget(af_mtl_renderer *r, uint64_t bytes);
af_cluster_stats af_mtl_r_cluster_stats(const af_mtl_renderer *r);
bool af_mtl_r_set_render_budget(af_mtl_renderer *r, double gpu_ms, uint64_t bytes);
af_engine_stats af_mtl_r_stats(const af_mtl_renderer *r);
bool af_mtl_r_texture_info(const af_mtl_renderer *r, const char *name, af_texture_info *out);
bool af_mtl_r_upload_environment(af_mtl_renderer *r, const char *name, const float *rgba, uint32_t width,
                                 uint32_t height);
bool af_mtl_r_upload_normal_map(af_mtl_renderer *r, const char *name, const unsigned char *rgba, uint32_t width,
                                uint32_t height);
bool af_mtl_r_commit_asset(af_mtl_renderer *r, const char *staged, const char *target, bool mesh);
bool af_mtl_r_upload_environment_baked(af_mtl_renderer *r, const char *name, const af_hdr_file *source,
                                       const af_ibl_images *images);
void af_mtl_r_set_interpolation(af_mtl_renderer *r, const af_scene *previous, float alpha);

#endif

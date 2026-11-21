/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_RENDERER_INTERNAL_H
#define AF_RENDERER_INTERNAL_H

#include "renderer.h"
#include <afterlight/version.h>
#include "animation/morph.h"
#include "core/extension_runtime.h"
#include "core/log.h"
#include "water/water_internal.h"
#include "water/water_particles_internal.h"
#include "vulkan_device_policy.h"
#include "ui_vulkan.h"
#include "texture_vulkan.h"
#include "shadow_vulkan.h"
#include "shadow_math.h"
#include "post_vulkan.h"
#include "refraction_vulkan.h"
#include "ray_query_vulkan.h"
#include "ibl.h"
#include "local_shadow.h"
#include "vk_memory.h"

#include <SDL3/SDL_vulkan.h>
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#define AF_FRAMES_IN_FLIGHT AF_UI_FRAMES_IN_FLIGHT
_Static_assert(AF_FRAMES_IN_FLIGHT == AF_SHADOW_FRAME_COUNT, "Independent shadow maps for each frame");

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
    AF_RESOURCE_RAY_QUERY = AF_RESOURCE_RAY_SHADOWS | AF_RESOURCE_RAY_REFLECTIONS | AF_RESOURCE_RAY_GI |
                            AF_RESOURCE_RAY_MATERIALS
};

#define AF_PORTABILITY_SUBSET "VK_KHR_portability_subset"
#define AF_STARTUP_EXTENSION_CAPACITY (AF_EXTENSION_LIMIT * AF_VULKAN_STARTUP_MAX_EXTENSIONS)
#define AF_SKIN_INSTANCES 128u
#define AF_SKIN_MATRICES (AF_SKIN_INSTANCES * AF_ANIMATION_MAX_JOINTS + 1u)
#define AF_CLUSTER_INDEX_RANGES (2 * AF_CLUSTER_RESIDENT_PAGES + 1)
#define AF_LIGHT_TILE_SIZE 32u

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

typedef struct af_gpu_buffer {
    VkBuffer handle;
    af_vk_allocation alloc;
    VkDeviceSize allocation_bytes, capacity;
} af_gpu_buffer;

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

typedef struct af_shadow_draw {
    af_mat4 model;
    size_t mesh;
    uint32_t skin_base;
    af_vk_texture *texture;
    float alpha, cutoff;
    uint32_t material_flags;
    bool ray_caster;
    size_t object_index;
} af_shadow_draw;

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

typedef struct af_environment_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_vk_texture *specular, *diffuse, *sky;
} af_environment_asset;

typedef struct af_texture_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_vk_texture *texture;
    bool normal;
} af_texture_asset;

typedef struct af_material_shader_asset {
    char name[AF_MESH_NAME_CAPACITY];
    uint32_t *spirv;
    size_t word_count;
    VkPipeline opaque_pipeline, blend_pipeline;
} af_material_shader_asset;

typedef struct af_cluster_candidate {
    af_cluster bounds;
    uint32_t object[4];
} af_cluster_candidate;
_Static_assert(sizeof(af_cluster_candidate) == 48, "std430 cluster candidate");
_Static_assert(sizeof(VkDrawIndexedIndirectCommand) == 20, "std430 indexed indirect command");

typedef struct af_cluster_span {
    uint32_t first, count;
} af_cluster_span;

typedef struct af_cluster_view {
    af_mat4 projection;
    float pixel_scale;
} af_cluster_view;

typedef struct af_frame {
    VkCommandBuffer command;
    VkSemaphore acquired;
    VkFence fence;
    af_gpu_buffer uniform, skin_buffer, motion_buffer, morph_buffer, light_buffer, light_tile_buffer;
    VkDescriptorSet descriptor;
    af_gpu_buffer cluster_input, cluster_transforms, cluster_output;
    VkDescriptorSet cluster_descriptor;
    uint32_t cluster_submitted, cluster_coarse_regions;
    VkQueryPool timestamps;
    bool timestamp_submitted;
    uint64_t shadow_signatures[4], local_shadow_signatures[24];
} af_frame;

typedef struct af_depth_attachment {
    VkDeviceSize allocation_bytes;
    VkImage image;
    af_vk_allocation alloc;
    VkImageView view;
} af_depth_attachment;

typedef struct af_swapchain {
    VkSwapchainKHR handle;
    VkExtent2D extent, render_extent;
    void *upscaler_context;
    bool upscaler_created;
    uint32_t resource_config;
    VkSurfaceFormatKHR surface_format;
    af_vk_post *post;
    af_vk_refraction *refraction;
    VkRenderPass render_pass, water_pass;
    VkPipelineLayout pipeline_layout;
    VkPipeline pipeline, blend_pipeline, sky_pipeline, water_attenuation, water_scattering;
    uint32_t image_count;
    VkImage *images;
    VkImageView *views;
    VkFramebuffer *framebuffers;
    VkFormat depth_format;
    af_depth_attachment *depths;
    /* Presentation waits are tied to images, not frames in flight. */
    VkSemaphore *present_ready;
} af_swapchain;

typedef struct af_cluster_residency af_cluster_residency;

typedef struct af_gpu_mesh {
    af_gpu_buffer vertices;
    af_gpu_buffer indices;
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
    af_cluster_residency *residency;
} af_gpu_mesh;

typedef struct af_cached_cluster_page {
    af_gpu_mesh gpu;
    uint32_t region, frame_mask;
    uint64_t last_used, generation;
} af_cached_cluster_page;

typedef struct af_pending_cluster_page {
    af_cluster_mesh mesh;
    af_gpu_mesh gpu;
    VkMemoryRequirements vertices, indices;
    uint32_t region;
    uint64_t last_wanted;
} af_pending_cluster_page;

struct af_cluster_residency {
    af_cluster_stream *stream;
    uint64_t *page_requirements;
    af_pending_cluster_page pending;
    af_cached_cluster_page pages[AF_CLUSTER_RESIDENT_PAGES];
};

typedef struct af_cluster_index_range {
    uint32_t first, count;
    af_cached_cluster_page *page;
} af_cluster_index_range;

typedef struct af_cluster_stream_batch {
    const af_gpu_mesh *mesh;
    af_cluster_span commands;
} af_cluster_stream_batch;

typedef struct af_morph_draw {
    VkDeviceSize offset;
    af_vec3 minimum, maximum;
    af_object_id object;
    uint64_t signature;
} af_morph_draw;

typedef struct af_stream_frame {
    af_gpu_mesh gpu;
    uint64_t revision;
} af_stream_frame;

typedef struct af_mesh_asset {
    char name[AF_MESH_NAME_CAPACITY];
    af_gpu_mesh gpu;
    bool dynamic, retired;
    uint64_t revision;
    af_vertex *cpu_vertices;
    uint32_t *cpu_indices;
    uint32_t vertex_count;
    af_stream_frame streams[AF_FRAMES_IN_FLIGHT];
} af_mesh_asset;

typedef struct af_draw_material {
    af_color tint;
    af_vec4 params, surface, emission;
} af_draw_material;

typedef struct af_mesh_draw {
    af_mat4 model;
    af_draw_material material;
} af_mesh_draw;
_Static_assert(sizeof(af_mesh_draw) == 128, "Portable draw push constant limit");

typedef struct af_prop_batch {
    const af_gpu_mesh *mesh;
    const af_material *material;
    af_mesh_draw draw;
    uint32_t first_object, first_record, count;
} af_prop_batch;

struct af_renderer {
    const af_physics *water_physics;
    af_water water_configs[AF_SCENE_MAX_WATERS];
    af_transform water_transforms[AF_SCENE_MAX_WATERS];
    float water_times[AF_SCENE_MAX_WATERS], water_alphas[AF_SCENE_MAX_WATERS];
    SDL_Window *window;
    const af_scene *previous_scene;
    float interpolation_alpha;
    af_vk_ui *ui;
    af_vk_shadows *shadows, *local_shadows;
    af_vk_ray_query *rays;
    bool ray_supported, ray_failed;
    uint64_t ray_revision, gi_emitter_signature;
    af_ray_instance *ray_inputs;
    size_t ray_input_capacity, ray_raster_casters;
    uint32_t local_shadow_layers, local_shadow_resolution, local_shadow_lights, local_shadow_overflow;
    uint32_t shadow_resolution, shadow_layers;
    af_shadow_draw *shadow_draws;
    af_shadow_bounds *shadow_bounds;
    size_t shadow_capacity, shadow_count, shadow_draw_calls;
    const af_scene *posed_scene;
    const af_mat4 *posed_worlds;
    const af_animation_pose *const *posed_morphs;
    size_t posed_count;
    const af_scene *morph_scene;
    af_morph_draw *morph_draws;
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
    const af_vulkan_upscaler_api *upscaler;
    void *upscaler_state;
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
    af_gpu_buffer capture_buffer;
    af_vec2 capture_position;
    VkExtent2D capture_extent;
    VkFormat capture_format;
    bool capture_supported, capture_pending, capture_ready, capture_motion;
    af_vk_texture *white, *flat_normal;
    af_vk_texture *environment, *irradiance, *brdf;
    uint32_t environment_profile;
    af_environment_asset environments[16];
    size_t environment_count;
    char active_environment[AF_MESH_NAME_CAPACITY];
    af_texture_asset *textures;
    size_t texture_count, texture_capacity, missing_textures;
    af_material_shader_asset *custom_shaders;
    size_t custom_shader_count, custom_shader_capacity;
    af_gpu_light local_light_staging[AF_SCENE_MAX_LOCAL_LIGHTS];
    uint32_t local_light_staging_count;
    af_light_tile_mask *light_tile_staging;
    size_t light_tile_capacity;
    uint32_t light_tile_cols, light_tile_rows;
    VkDescriptorSetLayout frame_layout;
    VkDescriptorPool frame_pool;
    VkInstance instance;
    uint32_t instance_api_version;
    VkDebugUtilsMessengerEXT debug_messenger;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical_device;
    VkDevice device;
    af_vk_allocator *allocator;
    float anisotropy;
    uint32_t timestamp_bits;
    float timestamp_period;
    bool timing_valid;
    double gpu_ms[5];
    double cpu_fence_ms, cpu_acquire_ms, cpu_record_ms;
    uint32_t graphics_family;
    uint32_t present_family;
    VkQueue graphics_queue;
    VkQueue present_queue;
    VkCommandPool command_pool;
    af_frame frames[AF_FRAMES_IN_FLIGHT];
    uint32_t frame_index;
    af_swapchain swapchain;
    af_mesh_asset *meshes;
    uint64_t water_upload_bytes, water_stream_uploads, mesh_idle_waits;
    double water_upload_ms;
    size_t mesh_count;
    size_t mesh_capacity;
    size_t draw_calls;
    size_t missing_meshes;
    af_debug_view debug_view;
    bool culling_disabled, prop_instancing_disabled;
    bool clusters_disabled, cluster_colors, cluster_lod_disabled;
    af_cluster_stats cluster_stats;
    bool cluster_gpu_supported, cluster_gpu_disabled;
    uint32_t cluster_draw_limit, cluster_gpu_candidates, cluster_gpu_visible, cluster_gpu_triangles,
        cluster_gpu_coarse_regions;
    VkDescriptorSetLayout cluster_layout;
    VkDescriptorPool cluster_pool;
    VkPipelineLayout cluster_pipeline_layout;
    VkPipeline cluster_pipeline;
    af_cluster_candidate *cluster_candidates;
    af_mat4 *cluster_matrices;
    af_cluster_span *cluster_spans;
    af_cluster_stream_batch *cluster_stream_batches;
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

/* Inline helpers shared across renderer translation units. */
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

/* Device, swapchain, pipeline & upscaler internals (renderer_device_vulkan.c). */
bool af_r_vk_ok(VkResult result, const char *operation);
bool af_r_build_mesh_pipelines(af_renderer *renderer, const uint32_t *frag_spirv, size_t frag_bytes,
                               VkPipeline *out_opaque, VkPipeline *out_blend);
void af_r_destroy_swapchain(af_renderer *renderer);
bool af_r_create_swapchain(af_renderer *renderer, af_render_settings settings, uint32_t config);
af_rect af_r_resolve_viewport(af_renderer *renderer, af_rect rect);
bool af_r_record_upscaler(af_renderer *r, VkCommandBuffer command, uint32_t image, af_rect area, float exposure,
                          const af_camera *camera, float frame_time_ms, bool reset_history);

/* Buffer, mesh, streaming, skinning & morph internals (renderer_mesh_vulkan.c). */
void af_r_destroy_buffer(af_renderer *renderer, af_gpu_buffer *buffer);
void af_r_destroy_mesh(af_renderer *renderer, af_gpu_mesh *mesh);
bool af_r_create_upload_buffer(af_renderer *renderer, VkDeviceSize size, VkBufferUsageFlags usage,
                               af_gpu_buffer *buffer, VkMemoryRequirements *requirements);
bool af_r_fill_upload_buffer(af_renderer *renderer, const void *data, VkDeviceSize size, VkBufferUsageFlags usage,
                             af_gpu_buffer *buffer, VkMemoryRequirements requirements);
bool af_r_upload_buffer(af_renderer *renderer, const void *data, VkDeviceSize size, VkBufferUsageFlags usage,
                        af_gpu_buffer *buffer);
size_t af_r_find_mesh_index(const af_renderer *renderer, const char *name);
void af_r_stream_destroy(af_renderer *r, af_mesh_asset *a);
bool af_r_stream_buffer(af_renderer *r, af_gpu_buffer *b, const void *data, VkDeviceSize bytes,
                        VkBufferUsageFlags usage);
bool af_r_stream_prepare(af_renderer *r);
void af_r_bind_object_mesh(af_frame *frame, const af_gpu_mesh *mesh, const af_morph_draw *morph);
bool af_r_object_world(af_renderer *renderer, const af_scene *scene, size_t index, af_mat4 *out);
const af_morph_draw *af_r_morph_draw_for(const af_renderer *r, const af_scene *scene, size_t index);
uint32_t af_r_skin_base(const af_renderer *renderer, af_object_id object, const af_gpu_mesh *mesh);
af_shadow_bounds af_r_object_bounds(af_renderer *renderer, af_object_id object, af_mat4 world, const af_gpu_mesh *mesh,
                                    const af_morph_draw *morph);
bool af_r_prepare_morphs(af_renderer *r, const af_scene *scene);

/* Cluster LOD, streaming residency & compute culling internals (renderer_cluster_vulkan.c). */
void af_r_destroy_cluster_compute(af_renderer *r);
void af_r_update_cluster_pages(af_renderer *r);
void af_r_use_cluster_page(af_renderer *r, af_cached_cluster_page *page);
af_cluster_view af_r_cluster_view(const af_frame_uniform *uniform, const af_camera *camera);
size_t af_r_cluster_index_ranges(const af_renderer *r, const af_gpu_mesh *base, af_mat4 model, af_cluster_view view,
                                 af_cluster_index_range *ranges);
bool af_r_prepare_cluster_commands(af_renderer *r, af_frame *frame, const af_scene *scene, af_mat4 projection,
                                   float pixel_scale, size_t objects);
size_t af_r_draw_clusters(af_renderer *r, af_frame *frame, const af_gpu_mesh *mesh, af_mesh_draw draw,
                          af_mat4 visibility, float pixel_scale, uint32_t object);
size_t af_r_draw_indirect_clusters(af_renderer *r, af_frame *frame, const af_gpu_mesh *mesh, af_cluster_span span);

/* Environment & asset internals (renderer_assets_vulkan.c). */
void af_r_update_environment_descriptors(af_renderer *renderer);
bool af_r_create_environment(af_renderer *renderer, uint32_t profile);
VkPipeline af_r_material_pipeline(const af_renderer *renderer, const char *shader_name, bool blend);

#endif

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_MATERIAL_H
#define AFTERLIGHT_MATERIAL_H
#include <stdbool.h>
#include <afterlight/mesh.h>
typedef enum af_alpha_mode { AF_ALPHA_OPAQUE, AF_ALPHA_MASK, AF_ALPHA_BLEND } af_alpha_mode;
typedef struct af_material {
    af_alpha_mode alpha_mode;
    float alpha_cutoff;
    af_color base_color;
    char texture[AF_MESH_NAME_CAPACITY]; /* Empty means white. */
    bool unlit;
    bool cast_shadows, receive_shadows;
    bool physically_based;
    float roughness, metallic;
    float ior; /* Dielectric index of refraction; used by ray-traced material transmission. */
    af_vec3 emission;
    float emission_strength;
    char normal_map[AF_MESH_NAME_CAPACITY]; /* Linear tangent-space RGB; empty means flat. */
    float normal_strength;
    char metallic_roughness_map[AF_MESH_NAME_CAPACITY]; /* Linear glTF G=roughness, B=metallic. */
} af_material;
typedef struct af_directional_light {
    af_vec3 direction; /* Direction the light travels; nonzero. */
    af_vec3 color;     /* Linear RGB. */
    float intensity, ambient;
    float angular_radius; /* Apparent sun radius in radians; 0 selects hard ray shadows. */
} af_directional_light;
#define AF_SCENE_MAX_LOCAL_LIGHTS 16
typedef enum af_light_type { AF_LIGHT_POINT, AF_LIGHT_SPOT } af_light_type;
typedef struct af_local_light {
    char name[64];
    af_light_type type;
    bool enabled;
    af_vec3 position, color;
    float intensity, range;
    af_vec3 direction;
    bool cast_shadows;
    float shadow_bias;              /* Depth comparison bias, [0,0.1]. */
    float inner_angle, outer_angle; /* Radians; 0 <= inner <= outer <= 89 degrees. */
} af_local_light;
af_local_light af_local_light_default(af_light_type type);
typedef enum af_anti_aliasing { AF_AA_OFF, AF_AA_FXAA, AF_AA_SMAA, AF_AA_TAA } af_anti_aliasing;
typedef enum af_debug_view {
    AF_DEBUG_SHADED,
    AF_DEBUG_DEPTH,
    AF_DEBUG_MOTION,
    AF_DEBUG_HISTORY,
    AF_DEBUG_REJECTION,
    AF_DEBUG_COUNT
} af_debug_view;
typedef enum af_tone_mapping {
    AF_TONE_MAP_OFF = 0,
    AF_TONE_MAP_REINHARD = 1,
    AF_TONE_MAP_ACES = 2,
    AF_TONE_MAP_AGX = 3
} af_tone_mapping;
typedef struct af_render_settings {
    bool ray_traced_shadows;           /* Optional hardware ray-query shadows; raster fallback. */
    bool ray_traced_water_reflections; /* Optional single-bounce water reflection. */
    bool ray_traced_material_effects; /* Optional glossy reflections, material transmission and RTAO. */
    bool ray_traced_gi;                /* Optional hardware ray-query diffuse indirect GI. */
    float rtgi_intensity;              /* RTGI indirect bounce intensity multiplier. */
    float rtgi_ray_distance;           /* RTGI max indirect ray trace distance. */
    uint32_t rtgi_samples;             /* RTGI hemisphere sample count (1..4). */
    af_anti_aliasing anti_aliasing;
    float aa_sharpness, taa_history_weight;
    float exposure;
    bool tone_mapping;
    af_tone_mapping tone_mapper;
    bool auto_exposure;
    float auto_exposure_speed, auto_exposure_min, auto_exposure_max;
    float saturation, contrast;
    af_vec3 color_filter;
    bool depth_of_field;
    float dof_focus_distance, dof_focus_range, dof_blur_radius;
    bool motion_blur;
    float motion_blur_strength;
    uint32_t motion_blur_samples;
    bool vignette;
    float vignette_intensity, vignette_smoothness;
    bool chromatic_aberration;
    float chromatic_aberration_strength;
    bool film_grain;
    float film_grain_intensity;
    bool underwater, refraction;
    float refraction_strength;
    bool bloom;
    float bloom_strength, bloom_threshold, bloom_radius;
    uint32_t local_shadow_resolution; /* 256, 512, 1024; zero selects 512. */
    bool environment_enabled;
    uint32_t environment_profile;                  /* 0=studio, 1=sunset, 2=overcast. */
    char environment_asset[AF_MESH_NAME_CAPACITY]; /* Empty: procedural profile. */
    bool skybox;
    float skybox_intensity;                            /* Independent background brightness. */
    float environment_intensity, environment_rotation; /* Linear multiplier; radians. */
} af_render_settings;                                  /* Exposure in stops, [-10,10]. */
af_render_settings af_render_settings_default(void);
af_material af_material_default(void);
af_directional_light af_directional_light_default(void);
#endif

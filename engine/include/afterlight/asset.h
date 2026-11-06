/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_ASSET_H
#define AFTERLIGHT_ASSET_H
#include <stdbool.h>
#include <stddef.h>
#include <afterlight/mesh.h>
#define AF_ASSET_PATH_CAPACITY 4096
#define AF_SCENE_MAX_ASSETS 1024
#define AF_ASSET_FILE_MAX_BYTES (64u*1024u*1024u)
typedef enum af_asset_kind { AF_ASSET_MESH, AF_ASSET_TEXTURE, AF_ASSET_NORMAL_MAP, AF_ASSET_AUDIO, AF_ASSET_METALLIC_ROUGHNESS, AF_ASSET_ENVIRONMENT, AF_ASSET_ANIMATION } af_asset_kind;
typedef struct af_scene_asset {
    char name[AF_MESH_NAME_CAPACITY];
    char path[AF_ASSET_PATH_CAPACITY];
    af_asset_kind kind;
} af_scene_asset;
typedef struct af_texture_info {
    uint32_t width,height,mip_levels;
    uint64_t allocation_bytes;
    float anisotropy;
    bool srgb;
} af_texture_info;
typedef struct af_asset_error { char message[256]; } af_asset_error;
typedef struct af_mesh_file { af_mesh_data data; } af_mesh_file;
typedef struct af_image_file { unsigned char *rgba; uint32_t width,height; } af_image_file;
typedef struct af_hdr_file {float *rgba;uint32_t width,height;} af_hdr_file;
/* Radiance RGBE only; linear RGBA, <=4096x2048 / 128 MiB decoded. */
bool af_hdr_file_load(const char *path,af_hdr_file *out,af_asset_error *error);
void af_hdr_file_free(af_hdr_file *image);
/* CPU-only loaders. Outputs are cleared on failure. Free successful outputs. */
/* Static glTF 2.0/GLB default scene is flattened into one mesh. Node transforms,
   normals, UV0, COLOR0 and base-color factors are retained. Textures/animation
   are not imported; compressed/skin/morph geometry is rejected. */
bool af_asset_is_model_path(const char *path);
bool af_mesh_file_load_gltf(const char *path,af_mesh_file *out,af_asset_error *error);
bool af_mesh_file_load(const char *path,af_mesh_file *out,af_asset_error *error);
bool af_mesh_file_load_obj(const char *path,af_mesh_file *out,af_asset_error *error);
void af_mesh_file_free(af_mesh_file *mesh);
bool af_image_file_load(const char *path,af_image_file *out,af_asset_error *error);
void af_image_file_free(af_image_file *image);
/* Resolve a reference relative to a scene file, or cwd when scene_path is NULL. */
bool af_asset_resolve_path(const char *scene_path,const char *reference,char *out,size_t capacity);
#endif

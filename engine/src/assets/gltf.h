/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_GLTF_H
#define AF_GLTF_H
#include <afterlight/asset.h>
#include <afterlight/animation.h>

typedef enum af_mesh_sidedness {
    AF_MESH_SIDES_DEFAULT,
    AF_MESH_SIDES_FRONT,
    AF_MESH_SIDES_BOTH
} af_mesh_sidedness;

bool af_mesh_file_load_with_sidedness(const char *path, af_mesh_file *out, af_mesh_sidedness *sidedness,
                                     af_asset_error *error);
bool af_animation_gltf_load(const char *path, af_animation_file *out, af_asset_error *error);
#endif

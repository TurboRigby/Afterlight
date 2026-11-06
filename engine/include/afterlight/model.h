/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_MODEL_H
#define AFTERLIGHT_MODEL_H
#include <afterlight/scene.h>
typedef struct af_model_primitive {af_mesh_file mesh;af_material material;int base_image,normal_image,surface_image;unsigned source_mesh,source_primitive;} af_model_primitive;
typedef struct af_model_node {char name[64];af_transform transform;int parent;int mesh;uint32_t source_node;} af_model_node;
typedef struct af_model_file {
    af_model_primitive *primitives;size_t primitive_count;
    af_model_node *nodes;size_t node_count;
    af_image_file *images;size_t image_count;
    char (*dependencies)[AF_ASSET_PATH_CAPACITY];size_t dependency_count;
} af_model_file;
/* Static glTF scene with per-primitive material and node-local TRS. No animation.
   Fails for unsupported UV sets/transforms, alpha blending, skins or shear. */
bool af_model_file_load(const char *path,af_model_file *out,af_asset_error *error);
void af_model_file_free(af_model_file *model);
/* Transactional CPU import; all generated nodes/assets are added or none are. */
bool af_scene_import_model(af_scene *scene,const char *prefix,const char *path,af_object_id *root,af_asset_error *error);
/* Source selectors used by persistent mesh/image manifest entries. */
bool af_model_mesh_load(const char *reference,af_mesh_file *out,af_asset_error *error);
bool af_model_image_load(const char *reference,af_image_file *out,af_asset_error *error);
#endif

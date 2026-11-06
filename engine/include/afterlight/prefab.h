/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_PREFAB_H
#define AFTERLIGHT_PREFAB_H
#include <afterlight/scene.h>
/* Prefabs are single-root scene files. Only the selected hierarchy and its
   referenced assets are saved. Instance IDs/references are remapped atomically.
   Placement replaces the prefab root TRS; children retain their local TRS. */
bool af_prefab_save(const af_scene*,af_object_id,const char *source_scene_path,const char *path,af_scene_error*);
bool af_prefab_instantiate(af_scene*,const char *path,af_object_id parent,af_transform placement,af_object_id *root,af_scene_error*);
#endif

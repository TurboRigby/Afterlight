/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_DOCUMENT_H
#define AF_EDITOR_DOCUMENT_H
#include <afterlight/scene.h>
#include <afterlight/joint.h>
#include <afterlight/compound.h>
#include <afterlight/water.h>

#define ED_PATH_CAPACITY 4096
#define ED_HISTORY_LIMIT 32
#define ED_HISTORY_BYTES (32u * 1024u * 1024u)
typedef struct ed_snapshot { af_scene *scene; af_object_id selection; uint64_t revision; } ed_snapshot;
typedef struct ed_document {
    af_scene *scene;
    af_object_id selection;
    char path[ED_PATH_CAPACITY];
    char status[256];
    uint64_t revision, saved_revision, serial;
    ed_snapshot undo[ED_HISTORY_LIMIT], redo[ED_HISTORY_LIMIT];
    size_t undo_count, redo_count;
    int group_kind;
    af_object_id group_object;
} ed_document;

bool ed_document_init(ed_document *document);
void ed_document_destroy(ed_document *document);
bool ed_document_dirty(const ed_document *document);
bool ed_document_load(ed_document *document, const char *path, bool as_untitled);
bool ed_document_save(ed_document *document, const char *path);
bool ed_document_new(ed_document *document);
void ed_document_end_edit(ed_document *document);
bool ed_document_undo(ed_document *document);
bool ed_document_redo(ed_document *document);
bool ed_document_transform(ed_document *document, af_transform transform);
bool ed_document_name(ed_document *document, const char *name);
bool ed_document_mesh(ed_document *document, const char *mesh);
bool ed_document_visible(ed_document *document, bool visible);
bool ed_document_add_water(ed_document*);
bool ed_document_water(ed_document*,af_water);
bool ed_document_remove_water(ed_document*);
bool ed_document_add_cube(ed_document *document);
bool ed_document_prefab(ed_document*,const char*);
bool ed_document_duplicate(ed_document *document);
bool ed_document_delete(ed_document *document);
bool ed_document_import_mesh(ed_document *document,const char *name,const char *path);
bool ed_document_rig(ed_document*,af_object_id,af_character_rig);
bool ed_document_animation(ed_document *d,af_object_id id,af_animation_component component);
bool ed_document_material(ed_document *document,af_material material);
bool ed_document_light(ed_document *document,af_directional_light light);
bool ed_document_import_texture(ed_document *document,const char *name,const char *path);
bool ed_document_parent(ed_document *document,af_object_id parent);
bool ed_document_shadows(ed_document *document,af_shadow_settings settings);
const char *ed_path_name(const char *path);
bool ed_document_import_environment(ed_document *document,const char *name,const char *path);
bool ed_document_import_surface(ed_document *document,const char *name,const char *path);
bool ed_document_import_sound(ed_document *document,const char *name,const char *path);
bool ed_document_import_normal_map(ed_document *document,const char *name,const char *path);
bool ed_document_wind(ed_document *,af_wind);
bool ed_document_environment(ed_document *,af_directional_light,af_render_settings);
bool ed_document_render_settings(ed_document *document,af_render_settings settings);
bool ed_document_add_light(ed_document *document,af_light_type type);
bool ed_document_local_light(ed_document *document,size_t index,af_local_light light);
bool ed_document_remove_light(ed_document *document,size_t index);
bool ed_document_create_mesh_object(ed_document *document,const char *mesh);
bool ed_document_gameplay(ed_document *document,af_gameplay_component component);
bool ed_document_part(ed_document*,af_collider_part);
bool ed_document_remove_part(ed_document*,af_collider_part_id);
bool ed_document_fit_part(ed_document*,af_collider_part_id);
bool ed_document_joint(ed_document*,af_joint);
bool ed_document_remove_joint(ed_document*,af_joint_id);
bool ed_document_body(ed_document*,af_rigid_body);
bool ed_document_collider(ed_document *document,af_collider collider);
#endif

/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ED_ANIMATION_EDITOR_H
#define ED_ANIMATION_EDITOR_H
#include <afterlight/afterlight.h>

#define ED_ANIMATION_HISTORY 8u
typedef struct ed_animation_snapshot {
    af_animation_file file;
    uint64_t revision;
    size_t bytes;
} ed_animation_snapshot;
typedef struct ed_animation_editor {
    af_animation_file file;
    af_animation_pose *poses;
    af_object_id root;
    size_t clip;
    uint32_t node;
    float time;
    af_vec3 rotation_degrees;
    int interpolation;
    bool open, playing, loop, pose_dirty, close_requested;
    char path[AF_ASSET_PATH_CAPACITY];
    char clip_name[64];
    float clip_duration;
    uint64_t revision, saved_revision, serial;
    ed_animation_snapshot undo[ED_ANIMATION_HISTORY], redo[ED_ANIMATION_HISTORY];
    size_t undo_count, redo_count;
} ed_animation_editor;

bool ed_animation_editor_open(ed_animation_editor *, af_object_id root, const char *path, size_t clip,
                              af_asset_error *);
void ed_animation_editor_destroy(ed_animation_editor *);
bool ed_animation_editor_dirty(const ed_animation_editor *);
void ed_animation_editor_sample(ed_animation_editor *);
void ed_animation_editor_select(ed_animation_editor *, uint32_t node);
bool ed_animation_editor_new_clip(ed_animation_editor *);
bool ed_animation_editor_record(ed_animation_editor *);
bool ed_animation_editor_delete_key(ed_animation_editor *);
bool ed_animation_editor_clip_settings(ed_animation_editor *, const char *name, double duration);
bool ed_animation_editor_undo(ed_animation_editor *, bool redo);
void ed_animation_editor_saved(ed_animation_editor *, const char *path);
#endif

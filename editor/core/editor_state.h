/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EDITOR_STATE_H
#define AF_EDITOR_STATE_H

#include <afterlight/afterlight.h>
#include <afterlight/project.h>
#include <afterlight/asset_watch.h>
#include <afterlight/extension.h>
#include <afterlight/extension_vulkan.h>
#include <afterlight/prefab.h>
#include "document/document.h"
#include "document/environment.h"
#include "content/asset_browser.h"
#include "content/thumbnail.h"
#include "tools/gizmo.h"
#include "tools/light_gizmo.h"
#include "tools/collider_view.h"
#include "tools/joint_gizmo.h"
#include "runtime/runtime.h"
#include "ui/gui.h"
#include "ui/play_hud.h"
#include "ui/dialog.h"
#include "tools/blueprint_editor.h"
#include "core/platform_sdl.h"
#include "render/render_test.h"
#include "cube.h"
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_mouse.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ed_action { ED_NONE, ED_NEW, ED_OPEN, ED_OPEN_PROJECT, ED_CLOSE } ed_action;

enum { ED_DIALOG_EXTENSION = 10 };

typedef struct ext_discovery {
    const char **paths;
    size_t *count;
    char storage[AF_EXTENSION_LIMIT][AF_ASSET_PATH_CAPACITY];
} ext_discovery;

typedef struct editor {
    ed_document document;
    ed_gui gui;
    ed_thumbnails thumbnails;
    ed_gizmo gizmo;
    ed_runtime runtime;
    af_asset_watch *asset_watch;
    bool watch_assets;
    af_project project;
    SDL_Process *tool_process;
    int tool_action;
    bool wizard_test, tool_success, thumbnail_test, light_test;
    unsigned light_test_step;
    af_vec3 light_test_position;
    char wizard_name[128], wizard_folder[AF_ASSET_PATH_CAPACITY], tool_project[AF_ASSET_PATH_CAPACITY];
    char project_path[AF_ASSET_PATH_CAPACITY];
    const char *initial_project;
    bool show_project, show_extensions, module_override, project_is_template;
    char startup_pref[AF_ASSET_PATH_CAPACITY], startup_extension[AF_ASSET_PATH_CAPACITY];
    af_camera edit_camera;
    char module_path[AF_ASSET_PATH_CAPACITY];
    char runtime_status[256];
    af_camera camera;
    af_engine *engine;
    af_rect viewport;
    ed_file_dialog *dialog;
    bool dialog_save;
    int dialog_asset;
    ed_action after_save;
    ed_action pending;
    bool modal;
    bool refresh_fields;
    bool fields_active;
    bool free_camera, resume_game_capture, maximize_play;
    af_collider_part_id edit_part;
    af_collider_part initial_part;
    uint64_t part_drag_revision;
    af_joint_id edit_joint;
    ed_joint_part joint_part;
    af_joint initial_joint;
    uint64_t joint_drag_revision;
    bool invalid_name;
    bool failed;
    bool show_environment, show_post_processing;
    bool show_performance, show_assets, show_colliders, show_input, show_prefab, show_physics;
    char prefab_path[4096];
    char input_path[4096];
    bool environment_pending;
    char environment_name[AF_MESH_NAME_CAPACITY], environment_path[ED_PATH_CAPACITY];
    uint64_t environment_document_revision;
    bool show_animation, animation_preview, show_skeleton;
    af_object_id animation_target;
    float animation_blend;
    const char *animation_test_path;
    unsigned animation_test_step;
    uint64_t animation_test_revision;
    double animation_test_time;
    af_rect animation_buttons[3];
    char asset_search[128], selected_asset[AF_MESH_NAME_CAPACITY];
    int asset_filter;
    size_t selected_light;
    bool light_selected, light_dragging;
    size_t drag_light;
    af_local_light initial_light;
    uint64_t light_drag_revision;
    unsigned light_test_focus;
    uint64_t light_test_focus_losses;
    bool document_ready;
    char name[AF_OBJECT_NAME_CAPACITY];
    char mesh[AF_MESH_NAME_CAPACITY];
    char texture[AF_MESH_NAME_CAPACITY];
    char normal_map[AF_MESH_NAME_CAPACITY];
    char title[512];
    const char *initial_scene;
    bool playground, play_controls_test;
    const char *smoke_path;
    uint64_t frame_limit;
    unsigned updates;
    uint64_t smoke_ticks;
    const char *play_test_failure;
    unsigned play_test_failure_frame, play_test_failure_line;
    unsigned play_test_started, play_test_focus;
    uint64_t play_test_focus_losses;

    /* Landscape editor */
    bool landscape_mode;
    int landscape_subtool;
    af_brush_mode brush_mode;
    af_brush_falloff brush_falloff;
    float brush_radius;
    float brush_strength;
    float brush_target_height;
    size_t active_paint_layer;
    size_t active_foliage_layer;
    char new_foliage_name[AF_LANDSCAPE_NAME_CAPACITY];
    char new_foliage_mesh[AF_MESH_NAME_CAPACITY];
    bool terrain_hit;
    bool landscape_stroke_active;
    af_vec3 terrain_hit_point;
    af_vec3 terrain_hit_normal;

    /* Resizable sidebars */
    float left_sidebar_width;
    float right_sidebar_width;
    bool dragging_left_splitter;
    bool dragging_right_splitter;

    /* Blueprint editor */
    bool blueprint_mode;
    ed_blueprint_state blueprint_state;
} editor;

void reset_camera(editor *app);
bool inside(af_rect r, af_vec2 p);
void set_status(editor *app, const char *text);
void refresh(editor *app);
void stop_play(editor *app);
void toggle_play(editor *app);
void start_dialog(editor *app, bool save);
void start_import(editor *app, af_asset_kind kind);
void start_import_terrain_layer(editor *app, size_t layer_idx, bool normal);
bool load_assets(editor *app);
bool open_project(editor *app, const char *path, bool untitled);
void perform_action(editor *app, ed_action action);
void request_action(editor *app, ed_action action);
void save_document(editor *app, bool save_as, ed_action after);
void poll_dialog(editor *app);
void select_object(editor *app, af_object_id id);
void select_light(editor *app, size_t index);
void focus_selection(editor *app);
nk_flags panel_flags(editor *app);
void label(struct nk_context *context, const char *text, float height);
bool vector_fields(struct nk_context *context, const char *title, af_vec3 *value, float step, float increment);

#ifdef __cplusplus
}
#endif

#endif /* AF_EDITOR_STATE_H */

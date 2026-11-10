/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "hierarchy.h"
#include "tools/landscape_editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void object_rows(editor *app, af_object_id parent, unsigned depth) {
    if (depth >= AF_SCENE_MAX_DEPTH)
        return;
    struct nk_context *context = &app->gui.context;
    size_t count = af_scene_object_count(app->document.scene);
    for (size_t i = 0; i < count; ++i) {
        af_scene_object object = *af_scene_object_at(app->document.scene, i);
        if (object.parent != parent)
            continue;
        float indent = fminf((float)depth * 12, 100);
        nk_layout_row_begin(context, NK_STATIC, 34, 3);
        nk_layout_row_push(context, 22);
        nk_bool visible = object.visible;
        if (nk_checkbox_label(context, "", &visible)) {
            af_object_id previous = app->document.selection;
            app->document.selection = object.id;
            ed_document_visible(&app->document, visible != 0);
            app->document.selection = previous;
        }
        nk_layout_row_push(context, indent);
        nk_spacing(context, 1);
        float name_w = fmaxf(100.0f, app->left_sidebar_width - 88.0f - indent);
        nk_layout_row_push(context, name_w);
        if (nk_select_label(context, object.name, NK_TEXT_LEFT, app->document.selection == object.id))
            select_object(app, object.id);
        nk_layout_row_end(context);
        object_rows(app, object.id, depth + 1);
    }
}

void object_list(editor *app, float height) {
    struct nk_context *context = &app->gui.context;
    if (nk_begin(context, "Objects", nk_rect(0, 64, app->left_sidebar_width, height - 96),
                 panel_flags(app) | NK_WINDOW_NO_SCROLLBAR)) {
        nk_style_push_vec2(context, &context->style.button.padding, nk_vec2(2, 2));
        nk_layout_row_dynamic(context, 26, 3);
        const char *landscape_title;
        if (app->left_sidebar_width >= 270.0f) {
            landscape_title = app->landscape_mode ? "[Landscape]" : "Landscape";
        } else {
            landscape_title = app->landscape_mode ? "[Terrain]" : "Terrain";
        }
        if (nk_button_label(context, (!app->landscape_mode && !app->blueprint_mode) ? "[Objects]" : "Objects")) {
            app->landscape_mode = false;
            app->blueprint_mode = false;
        }
        if (nk_button_label(context, landscape_title)) {
            app->landscape_mode = true;
            app->blueprint_mode = false;
            app->document.selection = AF_OBJECT_NONE;
            app->light_selected = false;
        }
        if (nk_button_label(context, app->blueprint_mode ? "[Scripts]" : "Scripts")) {
            app->blueprint_mode = true;
            app->landscape_mode = false;
            app->document.selection = AF_OBJECT_NONE;
            app->light_selected = false;
        }
        nk_style_pop_vec2(context);
        if (app->blueprint_mode) {
            ed_blueprint_sidebar_panel(&app->blueprint_state, context,
                                       app->runtime.playing ? app->runtime.scene : app->document.scene, height,
                                       app->runtime.playing);
            nk_end(context);
            return;
        }
        if (app->landscape_mode) {
            landscape_panel(app, height);
            nk_end(context);
            return;
        }
        size_t obj_count = af_scene_object_count(app->document.scene);
        size_t light_count = af_scene_local_light_count(app->document.scene);
        char title[64];
        snprintf(title, sizeof(title), "Objects  /  %zu", obj_count + light_count);
        label(context, title, 28);
        nk_layout_row_dynamic(context, 32, 2);
        if (nk_combo_begin_label(context, "+ Add Object", nk_vec2(220, 200))) {
            nk_layout_row_dynamic(context, 26, 1);
            if (nk_combo_item_label(context, "Cube (Mesh)", NK_TEXT_LEFT)) {
                ed_document_add_cube(&app->document);
                refresh(app);
            }
            if (nk_combo_item_label(context, "Point Light", NK_TEXT_LEFT)) {
                if (ed_document_add_light(&app->document, AF_LIGHT_POINT)) {
                    select_light(app, af_scene_local_light_count(app->document.scene) - 1);
                }
            }
            if (nk_combo_item_label(context, "Spot Light", NK_TEXT_LEFT)) {
                if (ed_document_add_light(&app->document, AF_LIGHT_SPOT)) {
                    select_light(app, af_scene_local_light_count(app->document.scene) - 1);
                }
            }
            if (nk_combo_item_label(context, "Water Plane", NK_TEXT_LEFT)) {
                ed_document_add_water(&app->document);
                refresh(app);
            }
            if (nk_combo_item_label(context, "Import 3D Model...", NK_TEXT_LEFT)) {
                start_import(app, AF_ASSET_MESH);
            }
            if (nk_combo_item_label(context, "Choose C Module...", NK_TEXT_LEFT)) {
                app->dialog = ed_dialog_begin_module(app->engine);
                app->dialog_asset = 3;
                app->dialog_save = false;
            }
            nk_combo_end(context);
        }
        if (nk_button_label(context, "Duplicate")) {
            if (app->light_selected && app->selected_light < af_scene_local_light_count(app->document.scene)) {
                af_local_light dup = *af_scene_local_light_at(app->document.scene, app->selected_light);
                dup.position.x += 1.0f;
                char copy_name[AF_OBJECT_NAME_CAPACITY + 16];
                snprintf(copy_name, sizeof(copy_name), "%.*s Copy", (int)(sizeof(dup.name) - 6), dup.name);
                snprintf(dup.name, sizeof(dup.name), "%s", copy_name);
                if (ed_document_add_light(&app->document, dup.type)) {
                    size_t new_idx = af_scene_local_light_count(app->document.scene) - 1;
                    ed_document_local_light(&app->document, new_idx, dup);
                    select_light(app, new_idx);
                }
            } else {
                ed_document_duplicate(&app->document);
                refresh(app);
            }
        }
        nk_layout_row_dynamic(context, fmaxf(60, height - 268), 1);
        if (nk_group_begin(context, "Object list", 0)) {
            if (!obj_count && !light_count) {
                nk_layout_row_dynamic(context, 50, 1);
                nk_label_wrap(context, "No objects yet. Click '+ Add Object' to begin.");
            }
            object_rows(app, AF_OBJECT_NONE, 0);
            for (size_t i = 0; i < light_count; ++i) {
                const af_local_light *light = af_scene_local_light_at(app->document.scene, i);
                nk_layout_row_begin(context, NK_STATIC, 34, 3);
                nk_layout_row_push(context, 22);
                nk_bool enabled = light->enabled;
                if (nk_checkbox_label(context, "", &enabled)) {
                    af_local_light mod = *light;
                    mod.enabled = enabled != 0;
                    ed_document_local_light(&app->document, i, mod);
                }
                nk_layout_row_push(context, 0);
                nk_spacing(context, 1);
                float light_w = fmaxf(100.0f, app->left_sidebar_width - 88.0f);
                nk_layout_row_push(context, light_w);
                char label_buf[AF_OBJECT_NAME_CAPACITY + 16];
                snprintf(label_buf, sizeof(label_buf), "[%s] %s", light->type == AF_LIGHT_SPOT ? "Spot" : "Point",
                         light->name);
                bool is_selected = app->light_selected && app->selected_light == i;
                if (nk_select_label(context, label_buf, NK_TEXT_LEFT, is_selected))
                    select_light(app, i);
                nk_layout_row_end(context);
            }
            nk_group_end(context);
        }
        nk_layout_row_dynamic(context, 32, 1);
        if (nk_button_label(context, "Delete selected")) {
            if (app->light_selected && app->selected_light < af_scene_local_light_count(app->document.scene)) {
                ed_document_remove_light(&app->document, app->selected_light);
                app->light_selected = false;
                app->gizmo.dragging = false;
            } else {
                ed_document_delete(&app->document);
                refresh(app);
            }
        }
    }
    nk_end(context);
}

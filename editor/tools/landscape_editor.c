/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "landscape_editor.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void landscape_panel(editor *app, float height) {
    struct nk_context *context = &app->gui.context;
    af_landscape *ls = af_scene_get_landscape_mut(app->document.scene);

    if (!ls) {
        label(context, "No Landscape", 26);
        nk_layout_row_dynamic(context, 48, 1);
        nk_label_wrap(context, "No terrain in this scene. Create one to begin sculpting and painting.");
        static int create_cx = 4;
        static int create_cz = 4;
        static float create_spacing = 1.0f;
        nk_layout_row_dynamic(context, 24, 1);
        nk_property_int(context, "Chunks X", 1, &create_cx, 16, 1, 1);
        nk_property_int(context, "Chunks Z", 1, &create_cz, 16, 1, 1);
        nk_property_float(context, "Spacing (m)", 0.25f, &create_spacing, 4.0f, 0.25f, 0.25f);
        nk_layout_row_dynamic(context, 32, 1);
        if (nk_button_label(context, "+ Create Landscape")) {
            af_landscape *new_ls = af_landscape_create((uint32_t)create_cx, (uint32_t)create_cz, create_spacing);
            if (new_ls) {
                af_scene_set_landscape(app->document.scene, new_ls);
                ++app->document.revision;
                refresh(app);
            }
        }
        return;
    }

    /* Subtool tabs: [ Sculpt ] [ Layers ] / [ Foliage ] [ Manage ] */
    nk_style_push_vec2(context, &context->style.button.padding, nk_vec2(4, 2));
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, app->landscape_subtool == 0 ? "[ Sculpt ]" : "Sculpt"))
        app->landscape_subtool = 0;
    if (nk_button_label(context, app->landscape_subtool == 1 ? "[ Layers ]" : "Layers"))
        app->landscape_subtool = 1;
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, app->landscape_subtool == 2 ? "[ Foliage ]" : "Foliage"))
        app->landscape_subtool = 2;
    if (nk_button_label(context, app->landscape_subtool == 3 ? "[ Manage ]" : "Manage"))
        app->landscape_subtool = 3;
    nk_style_pop_vec2(context);

    nk_layout_row_dynamic(context, fmaxf(60, height - 238), 1);
    if (nk_group_begin(context, "Landscape controls", 0)) {
        if (app->landscape_subtool == 0) {
            /* Sculpt */
            label(context, "Sculpt Tool", 24);
            nk_layout_row_dynamic(context, 26, 2);
            if (nk_button_label(context, app->brush_mode == AF_BRUSH_SCULPT_RAISE ? "[ Raise ]" : "Raise"))
                app->brush_mode = AF_BRUSH_SCULPT_RAISE;
            if (nk_button_label(context, app->brush_mode == AF_BRUSH_SCULPT_LOWER ? "[ Lower ]" : "Lower"))
                app->brush_mode = AF_BRUSH_SCULPT_LOWER;
            if (nk_button_label(context, app->brush_mode == AF_BRUSH_SCULPT_SMOOTH ? "[ Smooth ]" : "Smooth"))
                app->brush_mode = AF_BRUSH_SCULPT_SMOOTH;
            if (nk_button_label(context, app->brush_mode == AF_BRUSH_SCULPT_FLATTEN ? "[ Flatten ]" : "Flatten"))
                app->brush_mode = AF_BRUSH_SCULPT_FLATTEN;

            nk_layout_row_dynamic(context, 24, 1);
            nk_property_float(context, "Radius (m)", 1.0f, &app->brush_radius, 50.0f, 0.5f, 0.5f);
            nk_property_float(context, "Strength", 0.05f, &app->brush_strength, 2.0f, 0.05f, 0.05f);

            if (app->brush_mode == AF_BRUSH_SCULPT_FLATTEN) {
                nk_property_float(context, "Target Height", -50.0f, &app->brush_target_height, 100.0f, 0.5f, 0.5f);
                if (nk_button_label(context, "Pick Height Under Cursor")) {
                    if (app->terrain_hit)
                        app->brush_target_height = app->terrain_hit_point.y;
                }
            }

            label(context, "Falloff Curve", 22);
            static const char *const falloff_names[] = {"Smooth", "Linear", "Spherical", "Flat"};
            int cur_falloff = (int)app->brush_falloff;
            int next_falloff = nk_combo(context, falloff_names, 4, cur_falloff, 24, nk_vec2(200, 120));
            if (next_falloff != cur_falloff)
                app->brush_falloff = (af_brush_falloff)next_falloff;

            nk_layout_row_dynamic(context, 18, 1);
            nk_label(context, "Hold LMB to sculpt terrain.", NK_TEXT_LEFT);
            nk_layout_row_dynamic(context, 18, 1);
            nk_label(context, "Hold Shift to invert raise/lower.", NK_TEXT_LEFT);
        } else if (app->landscape_subtool == 1) {
            /* Material Layers */
            label(context, "Terrain Layers", 24);
            static const char *layer_labels[4] = {"1: Grass", "2: Dirt", "3: Rock", "4: Sand"};
            for (size_t i = 0; i < AF_LANDSCAPE_MAX_LAYERS; ++i) {
                nk_layout_row_dynamic(context, 26, 1);
                char btn_text[64];
                snprintf(btn_text, sizeof(btn_text), "%s %s",
                         (app->active_paint_layer == i && app->brush_mode == AF_BRUSH_PAINT_LAYER) ? "[Paint]"
                                                                                                   : "Select",
                         layer_labels[i]);
                if (nk_button_label(context, btn_text)) {
                    app->active_paint_layer = i;
                    app->brush_mode = AF_BRUSH_PAINT_LAYER;
                }
            }

            label(context, "Layer Settings", 22);
            af_terrain_layer *l = &ls->layers[app->active_paint_layer];

            label(context, "Layer Name", 20);
            nk_layout_row_dynamic(context, 26, 1);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, l->name, sizeof(l->name), nk_filter_default);

            label(context, "Diffuse Texture", 22);
            nk_layout_row_dynamic(context, 26, 1);
            const char *diff_combo_title = l->diffuse_texture[0] ? l->diffuse_texture : "[Select Scene Texture]";
            if (nk_combo_begin_label(context, diff_combo_title, nk_vec2(220, 200))) {
                nk_layout_row_dynamic(context, 24, 1);
                if (nk_combo_item_label(context, "[None (Solid Color)]", NK_TEXT_LEFT)) {
                    if (l->diffuse_texture[0] != '\0') {
                        ed_document_push_undo(&app->document);
                        ls = af_scene_get_landscape_mut(app->document.scene);
                        l = &ls->layers[app->active_paint_layer];
                        l->diffuse_texture[0] = '\0';
                        for (size_t c = 0; c < ls->chunk_count; ++c)
                            ls->chunks[c].dirty_mesh = true;
                        ls->modification_version++;
                        refresh(app);
                    }
                }
                for (size_t a = 0; a < af_scene_asset_count(app->document.scene); ++a) {
                    const af_scene_asset *asset = af_scene_asset_at(app->document.scene, a);
                    if (asset->kind == AF_ASSET_TEXTURE) {
                        if (nk_combo_item_label(context, asset->name, NK_TEXT_LEFT)) {
                            if (strcmp(l->diffuse_texture, asset->name) != 0) {
                                ed_document_push_undo(&app->document);
                                ls = af_scene_get_landscape_mut(app->document.scene);
                                l = &ls->layers[app->active_paint_layer];
                                snprintf(l->diffuse_texture, sizeof(l->diffuse_texture), "%s", asset->name);
                                l->color = (af_color){1.0f, 1.0f, 1.0f, 1.0f};
                                for (size_t c = 0; c < ls->chunk_count; ++c)
                                    ls->chunks[c].dirty_mesh = true;
                                ls->modification_version++;
                                refresh(app);
                            }
                        }
                    }
                }
                nk_combo_end(context);
            }

            nk_layout_row_dynamic(context, 26, 1);
            char prev_diffuse[AF_ASSET_PATH_CAPACITY];
            snprintf(prev_diffuse, sizeof(prev_diffuse), "%s", l->diffuse_texture);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, l->diffuse_texture, sizeof(l->diffuse_texture),
                                           nk_filter_default);
            if (strcmp(prev_diffuse, l->diffuse_texture) != 0) {
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
                ++app->document.revision;
            }

            nk_layout_row_dynamic(context, 28, 1);
            if (nk_button_label(context, "Import Texture File...")) {
                start_import_terrain_layer(app, app->active_paint_layer, false);
            }

            label(context, "Normal Map", 22);
            nk_layout_row_dynamic(context, 26, 1);
            const char *norm_combo_title = l->normal_texture[0] ? l->normal_texture : "[Select Normal Map]";
            if (nk_combo_begin_label(context, norm_combo_title, nk_vec2(220, 200))) {
                nk_layout_row_dynamic(context, 24, 1);
                if (nk_combo_item_label(context, "[None (Flat Normal)]", NK_TEXT_LEFT)) {
                    if (l->normal_texture[0] != '\0') {
                        ed_document_push_undo(&app->document);
                        ls = af_scene_get_landscape_mut(app->document.scene);
                        l = &ls->layers[app->active_paint_layer];
                        l->normal_texture[0] = '\0';
                        for (size_t c = 0; c < ls->chunk_count; ++c)
                            ls->chunks[c].dirty_mesh = true;
                        ls->modification_version++;
                        refresh(app);
                    }
                }
                for (size_t a = 0; a < af_scene_asset_count(app->document.scene); ++a) {
                    const af_scene_asset *asset = af_scene_asset_at(app->document.scene, a);
                    if (asset->kind == AF_ASSET_NORMAL_MAP) {
                        if (nk_combo_item_label(context, asset->name, NK_TEXT_LEFT)) {
                            if (strcmp(l->normal_texture, asset->name) != 0) {
                                ed_document_push_undo(&app->document);
                                ls = af_scene_get_landscape_mut(app->document.scene);
                                l = &ls->layers[app->active_paint_layer];
                                snprintf(l->normal_texture, sizeof(l->normal_texture), "%s", asset->name);
                                for (size_t c = 0; c < ls->chunk_count; ++c)
                                    ls->chunks[c].dirty_mesh = true;
                                ls->modification_version++;
                                refresh(app);
                            }
                        }
                    }
                }
                nk_combo_end(context);
            }

            nk_layout_row_dynamic(context, 26, 1);
            char prev_normal[AF_ASSET_PATH_CAPACITY];
            snprintf(prev_normal, sizeof(prev_normal), "%s", l->normal_texture);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, l->normal_texture, sizeof(l->normal_texture),
                                           nk_filter_default);
            if (strcmp(prev_normal, l->normal_texture) != 0) {
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
                ++app->document.revision;
            }

            nk_layout_row_dynamic(context, 28, 1);
            if (nk_button_label(context, "Import Normal Map File...")) {
                start_import_terrain_layer(app, app->active_paint_layer, true);
            }

            label(context, "Layer Material", 22);
            nk_layout_row_dynamic(context, 24, 1);
            float prev_scale = l->uv_scale;
            nk_property_float(context, "UV Scale", 0.05f, &l->uv_scale, 20.0f, 0.1f, 0.05f);
            if (fabsf(l->uv_scale - prev_scale) > 1e-5f) {
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
                ++app->document.revision;
            }

            float prev_roughness = l->roughness;
            float prev_metalness = l->metalness;
            nk_property_float(context, "Roughness", 0.0f, &l->roughness, 1.0f, 0.05f, 0.05f);
            nk_property_float(context, "Metalness", 0.0f, &l->metalness, 1.0f, 0.05f, 0.05f);
            if (fabsf(l->roughness - prev_roughness) > 1e-5f || fabsf(l->metalness - prev_metalness) > 1e-5f) {
                ls->modification_version++;
                ++app->document.revision;
            }

            label(context, "Layer Color & Tint", 22);
            nk_layout_row_dynamic(context, 22, 1);
            af_color prev_col = l->color;
            nk_property_float(context, "Red", 0.0f, &l->color.r, 1.0f, 0.02f, 0.02f);
            nk_property_float(context, "Green", 0.0f, &l->color.g, 1.0f, 0.02f, 0.02f);
            nk_property_float(context, "Blue", 0.0f, &l->color.b, 1.0f, 0.02f, 0.02f);
            if (fabsf(l->color.r - prev_col.r) > 1e-5f || fabsf(l->color.g - prev_col.g) > 1e-5f ||
                fabsf(l->color.b - prev_col.b) > 1e-5f) {
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
                ++app->document.revision;
            }

            nk_layout_row_dynamic(context, 26, 2);
            if (nk_button_label(context, "Reset (White)")) {
                ed_document_push_undo(&app->document);
                ls = af_scene_get_landscape_mut(app->document.scene);
                l = &ls->layers[app->active_paint_layer];
                l->color = (af_color){1.0f, 1.0f, 1.0f, 1.0f};
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
                refresh(app);
            }
            if (nk_button_label(context, "Reset (Default)")) {
                ed_document_push_undo(&app->document);
                ls = af_scene_get_landscape_mut(app->document.scene);
                l = &ls->layers[app->active_paint_layer];
                af_terrain_layer def = af_terrain_layer_default(app->active_paint_layer);
                l->color = def.color;
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
                refresh(app);
            }

            label(context, "Brush & Painting", 22);
            nk_layout_row_dynamic(context, 24, 1);
            nk_property_float(context, "Radius (m)", 1.0f, &app->brush_radius, 50.0f, 0.5f, 0.5f);
            nk_property_float(context, "Strength", 0.05f, &app->brush_strength, 2.0f, 0.05f, 0.05f);

            label(context, "Auto-Slope Texturing", 22);
            nk_layout_row_dynamic(context, 30, 1);
            if (nk_button_label(context, "Apply Slope Auto-Texture")) {
                ed_document_push_undo(&app->document);
                ls = af_scene_get_landscape_mut(app->document.scene);
                af_landscape_auto_paint_slopes(ls, 2, 0.6f, 3, 0.5f);
                refresh(app);
            }

            nk_layout_row_dynamic(context, 18, 1);
            nk_label(context, "Hold LMB in viewport to paint.", NK_TEXT_LEFT);
        } else if (app->landscape_subtool == 2) {
            /* Foliage / Mesh Painting */
            label(context, "Foliage Layers", 24);
            nk_layout_row_dynamic(context, 26, 2);
            if (nk_button_label(context, app->brush_mode == AF_BRUSH_PAINT_FOLIAGE ? "[ Paint ]" : "Paint"))
                app->brush_mode = AF_BRUSH_PAINT_FOLIAGE;
            if (nk_button_label(context, app->brush_mode == AF_BRUSH_ERASE_FOLIAGE ? "[ Erase ]" : "Erase"))
                app->brush_mode = AF_BRUSH_ERASE_FOLIAGE;

            for (size_t i = 0; i < ls->foliage_layer_count; ++i) {
                af_foliage_layer *fl = &ls->foliage_layers[i];
                nk_layout_row_begin(context, NK_STATIC, 26, 3);
                nk_layout_row_push(context, 22);
                nk_bool vis = fl->visible;
                if (nk_checkbox_label(context, "", &vis)) {
                    fl->visible = vis != 0;
                    ++app->document.revision;
                }
                float fol_w = fmaxf(80.0f, app->left_sidebar_width - 122.0f);
                nk_layout_row_push(context, fol_w);
                char layer_title[64];
                snprintf(layer_title, sizeof(layer_title), "%s (%zu)", fl->name, fl->total_instances);
                if (nk_select_label(context, layer_title, NK_TEXT_LEFT, app->active_foliage_layer == i)) {
                    app->active_foliage_layer = i;
                }
                nk_layout_row_push(context, 40);
                if (nk_button_label(context, "X")) {
                    ed_document_push_undo(&app->document);
                    ls = af_scene_get_landscape_mut(app->document.scene);
                    af_landscape_remove_foliage_layer(ls, i);
                    if (app->active_foliage_layer >= ls->foliage_layer_count && ls->foliage_layer_count > 0)
                        app->active_foliage_layer = ls->foliage_layer_count - 1;
                    refresh(app);
                    break;
                }
                nk_layout_row_end(context);
            }

            /* Add Layer */
            label(context, "Add Foliage Mesh", 22);
            nk_layout_row_dynamic(context, 24, 1);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, app->new_foliage_name, sizeof(app->new_foliage_name),
                                           nk_filter_default);
            nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, app->new_foliage_mesh, sizeof(app->new_foliage_mesh),
                                           nk_filter_default);
            nk_layout_row_dynamic(context, 28, 1);
            if (nk_button_label(context, "+ Add Foliage Layer")) {
                if (app->new_foliage_mesh[0]) {
                    ed_document_push_undo(&app->document);
                    ls = af_scene_get_landscape_mut(app->document.scene);
                    int added = af_landscape_add_foliage_layer(
                        ls, app->new_foliage_name[0] ? app->new_foliage_name : "Foliage", app->new_foliage_mesh);
                    if (added >= 0) {
                        app->active_foliage_layer = (size_t)added;
                    }
                    refresh(app);
                }
            }

            if (ls->foliage_layer_count > 0 && app->active_foliage_layer < ls->foliage_layer_count) {
                af_foliage_layer *fl = &ls->foliage_layers[app->active_foliage_layer];
                label(context, "Placement Rules", 22);
                nk_layout_row_dynamic(context, 24, 1);
                nk_property_float(context, "Brush Radius (m)", 1.0f, &app->brush_radius, 50.0f, 0.5f, 0.5f);
                nk_property_float(context, "Density", 0.1f, &fl->rules.density, 10.0f, 0.1f, 0.1f);
                nk_property_float(context, "Scale Min", 0.1f, &fl->rules.scale_min.x, 5.0f, 0.05f, 0.05f);
                fl->rules.scale_min.y = fl->rules.scale_min.z = fl->rules.scale_min.x;
                nk_property_float(context, "Scale Max", 0.1f, &fl->rules.scale_max.x, 5.0f, 0.05f, 0.05f);
                fl->rules.scale_max.y = fl->rules.scale_max.z = fl->rules.scale_max.x;

                nk_bool rand_yaw = fl->rules.random_yaw;
                if (nk_checkbox_label(context, "Random Yaw (0-360°)", &rand_yaw))
                    fl->rules.random_yaw = rand_yaw != 0;

                nk_bool rand_tilt = fl->rules.random_pitch_roll;
                if (nk_checkbox_label(context, "Random Tilt", &rand_tilt))
                    fl->rules.random_pitch_roll = rand_tilt != 0;
                if (fl->rules.random_pitch_roll) {
                    nk_property_float(context, "Max Tilt (rad)", 0.0f, &fl->rules.max_tilt, 0.78f, 0.02f, 0.02f);
                }

                nk_property_float(context, "Normal Align", 0.0f, &fl->rules.align_to_normal, 1.0f, 0.05f, 0.05f);
                nk_property_float(context, "Max Slope (rad)", 0.1f, &fl->rules.slope_max, 1.57f, 0.05f, 0.05f);
                nk_property_float(context, "Max Distance (m)", 20.0f, &fl->max_distance, 500.0f, 10.0f, 10.0f);

                nk_bool shadow = fl->cast_shadows;
                if (nk_checkbox_label(context, "Cast Shadows", &shadow))
                    fl->cast_shadows = shadow != 0;

                nk_bool col_avoid = fl->rules.collision_avoidance;
                if (nk_checkbox_label(context, "Collision Avoidance", &col_avoid))
                    fl->rules.collision_avoidance = col_avoid != 0;
                if (fl->rules.collision_avoidance) {
                    nk_property_float(context, "Min Spacing (m)", 0.5f, &fl->rules.min_spacing, 20.0f, 0.5f, 0.5f);
                }

                label(context, "Foliage Actions", 22);
                nk_layout_row_dynamic(context, 28, 1);
                if (nk_button_label(context, "Populate Procedurally")) {
                    ed_document_push_undo(&app->document);
                    ls = af_scene_get_landscape_mut(app->document.scene);
                    static uint32_t pop_seed = 42;
                    pop_seed += 13;
                    af_landscape_populate_procedural_foliage(ls, app->active_foliage_layer, pop_seed);
                    refresh(app);
                }
                if (nk_button_label(context, "Clear Layer Instances")) {
                    ed_document_push_undo(&app->document);
                    ls = af_scene_get_landscape_mut(app->document.scene);
                    af_landscape_clear_foliage(ls, (int)app->active_foliage_layer);
                    refresh(app);
                }
            }

            nk_layout_row_dynamic(context, 18, 1);
            nk_label(context, "Hold LMB to paint instances.", NK_TEXT_LEFT);
            nk_layout_row_dynamic(context, 18, 1);
            nk_label(context, "Hold Shift to erase instances.", NK_TEXT_LEFT);
        } else if (app->landscape_subtool == 3) {
            /* Manage / Heightmaps */
            label(context, "Procedural Generation", 22);
            static float hill_size = 45.0f;
            static float hill_amp = 8.0f;
            static int hill_seed = 1234;
            nk_layout_row_dynamic(context, 24, 1);
            nk_property_float(context, "Hill Size (m)", 15.0f, &hill_size, 150.0f, 2.0f, 2.0f);
            nk_property_float(context, "Height (m)", 1.0f, &hill_amp, 40.0f, 0.5f, 0.5f);
            nk_property_int(context, "Seed", 0, &hill_seed, 99999, 1, 1);

            nk_layout_row_dynamic(context, 28, 1);
            if (nk_button_label(context, "Generate Hills")) {
                ed_document_push_undo(&app->document);
                ls = af_scene_get_landscape_mut(app->document.scene);
                af_landscape_generate_procedural_hills(ls, hill_size, hill_amp, (uint32_t)hill_seed);
                refresh(app);
            }

            label(context, "Terrain Actions", 22);
            nk_layout_row_dynamic(context, 28, 1);
            if (nk_button_label(context, "Flatten Entire Terrain")) {
                ed_document_push_undo(&app->document);
                ls = af_scene_get_landscape_mut(app->document.scene);
                for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
                    af_terrain_chunk *c = &ls->chunks[c_idx];
                    memset(c->heights, 0, sizeof(c->heights));
                    c->dirty_mesh = true;
                }
                af_landscape_recompute_normals(ls, 0, 0, (int32_t)ls->chunk_count_x - 1,
                                               (int32_t)ls->chunk_count_z - 1);
                refresh(app);
            }
            if (nk_button_label(context, "Clear All Foliage")) {
                ed_document_push_undo(&app->document);
                ls = af_scene_get_landscape_mut(app->document.scene);
                af_landscape_clear_foliage(ls, -1);
                refresh(app);
            }
            if (nk_button_label(context, "Delete Landscape")) {
                ed_document_push_undo(&app->document);
                af_scene_remove_landscape(app->document.scene);
                refresh(app);
            }
        }
        nk_group_end(context);
    }
}

void draw_landscape_brush(editor *app) {
    if (!app->landscape_mode || !app->terrain_hit || app->runtime.playing || app->blueprint_mode)
        return;
    struct nk_context *ctx = &app->gui.context;
    af_rect r = app->viewport;
    const af_landscape *ls = af_scene_get_landscape(app->document.scene);
    if (!ls || !ls->enabled)
        return;

    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_color(nk_rgba(0, 0, 0, 0)));
    if (nk_begin(ctx, "Landscape Brush", nk_rect(r.x, r.y, r.width, r.height),
                 NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_BACKGROUND)) {
        struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
        const int SEGMENTS = 36;
        af_vec3 center = app->terrain_hit_point;
        float radius = app->brush_radius;
        af_vec2 prev_pt = {0};
        bool prev_valid = false;
        bool shift = af_engine_key_down(app->engine, AF_KEY_LEFT_SHIFT);
        struct nk_color ring_col =
            (app->brush_mode == AF_BRUSH_ERASE_FOLIAGE || (app->brush_mode == AF_BRUSH_SCULPT_LOWER) || shift)
                ? nk_rgb(255, 85, 85)
                : nk_rgb(85, 225, 255);

        for (int i = 0; i <= SEGMENTS; ++i) {
            float angle = (float)i / (float)SEGMENTS * 2.0f * AF_PI;
            float rx = center.x + cosf(angle) * radius;
            float rz = center.z + sinf(angle) * radius;
            float ry = af_landscape_get_height(ls, rx, rz) + 0.08f;
            af_vec2 screen_pt;
            if (ed_project(&app->camera, r, (af_vec3){rx, ry, rz}, &screen_pt)) {
                if (prev_valid) {
                    nk_stroke_line(canvas, prev_pt.x, prev_pt.y, screen_pt.x, screen_pt.y, 2.0f, ring_col);
                }
                prev_pt = screen_pt;
                prev_valid = true;
            } else {
                prev_valid = false;
            }
        }

        af_vec2 center_screen;
        if (ed_project(&app->camera, r, (af_vec3){center.x, center.y + 0.05f, center.z}, &center_screen)) {
            nk_fill_circle(canvas, nk_rect(center_screen.x - 3, center_screen.y - 3, 6, 6), ring_col);
        }
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
}

void landscape_update_stroke(editor *app, double delta, af_vec2 mouse, bool shift) {
    af_engine *engine = app->engine;
    af_landscape *ls = af_scene_get_landscape_mut(app->document.scene);
    if (!ls)
        return;

    if (!app->runtime.playing && inside(app->viewport, mouse) && !af_engine_mouse_captured(engine)) {
        bool lmb_down = af_engine_mouse_down(engine, AF_MOUSE_LEFT);
        if (app->terrain_hit && lmb_down) {
            if (!app->landscape_stroke_active) {
                app->landscape_stroke_active = true;
                ed_document_push_undo(&app->document);
                ls = af_scene_get_landscape_mut(app->document.scene);
            }
            float strength = app->brush_strength * (float)delta * 5.0f;
            if (strength > 2.0f)
                strength = 2.0f;
            if (strength < 0.001f)
                strength = 0.001f;

            if (app->landscape_subtool == 0) {
                af_brush_mode mode = app->brush_mode;
                if (shift) {
                    if (mode == AF_BRUSH_SCULPT_RAISE)
                        mode = AF_BRUSH_SCULPT_LOWER;
                    else if (mode == AF_BRUSH_SCULPT_LOWER)
                        mode = AF_BRUSH_SCULPT_RAISE;
                }
                if (mode == AF_BRUSH_SCULPT_RAISE) {
                    af_landscape_sculpt_raise_lower(ls, app->terrain_hit_point, app->brush_radius, strength,
                                                    app->brush_falloff);
                } else if (mode == AF_BRUSH_SCULPT_LOWER) {
                    af_landscape_sculpt_raise_lower(ls, app->terrain_hit_point, app->brush_radius, -strength,
                                                    app->brush_falloff);
                } else if (mode == AF_BRUSH_SCULPT_SMOOTH) {
                    af_landscape_sculpt_smooth(ls, app->terrain_hit_point, app->brush_radius, strength,
                                               app->brush_falloff);
                } else if (mode == AF_BRUSH_SCULPT_FLATTEN) {
                    af_landscape_sculpt_flatten(ls, app->terrain_hit_point, app->brush_radius, app->brush_target_height,
                                                strength, app->brush_falloff);
                }
                ++app->document.revision;
            } else if (app->landscape_subtool == 1) {
                af_landscape_paint_layer(ls, app->terrain_hit_point, app->brush_radius, app->active_paint_layer,
                                         strength, app->brush_falloff);
                ++app->document.revision;
            } else if (app->landscape_subtool == 2) {
                if (af_engine_mouse_pressed(engine, AF_MOUSE_LEFT) || ((app->updates % 3) == 0)) {
                    static uint32_t f_seed = 1337;
                    f_seed += 17;
                    if (app->brush_mode == AF_BRUSH_ERASE_FOLIAGE || shift) {
                        af_landscape_erase_foliage(ls, (int)app->active_foliage_layer, app->terrain_hit_point,
                                                   app->brush_radius);
                    } else {
                        af_landscape_paint_foliage(ls, app->active_foliage_layer, app->terrain_hit_point,
                                                   app->brush_radius, app->brush_strength, f_seed);
                    }
                    ++app->document.revision;
                }
            }
        } else if (app->landscape_stroke_active && !lmb_down) {
            app->landscape_stroke_active = false;
            ed_document_end_edit(&app->document);
        }
    } else if (app->landscape_stroke_active) {
        app->landscape_stroke_active = false;
        ed_document_end_edit(&app->document);
    }
    if (app->landscape_stroke_active && af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
        ed_document_undo(&app->document);
        app->landscape_stroke_active = false;
        refresh(app);
    }
}

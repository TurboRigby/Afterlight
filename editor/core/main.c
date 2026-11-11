/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "editor_state.h"
#include "tools/blueprint_editor.h"
#include "ui/hierarchy.h"
#include "ui/inspector.h"
#include "tools/landscape_editor.h"
#include "ui/panels.h"
#include "content/project_tools.h"
#include "testing/smoke_tests.h"
#include "tools/viewport_overlays.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

static bool blueprint_edit(void *document) {
    return ed_document_push_undo(document);
}

static bool should_close(af_engine *engine, void *userdata) {
    (void)engine;
    editor *app = userdata;
    if (app->dialog || app->tool_process)
        return false;
    stop_play(app);
    if (app->animation_author.open && ed_animation_editor_dirty(&app->animation_author)) {
        request_animation_close(app, ED_CLOSE);
        return false;
    }
    if (!app->document_ready || !ed_document_dirty(&app->document))
        return true;
    app->pending = ED_CLOSE;
    app->modal = true;
    (void)af_engine_set_mouse_capture(app->engine, false);
    return false;
}

static void keyboard(editor *app, double delta) {
    af_engine *engine = app->engine;
    if (app->free_camera && (!af_engine_mouse_down(engine, AF_MOUSE_RIGHT) || !af_engine_mouse_captured(engine))) {
        /* Focus loss already clears engine capture; never restore it in that case. */
        bool resume = app->resume_game_capture && app->runtime.playing && af_engine_mouse_captured(engine);
        app->free_camera = app->resume_game_capture = false;
        (void)af_engine_set_mouse_capture(engine, resume);
    }
    if (app->modal || app->dialog)
        return;
    if (af_engine_key_pressed(engine, AF_KEY_F5))
        toggle_play(app);
    if (app->runtime.playing && af_engine_key_pressed(engine, AF_KEY_F6))
        app->maximize_play = !app->maximize_play;
    bool modifier = af_engine_key_down(engine, AF_KEY_LEFT_CONTROL) || af_engine_key_down(engine, AF_KEY_LEFT_SUPER) ||
                    af_engine_key_pressed(engine, AF_KEY_LEFT_CONTROL) ||
                    af_engine_key_pressed(engine, AF_KEY_LEFT_SUPER);
    bool shift = af_engine_key_down(engine, AF_KEY_LEFT_SHIFT) || af_engine_key_pressed(engine, AF_KEY_LEFT_SHIFT);
    if (modifier && !app->runtime.playing) {
        if (af_engine_key_pressed(engine, AF_KEY_N)) {
            if (shift)
                app->show_project = true;
            else
                request_action(app, ED_NEW);
        }
        if (af_engine_key_pressed(engine, AF_KEY_O))
            request_action(app, ED_OPEN);
        if (af_engine_key_pressed(engine, AF_KEY_S)) {
            if (app->animation_author.open) {
                if (shift)
                    save_authored_animation_as(app);
                else
                    save_authored_animation(app, NULL);
            } else
                save_document(app, shift, ED_NONE);
        }
    }
    if (modifier && !app->fields_active && !app->runtime.playing) {
        if (af_engine_key_pressed(engine, AF_KEY_Z)) {
            if (app->animation_author.open)
                ed_animation_editor_undo(&app->animation_author, shift);
            else if (shift)
                ed_document_redo(&app->document);
            else
                ed_document_undo(&app->document);
            refresh(app);
        }
        if (af_engine_key_pressed(engine, AF_KEY_Y)) {
            if (app->animation_author.open)
                ed_animation_editor_undo(&app->animation_author, true);
            else
                ed_document_redo(&app->document);
            refresh(app);
        }
        if (!app->animation_author.open && af_engine_key_pressed(engine, AF_KEY_D)) {
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
    }
    if (!app->runtime.playing && !app->fields_active && !modifier && af_engine_key_pressed(engine, AF_KEY_DELETE)) {
        if (app->animation_author.open) {
            ed_animation_editor_delete_key(&app->animation_author);
        } else if (app->light_selected && app->selected_light < af_scene_local_light_count(app->document.scene)) {
            ed_document_remove_light(&app->document, app->selected_light);
            app->light_selected = false;
            app->gizmo.dragging = false;
        } else {
            ed_document_delete(&app->document);
            refresh(app);
        }
    }
    if (!app->runtime.playing && !app->fields_active && !modifier && af_engine_key_pressed(engine, AF_KEY_F))
        focus_selection(app);
    if (af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
        app->free_camera = app->resume_game_capture = false;
        (void)af_engine_set_mouse_capture(engine, false);
        ed_gui_unfocus(&app->gui);
    }
    af_vec2 mouse = af_engine_mouse_position(engine);
    int win_w = 0, win_h = 0;
    af_engine_get_window_size(engine, &win_w, &win_h);
    float width = (float)win_w, height = (float)win_h;

    /* Handle Sidebar Splitters */
    if (!app->runtime.playing || !app->maximize_play) {
        bool lmb_down = af_engine_mouse_down(engine, AF_MOUSE_LEFT);
        bool lmb_pressed = af_engine_mouse_pressed(engine, AF_MOUSE_LEFT);

        if (!lmb_down) {
            app->dragging_left_splitter = false;
            app->dragging_right_splitter = false;
        }

        if (lmb_pressed && mouse.y >= 64.0f && mouse.y <= height - 32.0f) {
            if (fabsf(mouse.x - app->left_sidebar_width) <= 6.0f) {
                app->dragging_left_splitter = true;
            } else if (fabsf(mouse.x - (width - app->right_sidebar_width)) <= 6.0f) {
                app->dragging_right_splitter = true;
            }
        }

        if (app->dragging_left_splitter) {
            app->left_sidebar_width = fminf(fmaxf(180.0f, mouse.x), width * 0.45f);
            return;
        }
        if (app->dragging_right_splitter) {
            app->right_sidebar_width = fminf(fmaxf(220.0f, width - mouse.x), width * 0.45f);
            return;
        }
    }

    if (modifier)
        return;
    const char *floating[] = {"Performance", "Assets",        "Project", "Extensions",  "Animation",
                              "HDR import",  "Input actions", "Prefabs", "Environment", "Post-processing"};
    bool open[] = {app->show_performance, app->show_assets,         app->show_project, app->show_extensions,
                   app->show_animation,   app->environment_pending, app->show_input,   app->show_prefab,
                   app->show_environment, app->show_post_processing};
    for (size_t i = 0; i < sizeof(open) / sizeof(open[0]); ++i)
        if (open[i]) {
            struct nk_window *panel = nk_window_find(&app->gui.context, floating[i]);
            if (panel && nk_input_is_mouse_hovering_rect(&app->gui.context.input, panel->bounds))
                return;
        }

    app->terrain_hit = false;
    af_landscape *ls = af_scene_get_landscape_mut(app->document.scene);
    if (app->landscape_mode && ls && ls->enabled && inside(app->viewport, mouse)) {
        af_vec3 forward = af_camera_forward(&app->camera);
        af_vec3 right = af_vec3_normalize(af_vec3_cross(forward, (af_vec3){0, 1, 0}));
        af_vec3 up = af_vec3_cross(right, forward);
        float spread = tanf(app->camera.vertical_fov * 0.5f);
        float x = ((mouse.x - app->viewport.x) / app->viewport.width * 2.0f - 1.0f) * spread *
                  (app->viewport.width / app->viewport.height);
        float y = (1.0f - (mouse.y - app->viewport.y) / app->viewport.height * 2.0f) * spread;
        af_vec3 ray_dir =
            af_vec3_normalize(af_vec3_add(forward, af_vec3_add(af_vec3_scale(right, x), af_vec3_scale(up, y))));
        af_vec3 ray_orig = app->camera.position;
        app->terrain_hit =
            af_landscape_raycast(ls, ray_orig, ray_dir, &app->terrain_hit_point, &app->terrain_hit_normal);
    }

    if (app->landscape_mode) {
        landscape_update_stroke(app, delta, mouse, shift);
    } else if (!app->blueprint_mode && !app->animation_author.open) {
        if (!app->runtime.playing && inside(app->viewport, mouse) && af_engine_mouse_pressed(engine, AF_MOUSE_LEFT) &&
            !af_engine_mouse_captured(engine)) {
            ed_gui_unfocus(&app->gui);
            app->fields_active = false;
            int axis = ed_gizmo_hit(&app->gizmo, mouse);
            const af_scene_object *object = af_scene_get_object(app->document.scene, app->document.selection);
            const af_local_light *light =
                app->light_selected ? af_scene_local_light_at(app->document.scene, app->selected_light) : NULL;
            const af_joint *joint = af_scene_get_joint(app->document.scene, app->edit_joint);
            const af_collider_part *part = af_scene_get_collider_part(app->document.scene, app->edit_part);
            if (axis >= 0 && part) {
                ed_document_end_edit(&app->document);
                app->initial_part = *part;
                app->part_drag_revision = app->document.revision;
                ed_gizmo_begin(&app->gizmo, axis, 0, part->local, mouse);
            } else if (axis >= 0 && joint) {
                af_transform pose;
                if (ed_joint_edit_pose(app->document.scene, *joint, app->joint_part, &pose)) {
                    ed_document_end_edit(&app->document);
                    app->initial_joint = *joint;
                    app->joint_drag_revision = app->document.revision;
                    ed_gizmo_begin(&app->gizmo, axis, 0, pose, mouse);
                }
            } else if (axis >= 0 && light) {
                ed_document_end_edit(&app->document);
                app->initial_light = *light;
                app->drag_light = app->selected_light;
                app->light_drag_revision = app->document.revision;
                app->light_dragging = true;
                ed_gizmo_begin(&app->gizmo, axis, 0, ed_light_transform(*light), mouse);
            } else if (axis >= 0 && object) {
                ed_document_end_edit(&app->document);
                ed_gizmo_begin(&app->gizmo, axis, object->id, object->transform, mouse);
            } else {
                int picked = ed_light_pick(app->document.scene, &app->camera, app->viewport, mouse);
                if (picked >= 0) {
                    select_light(app, (size_t)picked);
                } else
                    select_object(app, af_engine_pick_object(engine, mouse));
            }
        }
    }
    if (inside(app->viewport, mouse) && af_engine_mouse_pressed(engine, AF_MOUSE_RIGHT)) {
        ed_gui_unfocus(&app->gui);
        app->fields_active = false;
    }

    if (app->animation_author.open && !app->runtime.playing) {
        bool dragging = app->gizmo.dragging;
        animation_bone_input(app);
        if (dragging || app->gizmo.dragging)
            return;
    }
    if (app->gizmo.dragging && app->edit_part) {
        if (af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
            if (app->document.revision != app->part_drag_revision)
                ed_document_undo(&app->document);
            app->gizmo.dragging = false;
            ed_document_end_edit(&app->document);
        } else {
            bool held = af_engine_mouse_down(engine, AF_MOUSE_LEFT);
            if (held || (SDL_GetWindowFlags(af_engine_sdl_window(engine)) & SDL_WINDOW_INPUT_FOCUS)) {
                af_collider_part next = app->initial_part;
                next.local = ed_gizmo_drag(&app->gizmo, mouse);
                ed_document_part(&app->document, next);
            }
            if (!held) {
                app->gizmo.dragging = false;
                ed_document_end_edit(&app->document);
            }
        }
        return;
    }
    if (app->gizmo.dragging && app->edit_joint) {
        if (af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
            if (app->document.revision != app->joint_drag_revision)
                ed_document_undo(&app->document);
            app->gizmo.dragging = false;
            ed_document_end_edit(&app->document);
        } else {
            bool held = af_engine_mouse_down(engine, AF_MOUSE_LEFT);
            if (held || (SDL_GetWindowFlags(af_engine_sdl_window(engine)) & SDL_WINDOW_INPUT_FOCUS)) {
                af_joint next;
                if (ed_joint_edit_apply(app->document.scene, app->initial_joint, app->joint_part,
                                        ed_gizmo_drag(&app->gizmo, mouse), &next))
                    ed_document_joint(&app->document, next);
            }
            if (!held) {
                app->gizmo.dragging = false;
                ed_document_end_edit(&app->document);
            }
        }
        return;
    }
    if (app->gizmo.dragging && app->light_dragging) {
        if (af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
            if (app->document.revision != app->light_drag_revision)
                ed_document_undo(&app->document);
            app->light_dragging = app->gizmo.dragging = false;
            ed_document_end_edit(&app->document);
        } else if (af_engine_mouse_down(engine, AF_MOUSE_LEFT)) {
            af_transform next = ed_gizmo_drag(&app->gizmo, mouse);
            ed_document_local_light(&app->document, app->drag_light,
                                    ed_light_apply(app->initial_light, next, app->gizmo.mode, app->gizmo.axis));
        } else {
            if (SDL_GetWindowFlags(af_engine_sdl_window(engine)) & SDL_WINDOW_INPUT_FOCUS) {
                af_transform next = ed_gizmo_drag(&app->gizmo, mouse);
                ed_document_local_light(&app->document, app->drag_light,
                                        ed_light_apply(app->initial_light, next, app->gizmo.mode, app->gizmo.axis));
            }
            app->light_dragging = app->gizmo.dragging = false;
            ed_document_end_edit(&app->document);
        }
        return;
    }
    if (app->gizmo.dragging) {
        if (af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
            const af_scene_object *object = af_scene_get_object(app->document.scene, app->gizmo.object);
            if (object && memcmp(&object->transform, &app->gizmo.initial, sizeof(af_transform)))
                ed_document_undo(&app->document);
            app->gizmo.dragging = false;
            ed_document_end_edit(&app->document);
        } else if (af_engine_mouse_down(engine, AF_MOUSE_LEFT))
            ed_document_transform(&app->document, ed_gizmo_drag(&app->gizmo, mouse));
        else {
            if (SDL_GetWindowFlags(af_engine_sdl_window(engine)) & SDL_WINDOW_INPUT_FOCUS)
                ed_document_transform(&app->document, ed_gizmo_drag(&app->gizmo, mouse));
            app->gizmo.dragging = false;
            ed_document_end_edit(&app->document);
        }
        return;
    }
    if (app->fields_active)
        return;
    if (!app->blueprint_mode && !app->runtime.playing && !af_engine_mouse_captured(engine)) {
        if (!app->edit_joint && af_engine_key_pressed(engine, AF_KEY_W))
            app->gizmo.mode = ED_GIZMO_MOVE;
        if (!app->edit_joint && af_engine_key_pressed(engine, AF_KEY_E))
            app->gizmo.mode = ED_GIZMO_ROTATE;
        if (!app->edit_joint && af_engine_key_pressed(engine, AF_KEY_R))
            app->gizmo.mode = ED_GIZMO_SCALE;
    }
    bool navigate = (!app->blueprint_mode || app->blueprint_state.timeline_preview) &&
                    af_engine_mouse_pressed(engine, AF_MOUSE_RIGHT) &&
                    (inside(app->viewport, mouse) || (app->runtime.playing && af_engine_mouse_captured(engine)));
    if (navigate && !app->free_camera && !af_engine_key_pressed(engine, AF_KEY_ESCAPE)) {
        app->resume_game_capture = app->runtime.playing && af_engine_mouse_captured(engine);
        app->free_camera = af_engine_set_mouse_capture(engine, true);
    }
    if (app->free_camera && af_engine_mouse_captured(engine)) {
        af_vec2 motion = af_engine_mouse_delta(engine);
        af_camera_rotate(&app->camera, motion.x * 0.0025f, -motion.y * 0.0025f);
        af_vec3 movement = {(float)af_engine_key_down(engine, AF_KEY_D) - (float)af_engine_key_down(engine, AF_KEY_A),
                            (float)af_engine_key_down(engine, AF_KEY_E) - (float)af_engine_key_down(engine, AF_KEY_Q),
                            (float)af_engine_key_down(engine, AF_KEY_W) - (float)af_engine_key_down(engine, AF_KEY_S)};
        af_camera_move(&app->camera, af_vec3_scale(af_vec3_normalize(movement), (shift ? 9.0f : 3.0f) * (float)delta));
    }
}

static void build_ui(editor *app, float width, float height) {
    struct nk_context *context = &app->gui.context;
    ed_thumbnails_begin(&app->thumbnails);
    ed_thumbnails_poll(&app->thumbnails, app->engine);
    if (!app->blueprint_mode) {
        draw_colliders(app);
        draw_physics(app);
        draw_skeleton(app);
        draw_lights(app);
        draw_landscape_brush(app);
    }
    if (app->blueprint_mode && !app->blueprint_state.timeline_preview) {
        ed_blueprint_canvas_panel(&app->blueprint_state, context,
                                  app->runtime.playing ? app->runtime.scene : app->document.scene, app->viewport,
                                  app->runtime.playing);
    }
    if ((!app->animation_preview || app->animation_author.open) && !app->landscape_mode && !app->blueprint_mode)
        draw_gizmo(app);
    toolbar(app, width);
    if (!app->runtime.playing || !app->maximize_play) {
        if (!app->animation_author.open || app->runtime.playing) {
            object_list(app, height);
            inspector(app, width, height);
        }
        draw_splitters(app, width, height);
    }
    nk_style_push_vec2(context, &context->style.window.padding, nk_vec2(14, 5));
    if (!app->runtime.playing || !app->maximize_play) {
        float tools_x = app->left_sidebar_width;
        float tools_w = fmaxf(1.0f, width - app->left_sidebar_width - app->right_sidebar_width);
        if (nk_begin(context, "Viewport tools", nk_rect(tools_x, 64, tools_w, 40),
                     panel_flags(app) | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(context, 22, 10);
            if (nk_button_label(context, app->gizmo.mode == ED_GIZMO_MOVE ? "[Move]" : "Move"))
                app->gizmo.mode = ED_GIZMO_MOVE;
            if (nk_button_label(context, app->gizmo.mode == ED_GIZMO_ROTATE ? "[Rotate]" : "Rotate"))
                app->gizmo.mode = ED_GIZMO_ROTATE;
            if (nk_button_label(context, app->gizmo.mode == ED_GIZMO_SCALE ? "[Scale]" : "Scale"))
                app->gizmo.mode = ED_GIZMO_SCALE;
            nk_bool snap = app->gizmo.snap;
            nk_checkbox_label(context, "Snap", &snap);
            app->gizmo.snap = snap != 0;
            if (nk_button_label(context, app->show_colliders ? "[Colliders]" : "Colliders"))
                app->show_colliders = !app->show_colliders;
            if (nk_button_label(context, app->show_physics ? "[Physics]" : "Physics"))
                app->show_physics = !app->show_physics;
            if (nk_button_label(context, "Prefabs"))
                app->show_prefab = !app->show_prefab;
            if (nk_button_label(context, "Input"))
                app->show_input = !app->show_input;
            if (nk_button_label(context, "Reset view"))
                reset_camera(app);
            if (nk_button_label(context, "Project"))
                app->show_project = !app->show_project;
        }
        nk_end(context);
    }
    play_hud(app);
    if (app->environment_pending) {
        af_environment_job_status job = af_engine_environment_status(app->engine);
        if (nk_begin(context, "HDR import", nk_rect(fmaxf(20, (width - 480) / 2), 120, 480, 150),
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
            char progress[128];
            snprintf(progress, sizeof(progress), "%s: %.0f%%",
                     job.state == AF_ENV_LOADING  ? "Reading panorama"
                     : job.state == AF_ENV_BAKING ? "Baking environment"
                                                  : "Publishing environment",
                     job.progress * 100);
            nk_layout_row_dynamic(context, 30, 1);
            nk_label(context, progress, NK_TEXT_LEFT);
            nk_layout_row_dynamic(context, 32, 1);
            if (nk_button_label(context, "Cancel import"))
                af_engine_cancel_environment(app->engine);
        }
        nk_end(context);
    }
    if (nk_begin(context, "Status", nk_rect(0, height - 32, width, 32), NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, app->document.status, NK_TEXT_LEFT);
        nk_end(context);
    }
    nk_style_pop_vec2(context);
    performance_panel(app, width, height);
    asset_browser_panel(app, width, height);
    project_panel(app, width, height);
    extensions_panel(app, width, height);
    environment_panel(app, width, height);
    post_processing_panel(app, width, height);
    animation_panel(app, width, height);
    animation_author_panel(app, width, height);
    input_panel(app);
    prefab_panel(app);
    if (app->modal) {
        if (nk_begin(context, "Unsaved changes", nk_rect((width - 500) / 2, (height - 205) / 2, 500, 205),
                     NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
            nk_layout_row_dynamic(context, 58, 1);
            nk_label_wrap(context, "Save your changes before continuing? Discard keeps the last saved file unchanged.");
            nk_layout_row_dynamic(context, 36, 3);
            if (nk_button_label(context, "Save"))
                save_document(app, false, app->pending);
            if (nk_button_label(context, "Discard"))
                perform_action(app, app->pending);
            if (nk_button_label(context, "Cancel")) {
                app->modal = false;
                app->pending = ED_NONE;
            }
        }
        nk_end(context);
        nk_window_set_focus(context, "Unsaved changes");
    }
    app->fields_active = context->active && (context->active->edit.active || context->active->property.active);
    if (!ed_gui_set_text_input(&app->gui, app->engine,
                               app->fields_active && !app->runtime.playing && !app->modal && !app->dialog)) {
        app->failed = true;
        af_engine_request_quit(app->engine);
    }
    if (!af_engine_mouse_down(app->engine, AF_MOUSE_LEFT) && !app->fields_active)
        ed_document_end_edit(&app->document);
}

static bool init(af_engine *engine, void *userdata) {
    editor *app = userdata;
    app->engine = engine;
    af_engine_set_animation_playback(engine, false, false);
    app->landscape_mode = false;
    app->left_sidebar_width = 252.0f;
    app->right_sidebar_width = 334.0f;
    app->dragging_left_splitter = false;
    app->dragging_right_splitter = false;
    app->blueprint_mode = false;
    ed_blueprint_state_init(&app->blueprint_state);
    app->blueprint_state.record_edit = blueprint_edit;
    app->blueprint_state.edit_context = &app->document;
    app->landscape_subtool = 0;
    app->brush_mode = AF_BRUSH_SCULPT_RAISE;
    app->brush_falloff = AF_FALLOFF_SMOOTH;
    app->brush_radius = 5.0f;
    app->brush_strength = 0.5f;
    app->brush_target_height = 0.0f;
    app->active_paint_layer = 0;
    app->active_foliage_layer = 0;
    app->landscape_stroke_active = false;
    snprintf(app->new_foliage_name, sizeof(app->new_foliage_name), "Trees");
    snprintf(app->new_foliage_mesh, sizeof(app->new_foliage_mesh), "cube");
    if (!ed_document_init(&app->document))
        return false;
    app->document_ready = true;
    const char *base = af_engine_get_base_path(engine);
    if (!base)
        return false;
    char font[ED_PATH_CAPACITY], scene[ED_PATH_CAPACITY];
    if (snprintf(font, sizeof(font), "%sfonts/FiraSans-Regular.ttf", base) >= (int)sizeof(font) ||
        snprintf(scene, sizeof(scene), "%sscenes/%s", base,
                 app->playground ? "obstacle.afterlight.json" : "demo.afterlight.json") >= (int)sizeof(scene))
        return false;
    char bundled_project[AF_ASSET_PATH_CAPACITY];
    if (snprintf(bundled_project, sizeof(bundled_project), "%s%s.afterlight.project", base,
                 app->playground ? "playground" : "demo") >= (int)sizeof(bundled_project))
        return false;
    const char *project = app->initial_project ? app->initial_project : (!app->initial_scene ? bundled_project : NULL);
    if (!(project ? open_project(app, project, app->initial_project == NULL)
                  : ed_document_load(&app->document, app->initial_scene ? app->initial_scene : scene,
                                     app->initial_scene == NULL))) {
        fprintf(stderr, "Editor scene: %s\n", app->document.status);
        return false;
    }
    /* Development watches the build output. A moved bundle uses its own module. */
    if (!app->initial_project && !app->module_override && !app->initial_scene) {
        SDL_PathInfo info;
        const char *development = app->playground ? AF_PLAYGROUND_MODULE_PATH : AF_GAME_MODULE_PATH;
        if (SDL_GetPathInfo(development, &info) && info.type == SDL_PATHTYPE_FILE)
            snprintf(app->module_path, sizeof(app->module_path), "%s", development);
    }
    af_mesh_data cube = cube_mesh_data();
    if (!af_engine_upload_mesh(engine, "cube", &cube) || !ed_gui_init(&app->gui, engine, font))
        return false;
    if (app->animation_test_path) {
        if (!ed_document_new(&app->document) ||
            !ed_document_import_mesh(&app->document, "actor", app->animation_test_path))
            return false;
        app->animation_target = app->document.selection;
        af_animation_component c = af_scene_get_object(app->document.scene, app->animation_target)->animation;
        c.autoplay = false;
        if (!af_scene_set_object_animation(app->document.scene, app->animation_target, c))
            return false;
        app->show_animation = true;
        app->animation_test_revision = app->document.revision;
    }
    load_assets(app);
    app->asset_watch = af_asset_watch_create();
    app->watch_assets = true;
    SDL_SetWindowMinimumSize(af_engine_sdl_window(engine), 1000, 640);
    reset_camera(app);
    refresh(app);
    af_engine_set_scene(engine, app->document.scene);
    if (app->animation_authoring_test)
        start_animation_authoring(app);
    af_engine_set_clear_color(engine, (af_color){0.018f, 0.022f, 0.033f, 1});
    return true;
}

static void fixed_update(af_engine *engine, double delta, void *userdata) {
    editor *app = userdata;
    if (!app->runtime.playing)
        return;
    if (app->runtime.paused)
        ed_runtime_step(&app->runtime, delta);
    else
        ed_runtime_update(&app->runtime, delta,
                          !af_engine_mouse_down(engine, AF_MOUSE_RIGHT) && !app->fields_active && !app->modal &&
                              !app->dialog);
    af_engine_set_scene(engine, app->runtime.scene);
}

static void update(af_engine *engine, double delta, void *userdata) {
    editor *app = userdata;
    ++app->updates;
    if (app->frame_limit && af_engine_get_stats(engine).rendered_frames >= app->frame_limit) {
        if (app->play_controls_test || app->wizard_test || app->thumbnail_test || app->light_test ||
            app->animation_test_path) {
            app->failed = true;
            fputs("Editor test did not complete within its frame budget.\n", stderr);
        }
        af_engine_request_quit(engine);
        return;
    }
    poll_dialog(app);
    poll_project_tool(app);
    play_controls_smoke(app);
    smoke(app);
    if (app->environment_pending) {
        af_environment_job_status job = af_engine_environment_status(engine);
        if (job.state == AF_ENV_COMPLETE) {
            if (app->document.revision == app->environment_document_revision && !app->runtime.playing) {
                if (ed_document_import_environment(&app->document, app->environment_name, app->environment_path))
                    refresh(app);
            } else
                set_status(app, "HDR prepared, but the document changed; import again to assign it.");
            app->environment_pending = false;
        } else if (job.state == AF_ENV_FAILED || job.state == AF_ENV_CANCELED) {
            set_status(app,
                       job.state == AF_ENV_FAILED ? job.error : "HDR import canceled; previous environment retained.");
            app->environment_pending = false;
        }
    }
    if (app->thumbnail_test) {
        app->show_assets = true;
        if (app->updates == 12)
            app->asset_filter = 2;
    }
    if (app->wizard_test) {
        if (app->updates == 3) {
            app->show_project = true;
            snprintf(app->wizard_name, sizeof(app->wizard_name), "Editor wizard check");
            start_project_tool(app, ED_TOOL_NEW);
            if (!app->tool_process) {
                app->failed = true;
                af_engine_request_quit(engine);
            }
        }
        if (app->updates > 3 && !app->tool_process) {
            char path[AF_ASSET_PATH_CAPACITY];
            af_project project;
            af_asset_error error;
            bool valid =
                snprintf(path, sizeof(path), "%s/game.afterlight.project", app->wizard_folder) < (int)sizeof(path) &&
                af_project_load(path, &project, &error);
            app->failed |= !app->tool_success || !valid || af_scene_object_count(app->document.scene) != 4 ||
                           ed_document_dirty(&app->document) || nk_window_find(&app->gui.context, "Project") == NULL;
            af_engine_request_quit(engine);
        }
    }
    if (app->selected_light >= af_scene_local_light_count(app->document.scene))
        app->light_selected = false;
    if (!app->runtime.playing && !app->blueprint_state.preview_scene)
        af_engine_set_scene(engine, app->document.scene);
    int w = 0, h = 0;
    af_engine_get_window_size(engine, &w, &h);
    float width = (float)w, height = (float)h;
    float max_sidebar = fmaxf(100.0f, width * 0.45f);
    if (app->left_sidebar_width > max_sidebar)
        app->left_sidebar_width = max_sidebar;
    if (app->right_sidebar_width > max_sidebar)
        app->right_sidebar_width = max_sidebar;
    app->viewport =
        app->runtime.playing && app->maximize_play
            ? (af_rect){0, 64, width, fmaxf(1, height - 96)}
            : (af_rect){app->left_sidebar_width, 104,
                        fmaxf(1, width - app->left_sidebar_width - app->right_sidebar_width), fmaxf(1, height - 136)};
    if (app->animation_author.open && !app->runtime.playing)
        app->viewport.height = fmaxf(1, app->viewport.height - 244);
    af_engine_set_viewport(engine, &app->viewport);
    const af_scene_object *selected = af_scene_get_object(app->document.scene, app->document.selection);
    const af_joint *editing = af_scene_get_joint(app->document.scene, app->edit_joint);
    if (app->edit_joint && (!editing || !selected || (selected->id != editing->a && selected->id != editing->b))) {
        app->edit_joint = 0;
        app->gizmo.dragging = false;
    }

    const af_collider_part *editing_part = af_scene_get_collider_part(app->document.scene, app->edit_part);
    if (app->edit_part && (!editing_part || !selected || editing_part->owner != selected->id)) {
        app->edit_part = 0;
        app->gizmo.dragging = false;
    }
    if (app->edit_part && !app->runtime.playing) {
        af_mat4 matrix;
        if (af_scene_world_matrix(app->document.scene, editing_part->owner, &matrix)) {
            af_vec4 world =
                af_mat4_transform(matrix, (af_vec4){editing_part->local.position.x, editing_part->local.position.y,
                                                    editing_part->local.position.z, 1});
            ed_gizmo_project(&app->gizmo, &app->camera, app->viewport, (af_vec3){world.x, world.y, world.z}, matrix,
                             editing_part->local.rotation);
        } else
            app->gizmo.projected = false;
    } else if (app->edit_joint && !app->runtime.playing) {
        af_transform pose;
        if (ed_joint_edit_pose(app->document.scene, *editing, app->joint_part, &pose)) {
            app->gizmo.mode = app->joint_part < ED_JOINT_AXIS_A ? ED_GIZMO_MOVE : ED_GIZMO_ROTATE;
            ed_gizmo_project(&app->gizmo, &app->camera, app->viewport, pose.position, af_mat4_identity(),
                             pose.rotation);
        } else
            app->gizmo.projected = false;
    } else if (app->light_selected) {
        const af_local_light *light = af_scene_local_light_at(app->document.scene, app->selected_light);
        if (light)
            ed_gizmo_project(&app->gizmo, &app->camera, app->viewport, light->position, af_mat4_identity(),
                             ed_light_transform(*light).rotation);
    } else if (selected) {
        af_mat4 world, parent = af_mat4_identity();
        if (selected->parent)
            af_scene_world_matrix(app->document.scene, selected->parent, &parent);
        if (af_scene_world_matrix(app->document.scene, selected->id, &world))
            ed_gizmo_project(&app->gizmo, &app->camera, app->viewport,
                             (af_vec3){world.elements[12], world.elements[13], world.elements[14]}, parent,
                             selected->transform.rotation);
        else
            app->gizmo.projected = false;
    } else
        app->gizmo.projected = false;
    if (app->animation_author.open && !app->runtime.playing)
        project_animation_bone(app);
    if (app->watch_assets) {
        unsigned before = af_asset_watch_get_stats(app->asset_watch).reloads;
        af_asset_error message;
        af_asset_watch_update(app->asset_watch, engine, app->runtime.playing ? app->runtime.scene : app->document.scene,
                              delta, &message);
        if (af_asset_watch_get_stats(app->asset_watch).reloads != before)
            ed_thumbnails_invalidate(&app->thumbnails);
        if (message.message[0])
            set_status(app, message.message);
    }
    ed_gui_input(&app->gui, engine);
    keyboard(app, delta);
    build_ui(app, width, height);
    af_engine_set_camera(engine, &app->camera);
    ed_runtime_frame_update(&app->runtime, delta,
                            !af_engine_mouse_down(engine, AF_MOUSE_RIGHT) && !app->fields_active && !app->modal &&
                                !app->dialog);
    if (app->runtime.playing)
        app->camera = af_engine_get_camera(engine);
    if (app->runtime.playing && strcmp(app->runtime_status, app->runtime.status)) {
        snprintf(app->runtime_status, sizeof(app->runtime_status), "%s", app->runtime.status);
        set_status(app, app->runtime.status);
    }
    light_tools_smoke(app);
    if (app->animation_authoring_test)
        animation_authoring_smoke(app);
    else
        animation_tools_smoke(app);
    af_scene *display = ed_blueprint_preview(&app->blueprint_state, engine,
                                             app->runtime.playing ? app->runtime.scene : app->document.scene,
                                             app->document.revision, app->blueprint_mode && !app->runtime.playing);
    af_engine_set_scene(engine, display);
    update_animation_authoring(app, delta);
    af_engine_set_animation_playback(engine, app->runtime.playing || app->animation_preview,
                                     !app->runtime.playing && app->animation_preview);
    af_engine_highlight_object(engine, app->runtime.playing ? AF_OBJECT_NONE : app->document.selection);
    af_engine_set_simulation_paused(engine, !app->runtime.playing || app->runtime.paused);
    char title[512];
    snprintf(title, sizeof(title), "Afterlight Editor | %s%s",
             app->document.path[0] ? ed_path_name(app->document.path) : "Untitled scene",
             ed_document_dirty(&app->document) ? " *" : "");
    if (strcmp(title, app->title)) {
        snprintf(app->title, sizeof(app->title), "%s", title);
        af_engine_set_title(engine, title);
    }
    if (!ed_gui_render(&app->gui, engine, width, height)) {
        app->failed = true;
        af_engine_request_quit(engine);
    }
    if (app->thumbnail_test && app->updates == 14)
        ed_thumbnails_invalidate(&app->thumbnails);
    if (app->thumbnail_test && app->updates >= 50) {
        unsigned images = 0, ready = 0;
        for (uint32_t i = 0; i < app->gui.draw_count; ++i)
            if (app->gui.draws[i].texture >= 2)
                ++images;
        for (unsigned i = 0; i < ED_THUMBNAIL_CACHE_SIZE; ++i)
            if (app->thumbnails.entries[i].ready)
                ++ready;
        app->failed |= images < 1 || ready < 3 || app->thumbnails.completed < 3 || !app->thumbnails.worker ||
                       ed_document_dirty(&app->document);
        printf("Editor thumbnails: %u cached / %u image draws / %u asynchronous completions; document %s.\n", ready,
               images, app->thumbnails.completed, ed_document_dirty(&app->document) ? "dirty" : "clean");
        af_scene_asset shutdown_asset = {.name = "shutdown-job", .kind = AF_ASSET_MESH, .path = "Built-in mesh"};
        (void)ed_thumbnails_get(&app->thumbnails, engine, app->document.scene, &shutdown_asset, NULL);
        af_engine_request_quit(engine);
    }
}

static void shutdown(af_engine *engine, void *userdata) {
    editor *app = userdata;
    ed_dialog_release(app->dialog);
    app->dialog = NULL;
    if (app->tool_process)
        SDL_DestroyProcess(app->tool_process);
    af_engine_set_scene(engine, NULL);
    ed_animation_editor_destroy(&app->animation_author);
    if (app->animation_authoring_test)
        remove("editor-authoring.afanim");
    af_scene_destroy(app->blueprint_state.preview_scene);
    af_engine_set_overlay(engine, NULL);
    ed_runtime_stop(&app->runtime);
    af_asset_watch_destroy(app->asset_watch);
    ed_thumbnails_destroy(&app->thumbnails);
    ed_gui_destroy(&app->gui);
    if (app->document_ready)
        ed_document_destroy(&app->document);
    af_engine_stats stats = af_engine_get_stats(engine);
    if ((app->smoke_path || app->play_controls_test) && app->failed)
        fputs("Editor smoke checks failed.\n", stderr);
    printf("Editor: %" PRIu64 " frames; %u validation errors.\n", stats.rendered_frames, stats.validation_errors);
}

int main(int argc, char **argv) {
    editor app = {0};
    af_engine_config config = af_engine_config_default();
    const char *extensions[AF_EXTENSION_LIMIT];
    size_t extension_count = 0;
    snprintf(app.module_path, sizeof(app.module_path), "%s", AF_GAME_MODULE_PATH);
    config.title = "Afterlight Editor";
    config.width = 1280;
    config.height = 800;
    config.show_fps = true;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) {
            puts(
                "Usage: afterlight_editor [--playground] [--project PATH] [--scene PATH] [--game LIBRARY] [--extension "
                "LIBRARY] [--frames N] [--smoke-test PATH] [--play-controls-test] [--validation | --no-validation]");
            return 0;
        }
        if (!strcmp(argv[i], "--playground")) {
            app.playground = true;
            snprintf(app.module_path, sizeof(app.module_path), "%s", AF_PLAYGROUND_MODULE_PATH);
        } else if (!strcmp(argv[i], "--light-tools-test")) {
            app.light_test = true;
            if (!app.frame_limit)
                app.frame_limit = 600;
        } else if (!strcmp(argv[i], "--show-environment"))
            app.show_environment = true;
        else if (!strcmp(argv[i], "--show-assets"))
            app.show_assets = true;
        else if (!strcmp(argv[i], "--thumbnail-test")) {
            app.thumbnail_test = true;
            app.show_assets = true;
            if (!app.frame_limit)
                app.frame_limit = 600;
        } else if (!strcmp(argv[i], "--play-controls-test")) {
            app.play_controls_test = true;
            if (!app.frame_limit)
                app.frame_limit = 300;
        } else if (!strcmp(argv[i], "--project") && i + 1 < argc)
            app.initial_project = argv[++i];
        else if (!strcmp(argv[i], "--extension") && i + 1 < argc && extension_count < AF_EXTENSION_LIMIT)
            extensions[extension_count++] = argv[++i];
        else if (!strcmp(argv[i], "--wizard-test") && i + 1 < argc) {
            app.wizard_test = true;
            snprintf(app.wizard_folder, sizeof(app.wizard_folder), "%s-%" PRIu64, argv[++i], SDL_GetTicksNS());
            if (!app.frame_limit)
                app.frame_limit = 600;
        } else if (!strcmp(argv[i], "--animation-authoring-test") && i + 1 < argc) {
            app.animation_test_path = argv[++i];
            app.animation_authoring_test = true;
            if (!app.frame_limit)
                app.frame_limit = 600;
        } else if (!strcmp(argv[i], "--animation-tools-test") && i + 1 < argc) {
            app.animation_test_path = argv[++i];
            if (!app.frame_limit)
                app.frame_limit = 600;
        } else if (!strcmp(argv[i], "--scene") && i + 1 < argc)
            app.initial_scene = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) {
            app.module_override = true;
            const char *path = argv[++i];
            if (strlen(path) >= sizeof(app.module_path)) {
                fputs("Game module path too long.\n", stderr);
                return 1;
            }
            snprintf(app.module_path, sizeof(app.module_path), "%s", path);
        } else if (!strcmp(argv[i], "--smoke-test") && i + 1 < argc) {
            app.smoke_path = argv[++i];
            if (!app.frame_limit)
                app.frame_limit = 180;
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            const char *value = argv[++i];
            char *end = NULL;
            errno = 0;
            unsigned long long count = strtoull(value, &end, 10);
            if (errno || !value[0] || value[0] < '0' || value[0] > '9' || *end || !count) {
                fputs("--frames needs a positive integer.\n", stderr);
                return 1;
            }
            app.frame_limit = (uint64_t)count;
        } else if (!strcmp(argv[i], "--validation"))
            config.enable_validation = true;
        else if (!strcmp(argv[i], "--no-validation"))
            config.enable_validation = false;
        else {
            fprintf(stderr, "Unknown/incomplete argument: %s\n", argv[i]);
            return 1;
        }
    }
    read_startup_extension(&app);
    ext_discovery disc = {.paths = extensions, .count = &extension_count};
    bool cli_extensions = (extension_count > 0);
    bool is_automated_test = (app.smoke_path != NULL || app.play_controls_test || app.wizard_test ||
                              app.thumbnail_test || app.light_test || app.animation_test_path != NULL);
    if (!is_automated_test) {
        add_scheduled_startup_extensions(&disc, app.startup_extension);
        const char *base = SDL_GetBasePath();
        if (base) {
            char base_ext[AF_ASSET_PATH_CAPACITY];
            snprintf(base_ext, sizeof(base_ext), "%sextensions", base);
            discover_editor_ext_in_dir(base_ext, &disc);
            snprintf(base_ext, sizeof(base_ext), "%s../extensions", base);
            discover_editor_ext_in_dir(base_ext, &disc);
            snprintf(base_ext, sizeof(base_ext), "%s../Resources/extensions", base);
            discover_editor_ext_in_dir(base_ext, &disc);
            snprintf(base_ext, sizeof(base_ext), "%s../../../extensions", base);
            discover_editor_ext_in_dir(base_ext, &disc);
        }
        char *pref = SDL_GetPrefPath("Afterlight", "Extensions");
        if (pref) {
            discover_editor_ext_in_dir(pref, &disc);
            SDL_free(pref);
        }
        if (app.initial_project) {
            char proj_dir[AF_ASSET_PATH_CAPACITY];
            if (af_asset_resolve_path(app.initial_project, "extensions", proj_dir, sizeof(proj_dir))) {
                discover_editor_ext_in_dir(proj_dir, &disc);
            }
        }
    }
    af_engine *engine = af_engine_create_with_extensions(&config, extensions, extension_count);
    if (!engine && extension_count > 0) {
        fputs("Extensions failed to load. Starting the editor without extensions.\n", stderr);
        engine = af_engine_create_with_extensions(&config, NULL, 0);
    }
    if (!engine)
        return 1;

    size_t ext_count = af_engine_extension_count(engine);
    if (ext_count > 0 && !cli_extensions) {
        bool has_scheduled_upscaler = false;
        for (size_t i = 0; i < ext_count; ++i) {
            af_extension_info info;
            if (af_engine_get_extension(engine, i, &info) && info.vulkan_upscaler) {
                if (is_startup_extension_scheduled(&app, info.path)) {
                    has_scheduled_upscaler = true;
                    af_engine_set_extension_enabled(engine, i, true);
                    break;
                }
            }
        }
        if (!has_scheduled_upscaler) {
            for (size_t i = 0; i < ext_count; ++i) {
                af_extension_info info;
                if (af_engine_get_extension(engine, i, &info) && info.vulkan_upscaler) {
                    af_engine_set_extension_enabled(engine, i, false);
                }
            }
        }
    }
    const char *require_validation = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
    if ((app.smoke_path || app.play_controls_test) && require_validation && !strcmp(require_validation, "1") &&
        !af_engine_get_stats(engine).validation_enabled) {
        fputs("Editor smoke test requires validation.\n", stderr);
        af_engine_destroy(engine);
        return 1;
    }
    af_game game = {.init = init,
                    .update = update,
                    .shutdown = shutdown,
                    .user_data = &app,
                    .should_close = should_close,
                    .fixed_update = fixed_update};
    int result = af_engine_run(engine, &game);
    af_engine_destroy(engine);
    return app.failed ? 1 : result;
}

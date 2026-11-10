/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "editor_state.h"
#include "project_tools.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void reset_camera(editor *app) {
    app->camera = af_camera_default();
    app->camera.position = (af_vec3){6, 4, 8};
    app->camera.yaw = atan2f(-8, -6);
    app->camera.pitch = atan2f(-4, 10);
}

bool inside(af_rect r, af_vec2 p) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}

void set_status(editor *app, const char *text) {
    snprintf(app->document.status, sizeof(app->document.status), "%s", text);
}

void refresh(editor *app) {
    app->refresh_fields = true;
    ed_document_end_edit(&app->document);
}

void stop_play(editor *app) {
    if (!app->runtime.playing)
        return;
    af_engine_set_scene(app->engine, app->document.scene);
    ed_runtime_stop(&app->runtime);
    app->camera = app->edit_camera;
    (void)af_engine_set_mouse_capture(app->engine, false);
    app->free_camera = app->resume_game_capture = app->maximize_play = false;
    set_status(app, app->runtime.status);
    refresh(app);
}

void toggle_play(editor *app) {
    if (app->runtime.playing) {
        stop_play(app);
        return;
    }
    if (app->modal || app->dialog || app->invalid_name)
        return;
    ed_gui_unfocus(&app->gui);
    app->fields_active = false;
    ed_document_end_edit(&app->document);
    app->gizmo.dragging = false;
    app->edit_camera = app->camera;
    app->free_camera = app->resume_game_capture = false;
    if (app->project_path[0])
        ed_runtime_set_project(&app->runtime, app->project_path, app->project.startup_scene);
    if (ed_runtime_start_file(&app->runtime, app->document.scene, app->engine, app->module_path))
        app->blueprint_mode = false;
    set_status(app, app->runtime.status);
}

void start_dialog(editor *app, bool save) {
    if (app->dialog)
        return;
    (void)af_engine_set_mouse_capture(app->engine, false);
    const char *path = app->document.path[0] ? app->document.path : (save ? "Untitled.afterlight.json" : NULL);
    app->dialog = ed_dialog_begin(app->engine, save, path);
    app->dialog_save = save;
    app->dialog_asset = 0;
    if (!app->dialog)
        set_status(app, "Cannot open the file dialog.");
}

void start_import(editor *app, af_asset_kind kind) {
    if (app->dialog || app->modal)
        return;
    app->dialog = ed_dialog_begin_asset(app->engine, kind);
    app->dialog_asset = kind == AF_ASSET_ENVIRONMENT          ? 9
                        : kind == AF_ASSET_MESH               ? 1
                        : kind == AF_ASSET_AUDIO              ? 7
                        : kind == AF_ASSET_METALLIC_ROUGHNESS ? 8
                        : kind == AF_ASSET_NORMAL_MAP         ? 4
                                                              : 2;
    app->dialog_save = false;
    if (!app->dialog)
        set_status(app, "Cannot open import dialog.");
}

void start_import_terrain_layer(editor *app, size_t layer_idx, bool normal) {
    if (app->dialog || app->modal)
        return;
    app->dialog = ed_dialog_begin_asset(app->engine, normal ? AF_ASSET_NORMAL_MAP : AF_ASSET_TEXTURE);
    app->dialog_asset = (int)(normal ? (30 + layer_idx) : (20 + layer_idx));
    app->dialog_save = false;
    if (!app->dialog)
        set_status(app, "Cannot open import dialog.");
}

bool load_assets(editor *app) {
    af_asset_error error;
    if (af_engine_load_scene_assets(app->engine, app->document.scene, &error))
        return true;
    set_status(app, error.message);
    return false;
}

bool open_project(editor *app, const char *path, bool untitled) {
    af_project project;
    af_asset_error error;
    if (!af_project_load(path, &project, &error)) {
        set_status(app, error.message);
        return false;
    }
    if (!ed_document_load(&app->document, project.startup_scene, untitled))
        return false;
    char input_path[4096];
    if (af_asset_resolve_path(path, "input.json", input_path, sizeof(input_path))) {
        FILE *input = fopen(input_path, "rb");
        if (input) {
            fclose(input);
            if (!af_engine_load_input_map(app->engine, input_path, &error)) {
                set_status(app, error.message);
                return false;
            }
        }
    }
    app->project = project;
    app->project_is_template = untitled;
    snprintf(app->project_path, sizeof(app->project_path), "%s", path);
    if (!app->module_override)
        snprintf(app->module_path, sizeof(app->module_path), "%s", project.game_module);
    refresh(app);
    reset_camera(app);
    return true;
}

void perform_action(editor *app, ed_action action) {
    app->pending = ED_NONE;
    app->modal = false;
    if (action == ED_NEW) {
        if (ed_document_new(&app->document))
            refresh(app);
    }
    if (action == ED_OPEN)
        start_dialog(app, false);
    if (action == ED_OPEN_PROJECT) {
        app->dialog = ed_dialog_begin_project(app->engine, false, NULL);
        app->dialog_asset = 5;
    }
    if (action == ED_CLOSE)
        af_engine_request_quit(app->engine);
}

void request_action(editor *app, ed_action action) {
    if (app->dialog || app->modal || app->tool_process)
        return;
    stop_play(app);
    ed_document_end_edit(&app->document);
    if (ed_document_dirty(&app->document)) {
        app->pending = action;
        app->modal = true;
        (void)af_engine_set_mouse_capture(app->engine, false);
    } else
        perform_action(app, action);
}

void save_document(editor *app, bool save_as, ed_action after) {
    if (app->invalid_name) {
        set_status(app, "Enter a non-empty object name and mesh name before saving.");
        return;
    }
    ed_document_end_edit(&app->document);
    if (save_as || !app->document.path[0]) {
        app->after_save = after;
        app->modal = false;
        start_dialog(app, true);
    } else if (ed_document_save(&app->document, app->document.path))
        perform_action(app, after);
}

void poll_dialog(editor *app) {
    if (!app->dialog)
        return;
    char path[ED_PATH_CAPACITY], error[256];
    int result = ed_dialog_poll(app->dialog, path, sizeof(path), error, sizeof(error));
    if (!result)
        return;
    ed_dialog_release(app->dialog);
    app->dialog = NULL;
    if (result < 0) {
        set_status(app, error);
        app->after_save = ED_NONE;
        return;
    }
    if (result == 2) {
        set_status(app, "File dialog canceled.");
        app->after_save = ED_NONE;
        app->pending = ED_NONE;
        return;
    }
    const char *extension = strrchr(path, '.');
    bool open_project_file =
        !app->dialog_asset && !app->dialog_save && extension && SDL_strcasecmp(extension, ".project") == 0;
    if (app->dialog_asset == ED_DIALOG_EXTENSION) {
        bool loaded = af_engine_load_extension(app->engine, path);
        af_extension_info info;
        if (loaded && af_engine_get_extension(app->engine, af_engine_extension_count(app->engine) - 1, &info) &&
            info.startup_status == AF_EXTENSION_RESTART_REQUIRED)
            schedule_startup_extension(app, info.path, true);
        else
            set_status(app, loaded ? "Engine extension enabled."
                                   : "Extension load failed. Check its dependencies, ABI and ID.");
        app->dialog_asset = 0;
    } else if (app->dialog_asset == 5 || open_project_file) {
        app->module_override = false;
        if (open_project(app, path, false))
            load_assets(app);
        app->dialog_asset = 0;
    } else if (app->dialog_asset == 6) {
        af_project project = app->project;
        af_asset_error project_error;
        if (!project.name[0])
            snprintf(project.name, sizeof(project.name), "%s", ed_path_name(path));
        snprintf(project.startup_scene, sizeof(project.startup_scene), "%s", app->document.path);
        snprintf(project.game_module, sizeof(project.game_module), "%s", app->module_path);
        if (af_project_save(path, &project, &project_error)) {
            app->project = project;
            app->project_is_template = false;
            snprintf(app->project_path, sizeof(app->project_path), "%s", path);
            set_status(app, "Project saved.");
        } else
            set_status(app, project_error.message);
        app->dialog_asset = 0;
    } else if (app->dialog_asset == 3) {
        snprintf(app->module_path, sizeof(app->module_path), "%s", path);
        set_status(app, "C game selected. Press Play to load it.");
        app->dialog_asset = 0;
    } else if (app->dialog_asset >= 20 && app->dialog_asset < 24) {
        size_t layer_idx = (size_t)(app->dialog_asset - 20);
        uint64_t hash = UINT64_C(14695981039346656037);
        for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
            hash ^= *p;
            hash *= UINT64_C(1099511628211);
        }
        char name[AF_MESH_NAME_CAPACITY];
        snprintf(name, sizeof(name), "terrain_tex_%016" PRIx64, hash);
        af_asset_error asset_error;
        if (af_engine_load_texture(app->engine, name, path, &asset_error)) {
            ed_document_push_undo(&app->document);
            af_scene_set_asset(app->document.scene, name, AF_ASSET_TEXTURE, path);
            af_landscape *ls = af_scene_get_landscape_mut(app->document.scene);
            if (ls && layer_idx < ls->layer_count) {
                snprintf(ls->layers[layer_idx].diffuse_texture, sizeof(ls->layers[layer_idx].diffuse_texture), "%s",
                         name);
                ls->layers[layer_idx].color = (af_color){1.0f, 1.0f, 1.0f, 1.0f};
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
            }
            ++app->document.revision;
            refresh(app);
            set_status(app, "Terrain layer diffuse texture imported.");
        } else
            set_status(app, asset_error.message);
        app->dialog_asset = 0;
    } else if (app->dialog_asset >= 30 && app->dialog_asset < 34) {
        size_t layer_idx = (size_t)(app->dialog_asset - 30);
        uint64_t hash = UINT64_C(14695981039346656037);
        for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
            hash ^= *p;
            hash *= UINT64_C(1099511628211);
        }
        char name[AF_MESH_NAME_CAPACITY];
        snprintf(name, sizeof(name), "terrain_norm_%016" PRIx64, hash);
        af_asset_error asset_error;
        if (af_engine_load_normal_map(app->engine, name, path, &asset_error)) {
            ed_document_push_undo(&app->document);
            af_scene_set_asset(app->document.scene, name, AF_ASSET_NORMAL_MAP, path);
            af_landscape *ls = af_scene_get_landscape_mut(app->document.scene);
            if (ls && layer_idx < ls->layer_count) {
                snprintf(ls->layers[layer_idx].normal_texture, sizeof(ls->layers[layer_idx].normal_texture), "%s",
                         name);
                for (size_t c = 0; c < ls->chunk_count; ++c)
                    ls->chunks[c].dirty_mesh = true;
                ls->modification_version++;
            }
            ++app->document.revision;
            refresh(app);
            set_status(app, "Terrain layer normal map imported.");
        } else
            set_status(app, asset_error.message);
        app->dialog_asset = 0;
    } else if (app->dialog_asset) {
        uint64_t hash = UINT64_C(14695981039346656037);
        for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
            hash ^= *p;
            hash *= UINT64_C(1099511628211);
        }
        char name[AF_MESH_NAME_CAPACITY];
        snprintf(name, sizeof(name), "%s_%016" PRIx64,
                 app->dialog_asset == 9   ? "environment"
                 : app->dialog_asset == 1 ? "mesh"
                 : app->dialog_asset == 7 ? "sound"
                 : app->dialog_asset == 4 ? "normal"
                                          : "texture",
                 hash);
        af_asset_error asset_error;
        if (app->dialog_asset == 1) {
            bool model = af_asset_is_model_path(path);
            if (model) {
                if (ed_document_import_mesh(&app->document, name, path)) {
                    bool loaded = load_assets(app);
                    refresh(app);
                    const af_scene_object *root = af_scene_get_object(app->document.scene, app->document.selection);
                    if (loaded && root && root->animation.asset[0])
                        set_status(app, "Model imported. Open Animation clips in the Inspector to preview.");
                }
            } else if (af_engine_load_mesh(app->engine, name, path, &asset_error)) {
                ed_document_import_mesh(&app->document, name, path);
                refresh(app);
            } else
                set_status(app, asset_error.message);
        } else if (app->dialog_asset == 9) {
            if (af_engine_start_environment(app->engine, name, path, &asset_error)) {
                app->environment_pending = true;
                snprintf(app->environment_name, sizeof(app->environment_name), "%s", name);
                snprintf(app->environment_path, sizeof(app->environment_path), "%s", path);
                app->environment_document_revision = app->document.revision;
            } else
                set_status(app, asset_error.message);
        } else if (app->dialog_asset == 7) {
            if (af_engine_load_sound(app->engine, name, path, &asset_error)) {
                ed_document_import_sound(&app->document, name, path);
                refresh(app);
            } else
                set_status(app, asset_error.message);
        } else if (app->dialog_asset == 8) {
            if (af_engine_load_normal_map(app->engine, name, path, &asset_error)) {
                ed_document_import_surface(&app->document, name, path);
                refresh(app);
            } else
                set_status(app, asset_error.message);
        } else if (app->dialog_asset == 4) {
            if (af_engine_load_normal_map(app->engine, name, path, &asset_error)) {
                ed_document_import_normal_map(&app->document, name, path);
                refresh(app);
            } else
                set_status(app, asset_error.message);
        } else {
            if (af_engine_load_texture(app->engine, name, path, &asset_error)) {
                ed_document_import_texture(&app->document, name, path);
                refresh(app);
            } else
                set_status(app, asset_error.message);
        }
        app->dialog_asset = 0;
    } else if (app->dialog_save) {
        ed_action after = app->after_save;
        app->after_save = ED_NONE;
        if (ed_document_save(&app->document, path))
            perform_action(app, after);
        else if (after != ED_NONE) {
            app->pending = after;
            app->modal = true;
        }
    } else if (ed_document_load(&app->document, path, false)) {
        if (!app->project_path[0]) {
            char candidate[AF_ASSET_PATH_CAPACITY];
            if (af_asset_resolve_path(path, "game.afterlight.project", candidate, sizeof(candidate))) {
                SDL_PathInfo pi;
                if (SDL_GetPathInfo(candidate, &pi) && pi.type == SDL_PATHTYPE_FILE) {
                    af_project p;
                    if (af_project_load(candidate, &p, NULL)) {
                        app->project = p;
                        snprintf(app->project_path, sizeof(app->project_path), "%s", candidate);
                        if (!app->module_override)
                            snprintf(app->module_path, sizeof(app->module_path), "%s", p.game_module);
                    }
                }
            }
            if (!app->project_path[0] &&
                af_asset_resolve_path(path, "../game.afterlight.project", candidate, sizeof(candidate))) {
                SDL_PathInfo pi;
                if (SDL_GetPathInfo(candidate, &pi) && pi.type == SDL_PATHTYPE_FILE) {
                    af_project p;
                    if (af_project_load(candidate, &p, NULL)) {
                        app->project = p;
                        snprintf(app->project_path, sizeof(app->project_path), "%s", candidate);
                        if (!app->module_override)
                            snprintf(app->module_path, sizeof(app->module_path), "%s", p.game_module);
                    }
                }
            }
        }
        refresh(app);
        reset_camera(app);
        load_assets(app);
    }
}

void select_object(editor *app, af_object_id id) {
    app->edit_joint = 0;
    app->edit_part = 0;
    app->light_selected = false;
    if (app->document.selection != id) {
        app->document.selection = id;
        refresh(app);
    }
}

void select_light(editor *app, size_t index) {
    if (app->light_selected && app->selected_light == index)
        return;
    app->edit_joint = 0;
    app->edit_part = 0;
    ed_document_end_edit(&app->document);
    app->selected_light = index;
    app->light_selected = true;
    app->document.selection = 0;
    app->gizmo.dragging = false;
    struct nk_window *inspector_window = nk_window_find(&app->gui.context, "Inspector");
    if (inspector_window)
        inspector_window->scrollbar.y = 0;
}

void focus_selection(editor *app) {
    const af_scene_object *object = af_scene_get_object(app->document.scene, app->document.selection);

    if (app->light_selected) {
        const af_local_light *light = af_scene_local_light_at(app->document.scene, app->selected_light);
        if (light)
            app->camera.position = af_vec3_sub(
                light->position, af_vec3_scale(af_camera_forward(&app->camera), fmaxf(3, light->range * .3f)));
        return;
    }
    if (!object)
        return;
    af_mat4 world;
    if (!af_scene_world_matrix(app->document.scene, object->id, &world))
        return;
    float distance = fmaxf(3.5f, af_vec3_length(object->transform.scale) * 1.8f);
    af_vec3 center = {world.elements[12], world.elements[13], world.elements[14]};
    app->camera.position = af_vec3_sub(center, af_vec3_scale(af_camera_forward(&app->camera), distance));
}

nk_flags panel_flags(editor *app) {
    return NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | (app->runtime.playing ? NK_WINDOW_NO_INPUT : 0);
}

void label(struct nk_context *context, const char *text, float height) {
    nk_layout_row_dynamic(context, height, 1);
    nk_label(context, text, NK_TEXT_LEFT);
}

bool vector_fields(struct nk_context *context, const char *title, af_vec3 *value, float step, float increment) {
    label(context, title, 20);
    nk_layout_row_dynamic(context, 26, 3);
    bool changed = nk_property_float(context, "X", -1000.0f, &value->x, 1000.0f, step, increment);
    changed |= nk_property_float(context, "Y", -1000.0f, &value->y, 1000.0f, step, increment);
    changed |= nk_property_float(context, "Z", -1000.0f, &value->z, 1000.0f, step, increment);
    return changed;
}

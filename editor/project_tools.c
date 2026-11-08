/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "project_tools.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void read_startup_extension(editor *app) {
    char *directory = SDL_GetPrefPath("Afterlight", "Editor");
    if (!directory)
        return;
    int length = snprintf(app->startup_pref, sizeof(app->startup_pref), "%sstartup-extension.path", directory);
    SDL_free(directory);
    if (length < 0 || (size_t)length >= sizeof(app->startup_pref)) {
        app->startup_pref[0] = 0;
        return;
    }
    SDL_IOStream *file = SDL_IOFromFile(app->startup_pref, "rb");
    if (!file)
        return;
    Sint64 size = SDL_GetIOSize(file);
    if (size > 0 && size < (Sint64)sizeof(app->startup_extension) &&
        SDL_ReadIO(file, app->startup_extension, (size_t)size) == (size_t)size &&
        !memchr(app->startup_extension, 0, (size_t)size)) {
        app->startup_extension[size] = 0;
    } else
        app->startup_extension[0] = 0;
    SDL_CloseIO(file);
}

bool is_startup_extension_scheduled(const editor *app, const char *path) {
    if (!app->startup_extension[0] || !path || !path[0])
        return false;
    const char *p = app->startup_extension;
    size_t path_len = strlen(path);
    const char *p_base = strrchr(path, '/');
    p_base = p_base ? p_base + 1 : path;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        if (line_len && p[line_len - 1] == '\r')
            line_len--;
        if (line_len == path_len && !strncmp(p, path, path_len))
            return true;
        char line_buf[AF_ASSET_PATH_CAPACITY];
        if (line_len > 0 && line_len < sizeof(line_buf)) {
            memcpy(line_buf, p, line_len);
            line_buf[line_len] = '\0';
            const char *l_base = strrchr(line_buf, '/');
            l_base = l_base ? l_base + 1 : line_buf;
            if (!strcmp(l_base, p_base))
                return true;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    return false;
}

void schedule_startup_extension(editor *app, const char *path, bool enable) {
    if (!app->startup_pref[0]) {
        set_status(app, "Cannot access editor startup preferences.");
        return;
    }
    if (!path) {
        if (SDL_RemovePath(app->startup_pref)) {
            app->startup_extension[0] = 0;
            set_status(app, "Startup extension preferences cleared.");
        } else
            set_status(app, SDL_GetError());
        return;
    }
    char new_buf[sizeof(app->startup_extension)] = {0};
    size_t new_len = 0;
    const char *p = app->startup_extension;
    size_t path_len = strlen(path);
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        size_t check_len = (line_len && p[line_len - 1] == '\r') ? line_len - 1 : line_len;
        if (check_len == path_len && !strncmp(p, path, path_len)) {
            /* matched */
        } else if (line_len > 0) {
            if (new_len + check_len + 2 < sizeof(new_buf)) {
                memcpy(new_buf + new_len, p, check_len);
                new_len += check_len;
                new_buf[new_len++] = '\n';
            }
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    if (enable) {
        if (new_len + path_len + 2 < sizeof(new_buf)) {
            memcpy(new_buf + new_len, path, path_len);
            new_len += path_len;
            new_buf[new_len++] = '\n';
        }
    }
    new_buf[new_len] = 0;
    char temporary[AF_ASSET_PATH_CAPACITY + 32];
    snprintf(temporary, sizeof(temporary), "%s.%" PRIu64, app->startup_pref, (uint64_t)SDL_GetTicksNS());
    if (new_len == 0) {
        SDL_RemovePath(app->startup_pref);
        app->startup_extension[0] = 0;
        set_status(app, "Startup extension preference cleared.");
        return;
    }
    if (!SDL_SaveFile(temporary, new_buf, new_len) || !SDL_RenamePath(temporary, app->startup_pref)) {
        set_status(app, SDL_GetError());
        SDL_RemovePath(temporary);
        return;
    }
    memcpy(app->startup_extension, new_buf, sizeof(app->startup_extension));
    set_status(app, "Restart Afterlight Editor to activate scheduled startup extensions.");
}

void schedule_startup_upscaler(editor *app, const char *path, bool enable) {
    if (!app || !app->engine)
        return;
    size_t count = af_engine_extension_count(app->engine);
    for (size_t i = 0; i < count; ++i) {
        af_extension_info info;
        if (af_engine_get_extension(app->engine, i, &info) && info.vulkan_upscaler) {
            schedule_startup_extension(app, info.path, false);
        }
    }
    if (enable && path) {
        schedule_startup_extension(app, path, true);
    }
}

void add_editor_extension(ext_discovery *d, const char *path) {
    if (!path || !path[0] || *d->count >= AF_EXTENSION_LIMIT)
        return;
    const char *fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;
    for (size_t i = 0; i < *d->count; ++i) {
        if (!strcmp(d->paths[i], path))
            return;
        const char *existing_base = strrchr(d->paths[i], '/');
        existing_base = existing_base ? existing_base + 1 : d->paths[i];
        if (!strcmp(existing_base, fname))
            return;
    }
    size_t idx = *d->count;
    snprintf(d->storage[idx], sizeof(d->storage[idx]), "%s", path);
    d->paths[idx] = d->storage[idx];
    (*d->count)++;
}

void add_scheduled_startup_extensions(ext_discovery *d, const char *startup_extension) {
    if (!startup_extension || !startup_extension[0])
        return;
    const char *p = startup_extension;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t line_len = nl ? (size_t)(nl - p) : strlen(p);
        if (line_len && p[line_len - 1] == '\r')
            line_len--;
        if (line_len > 0 && line_len < AF_ASSET_PATH_CAPACITY) {
            char path[AF_ASSET_PATH_CAPACITY];
            memcpy(path, p, line_len);
            path[line_len] = '\0';
            add_editor_extension(d, path);
        }
        if (!nl)
            break;
        p = nl + 1;
    }
}

static SDL_EnumerationResult SDLCALL scan_editor_ext_file(void *userdata, const char *dirname, const char *fname) {
    ext_discovery *d = userdata;
    if (*d->count >= AF_EXTENSION_LIMIT)
        return SDL_ENUM_SUCCESS;
    const char *dot = strrchr(fname, '.');
    if (dot && (!strcmp(dot, ".so") || !strcmp(dot, ".dylib") || !strcmp(dot, ".dll"))) {
        char candidate[AF_ASSET_PATH_CAPACITY];
        int len = snprintf(candidate, sizeof(candidate), "%s/%s", dirname, fname);
        if (len <= 0 || len >= AF_ASSET_PATH_CAPACITY)
            return SDL_ENUM_CONTINUE;
        for (size_t i = 0; i < *d->count; ++i) {
            if (!strcmp(d->paths[i], candidate))
                return SDL_ENUM_CONTINUE;
            const char *existing_base = strrchr(d->paths[i], '/');
            existing_base = existing_base ? existing_base + 1 : d->paths[i];
            if (!strcmp(existing_base, fname))
                return SDL_ENUM_CONTINUE;
        }
        size_t idx = *d->count;
        memcpy(d->storage[idx], candidate, (size_t)len + 1);
        d->paths[idx] = d->storage[idx];
        (*d->count)++;
    }
    return SDL_ENUM_CONTINUE;
}

void discover_editor_ext_in_dir(const char *dir, ext_discovery *d) {
    if (!dir || *d->count >= AF_EXTENSION_LIMIT)
        return;
    SDL_PathInfo info;
    if (SDL_GetPathInfo(dir, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
        SDL_EnumerateDirectory(dir, scan_editor_ext_file, d);
    }
}

void poll_project_tool(editor *app) {
    if (!app->tool_process)
        return;
    int result;
    if (!SDL_WaitProcess(app->tool_process, false, &result))
        return;
    size_t size = 0;
    void *output = SDL_ReadProcess(app->tool_process, &size, &result);
    if (output) {
        if (size)
            fprintf(stdout, "Project tool: %.*s\n", (int)(size > 4096 ? 4096 : size), (const char *)output);
        SDL_free(output);
    }
    SDL_DestroyProcess(app->tool_process);
    app->tool_process = NULL;
    app->tool_success = result == 0;
    if (result) {
        set_status(app, "Project tool failed. Check the application log for details.");
        return;
    }
    if (app->tool_action == ED_TOOL_NEW) {
        set_status(app, "Project created. Configure/build its CMake project, then open it to Play.");
    } else if (app->tool_action == ED_TOOL_BUNDLE) {
        set_status(app, "Single-binary build completed.");
    } else
        set_status(app, "Export completed.");
}

void start_project_tool(editor *app, int mode) {
    if (app->tool_process || app->runtime.playing || app->dialog || app->modal)
        return;
    if (!app->wizard_folder[0]) {
        set_status(app, "Enter a destination path or directory.");
        return;
    }
    if (mode == ED_TOOL_NEW && !app->wizard_name[0]) {
        set_status(app, "Enter a project name.");
        return;
    }
    if (mode != ED_TOOL_NEW &&
        (!app->project_path[0] || app->project_is_template || ed_document_dirty(&app->document))) {
        set_status(app, "Save the scene and project before exporting or building.");
        return;
    }
    char tool[AF_ASSET_PATH_CAPACITY], player[AF_ASSET_PATH_CAPACITY];
    const char *base = af_engine_get_base_path(app->engine);
    SDL_PathInfo info;
    snprintf(tool, sizeof(tool), "%stools/scripts/afterlight.py", base ? base : "");
    if (!SDL_GetPathInfo(tool, &info))
        snprintf(tool, sizeof(tool), "%s", AF_PROJECT_TOOL_PATH);
    snprintf(player, sizeof(player), "%stools/afterlight_player.app", base ? base : "");
    if (!SDL_GetPathInfo(player, &info))
        snprintf(player, sizeof(player), "%s", AF_PLAYER_PACKAGE_PATH);
    const char *create[] = {AF_PYTHON_PATH, tool, "new", app->wizard_folder, "--name", app->wizard_name, NULL};
    const char *package[] = {AF_PYTHON_PATH, tool,   "export", app->project_path, "--output", app->wizard_folder,
                             "--player",     player, NULL};
    const char *bundle[] = {AF_PYTHON_PATH, tool,   "bundle", app->project_path, "--output", app->wizard_folder,
                            "--player",     player, NULL};
    const char **cmd = mode == ED_TOOL_NEW ? create : (mode == ED_TOOL_BUNDLE ? bundle : package);
    app->tool_success = false;
    app->tool_process = SDL_CreateProcess(cmd, true);
    app->tool_action = mode;
    set_status(app, app->tool_process ? (mode == ED_TOOL_NEW ? "Creating project..."
                                                             : (mode == ED_TOOL_BUNDLE ? "Building single binary..."
                                                                                       : "Exporting project..."))
                                      : SDL_GetError());
}

void project_panel(editor *app, float width, float height) {
    if (!app->show_project)
        return;
    struct nk_context *ctx = &app->gui.context;
    if (nk_begin(ctx, "Project", nk_rect(fmaxf(20, (width - 620) / 2), 70, 620, fminf(680, height - 100)),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE)) {
        nk_layout_row_dynamic(ctx, 32, 1);
        if (nk_button_label(ctx, "Extensions") && !app->modal && !app->dialog) {
            app->show_extensions = true;
            app->show_project = false;
        }
        label(ctx, app->project.name[0] ? app->project.name : "Create a project from this scene", 28);
        nk_layout_row_dynamic(ctx, 42, 1);
        nk_label_wrap(ctx, app->project_path[0] ? app->project_path : "No project file selected.");
        label(ctx, "Startup scene", 24);
        nk_layout_row_dynamic(ctx, 54, 1);
        nk_label_wrap(ctx, app->project.startup_scene);
        label(ctx, "C game module for Play", 24);
        nk_layout_row_dynamic(ctx, 54, 1);
        nk_label_wrap(ctx, app->module_path);
        nk_layout_row_dynamic(ctx, 34, 2);
        if (nk_button_label(ctx, "Open project") && !app->runtime.playing)
            request_action(app, ED_OPEN_PROJECT);
        if (nk_button_label(ctx, "Save project as") && !app->runtime.playing && !app->dialog) {
            if (!app->document.path[0] || ed_document_dirty(&app->document))
                set_status(app, "Save the startup scene before saving its project.");
            else {
                app->dialog = ed_dialog_begin_project(app->engine, true,
                                                      !app->project_is_template && app->project_path[0]
                                                          ? app->project_path
                                                          : "Untitled.afterlight.project");
                app->dialog_asset = 6;
            }
        }
        label(ctx, "New Project wizard / Export / Build", 28);
        label(ctx, "Project name (for starter project)", 22);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, app->wizard_name, sizeof(app->wizard_name),
                                       nk_filter_default);
        label(ctx, "Destination directory or binary path", 22);
        nk_layout_row_dynamic(ctx, 30, 1);
        nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, app->wizard_folder, sizeof(app->wizard_folder),
                                       nk_filter_default);
        nk_layout_row_dynamic(ctx, 34, 3);
        if (nk_button_label(ctx, app->tool_process ? "Working..." : "Create starter project") && !app->tool_process)
            start_project_tool(app, ED_TOOL_NEW);
        if (nk_button_label(ctx, "Export directory") && !app->tool_process)
            start_project_tool(app, ED_TOOL_EXPORT);
        if (nk_button_label(ctx, "Build single binary") && !app->tool_process)
            start_project_tool(app, ED_TOOL_BUNDLE);
        nk_layout_row_dynamic(ctx, 30, 1);
        if (nk_button_label(ctx, "Close project panel"))
            app->show_project = false;
    }
    nk_end(ctx);
}

void extensions_panel(editor *app, float width, float height) {
    if (!app->show_extensions)
        return;
    struct nk_context *ctx = &app->gui.context;
    nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE;
    flags |= app->modal || app->dialog ? NK_WINDOW_NO_INPUT : NK_WINDOW_MOVABLE;
    if (nk_begin(ctx, "Extensions", nk_rect(fmaxf(20, (width - 540) / 2), 100, 540, fminf(440, height - 140)), flags)) {
        nk_layout_row_dynamic(ctx, 46, 1);
        nk_label_wrap(ctx, "Load trusted native libraries. Startup extensions take effect after restart.");
        size_t count = af_engine_extension_count(app->engine);
        if (!count)
            label(ctx, "Built-in rendering. No extensions loaded.", 28);
        for (size_t i = 0; i < count; ++i) {
            af_extension_info info;
            if (!af_engine_get_extension(app->engine, i, &info))
                continue;
            nk_layout_row_dynamic(ctx, 30, 2);
            if (info.startup_status == AF_EXTENSION_RESTART_REQUIRED) {
                nk_bool scheduled = is_startup_extension_scheduled(app, info.path);
                if (nk_checkbox_label(ctx, info.id, &scheduled))
                    schedule_startup_extension(app, info.path, scheduled != 0);
                nk_label(ctx, scheduled ? "Scheduled (restart)" : "Restart required", NK_TEXT_LEFT);
                continue;
            }
            if (info.startup_status == AF_EXTENSION_STARTUP_UNAVAILABLE) {
                nk_label(ctx, info.id, NK_TEXT_LEFT);
                nk_label(ctx, "Vulkan requirements unavailable", NK_TEXT_LEFT);
                continue;
            }
            nk_bool enabled = info.enabled;
            if (nk_checkbox_label(ctx, info.id, &enabled)) {
                (void)af_engine_set_extension_enabled(app->engine, i, enabled != 0);
                if (info.vulkan_upscaler) {
                    schedule_startup_upscaler(app, info.path, enabled != 0);
                    if (enabled)
                        set_status(app, "Upscaler activated and saved for startup.");
                    else
                        set_status(app, "Upscaler deactivated. Startup preference cleared.");
                }
            }
            const char *state = info.enabled ? "Enabled" : "Disabled";
            if (info.vulkan_upscaler && info.enabled)
                state = info.upscaler_active ? "Upscaler active" : "Built-in rendering";
            nk_label(ctx, state, NK_TEXT_LEFT);
            if (info.vulkan_upscaler && info.enabled && info.upscaler_active) {
                uint32_t q_count = af_engine_upscaler_quality_count(app->engine);
                if (q_count > 1) {
                    nk_layout_row_dynamic(ctx, 28, 2);
                    nk_label(ctx, "Quality preset:", NK_TEXT_LEFT);
                    uint32_t current_q = af_engine_upscaler_quality(app->engine);
                    const char *q_names[16];
                    for (uint32_t q = 0; q < q_count && q < 16; ++q) {
                        const char *qn = af_engine_upscaler_quality_name(app->engine, q);
                        q_names[q] = qn ? qn : "Preset";
                    }
                    int selected_q = nk_combo(ctx, q_names, (int)q_count, (int)current_q, 24, nk_vec2(220, 140));
                    if (selected_q != (int)current_q && selected_q >= 0)
                        (void)af_engine_set_upscaler_quality(app->engine, (uint32_t)selected_q);
                }
            }
        }
        if (app->startup_extension[0]) {
            nk_layout_row_dynamic(ctx, 32, 1);
            if (nk_button_label(ctx, "Forget saved startup extensions"))
                schedule_startup_extension(app, NULL, false);
        }
        nk_layout_row_dynamic(ctx, 34, 2);
        if (nk_button_label(ctx, "Load extension") && !app->dialog) {
            (void)af_engine_set_mouse_capture(app->engine, false);
            app->dialog = ed_dialog_begin_extension(app->engine);
            app->dialog_asset = ED_DIALOG_EXTENSION;
            app->dialog_save = false;
            if (!app->dialog)
                set_status(app, "Cannot open the extension dialog.");
        }
        if (nk_button_label(ctx, "Close"))
            app->show_extensions = false;
    }
    nk_end(ctx);
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "dialog.h"
#include "core/platform_sdl.h"
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_filesystem.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ed_file_dialog {
    atomic_uint references;
    atomic_int state;
    char *path;
    char error[256];
};
void ed_dialog_release(ed_file_dialog *dialog) {
    if (dialog && atomic_fetch_sub(&dialog->references, 1) == 1) {
        free(dialog->path);
        free(dialog);
    }
}
static void SDLCALL completed(void *userdata, const char *const *files, int filter) {
    (void)filter;
    ed_file_dialog *dialog = userdata;
    int state;
    if (!files) {
        snprintf(dialog->error, sizeof(dialog->error), "%s", SDL_GetError());
        state = -1;
    } else if (!files[0])
        state = 2;
    else {
        size_t length = strlen(files[0]) + 1;
        dialog->path = malloc(length);
        if (dialog->path) {
            memcpy(dialog->path, files[0], length);
            state = 1;
        } else {
            snprintf(dialog->error, sizeof(dialog->error), "Not enough memory for file path.");
            state = -1;
        }
    }
    atomic_store_explicit(&dialog->state, state, memory_order_release);
    ed_dialog_release(dialog);
}
static ed_file_dialog *begin_dialog(af_engine *engine, bool save, const char *suggested_path,
                                    const SDL_DialogFileFilter *filters) {
    ed_file_dialog *dialog = calloc(1, sizeof(*dialog));
    if (!dialog)
        return NULL;
    atomic_init(&dialog->references, 2);
    atomic_init(&dialog->state, 0);
    if (save)
        SDL_ShowSaveFileDialog(completed, dialog, af_engine_sdl_window(engine), filters, 1, suggested_path);
    else
        SDL_ShowOpenFileDialog(completed, dialog, af_engine_sdl_window(engine), filters, 1, suggested_path, false);
    return dialog;
}
int ed_dialog_poll(ed_file_dialog *dialog, char *path, size_t capacity, char *error, size_t error_capacity) {
    int state = atomic_load_explicit(&dialog->state, memory_order_acquire);
    if (state == 1) {
        if (strlen(dialog->path) >= capacity) {
            snprintf(error, error_capacity, "File path is too long.");
            return -1;
        }
        snprintf(path, capacity, "%s", dialog->path);
    } else if (state == -1)
        snprintf(error, error_capacity, "%s", dialog->error);
    return state;
}

ed_file_dialog *ed_dialog_begin(af_engine *engine, bool save, const char *suggested_path) {
    static const SDL_DialogFileFilter scenes = {"Afterlight scenes", "json"};
    static const SDL_DialogFileFilter documents = {"Afterlight scenes and projects", "json;project"};
    return begin_dialog(engine, save, suggested_path, save ? &scenes : &documents);
}
ed_file_dialog *ed_dialog_begin_asset(af_engine *engine, af_asset_kind kind) {
    static const SDL_DialogFileFilter mesh = {"3D models (static or rigged)", "obj;gltf;glb"},
                                      texture = {"Images", "png;jpg;jpeg;bmp;tga"},
                                      audio = {"WAV sound effects", "wav"},
                                      environment = {"HDR equirectangular panoramas", "hdr"};
    return begin_dialog(engine, false, NULL,
                        kind == AF_ASSET_ENVIRONMENT ? &environment
                        : kind == AF_ASSET_MESH      ? &mesh
                        : kind == AF_ASSET_AUDIO     ? &audio
                                                     : &texture);
}

ed_file_dialog *ed_dialog_begin_module(af_engine *engine) {
    static const SDL_DialogFileFilter filter = {"C game libraries", "so;dylib;dll"};
    return begin_dialog(engine, false, NULL, &filter);
}

ed_file_dialog *ed_dialog_begin_extension(af_engine *engine, const char *project_path) {
    static const SDL_DialogFileFilter filter = {"Afterlight extension libraries", "so;dylib;dll"};
    char directory[AF_ASSET_PATH_CAPACITY];
    SDL_PathInfo info;

    /* 1. Project-local extensions folder */
    if (project_path && project_path[0]) {
        char proj_dir[AF_ASSET_PATH_CAPACITY];
        if (af_asset_resolve_path(project_path, "extensions", proj_dir, sizeof(proj_dir)) &&
            SDL_GetPathInfo(proj_dir, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            return begin_dialog(engine, false, proj_dir, &filter);
        }
    }

    const char *base = af_engine_get_base_path(engine);
    if (base) {
        /* 2. macOS app bundle Resources/extensions/ */
        int length = snprintf(directory, sizeof(directory), "%s../Resources/extensions/", base);
        if (length > 0 && (size_t)length < sizeof(directory) && SDL_GetPathInfo(directory, &info) &&
            info.type == SDL_PATHTYPE_DIRECTORY)
            return begin_dialog(engine, false, directory, &filter);

        /* 3. Beside macOS .app bundle: ../../../extensions/ */
        length = snprintf(directory, sizeof(directory), "%s../../../extensions/", base);
        if (length > 0 && (size_t)length < sizeof(directory) && SDL_GetPathInfo(directory, &info) &&
            info.type == SDL_PATHTYPE_DIRECTORY)
            return begin_dialog(engine, false, directory, &filter);

        /* 4. Linux/Windows installation base/../extensions/ */
        length = snprintf(directory, sizeof(directory), "%s../extensions/", base);
        if (length > 0 && (size_t)length < sizeof(directory) && SDL_GetPathInfo(directory, &info) &&
            info.type == SDL_PATHTYPE_DIRECTORY)
            return begin_dialog(engine, false, directory, &filter);

        /* 5. base/extensions/ */
        length = snprintf(directory, sizeof(directory), "%sextensions/", base);
        if (length > 0 && (size_t)length < sizeof(directory) && SDL_GetPathInfo(directory, &info) &&
            info.type == SDL_PATHTYPE_DIRECTORY)
            return begin_dialog(engine, false, directory, &filter);
    }

    /* 6. Workspace development directories */
    if (SDL_GetPathInfo("extensions", &info) && info.type == SDL_PATHTYPE_DIRECTORY)
        return begin_dialog(engine, false, "extensions", &filter);
    if (SDL_GetPathInfo("engine/extensions", &info) && info.type == SDL_PATHTYPE_DIRECTORY)
        return begin_dialog(engine, false, "engine/extensions", &filter);

    /* 7. Dedicated persistent user Extensions folder (~/Library/Application Support/Afterlight/Extensions on macOS) */
    char *pref = SDL_GetPrefPath("Afterlight", "Extensions");
    if (pref) {
        ed_file_dialog *dialog = begin_dialog(engine, false, pref, &filter);
        SDL_free(pref);
        return dialog;
    }

    return begin_dialog(engine, false, NULL, &filter);
}

ed_file_dialog *ed_dialog_begin_project(af_engine *engine, bool save, const char *path) {
    static const SDL_DialogFileFilter filter = {"Afterlight projects", "project"};
    return begin_dialog(engine, save, path, &filter);
}

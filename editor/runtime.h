/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef ED_RUNTIME_H
#define ED_RUNTIME_H
#include <afterlight/game_module.h>
#include <SDL3/SDL_loadso.h>
typedef struct ed_runtime {
    char current_scene[64];
    uint32_t authored_hash, authored_bytes;
    char scene_directory[AF_ASSET_PATH_CAPACITY], project_directory[AF_ASSET_PATH_CAPACITY], pending_scene[64];
    uint64_t transition_count;
    af_scene *scene;
    af_game_host host;
    const af_game_module *api;
    void *state;
    bool playing, paused;
    char status[256];
    SDL_SharedObject *library;
    char shadow_path[AF_ASSET_PATH_CAPACITY];
    char module_path[AF_ASSET_PATH_CAPACITY];
    uint64_t loaded_size, pending_size, reload_count;
    int64_t loaded_time, pending_time;
    double stable_time;
    bool auto_reload;
} ed_runtime;
bool ed_runtime_set_project(ed_runtime *, const char *project_path, const char *startup_scene);
bool ed_runtime_request_scene(ed_runtime *, const char *name);
bool ed_runtime_transition(ed_runtime *);
bool ed_runtime_start(ed_runtime *runtime, const af_scene *source, af_engine *engine, const af_game_module *module);
bool ed_runtime_start_file(ed_runtime *runtime, const af_scene *source, af_engine *engine, const char *path);
bool ed_runtime_scene_fingerprint(const ed_runtime *, const char *, uint32_t *, uint32_t *);
bool ed_runtime_capture_source(ed_runtime *);
bool ed_runtime_save_game(ed_runtime *, const char *path);
bool ed_runtime_load_game(ed_runtime *, const char *path);
bool ed_runtime_reload(ed_runtime *runtime);
void ed_runtime_poll_reload(ed_runtime *runtime, double delta);
void ed_runtime_frame_update(ed_runtime *runtime, double delta, bool input_enabled);
void ed_runtime_step(ed_runtime *runtime, double delta);
void ed_runtime_update(ed_runtime *runtime, double delta, bool input_enabled);
void ed_runtime_stop(ed_runtime *runtime);
#endif

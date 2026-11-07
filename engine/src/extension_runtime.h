/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EXTENSION_RUNTIME_H
#define AF_EXTENSION_RUNTIME_H

#include <SDL3/SDL_loadso.h>
#include <afterlight/asset.h>
#include <afterlight/extension.h>
#include <stddef.h>

typedef struct af_loaded_extension {
    SDL_SharedObject *library;
    const af_extension_api *api;
    const struct af_vulkan_upscaler_api *upscaler;
    const struct af_vulkan_startup_api *startup;
    af_extension_startup_status startup_status;
    char path[AF_ASSET_PATH_CAPACITY];
    void *state;
    bool enabled;
} af_loaded_extension;

typedef struct af_extensions {
    af_loaded_extension modules[AF_EXTENSION_LIMIT];
    size_t count;
    uint64_t presented_frames;
} af_extensions;

bool af_extensions_load(af_extensions *extensions, const char *path);
void af_extensions_present(af_extensions *extensions, uint64_t ticks, double delta);
void af_extensions_destroy(af_extensions *extensions);

#endif

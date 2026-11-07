/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_EXTENSION_RUNTIME_H
#define AF_EXTENSION_RUNTIME_H

#include <SDL3/SDL_loadso.h>
#include <afterlight/extension.h>
#include <stddef.h>

typedef struct af_loaded_extension {
    SDL_SharedObject *library;
    const af_extension_api *api;
    const struct af_vulkan_upscaler_api *upscaler;
    void *state;
} af_loaded_extension;

typedef struct af_extensions {
    af_loaded_extension modules[AF_EXTENSION_LIMIT];
    size_t count;
    uint64_t presented_frames;
    bool has_upscaler;
} af_extensions;

bool af_extensions_load(af_extensions *extensions, const char *path);
void af_extensions_present(af_extensions *extensions, uint64_t ticks, double delta);
void af_extensions_destroy(af_extensions *extensions);

#endif

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_EXTENSION_H
#define AFTERLIGHT_EXTENSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_EXTENSION_ABI_VERSION 1u
#define AF_EXTENSION_LIMIT 8u

#if defined(_WIN32)
#define AF_EXTENSION_EXPORT __declspec(dllexport)
#else
#define AF_EXTENSION_EXPORT __attribute__((visibility("default")))
#endif

typedef struct af_engine af_engine;
struct af_vulkan_upscaler_api;

typedef struct af_extension_host {
    uint32_t abi_version, struct_size;
    /* Available from start through stop. The host borrows message for the call. */
    void (*log)(const char *message);
} af_extension_host;

typedef struct af_extension_frame {
    uint32_t struct_size;
    uint64_t presented_frames;
    uint64_t simulation_ticks;
    double delta_seconds;
} af_extension_frame;

typedef struct af_extension_api {
    uint32_t abi_version, struct_size;
    const char *id; /* Immutable, unique identifier, e.g. "example.telemetry". */
    /* Return zero on success. On failure, release any partial state here.
       The extension owns all memory returned through state. */
    int32_t (*start)(const af_extension_host *host, void **state);
    /* Optional. Called after successful presentation, in load order. Frame
       data is borrowed for the call. No GPU completion guarantee. */
    void (*on_present)(void *state, const af_extension_frame *frame);
    void (*stop)(void *state);
    /* Optional ABI 1 tail. Hosts check struct_size before reading this field. */
    const struct af_vulkan_upscaler_api *vulkan_upscaler;
} af_extension_api;

typedef const af_extension_api *(*af_extension_get_api_fn)(uint32_t abi_version);

/* Modules export this symbol with C linkage and AF_EXTENSION_EXPORT. Return
   NULL for unsupported ABI versions. Keep the descriptor alive until unload. */
#ifdef AF_EXTENSION_BUILD
AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t abi_version);
#endif

typedef struct af_extension_info {
    const char *id;
    bool enabled, vulkan_upscaler, upscaler_active;
} af_extension_info;

/* Load a trusted native library before af_engine_run or from a game update
   callback on the main thread. Successfully started extensions stay resident
   and stop in reverse order during engine destruction. */
bool af_engine_load_extension(af_engine *engine, const char *path);
size_t af_engine_extension_count(const af_engine *engine);
/* The ID remains valid until engine destruction. */
bool af_engine_get_extension(const af_engine *engine, size_t index, af_extension_info *info);
/* Disabled extensions skip presentation callbacks. Enabling an upscaler
   disables the previous provider. The change applies on the next draw. */
bool af_engine_set_extension_enabled(af_engine *engine, size_t index, bool enabled);

#ifdef __cplusplus
}
#endif
#endif

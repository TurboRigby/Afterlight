/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "extension_runtime.h"
#include "log.h"
#include <afterlight/asset.h>
#include <afterlight/extension_vulkan.h>
#include <inttypes.h>
#include <string.h>

static void extension_log(const char *message) {
    if (message)
        af_log("extension", "%s", message);
}

static const af_extension_host host = {
    .abi_version = AF_EXTENSION_ABI_VERSION,
    .struct_size = sizeof(af_extension_host),
    .log = extension_log,
};

bool af_extensions_load(af_extensions *extensions, const char *path) {
    if (extensions->count == AF_EXTENSION_LIMIT) {
        af_log("error", "Too many engine extensions (limit %u).", AF_EXTENSION_LIMIT);
        return false;
    }
    char absolute[AF_ASSET_PATH_CAPACITY];
    if (!path || !path[0] || !af_asset_resolve_path(NULL, path, absolute, sizeof(absolute))) {
        af_log("error", "Invalid engine extension path.");
        return false;
    }
    SDL_SharedObject *library = SDL_LoadObject(absolute);
    if (!library) {
        af_log("error", "Loading extension %s: %s", absolute, SDL_GetError());
        return false;
    }

    SDL_FunctionPointer symbol = SDL_LoadFunction(library, "af_extension_get_api");
    af_extension_get_api_fn get_api = NULL;
    _Static_assert(sizeof(symbol) == sizeof(get_api), "Function pointer representation");
    memcpy(&get_api, &symbol, sizeof(get_api));
    if (!get_api) {
        af_log("error", "Extension %s does not export af_extension_get_api.", absolute);
        goto fail;
    }
    const af_extension_api *api = get_api(AF_EXTENSION_ABI_VERSION);
    if (!api || api->abi_version != AF_EXTENSION_ABI_VERSION ||
        api->struct_size < offsetof(af_extension_api, vulkan_upscaler) || !api->id || !api->id[0] || !api->start ||
        !api->stop) {
        af_log("error", "Extension %s has an incompatible ABI or incomplete descriptor.", absolute);
        goto fail;
    }
    for (size_t i = 0; i < extensions->count; ++i) {
        if (!strcmp(extensions->modules[i].api->id, api->id)) {
            af_log("error", "Engine extension %s is already loaded.", api->id);
            goto fail;
        }
    }
    const af_vulkan_upscaler_api *upscaler =
        api->struct_size >= offsetof(af_extension_api, vulkan_startup) ? api->vulkan_upscaler : NULL;
    if (upscaler && (upscaler->abi_version != AF_VULKAN_UPSCALER_ABI_VERSION ||
                     upscaler->struct_size < offsetof(af_vulkan_upscaler_api, flags) ||
                     !(upscaler->render_scale >= .5f && upscaler->render_scale <= 1.f) || !upscaler->create ||
                     !upscaler->record || !upscaler->destroy)) {
        af_log("error", "Extension %s has an incompatible Vulkan upscaler.", api->id);
        goto fail;
    }
    if (upscaler &&
        upscaler->struct_size >= offsetof(af_vulkan_upscaler_api, quality_scales) + sizeof(upscaler->quality_scales) &&
        upscaler->quality_count) {
        bool valid = upscaler->quality_count <= 16 && upscaler->quality_names && upscaler->quality_scales;
        for (uint32_t i = 0; valid && i < upscaler->quality_count; ++i)
            valid = upscaler->quality_names[i] && upscaler->quality_names[i][0] &&
                    upscaler->quality_scales[i] >= 1.f / 3.f && upscaler->quality_scales[i] <= 1.f;
        if (!valid) {
            af_log("error", "Extension %s has an incompatible Vulkan upscaler quality list.", api->id);
            goto fail;
        }
    }
    const af_vulkan_startup_api *startup = api->struct_size >= sizeof(*api) ? api->vulkan_startup : NULL;
    if (startup && (startup->abi_version != AF_VULKAN_STARTUP_ABI_VERSION || startup->struct_size < sizeof(*startup) ||
                    (!startup->instance_requirements && !startup->device_requirements))) {
        af_log("error", "Extension %s has an incompatible Vulkan startup descriptor.", api->id);
        goto fail;
    }
    void *state = NULL;
    int32_t status = api->start(&host, &state);
    if (status != 0) {
        af_log("error", "Engine extension %s failed to start (status %" PRId32 ").", api->id, status);
        goto fail;
    }
    af_loaded_extension *module = &extensions->modules[extensions->count++];
    *module = (af_loaded_extension){
        .library = library,
        .api = api,
        .upscaler = upscaler,
        .startup = startup,
        .state = state,
        .startup_status = startup ? AF_EXTENSION_RESTART_REQUIRED : AF_EXTENSION_LIVE,
    };
    memcpy(module->path, absolute, strlen(absolute) + 1);
    af_log("info", "Loaded engine extension %s.", api->id);
    return true;

fail:
    SDL_UnloadObject(library);
    return false;
}

void af_extensions_present(af_extensions *extensions, uint64_t ticks, double delta) {
    const af_extension_frame frame = {
        .struct_size = sizeof(af_extension_frame),
        .presented_frames = ++extensions->presented_frames,
        .simulation_ticks = ticks,
        .delta_seconds = delta,
    };
    for (size_t i = 0; i < extensions->count; ++i) {
        const af_loaded_extension *module = &extensions->modules[i];
        if (module->enabled && module->api->on_present)
            module->api->on_present(module->state, &frame);
    }
}

void af_extensions_destroy(af_extensions *extensions) {
    while (extensions->count) {
        const af_loaded_extension *module = &extensions->modules[--extensions->count];
        module->api->stop(module->state);
        SDL_UnloadObject(module->library);
    }
}

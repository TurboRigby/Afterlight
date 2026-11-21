/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer_internal.h"
#include "mesh_vert.h"
#include "mesh_frag.h"
#include "mesh_ray_frag.h"
#define af_fullscreen_vert af_sky_vertex
#include "fullscreen_vert.h"
#undef af_fullscreen_vert
#include "sky_frag.h"

bool af_r_vk_ok(VkResult result, const char *operation) {
    if (result == VK_SUCCESS)
        return true;
    af_log("error", "%s failed (VkResult %d).", operation, (int)result);
    return false;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                     VkDebugUtilsMessageTypeFlagsEXT type,
                                                     const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                     void *user_data) {
    (void)type;
    af_renderer *renderer = user_data;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        atomic_fetch_add(&renderer->validation_errors, 1);
    af_log((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "validation error" : "validation", "%s",
           data->pMessage);
    return VK_FALSE;
}

static bool instance_extension_available(const char *name) {
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) != VK_SUCCESS)
        return false;
    VkExtensionProperties *extensions = calloc(count, sizeof(*extensions));
    if (!extensions)
        return false;
    bool found = false;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, extensions) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i)
            if (strcmp(extensions[i].extensionName, name) == 0)
                found = true;
    }
    free(extensions);
    return found;
}

static bool validation_layer_available(void) {
    uint32_t count = 0;
    if (vkEnumerateInstanceLayerProperties(&count, NULL) != VK_SUCCESS)
        return false;
    VkLayerProperties *layers = calloc(count, sizeof(*layers));
    if (!layers)
        return false;
    bool found = false;
    if (vkEnumerateInstanceLayerProperties(&count, layers) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i)
            if (strcmp(layers[i].layerName, "VK_LAYER_KHRONOS_validation") == 0)
                found = true;
    }
    free(layers);
    return found;
}

static void negotiate_startup(af_renderer *, af_extensions *, const char **, uint32_t *,
                              const af_vulkan_requirements *supported, af_vulkan_requirements *enabled);

static void reject_startup(af_extensions *extensions) {
    for (size_t i = 0; i < extensions->count; ++i)
        if (extensions->modules[i].startup)
            extensions->modules[i].startup_status = AF_EXTENSION_STARTUP_UNAVAILABLE;
}

static bool create_instance(af_renderer *renderer, bool enable_validation, af_extensions *startup) {
    Uint32 sdl_count = 0;
    const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_count);
    if (!sdl_extensions) {
        af_log("error", "SDL Vulkan extensions: %s", SDL_GetError());
        return false;
    }
    const char **extensions = calloc((size_t)sdl_count + 2 + AF_STARTUP_EXTENSION_CAPACITY, sizeof(*extensions));
    if (!extensions)
        return false;
    uint32_t extension_count = sdl_count;
    for (Uint32 i = 0; i < sdl_count; ++i)
        extensions[i] = sdl_extensions[i];
    VkInstanceCreateFlags flags = 0;
    if (instance_extension_available(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        bool already_present = false;
        for (uint32_t i = 0; i < extension_count; ++i)
            if (strcmp(extensions[i], VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0)
                already_present = true;
        if (!already_present)
            extensions[extension_count++] = VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME;
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    renderer->validation_enabled = enable_validation && validation_layer_available() &&
                                   instance_extension_available(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (enable_validation && !renderer->validation_enabled)
        af_log("warning", "Validation requested but the layer or debug-utils extension is unavailable.");
    if (renderer->validation_enabled)
        extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    const char *validation_layer = "VK_LAYER_KHRONOS_validation";
    VkDebugUtilsMessengerCreateInfoEXT debug_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debug_callback,
        .pUserData = renderer};
    VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "Afterlight",
        .applicationVersion = VK_MAKE_VERSION(AF_VERSION_MAJOR, AF_VERSION_MINOR, AF_VERSION_PATCH),
        .pEngineName = "Afterlight",
        .engineVersion = VK_MAKE_VERSION(AF_VERSION_MAJOR, AF_VERSION_MINOR, AF_VERSION_PATCH),
        .apiVersion = VK_API_VERSION_1_1};
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion(&loader_version) == VK_SUCCESS && loader_version >= VK_API_VERSION_1_2)
        application.apiVersion = VK_API_VERSION_1_2;
    renderer->instance_api_version = application.apiVersion;
    uint32_t core_extension_count = extension_count;
    negotiate_startup(renderer, startup, extensions, &extension_count, NULL, NULL);
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pNext = renderer->validation_enabled ? &debug_info : NULL,
                                 .flags = flags,
                                 .pApplicationInfo = &application,
                                 .enabledExtensionCount = extension_count,
                                 .ppEnabledExtensionNames = extensions,
                                 .enabledLayerCount = renderer->validation_enabled ? 1u : 0u,
                                 .ppEnabledLayerNames = renderer->validation_enabled ? &validation_layer : NULL};
    VkResult result = vkCreateInstance(&info, NULL, &renderer->instance);
    if (result != VK_SUCCESS && extension_count > core_extension_count) {
        af_log("warning", "Vulkan instance rejected extension requirements. Retrying built-in rendering.");
        reject_startup(startup);
        info.enabledExtensionCount = core_extension_count;
        result = vkCreateInstance(&info, NULL, &renderer->instance);
    }
    free(extensions);
    if (!af_r_vk_ok(result, "vkCreateInstance"))
        return false;
    if (renderer->validation_enabled) {
        PFN_vkCreateDebugUtilsMessengerEXT create_debug = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            renderer->instance, "vkCreateDebugUtilsMessengerEXT");
        if (!create_debug || !af_r_vk_ok(create_debug(renderer->instance, &debug_info, NULL, &renderer->debug_messenger),
                                    "vkCreateDebugUtilsMessengerEXT"))
            return false;
        af_log("info", "Vulkan validation enabled.");
    }
    return true;
}

static bool device_extension_available(VkPhysicalDevice device, const char *name) {
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL) != VK_SUCCESS)
        return false;
    VkExtensionProperties *extensions = calloc(count, sizeof(*extensions));
    if (!extensions)
        return false;
    bool found = false;
    if (vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i)
            if (strcmp(extensions[i].extensionName, name) == 0)
                found = true;
    }
    free(extensions);
    return found;
}

static void negotiate_startup(af_renderer *renderer, af_extensions *startup, const char **names, uint32_t *count,
                              const af_vulkan_requirements *supported, af_vulkan_requirements *enabled) {
    bool device_stage = supported != NULL;
    for (size_t i = 0; i < startup->count; ++i) {
        af_loaded_extension *module = &startup->modules[i];
        const af_vulkan_startup_api *api = module->startup;
        if (!api || module->startup_status == AF_EXTENSION_STARTUP_UNAVAILABLE)
            continue;
        af_vulkan_requirements requirements = {.struct_size = sizeof(requirements)};
        bool ok =
            device_stage
                ? (!api->device_requirements || api->device_requirements(module->state, renderer->instance,
                                                                         renderer->physical_device, &requirements))
                : (!api->instance_requirements || api->instance_requirements(module->state, &requirements));
        uint32_t previous_count = *count;
        ok = ok && requirements.extension_count <= AF_VULKAN_STARTUP_MAX_EXTENSIONS &&
             (!requirements.buffer_device_address || (device_stage && supported->buffer_device_address)) &&
             (!requirements.storage_image_write_without_format ||
              (device_stage && supported->storage_image_write_without_format)) &&
             (!requirements.device_features ||
              (device_stage && !(requirements.device_features & ~supported->device_features)));
        for (uint32_t j = 0; ok && j < requirements.extension_count; ++j) {
            const char *name = requirements.extensions[j];
            if (!name || !*name || strlen(name) >= VK_MAX_EXTENSION_NAME_SIZE) {
                ok = false;
                break;
            }
            bool exists = false;
            for (uint32_t n = 0; n < *count; ++n)
                if (!strcmp(names[n], name)) {
                    exists = true;
                    break;
                }
            if (exists)
                continue;
            ok = device_stage ? device_extension_available(renderer->physical_device, name)
                              : instance_extension_available(name);
            if (ok)
                names[(*count)++] = name;
            else
                af_log("warning", "Extension %s requires unavailable Vulkan extension %s.", module->api->id, name);
        }
        if (!ok) {
            *count = previous_count;
            module->startup_status = AF_EXTENSION_STARTUP_UNAVAILABLE;
            af_log("warning", "Vulkan %s requirements unavailable for %s.", device_stage ? "device" : "instance",
                   module->api->id);
        } else if (device_stage) {
            module->startup_status = AF_EXTENSION_STARTUP_READY;
            enabled->buffer_device_address |= requirements.buffer_device_address;
            enabled->storage_image_write_without_format |= requirements.storage_image_write_without_format;
            enabled->device_features |= requirements.device_features;
        }
    }
}

static bool find_queue_families(af_renderer *renderer, VkPhysicalDevice device, uint32_t *graphics, uint32_t *present) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    if (!families)
        return false;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families);
    *graphics = UINT32_MAX;
    *present = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        VkBool32 supported = VK_FALSE;
        if (families[i].queueCount == 0)
            continue;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(device, i, renderer->surface, &supported) != VK_SUCCESS)
            continue;
        bool draws = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (draws && *graphics == UINT32_MAX)
            *graphics = i;
        if (supported && *present == UINT32_MAX)
            *present = i;
        if (draws && supported) {
            *graphics = *present = i;
            break;
        }
    }
    free(families);
    return *graphics != UINT32_MAX && *present != UINT32_MAX;
}

static bool create_device(af_renderer *renderer, af_extensions *startup) {
    uint32_t count = 0;
    if (!af_r_vk_ok(vkEnumeratePhysicalDevices(renderer->instance, &count, NULL), "vkEnumeratePhysicalDevices"))
        return false;
    if (count == 0) {
        af_log("error", "No Vulkan GPU found. Install a Vulkan driver (MoltenVK on macOS).");
        return false;
    }
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices)
        return false;
    if (!af_r_vk_ok(vkEnumeratePhysicalDevices(renderer->instance, &count, devices), "vkEnumeratePhysicalDevices")) {
        free(devices);
        return false;
    }
    int best_score = -1;
    VkPhysicalDeviceProperties selected_properties = {0};
    VkPhysicalDeviceDriverProperties selected_driver = {0};
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t graphics = 0, present = 0, formats = 0, modes = 0;
        VkPhysicalDeviceProperties properties;
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_1 || properties.limits.maxBoundDescriptorSets < 5 ||
            !device_extension_available(devices[i], VK_KHR_SWAPCHAIN_EXTENSION_NAME) ||
            !find_queue_families(renderer, devices[i], &graphics, &present))
            continue;
        if (vkGetPhysicalDeviceSurfaceFormatsKHR(devices[i], renderer->surface, &formats, NULL) != VK_SUCCESS ||
            vkGetPhysicalDeviceSurfacePresentModesKHR(devices[i], renderer->surface, &modes, NULL) != VK_SUCCESS ||
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(devices[i], renderer->surface, &capabilities) != VK_SUCCESS ||
            formats == 0 || modes == 0 || !(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
            continue;
        VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        if (properties.apiVersion >= VK_API_VERSION_1_2 ||
            device_extension_available(devices[i], VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
            VkPhysicalDeviceProperties2 query = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                                                 .pNext = &driver};
            vkGetPhysicalDeviceProperties2(devices[i], &query);
        }
#ifdef __APPLE__
        const bool prefer_moltenvk = true;
#else
        const bool prefer_moltenvk = false;
#endif
        int score = af_vulkan_device_score(properties.deviceType, driver.driverID, prefer_moltenvk);
        if (score > best_score) {
            best_score = score;
            renderer->physical_device = devices[i];
            renderer->graphics_family = graphics;
            renderer->present_family = present;
            selected_properties = properties;
            selected_driver = driver;
        }
    }
    free(devices);
    if (best_score < 0) {
        af_log("error", "No Vulkan 1.1 device supports graphics and presentation for this window.");
        return false;
    }
    uint32_t queue_families = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_families, NULL);
    VkQueueFamilyProperties *queue_properties = calloc(queue_families, sizeof(*queue_properties));
    if (!queue_properties)
        return false;
    vkGetPhysicalDeviceQueueFamilyProperties(renderer->physical_device, &queue_families, queue_properties);
    renderer->timestamp_bits = queue_properties[renderer->graphics_family].timestampValidBits;
    bool graphics_compute = (queue_properties[renderer->graphics_family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    free(queue_properties);
    renderer->timestamp_period = selected_properties.limits.timestampPeriod;
    float priority = 1.0f;
    uint32_t families[] = {renderer->graphics_family, renderer->present_family};
    VkDeviceQueueCreateInfo queues[2] = {0};
    uint32_t queue_count = families[0] == families[1] ? 1u : 2u;
    for (uint32_t i = 0; i < queue_count; ++i) {
        queues[i] = (VkDeviceQueueCreateInfo){.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                              .queueFamilyIndex = families[i],
                                              .queueCount = 1,
                                              .pQueuePriorities = &priority};
    }
    const char *extensions[5 + AF_STARTUP_EXTENSION_CAPACITY] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    uint32_t extension_count = 1;
    if (device_extension_available(renderer->physical_device, AF_PORTABILITY_SUBSET))
        extensions[extension_count++] = AF_PORTABILITY_SUBSET;
    VkPhysicalDeviceRayQueryFeaturesKHR ray = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, .pNext = &ray};
    VkPhysicalDeviceBufferDeviceAddressFeatures address = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, .pNext = &acceleration};
    bool query_available =
        selected_properties.apiVersion >= VK_API_VERSION_1_2 &&
        device_extension_available(renderer->physical_device, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        device_extension_available(renderer->physical_device, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME) &&
        device_extension_available(renderer->physical_device, VK_KHR_RAY_QUERY_EXTENSION_NAME);
    bool address_available =
        renderer->instance_api_version >= VK_API_VERSION_1_2 && selected_properties.apiVersion >= VK_API_VERSION_1_2;
    if (address_available) {
        address.pNext = query_available ? &acceleration : NULL;
        VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &address};
        vkGetPhysicalDeviceFeatures2(renderer->physical_device, &features);
    }
    bool address_supported = address.bufferDeviceAddress != 0;
    renderer->ray_supported = query_available && ray.rayQuery && acceleration.accelerationStructure &&
                              address.bufferDeviceAddress && !getenv("AFTERLIGHT_DISABLE_RAY_QUERY");
    if (renderer->ray_supported) {
        extensions[extension_count++] = VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME;
        extensions[extension_count++] = VK_KHR_RAY_QUERY_EXTENSION_NAME;
        // Enable the needed feature bits only, not all queried optional features.
        address = (VkPhysicalDeviceBufferDeviceAddressFeatures){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES,
            .pNext = &acceleration,
            .bufferDeviceAddress = VK_TRUE};
        acceleration = (VkPhysicalDeviceAccelerationStructureFeaturesKHR){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            .pNext = &ray,
            .accelerationStructure = VK_TRUE};
        ray = (VkPhysicalDeviceRayQueryFeaturesKHR){.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR,
                                                    .rayQuery = VK_TRUE};
    }
    af_log("info", "Hardware ray queries: %s.",
           renderer->ray_supported ? "available" : "unavailable (raster fallback)");
    VkPhysicalDeviceFeatures supported;
    vkGetPhysicalDeviceFeatures(renderer->physical_device, &supported);
    VkPhysicalDeviceFeatures enabled = {.samplerAnisotropy = supported.samplerAnisotropy,
                                        .independentBlend = supported.independentBlend};
    renderer->cluster_draw_limit = selected_properties.limits.maxDrawIndirectCount;
    renderer->cluster_gpu_supported = graphics_compute && supported.multiDrawIndirect &&
                                      supported.drawIndirectFirstInstance && renderer->cluster_draw_limit > 1;
    enabled.multiDrawIndirect = enabled.drawIndirectFirstInstance = renderer->cluster_gpu_supported;
    renderer->temporal_supported = supported.independentBlend != 0;
    if (!renderer->temporal_supported)
        af_log("info", "TAA requires independent attachment blending; TAA scenes will use FXAA on this device.");
    renderer->anisotropy = supported.samplerAnisotropy ? fminf(8, selected_properties.limits.maxSamplerAnisotropy) : 1;
    uint32_t core_extension_count = extension_count;
    af_vulkan_requirements supported_startup = {
        .buffer_device_address = address_supported,
        .storage_image_write_without_format = supported.shaderStorageImageWriteWithoutFormat != VK_FALSE,
        .device_features =
            supported.shaderStorageImageExtendedFormats ? AF_VULKAN_DEVICE_STORAGE_IMAGE_EXTENDED_FORMATS : 0,
    };
    af_vulkan_requirements enabled_startup = {0};
    negotiate_startup(renderer, startup, extensions, &extension_count, &supported_startup, &enabled_startup);
    bool address_requested = enabled_startup.buffer_device_address;
    enabled.shaderStorageImageWriteWithoutFormat = enabled_startup.storage_image_write_without_format;
    enabled.shaderStorageImageExtendedFormats =
        (enabled_startup.device_features & AF_VULKAN_DEVICE_STORAGE_IMAGE_EXTENDED_FORMATS) != 0;
    if (address_requested && !renderer->ray_supported)
        address = (VkPhysicalDeviceBufferDeviceAddressFeatures){
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, .bufferDeviceAddress = VK_TRUE};
    VkDeviceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                               .queueCreateInfoCount = queue_count,
                               .pQueueCreateInfos = queues,
                               .pNext = (renderer->ray_supported || address_requested) ? &address : NULL,
                               .pEnabledFeatures = &enabled,
                               .enabledExtensionCount = extension_count,
                               .ppEnabledExtensionNames = extensions};
    VkResult result = vkCreateDevice(renderer->physical_device, &info, NULL, &renderer->device);
    if (result != VK_SUCCESS &&
        (extension_count > core_extension_count || address_requested || enabled.shaderStorageImageWriteWithoutFormat ||
         enabled.shaderStorageImageExtendedFormats)) {
        af_log("warning", "Vulkan device rejected extension requirements. Retrying built-in rendering.");
        reject_startup(startup);
        info.enabledExtensionCount = core_extension_count;
        info.pNext = renderer->ray_supported ? &address : NULL;
        enabled.shaderStorageImageWriteWithoutFormat = VK_FALSE;
        enabled.shaderStorageImageExtendedFormats = VK_FALSE;
        result = vkCreateDevice(renderer->physical_device, &info, NULL, &renderer->device);
    }
    if (!af_r_vk_ok(result, "vkCreateDevice"))
        return false;
    vkGetDeviceQueue(renderer->device, renderer->graphics_family, 0, &renderer->graphics_queue);
    vkGetDeviceQueue(renderer->device, renderer->present_family, 0, &renderer->present_queue);
    af_log("info", "GPU: %s (Vulkan %u.%u.%u)", selected_properties.deviceName,
           VK_VERSION_MAJOR(selected_properties.apiVersion), VK_VERSION_MINOR(selected_properties.apiVersion),
           VK_VERSION_PATCH(selected_properties.apiVersion));
    af_log("info", "Driver: %s%s%s", selected_driver.driverName[0] ? selected_driver.driverName : "unknown",
           selected_driver.driverInfo[0] ? " | " : "", selected_driver.driverInfo);
    return true;
}

bool af_r_build_mesh_pipelines(af_renderer *renderer, const uint32_t *frag_spirv, size_t frag_bytes,
                               VkPipeline *out_opaque, VkPipeline *out_blend) {
    af_swapchain *swapchain = &renderer->swapchain;
    if (!frag_spirv || !frag_bytes || !out_opaque || !out_blend || !swapchain->pipeline_layout || !swapchain->render_pass)
        return false;
    *out_opaque = VK_NULL_HANDLE;
    *out_blend = VK_NULL_HANDLE;
    VkShaderModule vertex_shader = VK_NULL_HANDLE, fragment_shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof(af_mesh_vert), .pCode = af_mesh_vert};
    if (!af_r_vk_ok(vkCreateShaderModule(renderer->device, &shader_info, NULL, &vertex_shader),
                    "vkCreateShaderModule (vertex)"))
        return false;
    bool success = false;
    shader_info.codeSize = frag_bytes;
    shader_info.pCode = frag_spirv;
    if (!af_r_vk_ok(vkCreateShaderModule(renderer->device, &shader_info, NULL, &fragment_shader),
                    "vkCreateShaderModule (fragment)"))
        goto cleanup;
    VkPipelineShaderStageCreateInfo stages[] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                                 .module = vertex_shader,
                                                 .pName = "main"},
                                                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                 .module = fragment_shader,
                                                 .pName = "main"}};
    VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = sizeof(af_vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(af_vertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(af_vertex, color)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(af_vertex, normal)},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(af_vertex, uv)},
        {.location = 4, .binding = 0, .format = VK_FORMAT_R16G16B16A16_UINT, .offset = offsetof(af_vertex, joints)},
        {.location = 5, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(af_vertex, weights)}};
    VkPipelineVertexInputStateCreateInfo vertex_input = {.sType =
                                                             VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                         .vertexBindingDescriptionCount = 1,
                                                         .pVertexBindingDescriptions = &binding,
                                                         .vertexAttributeDescriptionCount = 6,
                                                         .pVertexAttributeDescriptions = attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType =
                                                           VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType =
                                                         VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE,
                                                     .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                     .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType =
                                                            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                   .depthTestEnable = VK_TRUE,
                                                   .depthWriteEnable = VK_TRUE,
                                                   .depthCompareOp = VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask =
                                                          VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendAttachmentState motion_attachment[3] = {attachment, attachment, attachment};
    VkPipelineColorBlendStateCreateInfo blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                    .attachmentCount = 3u,
                                                    .pAttachments = motion_attachment};
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                .dynamicStateCount = 2,
                                                .pDynamicStates = dynamic_states};
    VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                  .stageCount = 2,
                                                  .pStages = stages,
                                                  .pVertexInputState = &vertex_input,
                                                  .pInputAssemblyState = &assembly,
                                                  .pViewportState = &viewport,
                                                  .pRasterizationState = &raster,
                                                  .pMultisampleState = &multisample,
                                                  .pColorBlendState = &blending,
                                                  .pDynamicState = &dynamic,
                                                  .pDepthStencilState = &depth,
                                                  .layout = swapchain->pipeline_layout,
                                                  .renderPass = swapchain->render_pass,
                                                  .subpass = 0,
                                                  .basePipelineIndex = -1};
    success = af_r_vk_ok(
        vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, out_opaque),
        "vkCreateGraphicsPipelines");
    if (success) {
        depth.depthWriteEnable = VK_FALSE;
        attachment.blendEnable = VK_TRUE;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        motion_attachment[0] = attachment;
        if (!renderer->temporal_supported)
            motion_attachment[1] = attachment;
        success = af_r_vk_ok(
            vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, out_blend),
            "blend pipeline");
        if (!success) {
            vkDestroyPipeline(renderer->device, *out_opaque, NULL);
            *out_opaque = VK_NULL_HANDLE;
        }
    }
cleanup:
    if (fragment_shader)
        vkDestroyShaderModule(renderer->device, fragment_shader, NULL);
    vkDestroyShaderModule(renderer->device, vertex_shader, NULL);
    return success;
}

static bool create_pipeline(af_renderer *renderer) {
    af_swapchain *swapchain = &renderer->swapchain;
    if ((swapchain->resource_config & AF_RESOURCE_RAY_QUERY) && getenv("AFTERLIGHT_TEST_RAY_PIPELINE_FAIL"))
        return false;
    VkPushConstantRange push_range = {.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                      .size = 128};
    VkPhysicalDeviceProperties material_limits;
    vkGetPhysicalDeviceProperties(renderer->physical_device, &material_limits);
    if (material_limits.limits.maxBoundDescriptorSets < 5) {
        af_log("error", "Scene materials require five bound descriptor sets.");
        return false;
    }
    VkDescriptorSetLayout layouts[] = {
        renderer->frame_layout, af_vk_texture_layout(renderer->white), af_vk_shadows_layout(renderer->shadows),
        af_vk_texture_layout(renderer->flat_normal), af_vk_texture_layout(renderer->white)};
    VkPipelineLayoutCreateInfo layout_info = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                              .setLayoutCount = 5,
                                              .pSetLayouts = layouts,
                                              .pushConstantRangeCount = 1,
                                              .pPushConstantRanges = &push_range};
    if (!af_r_vk_ok(vkCreatePipelineLayout(renderer->device, &layout_info, NULL, &swapchain->pipeline_layout),
                    "vkCreatePipelineLayout"))
        return false;
    const uint32_t *frag_code = (swapchain->resource_config & AF_RESOURCE_RAY_QUERY) ? af_mesh_ray_frag : af_mesh_frag;
    size_t frag_bytes =
        (swapchain->resource_config & AF_RESOURCE_RAY_QUERY) ? sizeof(af_mesh_ray_frag) : sizeof(af_mesh_frag);
    if (!af_r_build_mesh_pipelines(renderer, frag_code, frag_bytes, &swapchain->pipeline, &swapchain->blend_pipeline))
        return false;
    VkShaderModule vertex_shader = VK_NULL_HANDLE, fragment_shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = sizeof(af_mesh_vert), .pCode = af_mesh_vert};
    if (!af_r_vk_ok(vkCreateShaderModule(renderer->device, &shader_info, NULL, &vertex_shader),
                    "vkCreateShaderModule (vertex)"))
        return false;
    bool success = false;
    shader_info.codeSize = frag_bytes;
    shader_info.pCode = frag_code;
    if (!af_r_vk_ok(vkCreateShaderModule(renderer->device, &shader_info, NULL, &fragment_shader),
                    "vkCreateShaderModule (fragment)"))
        goto cleanup;
    VkPipelineShaderStageCreateInfo stages[] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                                 .module = vertex_shader,
                                                 .pName = "main"},
                                                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                 .module = fragment_shader,
                                                 .pName = "main"}};
    VkVertexInputBindingDescription binding = {
        .binding = 0, .stride = sizeof(af_vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(af_vertex, position)},
        {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(af_vertex, color)},
        {.location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(af_vertex, normal)},
        {.location = 3, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(af_vertex, uv)},
        {.location = 4, .binding = 0, .format = VK_FORMAT_R16G16B16A16_UINT, .offset = offsetof(af_vertex, joints)},
        {.location = 5, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(af_vertex, weights)}};
    VkPipelineVertexInputStateCreateInfo vertex_input = {.sType =
                                                             VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                         .vertexBindingDescriptionCount = 1,
                                                         .pVertexBindingDescriptions = &binding,
                                                         .vertexAttributeDescriptionCount = 6,
                                                         .pVertexAttributeDescriptions = attributes};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType =
                                                           VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    /* Prefer opaque walls/floors at coincident liquid boundaries. Both water
       passes need identical depth bias to avoid alternating attenuation/scatter. */
    VkPipelineRasterizationStateCreateInfo raster = {.sType =
                                                         VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE,
                                                     .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                     .depthBiasEnable = VK_TRUE,
                                                     .depthBiasConstantFactor = 4,
                                                     .depthBiasSlopeFactor = 1,
                                                     .lineWidth = 1.0f};
    VkPipelineMultisampleStateCreateInfo multisample = {.sType =
                                                            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                   .depthTestEnable = VK_TRUE,
                                                   .depthWriteEnable = VK_FALSE,
                                                   .depthCompareOp = VK_COMPARE_OP_LESS};
    VkPipelineColorBlendAttachmentState attachment = {.blendEnable = VK_TRUE,
                                                      .srcColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                                                      .dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR,
                                                      .colorBlendOp = VK_BLEND_OP_ADD,
                                                      .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                                                      .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
                                                      .alphaBlendOp = VK_BLEND_OP_ADD,
                                                      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                                        VK_COLOR_COMPONENT_G_BIT |
                                                                        VK_COLOR_COMPONENT_B_BIT |
                                                                        VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendAttachmentState no_blend = {.colorWriteMask =
                                                        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
    VkPipelineColorBlendAttachmentState motion_attachment[3] = {
        attachment, renderer->temporal_supported ? no_blend : attachment, no_blend};
    VkPipelineColorBlendStateCreateInfo blending = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                    .attachmentCount = 3u,
                                                    .pAttachments = motion_attachment};
    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                .dynamicStateCount = 2,
                                                .pDynamicStates = dynamic_states};
    VkGraphicsPipelineCreateInfo pipeline_info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                  .stageCount = 2,
                                                  .pStages = stages,
                                                  .pVertexInputState = &vertex_input,
                                                  .pInputAssemblyState = &assembly,
                                                  .pViewportState = &viewport,
                                                  .pRasterizationState = &raster,
                                                  .pMultisampleState = &multisample,
                                                  .pColorBlendState = &blending,
                                                  .pDynamicState = &dynamic,
                                                  .pDepthStencilState = &depth,
                                                  .layout = swapchain->pipeline_layout,
                                                  .renderPass = swapchain->render_pass,
                                                  .subpass = 0,
                                                  .basePipelineIndex = -1};
    success = af_r_vk_ok(vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                                                   &swapchain->water_attenuation),
                         "water attenuation pipeline");
    if (success) {
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        motion_attachment[0] = attachment;
        if (!renderer->temporal_supported)
            motion_attachment[1] = attachment;
        success = af_r_vk_ok(vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &pipeline_info, NULL,
                                                       &swapchain->water_scattering),
                             "water scattering pipeline");
    }

cleanup:
    if (fragment_shader)
        vkDestroyShaderModule(renderer->device, fragment_shader, NULL);
    vkDestroyShaderModule(renderer->device, vertex_shader, NULL);
    return success;
}

static bool create_sky_pipeline(af_renderer *r) {
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
    bool success = false;
    VkShaderModuleCreateInfo code = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                     .codeSize = sizeof(af_sky_vertex),
                                     .pCode = af_sky_vertex};
    if (!af_r_vk_ok(vkCreateShaderModule(r->device, &code, NULL, &vertex), "sky vertex"))
        goto done;
    code.codeSize = sizeof(af_sky_frag);
    code.pCode = af_sky_frag;
    if (!af_r_vk_ok(vkCreateShaderModule(r->device, &code, NULL, &fragment), "sky fragment"))
        goto done;
    VkPipelineShaderStageCreateInfo stages[] = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_VERTEX_BIT,
                                                 .module = vertex,
                                                 .pName = "main"},
                                                {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                 .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                                                 .module = fragment,
                                                 .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo input = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly = {.sType =
                                                           VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
                                                       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkPipelineViewportStateCreateInfo viewport = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster = {.sType =
                                                         VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                     .polygonMode = VK_POLYGON_MODE_FILL,
                                                     .cullMode = VK_CULL_MODE_NONE,
                                                     .lineWidth = 1};
    VkPipelineMultisampleStateCreateInfo samples = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
                                                    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState attachment[3] = {
        {.colorWriteMask = 15}, {.colorWriteMask = 15}, {.colorWriteMask = 15}};
    VkPipelineColorBlendStateCreateInfo blend = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
                                                 .attachmentCount = 3u,
                                                 .pAttachments = attachment};
    VkPipelineDepthStencilStateCreateInfo depth = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
                                                .dynamicStateCount = 2,
                                                .pDynamicStates = states};
    VkGraphicsPipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                         .stageCount = 2,
                                         .pStages = stages,
                                         .pVertexInputState = &input,
                                         .pInputAssemblyState = &assembly,
                                         .pViewportState = &viewport,
                                         .pRasterizationState = &raster,
                                         .pMultisampleState = &samples,
                                         .pColorBlendState = &blend,
                                         .pDepthStencilState = &depth,
                                         .pDynamicState = &dynamic,
                                         .layout = r->swapchain.pipeline_layout,
                                         .renderPass = r->swapchain.render_pass,
                                         .basePipelineIndex = -1};
    success = af_r_vk_ok(vkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &info, NULL, &r->swapchain.sky_pipeline),
                    "sky pipeline");
done:
    if (vertex)
        vkDestroyShaderModule(r->device, vertex, NULL);
    if (fragment)
        vkDestroyShaderModule(r->device, fragment, NULL);
    return success;
}

static VkFormat select_depth_format(af_renderer *renderer) {
    const VkFormat formats[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM, VK_FORMAT_D24_UNORM_S8_UINT,
                                VK_FORMAT_D32_SFLOAT_S8_UINT};
    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(renderer->physical_device, formats[i], &properties);
        if ((properties.optimalTilingFeatures &
             (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
              VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) ==
            (VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
             VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT))
            return formats[i];
    }
    return VK_FORMAT_UNDEFINED;
}

static bool create_depth_attachment(af_renderer *renderer, af_depth_attachment *depth) {
    af_swapchain *swapchain = &renderer->swapchain;
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = swapchain->depth_format,
                              .extent = {swapchain->render_extent.width, swapchain->render_extent.height, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (swapchain->resource_config & AF_RESOURCE_UPSCALER)
        info.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (!af_r_vk_ok(vkCreateImage(renderer->device, &info, NULL, &depth->image), "vkCreateImage (depth)"))
        return false;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(renderer->device, depth->image, &requirements);
    depth->allocation_bytes = requirements.size;
    if (!af_vk_allocate(renderer->allocator, requirements, AF_VK_MEM_IMAGE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                        false, &depth->alloc) ||
        !af_r_vk_ok(vkBindImageMemory(renderer->device, depth->image, depth->alloc.memory, depth->alloc.offset),
               "vkBindImageMemory"))
        return false;
    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = depth->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapchain->depth_format,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}};
    return af_r_vk_ok(vkCreateImageView(renderer->device, &view_info, NULL, &depth->view), "vkCreateImageView (depth)");
}

static void destroy_upscaler_context(af_renderer *renderer) {
    af_swapchain *swapchain = &renderer->swapchain;
    if (swapchain->upscaler_created) {
        renderer->upscaler->destroy(swapchain->upscaler_context);
        swapchain->upscaler_created = false;
        swapchain->upscaler_context = NULL;
    }
}

static bool upscaler_has_qualities(const af_vulkan_upscaler_api *api) {
    return api && api->struct_size >= offsetof(af_vulkan_upscaler_api, quality_scales) + sizeof(api->quality_scales) &&
           api->quality_count && api->quality_names && api->quality_scales;
}

static uint32_t upscaler_quality_count(const af_vulkan_upscaler_api *api) {
    return upscaler_has_qualities(api) ? api->quality_count : 0;
}

static float upscaler_render_scale(const af_renderer *renderer) {
    const af_vulkan_upscaler_api *api = renderer->upscaler;
    if (!api)
        return 1.f;
    if (upscaler_has_qualities(api) && renderer->upscaler_quality < api->quality_count)
        return api->quality_scales[renderer->upscaler_quality];
    return api->render_scale;
}

void af_renderer_set_upscaler(af_renderer *renderer, const af_vulkan_upscaler_api *api, void *state) {
    if (renderer->upscaler == api && renderer->upscaler_state == state) {
        (void)af_renderer_set_upscaler_enabled(renderer, true);
        return;
    }
    af_renderer_release_upscaler(renderer);
    if (renderer->swapchain.resource_config & AF_RESOURCE_UPSCALER)
        renderer->swapchain_invalid = renderer->resize_pending = true;
    renderer->upscaler = api;
    renderer->upscaler_state = state;
    renderer->upscaler_enabled = true;
    renderer->upscaler_failed = false;
    renderer->upscaler_quality = 0;
}

bool af_renderer_set_upscaler_enabled(af_renderer *renderer, bool enabled) {
    if (!renderer || !renderer->upscaler)
        return false;
    if (renderer->upscaler_enabled != enabled) {
        renderer->upscaler_enabled = enabled;
        renderer->upscaler_failed = false;
    }
    return true;
}

bool af_renderer_upscaler_active(const af_renderer *renderer) {
    return renderer && renderer->upscaler_enabled && !renderer->upscaler_failed && renderer->swapchain.upscaler_created;
}

uint32_t af_renderer_upscaler_quality_count(const af_renderer *renderer) {
    return renderer ? upscaler_quality_count(renderer->upscaler) : 0;
}

const char *af_renderer_upscaler_quality_name(const af_renderer *renderer, uint32_t index) {
    if (!renderer || !upscaler_has_qualities(renderer->upscaler) || index >= renderer->upscaler->quality_count)
        return NULL;
    return renderer->upscaler->quality_names[index];
}

uint32_t af_renderer_upscaler_quality(const af_renderer *renderer) {
    return renderer ? renderer->upscaler_quality : 0;
}

bool af_renderer_set_upscaler_quality(af_renderer *renderer, uint32_t index) {
    if (!renderer || !renderer->upscaler || index >= upscaler_quality_count(renderer->upscaler))
        return false;
    if (renderer->upscaler_quality != index) {
        renderer->upscaler_quality = index;
        renderer->swapchain_invalid = renderer->resize_pending = true;
        renderer->temporal_valid = false;
    }
    return true;
}

void af_renderer_release_upscaler(af_renderer *renderer) {
    if (!renderer || !renderer->swapchain.upscaler_created)
        return;
    (void)af_renderer_wait_idle(renderer);
    destroy_upscaler_context(renderer);
}

void af_r_destroy_swapchain(af_renderer *renderer) {
    af_swapchain *swapchain = &renderer->swapchain;
    destroy_upscaler_context(renderer);
    af_vk_ui_release_pipeline(renderer->ui);
    af_vk_post_destroy(swapchain->post);
    af_vk_refraction_destroy(swapchain->refraction);
    if (swapchain->water_pass)
        vkDestroyRenderPass(renderer->device, swapchain->water_pass, NULL);
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        if (swapchain->framebuffers && swapchain->framebuffers[i])
            vkDestroyFramebuffer(renderer->device, swapchain->framebuffers[i], NULL);
        if (swapchain->depths) {
            af_depth_attachment *depth = &swapchain->depths[i];
            if (depth->view)
                vkDestroyImageView(renderer->device, depth->view, NULL);
            if (depth->image)
                vkDestroyImage(renderer->device, depth->image, NULL);
            af_vk_free(renderer->allocator, &depth->alloc);
        }
        if (swapchain->views && swapchain->views[i])
            vkDestroyImageView(renderer->device, swapchain->views[i], NULL);
        if (swapchain->present_ready && swapchain->present_ready[i])
            vkDestroySemaphore(renderer->device, swapchain->present_ready[i], NULL);
    }
    for (size_t i = 0; i < renderer->custom_shader_count; ++i) {
        if (renderer->custom_shaders[i].opaque_pipeline)
            vkDestroyPipeline(renderer->device, renderer->custom_shaders[i].opaque_pipeline, NULL);
        if (renderer->custom_shaders[i].blend_pipeline)
            vkDestroyPipeline(renderer->device, renderer->custom_shaders[i].blend_pipeline, NULL);
        renderer->custom_shaders[i].opaque_pipeline = VK_NULL_HANDLE;
        renderer->custom_shaders[i].blend_pipeline = VK_NULL_HANDLE;
    }
    if (swapchain->water_attenuation)
        vkDestroyPipeline(renderer->device, swapchain->water_attenuation, NULL);
    if (swapchain->water_scattering)
        vkDestroyPipeline(renderer->device, swapchain->water_scattering, NULL);
    if (swapchain->blend_pipeline)
        vkDestroyPipeline(renderer->device, swapchain->blend_pipeline, NULL);
    if (swapchain->sky_pipeline)
        vkDestroyPipeline(renderer->device, swapchain->sky_pipeline, NULL);
    if (swapchain->pipeline)
        vkDestroyPipeline(renderer->device, swapchain->pipeline, NULL);
    if (swapchain->pipeline_layout)
        vkDestroyPipelineLayout(renderer->device, swapchain->pipeline_layout, NULL);
    if (swapchain->render_pass)
        vkDestroyRenderPass(renderer->device, swapchain->render_pass, NULL);
    if (swapchain->handle)
        vkDestroySwapchainKHR(renderer->device, swapchain->handle, NULL);
    free(swapchain->images);
    free(swapchain->views);
    free(swapchain->framebuffers);
    free(swapchain->depths);
    free(swapchain->present_ready);
    *swapchain = (af_swapchain){0};
}

static uint32_t clamp_extent(uint32_t value, uint32_t minimum, uint32_t maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

bool af_r_create_swapchain(af_renderer *renderer, af_render_settings settings, uint32_t config) {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(renderer->window, &width, &height)) {
        af_log("error", "Framebuffer size: %s", SDL_GetError());
        return false;
    }
    if (width <= 0 || height <= 0)
        return true; /* Try again after restore. */
    VkSurfaceCapabilitiesKHR capabilities;
    if (!af_r_vk_ok(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(renderer->physical_device, renderer->surface, &capabilities),
               "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"))
        return false;
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width =
            clamp_extent((uint32_t)width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height =
            clamp_extent((uint32_t)height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    if (extent.width == 0 || extent.height == 0)
        return true;
    uint32_t format_count = 0;
    if (!af_r_vk_ok(vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface, &format_count, NULL),
               "vkGetPhysicalDeviceSurfaceFormatsKHR") ||
        format_count == 0)
        return false;
    VkSurfaceFormatKHR *formats = calloc(format_count, sizeof(*formats));
    if (!formats)
        return false;
    if (!af_r_vk_ok(
            vkGetPhysicalDeviceSurfaceFormatsKHR(renderer->physical_device, renderer->surface, &format_count, formats),
            "vkGetPhysicalDeviceSurfaceFormatsKHR")) {
        free(formats);
        return false;
    }
    VkSurfaceFormatKHR format = formats[0];
    if (format_count == 1 && format.format == VK_FORMAT_UNDEFINED)
        format.format = VK_FORMAT_B8G8R8A8_SRGB;
    for (uint32_t i = 0; i < format_count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
            formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = formats[i];
            break;
        }
    }
    free(formats);
    /* SDL sends size/expose events when the window first appears, and may
       send duplicate resize notifications. Keep the existing resources when
       the surface configuration is unchanged. Vulkan out-of-date/suboptimal
       results bypass this optimization even if the size did not change. */
    af_swapchain *existing = &renderer->swapchain;
    if (existing->handle && !renderer->swapchain_invalid && existing->extent.width == extent.width &&
        existing->extent.height == extent.height && existing->resource_config == config &&
        existing->surface_format.format == format.format && existing->surface_format.colorSpace == format.colorSpace) {
        renderer->resize_pending = false;
        return true;
    }
    uint32_t image_count = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && image_count > capabilities.maxImageCount)
        image_count = capabilities.maxImageCount;
    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    const VkCompositeAlphaFlagBitsKHR alpha_modes[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
    for (size_t i = 0; i < sizeof(alpha_modes) / sizeof(alpha_modes[0]); ++i) {
        if (capabilities.supportedCompositeAlpha & alpha_modes[i]) {
            composite = alpha_modes[i];
            break;
        }
    }
    if (!af_r_vk_ok(vkDeviceWaitIdle(renderer->device), "vkDeviceWaitIdle"))
        return false;
    af_r_destroy_swapchain(renderer);
    af_swapchain *swapchain = &renderer->swapchain;
    swapchain->extent = extent;
    swapchain->resource_config = config;
    bool upscale = (config & AF_RESOURCE_UPSCALER) != 0;
    float scale = upscale ? upscaler_render_scale(renderer) : 1.f;
    swapchain->render_extent = (VkExtent2D){
        (uint32_t)fmaxf(1, (float)extent.width * scale),
        (uint32_t)fmaxf(1, (float)extent.height * scale),
    };
    swapchain->surface_format = format;
    renderer->capture_supported =
        (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) &&
        (format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_B8G8R8A8_UNORM ||
         format.format == VK_FORMAT_R8G8B8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_UNORM);
    uint32_t families[] = {renderer->graphics_family, renderer->present_family};
    bool separate_queues = families[0] != families[1];
    VkSwapchainCreateInfoKHR info = {.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
                                     .surface = renderer->surface,
                                     .minImageCount = image_count,
                                     .imageFormat = format.format,
                                     .imageColorSpace = format.colorSpace,
                                     .imageExtent = extent,
                                     .imageArrayLayers = 1,
                                     .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                   (renderer->capture_supported ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
                                     .imageSharingMode =
                                         separate_queues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
                                     .queueFamilyIndexCount = separate_queues ? 2u : 0u,
                                     .pQueueFamilyIndices = separate_queues ? families : NULL,
                                     .preTransform = capabilities.currentTransform,
                                     .compositeAlpha = composite,
                                     .presentMode = VK_PRESENT_MODE_FIFO_KHR,
                                     .clipped = VK_TRUE};
    if (!af_r_vk_ok(vkCreateSwapchainKHR(renderer->device, &info, NULL, &swapchain->handle), "vkCreateSwapchainKHR"))
        return false;
    if (!af_r_vk_ok(vkGetSwapchainImagesKHR(renderer->device, swapchain->handle, &swapchain->image_count, NULL),
               "vkGetSwapchainImagesKHR") ||
        swapchain->image_count == 0)
        return false;
    swapchain->images = calloc(swapchain->image_count, sizeof(*swapchain->images));
    swapchain->views = calloc(swapchain->image_count, sizeof(*swapchain->views));
    swapchain->framebuffers = calloc(swapchain->image_count, sizeof(*swapchain->framebuffers));
    swapchain->depths = calloc(swapchain->image_count, sizeof(*swapchain->depths));
    swapchain->present_ready = calloc(swapchain->image_count, sizeof(*swapchain->present_ready));
    if (!swapchain->images || !swapchain->views || !swapchain->framebuffers || !swapchain->present_ready ||
        !swapchain->depths)
        return false;
    uint32_t retrieved_count = swapchain->image_count;
    if (!af_r_vk_ok(vkGetSwapchainImagesKHR(renderer->device, swapchain->handle, &retrieved_count, swapchain->images),
               "vkGetSwapchainImagesKHR"))
        return false;
    swapchain->image_count = retrieved_count;
    swapchain->depth_format = select_depth_format(renderer);
    if (swapchain->depth_format == VK_FORMAT_UNDEFINED) {
        af_log("error", "No supported depth attachment format.");
        return false;
    }
    VkAttachmentDescription attachments[4] = {{.format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                               .samples = VK_SAMPLE_COUNT_1_BIT,
                                               .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                               .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                               .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                               .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                               .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                              {.format = swapchain->depth_format,
                                               .samples = VK_SAMPLE_COUNT_1_BIT,
                                               .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                               .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                               .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                               .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                               .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}};
    attachments[2] = attachments[0];
    attachments[3] = attachments[0];
    VkAttachmentReference color_reference[] = {{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                               {.attachment = (config & AF_RESOURCE_MOTION) ? 2u : VK_ATTACHMENT_UNUSED,
                                                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                                               {.attachment = (config & AF_RESOURCE_RAY_GI) ? 3u : VK_ATTACHMENT_UNUSED,
                                                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
    VkAttachmentReference depth_reference = {.attachment = 1,
                                             .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    .colorAttachmentCount = 3u,
                                    .pColorAttachments = color_reference,
                                    .pDepthStencilAttachment = &depth_reference};
    VkPipelineStageFlags attachment_stages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkSubpassDependency dependencies[] = {
        {.srcSubpass = VK_SUBPASS_EXTERNAL,
         .dstSubpass = 0,
         .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | attachment_stages | VK_PIPELINE_STAGE_TRANSFER_BIT,
         .dstStageMask = attachment_stages,
         .srcAccessMask =
             VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT},
        {.srcSubpass = 0,
         .dstSubpass = VK_SUBPASS_EXTERNAL,
         .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT}};
    if (upscale)
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkRenderPassCreateInfo pass_info = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
                                        .attachmentCount = (config & AF_RESOURCE_RAY_GI)   ? 4u
                                                           : (config & AF_RESOURCE_MOTION) ? 3u
                                                                                           : 2u,
                                        .pAttachments = attachments,
                                        .subpassCount = 1,
                                        .pSubpasses = &subpass,
                                        .dependencyCount = 2,
                                        .pDependencies = dependencies};
    if (!af_r_vk_ok(vkCreateRenderPass(renderer->device, &pass_info, NULL, &swapchain->render_pass), "vkCreateRenderPass"))
        return false;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[3].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[2].loadOp = attachments[3].loadOp = attachments[0].loadOp = attachments[1].loadOp =
        VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    if (!af_r_vk_ok(vkCreateRenderPass(renderer->device, &pass_info, NULL, &swapchain->water_pass),
               "water load render pass"))
        return false;
    for (uint32_t i = 0; i < swapchain->image_count; ++i) {
        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = swapchain->images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
        if (!af_r_vk_ok(vkCreateImageView(renderer->device, &view_info, NULL, &swapchain->views[i]), "vkCreateImageView"))
            return false;
        if (!create_depth_attachment(renderer, &swapchain->depths[i]))
            return false;
        VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        if (!af_r_vk_ok(vkCreateSemaphore(renderer->device, &semaphore_info, NULL, &swapchain->present_ready[i]),
                   "vkCreateSemaphore"))
            return false;
    }
    VkImageView *depth_views = calloc(swapchain->image_count, sizeof(*depth_views));
    if (!depth_views)
        return false;
    for (uint32_t i = 0; i < swapchain->image_count; ++i)
        depth_views[i] = swapchain->depths[i].view;
    swapchain->post = af_vk_post_create(
        renderer->physical_device, renderer->device, renderer->allocator, extent,
        swapchain->render_extent, swapchain->image_count, swapchain->views, format.format, swapchain->render_pass,
        depth_views, settings, (config & AF_RESOURCE_MOTION) != 0, (config & AF_RESOURCE_RAY_GI) != 0, upscale);
    free(depth_views);
    swapchain->refraction =
        af_vk_refraction_create(renderer->device, renderer->allocator,
                                (config & AF_RESOURCE_REFRACTION) ? swapchain->render_extent : (VkExtent2D){1, 1},
                                swapchain->image_count, swapchain->depth_format);
    if (!swapchain->post || !swapchain->refraction || !create_pipeline(renderer) || !create_sky_pipeline(renderer))
        return false;
    for (size_t i = 0; i < renderer->custom_shader_count; ++i) {
        af_material_shader_asset *shader = &renderer->custom_shaders[i];
        if (!af_r_build_mesh_pipelines(renderer, shader->spirv, shader->word_count * sizeof(uint32_t),
                                       &shader->opaque_pipeline, &shader->blend_pipeline))
            return false;
    }
    if (renderer->ui && !af_vk_ui_create_pipeline(renderer->ui, af_vk_post_output_pass(swapchain->post)))
        return false;
    if (upscale) {
        const af_vulkan_upscaler_config upscaler_config = {
            .struct_size = sizeof(af_vulkan_upscaler_config),
            .instance = renderer->instance,
            .physical_device = renderer->physical_device,
            .device = renderer->device,
            .graphics_queue_family = renderer->graphics_family,
            .frames_in_flight = AF_FRAMES_IN_FLIGHT,
            .render_extent = swapchain->render_extent,
            .output_extent = extent,
            .depth_format = swapchain->depth_format,
            .color_format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .motion_format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .output_format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .quality = renderer->upscaler_quality,
        };
        if (!renderer->upscaler->create(renderer->upscaler_state, &upscaler_config, &swapchain->upscaler_context))
            return false;
        swapchain->upscaler_created = true;
    }
    renderer->resize_pending = false;
    renderer->temporal_valid = false;
    renderer->swapchain_invalid = false;
    ++renderer->swapchain_rebuilds;
    af_log("info", "Swapchain: %ux%u, %u images, FIFO.", extent.width, extent.height, swapchain->image_count);
    return true;
}

static bool create_frames(af_renderer *renderer) {
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                         .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                         .queueFamilyIndex = renderer->graphics_family};
    if (!af_r_vk_ok(vkCreateCommandPool(renderer->device, &pool_info, NULL, &renderer->command_pool), "vkCreateCommandPool"))
        return false;
    VkCommandBuffer commands[AF_FRAMES_IN_FLIGHT];
    VkCommandBufferAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                              .commandPool = renderer->command_pool,
                                              .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                              .commandBufferCount = AF_FRAMES_IN_FLIGHT};
    if (!af_r_vk_ok(vkAllocateCommandBuffers(renderer->device, &allocation, commands), "vkAllocateCommandBuffers"))
        return false;
    for (uint32_t i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
        renderer->frames[i].command = commands[i];
        VkSemaphoreCreateInfo semaphore_info = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                                        .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        if (!af_r_vk_ok(vkCreateSemaphore(renderer->device, &semaphore_info, NULL, &renderer->frames[i].acquired),
                   "vkCreateSemaphore") ||
            !af_r_vk_ok(vkCreateFence(renderer->device, &fence_info, NULL, &renderer->frames[i].fence), "vkCreateFence"))
            return false;
        if (renderer->timestamp_bits) {
            VkQueryPoolCreateInfo query = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                                           .queryType = VK_QUERY_TYPE_TIMESTAMP,
                                           .queryCount = 5};
            if (!af_r_vk_ok(vkCreateQueryPool(renderer->device, &query, NULL, &renderer->frames[i].timestamps),
                       "timestamp query pool"))
                return false;
        }
    }
    return true;
}


static bool create_frame_layout(af_renderer *renderer) {
    VkDescriptorSetLayoutBinding bindings[14];
    uint32_t count = 0;
    bindings[count++] = (VkDescriptorSetLayoutBinding){
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT};
    for (uint32_t i = 1; i < 6; ++i)
        bindings[count++] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                           VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    bindings[count++] =
        (VkDescriptorSetLayoutBinding){6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
    bindings[count++] = (VkDescriptorSetLayoutBinding){7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                       VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    bindings[count++] = (VkDescriptorSetLayoutBinding){8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                                       VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    bindings[count++] =
        (VkDescriptorSetLayoutBinding){9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
    if (renderer->ray_supported) {
        bindings[count++] = (VkDescriptorSetLayoutBinding){10, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1,
                                                           VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
        bindings[count++] = (VkDescriptorSetLayoutBinding){11, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                                           VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    }
    bindings[count++] =
        (VkDescriptorSetLayoutBinding){12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    bindings[count++] =
        (VkDescriptorSetLayoutBinding){13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
                                              .bindingCount = count,
                                              .pBindings = bindings};
    return af_r_vk_ok(vkCreateDescriptorSetLayout(renderer->device, &layout, NULL, &renderer->frame_layout), "frame layout");
}
static bool create_uniforms(af_renderer *renderer) {
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, AF_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 * AF_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (renderer->ray_supported ? 5u : 4u) * AF_FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, AF_FRAMES_IN_FLIGHT}};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                       .maxSets = AF_FRAMES_IN_FLIGHT,
                                       .poolSizeCount = renderer->ray_supported ? 4u : 3u,
                                       .pPoolSizes = sizes};
    if (!af_r_vk_ok(vkCreateDescriptorPool(renderer->device, &pool, NULL, &renderer->frame_pool), "frame pool"))
        return false;
    for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
        af_frame *frame = &renderer->frames[i];
        af_frame_uniform initial = {0};
        af_mat4 *bones = calloc(2 * AF_SKIN_MATRICES, sizeof(*bones));
        if (!bones)
            return false;
        for (unsigned j = 0; j < 2 * AF_SKIN_MATRICES; ++j)
            bones[j] = af_mat4_identity();
        bool bone_ok = af_r_upload_buffer(renderer, bones, 2 * AF_SKIN_MATRICES * sizeof(*bones),
                                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &frame->skin_buffer);
        free(bones);
        if (!bone_ok)
            return false;
        if (!af_r_upload_buffer(renderer, &initial, sizeof(initial), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &frame->uniform))
            return false;
        VkDescriptorSetAllocateInfo allocate = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                                .descriptorPool = renderer->frame_pool,
                                                .descriptorSetCount = 1,
                                                .pSetLayouts = &renderer->frame_layout};
        if (!af_r_vk_ok(vkAllocateDescriptorSets(renderer->device, &allocate, &frame->descriptor), "frame descriptor"))
            return false;
        VkDescriptorBufferInfo buffer = {frame->uniform.handle, 0, sizeof(initial)};
        VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstSet = frame->descriptor,
                                      .dstBinding = 0,
                                      .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                      .pBufferInfo = &buffer};
        vkUpdateDescriptorSets(renderer->device, 1, &write, 0, NULL);
        VkDescriptorBufferInfo bone_buffer = {frame->skin_buffer.handle, 0, 2 * AF_SKIN_MATRICES * sizeof(af_mat4)};
        write.dstBinding = 6;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.pBufferInfo = &bone_buffer;
        vkUpdateDescriptorSets(renderer->device, 1, &write, 0, NULL);
        af_motion_record initial_motion = {0};
        if (!af_r_upload_buffer(renderer, &initial_motion, sizeof(initial_motion), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                           &frame->motion_buffer))
            return false;
        frame->motion_buffer.capacity = sizeof(initial_motion);
        VkDescriptorBufferInfo motion_buffer = {frame->motion_buffer.handle, 0, VK_WHOLE_SIZE};
        write.dstBinding = 9;
        write.pBufferInfo = &motion_buffer;
        vkUpdateDescriptorSets(renderer->device, 1, &write, 0, NULL);
        VkDeviceSize light_bytes = (VkDeviceSize)AF_SCENE_MAX_LOCAL_LIGHTS * sizeof(af_gpu_light);
        if (!af_r_upload_buffer(renderer, NULL, light_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &frame->light_buffer))
            return false;
        frame->light_buffer.capacity = light_bytes;
        memset(frame->light_buffer.alloc.mapped, 0, (size_t)light_bytes);
        if (!af_vk_flush(renderer->allocator, &frame->light_buffer.alloc, 0, light_bytes))
            return false;
        VkDescriptorBufferInfo light_info = {frame->light_buffer.handle, 0, VK_WHOLE_SIZE};
        write.dstBinding = 12;
        write.pBufferInfo = &light_info;
        vkUpdateDescriptorSets(renderer->device, 1, &write, 0, NULL);
        VkDeviceSize tile_bytes = (VkDeviceSize)64u * sizeof(af_light_tile_mask);
        if (!af_r_upload_buffer(renderer, NULL, tile_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                &frame->light_tile_buffer))
            return false;
        frame->light_tile_buffer.capacity = tile_bytes;
        memset(frame->light_tile_buffer.alloc.mapped, 0, (size_t)tile_bytes);
        if (!af_vk_flush(renderer->allocator, &frame->light_tile_buffer.alloc, 0, tile_bytes))
            return false;
        VkDescriptorBufferInfo tile_info = {frame->light_tile_buffer.handle, 0, VK_WHOLE_SIZE};
        write.dstBinding = 13;
        write.pBufferInfo = &tile_info;
        vkUpdateDescriptorSets(renderer->device, 1, &write, 0, NULL);
    }
    af_r_update_environment_descriptors(renderer);
    return true;
}
af_renderer *af_renderer_create(SDL_Window *window, bool enable_validation, af_extensions *startup) {
    af_renderer *renderer = calloc(1, sizeof(*renderer));
    if (!renderer)
        return NULL;
    atomic_init(&renderer->validation_errors, 0);
    renderer->window = window;
    renderer->gpu_budget_ms = 1000.0 / 60;
    renderer->gpu_budget_bytes = UINT64_C(512) * 1024 * 1024;
    renderer->page_budget = UINT64_C(8) * 1024 * 1024;
    renderer->resize_pending = true;
    if (!create_instance(renderer, enable_validation, startup))
        goto fail;
    if (!SDL_Vulkan_CreateSurface(window, renderer->instance, NULL, &renderer->surface)) {
        af_log("error", "Vulkan surface creation: %s", SDL_GetError());
        goto fail;
    }
    if (!create_device(renderer, startup))
        goto fail;
    renderer->allocator = af_vk_allocator_create(renderer->physical_device, renderer->device, renderer->graphics_queue,
                                                 renderer->graphics_family);
    if (!renderer->allocator || !create_frames(renderer) || !create_frame_layout(renderer))
        goto fail;
    const unsigned char white[] = {255, 255, 255, 255};
    renderer->white = af_vk_texture_create(renderer->physical_device, renderer->device, renderer->allocator, white, 1,
                                           1, true, true, renderer->anisotropy);
    const unsigned char flat[] = {128, 128, 255, 255};
    renderer->flat_normal = af_vk_texture_create(renderer->physical_device, renderer->device, renderer->allocator, flat,
                                                 1, 1, false, true, renderer->anisotropy);
    af_shadow_settings shadows = af_shadow_settings_default();
    renderer->shadow_resolution = shadows.resolution;
    renderer->shadow_layers = shadows.mode == AF_SHADOW_CASCADED ? 4u : 1u;
    renderer->shadows = af_vk_shadows_create(renderer->physical_device, renderer->device, renderer->allocator,
                                             shadows.resolution, renderer->shadow_layers, renderer->frame_layout,
                                             af_vk_texture_layout(renderer->white));
    renderer->local_shadow_resolution = 512;
    renderer->local_shadow_layers = 1;
    renderer->local_shadows = af_vk_shadows_create(renderer->physical_device, renderer->device, renderer->allocator,
                                                   512, 1, renderer->frame_layout,
                                                   af_vk_texture_layout(renderer->white));
    if (!renderer->white || !renderer->flat_normal || !renderer->shadows || !renderer->local_shadows ||
        !af_r_create_environment(renderer, 0) || !create_uniforms(renderer) ||
        !af_r_create_swapchain(renderer, af_render_settings_default(), 0))
        goto fail;
    return renderer;
fail:
    af_renderer_destroy(renderer);
    return NULL;
}


void af_renderer_destroy(af_renderer *renderer) {
    if (!renderer)
        return;
    if (renderer->device) {
        (void)af_r_vk_ok(vkDeviceWaitIdle(renderer->device), "vkDeviceWaitIdle during shutdown");
        af_r_destroy_swapchain(renderer);
        af_r_destroy_cluster_compute(renderer);
        af_vk_ray_query_destroy(renderer->rays);
        af_r_destroy_buffer(renderer, &renderer->capture_buffer);
        af_vk_ui_destroy(renderer->ui);
        af_vk_shadows_destroy(renderer->local_shadows);
        af_vk_shadows_destroy(renderer->shadows);
        free(renderer->shadow_draws);
        free(renderer->shadow_bounds);
        for (size_t i = 0; i < renderer->texture_count; ++i)
            af_vk_texture_destroy(renderer->textures[i].texture);
        free(renderer->textures);
        for (size_t i = 0; i < renderer->custom_shader_count; ++i)
            free(renderer->custom_shaders[i].spirv);
        free(renderer->custom_shaders);
        af_vk_texture_destroy(renderer->white);
        af_vk_texture_destroy(renderer->flat_normal);
        for (size_t i = 0; i < renderer->environment_count; ++i) {
            af_vk_texture_destroy(renderer->environments[i].specular);
            af_vk_texture_destroy(renderer->environments[i].diffuse);
            af_vk_texture_destroy(renderer->environments[i].sky);
        }
        af_vk_texture_destroy(renderer->environment);
        af_vk_texture_destroy(renderer->irradiance);
        af_vk_texture_destroy(renderer->brdf);
        if (renderer->frame_pool)
            vkDestroyDescriptorPool(renderer->device, renderer->frame_pool, NULL);
        if (renderer->frame_layout)
            vkDestroyDescriptorSetLayout(renderer->device, renderer->frame_layout, NULL);
        for (size_t i = 0; i < renderer->mesh_count; ++i)
            af_r_stream_destroy(renderer, &renderer->meshes[i]);
        free(renderer->meshes);
        for (uint32_t i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
            af_r_destroy_buffer(renderer, &renderer->frames[i].uniform);
            af_r_destroy_buffer(renderer, &renderer->frames[i].skin_buffer);
            af_r_destroy_buffer(renderer, &renderer->frames[i].motion_buffer);
            af_r_destroy_buffer(renderer, &renderer->frames[i].morph_buffer);
            af_r_destroy_buffer(renderer, &renderer->frames[i].light_buffer);
            af_r_destroy_buffer(renderer, &renderer->frames[i].light_tile_buffer);
            if (renderer->frames[i].acquired)
                vkDestroySemaphore(renderer->device, renderer->frames[i].acquired, NULL);
            if (renderer->frames[i].timestamps)
                vkDestroyQueryPool(renderer->device, renderer->frames[i].timestamps, NULL);
            if (renderer->frames[i].fence)
                vkDestroyFence(renderer->device, renderer->frames[i].fence, NULL);
        }
        if (renderer->command_pool)
            vkDestroyCommandPool(renderer->device, renderer->command_pool, NULL);
        af_vk_allocator_destroy(renderer->allocator);
        renderer->allocator = NULL;
        vkDestroyDevice(renderer->device, NULL);
    }
    if (renderer->surface)
        SDL_Vulkan_DestroySurface(renderer->instance, renderer->surface, NULL);
    if (renderer->debug_messenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT destroy_debug = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            renderer->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (destroy_debug)
            destroy_debug(renderer->instance, renderer->debug_messenger, NULL);
    }
    if (renderer->instance)
        vkDestroyInstance(renderer->instance, NULL);
    free(renderer->ray_inputs);
    free(renderer->light_tile_staging);
    free(renderer->motion_previous);
    free(renderer->motion_next);
    free(renderer->motion_records);
    free(renderer->morph_draws);
    free(renderer->morph_vertices);
    free(renderer->morph_deltas);
    free(renderer);
}

void af_renderer_resize(af_renderer *renderer) {
    renderer->resize_pending = true;
}

bool af_renderer_wait_idle(af_renderer *renderer) {
    return af_r_vk_ok(vkDeviceWaitIdle(renderer->device), "vkDeviceWaitIdle");
}

af_rect af_r_resolve_viewport(af_renderer *renderer, af_rect rect) {
    int width = 0, height = 0;
    SDL_GetWindowSize(renderer->window, &width, &height);
    float w = (float)(width > 0 ? width : 1), h = (float)(height > 0 ? height : 1);
    if (!isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) || rect.width <= 0 ||
        rect.height <= 0)
        return (af_rect){0, 0, w, h};
    rect.x = fmaxf(0, fminf(w - 1, rect.x));
    rect.y = fmaxf(0, fminf(h - 1, rect.y));
    rect.width = fmaxf(1, fminf(w - rect.x, rect.width));
    rect.height = fmaxf(1, fminf(h - rect.y, rect.height));
    return rect;
}


static VkImageMemoryBarrier upscaler_barrier(VkImage image, VkImageAspectFlags aspect, VkImageLayout old_layout,
                                             VkImageLayout new_layout, VkAccessFlags source,
                                             VkAccessFlags destination) {
    return (VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = source,
        .dstAccessMask = destination,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
}

bool af_r_record_upscaler(af_renderer *r, VkCommandBuffer command, uint32_t image, af_rect area, float exposure,
                            const af_camera *camera, float frame_time_ms, bool reset_history) {
    af_swapchain *swapchain = &r->swapchain;
    af_depth_attachment depth = swapchain->depths[image];
    af_vulkan_upscaler_frame frame = {
        .struct_size = sizeof(af_vulkan_upscaler_frame),
        .frame_slot = r->frame_index,
        .reset_history = reset_history,
        .command_buffer = command,
        .depth = {depth.image, depth.view},
        .viewport_uv = {area.x, area.y, area.width, area.height},
        .jitter_pixels = {r->temporal_jitter.x * (float)swapchain->render_extent.width,
                          r->temporal_jitter.y * (float)swapchain->render_extent.height},
        .exposure_multiplier = exp2f(exposure),
        .camera_near = camera->near_clip,
        .camera_far = camera->far_clip,
        .vertical_fov_radians = camera->vertical_fov,
        .frame_time_ms = frame_time_ms,
    };
    af_vk_post_upscaler_images(swapchain->post, image, &frame);
    VkImageAspectFlags depth_aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (swapchain->depth_format == VK_FORMAT_D24_UNORM_S8_UINT ||
        swapchain->depth_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        depth_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    const VkImageLayout sampled = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageMemoryBarrier inputs[] = {
        upscaler_barrier(frame.color.image, VK_IMAGE_ASPECT_COLOR_BIT, sampled, sampled,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT),
        upscaler_barrier(frame.motion.image, VK_IMAGE_ASPECT_COLOR_BIT, sampled, sampled,
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT),
        upscaler_barrier(frame.depth.image, depth_aspects, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT),
        upscaler_barrier(frame.output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT),
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 4, inputs);
    if (!r->upscaler->record(swapchain->upscaler_context, &frame)) {
        af_log("warning", "Vulkan upscaler declined the frame. Restoring built-in rendering.");
        r->upscaler_failed = true;
        return false;
    }
    VkImageMemoryBarrier output =
        upscaler_barrier(frame.output.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, sampled,
                         VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                         0, NULL, 1, &output);
    return true;
}


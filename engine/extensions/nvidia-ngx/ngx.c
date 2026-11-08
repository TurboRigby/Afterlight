/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/extension_vulkan.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char project_id[] = "38be4447-7dfd-4d1f-915e-2af138fa57c3";
static const char engine_version[] = "0.23";
static const char *const quality_names[] = {
    "Quality",
    "Balanced",
    "Performance",
    "Ultra Performance",
};
static const float quality_scales[] = {2.f / 3.f, .58f, .5f, 1.f / 3.f};
static const NVSDK_NGX_PerfQuality_Value quality_values[] = {
    NVSDK_NGX_PerfQuality_Value_MaxQuality,
    NVSDK_NGX_PerfQuality_Value_Balanced,
    NVSDK_NGX_PerfQuality_Value_MaxPerf,
    NVSDK_NGX_PerfQuality_Value_UltraPerformance,
};
_Static_assert(sizeof(quality_names) / sizeof(quality_names[0]) == sizeof(quality_scales) / sizeof(quality_scales[0]) &&
                   sizeof(quality_names) / sizeof(quality_names[0]) ==
                       sizeof(quality_values) / sizeof(quality_values[0]),
               "DLSS quality tables must match");

typedef struct ngx_module {
    const af_extension_host *host;
    wchar_t *runtime_path, *data_path;
    const wchar_t *paths[1];
    NVSDK_NGX_FeatureCommonInfo feature_info;
    VkExtensionProperties instance_extensions[AF_VULKAN_STARTUP_MAX_EXTENSIONS];
    VkExtensionProperties device_extensions[AF_VULKAN_STARTUP_MAX_EXTENSIONS];
} ngx_module;

typedef struct ngx_context {
    const ngx_module *module;
    af_vulkan_upscaler_config config;
    NVSDK_NGX_Parameter *parameters;
    NVSDK_NGX_Handle *feature;
    VkCommandPool pool;
    VkCommandBuffer *frames;
    uint32_t quality;
    bool initialized, evaluated;
} ngx_context;

static bool ngx_ok(const ngx_module *module, NVSDK_NGX_Result result, const char *operation) {
    if (NVSDK_NGX_SUCCEED(result))
        return true;
    char message[160];
    snprintf(message, sizeof(message), "%s failed with NGX result 0x%08x.", operation, (unsigned)result);
    module->host->log(message);
    return false;
}

static wchar_t *wide_path(const char *path) {
    return path ? (wchar_t *)SDL_iconv_string("WCHAR_T", "UTF-8", path, strlen(path) + 1) : NULL;
}

static void stop(void *state) {
    ngx_module *module = state;
    SDL_free(module->runtime_path);
    SDL_free(module->data_path);
    free(module);
}

static int32_t start(const af_extension_host *host, void **state) {
    ngx_module *module = calloc(1, sizeof(*module));
    if (!module)
        return 1;
    module->host = host;
    const char *runtime = SDL_getenv("AFTERLIGHT_NGX_RUNTIME_DIR");
    if (!runtime || !runtime[0])
        runtime = AF_NGX_RUNTIME_DIR;
    char *directory = SDL_GetPrefPath("Afterlight", "NGX");
    module->runtime_path = wide_path(runtime);
    module->data_path = wide_path(directory);
    SDL_free(directory);
    if (!module->runtime_path || !module->data_path) {
        stop(module);
        return 2;
    }
    module->paths[0] = module->runtime_path;
    module->feature_info.PathListInfo = (NVSDK_NGX_PathListInfo){module->paths, 1};
    *state = module;
    return 0;
}

static NVSDK_NGX_FeatureDiscoveryInfo discovery(const ngx_module *module) {
    return (NVSDK_NGX_FeatureDiscoveryInfo){
        .SDKVersion = NVSDK_NGX_Version_API,
        .FeatureID = NVSDK_NGX_Feature_SuperSampling,
        .Identifier = {NVSDK_NGX_Application_Identifier_Type_Project_Id,
                       {.ProjectDesc = {project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, engine_version}}},
        .ApplicationDataPath = module->data_path,
        .FeatureInfo = &module->feature_info,
    };
}

static bool copy_requirements(af_vulkan_requirements *requirements, VkExtensionProperties *storage,
                              const VkExtensionProperties *properties, uint32_t count) {
    if (count > AF_VULKAN_STARTUP_MAX_EXTENSIONS || (count && !properties))
        return false;
    requirements->extension_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        storage[i] = properties[i];
        requirements->extensions[i] = storage[i].extensionName;
    }
    return true;
}

static bool instance_requirements(void *state, af_vulkan_requirements *requirements) {
    ngx_module *module = state;
    NVSDK_NGX_FeatureDiscoveryInfo info = discovery(module);
    uint32_t count = 0;
    VkExtensionProperties *properties = NULL;
    NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(&info, &count, &properties);
    return ngx_ok(module, result, "DLSS instance requirements") &&
           copy_requirements(requirements, module->instance_extensions, properties, count);
}

static bool device_requirements(void *state, VkInstance instance, VkPhysicalDevice device,
                                af_vulkan_requirements *requirements) {
    ngx_module *module = state;
    NVSDK_NGX_FeatureDiscoveryInfo info = discovery(module);
    NVSDK_NGX_FeatureRequirement supported = {0};
    NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_GetFeatureRequirements(instance, device, &info, &supported);
    if (!ngx_ok(module, result, "DLSS feature requirements") ||
        supported.FeatureSupported != NVSDK_NGX_FeatureSupportResult_Supported)
        return false;
    uint32_t count = 0;
    VkExtensionProperties *properties = NULL;
    result = NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(instance, device, &info, &count, &properties);
    requirements->buffer_device_address = true;
    requirements->storage_image_write_without_format = true;
    return ngx_ok(module, result, "DLSS device requirements") &&
           copy_requirements(requirements, module->device_extensions, properties, count);
}

static void destroy(void *state) {
    ngx_context *context = state;
    VkDevice device = context->config.device;
    if (context->feature)
        NVSDK_NGX_VULKAN_ReleaseFeature(context->feature);
    if (context->parameters)
        NVSDK_NGX_VULKAN_DestroyParameters(context->parameters);
    if (context->initialized)
        NVSDK_NGX_VULKAN_Shutdown1(device);
    if (context->pool)
        vkDestroyCommandPool(device, context->pool, NULL);
    free(context->frames);
    free(context);
}

static bool create_feature(ngx_context *context) {
    const af_vulkan_upscaler_config *config = &context->config;
    VkDevice device = config->device;
    VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = context->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer command;
    if (vkAllocateCommandBuffers(device, &allocate, &command) != VK_SUCCESS)
        return false;
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(command, &begin) != VK_SUCCESS)
        return false;
    NVSDK_NGX_DLSS_Create_Params parameters = {
        .Feature =
            {
                .InWidth = config->render_extent.width,
                .InHeight = config->render_extent.height,
                .InTargetWidth = config->output_extent.width,
                .InTargetHeight = config->output_extent.height,
                .InPerfQualityValue = quality_values[context->quality],
            },
        .InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                                NVSDK_NGX_DLSS_Feature_Flags_AutoExposure,
    };
    NVSDK_NGX_Result result =
        NGX_VULKAN_CREATE_DLSS_EXT1(device, command, 1, 1, &context->feature, context->parameters, &parameters);
    if (!ngx_ok(context->module, result, "DLSS feature creation") || vkEndCommandBuffer(command) != VK_SUCCESS)
        return false;
    VkQueue queue;
    vkGetDeviceQueue(device, config->graphics_queue_family, 0, &queue);
    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command,
    };
    if (vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS || vkQueueWaitIdle(queue) != VK_SUCCESS)
        return false;
    vkFreeCommandBuffers(device, context->pool, 1, &command);
    return true;
}

static bool create(void *state, const af_vulkan_upscaler_config *config, void **out) {
    ngx_module *module = state;
    if (config->struct_size < offsetof(af_vulkan_upscaler_config, quality) || !config->frames_in_flight)
        return false;
    ngx_context *context = calloc(1, sizeof(*context));
    if (!context)
        return false;
    context->module = module;
    context->config = *config;
    if (config->struct_size >= offsetof(af_vulkan_upscaler_config, quality) + sizeof(config->quality) &&
        config->quality < sizeof(quality_names) / sizeof(quality_names[0]))
        context->quality = config->quality;
    NVSDK_NGX_Result result = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        project_id, NVSDK_NGX_ENGINE_TYPE_CUSTOM, engine_version, module->data_path, config->instance,
        config->physical_device, config->device, vkGetInstanceProcAddr, vkGetDeviceProcAddr, &module->feature_info,
        NVSDK_NGX_Version_API);
    if (!ngx_ok(module, result, "Vulkan NGX initialization"))
        goto fail;
    context->initialized = true;
    result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&context->parameters);
    if (!ngx_ok(module, result, "NGX capability parameters"))
        goto fail;
    int available = 0;
    result = NVSDK_NGX_Parameter_GetI(context->parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    if (!ngx_ok(module, result, "DLSS availability query") || !available) {
        module->host->log("DLSS Super Resolution is unavailable on this device or SDK runtime.");
        goto fail;
    }
    VkCommandPoolCreateInfo pool = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = config->graphics_queue_family,
    };
    context->frames = calloc(config->frames_in_flight, sizeof(*context->frames));
    if (!context->frames || vkCreateCommandPool(config->device, &pool, NULL, &context->pool) != VK_SUCCESS)
        goto fail;
    VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = context->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
        .commandBufferCount = config->frames_in_flight,
    };
    if (vkAllocateCommandBuffers(config->device, &allocate, context->frames) != VK_SUCCESS || !create_feature(context))
        goto fail;
    *out = context;
    char message[128];
    snprintf(message, sizeof(message), "NVIDIA DLSS Super Resolution (%s): %ux%u -> %ux%u.",
             quality_names[context->quality], config->render_extent.width, config->render_extent.height,
             config->output_extent.width, config->output_extent.height);
    module->host->log(message);
    return true;
fail:
    destroy(context);
    return false;
}

static NVSDK_NGX_Resource_VK resource(af_vulkan_upscaler_image image, VkExtent2D extent, VkFormat format,
                                      VkImageAspectFlags aspects, bool writable) {
    return NVSDK_NGX_Create_ImageView_Resource_VK(image.view, image.image,
                                                  (VkImageSubresourceRange){aspects, 0, 1, 0, 1}, format, extent.width,
                                                  extent.height, writable);
}

static void depth_layout(VkCommandBuffer command, VkImage image, VkFormat format, bool for_ngx) {
    VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    VkImageLayout engine = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    VkImageLayout ngx = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = for_ngx ? VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = for_ngx ? engine : ngx,
        .newLayout = for_ngx ? ngx : engine,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {aspects, 0, 1, 0, 1},
    };
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0,
                         NULL, 1, &barrier);
}

static bool record(void *state, const af_vulkan_upscaler_frame *frame) {
    ngx_context *context = state;
    const af_vulkan_upscaler_config *config = &context->config;
    if (frame->struct_size < sizeof(*frame) || frame->frame_slot >= config->frames_in_flight || !frame->color.view ||
        !frame->motion.view || !frame->depth.view || !frame->output.view)
        return false;
    VkCommandBuffer command = context->frames[frame->frame_slot];
    VkCommandBufferInheritanceInfo inheritance = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = &inheritance,
    };
    /* Keep SDK commands off the primary buffer until evaluation succeeds. */
    if (vkResetCommandBuffer(command, 0) != VK_SUCCESS || vkBeginCommandBuffer(command, &begin) != VK_SUCCESS)
        return false;
    depth_layout(command, frame->depth.image, config->depth_format, true);
    VkExtent2D render = config->render_extent;
    NVSDK_NGX_Resource_VK color =
        resource(frame->color, render, config->color_format, VK_IMAGE_ASPECT_COLOR_BIT, false);
    NVSDK_NGX_Resource_VK motion =
        resource(frame->motion, render, config->motion_format, VK_IMAGE_ASPECT_COLOR_BIT, false);
    NVSDK_NGX_Resource_VK depth =
        resource(frame->depth, render, config->depth_format, VK_IMAGE_ASPECT_DEPTH_BIT, false);
    NVSDK_NGX_Resource_VK output =
        resource(frame->output, config->output_extent, config->output_format, VK_IMAGE_ASPECT_COLOR_BIT, true);
    NVSDK_NGX_VK_DLSS_Eval_Params parameters = {
        .Feature = {.pInColor = &color, .pInOutput = &output},
        .pInDepth = &depth,
        .pInMotionVectors = &motion,
        .InJitterOffsetX = frame->jitter_pixels[0],
        .InJitterOffsetY = frame->jitter_pixels[1],
        .InRenderSubrectDimensions = {render.width, render.height},
        .InReset = frame->reset_history || !context->evaluated,
        .InMVScaleX = (float)render.width,
        .InMVScaleY = (float)render.height,
        .InPreExposure = 1.f,
        .InExposureScale = 1.f,
    };
    NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT(command, context->feature, context->parameters, &parameters);
    if (!ngx_ok(context->module, result, "DLSS evaluation"))
        return false;
    depth_layout(command, frame->depth.image, config->depth_format, false);
    if (vkEndCommandBuffer(command) != VK_SUCCESS)
        return false;
    vkCmdExecuteCommands(frame->command_buffer, 1, &command);
    if (!context->evaluated)
        context->module->host->log("NVIDIA DLSS Super Resolution evaluated.");
    context->evaluated = true;
    return true;
}

AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t version) {
    static const af_vulkan_startup_api startup = {
        .abi_version = AF_VULKAN_STARTUP_ABI_VERSION,
        .struct_size = sizeof(startup),
        .instance_requirements = instance_requirements,
        .device_requirements = device_requirements,
    };
    static const af_vulkan_upscaler_api upscaler = {
        .abi_version = AF_VULKAN_UPSCALER_ABI_VERSION,
        .struct_size = sizeof(upscaler),
        .render_scale = 2.f / 3.f,
        .create = create,
        .record = record,
        .destroy = destroy,
        .quality_count = sizeof(quality_names) / sizeof(quality_names[0]),
        .quality_names = quality_names,
        .quality_scales = quality_scales,
    };
    static const af_extension_api api = {
        .abi_version = AF_EXTENSION_ABI_VERSION,
        .struct_size = sizeof(api),
        .id = "NVIDIA DLSS",
        .start = start,
        .stop = stop,
        .vulkan_upscaler = &upscaler,
        .vulkan_startup = &startup,
    };
    return version == AF_EXTENSION_ABI_VERSION ? &api : NULL;
}

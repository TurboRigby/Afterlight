/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/extension_vulkan.h>
#include <FidelityFX/host/backends/vk/ffx_vk.h>
#if AF_FSR_VERSION == 2
#include <FidelityFX/host/ffx_fsr2.h>
#else
#include <FidelityFX/host/ffx_fsr3upscaler.h>
#endif
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

namespace {
#if AF_FSR_VERSION == 2
constexpr const char *name = "AMD FSR 2.3.3";
using SdkContext = FfxFsr2Context;
using ContextDescription = FfxFsr2ContextDescription;
using DispatchDescription = FfxFsr2DispatchDescription;
constexpr auto sdk_create = ffxFsr2ContextCreate;
constexpr auto sdk_dispatch = ffxFsr2ContextDispatch;
constexpr auto sdk_destroy = ffxFsr2ContextDestroy;
constexpr uint32_t init_flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR2_ENABLE_AUTO_EXPOSURE;
#else
constexpr const char *name = "AMD FSR 3.1.4";
using SdkContext = FfxFsr3UpscalerContext;
using ContextDescription = FfxFsr3UpscalerContextDescription;
using DispatchDescription = FfxFsr3UpscalerDispatchDescription;
constexpr auto sdk_create = ffxFsr3UpscalerContextCreate;
constexpr auto sdk_dispatch = ffxFsr3UpscalerContextDispatch;
constexpr auto sdk_destroy = ffxFsr3UpscalerContextDestroy;
constexpr uint32_t init_flags = FFX_FSR3UPSCALER_ENABLE_HIGH_DYNAMIC_RANGE | FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
constexpr std::array<VkFormat, 3> shared_formats = {
    VK_FORMAT_R32_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R32_UINT,
};
#endif

struct Module {
    const af_extension_host *host;
    bool startup_checked = false;
};

struct Context {
    const af_extension_host *host;
    af_vulkan_upscaler_config config;
    alignas(std::max_align_t) SdkContext sdk{};
    void *scratch = nullptr;
    VkCommandPool pool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, FFX_MAX_QUEUED_FRAMES> commands{};
    bool created = false;
    bool dispatched = false;
#if AF_FSR_VERSION == 3
    struct Image { VkImage handle; VkDeviceMemory memory; };
    std::array<Image, shared_formats.size()> shared{};
#endif

    ~Context()
    {
        if (pool) vkDestroyCommandPool(config.device, pool, nullptr);
        if (created) sdk_destroy(&sdk);
#if AF_FSR_VERSION == 3
        for (const auto &image : shared) {
            if (image.handle) vkDestroyImage(config.device, image.handle, nullptr);
            if (image.memory) vkFreeMemory(config.device, image.memory, nullptr);
        }
#endif
        std::free(scratch);
    }
};

bool sdk_ok(const af_extension_host *host, FfxErrorCode code, const char *operation)
{
    if (code == FFX_OK) return true;
    char message[160];
    std::snprintf(message, sizeof(message), "%s %s failed (0x%08x).", name, operation,
                  static_cast<unsigned>(code));
    host->log(message);
    return false;
}

int32_t start(const af_extension_host *host, void **state)
{
    *state = new (std::nothrow) Module{host};
    return *state ? 0 : 1;
}

void stop(void *state) { delete static_cast<Module *>(state); }

bool device_requirements(void *state, VkInstance, VkPhysicalDevice device, af_vulkan_requirements *requirements)
{
    uint32_t loader_version = VK_API_VERSION_1_0;
    if (requirements->struct_size < sizeof(*requirements) ||
        vkEnumerateInstanceVersion(&loader_version) != VK_SUCCESS || loader_version < VK_API_VERSION_1_2)
        return false;
    VkPhysicalDeviceSubgroupProperties subgroup{};
    subgroup.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &subgroup;
    vkGetPhysicalDeviceProperties2(device, &properties);
    requirements->storage_image_write_without_format = true;
    requirements->device_features = AF_VULKAN_DEVICE_STORAGE_IMAGE_EXTENDED_FORMATS;
    auto &module = *static_cast<Module *>(state);
    module.startup_checked = properties.properties.apiVersion >= VK_API_VERSION_1_2 &&
                             (subgroup.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) &&
                             (subgroup.supportedOperations & VK_SUBGROUP_FEATURE_QUAD_BIT);
    return module.startup_checked;
}

bool compute_queue_supported(const af_vulkan_upscaler_config &config)
{
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(config.physical_device, &count, nullptr);
    std::unique_ptr<VkQueueFamilyProperties[]> families(new (std::nothrow) VkQueueFamilyProperties[count]);
    if (!families) return false;
    vkGetPhysicalDeviceQueueFamilyProperties(config.physical_device, &count, families.get());
    return config.graphics_queue_family < count &&
           (families[config.graphics_queue_family].queueFlags & VK_QUEUE_COMPUTE_BIT);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice device, const char *symbol)
{
    // The SDK asks for the extension spelling of this Vulkan 1.1 command.
    if (std::strcmp(symbol, "vkGetBufferMemoryRequirements2KHR") == 0)
        symbol = "vkGetBufferMemoryRequirements2";
    return vkGetDeviceProcAddr(device, symbol);
}

#if AF_FSR_VERSION == 3
bool create_shared_images(Context &context)
{
    const auto &config = context.config;
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(config.physical_device, &properties);
    for (size_t index = 0; index < context.shared.size(); ++index) {
        auto &image = context.shared[index];
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = shared_formats[index];
        info.extent = {config.render_extent.width, config.render_extent.height, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (vkCreateImage(config.device, &info, nullptr, &image.handle) != VK_SUCCESS) return false;

        VkMemoryRequirements memory;
        vkGetImageMemoryRequirements(config.device, image.handle, &memory);
        uint32_t type = 0;
        for (; type < properties.memoryTypeCount; ++type) {
            if ((memory.memoryTypeBits & (1u << type)) &&
                (properties.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
        }
        if (type == properties.memoryTypeCount) return false;
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = memory.size;
        allocate.memoryTypeIndex = type;
        if (vkAllocateMemory(config.device, &allocate, nullptr, &image.memory) != VK_SUCCESS ||
            vkBindImageMemory(config.device, image.handle, image.memory, 0) != VK_SUCCESS) return false;
    }
    return true;
}
#endif

bool create(void *state, const af_vulkan_upscaler_config *config, void **out)
{
    const auto &module = *static_cast<Module *>(state);
    const auto *host = module.host;
    if (!module.startup_checked) {
        host->log("AMD FSR requires an Afterlight build with Vulkan startup negotiation.");
        return false;
    }
    if (config->struct_size < sizeof(*config) || !config->frames_in_flight ||
        config->frames_in_flight > FFX_MAX_QUEUED_FRAMES ||
        config->color_format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        config->output_format != VK_FORMAT_R16G16B16A16_SFLOAT ||
        config->render_extent.width < 64 || config->render_extent.height < 64 ||
        !compute_queue_supported(*config)) return false;

    std::unique_ptr<Context> context(new (std::nothrow) Context{host, *config});
    if (!context) return false;
    size_t size = ffxGetScratchMemorySizeVK(config->physical_device, 1);
    context->scratch = std::calloc(1, size);
    if (!context->scratch) return false;

    ContextDescription description{};
    description.flags = init_flags;
    description.maxRenderSize = {config->render_extent.width, config->render_extent.height};
#if AF_FSR_VERSION == 2
    description.displaySize = {config->output_extent.width, config->output_extent.height};
#else
    description.maxUpscaleSize = {config->output_extent.width, config->output_extent.height};
    if (!create_shared_images(*context)) return false;
#endif
    VkDeviceContext device{config->device, config->physical_device, device_proc};
    if (!sdk_ok(host, ffxGetInterfaceVK(&description.backendInterface, ffxGetDeviceVK(&device),
                                      context->scratch, size, 1), "Vulkan backend setup")) return false;

    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = config->graphics_queue_family;
    if (vkCreateCommandPool(config->device, &pool, nullptr, &context->pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = context->pool;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    allocate.commandBufferCount = config->frames_in_flight;
    if (vkAllocateCommandBuffers(config->device, &allocate, context->commands.data()) != VK_SUCCESS ||
        !sdk_ok(host, sdk_create(&context->sdk, &description), "context creation")) return false;

    context->created = true;
    *out = context.release();
    char message[144];
    std::snprintf(message, sizeof(message), "%s (Quality): %ux%u -> %ux%u.", name,
                  config->render_extent.width, config->render_extent.height,
                  config->output_extent.width, config->output_extent.height);
    host->log(message);
    return true;
}

FfxResource resource(VkImage image, VkExtent2D extent, VkFormat format, FfxResourceUsage usage)
{
    FfxResourceDescription description{};
    description.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    description.format = ffxGetSurfaceFormatVK(format);
    description.width = extent.width;
    description.height = extent.height;
    description.depth = description.mipCount = 1;
    description.usage = usage;
    return ffxGetResourceVK(reinterpret_cast<void *>(image), description, nullptr,
        usage == FFX_RESOURCE_USAGE_UAV ? FFX_RESOURCE_STATE_UNORDERED_ACCESS : FFX_RESOURCE_STATE_COMPUTE_READ);
}

void transition_depth(VkCommandBuffer command, VkImage image, VkFormat format, bool for_sdk)
{
    VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
        aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = for_sdk ? VK_ACCESS_MEMORY_WRITE_BIT : VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    const auto host = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    const auto sdk = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.oldLayout = for_sdk ? host : sdk;
    barrier.newLayout = for_sdk ? sdk : host;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspects, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool record(void *state, const af_vulkan_upscaler_frame *frame)
{
    auto &context = *static_cast<Context *>(state);
    const auto &config = context.config;
    if (frame->struct_size < sizeof(*frame) || frame->frame_slot >= config.frames_in_flight ||
        !frame->color.image || !frame->depth.image || !frame->motion.image || !frame->output.image) return false;
    VkCommandBuffer command = context.commands[frame->frame_slot];
    VkCommandBufferInheritanceInfo inheritance{};
    inheritance.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    begin.pInheritanceInfo = &inheritance;
    if (vkResetCommandBuffer(command, 0) != VK_SUCCESS ||
        vkBeginCommandBuffer(command, &begin) != VK_SUCCESS) return false;

    // The temporal resources are shared across the renderer's in-flight frames.
    VkMemoryBarrier history{};
    history.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    history.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    history.dstAccessMask = history.srcAccessMask | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    constexpr auto sdk_stages = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    vkCmdPipelineBarrier(command, sdk_stages, sdk_stages,
                         0, 1, &history, 0, nullptr, 0, nullptr);
    transition_depth(command, frame->depth.image, config.depth_format, true);

    DispatchDescription dispatch{};
    const auto render = config.render_extent;
    dispatch.commandList = ffxGetCommandListVK(command);
    dispatch.color = resource(frame->color.image, render, config.color_format, FFX_RESOURCE_USAGE_READ_ONLY);
    dispatch.depth = resource(frame->depth.image, render, config.depth_format, FFX_RESOURCE_USAGE_DEPTHTARGET);
    dispatch.motionVectors = resource(frame->motion.image, render, config.motion_format, FFX_RESOURCE_USAGE_READ_ONLY);
    dispatch.output = resource(frame->output.image, config.output_extent, config.output_format, FFX_RESOURCE_USAGE_UAV);
    dispatch.jitterOffset = {frame->jitter_pixels[0], frame->jitter_pixels[1]};
    dispatch.motionVectorScale = {static_cast<float>(render.width), static_cast<float>(render.height)};
    dispatch.renderSize = {render.width, render.height};
    dispatch.frameTimeDelta = frame->frame_time_ms;
    dispatch.preExposure = 1.f;
    dispatch.reset = frame->reset_history || !context.dispatched;
    dispatch.cameraNear = frame->camera_near;
    dispatch.cameraFar = frame->camera_far;
    dispatch.cameraFovAngleVertical = frame->vertical_fov_radians;
    dispatch.viewSpaceToMetersFactor = 1.f;
#if AF_FSR_VERSION == 3
    if (!context.dispatched) {
        std::array<VkImageMemoryBarrier, shared_formats.size()> barriers{};
        for (size_t index = 0; index < barriers.size(); ++index) {
            auto &barrier = barriers[index];
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = context.shared[index].handle;
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        }
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
    }
    dispatch.upscaleSize = {config.output_extent.width, config.output_extent.height};
    dispatch.dilatedDepth = resource(context.shared[0].handle, render, shared_formats[0], FFX_RESOURCE_USAGE_UAV);
    dispatch.dilatedMotionVectors = resource(context.shared[1].handle, render, shared_formats[1], FFX_RESOURCE_USAGE_UAV);
    dispatch.reconstructedPrevNearestDepth = resource(context.shared[2].handle, render, shared_formats[2],
                                                      FFX_RESOURCE_USAGE_UAV);
#endif
    if (!sdk_ok(context.host, sdk_dispatch(&context.sdk, &dispatch), "dispatch")) return false;
    transition_depth(command, frame->depth.image, config.depth_format, false);
    if (vkEndCommandBuffer(command) != VK_SUCCESS) return false;
    vkCmdExecuteCommands(frame->command_buffer, 1, &command);
    if (!context.dispatched) {
        char message[80];
        std::snprintf(message, sizeof(message), "%s dispatched.", name);
        context.host->log(message);
    }
    context.dispatched = true;
    return true;
}

void destroy(void *state) { delete static_cast<Context *>(state); }
} // namespace

// The SDK's Vulkan backend always references this callback. Upscaling never calls it.
FfxErrorCode ffxSetFrameGenerationConfigToSwapchainVK(const FfxFrameGenerationConfig *)
{
    return FFX_ERROR_INVALID_ARGUMENT;
}

AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t version)
{
    static const af_vulkan_startup_api startup = {
        AF_VULKAN_STARTUP_ABI_VERSION, sizeof(startup), nullptr, device_requirements,
    };
    static const af_vulkan_upscaler_api upscaler = {
        AF_VULKAN_UPSCALER_ABI_VERSION, sizeof(upscaler), 2.f / 3.f, create, record, destroy, 0,
    };
    static const af_extension_api api = {
        AF_EXTENSION_ABI_VERSION, sizeof(api), name, start, nullptr, stop, &upscaler, &startup,
    };
    return version == AF_EXTENSION_ABI_VERSION ? &api : nullptr;
}

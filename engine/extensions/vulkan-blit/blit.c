/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/extension_vulkan.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct blit_context {
    af_vulkan_upscaler_config config;
} blit_context;

static int32_t start(const af_extension_host *host, void **state)
{
    *state = (void *)host;
    return 0;
}

static void stop(void *state) { (void)state; }

static bool create(void *state, const af_vulkan_upscaler_config *config, void **out)
{
    const af_extension_host *host = state;
    if (config->struct_size < sizeof(*config)) return false;
    VkFormatProperties source, destination;
    vkGetPhysicalDeviceFormatProperties(config->physical_device, config->color_format, &source);
    vkGetPhysicalDeviceFormatProperties(config->physical_device, config->output_format, &destination);
    const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                                         VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((source.optimalTilingFeatures & required) != required ||
        !(destination.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        host->log("Linear HDR blit unsupported.");
        return false;
    }
    blit_context *context = malloc(sizeof(*context));
    if (!context) return false;
    context->config = *config;
    *out = context;
    char message[128];
    snprintf(message, sizeof(message), "Vulkan blit upscaler: %ux%u -> %ux%u.",
             config->render_extent.width, config->render_extent.height,
             config->output_extent.width, config->output_extent.height);
    host->log(message);
    return true;
}

static bool record(void *state, const af_vulkan_upscaler_frame *frame)
{
    const blit_context *context = state;
    const af_vulkan_upscaler_config *config = &context->config;
    if (frame->struct_size < sizeof(*frame) || frame->frame_slot >= config->frames_in_flight ||
        !frame->color.view || !frame->depth.view || !frame->motion.view || !frame->output.view)
        return false;

    VkImageMemoryBarrier source = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = frame->color.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkCommandBuffer command = frame->command_buffer;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, NULL, 0, NULL, 1, &source);
    VkImageBlit region = {
        .srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
        .srcOffsets = {{0, 0, 0}, {(int32_t)config->render_extent.width, (int32_t)config->render_extent.height, 1}},
        .dstOffsets = {{0, 0, 0}, {(int32_t)config->output_extent.width, (int32_t)config->output_extent.height, 1}},
    };
    vkCmdBlitImage(command, frame->color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   frame->output.image, VK_IMAGE_LAYOUT_GENERAL, 1, &region, VK_FILTER_LINEAR);
    source.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    source.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    source.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    source.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 0, NULL, 0, NULL, 1, &source);
    return true;
}

static void destroy(void *context) { free(context); }

AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t version)
{
    static const af_vulkan_upscaler_api upscaler = {
        .abi_version = AF_VULKAN_UPSCALER_ABI_VERSION, .struct_size = sizeof(af_vulkan_upscaler_api),
        .render_scale = .75f, .create = create, .record = record, .destroy = destroy,
    };
    static const af_extension_api api = {
        .abi_version = AF_EXTENSION_ABI_VERSION, .struct_size = sizeof(af_extension_api),
        .id = "afterlight.vulkan-blit", .start = start, .stop = stop, .vulkan_upscaler = &upscaler,
    };
    return version == AF_EXTENSION_ABI_VERSION ? &api : NULL;
}

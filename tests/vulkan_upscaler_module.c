/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/extension_vulkan.h>
#include <math.h>
#include <stdlib.h>

typedef struct fixture {
    uint32_t slots;
    bool first;
} fixture;

static int32_t start(const af_extension_host *host, void **state)
{
    (void)host;
    (void)state;
    return 0;
}

static void stop(void *state) { (void)state; }

static bool create(void *state, const af_vulkan_upscaler_config *config, void **out)
{
    (void)state;
    if (!config->device || !config->physical_device || !config->instance ||
        config->render_extent.width != config->output_extent.width / 2 ||
        config->render_extent.height != config->output_extent.height / 2 ||
        config->depth_format == VK_FORMAT_UNDEFINED ||
        config->output_format != VK_FORMAT_R16G16B16A16_SFLOAT) return false;
    fixture *context = malloc(sizeof(*context));
    if (!context) return false;
    *context = (fixture){config->frames_in_flight, true};
    *out = context;
    return true;
}

static bool record(void *state, const af_vulkan_upscaler_frame *frame)
{
    fixture *context = state;
    /* An inset viewport simulates a provider declining a supported config. */
    if (frame->viewport_uv[0] > 0 || !frame->color.view || !frame->depth.view ||
        !frame->motion.view || !frame->output.view ||
        frame->frame_slot >= context->slots ||
        (context->first && !frame->reset_history) ||
        !(fabsf(frame->jitter_pixels[0]) < .5f && fabsf(frame->jitter_pixels[1]) < .5f) ||
        frame->exposure_multiplier != 1.f) return false;
    context->first = false;
    const VkClearColorValue green = {.float32 = {.01f, .8f, .01f, 1.f}};
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(frame->command_buffer, frame->output.image, VK_IMAGE_LAYOUT_GENERAL,
                         &green, 1, &range);
    return true;
}

static void destroy(void *state) { free(state); }

AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t version)
{
    static const af_vulkan_upscaler_api provider = {
        .abi_version = AF_VULKAN_UPSCALER_ABI_VERSION, .struct_size = sizeof(provider),
        .render_scale = .5f, .create = create, .record = record, .destroy = destroy,
    };
    static const af_extension_api api = {
        .abi_version = AF_EXTENSION_ABI_VERSION, .struct_size = sizeof(api),
        .id = "test.vulkan-upscaler", .start = start, .stop = stop, .vulkan_upscaler = &provider,
    };
    return version == AF_EXTENSION_ABI_VERSION ? &api : NULL;
}

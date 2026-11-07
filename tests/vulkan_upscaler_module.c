/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/extension_vulkan.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static const char *const quality_names[] = {"Half", "Three quarters", "Third"};
static const float quality_scales[] = {.5f, .75f, 1.f / 3.f};

typedef struct fixture {
    uint32_t slots, quality;
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
        config->struct_size < offsetof(af_vulkan_upscaler_config, quality) ||
        config->depth_format == VK_FORMAT_UNDEFINED ||
        config->output_format != VK_FORMAT_R16G16B16A16_SFLOAT) return false;
    uint32_t quality =
        config->struct_size >= offsetof(af_vulkan_upscaler_config, quality) +
                                  sizeof(config->quality)
            ? config->quality
            : 0;
    if (quality >= sizeof(quality_scales) / sizeof(quality_scales[0]) ||
        config->render_extent.width !=
            (uint32_t)fmaxf(1, (float)config->output_extent.width *
                                   quality_scales[quality]) ||
        config->render_extent.height !=
            (uint32_t)fmaxf(1, (float)config->output_extent.height *
                                   quality_scales[quality]))
        return false;
    fixture *context = malloc(sizeof(*context));
    if (!context) return false;
    *context = (fixture){config->frames_in_flight, quality, true};
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
    const VkClearColorValue color = context->quality
        ? (VkClearColorValue){.float32 = {.01f, .01f, .8f, 1.f}}
        : (VkClearColorValue){.float32 = {.01f, .8f, .01f, 1.f}};
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(frame->command_buffer, frame->output.image, VK_IMAGE_LAYOUT_GENERAL,
                         &color, 1, &range);
    return true;
}

static void destroy(void *state) { free(state); }

AF_EXTENSION_EXPORT const af_extension_api *af_extension_get_api(uint32_t version)
{
    static const af_vulkan_upscaler_api provider = {
        .abi_version = AF_VULKAN_UPSCALER_ABI_VERSION, .struct_size = sizeof(provider),
        .render_scale = .5f, .create = create, .record = record, .destroy = destroy,
        .quality_count = sizeof(quality_names) / sizeof(quality_names[0]),
        .quality_names = quality_names, .quality_scales = quality_scales,
    };
    static const af_extension_api api = {
        .abi_version = AF_EXTENSION_ABI_VERSION, .struct_size = sizeof(api),
        .id = "test.vulkan-upscaler", .start = start, .stop = stop, .vulkan_upscaler = &provider,
    };
    return version == AF_EXTENSION_ABI_VERSION ? &api : NULL;
}

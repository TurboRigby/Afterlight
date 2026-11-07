/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_EXTENSION_VULKAN_H
#define AFTERLIGHT_EXTENSION_VULKAN_H

#include <afterlight/extension.h>
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_VULKAN_UPSCALER_ABI_VERSION 1u
#define AF_VULKAN_UPSCALER_SPATIAL 1u

typedef struct af_vulkan_upscaler_config {
    uint32_t struct_size;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    uint32_t graphics_queue_family, frames_in_flight;
    VkExtent2D render_extent, output_extent;
    VkFormat color_format, depth_format, motion_format, output_format;
} af_vulkan_upscaler_config;

typedef struct af_vulkan_upscaler_image {
    VkImage image;
    VkImageView view;
} af_vulkan_upscaler_image;

typedef struct af_vulkan_upscaler_frame {
    uint32_t struct_size, frame_slot, reset_history;
    VkCommandBuffer command_buffer;
    /* Color and motion start/end in SHADER_READ_ONLY_OPTIMAL. Depth starts/ends
       in DEPTH_STENCIL_READ_ONLY_OPTIMAL. All three inputs are sampled images.
       Output starts/ends in GENERAL and supports storage and transfer writes. */
    af_vulkan_upscaler_image color, depth, motion, output;
    /* x, y, width, height normalized to the full attachment. */
    float viewport_uv[4];
    /* Projection offset in render pixels, positive right/down. */
    float jitter_pixels[2];
    float exposure_multiplier;
} af_vulkan_upscaler_frame;

typedef struct af_vulkan_upscaler_api {
    uint32_t abi_version, struct_size;
    /* Fixed fraction of output width and height, between 0.5 and 1.0. */
    float render_scale;
    /* Called after device creation for each resource configuration. Copy any
       config values you retain. On failure, clean up partial state here. */
    bool (*create)(void *extension_state, const af_vulkan_upscaler_config *config,
                   void **context);
    /* Record inline outside render passes. Inputs are read-only and the output
       must be completely written. Restore entry layouts and never submit or
       retain the command buffer. Return false before recording any commands
       to request built-in rendering. The frame_slot fence has completed. */
    bool (*record)(void *context, const af_vulkan_upscaler_frame *frame);
    /* Device idle. Called before the borrowed attachments are destroyed. */
    void (*destroy)(void *context);
    /* Optional ABI 1 tail. Spatial providers render without projection jitter. */
    uint32_t flags;
} af_vulkan_upscaler_api;

/* One loaded provider. It starts enabled, and disabling restores the authored
   anti-aliasing mode and native resolution. Changing state applies next draw.
   Enable after disabling to retry a provider that previously fell back. */
bool af_engine_set_upscaler_enabled(af_engine *engine, bool enabled);

#ifdef __cplusplus
}
#endif
#endif

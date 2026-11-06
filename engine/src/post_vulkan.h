/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_POST_VULKAN_H
#define AF_POST_VULKAN_H
#include <vulkan/vulkan.h>
#include <afterlight/material.h>
#include <afterlight/overlay.h>
typedef struct af_vk_post af_vk_post;
af_vk_post *af_vk_post_create(VkPhysicalDevice physical,VkDevice device,VkExtent2D extent,uint32_t count,
    const VkImageView *outputs,VkFormat output_format,VkRenderPass scene_pass,const VkImageView *depths);
void af_vk_post_destroy(af_vk_post *post);
VkImage af_vk_post_scene_image(const af_vk_post*,uint32_t);
VkFramebuffer af_vk_post_scene_framebuffer(const af_vk_post *post,uint32_t image);
VkRenderPass af_vk_post_output_pass(const af_vk_post *post);
/* Ends with the presentation render pass open so the UI can render after HDR. */
void af_vk_post_draw(af_vk_post *post,VkCommandBuffer command,uint32_t image,af_render_settings settings,af_rect normalized_viewport);
#endif

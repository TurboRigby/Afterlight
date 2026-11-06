/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_REFRACTION_VULKAN_H
#define AF_REFRACTION_VULKAN_H
#include <stdbool.h>
#include <vulkan/vulkan.h>
typedef struct af_vk_refraction af_vk_refraction;
af_vk_refraction *af_vk_refraction_create(VkPhysicalDevice, VkDevice, VkExtent2D, uint32_t, VkFormat);
void af_vk_refraction_destroy(af_vk_refraction *);
VkDescriptorImageInfo af_vk_refraction_color(const af_vk_refraction *, uint32_t);
VkDescriptorImageInfo af_vk_refraction_depth(const af_vk_refraction *, uint32_t);
void af_vk_refraction_prepare(af_vk_refraction *, VkCommandBuffer, uint32_t);
/* Outside render passes: snapshot opaque HDR/depth and restore source layouts. */
void af_vk_refraction_copy(af_vk_refraction *, VkCommandBuffer, uint32_t, VkImage, VkImage);
#endif

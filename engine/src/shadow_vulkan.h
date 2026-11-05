/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_SHADOW_VULKAN_H
#define AF_SHADOW_VULKAN_H
#include <vulkan/vulkan.h>
#include <afterlight/shadow.h>
#define AF_SHADOW_FRAME_COUNT 2
#define AF_SHADOW_LAYER_COUNT 24
typedef struct af_vk_shadows af_vk_shadows;
af_vk_shadows *af_vk_shadows_create(VkPhysicalDevice physical,VkDevice device,uint32_t resolution,uint32_t layers);
void af_vk_shadows_destroy(af_vk_shadows *shadows);
VkDescriptorSetLayout af_vk_shadows_layout(const af_vk_shadows *shadows);
VkDescriptorSet af_vk_shadows_descriptor(const af_vk_shadows *shadows,uint32_t frame);
void af_vk_shadows_begin(af_vk_shadows *shadows,VkCommandBuffer command,uint32_t frame,uint32_t layer,af_shadow_settings settings);
void af_vk_shadows_push(af_vk_shadows *shadows,VkCommandBuffer command,const float *mvp);
VkDescriptorImageInfo af_vk_shadows_image(const af_vk_shadows *shadows,uint32_t frame);
#endif

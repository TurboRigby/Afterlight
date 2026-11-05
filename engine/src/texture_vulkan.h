/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_TEXTURE_VULKAN_H
#define AF_TEXTURE_VULKAN_H
#include <stdbool.h>
#include <afterlight/asset.h>
#include <vulkan/vulkan.h>
typedef struct af_vk_texture af_vk_texture;
af_vk_texture *af_vk_texture_create(VkPhysicalDevice physical,VkDevice device,VkQueue queue,uint32_t family,const unsigned char *rgba,uint32_t width,uint32_t height,bool srgb,bool repeat,float anisotropy);
af_texture_info af_vk_texture_info(const af_vk_texture *texture);
void af_vk_texture_destroy(af_vk_texture *texture);
VkDescriptorSetLayout af_vk_texture_layout(const af_vk_texture *texture);
VkDescriptorSet af_vk_texture_descriptor(const af_vk_texture *texture);
af_vk_texture *af_vk_texture_create_hdr(VkPhysicalDevice physical,VkDevice device,VkQueue queue,uint32_t family,const float *rgba,uint32_t width,uint32_t height,uint32_t levels,bool wrap_u);
VkDescriptorImageInfo af_vk_texture_image(const af_vk_texture *texture);
#endif

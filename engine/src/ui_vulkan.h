/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_UI_VULKAN_H
#define AF_UI_VULKAN_H
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <afterlight/overlay.h>

#define AF_UI_FRAMES_IN_FLIGHT 2

typedef struct af_vk_ui af_vk_ui;
af_vk_ui *af_vk_ui_create(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family,
    const unsigned char *rgba, uint32_t width, uint32_t height);
bool af_vk_ui_upload_texture(af_vk_ui *ui,VkQueue queue,uint32_t family,uint32_t id,const unsigned char *rgba,uint32_t width,uint32_t height);
bool af_vk_ui_remove_texture(af_vk_ui *ui,uint32_t id);
void af_vk_ui_destroy(af_vk_ui *ui);
void af_vk_ui_release_pipeline(af_vk_ui *ui);
bool af_vk_ui_create_pipeline(af_vk_ui *ui, VkRenderPass pass);
/* Called only after the corresponding frame fence has completed. */
void af_vk_ui_record_uploads(af_vk_ui *ui,VkCommandBuffer command,uint32_t frame_index);
bool af_vk_ui_prepare(af_vk_ui *ui, uint32_t frame_index, const af_overlay_frame *frame);
void af_vk_ui_draw(af_vk_ui *ui, VkCommandBuffer command, uint32_t frame_index,
    const af_overlay_frame *frame, VkExtent2D extent);
#endif

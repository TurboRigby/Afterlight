/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "texture_vulkan.h"
#include "log.h"
#include "mipmap.h"
#include <stdlib.h>
#include <string.h>
typedef struct ui_buffer {
    VkBuffer buffer;
    VkDeviceMemory memory;
    void *mapped;
    VkDeviceSize capacity;
    bool coherent;
} ui_buffer;
struct af_vk_texture {
    af_texture_info info;
    VkPhysicalDevice physical;
    VkDevice device;
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor;
};
static bool ok(VkResult result, const char *operation)
{
    if (result == VK_SUCCESS) return true;
    af_log("error", "Texture %s failed (VkResult %d).", operation, (int)result);
    return false;
}
static uint32_t memory_type(af_vk_texture *ui, uint32_t bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred)
{
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(ui->physical, &properties);
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = properties.memoryTypes[i].propertyFlags;
        if (!(bits & (1u << i)) || (flags & required) != required) continue;
        fallback = i;
        if ((flags & preferred) == preferred) return i;
    }
    return fallback;
}
static void free_buffer(af_vk_texture *ui, ui_buffer *buffer)
{
    if (buffer->mapped) vkUnmapMemory(ui->device, buffer->memory);
    if (buffer->buffer) vkDestroyBuffer(ui->device, buffer->buffer, NULL);
    if (buffer->memory) vkFreeMemory(ui->device, buffer->memory, NULL);
    *buffer = (ui_buffer){0};
}
static bool reserve_buffer(af_vk_texture *ui, ui_buffer *buffer, VkDeviceSize size, VkBufferUsageFlags usage)
{
    if (buffer->capacity >= size) return true;
    free_buffer(ui, buffer); /* Owner frame fence is already complete. */
    VkDeviceSize capacity = size < 65536 ? 65536 : size * 2;
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = capacity, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (!ok(vkCreateBuffer(ui->device, &info, NULL, &buffer->buffer), "vkCreateBuffer")) return false;
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(ui->device, buffer->buffer, &requirements);
    uint32_t index = memory_type(ui, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (index == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = index};
    if (!ok(vkAllocateMemory(ui->device, &allocation, NULL, &buffer->memory), "vkAllocateMemory") ||
        !ok(vkBindBufferMemory(ui->device, buffer->buffer, buffer->memory, 0), "vkBindBufferMemory") ||
        !ok(vkMapMemory(ui->device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped), "vkMapMemory")) return false;
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(ui->physical, &properties);
    buffer->coherent = (properties.memoryTypes[index].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    buffer->capacity = capacity;
    return true;
}
static bool flush_buffer(af_vk_texture *ui, ui_buffer *buffer)
{
    if (buffer->coherent) return true;
    VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .memory = buffer->memory, .size = VK_WHOLE_SIZE};
    return ok(vkFlushMappedMemoryRanges(ui->device, 1, &range), "vkFlushMappedMemoryRanges");
}

void af_vk_texture_destroy(af_vk_texture *ui){
    if(!ui)return;
    if(ui->descriptor_pool)vkDestroyDescriptorPool(ui->device,ui->descriptor_pool,NULL);
    if(ui->descriptor_layout)vkDestroyDescriptorSetLayout(ui->device,ui->descriptor_layout,NULL);
    if(ui->sampler)vkDestroySampler(ui->device,ui->sampler,NULL);
    if(ui->view)vkDestroyImageView(ui->device,ui->view,NULL);
    if(ui->image)vkDestroyImage(ui->device,ui->image,NULL);
    if(ui->memory)vkFreeMemory(ui->device,ui->memory,NULL);
    free(ui);
}
VkDescriptorSetLayout af_vk_texture_layout(const af_vk_texture *ui){return ui->descriptor_layout;}
VkDescriptorSet af_vk_texture_descriptor(const af_vk_texture *ui){return ui->descriptor;}
af_vk_texture *af_vk_texture_create(VkPhysicalDevice physical, VkDevice device, VkQueue queue, uint32_t family,
    const unsigned char *rgba, uint32_t width, uint32_t height,bool srgb,bool repeat,float anisotropy)
{
    if(!rgba||!width||!height||width>8192||height>8192)return NULL;
    VkFormat format=srgb?VK_FORMAT_R8G8B8A8_SRGB:VK_FORMAT_R8G8B8A8_UNORM;
    af_vk_texture *ui = calloc(1, sizeof(*ui));
    if (!ui) return NULL;
    ui->device = device; ui->physical = physical;
    ui_buffer staging = {0};
    VkCommandPool pool = VK_NULL_HANDLE;
    af_mip_chain mips;
    if(!af_mip_chain_build(rgba,width,height,srgb,repeat,&mips)){free(ui);return NULL;}
    VkDeviceSize bytes = mips.bytes;
    if (!reserve_buffer(ui, &staging, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) goto fail;
    memcpy(staging.mapped, mips.pixels, (size_t)bytes);
    if (!flush_buffer(ui, &staging)) goto fail;
    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = format, .extent = {width,height,1},
        .mipLevels = mips.count, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (!ok(vkCreateImage(device, &image, NULL, &ui->image), "vkCreateImage")) goto fail;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, ui->image, &requirements);
    ui->info=(af_texture_info){.width=width,.height=height,.mip_levels=mips.count,.allocation_bytes=requirements.size,.anisotropy=repeat?anisotropy:1,.srgb=srgb};
    uint32_t index = memory_type(ui, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
    if (index == UINT32_MAX) goto fail;
    VkMemoryAllocateInfo allocation = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = index};
    if (!ok(vkAllocateMemory(device, &allocation, NULL, &ui->memory), "atlas memory") ||
        !ok(vkBindImageMemory(device, ui->image, ui->memory, 0), "vkBindImageMemory")) goto fail;
    VkCommandPoolCreateInfo pool_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family};
    if (!ok(vkCreateCommandPool(device, &pool_info, NULL, &pool), "upload pool")) goto fail;
    VkCommandBuffer command;
    VkCommandBufferAllocateInfo command_info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
    if (!ok(vkAllocateCommandBuffers(device, &command_info, &command), "upload commands")) goto fail;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (!ok(vkBeginCommandBuffer(command, &begin), "begin upload")) goto fail;
    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = ui->image, .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,mips.count,0,1},
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
        0, NULL, 0, NULL, 1, &barrier);
    VkBufferImageCopy copies[AF_MAX_MIPS]={0};
    for(uint32_t level=0;level<mips.count;++level)copies[level]=(VkBufferImageCopy){.bufferOffset=mips.offsets[level],
        .imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,level,0,1},.imageExtent={mips.widths[level],mips.heights[level],1}};
    vkCmdCopyBufferToImage(command, staging.buffer, ui->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mips.count, copies);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, NULL, 0, NULL, 1, &barrier);
    if (!ok(vkEndCommandBuffer(command), "end upload")) goto fail;
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &command};
    if (!ok(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "upload submit") || !ok(vkQueueWaitIdle(queue), "upload wait")) goto fail;
    vkDestroyCommandPool(device, pool, NULL); pool = VK_NULL_HANDLE;
    free_buffer(ui, &staging);
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = ui->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,mips.count,0,1}};
    if (!ok(vkCreateImageView(device, &view, NULL, &ui->view), "atlas view")) goto fail;
    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR, .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR, .maxLod=(float)(mips.count-1),
        .anisotropyEnable=repeat&&anisotropy>1, .maxAnisotropy=repeat?anisotropy:1, .addressModeU = (repeat?VK_SAMPLER_ADDRESS_MODE_REPEAT:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
        .addressModeV = (repeat?VK_SAMPLER_ADDRESS_MODE_REPEAT:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE), .addressModeW = (repeat?VK_SAMPLER_ADDRESS_MODE_REPEAT:VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)};
    if (!ok(vkCreateSampler(device, &sampler, NULL, &ui->sampler), "atlas sampler")) goto fail;
    VkDescriptorSetLayoutBinding binding = {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo descriptor_layout = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding};
    if (!ok(vkCreateDescriptorSetLayout(device, &descriptor_layout, NULL, &ui->descriptor_layout), "descriptor layout")) goto fail;
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1};
    VkDescriptorPoolCreateInfo descriptor_pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size};
    if (!ok(vkCreateDescriptorPool(device, &descriptor_pool, NULL, &ui->descriptor_pool), "descriptor pool")) goto fail;
    VkDescriptorSetAllocateInfo descriptor = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = ui->descriptor_pool, .descriptorSetCount = 1, .pSetLayouts = &ui->descriptor_layout};
    if (!ok(vkAllocateDescriptorSets(device, &descriptor, &ui->descriptor), "descriptor allocation")) goto fail;
    VkDescriptorImageInfo image_info = {ui->sampler,ui->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ui->descriptor,
        .dstBinding = 0, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &image_info};
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);
    free(mips.pixels);return ui;
fail:
    free(mips.pixels);
    if(pool)vkDestroyCommandPool(device,pool,NULL);
    free_buffer(ui,&staging);af_vk_texture_destroy(ui);return NULL;
}

af_texture_info af_vk_texture_info(const af_vk_texture *texture){return texture->info;}

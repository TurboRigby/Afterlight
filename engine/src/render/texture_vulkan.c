/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "texture_vulkan.h"
#include "core/log.h"
#include "assets/mipmap.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct staging_buffer {
    VkBuffer buffer;
    af_vk_allocation alloc;
} staging_buffer;

struct af_vk_texture {
    af_texture_info info;
    VkPhysicalDevice physical;
    VkDevice device;
    af_vk_allocator *allocator;
    VkImage image;
    af_vk_allocation memory;
    VkImageView view;
    VkSampler sampler;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptor_pool;
    VkDescriptorSet descriptor;
    staging_buffer upload;
};

static bool ok(VkResult result, const char *operation) {
    if (result == VK_SUCCESS)
        return true;
    af_log("error", "Texture %s failed (VkResult %d).", operation, (int)result);
    return false;
}

void af_vk_texture_destroy(af_vk_texture *ui) {
    if (!ui)
        return;
    if (ui->descriptor_pool)
        vkDestroyDescriptorPool(ui->device, ui->descriptor_pool, NULL);
    if (ui->descriptor_layout)
        vkDestroyDescriptorSetLayout(ui->device, ui->descriptor_layout, NULL);
    if (ui->sampler)
        vkDestroySampler(ui->device, ui->sampler, NULL);
    if (ui->view)
        vkDestroyImageView(ui->device, ui->view, NULL);
    if (ui->image)
        vkDestroyImage(ui->device, ui->image, NULL);
    af_vk_free(ui->allocator, &ui->memory);
    af_vk_destroy_buffer(ui->allocator, &ui->upload.buffer, &ui->upload.alloc);
    free(ui);
}

VkDescriptorSetLayout af_vk_texture_layout(const af_vk_texture *ui) {
    return ui->descriptor_layout;
}

VkDescriptorSet af_vk_texture_descriptor(const af_vk_texture *ui) {
    return ui->descriptor;
}

static af_vk_texture *create_texture(VkPhysicalDevice physical, VkDevice device, af_vk_allocator *allocator,
                                     const void *rgba, uint32_t width, uint32_t height, bool srgb, bool repeat,
                                     float anisotropy, uint32_t hdr_levels, VkCommandBuffer external) {
    if (!allocator || !rgba || !width || !height || width > 8192 || height > 8192)
        return NULL;
    VkFormat format = hdr_levels ? VK_FORMAT_R16G16B16A16_SFLOAT
                      : srgb     ? VK_FORMAT_R8G8B8A8_SRGB
                                 : VK_FORMAT_R8G8B8A8_UNORM;
    VkFormatProperties features;
    vkGetPhysicalDeviceFormatProperties(physical, format, &features);
    if (!(features.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        return NULL;
    af_vk_texture *ui = calloc(1, sizeof(*ui));
    if (!ui)
        return NULL;
    ui->device = device;
    ui->physical = physical;
    ui->allocator = allocator;
    staging_buffer staging = {0};
    af_mip_chain mips = {0};
    if (hdr_levels) {
        if (hdr_levels > AF_MAX_MIPS) {
            free(ui);
            return NULL;
        }
        mips.count = hdr_levels;
        uint32_t w = width, h = height;
        for (uint32_t i = 0; i < hdr_levels; ++i) {
            mips.widths[i] = w;
            mips.heights[i] = h;
            mips.offsets[i] = mips.bytes;
            mips.bytes += (size_t)w * h * 8;
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
        }
        mips.pixels = malloc(mips.bytes);
        if (!mips.pixels) {
            free(ui);
            return NULL;
        }
        const float *values = rgba;
        uint16_t *half = (uint16_t *)mips.pixels;
        for (size_t i = 0; i < mips.bytes / 2; ++i) {
            float value = values[i];
            if (!isfinite(value) || value < 0 || value > 65504) {
                free(mips.pixels);
                free(ui);
                return NULL;
            }
            if (value == 0) {
                half[i] = 0;
                continue;
            }
            int exponent = 0;
            float fraction = frexpf(value, &exponent);
            unsigned mantissa;
            if (exponent < -13)
                mantissa = (unsigned)lrintf(ldexpf(value, 24));
            else {
                unsigned significand = (unsigned)lrintf((fraction * 2 - 1) * 1024);
                mantissa = ((unsigned)(exponent + 14) << 10) + significand;
            }
            half[i] = (uint16_t)mantissa;
        }
    } else if (!af_mip_chain_build(rgba, width, height, srgb, repeat, &mips)) {
        free(ui);
        return NULL;
    }

    VkDeviceSize bytes = mips.bytes;
    if (!af_vk_create_buffer(allocator, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &staging.buffer, &staging.alloc))
        goto fail;
    memcpy(staging.alloc.mapped, mips.pixels, (size_t)bytes);
    if (!af_vk_flush(allocator, &staging.alloc, 0, bytes))
        goto fail;

    VkImageCreateInfo image = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                               .imageType = VK_IMAGE_TYPE_2D,
                               .format = format,
                               .extent = {width, height, 1},
                               .mipLevels = mips.count,
                               .arrayLayers = 1,
                               .samples = VK_SAMPLE_COUNT_1_BIT,
                               .tiling = VK_IMAGE_TILING_OPTIMAL,
                               .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                               .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
    if (!ok(vkCreateImage(device, &image, NULL, &ui->image), "vkCreateImage"))
        goto fail;
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device, ui->image, &requirements);
    ui->info = (af_texture_info){.width = width,
                                 .height = height,
                                 .mip_levels = mips.count,
                                 .allocation_bytes = requirements.size,
                                 .anisotropy = repeat ? anisotropy : 1,
                                 .srgb = srgb};
    if (!af_vk_allocate(allocator, requirements, AF_VK_MEM_IMAGE, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, false,
                        &ui->memory) ||
        !ok(vkBindImageMemory(device, ui->image, ui->memory.memory, ui->memory.offset), "vkBindImageMemory"))
        goto fail;

    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                  .image = ui->image,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                  .format = format,
                                  .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips.count, 0, 1}};
    if (!ok(vkCreateImageView(device, &view, NULL, &ui->view), "atlas view"))
        goto fail;
    VkSamplerCreateInfo sampler = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .maxLod = (float)(mips.count - 1),
        .anisotropyEnable = repeat && anisotropy > 1,
        .maxAnisotropy = repeat ? anisotropy : 1,
        .addressModeU = (repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
        .addressModeV =
            (repeat && !hdr_levels ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
        .addressModeW = (repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE)};
    if (!ok(vkCreateSampler(device, &sampler, NULL, &ui->sampler), "atlas sampler"))
        goto fail;
    VkDescriptorSetLayoutBinding binding = {.binding = 0,
                                            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                            .descriptorCount = 1,
                                            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo descriptor_layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 1, .pBindings = &binding};
    if (!ok(vkCreateDescriptorSetLayout(device, &descriptor_layout, NULL, &ui->descriptor_layout), "descriptor layout"))
        goto fail;
    VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo descriptor_pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                                  .maxSets = 1,
                                                  .poolSizeCount = 1,
                                                  .pPoolSizes = &pool_size};
    if (!ok(vkCreateDescriptorPool(device, &descriptor_pool, NULL, &ui->descriptor_pool), "descriptor pool"))
        goto fail;
    VkDescriptorSetAllocateInfo descriptor = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                              .descriptorPool = ui->descriptor_pool,
                                              .descriptorSetCount = 1,
                                              .pSetLayouts = &ui->descriptor_layout};
    if (!ok(vkAllocateDescriptorSets(device, &descriptor, &ui->descriptor), "descriptor allocation"))
        goto fail;
    VkDescriptorImageInfo image_info = {ui->sampler, ui->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstSet = ui->descriptor,
                                  .dstBinding = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  .pImageInfo = &image_info};
    vkUpdateDescriptorSets(device, 1, &write, 0, NULL);

    VkCommandBuffer command = external ? external : af_vk_begin_transfer(allocator);
    if (!command)
        goto fail;

    VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                                    .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                    .image = ui->image,
                                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips.count, 0, 1},
                                    .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &barrier);
    VkBufferImageCopy copies[AF_MAX_MIPS] = {0};
    for (uint32_t level = 0; level < mips.count; ++level)
        copies[level] = (VkBufferImageCopy){.bufferOffset = mips.offsets[level],
                                            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, 1},
                                            .imageExtent = {mips.widths[level], mips.heights[level], 1}};
    vkCmdCopyBufferToImage(command, staging.buffer, ui->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mips.count,
                           copies);
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0,
                         NULL, 1, &barrier);
    if (!external) {
        if (!af_vk_end_transfer(allocator, command))
            goto fail;
        af_vk_destroy_buffer(allocator, &staging.buffer, &staging.alloc);
    } else {
        ui->upload = staging;
        staging = (staging_buffer){0};
    }
    free(mips.pixels);
    return ui;
fail:
    free(mips.pixels);
    af_vk_destroy_buffer(allocator, &staging.buffer, &staging.alloc);
    af_vk_texture_destroy(ui);
    return NULL;
}

af_texture_info af_vk_texture_info(const af_vk_texture *texture) {
    return texture->info;
}

af_vk_texture *af_vk_texture_create(VkPhysicalDevice physical, VkDevice device, af_vk_allocator *allocator,
                                    const unsigned char *rgba, uint32_t width, uint32_t height, bool srgb, bool repeat,
                                    float anisotropy) {
    return create_texture(physical, device, allocator, rgba, width, height, srgb, repeat, anisotropy, 0,
                          VK_NULL_HANDLE);
}

af_vk_texture *af_vk_texture_create_hdr(VkPhysicalDevice physical, VkDevice device, af_vk_allocator *allocator,
                                        const float *rgba, uint32_t width, uint32_t height, uint32_t levels,
                                        bool wrap_u) {
    return create_texture(physical, device, allocator, rgba, width, height, false, wrap_u, 1, levels, VK_NULL_HANDLE);
}

VkDescriptorImageInfo af_vk_texture_image(const af_vk_texture *texture) {
    return (VkDescriptorImageInfo){texture->sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}

af_vk_texture *af_vk_texture_create_recorded(VkPhysicalDevice physical, VkDevice device, af_vk_allocator *allocator,
                                             VkCommandBuffer command, const unsigned char *rgba, uint32_t width,
                                             uint32_t height) {
    return command ? create_texture(physical, device, allocator, rgba, width, height, false, false, 1, 0, command)
                   : NULL;
}

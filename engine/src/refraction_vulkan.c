/* SPDX-License-Identifier: MPL-2.0 */
#include "refraction_vulkan.h"
#include "log.h"
#include <stdlib.h>
typedef struct image {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
} image;
typedef struct frame {
    image color, depth;
    bool initialized;
} frame;
struct af_vk_refraction {
    uint64_t allocation_bytes;
    VkPhysicalDevice physical;
    VkDevice device;
    VkExtent2D extent;
    uint32_t count;
    VkFormat depth_format;
    VkSampler color_sampler, depth_sampler;
    frame *frames;
};
static bool ok(VkResult result) {
    if (result == VK_SUCCESS)
        return true;
    af_log("error", "Refraction resource failed (%d).", (int)result);
    return false;
}
static VkImageAspectFlags depth_aspects(VkFormat format) {
    return VK_IMAGE_ASPECT_DEPTH_BIT |
           ((format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT)
                ? VK_IMAGE_ASPECT_STENCIL_BIT
                : 0);
}
static bool create_image(af_vk_refraction *r, image *out, VkFormat format, VkImageAspectFlags aspect) {
    VkImageCreateInfo info = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .extent = {r->extent.width, r->extent.height, 1},
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (!ok(vkCreateImage(r->device, &info, NULL, &out->image)))
        return false;
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(r->device, out->image, &req);
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(r->physical, &props);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if ((req.memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            type = i;
            break;
        }
    if (type == UINT32_MAX)
        return false;
    r->allocation_bytes += req.size;
    VkMemoryAllocateInfo allocation = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type};
    if (!ok(vkAllocateMemory(r->device, &allocation, NULL, &out->memory)) ||
        !ok(vkBindImageMemory(r->device, out->image, out->memory, 0)))
        return false;
    VkImageViewCreateInfo view = {.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                  .image = out->image,
                                  .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                  .format = format,
                                  .subresourceRange = {aspect, 0, 1, 0, 1}};
    return ok(vkCreateImageView(r->device, &view, NULL, &out->view));
}
af_vk_refraction *af_vk_refraction_create(VkPhysicalDevice physical, VkDevice device, VkExtent2D extent, uint32_t count,
                                          VkFormat depth) {
    af_vk_refraction *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->physical = physical;
    r->device = device;
    r->extent = extent;
    r->count = count;
    r->depth_format = depth;
    r->frames = calloc(count, sizeof(*r->frames));
    if (!r->frames)
        goto fail;
    VkSamplerCreateInfo sampler = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                   .magFilter = VK_FILTER_LINEAR,
                                   .minFilter = VK_FILTER_LINEAR,
                                   .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
                                   .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                                   .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    if (!ok(vkCreateSampler(device, &sampler, NULL, &r->color_sampler)))
        goto fail;
    sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
    if (!ok(vkCreateSampler(device, &sampler, NULL, &r->depth_sampler)))
        goto fail;
    for (uint32_t i = 0; i < count; ++i)
        if (!create_image(r, &r->frames[i].color, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT) ||
            !create_image(r, &r->frames[i].depth, depth, VK_IMAGE_ASPECT_DEPTH_BIT))
            goto fail;
    return r;
fail:
    af_vk_refraction_destroy(r);
    return NULL;
}
uint64_t af_vk_refraction_allocation_bytes(const af_vk_refraction *p) {
    return p ? p->allocation_bytes : 0;
}
void af_vk_refraction_destroy(af_vk_refraction *r) {
    if (!r)
        return;
    for (uint32_t i = 0; r->frames && i < r->count; ++i) {
        image *images[] = {&r->frames[i].color, &r->frames[i].depth};
        for (unsigned k = 0; k < 2; ++k) {
            image *a = images[k];
            if (a->view)
                vkDestroyImageView(r->device, a->view, NULL);
            if (a->image)
                vkDestroyImage(r->device, a->image, NULL);
            if (a->memory)
                vkFreeMemory(r->device, a->memory, NULL);
        }
    }
    if (r->color_sampler)
        vkDestroySampler(r->device, r->color_sampler, NULL);
    if (r->depth_sampler)
        vkDestroySampler(r->device, r->depth_sampler, NULL);
    free(r->frames);
    free(r);
}
VkDescriptorImageInfo af_vk_refraction_color(const af_vk_refraction *r, uint32_t i) {
    return (VkDescriptorImageInfo){r->color_sampler, r->frames[i].color.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}
VkDescriptorImageInfo af_vk_refraction_depth(const af_vk_refraction *r, uint32_t i) {
    return (VkDescriptorImageInfo){r->depth_sampler, r->frames[i].depth.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
}
static VkImageMemoryBarrier barrier(VkImage image, VkImageAspectFlags aspect, VkImageLayout before, VkImageLayout after,
                                    VkAccessFlags source, VkAccessFlags target) {
    return (VkImageMemoryBarrier){.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                  .srcAccessMask = source,
                                  .dstAccessMask = target,
                                  .oldLayout = before,
                                  .newLayout = after,
                                  .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                  .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                  .image = image,
                                  .subresourceRange = {aspect, 0, 1, 0, 1}};
}
void af_vk_refraction_prepare(af_vk_refraction *r, VkCommandBuffer command, uint32_t i) {
    frame *f = &r->frames[i];
    if (f->initialized)
        return;
    VkImageMemoryBarrier barriers[] = {barrier(f->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, VK_ACCESS_SHADER_READ_BIT),
                                       barrier(f->depth.image, depth_aspects(r->depth_format),
                                               VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0,
                                               VK_ACCESS_SHADER_READ_BIT)};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL,
                         0, NULL, 2, barriers);
    f->initialized = true;
}
void af_vk_refraction_copy(af_vk_refraction *r, VkCommandBuffer command, uint32_t i, VkImage color, VkImage depth) {
    frame *f = &r->frames[i];
    VkImageMemoryBarrier barriers[] = {
        barrier(color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT),
        barrier(depth, depth_aspects(r->depth_format), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT),
        barrier(f->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT),
        barrier(f->depth.image, depth_aspects(r->depth_format), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT)};
    vkCmdPipelineBarrier(command,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 4, barriers);
    VkImageCopy region = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                          .extent = {r->extent.width, r->extent.height, 1}};
    vkCmdCopyImage(command, color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f->color.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    region.srcSubresource.aspectMask = region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyImage(command, depth, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, f->depth.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    barriers[0] = barrier(color, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                          VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
    barriers[1] = barrier(depth, depth_aspects(r->depth_format), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                          VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    barriers[2] =
        barrier(f->color.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    barriers[3] =
        barrier(f->depth.image, depth_aspects(r->depth_format), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, NULL, 0, NULL, 4, barriers);
}

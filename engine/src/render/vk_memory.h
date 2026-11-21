/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_VK_MEMORY_H
#define AF_VK_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

typedef struct af_vk_allocator af_vk_allocator;

typedef enum af_vk_memory_class {
    AF_VK_MEM_BUFFER = 0,
    AF_VK_MEM_IMAGE = 1
} af_vk_memory_class;

typedef struct af_vk_allocation {
    VkDeviceMemory memory;
    VkDeviceSize offset;
    VkDeviceSize size;
    VkDeviceSize requested_size;
    void *mapped;
    uint32_t memory_type;
    bool coherent;
    bool host_visible;
    bool device_local;
    void *block;
} af_vk_allocation;

typedef struct af_vk_allocator_stats {
    uint64_t vk_allocations;
    uint64_t total_vk_allocations;
    uint64_t live_suballocations;
    uint64_t block_bytes;
    uint64_t used_bytes;
    uint64_t staged_uploads;
    uint64_t staged_bytes;
} af_vk_allocator_stats;

af_vk_allocator *af_vk_allocator_create(VkPhysicalDevice physical, VkDevice device, VkQueue transfer_queue,
                                        uint32_t transfer_family);
void af_vk_allocator_destroy(af_vk_allocator *allocator);

uint32_t af_vk_allocator_find_type(const af_vk_allocator *allocator, uint32_t type_bits,
                                   VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred);
VkMemoryPropertyFlags af_vk_allocator_type_flags(const af_vk_allocator *allocator, uint32_t memory_type);

bool af_vk_allocate(af_vk_allocator *allocator, VkMemoryRequirements requirements, af_vk_memory_class mem_class,
                    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool device_address,
                    af_vk_allocation *out);
void af_vk_free(af_vk_allocator *allocator, af_vk_allocation *allocation);

bool af_vk_flush(const af_vk_allocator *allocator, const af_vk_allocation *allocation, VkDeviceSize offset,
                 VkDeviceSize size);
bool af_vk_invalidate(const af_vk_allocator *allocator, const af_vk_allocation *allocation, VkDeviceSize offset,
                      VkDeviceSize size);

bool af_vk_create_buffer(af_vk_allocator *allocator, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool device_address,
                         VkBuffer *out_buffer, af_vk_allocation *out_alloc);
void af_vk_destroy_buffer(af_vk_allocator *allocator, VkBuffer *buffer, af_vk_allocation *alloc);

/* Allocates a DEVICE_LOCAL buffer and uploads initial data. Uses direct copy on unified
   DEVICE_LOCAL | HOST_VISIBLE memory unless AFTERLIGHT_FORCE_STAGING_UPLOAD is set, in
   which case (and on discrete VRAM heaps) it stages through a suballocated host buffer. */
bool af_vk_upload_buffer_staged(af_vk_allocator *allocator, const void *data, VkDeviceSize size,
                                VkBufferUsageFlags usage, bool device_address, VkBuffer *out_buffer,
                                af_vk_allocation *out_alloc);

/* One-shot transfer command buffer helpers backed by the allocator's transient pool. */
VkCommandBuffer af_vk_begin_transfer(af_vk_allocator *allocator);
bool af_vk_end_transfer(af_vk_allocator *allocator, VkCommandBuffer command);

af_vk_allocator_stats af_vk_allocator_get_stats(const af_vk_allocator *allocator);

#endif

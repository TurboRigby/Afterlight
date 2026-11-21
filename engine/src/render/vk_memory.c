/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "vk_memory.h"
#include "core/log.h"
#include <stdlib.h>
#include <string.h>

#define AF_VK_DEVICE_BLOCK_BYTES (UINT64_C(16) * 1024 * 1024)
#define AF_VK_HOST_BLOCK_BYTES (UINT64_C(8) * 1024 * 1024)

typedef struct af_vk_span {
    VkDeviceSize offset;
    VkDeviceSize size;
} af_vk_span;

typedef struct af_vk_block {
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkDeviceSize used;
    void *mapped;
    uint32_t memory_type;
    af_vk_memory_class mem_class;
    bool device_address;
    bool host_visible;
    bool coherent;
    bool device_local;
    bool dedicated;
    af_vk_span *free_spans;
    size_t free_count;
    size_t free_capacity;
    struct af_vk_block *next;
} af_vk_block;

struct af_vk_allocator {
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue transfer_queue;
    uint32_t transfer_family;
    VkCommandPool transfer_pool;
    VkPhysicalDeviceMemoryProperties memory_props;
    VkDeviceSize non_coherent_atom;
    af_vk_block *blocks;
    af_vk_allocator_stats stats;
};

static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1)
        return value;
    VkDeviceSize rem = value % alignment;
    return rem ? value + (alignment - rem) : value;
}

static VkDeviceSize align_down(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1)
        return value;
    return value - (value % alignment);
}

static bool vk_check(VkResult code, const char *operation) {
    if (code == VK_SUCCESS)
        return true;
    af_log("error", "Vulkan memory %s failed (VkResult %d).", operation, (int)code);
    return false;
}

static bool block_ensure_span_capacity(af_vk_block *block, size_t needed) {
    if (needed <= block->free_capacity)
        return true;
    size_t cap = block->free_capacity ? block->free_capacity * 2 : 16;
    while (cap < needed)
        cap *= 2;
    af_vk_span *spans = realloc(block->free_spans, cap * sizeof(*spans));
    if (!spans)
        return false;
    block->free_spans = spans;
    block->free_capacity = cap;
    return true;
}

static bool block_suballocate(af_vk_block *block, VkDeviceSize size, VkDeviceSize alignment, VkDeviceSize *out_offset) {
    for (size_t i = 0; i < block->free_count; ++i) {
        VkDeviceSize start = block->free_spans[i].offset;
        VkDeviceSize span_size = block->free_spans[i].size;
        VkDeviceSize aligned = align_up(start, alignment);
        if (aligned < start)
            continue;
        VkDeviceSize pad = aligned - start;
        if (pad > span_size || size > span_size - pad)
            continue;

        VkDeviceSize tail_start = aligned + size;
        VkDeviceSize tail_size = (start + span_size) - tail_start;

        if (pad > 0 && tail_size > 0) {
            if (!block_ensure_span_capacity(block, block->free_count + 1))
                return false;
            block->free_spans[i].size = pad;
            memmove(&block->free_spans[i + 2], &block->free_spans[i + 1],
                    (block->free_count - i - 1) * sizeof(*block->free_spans));
            block->free_spans[i + 1] = (af_vk_span){.offset = tail_start, .size = tail_size};
            ++block->free_count;
        } else if (pad > 0) {
            block->free_spans[i].size = pad;
        } else if (tail_size > 0) {
            block->free_spans[i] = (af_vk_span){.offset = tail_start, .size = tail_size};
        } else {
            memmove(&block->free_spans[i], &block->free_spans[i + 1],
                    (block->free_count - i - 1) * sizeof(*block->free_spans));
            --block->free_count;
        }

        block->used += size;
        *out_offset = aligned;
        return true;
    }
    return false;
}

static void block_release_span(af_vk_block *block, VkDeviceSize offset, VkDeviceSize size) {
    size_t pos = 0;
    while (pos < block->free_count && block->free_spans[pos].offset < offset)
        ++pos;

    bool merge_left = pos > 0 && block->free_spans[pos - 1].offset + block->free_spans[pos - 1].size == offset;
    bool merge_right = pos < block->free_count && offset + size == block->free_spans[pos].offset;

    if (merge_left && merge_right) {
        block->free_spans[pos - 1].size += size + block->free_spans[pos].size;
        memmove(&block->free_spans[pos], &block->free_spans[pos + 1],
                (block->free_count - pos - 1) * sizeof(*block->free_spans));
        --block->free_count;
    } else if (merge_left) {
        block->free_spans[pos - 1].size += size;
    } else if (merge_right) {
        block->free_spans[pos].offset = offset;
        block->free_spans[pos].size += size;
    } else if (block_ensure_span_capacity(block, block->free_count + 1)) {
        memmove(&block->free_spans[pos + 1], &block->free_spans[pos],
                (block->free_count - pos) * sizeof(*block->free_spans));
        block->free_spans[pos] = (af_vk_span){.offset = offset, .size = size};
        ++block->free_count;
    }
    if (block->used >= size)
        block->used -= size;
    else
        block->used = 0;
}

static void block_destroy(af_vk_allocator *a, af_vk_block *block) {
    if (!block)
        return;
    if (block->mapped)
        vkUnmapMemory(a->device, block->memory);
    if (block->memory) {
        vkFreeMemory(a->device, block->memory, NULL);
        if (a->stats.vk_allocations > 0)
            --a->stats.vk_allocations;
        if (a->stats.block_bytes >= block->size)
            a->stats.block_bytes -= block->size;
        else
            a->stats.block_bytes = 0;
    }
    free(block->free_spans);
    free(block);
}

static af_vk_block *block_create(af_vk_allocator *a, uint32_t memory_type, af_vk_memory_class mem_class,
                                 bool device_address, VkDeviceSize min_size, bool dedicated) {
    VkMemoryPropertyFlags flags = a->memory_props.memoryTypes[memory_type].propertyFlags;
    bool host_visible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    bool coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    bool device_local = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;

    VkDeviceSize default_bytes = host_visible ? AF_VK_HOST_BLOCK_BYTES : AF_VK_DEVICE_BLOCK_BYTES;
    VkDeviceSize target = dedicated ? min_size : (default_bytes > min_size ? default_bytes : min_size);
    if (host_visible && !coherent && a->non_coherent_atom > 1)
        target = align_up(target, a->non_coherent_atom);

    VkMemoryAllocateFlagsInfo alloc_flags = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
                                             .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT};
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize actual = target;
    for (;;) {
        VkMemoryAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                     .pNext = device_address ? &alloc_flags : NULL,
                                     .allocationSize = actual,
                                     .memoryTypeIndex = memory_type};
        VkResult res = vkAllocateMemory(a->device, &info, NULL, &memory);
        if (res == VK_SUCCESS)
            break;
        if (dedicated || actual <= min_size) {
            vk_check(res, "vkAllocateMemory");
            return NULL;
        }
        actual /= 2;
        if (actual < min_size)
            actual = min_size;
        if (host_visible && !coherent && a->non_coherent_atom > 1)
            actual = align_up(actual, a->non_coherent_atom);
    }

    af_vk_block *block = calloc(1, sizeof(*block));
    if (!block) {
        vkFreeMemory(a->device, memory, NULL);
        return NULL;
    }
    block->memory = memory;
    block->size = actual;
    block->memory_type = memory_type;
    block->mem_class = mem_class;
    block->device_address = device_address;
    block->host_visible = host_visible;
    block->coherent = coherent;
    block->device_local = device_local;
    block->dedicated = dedicated;

    if (host_visible) {
        if (!vk_check(vkMapMemory(a->device, memory, 0, VK_WHOLE_SIZE, 0, &block->mapped), "vkMapMemory")) {
            vkFreeMemory(a->device, memory, NULL);
            free(block);
            return NULL;
        }
    }
    if (!block_ensure_span_capacity(block, 16)) {
        if (block->mapped)
            vkUnmapMemory(a->device, memory);
        vkFreeMemory(a->device, memory, NULL);
        free(block);
        return NULL;
    }
    block->free_spans[0] = (af_vk_span){.offset = 0, .size = actual};
    block->free_count = 1;

    ++a->stats.vk_allocations;
    ++a->stats.total_vk_allocations;
    a->stats.block_bytes += actual;

    block->next = a->blocks;
    a->blocks = block;
    return block;
}

af_vk_allocator *af_vk_allocator_create(VkPhysicalDevice physical, VkDevice device, VkQueue transfer_queue,
                                        uint32_t transfer_family) {
    if (!physical || !device)
        return NULL;
    af_vk_allocator *a = calloc(1, sizeof(*a));
    if (!a)
        return NULL;
    a->physical = physical;
    a->device = device;
    a->transfer_queue = transfer_queue;
    a->transfer_family = transfer_family;
    vkGetPhysicalDeviceMemoryProperties(physical, &a->memory_props);
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(physical, &props);
    a->non_coherent_atom = props.limits.nonCoherentAtomSize ? props.limits.nonCoherentAtomSize : 1;

    VkCommandPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                    .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                             VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                                    .queueFamilyIndex = transfer_family};
    if (!vk_check(vkCreateCommandPool(device, &pool, NULL, &a->transfer_pool), "create transfer command pool")) {
        free(a);
        return NULL;
    }
    return a;
}

void af_vk_allocator_destroy(af_vk_allocator *a) {
    if (!a)
        return;
#ifdef AF_DEBUG
    if (a->stats.live_suballocations != 0)
        af_log("warning", "Vulkan allocator destroyed with %llu live suballocations (%llu bytes).",
               (unsigned long long)a->stats.live_suballocations, (unsigned long long)a->stats.used_bytes);
#endif
    af_vk_block *block = a->blocks;
    while (block) {
        af_vk_block *next = block->next;
        block_destroy(a, block);
        block = next;
    }
    a->blocks = NULL;
    if (a->transfer_pool)
        vkDestroyCommandPool(a->device, a->transfer_pool, NULL);
    free(a);
}

uint32_t af_vk_allocator_find_type(const af_vk_allocator *a, uint32_t type_bits, VkMemoryPropertyFlags required,
                                   VkMemoryPropertyFlags preferred) {
    if (!a)
        return UINT32_MAX;
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < a->memory_props.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = a->memory_props.memoryTypes[i].propertyFlags;
        if (!(type_bits & (1u << i)) || (flags & required) != required)
            continue;
        if (fallback == UINT32_MAX)
            fallback = i;
        if ((flags & preferred) == preferred)
            return i;
    }
    return fallback;
}

VkMemoryPropertyFlags af_vk_allocator_type_flags(const af_vk_allocator *a, uint32_t memory_type) {
    if (!a || memory_type >= a->memory_props.memoryTypeCount)
        return 0;
    return a->memory_props.memoryTypes[memory_type].propertyFlags;
}

bool af_vk_allocate(af_vk_allocator *a, VkMemoryRequirements requirements, af_vk_memory_class mem_class,
                    VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool device_address,
                    af_vk_allocation *out) {
    if (!a || !out || !requirements.size)
        return false;
    *out = (af_vk_allocation){0};

    uint32_t type = af_vk_allocator_find_type(a, requirements.memoryTypeBits, required, preferred);
    if (type == UINT32_MAX)
        return false;

    VkMemoryPropertyFlags flags = a->memory_props.memoryTypes[type].propertyFlags;
    bool host_visible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    bool coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;

    VkDeviceSize alignment = requirements.alignment ? requirements.alignment : 1;
    VkDeviceSize size = requirements.size;
    if (host_visible && !coherent && a->non_coherent_atom > 1) {
        if (alignment < a->non_coherent_atom)
            alignment = a->non_coherent_atom;
        size = align_up(size, a->non_coherent_atom);
    }

    VkDeviceSize default_bytes = host_visible ? AF_VK_HOST_BLOCK_BYTES : AF_VK_DEVICE_BLOCK_BYTES;
    bool dedicated = size > default_bytes / 2;

    af_vk_block *chosen = NULL;
    VkDeviceSize offset = 0;
    if (!dedicated) {
        for (af_vk_block *b = a->blocks; b; b = b->next) {
            if (b->dedicated || b->memory_type != type || b->mem_class != mem_class ||
                b->device_address != device_address)
                continue;
            if (block_suballocate(b, size, alignment, &offset)) {
                chosen = b;
                break;
            }
        }
    }

    if (!chosen) {
        chosen = block_create(a, type, mem_class, device_address, size, dedicated);
        if (!chosen)
            return false;
        if (!block_suballocate(chosen, size, alignment, &offset))
            return false;
    }

    ++a->stats.live_suballocations;
    a->stats.used_bytes += size;

    *out = (af_vk_allocation){.memory = chosen->memory,
                              .offset = offset,
                              .size = size,
                              .requested_size = requirements.size,
                              .mapped = chosen->mapped ? (unsigned char *)chosen->mapped + offset : NULL,
                              .memory_type = type,
                              .coherent = chosen->coherent,
                              .host_visible = chosen->host_visible,
                              .device_local = chosen->device_local,
                              .block = chosen};
    return true;
}

void af_vk_free(af_vk_allocator *a, af_vk_allocation *alloc) {
    if (!a || !alloc || !alloc->block)
        return;
    af_vk_block *block = (af_vk_block *)alloc->block;
    block_release_span(block, alloc->offset, alloc->size);
    if (a->stats.live_suballocations > 0)
        --a->stats.live_suballocations;
    if (a->stats.used_bytes >= alloc->size)
        a->stats.used_bytes -= alloc->size;
    else
        a->stats.used_bytes = 0;

    if (block->used == 0) {
        bool should_free = block->dedicated;
        if (!should_free) {
            /* Keep at most one warm empty pool block per (type, class, bda) to prevent churn. */
            for (af_vk_block *other = a->blocks; other; other = other->next) {
                if (other != block && !other->dedicated && other->used == 0 &&
                    other->memory_type == block->memory_type && other->mem_class == block->mem_class &&
                    other->device_address == block->device_address) {
                    should_free = true;
                    break;
                }
            }
        }
        if (should_free) {
            af_vk_block **link = &a->blocks;
            while (*link && *link != block)
                link = &(*link)->next;
            if (*link == block)
                *link = block->next;
            block_destroy(a, block);
        }
    }

    *alloc = (af_vk_allocation){0};
}

static VkMappedMemoryRange make_mapped_range(const af_vk_allocator *a, const af_vk_allocation *alloc,
                                             VkDeviceSize offset, VkDeviceSize size) {
    const af_vk_block *block = (const af_vk_block *)alloc->block;
    VkDeviceSize atom = a->non_coherent_atom;
    VkDeviceSize begin = align_down(alloc->offset + offset, atom);
    VkDeviceSize raw_end = (size == VK_WHOLE_SIZE) ? (alloc->offset + alloc->size) : (alloc->offset + offset + size);
    if (raw_end > alloc->offset + alloc->size)
        raw_end = alloc->offset + alloc->size;
    VkDeviceSize end = align_up(raw_end, atom);
    if (block && end > block->size)
        end = block->size;
    return (VkMappedMemoryRange){.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                 .memory = alloc->memory,
                                 .offset = begin,
                                 .size = end > begin ? end - begin : VK_WHOLE_SIZE};
}

bool af_vk_flush(const af_vk_allocator *a, const af_vk_allocation *alloc, VkDeviceSize offset, VkDeviceSize size) {
    if (!a || !alloc || !alloc->memory || alloc->coherent)
        return true;
    VkMappedMemoryRange range = make_mapped_range(a, alloc, offset, size);
    return vk_check(vkFlushMappedMemoryRanges(a->device, 1, &range), "vkFlushMappedMemoryRanges");
}

bool af_vk_invalidate(const af_vk_allocator *a, const af_vk_allocation *alloc, VkDeviceSize offset, VkDeviceSize size) {
    if (!a || !alloc || !alloc->memory || alloc->coherent)
        return true;
    VkMappedMemoryRange range = make_mapped_range(a, alloc, offset, size);
    return vk_check(vkInvalidateMappedMemoryRanges(a->device, 1, &range), "vkInvalidateMappedMemoryRanges");
}

bool af_vk_create_buffer(af_vk_allocator *a, VkDeviceSize size, VkBufferUsageFlags usage,
                         VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred, bool device_address,
                         VkBuffer *out_buffer, af_vk_allocation *out_alloc) {
    if (!a || !size || !out_buffer || !out_alloc)
        return false;
    *out_buffer = VK_NULL_HANDLE;
    *out_alloc = (af_vk_allocation){0};
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = size,
                               .usage = usage,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (!vk_check(vkCreateBuffer(a->device, &info, NULL, out_buffer), "vkCreateBuffer"))
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(a->device, *out_buffer, &req);
    if (!af_vk_allocate(a, req, AF_VK_MEM_BUFFER, required, preferred, device_address, out_alloc) ||
        !vk_check(vkBindBufferMemory(a->device, *out_buffer, out_alloc->memory, out_alloc->offset),
                  "vkBindBufferMemory")) {
        af_vk_destroy_buffer(a, out_buffer, out_alloc);
        return false;
    }
    return true;
}

void af_vk_destroy_buffer(af_vk_allocator *a, VkBuffer *buffer, af_vk_allocation *alloc) {
    if (!a)
        return;
    if (buffer && *buffer) {
        vkDestroyBuffer(a->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
    }
    if (alloc && alloc->block)
        af_vk_free(a, alloc);
}

VkCommandBuffer af_vk_begin_transfer(af_vk_allocator *a) {
    if (!a || !a->transfer_pool)
        return VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo info = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                        .commandPool = a->transfer_pool,
                                        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                        .commandBufferCount = 1};
    VkCommandBuffer command = VK_NULL_HANDLE;
    if (!vk_check(vkAllocateCommandBuffers(a->device, &info, &command), "allocate transfer command buffer"))
        return VK_NULL_HANDLE;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (!vk_check(vkBeginCommandBuffer(command, &begin), "begin transfer command buffer")) {
        vkFreeCommandBuffers(a->device, a->transfer_pool, 1, &command);
        return VK_NULL_HANDLE;
    }
    return command;
}

bool af_vk_end_transfer(af_vk_allocator *a, VkCommandBuffer command) {
    if (!a || !command)
        return false;
    bool ok = vk_check(vkEndCommandBuffer(command), "end transfer command buffer");
    VkFence fence = VK_NULL_HANDLE;
    if (ok) {
        VkFenceCreateInfo fence_info = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        ok = vk_check(vkCreateFence(a->device, &fence_info, NULL, &fence), "create transfer fence");
    }
    if (ok) {
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                               .commandBufferCount = 1,
                               .pCommandBuffers = &command};
        ok = vk_check(vkQueueSubmit(a->transfer_queue, 1, &submit, fence), "submit transfer") &&
             vk_check(vkWaitForFences(a->device, 1, &fence, VK_TRUE, UINT64_MAX), "wait transfer fence");
    }
    if (fence)
        vkDestroyFence(a->device, fence, NULL);
    vkFreeCommandBuffers(a->device, a->transfer_pool, 1, &command);
    if (ok)
        ++a->stats.staged_uploads;
    return ok;
}

bool af_vk_upload_buffer_staged(af_vk_allocator *a, const void *data, VkDeviceSize size, VkBufferUsageFlags usage,
                                bool device_address, VkBuffer *out_buffer, af_vk_allocation *out_alloc) {
    if (!a || !data || !size || !out_buffer || !out_alloc)
        return false;
    *out_buffer = VK_NULL_HANDLE;
    *out_alloc = (af_vk_allocation){0};

    const char *force_env = getenv("AFTERLIGHT_FORCE_STAGING_UPLOAD");
    bool force_staging = force_env && force_env[0] && strcmp(force_env, "0") != 0;

    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = size,
                               .usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    VkBuffer dst_buffer = VK_NULL_HANDLE;
    if (!vk_check(vkCreateBuffer(a->device, &info, NULL, &dst_buffer), "create device buffer"))
        return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(a->device, dst_buffer, &req);

    /* Fast path on unified DEVICE_LOCAL | HOST_VISIBLE heaps (e.g., Apple Silicon, ReBAR). */
    if (!force_staging) {
        uint32_t unified =
            af_vk_allocator_find_type(a, req.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (unified != UINT32_MAX) {
            af_vk_allocation dst_alloc = {0};
            if (!af_vk_allocate(a, req, AF_VK_MEM_BUFFER,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, device_address, &dst_alloc) ||
                !vk_check(vkBindBufferMemory(a->device, dst_buffer, dst_alloc.memory, dst_alloc.offset),
                          "bind unified buffer")) {
                vkDestroyBuffer(a->device, dst_buffer, NULL);
                af_vk_free(a, &dst_alloc);
                return false;
            }
            memcpy(dst_alloc.mapped, data, (size_t)size);
            if (!af_vk_flush(a, &dst_alloc, 0, size)) {
                vkDestroyBuffer(a->device, dst_buffer, NULL);
                af_vk_free(a, &dst_alloc);
                return false;
            }
            *out_buffer = dst_buffer;
            *out_alloc = dst_alloc;
            return true;
        }
    }

    /* Discrete GPU (or forced staging test) path: stage via HOST_VISIBLE -> DEVICE_LOCAL. */
    af_vk_allocation dst_alloc = {0};
    if (!af_vk_allocate(a, req, AF_VK_MEM_BUFFER, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, device_address,
                        &dst_alloc) ||
        !vk_check(vkBindBufferMemory(a->device, dst_buffer, dst_alloc.memory, dst_alloc.offset),
                  "bind device-local buffer")) {
        vkDestroyBuffer(a->device, dst_buffer, NULL);
        af_vk_free(a, &dst_alloc);
        return false;
    }

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    af_vk_allocation staging_alloc = {0};
    if (!af_vk_create_buffer(a, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &staging_buffer, &staging_alloc)) {
        vkDestroyBuffer(a->device, dst_buffer, NULL);
        af_vk_free(a, &dst_alloc);
        return false;
    }

    memcpy(staging_alloc.mapped, data, (size_t)size);
    bool ok = af_vk_flush(a, &staging_alloc, 0, size);
    VkCommandBuffer cmd = ok ? af_vk_begin_transfer(a) : VK_NULL_HANDLE;
    if (cmd) {
        VkBufferCopy copy = {.srcOffset = 0, .dstOffset = 0, .size = size};
        vkCmdCopyBuffer(cmd, staging_buffer, dst_buffer, 1, &copy);
        VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                         .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                                         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                         .buffer = dst_buffer,
                                         .offset = 0,
                                         .size = size};
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 1,
                             &barrier, 0, NULL);
        ok = af_vk_end_transfer(a, cmd);
    } else {
        ok = false;
    }

    af_vk_destroy_buffer(a, &staging_buffer, &staging_alloc);
    if (!ok) {
        vkDestroyBuffer(a->device, dst_buffer, NULL);
        af_vk_free(a, &dst_alloc);
        return false;
    }

    a->stats.staged_bytes += size;
    *out_buffer = dst_buffer;
    *out_alloc = dst_alloc;
    return true;
}

af_vk_allocator_stats af_vk_allocator_get_stats(const af_vk_allocator *a) {
    return a ? a->stats : (af_vk_allocator_stats){0};
}

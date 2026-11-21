/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer_internal.h"
#include "cluster_comp.h"

void af_renderer_set_cluster_rendering(af_renderer *r, bool enabled, bool debug_colors) {
    r->clusters_disabled = !enabled;
    r->cluster_colors = debug_colors;
}

void af_renderer_set_cluster_lod(af_renderer *r, bool enabled) {
    r->cluster_lod_disabled = !enabled;
}

void af_renderer_set_cluster_gpu_culling(af_renderer *r, bool enabled) {
    r->cluster_gpu_disabled = !enabled;
}

af_cluster_stats af_renderer_cluster_stats(const af_renderer *r) {
    af_cluster_stats stats = r->cluster_stats;
    stats.gpu_supported = r->cluster_gpu_supported;
    stats.page_uploads = r->page_uploads;
    stats.page_evictions = r->page_evictions;
    stats.page_failures = r->page_failures;
    stats.page_budget = r->page_budget;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        const af_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache)
            continue;
        stats.queued_requests += af_cluster_request_depth(cache->stream);
        uint32_t peak = af_cluster_request_peak(cache->stream);
        if (peak > stats.peak_asset_requests)
            stats.peak_asset_requests = peak;
        for (unsigned p = 0; p < AF_CLUSTER_RESIDENT_PAGES; ++p) {
            const af_gpu_mesh *gpu = &cache->pages[p].gpu;
            stats.resident_pages += gpu->cluster_count != 0;
            stats.page_bytes += gpu->vertices.allocation_bytes + gpu->indices.allocation_bytes;
        }
    }
    return stats;
}



static float cluster_error_scale(af_mat4 world, float pixel_scale) {
    af_vec3 axes[3];
    for (unsigned i = 0; i < 3; ++i)
        axes[i] = (af_vec3){world.elements[i * 4], world.elements[i * 4 + 1], world.elements[i * 4 + 2]};
    /* Bound the largest singular value, including parent-induced shear. */
    float squared = 0;
    for (unsigned i = 0; i < 3; ++i) {
        float sum = 0;
        for (unsigned j = 0; j < 3; ++j)
            sum += fabsf(af_vec3_dot(axes[i], axes[j]));
        squared = fmaxf(squared, sum);
    }
    return sqrtf(squared) * pixel_scale;
}

static bool cluster_wants_coarse(af_mat4 clip, af_vec3 minimum, af_vec3 maximum, float pixels, bool lod) {
    if (!lod)
        return false;
    float x = clip.elements[3], y = clip.elements[7], z = clip.elements[11];
    float nearest = clip.elements[15] + x * (x >= 0 ? minimum.x : maximum.x) + y * (y >= 0 ? minimum.y : maximum.y) +
                    z * (z >= 0 ? minimum.z : maximum.z);
    return nearest > 0 && pixels <= 2.f * nearest;
}

static uint64_t cluster_page_bytes(const af_cached_cluster_page *page) {
    return page->gpu.vertices.allocation_bytes + page->gpu.indices.allocation_bytes;
}

static af_cached_cluster_page *oldest_cluster_page(af_cluster_residency *cache, af_cached_cluster_page *oldest) {
    for (unsigned i = 0; i < AF_CLUSTER_RESIDENT_PAGES; ++i) {
        af_cached_cluster_page *page = &cache->pages[i];
        if (page->gpu.cluster_count && !page->frame_mask && (!oldest || page->last_used < oldest->last_used))
            oldest = page;
    }
    return oldest;
}

static af_cached_cluster_page *oldest_cluster_page_global(af_renderer *r) {
    af_cached_cluster_page *oldest = NULL;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (cache)
            oldest = oldest_cluster_page(cache, oldest);
    }
    return oldest;
}

static uint64_t evict_cluster_page(af_renderer *r, af_cached_cluster_page *page) {
    uint64_t bytes = cluster_page_bytes(page);
    af_r_destroy_mesh(r, &page->gpu);
    *page = (af_cached_cluster_page){0};
    ++r->page_evictions;
    return bytes;
}

static void release_pending_cluster_page(af_renderer *r, af_pending_cluster_page *pending) {
    af_cluster_free(&pending->mesh);
    af_r_destroy_mesh(r, &pending->gpu);
    *pending = (af_pending_cluster_page){0};
}

static void fail_cluster_page(af_renderer *r, af_cluster_residency *cache) {
    cache->page_requirements[cache->pending.region] = UINT64_MAX;
    ++r->page_failures;
    release_pending_cluster_page(r, &cache->pending);
}

static bool stage_cluster_page(af_renderer *r, af_cluster_residency *cache) {
    af_pending_cluster_page *p = &cache->pending;
    if (p->mesh.count && r->rendered_frames - p->last_wanted > 2)
        release_pending_cluster_page(r, p);
    if (p->mesh.count)
        return true;
    if (!af_cluster_take_page(cache->stream, &p->region, &p->mesh))
        return false;
    p->last_wanted = r->rendered_frames;
    if (!p->mesh.count ||
        !af_r_create_upload_buffer(r, (VkDeviceSize)p->mesh.mesh.vertex_count * sizeof(af_vertex),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &p->gpu.vertices, &p->vertices) ||
        !af_r_create_upload_buffer(r, (VkDeviceSize)p->mesh.mesh.index_count * sizeof(uint32_t),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &p->gpu.indices, &p->indices)) {
        fail_cluster_page(r, cache);
        return false;
    }
    cache->page_requirements[p->region] = p->vertices.size + p->indices.size;
    return true;
}

void af_r_update_cluster_pages(af_renderer *r) {
    uint32_t frame = 1u << r->frame_index;
    uint64_t resident = 0, reclaimable = 0;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache)
            continue;
        af_cluster_prune_requests(cache->stream, r->rendered_frames);
        for (unsigned j = 0; j < AF_CLUSTER_RESIDENT_PAGES; ++j) {
            af_cached_cluster_page *page = &cache->pages[j];
            page->frame_mask &= ~frame;
            uint64_t bytes = cluster_page_bytes(page);
            resident += bytes;
            if (!page->frame_mask)
                reclaimable += bytes;
        }
    }
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache || !stage_cluster_page(r, cache))
            continue;
        af_pending_cluster_page *p = &cache->pending;
        uint64_t bytes = cache->page_requirements[p->region];
        if (bytes > r->page_budget) {
            release_pending_cluster_page(r, p);
            continue;
        }
        if (bytes > r->page_budget - (resident - reclaimable))
            continue;
        af_cached_cluster_page *slot = NULL;
        for (unsigned j = 0; j < AF_CLUSTER_RESIDENT_PAGES; ++j) {
            if (!cache->pages[j].gpu.cluster_count) {
                slot = &cache->pages[j];
                break;
            }
        }
        if (!slot) {
            slot = oldest_cluster_page(cache, NULL);
            if (!slot)
                continue;
            uint64_t removed = evict_cluster_page(r, slot);
            resident -= removed;
            reclaimable -= removed;
        }
        while (bytes > r->page_budget - resident) {
            uint64_t removed = evict_cluster_page(r, oldest_cluster_page_global(r));
            resident -= removed;
            reclaimable -= removed;
        }
        af_mesh_data *data = &p->mesh.mesh;
        if (!af_r_fill_upload_buffer(r, data->vertices, (VkDeviceSize)data->vertex_count * sizeof(af_vertex),
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &p->gpu.vertices, p->vertices) ||
            !af_r_fill_upload_buffer(r, data->indices, (VkDeviceSize)data->index_count * sizeof(uint32_t),
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &p->gpu.indices, p->indices)) {
            fail_cluster_page(r, cache);
            continue;
        }
        p->gpu.vertex_count = data->vertex_count;
        p->gpu.index_count = data->index_count;
        p->gpu.clusters = p->mesh.clusters;
        p->gpu.cluster_count = p->mesh.count;
        p->mesh.clusters = NULL;
        *slot = (af_cached_cluster_page){.gpu = p->gpu,
                                         .region = p->region,
                                         .frame_mask = frame,
                                         .last_used = r->rendered_frames,
                                         .generation = ++r->page_uploads};
        p->gpu = (af_gpu_mesh){0};
        release_pending_cluster_page(r, p);
        resident += bytes;
    }
}

bool af_renderer_set_cluster_cache_budget(af_renderer *r, uint64_t bytes) {
    if (bytes < r->page_budget) {
        uint64_t resident = af_renderer_cluster_stats(r).page_bytes;
        if (resident > bytes) {
            if (!af_renderer_wait_idle(r))
                return false;
            for (size_t i = 0; i < r->mesh_count; ++i) {
                af_cluster_residency *cache = r->meshes[i].gpu.residency;
                if (cache)
                    for (unsigned j = 0; j < AF_CLUSTER_RESIDENT_PAGES; ++j)
                        cache->pages[j].frame_mask = 0;
            }
            while (resident > bytes)
                resident -= evict_cluster_page(r, oldest_cluster_page_global(r));
        }
    }
    r->page_budget = bytes;
    return true;
}

void af_r_use_cluster_page(af_renderer *r, af_cached_cluster_page *page) {
    page->frame_mask |= 1u << r->frame_index;
    page->last_used = r->rendered_frames;
}

static bool cluster_region_visible(const af_renderer *r, const af_cluster_stream_region *region, af_mat4 model,
                                   af_mat4 projection) {
    af_shadow_bounds bounds = af_shadow_transform_bounds(model, region->minimum, region->maximum);
    return r->culling_disabled || af_shadow_intersects(projection, &bounds);
}

static bool cluster_region_wants_page(const af_renderer *r, const af_cluster_stream_region *region, af_mat4 clip,
                                      float error_scale) {
    return region->page_clusters && !cluster_wants_coarse(clip, region->minimum, region->maximum,
                                                          region->error * error_scale, !r->cluster_lod_disabled);
}

static const af_gpu_mesh *select_cluster_page(af_renderer *r, const af_gpu_mesh *base, uint32_t region_index,
                                              af_mat4 model, af_mat4 projection, af_mat4 clip, float error_scale,
                                              af_cluster_span *span) {
    af_cluster_residency *cache = base->residency;
    const af_cluster_stream_region *region = af_cluster_stream_region_at(cache->stream, region_index);
    if (!cluster_region_visible(r, region, model, projection))
        return NULL;
    uint64_t bytes = cache->page_requirements[region_index];
    if (!bytes)
        bytes = (uint64_t)region->page_vertices * sizeof(af_vertex) + (uint64_t)region->page_indices * sizeof(uint32_t);
    if (bytes != UINT64_MAX && bytes <= r->page_budget && cluster_region_wants_page(r, region, clip, error_scale)) {
        for (unsigned i = 0; i < AF_CLUSTER_RESIDENT_PAGES; ++i) {
            af_cached_cluster_page *page = &cache->pages[i];
            if (page->gpu.cluster_count && page->region == region_index) {
                af_r_use_cluster_page(r, page);
                *span = (af_cluster_span){0, page->gpu.cluster_count};
                return &page->gpu;
            }
        }
        if (cache->pending.mesh.count && cache->pending.region == region_index) {
            cache->pending.last_wanted = r->rendered_frames;
        } else {
            af_vec3 center = af_vec3_scale(af_vec3_add(region->minimum, region->maximum), .5f);
            float distance = af_mat4_transform(clip, (af_vec4){center.x, center.y, center.z, 1}).w;
            af_cluster_request_page(cache->stream, region_index, r->rendered_frames, distance);
        }
    }
    *span = (af_cluster_span){region->coarse_first, region->coarse_count};
    return base;
}

af_cluster_view af_r_cluster_view(const af_frame_uniform *uniform, const af_camera *camera) {
    af_cluster_view view = {.projection = uniform->view_projection};
    float width = uniform->water_screen.x * (uniform->water_viewport.z - uniform->water_viewport.x);
    float height = uniform->water_screen.y * (uniform->water_viewport.w - uniform->water_viewport.y);
    /* Two pixels of guard space for jittered rasterization. */
    for (unsigned column = 0; column < 4; ++column) {
        view.projection.elements[column * 4] *= width / (width + 4);
        view.projection.elements[column * 4 + 1] *= height / (height + 4);
    }
    view.pixel_scale = height * .5f / tanf(camera->vertical_fov * .5f);
    return view;
}

static int compare_cluster_page_regions(const void *a, const void *b) {
    const af_cached_cluster_page *const *left = a, *const *right = b;
    return ((*left)->region > (*right)->region) - ((*left)->region < (*right)->region);
}

/* Secondary passes replace coarse regions with the same visible fine pages
   as the opaque pass. They keep coarse geometry outside the camera frustum. */
size_t af_r_cluster_index_ranges(const af_renderer *r, const af_gpu_mesh *base, af_mat4 model, af_cluster_view view,
                                   af_cluster_index_range *ranges) {
    af_cluster_residency *cache = base->residency;
    if (r->clusters_disabled || !cache)
        return 0;
    af_mat4 clip = af_mat4_mul(view.projection, model);
    float error_scale = cluster_error_scale(model, view.pixel_scale);
    af_cached_cluster_page *fine[AF_CLUSTER_RESIDENT_PAGES];
    size_t count = 0;
    for (unsigned i = 0; i < AF_CLUSTER_RESIDENT_PAGES; ++i) {
        af_cached_cluster_page *page = &cache->pages[i];
        if (!page->gpu.cluster_count)
            continue;
        const af_cluster_stream_region *region = af_cluster_stream_region_at(cache->stream, page->region);
        if (cluster_region_wants_page(r, region, clip, error_scale) &&
            cluster_region_visible(r, region, model, view.projection))
            fine[count++] = page;
    }
    if (!count)
        return 0;
    qsort(fine, count, sizeof(*fine), compare_cluster_page_regions);
    size_t used = 0;
    uint32_t first = 0;
    for (size_t i = 0; i < count; ++i) {
        af_cached_cluster_page *page = fine[i];
        const af_cluster_stream_region *region = af_cluster_stream_region_at(cache->stream, page->region);
        uint32_t start = base->clusters[region->coarse_first].first_index;
        if (first < start)
            ranges[used++] = (af_cluster_index_range){.first = first, .count = start - first};
        ranges[used++] = (af_cluster_index_range){.count = page->gpu.index_count, .page = page};
        const af_cluster *last = &base->clusters[region->coarse_first + region->coarse_count - 1];
        first = last->first_index + last->index_count;
    }
    if (first < base->index_count)
        ranges[used++] = (af_cluster_index_range){.first = first, .count = base->index_count - first};
    return used;
}

static af_cluster_span cluster_range(const af_gpu_mesh *mesh, uint32_t region, af_mat4 clip, float error_scale,
                                     bool lod, bool *coarse) {
    *coarse = false;
    if (!mesh->cluster_region_count)
        return (af_cluster_span){0, mesh->cluster_fine_count};
    const af_cluster_region *r = &mesh->cluster_regions[region];
    if (r->coarse_count && cluster_wants_coarse(clip, r->minimum, r->maximum, r->error * error_scale, lod)) {
        *coarse = true;
        return (af_cluster_span){r->coarse_first, r->coarse_count};
    }
    return (af_cluster_span){r->fine_first, r->fine_count};
}



#define AF_CLUSTER_GPU_LIMIT 65536u

void af_r_destroy_cluster_compute(af_renderer *r) {
    if (r->cluster_pipeline)
        vkDestroyPipeline(r->device, r->cluster_pipeline, NULL);
    if (r->cluster_pipeline_layout)
        vkDestroyPipelineLayout(r->device, r->cluster_pipeline_layout, NULL);
    if (r->cluster_pool)
        vkDestroyDescriptorPool(r->device, r->cluster_pool, NULL);
    if (r->cluster_layout)
        vkDestroyDescriptorSetLayout(r->device, r->cluster_layout, NULL);
    for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
        af_r_destroy_buffer(r, &r->frames[i].cluster_input);
        af_r_destroy_buffer(r, &r->frames[i].cluster_transforms);
        af_r_destroy_buffer(r, &r->frames[i].cluster_output);
    }
    free(r->cluster_candidates);
    free(r->cluster_matrices);
    free(r->cluster_spans);
    free(r->cluster_stream_batches);
}

static bool create_cluster_compute(af_renderer *r) {
    VkDescriptorSetLayoutBinding bindings[3] = {0};
    for (unsigned i = 0; i < 3; ++i)
        bindings[i] =
            (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, NULL};
    VkDescriptorSetLayoutCreateInfo layout = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = bindings};
    VkDescriptorPoolSize storage = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * AF_FRAMES_IN_FLIGHT};
    VkDescriptorPoolCreateInfo pool = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                       .maxSets = AF_FRAMES_IN_FLIGHT,
                                       .poolSizeCount = 1,
                                       .pPoolSizes = &storage};
    if (vkCreateDescriptorSetLayout(r->device, &layout, NULL, &r->cluster_layout) != VK_SUCCESS ||
        vkCreateDescriptorPool(r->device, &pool, NULL, &r->cluster_pool) != VK_SUCCESS)
        return false;
    VkPushConstantRange push = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 2 * sizeof(uint32_t)};
    VkPipelineLayoutCreateInfo pipeline_layout = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
                                                  .setLayoutCount = 1,
                                                  .pSetLayouts = &r->cluster_layout,
                                                  .pushConstantRangeCount = 1,
                                                  .pPushConstantRanges = &push};
    if (vkCreatePipelineLayout(r->device, &pipeline_layout, NULL, &r->cluster_pipeline_layout) != VK_SUCCESS)
        return false;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo module = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                                       .codeSize = sizeof(af_cluster_comp),
                                       .pCode = af_cluster_comp};
    if (vkCreateShaderModule(r->device, &module, NULL, &shader) != VK_SUCCESS)
        return false;
    VkComputePipelineCreateInfo info = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
                                        .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                                                  .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                                                  .module = shader,
                                                  .pName = "main"},
                                        .layout = r->cluster_pipeline_layout};
    VkResult result = vkCreateComputePipelines(r->device, VK_NULL_HANDLE, 1, &info, NULL, &r->cluster_pipeline);
    vkDestroyShaderModule(r->device, shader, NULL);
    if (result != VK_SUCCESS)
        return false;
    VkDescriptorSetAllocateInfo sets = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                                        .descriptorPool = r->cluster_pool,
                                        .descriptorSetCount = 1,
                                        .pSetLayouts = &r->cluster_layout};
    for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i)
        if (vkAllocateDescriptorSets(r->device, &sets, &r->frames[i].cluster_descriptor) != VK_SUCCESS)
            return false;
    return true;
}

static bool cluster_buffers(af_renderer *r, af_frame *frame, uint32_t count, size_t objects) {
    VkDeviceSize bytes = (VkDeviceSize)(count + 1) * sizeof(VkDrawIndexedIndirectCommand);
    if (frame->cluster_output.capacity < bytes) {
        af_r_destroy_buffer(r, &frame->cluster_output);
        if (!af_r_upload_buffer(r, NULL, bytes,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           &frame->cluster_output))
            return false;
        frame->cluster_output.capacity = bytes;
    }
    if (!af_r_stream_buffer(r, &frame->cluster_input, r->cluster_candidates, count * sizeof(*r->cluster_candidates),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !af_r_stream_buffer(r, &frame->cluster_transforms, r->cluster_matrices, objects * sizeof(*r->cluster_matrices),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT))
        return false;
    VkDescriptorBufferInfo buffers[] = {{frame->cluster_input.handle, 0, VK_WHOLE_SIZE},
                                        {frame->cluster_transforms.handle, 0, VK_WHOLE_SIZE},
                                        {frame->cluster_output.handle, 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet writes[3] = {0};
    for (unsigned i = 0; i < 3; ++i)
        writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                           .dstSet = frame->cluster_descriptor,
                                           .dstBinding = i,
                                           .descriptorCount = 1,
                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                           .pBufferInfo = &buffers[i]};
    vkUpdateDescriptorSets(r->device, 3, writes, 0, NULL);
    return true;
}

static void cluster_read_stats(af_renderer *r, af_frame *frame) {
    if (!frame->cluster_submitted)
        return;
    void *mapped = frame->cluster_output.alloc.mapped;
    if (mapped && af_vk_invalidate(r->allocator, &frame->cluster_output.alloc, 0, VK_WHOLE_SIZE)) {
        const VkDrawIndexedIndirectCommand *counts = mapped;
        r->cluster_gpu_candidates = frame->cluster_submitted;
        r->cluster_gpu_coarse_regions = frame->cluster_coarse_regions;
        r->cluster_gpu_visible = counts[frame->cluster_submitted].instanceCount;
        r->cluster_gpu_triangles = counts[frame->cluster_submitted].indexCount;
    }
    frame->cluster_submitted = 0;
}

static void record_cluster_compute(af_renderer *r, af_frame *frame, uint32_t count) {
    VkDeviceSize counter = (VkDeviceSize)count * sizeof(VkDrawIndexedIndirectCommand);
    vkCmdFillBuffer(frame->command, frame->cluster_output.handle, counter, sizeof(VkDrawIndexedIndirectCommand), 0);
    VkMemoryBarrier init = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT};
    vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &init, 0, NULL, 0, NULL);
    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, r->cluster_pipeline);
    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_COMPUTE, r->cluster_pipeline_layout, 0, 1,
                            &frame->cluster_descriptor, 0, NULL);
    uint32_t push[] = {count, !r->culling_disabled};
    vkCmdPushConstants(frame->command, r->cluster_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), push);
    vkCmdDispatch(frame->command, (count + 63) / 64, 1, 1);
    VkMemoryBarrier ready = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                             .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                             .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &ready, 0, NULL, 0,
                         NULL);
    frame->cluster_submitted = count;
}

static bool reserve_cluster_commands(af_renderer *r, size_t objects) {
    if (!r->cluster_candidates) {
        r->cluster_candidates = malloc(AF_CLUSTER_GPU_LIMIT * sizeof(*r->cluster_candidates));
        if (!r->cluster_candidates)
            return false;
    }
    if (objects <= r->cluster_object_capacity)
        return true;
    af_cluster_span *spans = realloc(r->cluster_spans, objects * sizeof(*spans));
    if (!spans)
        return false;
    r->cluster_spans = spans;
    af_mat4 *matrices = realloc(r->cluster_matrices, objects * sizeof(*matrices));
    if (!matrices)
        return false;
    r->cluster_matrices = matrices;
    r->cluster_object_capacity = objects;
    return true;
}

bool af_r_prepare_cluster_commands(af_renderer *r, af_frame *frame, const af_scene *scene, af_mat4 projection,
                                     float pixel_scale, size_t objects) {
    cluster_read_stats(r, frame);
    if (!r->cluster_gpu_supported || r->cluster_gpu_disabled || r->clusters_disabled || r->cluster_colors || !objects ||
        objects > AF_SCENE_FILE_MAX_OBJECTS)
        return false;
    bool any = false;
    for (size_t i = 0; i < r->mesh_count && !any; ++i)
        any = r->meshes[i].gpu.cluster_count != 0;
    if (!any || !reserve_cluster_commands(r, objects))
        return false;
    memset(r->cluster_spans, 0, objects * sizeof(*r->cluster_spans));
    memset(r->cluster_matrices, 0, objects * sizeof(*r->cluster_matrices));
    uint32_t count = 0, coarse_regions = 0, batches = 0;
    for (size_t i = 0; i < objects; ++i) {
        const af_scene_object *o = af_scene_object_at(scene, i);
        if (o->material.alpha_mode != AF_ALPHA_OPAQUE || !af_scene_object_visible(scene, o->id) ||
            af_scene_get_water(scene, o->id) || !strcmp(o->mesh, "__node"))
            continue;
        size_t index = af_r_find_mesh_index(r, o->mesh);
        if (index == r->mesh_count)
            continue;
        const af_gpu_mesh *mesh = &r->meshes[index].gpu;
        af_mat4 world;
        if (!mesh->cluster_count || (!mesh->residency && mesh->cluster_count > AF_CLUSTER_GPU_LIMIT - count) ||
            !af_r_object_world(r, scene, i, &world))
            continue;
        af_shadow_bounds bounds = af_shadow_transform_bounds(world, mesh->minimum, mesh->maximum);
        if (!r->culling_disabled && !af_shadow_intersects(projection, &bounds))
            continue;
        af_mat4 clip = af_mat4_mul(projection, world);
        r->cluster_matrices[i] = clip;
        uint32_t first = count;
        float error_scale = cluster_error_scale(world, pixel_scale);
        if (mesh->residency) {
            if (!r->cluster_stream_batches) {
                r->cluster_stream_batches = malloc(AF_CLUSTER_GPU_LIMIT * sizeof(*r->cluster_stream_batches));
                if (!r->cluster_stream_batches)
                    continue;
            }
            uint32_t batch_start = batches, coarse = 0;
            uint32_t regions = af_cluster_stream_region_count(mesh->residency->stream);
            for (uint32_t region = 0; region < regions; ++region) {
                af_cluster_span span;
                const af_gpu_mesh *selected =
                    select_cluster_page(r, mesh, region, world, projection, clip, error_scale, &span);
                if (!selected)
                    continue;
                if (span.count > AF_CLUSTER_GPU_LIMIT - count) {
                    count = first;
                    batches = batch_start;
                    coarse = 0;
                    break;
                }
                af_cluster_stream_batch *last = batches > batch_start ? &r->cluster_stream_batches[batches - 1] : NULL;
                if (last && last->mesh == selected)
                    last->commands.count += span.count;
                else
                    r->cluster_stream_batches[batches++] = (af_cluster_stream_batch){selected, {count, span.count}};
                coarse += selected == mesh;
                for (uint32_t c = span.first; c < span.first + span.count; ++c)
                    r->cluster_candidates[count++] = (af_cluster_candidate){selected->clusters[c], {(uint32_t)i}};
            }
            coarse_regions += coarse;
            r->cluster_spans[i] = (af_cluster_span){batch_start, batches - batch_start};
            continue;
        }
        uint32_t regions = mesh->cluster_region_count ? mesh->cluster_region_count : 1;
        for (uint32_t region = 0; region < regions; ++region) {
            bool coarse;
            af_cluster_span span = cluster_range(mesh, region, clip, error_scale, !r->cluster_lod_disabled, &coarse);
            coarse_regions += coarse;
            for (uint32_t c = span.first; c < span.first + span.count; ++c)
                r->cluster_candidates[count++] = (af_cluster_candidate){mesh->clusters[c], {(uint32_t)i}};
        }
        r->cluster_spans[i] = (af_cluster_span){first, count - first};
    }
    if (!count)
        return false;
    if ((!r->cluster_pipeline && !create_cluster_compute(r)) || !cluster_buffers(r, frame, count, objects)) {
        af_log("warning", "Cluster compute unavailable. Using CPU culling.");
        r->cluster_gpu_supported = false;
        return false;
    }
    record_cluster_compute(r, frame, count);
    r->cluster_stats.gpu_active = true;
    frame->cluster_coarse_regions = coarse_regions;
    r->cluster_stats.coarse_regions = r->cluster_gpu_coarse_regions;
    r->cluster_stats.candidates = r->cluster_gpu_candidates;
    r->cluster_stats.visible = r->cluster_gpu_visible;
    r->cluster_stats.triangles = r->cluster_gpu_triangles;
    return true;
}

static size_t draw_cluster_commands(af_renderer *r, af_frame *frame, af_cluster_span span) {
    size_t calls = 0;
    while (span.count) {
        uint32_t count = span.count < r->cluster_draw_limit ? span.count : r->cluster_draw_limit;
        vkCmdDrawIndexedIndirect(frame->command, frame->cluster_output.handle,
                                 (VkDeviceSize)span.first * sizeof(VkDrawIndexedIndirectCommand), count,
                                 sizeof(VkDrawIndexedIndirectCommand));
        span.first += count;
        span.count -= count;
        ++calls;
    }
    return calls;
}

static size_t draw_cluster_span(af_renderer *r, af_frame *frame, const af_gpu_mesh *mesh, af_mesh_draw draw,
                                af_mat4 visibility, af_cluster_span span, uint32_t object, uint32_t color_offset) {
    size_t draws = 0;
    r->cluster_stats.candidates += span.count;
    for (uint32_t i = span.first; i < span.first + span.count; ++i) {
        const af_cluster *c = &mesh->clusters[i];
        af_shadow_bounds bounds = af_shadow_transform_bounds(draw.model, c->minimum, c->maximum);
        if (!r->culling_disabled && !af_shadow_intersects(visibility, &bounds))
            continue;
        if (r->cluster_colors) {
            uint32_t color = (i + color_offset + 1) * UINT32_C(2654435761);
            draw.material.tint = (af_color){.2f + (float)(color & 255u) / 320, .2f + (float)((color >> 8) & 255u) / 320,
                                            .2f + (float)((color >> 16) & 255u) / 320, 1};
            draw.material.params.x = 1;
            draw.material.params.y = (float)((uint32_t)draw.material.params.y | AF_DRAW_CLUSTER_DEBUG);
            draw.material.emission = (af_vec4){0};
            vkCmdPushConstants(frame->command, r->swapchain.pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(draw), &draw);
        }
        vkCmdDrawIndexed(frame->command, c->index_count, 1, c->first_index, 0, object);
        r->cluster_stats.triangles += c->index_count / 3;
        ++draws;
    }
    r->cluster_stats.visible += draws;
    return draws;
}

size_t af_r_draw_clusters(af_renderer *r, af_frame *frame, const af_gpu_mesh *mesh, af_mesh_draw draw,
                            af_mat4 visibility, float pixel_scale, uint32_t object) {
    size_t draws = 0;
    af_mat4 clip = af_mat4_mul(visibility, draw.model);
    float error_scale = cluster_error_scale(draw.model, pixel_scale);
    if (mesh->residency) {
        uint32_t regions = af_cluster_stream_region_count(mesh->residency->stream);
        const af_gpu_mesh *bound = mesh;
        for (uint32_t region = 0; region < regions; ++region) {
            af_cluster_span span;
            const af_gpu_mesh *selected =
                select_cluster_page(r, mesh, region, draw.model, visibility, clip, error_scale, &span);
            if (!selected)
                continue;
            if (selected != bound) {
                af_r_bind_object_mesh(frame, selected, NULL);
                bound = selected;
            }
            r->cluster_stats.coarse_regions += selected == mesh;
            draws += draw_cluster_span(r, frame, selected, draw, visibility, span, object,
                                       selected == mesh ? 0 : region * AF_CLUSTER_REGION_LEAVES);
        }
        return draws;
    }
    uint32_t regions = mesh->cluster_region_count ? mesh->cluster_region_count : 1;
    for (uint32_t region = 0; region < regions; ++region) {
        bool coarse;
        af_cluster_span span = cluster_range(mesh, region, clip, error_scale, !r->cluster_lod_disabled, &coarse);
        r->cluster_stats.coarse_regions += coarse;
        draws += draw_cluster_span(r, frame, mesh, draw, visibility, span, object, 0);
    }
    return draws;
}

size_t af_r_draw_indirect_clusters(af_renderer *r, af_frame *frame, const af_gpu_mesh *mesh, af_cluster_span span) {
    if (!mesh->residency)
        return draw_cluster_commands(r, frame, span);
    size_t calls = 0;
    const af_gpu_mesh *bound = mesh;
    for (uint32_t i = span.first; i < span.first + span.count; ++i) {
        af_cluster_stream_batch batch = r->cluster_stream_batches[i];
        if (batch.mesh != bound) {
            af_r_bind_object_mesh(frame, batch.mesh, NULL);
            bound = batch.mesh;
        }
        calls += draw_cluster_commands(r, frame, batch.commands);
    }
    return calls;
}


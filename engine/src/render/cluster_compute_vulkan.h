/* SPDX-License-Identifier: MPL-2.0 */
/* Included by renderer_vulkan.c to share its frame fences and buffer allocator. */

#define AF_CLUSTER_GPU_LIMIT 65536u

static void destroy_cluster_compute(af_renderer *r) {
    if (r->cluster_pipeline)
        vkDestroyPipeline(r->device, r->cluster_pipeline, NULL);
    if (r->cluster_pipeline_layout)
        vkDestroyPipelineLayout(r->device, r->cluster_pipeline_layout, NULL);
    if (r->cluster_pool)
        vkDestroyDescriptorPool(r->device, r->cluster_pool, NULL);
    if (r->cluster_layout)
        vkDestroyDescriptorSetLayout(r->device, r->cluster_layout, NULL);
    for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
        destroy_buffer(r, &r->frames[i].cluster_input);
        destroy_buffer(r, &r->frames[i].cluster_transforms);
        destroy_buffer(r, &r->frames[i].cluster_output);
    }
    free(r->cluster_candidates);
    free(r->cluster_matrices);
    free(r->cluster_spans);
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
        destroy_buffer(r, &frame->cluster_output);
        if (!upload_buffer(r, NULL, bytes,
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                               VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           &frame->cluster_output))
            return false;
        frame->cluster_output.capacity = bytes;
    }
    if (!stream_buffer(r, &frame->cluster_input, r->cluster_candidates, count * sizeof(*r->cluster_candidates),
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !stream_buffer(r, &frame->cluster_transforms, r->cluster_matrices, objects * sizeof(*r->cluster_matrices),
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
    void *mapped = NULL;
    if (vkMapMemory(r->device, frame->cluster_output.memory, 0, VK_WHOLE_SIZE, 0, &mapped) == VK_SUCCESS) {
        VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                     .memory = frame->cluster_output.memory,
                                     .size = VK_WHOLE_SIZE};
        if (vkInvalidateMappedMemoryRanges(r->device, 1, &range) == VK_SUCCESS) {
            const VkDrawIndexedIndirectCommand *counts = mapped;
            r->cluster_gpu_visible = counts[frame->cluster_submitted].instanceCount;
            r->cluster_gpu_triangles = counts[frame->cluster_submitted].indexCount;
        }
        vkUnmapMemory(r->device, frame->cluster_output.memory);
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

static bool prepare_cluster_commands(af_renderer *r, af_frame *frame, const af_scene *scene, af_mat4 projection,
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
    uint32_t count = 0, coarse_regions = 0;
    for (size_t i = 0; i < objects; ++i) {
        const af_scene_object *o = af_scene_object_at(scene, i);
        if (o->material.alpha_mode != AF_ALPHA_OPAQUE || !af_scene_object_visible(scene, o->id) ||
            af_scene_get_water(scene, o->id) || !strcmp(o->mesh, "__node"))
            continue;
        size_t index = find_mesh_index(r, o->mesh);
        if (index == r->mesh_count)
            continue;
        const af_gpu_mesh *mesh = &r->meshes[index].gpu;
        af_mat4 world;
        if (!mesh->cluster_count || mesh->cluster_count > AF_CLUSTER_GPU_LIMIT - count ||
            !object_world(r, scene, i, &world))
            continue;
        af_shadow_bounds bounds = af_shadow_transform_bounds(world, mesh->minimum, mesh->maximum);
        if (!r->culling_disabled && !af_shadow_intersects(projection, &bounds))
            continue;
        af_mat4 clip = af_mat4_mul(projection, world);
        r->cluster_matrices[i] = clip;
        uint32_t first = count;
        float error_scale = cluster_error_scale(world, pixel_scale);
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
    r->cluster_stats.coarse_regions = coarse_regions;
    r->cluster_stats.candidates = count;
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

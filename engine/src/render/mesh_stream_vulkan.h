/* SPDX-License-Identifier: MPL-2.0 */
/* Included inside renderer_vulkan.c: dynamic buffers are owned by one frame's
   fence. Retired slots remain stable until every frame has released its buffers. */
static void stream_destroy(af_renderer *r, af_mesh_asset *a) {
    destroy_mesh(r, &a->gpu);
    for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
        destroy_mesh(r, &a->streams[f].gpu);
    free(a->cpu_vertices);
    free(a->cpu_indices);
    *a = (af_mesh_asset){0};
}
static bool stream_buffer(af_renderer *r, af_gpu_buffer *b, const void *data, VkDeviceSize bytes,
                          VkBufferUsageFlags usage) {
    if (b->capacity < bytes) {
        destroy_buffer(r, b);
        VkDeviceSize size = 4096;
        while (size < bytes)
            size *= 2;
        VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                                   .size = size,
                                   .usage = usage,
                                   .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
        if (!vk_ok(vkCreateBuffer(r->device, &info, NULL, &b->handle), "create streamed buffer"))
            return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(r->device, b->handle, &req);
        b->allocation_bytes = req.size;
        VkPhysicalDeviceMemoryProperties props;
        vkGetPhysicalDeviceMemoryProperties(r->physical_device, &props);
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
            VkMemoryPropertyFlags flags = props.memoryTypes[i].propertyFlags;
            if (!(req.memoryTypeBits & (1u << i)) || !(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
                continue;
            type = i;
            if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                break;
        }
        if (type == UINT32_MAX)
            return false;
        VkMemoryAllocateInfo allocation = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = req.size, .memoryTypeIndex = type};
        if (!vk_ok(vkAllocateMemory(r->device, &allocation, NULL, &b->memory), "allocate streamed memory") ||
            !vk_ok(vkBindBufferMemory(r->device, b->handle, b->memory, 0), "bind streamed buffer") ||
            !vk_ok(vkMapMemory(r->device, b->memory, 0, VK_WHOLE_SIZE, 0, &b->mapped), "map streamed buffer"))
            return false;
        b->capacity = size;
        b->coherent = (props.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
    memcpy(b->mapped, data, (size_t)bytes);
    if (!b->coherent) {
        VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, .memory = b->memory, .size = VK_WHOLE_SIZE};
        if (!vk_ok(vkFlushMappedMemoryRanges(r->device, 1, &range), "flush streamed buffer"))
            return false;
    }
    return true;
}
static bool stream_set(af_renderer *r, const char *name, const af_mesh_data *m) {
    if (!m || !m->vertex_count || !m->index_count)
        return false;
    size_t i = find_mesh_index(r, name);
    if (i == r->mesh_count) {
        for (size_t k = 0; k < r->mesh_count; ++k)
            if (!r->meshes[k].name[0]) {
                i = k;
                break;
            }
    }
    if (i == r->mesh_count && r->mesh_count == r->mesh_capacity) {
        size_t capacity = r->mesh_capacity ? r->mesh_capacity * 2 : 8;
        af_mesh_asset *assets = realloc(r->meshes, capacity * sizeof(*assets));
        if (!assets)
            return false;
        r->meshes = assets;
        r->mesh_capacity = capacity;
    }
    af_vertex *v = malloc((size_t)m->vertex_count * sizeof(*v));
    uint32_t *ix = malloc((size_t)m->index_count * sizeof(*ix));
    if (!v || !ix) {
        free(v);
        free(ix);
        return false;
    }
    memcpy(v, m->vertices, (size_t)m->vertex_count * sizeof(*v));
    memcpy(ix, m->indices, (size_t)m->index_count * sizeof(*ix));
    if (i == r->mesh_count) {
        r->meshes[i] = (af_mesh_asset){0};
        ++r->mesh_count;
    }
    af_mesh_asset *a = &r->meshes[i];
    if (a->name[0] && !a->dynamic) {
        free(v);
        free(ix);
        return false;
    }
    snprintf(a->name, sizeof(a->name), "%s", name);
    free(a->cpu_vertices);
    free(a->cpu_indices);
    a->cpu_vertices = v;
    a->cpu_indices = ix;
    a->vertex_count = m->vertex_count;
    a->gpu.index_count = m->index_count;
    a->dynamic = true;
    ++a->revision;
    a->gpu.minimum = a->gpu.maximum = v[0].position;
    for (uint32_t k = 0; k < m->vertex_count; ++k) {
        af_vec3 p = v[k].position;
        a->gpu.minimum =
            (af_vec3){fminf(a->gpu.minimum.x, p.x), fminf(a->gpu.minimum.y, p.y), fminf(a->gpu.minimum.z, p.z)};
        a->gpu.maximum =
            (af_vec3){fmaxf(a->gpu.maximum.x, p.x), fmaxf(a->gpu.maximum.y, p.y), fmaxf(a->gpu.maximum.z, p.z)};
    }
    return true;
}
static bool stream_prepare(af_renderer *r) {
    Uint64 start = SDL_GetPerformanceCounter();
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_mesh_asset *a = &r->meshes[i];
        if (!a->dynamic)
            continue;
        af_stream_frame *f = &a->streams[r->frame_index];
        if (a->retired) {
            destroy_mesh(r, &f->gpu);
            bool pending = false;
            for (unsigned k = 0; k < AF_FRAMES_IN_FLIGHT; ++k)
                pending |= a->streams[k].gpu.vertices.handle != VK_NULL_HANDLE ||
                           a->streams[k].gpu.indices.handle != VK_NULL_HANDLE;
            if (!pending)
                stream_destroy(r, a);
            continue;
        }
        if (f->revision == a->revision)
            continue;
        VkDeviceSize vb = (VkDeviceSize)a->vertex_count * sizeof(af_vertex),
                     ib = (VkDeviceSize)a->gpu.index_count * sizeof(uint32_t);
        if (!stream_buffer(r, &f->gpu.vertices, a->cpu_vertices, vb, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !stream_buffer(r, &f->gpu.indices, a->cpu_indices, ib, VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
            return false;
        f->gpu.index_count = a->gpu.index_count;
        f->gpu.minimum = a->gpu.minimum;
        f->gpu.maximum = a->gpu.maximum;
        f->revision = a->revision;
        r->water_upload_bytes += (uint64_t)(vb + ib);
        ++r->water_stream_uploads;
    }
    r->water_upload_ms = (double)(SDL_GetPerformanceCounter() - start) * 1000 / (double)SDL_GetPerformanceFrequency();
    return true;
}

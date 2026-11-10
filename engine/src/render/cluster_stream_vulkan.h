/* SPDX-License-Identifier: MPL-2.0 */
/* Included by renderer_vulkan.c to share buffer ownership and frame fences. */

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
    destroy_mesh(r, &page->gpu);
    *page = (af_cached_cluster_page){0};
    ++r->page_evictions;
    return bytes;
}

static void release_pending_cluster_page(af_renderer *r, af_pending_cluster_page *pending) {
    af_cluster_free(&pending->mesh);
    destroy_mesh(r, &pending->gpu);
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
        !create_upload_buffer(r, (VkDeviceSize)p->mesh.mesh.vertex_count * sizeof(af_vertex),
                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &p->gpu.vertices, &p->vertices) ||
        !create_upload_buffer(r, (VkDeviceSize)p->mesh.mesh.index_count * sizeof(uint32_t),
                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &p->gpu.indices, &p->indices)) {
        fail_cluster_page(r, cache);
        return false;
    }
    cache->page_requirements[p->region] = p->vertices.size + p->indices.size;
    return true;
}

static void update_cluster_pages(af_renderer *r) {
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
        if (!fill_upload_buffer(r, data->vertices, (VkDeviceSize)data->vertex_count * sizeof(af_vertex),
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &p->gpu.vertices, p->vertices) ||
            !fill_upload_buffer(r, data->indices, (VkDeviceSize)data->index_count * sizeof(uint32_t),
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &p->gpu.indices, p->indices)) {
            fail_cluster_page(r, cache);
            continue;
        }
        p->gpu.vertex_count = data->vertex_count;
        p->gpu.index_count = data->index_count;
        p->gpu.clusters = p->mesh.clusters;
        p->gpu.cluster_count = p->mesh.count;
        p->mesh.clusters = NULL;
        *slot = (af_cached_cluster_page){
            .gpu = p->gpu, .region = p->region, .frame_mask = frame, .last_used = r->rendered_frames};
        p->gpu = (af_gpu_mesh){0};
        release_pending_cluster_page(r, p);
        resident += bytes;
        ++r->page_uploads;
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

static const af_gpu_mesh *select_cluster_page(af_renderer *r, const af_gpu_mesh *base, uint32_t region_index,
                                              af_mat4 model, af_mat4 projection, af_mat4 clip, float error_scale,
                                              af_cluster_span *span) {
    af_cluster_residency *cache = base->residency;
    const af_cluster_stream_region *region = af_cluster_stream_region_at(cache->stream, region_index);
    af_shadow_bounds bounds = af_shadow_transform_bounds(model, region->minimum, region->maximum);
    if (!r->culling_disabled && !af_shadow_intersects(projection, &bounds))
        return NULL;
    uint64_t bytes = cache->page_requirements[region_index];
    if (!bytes)
        bytes = (uint64_t)region->page_vertices * sizeof(af_vertex) + (uint64_t)region->page_indices * sizeof(uint32_t);
    if (region->page_clusters && bytes != UINT64_MAX && bytes <= r->page_budget &&
        !cluster_wants_coarse(clip, region->minimum, region->maximum, region->error * error_scale,
                              !r->cluster_lod_disabled)) {
        for (unsigned i = 0; i < AF_CLUSTER_RESIDENT_PAGES; ++i) {
            af_cached_cluster_page *page = &cache->pages[i];
            if (page->gpu.cluster_count && page->region == region_index) {
                page->frame_mask |= 1u << r->frame_index;
                page->last_used = r->rendered_frames;
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

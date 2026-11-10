/* SPDX-License-Identifier: MPL-2.0 */
/* Included by renderer_vulkan.c to share buffer ownership and frame fences. */

static void update_cluster_pages(af_renderer *r) {
    uint32_t frame = 1u << r->frame_index;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache)
            continue;
        af_cached_cluster_page *slot = NULL;
        for (unsigned j = 0; j < AF_CLUSTER_RESIDENT_PAGES; ++j) {
            af_cached_cluster_page *page = &cache->pages[j];
            page->frame_mask &= ~frame;
            if (!page->frame_mask &&
                (!slot || (slot->gpu.cluster_count && (!page->gpu.cluster_count || page->last_used < slot->last_used))))
                slot = page;
        }
        uint32_t region;
        af_cluster_mesh page;
        if (!slot || !af_cluster_take_page(cache->stream, &region, &page))
            continue;
        af_gpu_mesh gpu = {0};
        bool ok = page.count &&
                  upload_buffer(r, page.mesh.vertices, (VkDeviceSize)page.mesh.vertex_count * sizeof(af_vertex),
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &gpu.vertices) &&
                  upload_buffer(r, page.mesh.indices, (VkDeviceSize)page.mesh.index_count * sizeof(uint32_t),
                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &gpu.indices);
        if (ok) {
            gpu.clusters = page.clusters;
            gpu.cluster_count = page.count;
            page.clusters = NULL;
            r->page_evictions += slot->gpu.cluster_count != 0;
            ++r->page_uploads;
            destroy_mesh(r, &slot->gpu);
            *slot = (af_cached_cluster_page){.gpu = gpu, .region = region, .last_used = r->rendered_frames};
        } else {
            cache->failed[region] = true;
            ++r->page_failures;
            destroy_mesh(r, &gpu);
        }
        af_cluster_free(&page);
    }
}

static const af_gpu_mesh *select_cluster_page(af_renderer *r, const af_gpu_mesh *base, uint32_t region_index,
                                              af_mat4 model, af_mat4 projection, af_mat4 clip, float error_scale,
                                              af_cluster_span *span) {
    af_cluster_residency *cache = base->residency;
    const af_cluster_stream_region *region = af_cluster_stream_region_at(cache->stream, region_index);
    af_shadow_bounds bounds = af_shadow_transform_bounds(model, region->minimum, region->maximum);
    if (!r->culling_disabled && !af_shadow_intersects(projection, &bounds))
        return NULL;
    if (region->page_clusters && !cache->failed[region_index] &&
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
        (void)af_cluster_request_page(cache->stream, region_index);
    }
    *span = (af_cluster_span){region->coarse_first, region->coarse_count};
    return base;
}

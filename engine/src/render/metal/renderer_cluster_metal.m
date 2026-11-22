/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"
#include <unistd.h>

#define AF_MTL_CLUSTER_GPU_LIMIT 65536u

static uint64_t estimate_mtl_buffer_bytes(id<MTLDevice> device, uint64_t size) {
    MTLSizeAndAlign sa = [device heapBufferSizeAndAlignWithLength:(NSUInteger)size options:MTLResourceStorageModeShared];
    long ps = sysconf(_SC_PAGESIZE);
    uint64_t page = ps > 0 ? (uint64_t)ps : 16384u;
    uint64_t align = sa.align > page ? (uint64_t)sa.align : page;
    uint64_t base = sa.size > size ? (uint64_t)sa.size : size;
    return (base + align - 1u) & ~(align - 1u);
}

void af_mtl_r_set_cluster_rendering(af_mtl_renderer *r, bool enabled, bool debug_colors) {
    r->clusters_disabled = !enabled;
    r->cluster_colors = debug_colors;
}

void af_mtl_r_set_cluster_lod(af_mtl_renderer *r, bool enabled) {
    r->cluster_lod_disabled = !enabled;
}

void af_mtl_r_set_cluster_gpu_culling(af_mtl_renderer *r, bool enabled) {
    r->cluster_gpu_disabled = !enabled;
}

af_cluster_stats af_mtl_r_cluster_stats(const af_mtl_renderer *r) {
    af_cluster_stats stats = r->cluster_stats;
    stats.gpu_supported = r->cluster_gpu_supported;
    stats.page_uploads = r->page_uploads;
    stats.page_evictions = r->page_evictions;
    stats.page_failures = r->page_failures;
    stats.page_budget = r->page_budget;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        const af_mtl_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache)
            continue;
        stats.queued_requests += af_cluster_request_depth(cache->stream);
        uint32_t peak = af_cluster_request_peak(cache->stream);
        if (peak > stats.peak_asset_requests)
            stats.peak_asset_requests = peak;
        for (unsigned p = 0; p < AF_CLUSTER_RESIDENT_PAGES; ++p) {
            const af_mtl_gpu_mesh *gpu = &cache->pages[p].gpu;
            stats.resident_pages += gpu->cluster_count != 0;
            stats.page_bytes += gpu->vertices.allocation_bytes + gpu->indices.allocation_bytes;
        }
    }
    return stats;
}

static uint64_t cluster_page_bytes(const af_mtl_cached_cluster_page *page) {
    return page->gpu.vertices.allocation_bytes + page->gpu.indices.allocation_bytes;
}

static af_mtl_cached_cluster_page *oldest_cluster_page(af_mtl_cluster_residency *cache,
                                                       af_mtl_cached_cluster_page *oldest) {
    for (unsigned i = 0; i < AF_CLUSTER_RESIDENT_PAGES; ++i) {
        af_mtl_cached_cluster_page *page = &cache->pages[i];
        if (page->gpu.cluster_count && !page->frame_mask && (!oldest || page->last_used < oldest->last_used))
            oldest = page;
    }
    return oldest;
}

static af_mtl_cached_cluster_page *oldest_cluster_page_global(af_mtl_renderer *r) {
    af_mtl_cached_cluster_page *oldest = NULL;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_mtl_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (cache)
            oldest = oldest_cluster_page(cache, oldest);
    }
    return oldest;
}

static uint64_t evict_cluster_page(af_mtl_renderer *r, af_mtl_cached_cluster_page *page) {
    uint64_t bytes = cluster_page_bytes(page);
    af_mtl_destroy_mesh(&page->gpu);
    *page = (af_mtl_cached_cluster_page){0};
    ++r->page_evictions;
    return bytes;
}

static void release_pending_cluster_page(af_mtl_pending_cluster_page *pending) {
    af_cluster_free(&pending->mesh);
    *pending = (af_mtl_pending_cluster_page){0};
}

static void fail_cluster_page(af_mtl_renderer *r, af_mtl_cluster_residency *cache) {
    cache->page_requirements[cache->pending.region] = UINT64_MAX;
    ++r->page_failures;
    release_pending_cluster_page(&cache->pending);
}

static bool stage_cluster_page(af_mtl_renderer *r, af_mtl_cluster_residency *cache) {
    af_mtl_pending_cluster_page *p = &cache->pending;
    if (p->mesh.count && r->rendered_frames - p->last_wanted > 2)
        release_pending_cluster_page(p);
    if (p->mesh.count)
        return true;
    if (!af_cluster_take_page(cache->stream, &p->region, &p->mesh))
        return false;
    p->last_wanted = r->rendered_frames;
    if (!p->mesh.count) {
        fail_cluster_page(r, cache);
        return false;
    }
    p->vertex_bytes = (uint64_t)p->mesh.mesh.vertex_count * sizeof(af_vertex);
    p->index_bytes = (uint64_t)p->mesh.mesh.index_count * sizeof(uint32_t);
    cache->page_requirements[p->region] =
        estimate_mtl_buffer_bytes(r->device, p->vertex_bytes) + estimate_mtl_buffer_bytes(r->device, p->index_bytes);
    return true;
}

void af_mtl_update_cluster_pages(af_mtl_renderer *r) {
    uint32_t frame = 1u << r->frame_index;
    uint64_t resident = 0, reclaimable = 0;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_mtl_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache)
            continue;
        af_cluster_prune_requests(cache->stream, r->rendered_frames);
        for (unsigned j = 0; j < AF_CLUSTER_RESIDENT_PAGES; ++j) {
            af_mtl_cached_cluster_page *page = &cache->pages[j];
            page->frame_mask &= ~frame;
            uint64_t bytes = cluster_page_bytes(page);
            resident += bytes;
            if (!page->frame_mask)
                reclaimable += bytes;
        }
    }
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_mtl_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache || !stage_cluster_page(r, cache))
            continue;
        af_mtl_pending_cluster_page *p = &cache->pending;
        uint64_t bytes = cache->page_requirements[p->region];
        if (bytes > r->page_budget) {
            release_pending_cluster_page(p);
            continue;
        }
        if (bytes > r->page_budget - (resident - reclaimable))
            continue;
        af_mtl_cached_cluster_page *slot = NULL;
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
            af_mtl_cached_cluster_page *victim = oldest_cluster_page_global(r);
            if (!victim)
                break;
            uint64_t removed = evict_cluster_page(r, victim);
            resident -= removed;
            reclaimable -= removed;
        }
        if (bytes > r->page_budget - resident)
            continue;
        af_mesh_data *data = &p->mesh.mesh;
        af_mtl_gpu_mesh gpu = {0};
        if (!af_mtl_create_buffer(r, data->vertices, p->vertex_bytes, &gpu.vertices) ||
            !af_mtl_create_buffer(r, data->indices, p->index_bytes, &gpu.indices)) {
            af_mtl_destroy_mesh(&gpu);
            fail_cluster_page(r, cache);
            continue;
        }
        uint64_t actual = gpu.vertices.allocation_bytes + gpu.indices.allocation_bytes;
        cache->page_requirements[p->region] = actual;
        if (actual > r->page_budget) {
            af_mtl_destroy_mesh(&gpu);
            release_pending_cluster_page(p);
            continue;
        }
        while (actual > r->page_budget - resident) {
            af_mtl_cached_cluster_page *victim = oldest_cluster_page_global(r);
            if (!victim)
                break;
            uint64_t removed = evict_cluster_page(r, victim);
            resident -= removed;
            reclaimable -= removed;
        }
        if (actual > r->page_budget - resident) {
            af_mtl_destroy_mesh(&gpu);
            continue;
        }
        gpu.vertex_count = data->vertex_count;
        gpu.index_count = data->index_count;
        gpu.clusters = p->mesh.clusters;
        gpu.cluster_count = p->mesh.count;
        p->mesh.clusters = NULL;
        *slot = (af_mtl_cached_cluster_page){.gpu = gpu,
                                             .region = p->region,
                                             .last_used = r->rendered_frames,
                                             .generation = ++r->page_uploads};
        resident += actual;
        reclaimable += actual;
        release_pending_cluster_page(p);
    }
}

bool af_mtl_r_set_cluster_cache_budget(af_mtl_renderer *r, uint64_t bytes) {
    if (!r || bytes > UINT64_C(4) * 1024 * 1024 * 1024)
        return false;
    r->page_budget = bytes;
    if (!af_mtl_r_wait_idle(r))
        return false;
    uint64_t resident = 0;
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_mtl_cluster_residency *cache = r->meshes[i].gpu.residency;
        if (!cache)
            continue;
        for (unsigned j = 0; j < AF_CLUSTER_RESIDENT_PAGES; ++j) {
            cache->pages[j].frame_mask = 0;
            resident += cluster_page_bytes(&cache->pages[j]);
        }
    }
    while (resident > bytes) {
        af_mtl_cached_cluster_page *oldest = oldest_cluster_page_global(r);
        if (!oldest)
            break;
        resident -= evict_cluster_page(r, oldest);
    }
    return true;
}

static af_mtl_cached_cluster_page *find_cluster_page(af_mtl_cluster_residency *cache, uint32_t region) {
    for (unsigned i = 0; i < AF_CLUSTER_RESIDENT_PAGES; ++i)
        if (cache->pages[i].gpu.cluster_count && cache->pages[i].region == region)
            return &cache->pages[i];
    return NULL;
}

void af_mtl_use_cluster_page(af_mtl_renderer *r, af_mtl_cached_cluster_page *page) {
    page->last_used = r->rendered_frames;
    page->frame_mask |= 1u << r->frame_index;
}

static af_cluster_span cluster_range(const af_mtl_gpu_mesh *mesh, uint32_t region, af_mat4 clip, float error_scale,
                                     bool lod, bool *coarse) {
    *coarse = false;
    if (!mesh->cluster_region_count)
        return (af_cluster_span){0, mesh->cluster_count};
    const af_cluster_region *group = &mesh->cluster_regions[region];
    if (group->coarse_count &&
        af_rc_cluster_wants_coarse(clip, group->minimum, group->maximum, group->error * error_scale, lod)) {
        *coarse = true;
        return (af_cluster_span){group->coarse_first, group->coarse_count};
    }
    return (af_cluster_span){group->fine_first, group->fine_count};
}

static const af_mtl_gpu_mesh *select_cluster_page(af_mtl_renderer *r, const af_mtl_gpu_mesh *mesh, uint32_t region,
                                                  af_mat4 world, af_mat4 projection, af_mat4 clip, float error_scale,
                                                  af_cluster_span *span) {
    af_mtl_cluster_residency *cache = mesh->residency;
    const af_cluster_stream_region *info = af_cluster_stream_region_at(cache->stream, region);
    af_shadow_bounds bounds = af_shadow_transform_bounds(world, info->minimum, info->maximum);
    if (!r->culling_disabled && !af_shadow_intersects(projection, &bounds))
        return NULL;
    uint64_t bytes = cache->page_requirements[region];
    if (!bytes)
        bytes = (uint64_t)info->page_vertices * sizeof(af_vertex) + (uint64_t)info->page_indices * sizeof(uint32_t);
    bool want_fine = info->page_clusters > 0 && !af_rc_cluster_wants_coarse(clip, info->minimum, info->maximum,
                                                                            info->error * error_scale,
                                                                            !r->cluster_lod_disabled);
    if (want_fine && bytes != UINT64_MAX && bytes <= r->page_budget) {
        af_mtl_cached_cluster_page *page = find_cluster_page(cache, region);
        if (page) {
            af_mtl_use_cluster_page(r, page);
            *span = (af_cluster_span){0, page->gpu.cluster_count};
            return &page->gpu;
        }
        af_vec3 center = {(info->minimum.x + info->maximum.x) * .5f, (info->minimum.y + info->maximum.y) * .5f,
                          (info->minimum.z + info->maximum.z) * .5f};
        af_vec4 q = af_mat4_transform(clip, (af_vec4){center.x, center.y, center.z, 1});
        if (cache->pending.mesh.count && cache->pending.region == region)
            cache->pending.last_wanted = r->rendered_frames;
        else
            af_cluster_request_page(cache->stream, region, r->rendered_frames, fmaxf(q.w, 0.f));
    }
    *span = (af_cluster_span){info->coarse_first, info->coarse_count};
    return mesh;
}

size_t af_mtl_cluster_index_ranges(const af_mtl_renderer *r, const af_mtl_gpu_mesh *base, af_mat4 model,
                                   af_cluster_view view, af_mtl_cluster_index_range *ranges) {
    if (!base->residency)
        return 0;
    af_mtl_cluster_residency *cache = base->residency;
    uint32_t regions = af_cluster_stream_region_count(cache->stream);
    af_mat4 clip = af_mat4_mul(view.projection, model);
    float error_scale = af_rc_cluster_error_scale(model, view.pixel_scale);
    size_t count = 0;
    bool has_fine = false;
    for (uint32_t region = 0; region < regions; ++region) {
        const af_cluster_stream_region *info = af_cluster_stream_region_at(cache->stream, region);
        af_mtl_cached_cluster_page *page = NULL;
        uint64_t bytes = cache->page_requirements[region];
        if (!bytes)
            bytes = (uint64_t)info->page_vertices * sizeof(af_vertex) + (uint64_t)info->page_indices * sizeof(uint32_t);
        if (info->page_clusters > 0 && bytes != UINT64_MAX && bytes <= r->page_budget && !r->clusters_disabled) {
            af_shadow_bounds bounds = af_shadow_transform_bounds(model, info->minimum, info->maximum);
            if ((r->culling_disabled || af_shadow_intersects(view.projection, &bounds)) &&
                !af_rc_cluster_wants_coarse(clip, info->minimum, info->maximum, info->error * error_scale,
                                            !r->cluster_lod_disabled))
                page = find_cluster_page(cache, region);
        }
        if (page) {
            has_fine = true;
            ranges[count++] = (af_mtl_cluster_index_range){0, page->gpu.index_count, page};
            continue;
        }
        if (!info->coarse_count)
            continue;
        uint32_t first = base->clusters[info->coarse_first].first_index;
        af_cluster last = base->clusters[info->coarse_first + info->coarse_count - 1];
        uint32_t indices = last.first_index + last.index_count - first;
        if (count && !ranges[count - 1].page && ranges[count - 1].first + ranges[count - 1].count == first)
            ranges[count - 1].count += indices;
        else
            ranges[count++] = (af_mtl_cluster_index_range){first, indices, NULL};
    }
    return has_fine ? count : 0;
}

void af_mtl_destroy_cluster_compute(af_mtl_renderer *r) {
    for (unsigned i = 0; i < AF_FRAMES_IN_FLIGHT; ++i) {
        af_mtl_destroy_buffer(&r->frames[i].cluster_input);
        af_mtl_destroy_buffer(&r->frames[i].cluster_transforms);
        af_mtl_destroy_buffer(&r->frames[i].cluster_stats_buf);
        af_mtl_destroy_buffer(&r->frames[i].cluster_indirect);
    }
    AF_MTL_RELEASE(r->cluster_pipeline);
    free(r->cluster_candidates);
    free(r->cluster_matrices);
    free(r->cluster_spans);
    free(r->cluster_stream_batches);
    r->cluster_candidates = NULL;
    r->cluster_matrices = NULL;
    r->cluster_spans = NULL;
    r->cluster_stream_batches = NULL;
    r->cluster_object_capacity = 0;
}

static bool ensure_cluster_compute(af_mtl_renderer *r, af_mtl_frame *frame, uint32_t count, size_t objects) {
    if (!r->cluster_pipeline) {
        if (!r->cluster_icb_fn)
            return false;
        NSError *err = nil;
        r->cluster_pipeline = [r->device newComputePipelineStateWithFunction:r->cluster_icb_fn error:&err];
        if (!r->cluster_pipeline) {
            af_log("warning", "Metal cluster compute pipeline failed: %s",
                   err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }
    }

    uint64_t in_bytes = (uint64_t)count * sizeof(af_cluster_candidate);
    uint64_t xf_bytes = (uint64_t)objects * sizeof(af_mat4);
    uint64_t draw_bytes = (uint64_t)count * sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
    if (!af_mtl_stream_buffer(r, &frame->cluster_input, r->cluster_candidates, in_bytes) ||
        !af_mtl_stream_buffer(r, &frame->cluster_transforms, r->cluster_matrices, xf_bytes))
        return false;

    uint32_t zero_stats[2] = {0, 0};
    if (!af_mtl_stream_buffer(r, &frame->cluster_stats_buf, zero_stats, sizeof(zero_stats)))
        return false;

    if (frame->cluster_indirect.capacity < draw_bytes || !frame->cluster_indirect.handle) {
        uint64_t cap = 256u * sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
        while (cap < draw_bytes)
            cap *= 2u;
        af_mtl_buffer next = {0};
        if (!af_mtl_create_buffer(r, NULL, cap, &next))
            return false;
        af_mtl_destroy_buffer(&frame->cluster_indirect);
        frame->cluster_indirect = next;
    }
    return true;
}

bool af_mtl_prepare_cluster_commands(af_mtl_renderer *r, af_mtl_frame *frame, id<MTLCommandBuffer> cmd,
                                     const af_scene *scene, af_mat4 projection, float pixel_scale, size_t objects) {
    if (frame->cluster_submitted) {
        const uint32_t *stats = (const uint32_t *)[frame->cluster_stats_buf.handle contents];
        r->cluster_gpu_candidates = frame->cluster_submitted;
        r->cluster_gpu_triangles = stats[0];
        r->cluster_gpu_visible = stats[1];
        r->cluster_gpu_coarse_regions = frame->cluster_coarse_regions;
        frame->cluster_submitted = frame->cluster_coarse_regions = 0;
    }

    if (!r->cluster_gpu_supported || r->cluster_gpu_disabled || r->clusters_disabled || r->cluster_colors || !objects)
        return false;

    bool has_clusters = false;
    for (size_t i = 0; i < r->mesh_count; ++i)
        if (!r->meshes[i].retired && r->meshes[i].gpu.cluster_count) {
            has_clusters = true;
            break;
        }
    if (!has_clusters)
        return false;

    if (!r->cluster_candidates) {
        r->cluster_candidates = malloc(AF_MTL_CLUSTER_GPU_LIMIT * sizeof(*r->cluster_candidates));
        if (!r->cluster_candidates)
            return false;
    }
    if (r->cluster_object_capacity < objects) {
        size_t cap = r->cluster_object_capacity ? r->cluster_object_capacity : 16u;
        while (cap < objects)
            cap *= 2u;
        af_mat4 *m = realloc(r->cluster_matrices, cap * sizeof(*m));
        af_cluster_span *s = realloc(r->cluster_spans, cap * sizeof(*s));
        if (m)
            r->cluster_matrices = m;
        if (s)
            r->cluster_spans = s;
        if (!m || !s)
            return false;
        r->cluster_object_capacity = cap;
    }

    memset(r->cluster_spans, 0, objects * sizeof(*r->cluster_spans));
    memset(r->cluster_matrices, 0, objects * sizeof(*r->cluster_matrices));
    uint32_t count = 0, coarse_regions = 0, batches = 0;

    for (size_t i = 0; i < objects; ++i) {
        const af_scene_object *o = af_scene_object_at(scene, i);
        if (o->material.alpha_mode != AF_ALPHA_OPAQUE || !af_scene_object_visible(scene, o->id) ||
            af_scene_get_water(scene, o->id) || !strcmp(o->mesh, "__node"))
            continue;
        size_t index = af_mtl_find_mesh_index(r, o->mesh);
        if (index == r->mesh_count)
            continue;
        const af_mtl_gpu_mesh *mesh = &r->meshes[index].gpu;
        af_mat4 world;
        if (!mesh->cluster_count || (!mesh->residency && mesh->cluster_count > AF_MTL_CLUSTER_GPU_LIMIT - count) ||
            !af_mtl_object_world(r, scene, i, &world))
            continue;
        af_shadow_bounds bounds = af_shadow_transform_bounds(world, mesh->minimum, mesh->maximum);
        if (!r->culling_disabled && !af_shadow_intersects(projection, &bounds))
            continue;
        af_mat4 clip = af_mat4_mul(projection, world);
        r->cluster_matrices[i] = clip;
        uint32_t first = count;
        float error_scale = af_rc_cluster_error_scale(world, pixel_scale);

        if (mesh->residency) {
            if (!r->cluster_stream_batches) {
                r->cluster_stream_batches = malloc(AF_MTL_CLUSTER_GPU_LIMIT * sizeof(*r->cluster_stream_batches));
                if (!r->cluster_stream_batches)
                    continue;
            }
            uint32_t batch_start = batches, coarse = 0;
            uint32_t regions = af_cluster_stream_region_count(mesh->residency->stream);
            for (uint32_t region = 0; region < regions; ++region) {
                af_cluster_span span;
                const af_mtl_gpu_mesh *selected =
                    select_cluster_page(r, mesh, region, world, projection, clip, error_scale, &span);
                if (!selected)
                    continue;
                if (span.count > AF_MTL_CLUSTER_GPU_LIMIT - count) {
                    count = first;
                    batches = batch_start;
                    coarse = 0;
                    break;
                }
                af_mtl_cluster_stream_batch *last =
                    batches > batch_start ? &r->cluster_stream_batches[batches - 1] : NULL;
                if (last && last->mesh == selected)
                    last->commands.count += span.count;
                else
                    r->cluster_stream_batches[batches++] = (af_mtl_cluster_stream_batch){selected, {count, span.count}};
                coarse += (selected == mesh);
                for (uint32_t c = span.first; c < span.first + span.count; ++c)
                    r->cluster_candidates[count++] = (af_cluster_candidate){selected->clusters[c], {(uint32_t)i}};
            }
            coarse_regions += coarse;
            r->cluster_spans[i] = (af_cluster_span){batch_start, batches - batch_start};
            continue;
        }

        uint32_t regions = mesh->cluster_region_count ? mesh->cluster_region_count : 1u;
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

    if (!ensure_cluster_compute(r, frame, count, objects)) {
        af_log("warning", "Metal cluster compute unavailable. Using CPU culling.");
        r->cluster_gpu_supported = false;
        return false;
    }

    id<MTLComputeCommandEncoder> comp = [cmd computeCommandEncoder];
    [comp setComputePipelineState:r->cluster_pipeline];
    struct {
        uint32_t count, cull, pad0, pad1;
    } push = {count, r->culling_disabled ? 0u : 1u, 0u, 0u};
    [comp setBytes:&push length:sizeof(push) atIndex:AF_MTL_CLUSTER_BUF_DISPATCH];
    [comp setBuffer:frame->cluster_input.handle offset:0 atIndex:AF_MTL_CLUSTER_BUF_CANDIDATES];
    [comp setBuffer:frame->cluster_transforms.handle offset:0 atIndex:AF_MTL_CLUSTER_BUF_TRANSFORMS];
    [comp setBuffer:frame->cluster_stats_buf.handle offset:0 atIndex:AF_MTL_CLUSTER_BUF_STATS];
    [comp setBuffer:frame->cluster_indirect.handle offset:0 atIndex:AF_MTL_CLUSTER_BUF_ICB_ARGS];
    MTLSize grid = MTLSizeMake((count + 63u) / 64u, 1, 1);
    MTLSize tg = MTLSizeMake(64, 1, 1);
    [comp dispatchThreadgroups:grid threadsPerThreadgroup:tg];
    [comp endEncoding];

    frame->cluster_submitted = count;
    frame->cluster_coarse_regions = coarse_regions;
    r->cluster_stats.gpu_active = true;
    r->cluster_stats.coarse_regions = r->cluster_gpu_coarse_regions;
    r->cluster_stats.candidates = r->cluster_gpu_candidates;
    r->cluster_stats.visible = r->cluster_gpu_visible;
    r->cluster_stats.triangles = r->cluster_gpu_triangles;
    return true;
}

static size_t draw_cluster_span(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, const af_mtl_gpu_mesh *mesh,
                                af_mesh_draw draw, af_mat4 visibility, af_cluster_span span, uint32_t object,
                                uint32_t color_offset) {
    size_t draws = 0;
    r->cluster_stats.candidates += span.count;
    for (uint32_t i = span.first; i < span.first + span.count; ++i) {
        const af_cluster *c = &mesh->clusters[i];
        af_shadow_bounds bounds = af_shadow_transform_bounds(draw.model, c->minimum, c->maximum);
        if (!r->culling_disabled && !af_shadow_intersects(visibility, &bounds))
            continue;
        if (r->cluster_colors) {
            uint32_t color = (i + color_offset + 1) * UINT32_C(2654435761);
            draw.material.tint =
                (af_color){0.2f + (float)(color & 255u) / 320.0f, 0.2f + (float)((color >> 8) & 255u) / 320.0f,
                           0.2f + (float)((color >> 16) & 255u) / 320.0f, 1.0f};
            draw.material.params.x = 1.0f;
            draw.material.params.y = (float)((uint32_t)draw.material.params.y | AF_DRAW_CLUSTER_DEBUG);
            draw.material.emission = (af_vec4){0};
            [enc setVertexBytes:&draw length:sizeof(draw) atIndex:AF_MTL_BUF_PUSH];
            [enc setFragmentBytes:&draw length:sizeof(draw) atIndex:AF_MTL_BUF_PUSH];
        }
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                        indexCount:c->index_count
                         indexType:MTLIndexTypeUInt32
                       indexBuffer:mesh->indices.handle
                 indexBufferOffset:(NSUInteger)c->first_index * sizeof(uint32_t)
                     instanceCount:1
                        baseVertex:0
                      baseInstance:object];
        r->cluster_stats.triangles += c->index_count / 3;
        ++draws;
    }
    r->cluster_stats.visible += draws;
    return draws;
}

size_t af_mtl_draw_clusters(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame,
                            const af_mtl_gpu_mesh *mesh, af_mesh_draw draw, af_mat4 visibility, float pixel_scale,
                            uint32_t object) {
    size_t draws = 0;
    af_mat4 clip = af_mat4_mul(visibility, draw.model);
    float error_scale = af_rc_cluster_error_scale(draw.model, pixel_scale);
    if (mesh->residency) {
        uint32_t regions = af_cluster_stream_region_count(mesh->residency->stream);
        const af_mtl_gpu_mesh *bound = mesh;
        for (uint32_t region = 0; region < regions; ++region) {
            af_cluster_span span;
            const af_mtl_gpu_mesh *selected =
                select_cluster_page(r, mesh, region, draw.model, visibility, clip, error_scale, &span);
            if (!selected)
                continue;
            if (selected != bound) {
                af_mtl_bind_object_mesh(enc, frame, selected, NULL);
                bound = selected;
            }
            r->cluster_stats.coarse_regions += (selected == mesh);
            draws += draw_cluster_span(r, enc, selected, draw, visibility, span, object,
                                       selected == mesh ? 0 : region * AF_CLUSTER_REGION_LEAVES);
        }
        return draws;
    }
    uint32_t regions = mesh->cluster_region_count ? mesh->cluster_region_count : 1u;
    for (uint32_t region = 0; region < regions; ++region) {
        bool coarse;
        af_cluster_span span = cluster_range(mesh, region, clip, error_scale, !r->cluster_lod_disabled, &coarse);
        r->cluster_stats.coarse_regions += coarse;
        draws += draw_cluster_span(r, enc, mesh, draw, visibility, span, object, 0);
    }
    return draws;
}

size_t af_mtl_draw_indirect_clusters(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame,
                                     const af_mtl_gpu_mesh *mesh, af_cluster_span span) {
    const NSUInteger stride = sizeof(MTLDrawIndexedPrimitivesIndirectArguments);
    if (!mesh->residency) {
        if (!span.count)
            return 0;
        for (uint32_t c = 0; c < span.count; ++c) {
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:mesh->indices.handle
                     indexBufferOffset:0
                        indirectBuffer:frame->cluster_indirect.handle
                  indirectBufferOffset:(NSUInteger)(span.first + c) * stride];
        }
        return span.count;
    }
    size_t calls = 0;
    const af_mtl_gpu_mesh *bound = mesh;
    for (uint32_t i = span.first; i < span.first + span.count; ++i) {
        af_mtl_cluster_stream_batch batch = r->cluster_stream_batches[i];
        if (batch.mesh != bound) {
            af_mtl_bind_object_mesh(enc, frame, batch.mesh, NULL);
            bound = batch.mesh;
        }
        for (uint32_t c = 0; c < batch.commands.count; ++c) {
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:batch.mesh->indices.handle
                     indexBufferOffset:0
                        indirectBuffer:frame->cluster_indirect.handle
                  indirectBufferOffset:(NSUInteger)(batch.commands.first + c) * stride];
            ++calls;
        }
    }
    return calls;
}

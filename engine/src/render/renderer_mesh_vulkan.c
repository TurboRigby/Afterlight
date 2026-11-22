/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer_internal.h"
#include "core/jobs.h"

void af_r_destroy_buffer(af_vk_renderer *renderer, af_gpu_buffer *buffer) {
    if (renderer->allocator)
        af_vk_destroy_buffer(renderer->allocator, &buffer->handle, &buffer->alloc);
    else if (buffer->handle)
        vkDestroyBuffer(renderer->device, buffer->handle, NULL);
    *buffer = (af_gpu_buffer){0};
}

void af_r_destroy_mesh(af_vk_renderer *renderer, af_gpu_mesh *mesh) {
    if (mesh->residency) {
        af_cluster_residency *r = mesh->residency;
        af_cluster_close_stream(r->stream);
        for (unsigned i = 0; i < AF_CLUSTER_RESIDENT_PAGES; ++i)
            af_r_destroy_mesh(renderer, &r->pages[i].gpu);
        af_cluster_free(&r->pending.mesh);
        af_r_destroy_mesh(renderer, &r->pending.gpu);
        free(r->page_requirements);
        free(r);
        mesh->residency = NULL;
    }
    af_r_destroy_buffer(renderer, &mesh->vertices);
    af_r_destroy_buffer(renderer, &mesh->indices);
    af_mesh_morph_free(mesh->morph);
    free(mesh->morph_base);
    free(mesh->clusters);
    free(mesh->cluster_regions);
    mesh->cluster_regions = NULL;
    mesh->cluster_region_count = 0;
    mesh->clusters = NULL;
    mesh->cluster_count = 0;
    mesh->morph = NULL;
    mesh->morph_base = NULL;
    mesh->index_count = 0;
}

static bool ray_buffer_usage(const af_vk_renderer *renderer, VkBufferUsageFlags usage) {
    return renderer->ray_supported && (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
}

static bool static_geometry_usage(VkBufferUsageFlags usage) {
    return (usage & (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT)) != 0;
}

bool af_r_create_upload_buffer(af_vk_renderer *renderer, VkDeviceSize size, VkBufferUsageFlags usage,
                                 af_gpu_buffer *buffer, VkMemoryRequirements *requirements) {
    if (static_geometry_usage(usage))
        usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (ray_buffer_usage(renderer, usage))
        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                 VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkBufferCreateInfo info = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                               .size = size,
                               .usage = usage,
                               .sharingMode = VK_SHARING_MODE_EXCLUSIVE};
    if (!af_r_vk_ok(vkCreateBuffer(renderer->device, &info, NULL, &buffer->handle), "vkCreateBuffer"))
        return false;
    vkGetBufferMemoryRequirements(renderer->device, buffer->handle, requirements);
    return true;
}

bool af_r_fill_upload_buffer(af_vk_renderer *renderer, const void *data, VkDeviceSize size, VkBufferUsageFlags usage,
                               af_gpu_buffer *buffer, VkMemoryRequirements requirements) {
    bool bda = ray_buffer_usage(renderer, usage);
    if (static_geometry_usage(usage) && data) {
        const char *force_env = getenv("AFTERLIGHT_FORCE_STAGING_UPLOAD");
        bool force_staging = force_env && force_env[0] && strcmp(force_env, "0") != 0;
        bool use_unified =
            !force_staging &&
            af_vk_allocator_find_type(renderer->allocator, requirements.memoryTypeBits,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != UINT32_MAX;
        if (use_unified) {
            if (!af_vk_allocate(renderer->allocator, requirements, AF_VK_MEM_BUFFER,
                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bda, &buffer->alloc))
                return false;
            buffer->allocation_bytes = requirements.size;
            if (!af_r_vk_ok(vkBindBufferMemory(renderer->device, buffer->handle, buffer->alloc.memory, buffer->alloc.offset),
                       "vkBindBufferMemory"))
                return false;
            memcpy(buffer->alloc.mapped, data, (size_t)size);
            return af_vk_flush(renderer->allocator, &buffer->alloc, 0, size);
        }
        /* Discrete VRAM or forced staging path: allocate DEVICE_LOCAL and transfer via staging buffer. */
        if (!af_vk_allocate(renderer->allocator, requirements, AF_VK_MEM_BUFFER, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                            bda, &buffer->alloc) &&
            !af_vk_allocate(renderer->allocator, requirements, AF_VK_MEM_BUFFER, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                            bda, &buffer->alloc))
            return false;
        buffer->allocation_bytes = requirements.size;
        if (!af_r_vk_ok(vkBindBufferMemory(renderer->device, buffer->handle, buffer->alloc.memory, buffer->alloc.offset),
                   "vkBindBufferMemory"))
            return false;
        VkBuffer staging = VK_NULL_HANDLE;
        af_vk_allocation staging_alloc = {0};
        if (!af_vk_create_buffer(renderer->allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false,
                                 &staging, &staging_alloc))
            return false;
        memcpy(staging_alloc.mapped, data, (size_t)size);
        bool ok = af_vk_flush(renderer->allocator, &staging_alloc, 0, size);
        VkCommandBuffer cmd = ok ? af_vk_begin_transfer(renderer->allocator) : VK_NULL_HANDLE;
        if (cmd) {
            VkBufferCopy copy = {.srcOffset = 0, .dstOffset = 0, .size = size};
            vkCmdCopyBuffer(cmd, staging, buffer->handle, 1, &copy);
            VkBufferMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                             .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                             .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
                                             .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                             .buffer = buffer->handle,
                                             .offset = 0,
                                             .size = size};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 1,
                                 &barrier, 0, NULL);
            ok = af_vk_end_transfer(renderer->allocator, cmd);
        } else {
            ok = false;
        }
        af_vk_destroy_buffer(renderer->allocator, &staging, &staging_alloc);
        return ok;
    }

    if (!af_vk_allocate(renderer->allocator, requirements, AF_VK_MEM_BUFFER, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bda, &buffer->alloc)) {
        af_log("error", "No host-visible memory type for buffer data.");
        return false;
    }
    buffer->allocation_bytes = requirements.size;
    if (!af_r_vk_ok(vkBindBufferMemory(renderer->device, buffer->handle, buffer->alloc.memory, buffer->alloc.offset),
               "vkBindBufferMemory"))
        return false;
    if (!data)
        return true;
    memcpy(buffer->alloc.mapped, data, (size_t)size);
    return af_vk_flush(renderer->allocator, &buffer->alloc, 0, size);
}

bool af_r_upload_buffer(af_vk_renderer *renderer, const void *data, VkDeviceSize size, VkBufferUsageFlags usage,
                          af_gpu_buffer *buffer) {
    VkMemoryRequirements requirements;
    return af_r_create_upload_buffer(renderer, size, usage, buffer, &requirements) &&
           af_r_fill_upload_buffer(renderer, data, size, usage, buffer, requirements);
}

size_t af_r_find_mesh_index(const af_vk_renderer *renderer, const char *name) {
    for (size_t i = 0; i < renderer->mesh_count; ++i)
        if (!renderer->meshes[i].retired && strcmp(renderer->meshes[i].name, name) == 0)
            return i;
    return renderer->mesh_count;
}


void af_r_stream_destroy(af_vk_renderer *r, af_mesh_asset *a) {
    af_r_destroy_mesh(r, &a->gpu);
    for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
        af_r_destroy_mesh(r, &a->streams[f].gpu);
    free(a->cpu_vertices);
    free(a->cpu_indices);
    *a = (af_mesh_asset){0};
}
bool af_r_stream_buffer(af_vk_renderer *r, af_gpu_buffer *b, const void *data, VkDeviceSize bytes,
                          VkBufferUsageFlags usage) {
    if (b->capacity < bytes) {
        af_r_destroy_buffer(r, b);
        VkDeviceSize size = 4096;
        while (size < bytes)
            size *= 2;
        if (!af_vk_create_buffer(r->allocator, size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false, &b->handle, &b->alloc))
            return false;
        b->allocation_bytes = b->alloc.requested_size;
        b->capacity = size;
    }
    memcpy(b->alloc.mapped, data, (size_t)bytes);
    return af_vk_flush(r->allocator, &b->alloc, 0, bytes);
}
static bool stream_set(af_vk_renderer *r, const char *name, const af_mesh_data *m) {
    if (!m || !m->vertex_count || !m->index_count)
        return false;
    size_t i = af_r_find_mesh_index(r, name);
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
bool af_r_stream_prepare(af_vk_renderer *r) {
    Uint64 start = SDL_GetPerformanceCounter();
    for (size_t i = 0; i < r->mesh_count; ++i) {
        af_mesh_asset *a = &r->meshes[i];
        if (!a->dynamic)
            continue;
        af_stream_frame *f = &a->streams[r->frame_index];
        if (a->retired) {
            af_r_destroy_mesh(r, &f->gpu);
            bool pending = false;
            for (unsigned k = 0; k < AF_FRAMES_IN_FLIGHT; ++k)
                pending |= a->streams[k].gpu.vertices.handle != VK_NULL_HANDLE ||
                           a->streams[k].gpu.indices.handle != VK_NULL_HANDLE;
            if (!pending)
                af_r_stream_destroy(r, a);
            continue;
        }
        if (f->revision == a->revision)
            continue;
        VkDeviceSize vb = (VkDeviceSize)a->vertex_count * sizeof(af_vertex),
                     ib = (VkDeviceSize)a->gpu.index_count * sizeof(uint32_t);
        if (!af_r_stream_buffer(r, &f->gpu.vertices, a->cpu_vertices, vb, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) ||
            !af_r_stream_buffer(r, &f->gpu.indices, a->cpu_indices, ib, VK_BUFFER_USAGE_INDEX_BUFFER_BIT))
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

static size_t morph_bytes(const af_mesh_morph *morph, uint32_t expanded_vertices) {
    return morph ? sizeof(*morph) + (size_t)morph->target_count * morph->source_count * sizeof(af_morph_delta) +
                       (size_t)expanded_vertices * (sizeof(af_vertex) + sizeof(uint32_t))
                 : 0;
}

static bool upload_mesh(af_vk_renderer *renderer, const char *name, const af_mesh_data *mesh, af_mesh_morph *morph,
                        af_mesh_sidedness sidedness) {
    if (!name || !name[0] || strlen(name) >= AF_MESH_NAME_CAPACITY || !mesh || !mesh->vertices || !mesh->indices ||
        mesh->vertex_count == 0 || mesh->index_count == 0 || mesh->index_count % 3 != 0 ||
        (uint64_t)mesh->vertex_count * sizeof(af_vertex) > SIZE_MAX ||
        (uint64_t)mesh->index_count * sizeof(uint32_t) > SIZE_MAX) {
        af_log("error", "Mesh requires a valid asset name, vertices, and indexed whole triangles.");
        return false;
    }
    for (uint32_t i = 0; i < mesh->index_count; ++i) {
        if (mesh->indices[i] >= mesh->vertex_count) {
            af_log("error", "Mesh index %u is out of range (vertex count %u).", mesh->indices[i], mesh->vertex_count);
            return false;
        }
    }
    char asset_name[AF_MESH_NAME_CAPACITY];
    memcpy(asset_name, name, strlen(name) + 1);
    size_t index = af_r_find_mesh_index(renderer, asset_name);
    af_gpu_mesh uploaded = {.sidedness = sidedness};
    if (morph) {
        size_t resident = morph_bytes(morph, mesh->vertex_count);
        for (size_t i = 0; i < renderer->mesh_count; ++i)
            if (i != index)
                resident += morph_bytes(renderer->meshes[i].gpu.morph, renderer->meshes[i].gpu.vertex_count);
        if (resident > 256u * 1024u * 1024u) {
            af_log("error", "Resident morph geometry exceeds 256 MiB.");
            return false;
        }
        size_t bytes = mesh->vertex_count * sizeof(*mesh->vertices);
        uploaded.morph_base = malloc(bytes);
        if (!uploaded.morph_base)
            return false;
        memcpy(uploaded.morph_base, mesh->vertices, bytes);
    }
    if (!af_r_upload_buffer(renderer, mesh->vertices, (VkDeviceSize)mesh->vertex_count * sizeof(af_vertex),
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &uploaded.vertices) ||
        !af_r_upload_buffer(renderer, mesh->indices, (VkDeviceSize)mesh->index_count * sizeof(uint32_t),
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &uploaded.indices))
        goto fail;
    uploaded.vertex_count = mesh->vertex_count;
    uploaded.index_count = mesh->index_count;
    uploaded.minimum = uploaded.maximum = mesh->vertices[0].position;
    for (uint32_t i = 0; i < mesh->vertex_count; ++i) {
        const af_vertex *v = &mesh->vertices[i];
        float weights[] = {v->weights.x, v->weights.y, v->weights.z, v->weights.w};
        float sum = 0;
        for (unsigned j = 0; j < 4; ++j) {
            if (!isfinite(weights[j]) || weights[j] < 0 || v->joints[j] >= AF_ANIMATION_MAX_JOINTS)
                goto fail;
            sum += weights[j];
            if (v->joints[j] > uploaded.max_joint)
                uploaded.max_joint = v->joints[j];
        }
        if (sum > 0) {
            if (fabsf(sum - 1) > .001f)
                goto fail;
            uploaded.skinned = true;
        }
        af_vec3 p = mesh->vertices[i].position;
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z))
            goto fail;
        uploaded.minimum.x = fminf(uploaded.minimum.x, p.x);
        uploaded.maximum.x = fmaxf(uploaded.maximum.x, p.x);
        uploaded.minimum.y = fminf(uploaded.minimum.y, p.y);
        uploaded.maximum.y = fmaxf(uploaded.maximum.y, p.y);
        uploaded.minimum.z = fminf(uploaded.minimum.z, p.z);
        uploaded.maximum.z = fmaxf(uploaded.maximum.z, p.z);
    }
    if (index == renderer->mesh_count && renderer->mesh_count == renderer->mesh_capacity) {
        if (renderer->mesh_capacity > SIZE_MAX / 2 / sizeof(*renderer->meshes))
            goto fail;
        size_t capacity = renderer->mesh_capacity ? renderer->mesh_capacity * 2 : 8;
        af_mesh_asset *assets = realloc(renderer->meshes, capacity * sizeof(*assets));
        if (!assets)
            goto fail;
        renderer->meshes = assets;
        renderer->mesh_capacity = capacity;
    }
    ++renderer->mesh_idle_waits;
    if (!af_vk_r_wait_idle(renderer))
        goto fail;
    if (index < renderer->mesh_count)
        af_r_stream_destroy(renderer, &renderer->meshes[index]);
    else
        ++renderer->mesh_count;
    uploaded.morph = morph;
    renderer->meshes[index] = (af_mesh_asset){.gpu = uploaded};
    memcpy(renderer->meshes[index].name, asset_name, strlen(asset_name) + 1);
    ++renderer->mesh_revision;
    return true;
fail:
    af_r_destroy_mesh(renderer, &uploaded);
    return false;
}
bool af_vk_r_upload_mesh(af_vk_renderer *renderer, const char *name, const af_mesh_data *mesh) {
    return upload_mesh(renderer, name, mesh, NULL, AF_MESH_SIDES_DEFAULT);
}
bool af_vk_r_upload_mesh_file(af_vk_renderer *renderer, const char *name, af_mesh_file *mesh,
                                  af_mesh_sidedness sidedness) {
    if (!upload_mesh(renderer, name, &mesh->data, mesh->morph, sidedness))
        return false;
    mesh->morph = NULL;
    return true;
}

bool af_vk_r_upload_clusters(af_vk_renderer *r, const char *name, af_cluster_mesh *mesh) {
    if (!af_vk_r_upload_mesh(r, name, &mesh->mesh))
        return false;
    af_gpu_mesh *gpu = &r->meshes[af_r_find_mesh_index(r, name)].gpu;
    gpu->clusters = mesh->clusters;
    gpu->cluster_count = mesh->count;
    gpu->cluster_fine_count = mesh->fine_count;
    gpu->cluster_regions = mesh->regions;
    gpu->cluster_region_count = mesh->region_count;
    af_cluster last = gpu->clusters[gpu->cluster_fine_count - 1];
    gpu->index_count = last.first_index + last.index_count;
    mesh->clusters = NULL;
    mesh->regions = NULL;
    return true;
}

bool af_vk_r_upload_cluster_stream(af_vk_renderer *r, const char *name, af_cluster_mesh *mesh,
                                       af_cluster_stream *stream) {
    if (!name)
        return false;
    unsigned streams = 0;
    for (size_t i = 0; i < r->mesh_count; ++i)
        streams += r->meshes[i].gpu.residency && strcmp(r->meshes[i].name, name);
    if (streams >= AF_CLUSTER_STREAM_ASSETS)
        return false;
    af_cluster_residency *cache = calloc(1, sizeof(*cache));
    if (!cache)
        return false;
    uint32_t count = af_cluster_stream_region_count(stream);
    cache->page_requirements = calloc(count, sizeof(*cache->page_requirements));
    if (!cache->page_requirements || !af_vk_r_upload_clusters(r, name, mesh)) {
        free(cache->page_requirements);
        free(cache);
        return false;
    }
    cache->stream = stream;
    af_gpu_mesh *gpu = &r->meshes[af_r_find_mesh_index(r, name)].gpu;
    gpu->residency = cache;
    for (uint32_t i = 0; i < count; ++i) {
        const af_cluster_stream_region *region = af_cluster_stream_region_at(stream, i);
        af_vec3 lo = region->minimum, hi = region->maximum;
        gpu->minimum = (af_vec3){fminf(gpu->minimum.x, lo.x), fminf(gpu->minimum.y, lo.y), fminf(gpu->minimum.z, lo.z)};
        gpu->maximum = (af_vec3){fmaxf(gpu->maximum.x, hi.x), fmaxf(gpu->maximum.y, hi.y), fmaxf(gpu->maximum.z, hi.z)};
    }
    return true;
}


bool af_vk_r_remove_mesh(af_vk_renderer *renderer, const char *name) {
    if (!name)
        return false;
    size_t index = af_r_find_mesh_index(renderer, name);
    if (index == renderer->mesh_count)
        return false;
    if (renderer->meshes[index].dynamic) {
        renderer->meshes[index].retired = true;
        free(renderer->meshes[index].cpu_vertices);
        free(renderer->meshes[index].cpu_indices);
        renderer->meshes[index].cpu_vertices = NULL;
        renderer->meshes[index].cpu_indices = NULL;
        return true;
    }
    ++renderer->mesh_idle_waits;
    if (!af_vk_r_wait_idle(renderer))
        return false;
    af_r_destroy_mesh(renderer, &renderer->meshes[index].gpu);
    memmove(&renderer->meshes[index], &renderer->meshes[index + 1],
            (renderer->mesh_count - index - 1) * sizeof(*renderer->meshes));
    --renderer->mesh_count;
    ++renderer->mesh_revision;
    return true;
}


bool af_vk_r_update_water(af_vk_renderer *r, const af_physics *physics, const af_scene *s, float alpha) {
    r->water_physics = physics;
    const af_water_runtime *runtime = ph_water_runtime(physics);
    for (size_t i = r->mesh_count; i > 0; --i) {
        const char *name = r->meshes[i - 1].name;
        if (strncmp(name, "__water_", 8))
            continue;
        bool keep = false;
        for (size_t k = 0; k < af_scene_water_count(s); ++k) {
            char expected[64];
            af_r_water_name(af_scene_water_at(s, k)->owner, expected);
            if (!strcmp(name, expected))
                keep = true;
        }
        if (!r->meshes[i - 1].retired && !keep && !af_vk_r_remove_mesh(r, name))
            return false;
    }
    for (size_t i = 0; i < af_scene_water_count(s); ++i) {
        af_water w = *af_scene_water_at(s, i);
        const af_scene_object *o = af_scene_get_object(s, w.owner);
        if (!o)
            return false;
        float time = 0;
        bool live = false;
        for (size_t k = 0; runtime && k < runtime->count; ++k)
            if (runtime->slots[k].config.owner == w.owner && !memcmp(&runtime->slots[k].config, &w, sizeof(w)) &&
                !memcmp(&runtime->slots[k].transform, &o->transform, sizeof(o->transform))) {
                time = runtime->slots[k].time;
                live = true;
                if (runtime->slots[k].sleeping ||
                    (runtime->slots[k].particles && runtime->slots[k].particles->sleeping))
                    alpha = 1;
                break;
            }
        float a = live ? alpha : 1;
        char name[64];
        af_r_water_name(w.owner, name);
        if (af_r_find_mesh_index(r, name) < r->mesh_count && !memcmp(&r->water_configs[i], &w, sizeof(w)) &&
            !memcmp(&r->water_transforms[i], &o->transform, sizeof(o->transform)) && r->water_times[i] == time &&
            r->water_alphas[i] == a)
            continue;
        af_vertex *v = NULL;
        uint32_t *ix = NULL;
        af_mesh_data mesh;
        bool ok = wa_mesh(live ? runtime : NULL, s, w.owner, a, &v, &ix, &mesh) && stream_set(r, name, &mesh);
        free(v);
        free(ix);
        if (!ok)
            return false;
        r->water_configs[i] = w;
        r->water_transforms[i] = o->transform;
        r->water_times[i] = time;
        r->water_alphas[i] = a;
    }
    return true;
}

typedef struct terrain_chunk_build_context {
    const af_landscape *landscape;
    const size_t *chunk_indices;
    af_vertex *vertices;
    uint32_t *indices;
    size_t v_count;
    size_t idx_count;
    bool *ok;
} terrain_chunk_build_context;

static void build_terrain_chunk_range(size_t start, size_t end, void *user_data) {
    const terrain_chunk_build_context *ctx = user_data;
    for (size_t k = start; k < end; ++k) {
        ctx->ok[k] = af_landscape_build_chunk_mesh(ctx->landscape, ctx->chunk_indices[k],
                                                   ctx->vertices + k * ctx->v_count,
                                                   ctx->indices + k * ctx->idx_count);
    }
}

bool af_vk_r_update_landscape(af_vk_renderer *r, const af_scene *s) {
    if (!r)
        return false;
    const af_landscape *ls = s ? af_scene_get_landscape(s) : NULL;
    for (size_t i = r->mesh_count; i > 0; --i) {
        const char *name = r->meshes[i - 1].name;
        if (strncmp(name, "__terrain_", 10))
            continue;
        bool keep = false;
        if (ls && ls->enabled) {
            for (size_t k = 0; k < ls->chunk_count; ++k) {
                if (!strcmp(name, ls->chunks[k].mesh_name)) {
                    keep = true;
                    break;
                }
            }
        }
        if (!r->meshes[i - 1].retired && !keep && !af_vk_r_remove_mesh(r, name))
            return false;
    }

    if (!ls || !ls->enabled || ls->chunk_count == 0) {
        r->landscape_chunks = NULL;
        r->landscape_version = 0;
        return true;
    }

    bool force_rebuild = (ls->chunks != r->landscape_chunks || ls->modification_version != r->landscape_version);
    size_t *dirty = malloc(ls->chunk_count * sizeof(*dirty));
    if (!dirty)
        return false;
    size_t dirty_count = 0;
    for (size_t i = 0; i < ls->chunk_count; ++i) {
        af_terrain_chunk *c = (af_terrain_chunk *)&ls->chunks[i];
        if (c->mesh_name[0] == '\0') {
            snprintf(c->mesh_name, sizeof(c->mesh_name), "__terrain_%d_%d", c->chunk_x, c->chunk_z);
            c->dirty_mesh = true;
        }
        if (force_rebuild || c->dirty_mesh || af_r_find_mesh_index(r, c->mesh_name) == r->mesh_count)
            dirty[dirty_count++] = i;
    }

    if (dirty_count == 0) {
        free(dirty);
        r->landscape_chunks = ls->chunks;
        r->landscape_version = ls->modification_version;
        return true;
    }

    const size_t batch_cap = dirty_count < 32u ? dirty_count : 32u;
    size_t v_count = af_landscape_chunk_vertex_count();
    size_t idx_count = af_landscape_chunk_index_count();
    af_vertex *v = malloc(batch_cap * v_count * sizeof(*v));
    uint32_t *ix = malloc(batch_cap * idx_count * sizeof(*ix));
    bool *ok = malloc(batch_cap * sizeof(*ok));
    if (!v || !ix || !ok) {
        free(dirty);
        free(v);
        free(ix);
        free(ok);
        return false;
    }

    bool success = true;
    af_jobs *jobs = af_jobs_global();
    for (size_t offset = 0; offset < dirty_count && success; offset += batch_cap) {
        size_t batch = dirty_count - offset;
        if (batch > batch_cap)
            batch = batch_cap;
        terrain_chunk_build_context ctx = {.landscape = ls,
                                           .chunk_indices = dirty + offset,
                                           .vertices = v,
                                           .indices = ix,
                                           .v_count = v_count,
                                           .idx_count = idx_count,
                                           .ok = ok};
        af_jobs_parallel_for(jobs, batch, 1u, build_terrain_chunk_range, &ctx);
        for (size_t k = 0; k < batch; ++k) {
            if (!ok[k]) {
                success = false;
                break;
            }
            af_terrain_chunk *c = (af_terrain_chunk *)&ls->chunks[dirty[offset + k]];
            af_mesh_data md = {.vertices = v + k * v_count,
                               .indices = ix + k * idx_count,
                               .vertex_count = (uint32_t)v_count,
                               .index_count = (uint32_t)idx_count};
            if (!stream_set(r, c->mesh_name, &md)) {
                success = false;
                break;
            }
            c->dirty_mesh = false;
        }
    }

    if (success) {
        r->landscape_chunks = ls->chunks;
        r->landscape_version = ls->modification_version;
    }
    free(dirty);
    free(v);
    free(ix);
    free(ok);
    return success;
}

bool af_r_object_world(af_vk_renderer *renderer, const af_scene *scene, size_t index, af_mat4 *out) {
    if (scene && renderer->posed_scene == scene && renderer->posed_count == af_scene_object_count(scene) &&
        index < renderer->posed_count) {
        *out = renderer->posed_worlds[index];
        return true;
    }
    const af_scene_object *o = af_scene_object_at(scene, index);
    return o && af_scene_interpolated_world_matrix(scene, renderer->previous_scene, o->id,
                                                   renderer->interpolation_alpha, out);
}
const af_morph_draw *af_r_morph_draw_for(const af_vk_renderer *r, const af_scene *scene, size_t index) {
    return r->morph_scene == scene && index < r->morph_draw_count && r->morph_draws[index].signature
               ? &r->morph_draws[index]
               : NULL;
}
af_object_id af_vk_r_pick_object(af_vk_renderer *renderer, const af_scene *scene, const af_camera *camera,
                                     af_rect viewport, af_vec2 point) {
    viewport = af_r_resolve_viewport(renderer, viewport);
    if (!isfinite(point.x) || !isfinite(point.y) || point.x < viewport.x || point.y < viewport.y ||
        point.x >= viewport.x + viewport.width || point.y >= viewport.y + viewport.height)
        return AF_OBJECT_NONE;
    af_vec3 forward = af_camera_forward(camera);
    af_vec3 right = af_vec3_normalize(af_vec3_cross(forward, (af_vec3){0, 1, 0}));
    af_vec3 up = af_vec3_cross(right, forward);
    float spread = tanf(camera->vertical_fov * 0.5f);
    float x = ((point.x - viewport.x) / viewport.width * 2 - 1) * spread * viewport.width / viewport.height;
    float y = (1 - (point.y - viewport.y) / viewport.height * 2) * spread;
    af_vec3 ray = af_vec3_normalize(af_vec3_add(forward, af_vec3_add(af_vec3_scale(right, x), af_vec3_scale(up, y))));
    float closest = camera->far_clip;
    af_object_id selected = AF_OBJECT_NONE;
    for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
        const af_scene_object *object = af_scene_object_at(scene, i);
        const af_water *water = af_scene_get_water(scene, object->id);
        if (water && !water->enabled)
            continue;
        if (!af_scene_object_visible(scene, object->id) || (!water && !strcmp(object->mesh, "__node")))
            continue;
        char name[64];
        if (water)
            af_r_water_name(object->id, name);
        size_t index = af_r_find_mesh_index(renderer, water ? name : object->mesh);
        if (index == renderer->mesh_count)
            continue;
        af_mat4 inverse;
        af_mat4 world;
        if (!af_r_object_world(renderer, scene, i, &world) || !af_mat4_inverse(world, &inverse))
            continue;
        af_vec4 origin =
            af_mat4_transform(inverse, (af_vec4){camera->position.x, camera->position.y, camera->position.z, 1});
        af_vec4 direction = af_mat4_transform(inverse, (af_vec4){ray.x, ray.y, ray.z, 0});
        af_gpu_mesh *mesh = &renderer->meshes[index].gpu;
        const af_morph_draw *morph = af_r_morph_draw_for(renderer, scene, i);
        if (morph && morph->object != object->id)
            morph = NULL;
        af_vec3 minimum = morph ? morph->minimum : mesh->minimum, maximum = morph ? morph->maximum : mesh->maximum;
        if (af_r_skin_base(renderer, object->id, mesh)) {
            af_shadow_bounds bounds = af_r_object_bounds(renderer, object->id, world, mesh, morph);
            minimum = (af_vec3){INFINITY, INFINITY, INFINITY};
            maximum = (af_vec3){-INFINITY, -INFINITY, -INFINITY};
            for (unsigned j = 0; j < 8; ++j) {
                af_vec3 p = bounds.corners[j];
                minimum = (af_vec3){fminf(minimum.x, p.x), fminf(minimum.y, p.y), fminf(minimum.z, p.z)};
                maximum = (af_vec3){fmaxf(maximum.x, p.x), fmaxf(maximum.y, p.y), fmaxf(maximum.z, p.z)};
            }
            origin = (af_vec4){camera->position.x, camera->position.y, camera->position.z, 1};
            direction = (af_vec4){ray.x, ray.y, ray.z, 0};
        }
        float o[] = {origin.x, origin.y, origin.z}, d[] = {direction.x, direction.y, direction.z};
        float lo[] = {minimum.x, minimum.y, minimum.z}, hi[] = {maximum.x, maximum.y, maximum.z};
        float near = camera->near_clip, far = closest;
        bool hit = true;
        for (unsigned axis = 0; axis < 3; ++axis) {
            if (fabsf(d[axis]) < 1e-12f) {
                if (o[axis] < lo[axis] || o[axis] > hi[axis])
                    hit = false;
                continue;
            }
            float a = (lo[axis] - o[axis]) / d[axis], b = (hi[axis] - o[axis]) / d[axis];
            near = fmaxf(near, fminf(a, b));
            far = fminf(far, fmaxf(a, b));
            if (near > far)
                hit = false;
        }
        if (hit && near < closest) {
            closest = near;
            selected = object->id;
        }
    }
    return selected;
}

uint32_t af_r_skin_base(const af_vk_renderer *renderer, af_object_id object, const af_gpu_mesh *mesh) {
    if (!mesh->skinned)
        return 0;
    for (size_t i = 0; i < renderer->skin_count; ++i)
        if (renderer->skins[i].object == object && renderer->skins[i].count > mesh->max_joint)
            return 1u + (uint32_t)i * AF_ANIMATION_MAX_JOINTS;
    return 0;
}
af_shadow_bounds af_r_object_bounds(af_vk_renderer *renderer, af_object_id object, af_mat4 world,
                                      const af_gpu_mesh *mesh, const af_morph_draw *morph) {
    af_vec3 minimum = morph ? morph->minimum : mesh->minimum, maximum = morph ? morph->maximum : mesh->maximum;
    uint32_t base = af_r_skin_base(renderer, object, mesh);
    if (!base)
        return af_shadow_transform_bounds(world, minimum, maximum);
    const af_skin_instance *skin = &renderer->skins[(base - 1) / AF_ANIMATION_MAX_JOINTS];
    af_vec3 lo = {INFINITY, INFINITY, INFINITY}, hi = {-INFINITY, -INFINITY, -INFINITY};
    for (size_t i = 0; i < skin->count; ++i) {
        af_shadow_bounds b = af_shadow_transform_bounds(af_mat4_mul(world, skin->matrices[i]), minimum, maximum);
        for (unsigned j = 0; j < 8; ++j) {
            af_vec3 v = b.corners[j];
            lo = (af_vec3){fminf(lo.x, v.x), fminf(lo.y, v.y), fminf(lo.z, v.z)};
            hi = (af_vec3){fmaxf(hi.x, v.x), fmaxf(hi.y, v.y), fmaxf(hi.z, v.z)};
        }
    }
    return af_shadow_transform_bounds(af_mat4_identity(), lo, hi);
}

typedef struct morph_vertex_context {
    const af_mesh_morph *morph;
    const af_morph_delta *deltas;
    const af_vertex *base;
    af_vertex *out;
} morph_vertex_context;

static void morph_displace_range(size_t start, size_t end, void *user_data) {
    const morph_vertex_context *ctx = user_data;
    for (size_t j = start; j < end; ++j) {
        const af_morph_delta *delta = &ctx->deltas[ctx->morph->source_vertices[j]];
        af_vertex v = ctx->base[j];
        v.position = af_vec3_add(v.position, delta->position);
        v.normal = af_vec3_add(v.normal, delta->normal);
        ctx->out[j] = v;
    }
}

static void morph_normals_range(size_t start, size_t end, void *user_data) {
    const morph_vertex_context *ctx = user_data;
    for (size_t tri = start; tri < end; ++tri) {
        af_vertex *triangle = ctx->out + tri * 3u;
        af_vec3 normal = af_vec3_normalize(af_vec3_cross(af_vec3_sub(triangle[1].position, triangle[0].position),
                                                         af_vec3_sub(triangle[2].position, triangle[0].position)));
        triangle[0].normal = triangle[1].normal = triangle[2].normal = normal;
    }
}

bool af_r_prepare_morphs(af_vk_renderer *r, const af_scene *scene) {
    r->morph_scene = scene;
    r->morph_draw_count = r->morph_vertex_count = 0;
    if (scene != r->posed_scene || !r->posed_morphs)
        return true;
    size_t count = af_scene_object_count(scene);
    if (count != r->posed_count)
        return false;
    if (r->morph_draw_capacity < count) {
        af_morph_draw *draws = realloc(r->morph_draws, count * sizeof(*draws));
        if (!draws)
            return false;
        r->morph_draws = draws;
        r->morph_draw_capacity = count;
    }
    if (count)
        memset(r->morph_draws, 0, count * sizeof(*r->morph_draws));
    r->morph_draw_count = count;
    for (size_t i = 0; i < count; ++i) {
        const af_animation_pose *pose = r->posed_morphs[i];
        if (!pose)
            continue;
        const af_scene_object *object = af_scene_object_at(scene, i);
        size_t index = af_r_find_mesh_index(r, object->mesh);
        if (index == r->mesh_count || !af_scene_object_visible(scene, object->id))
            continue;
        const af_gpu_mesh *mesh = &r->meshes[index].gpu;
        const af_mesh_morph *morph = mesh->morph;
        if (!morph || morph->target_count != pose->morph_count)
            continue;
        uint64_t signature = UINT64_C(14695981039346656037);
        bool active = false;
        for (uint32_t j = 0; j < morph->target_count; ++j) {
            if (!(morph->used_targets & (UINT64_C(1) << j)))
                continue;
            float weight = pose->morph_weights[j];
            active |= weight != morph->defaults[j];
            signature = af_r_hash(signature, &weight, sizeof(weight));
        }
        if (!active)
            continue;
        size_t needed = r->morph_vertex_count + mesh->vertex_count;
        if (needed > 1048576u) {
            af_log("error", "Active morph geometry exceeds one million triangle vertices.");
            return false;
        }
        if (r->morph_vertex_capacity < needed) {
            size_t capacity = r->morph_vertex_capacity ? r->morph_vertex_capacity : 1024;
            while (capacity < needed)
                capacity *= 2;
            af_vertex *vertices = realloc(r->morph_vertices, capacity * sizeof(*vertices));
            if (!vertices)
                return false;
            r->morph_vertices = vertices;
            r->morph_vertex_capacity = capacity;
        }
        if (r->morph_delta_capacity < morph->source_count) {
            af_morph_delta *deltas = realloc(r->morph_deltas, morph->source_count * sizeof(*deltas));
            if (!deltas)
                return false;
            r->morph_deltas = deltas;
            r->morph_delta_capacity = morph->source_count;
        }
        af_mesh_morph_blend(morph, pose->morph_weights, true, r->morph_deltas);
        af_morph_draw *draw = &r->morph_draws[i];
        draw->offset = r->morph_vertex_count * sizeof(af_vertex);
        draw->object = object->id;
        draw->signature = signature ? signature : 1;
        af_vertex *vertices = r->morph_vertices + r->morph_vertex_count;
        draw->minimum = (af_vec3){INFINITY, INFINITY, INFINITY};
        draw->maximum = (af_vec3){-INFINITY, -INFINITY, -INFINITY};
        morph_vertex_context morph_ctx = {
            .morph = morph, .deltas = r->morph_deltas, .base = mesh->morph_base, .out = vertices};
        af_jobs_parallel_for(af_jobs_global(), mesh->vertex_count, 1024u, morph_displace_range, &morph_ctx);
        for (uint32_t j = 0; j < mesh->vertex_count; ++j) {
            af_vec3 p = vertices[j].position;
            af_vec3 n = vertices[j].normal;
            if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) || !isfinite(n.x) || !isfinite(n.y) ||
                !isfinite(n.z))
                return false;
            draw->minimum =
                (af_vec3){fminf(draw->minimum.x, p.x), fminf(draw->minimum.y, p.y), fminf(draw->minimum.z, p.z)};
            draw->maximum =
                (af_vec3){fmaxf(draw->maximum.x, p.x), fmaxf(draw->maximum.y, p.y), fmaxf(draw->maximum.z, p.z)};
        }
        if (!morph->has_normals)
            af_jobs_parallel_for(af_jobs_global(), mesh->vertex_count / 3u, 512u, morph_normals_range, &morph_ctx);
        r->morph_vertex_count = needed;
    }
    return true;
}
void af_r_bind_object_mesh(af_frame *frame, const af_gpu_mesh *mesh, const af_morph_draw *morph) {
    VkDeviceSize offset = morph ? morph->offset : 0;
    VkBuffer vertices = morph ? frame->morph_buffer.handle : mesh->vertices.handle;
    vkCmdBindVertexBuffers(frame->command, 0, 1, &vertices, &offset);
    vkCmdBindIndexBuffer(frame->command, mesh->indices.handle, 0, VK_INDEX_TYPE_UINT32);
}

bool af_vk_r_set_skin_palette(af_vk_renderer *r, af_object_id object, const af_mat4 *matrices, size_t count) {
    if (!r || !object || count > AF_ANIMATION_MAX_JOINTS || (count && !matrices))
        return false;
    size_t index = 0;
    while (index < r->skin_count && r->skins[index].object != object)
        ++index;
    if (!count) {
        if (index < r->skin_count) {
            r->skins[index] = r->skins[--r->skin_count];
            ++r->skin_revision;
        }
        return true;
    }
    for (size_t i = 0; i < count; ++i)
        for (unsigned j = 0; j < 16; ++j)
            if (!isfinite(matrices[i].elements[j]))
                return false;
    if (index < r->skin_count && r->skins[index].count == count &&
        !memcmp(r->skins[index].matrices, matrices, count * sizeof(*matrices)))
        return true;
    if (index == AF_SKIN_INSTANCES)
        return false;
    if (index == r->skin_count)
        ++r->skin_count;
    r->skins[index].object = object;
    r->skins[index].count = count;
    memcpy(r->skins[index].matrices, matrices, count * sizeof(*matrices));
    ++r->skin_revision;
    return true;
}
void af_vk_r_clear_skin_palettes(af_vk_renderer *r) {
    if (r && r->skin_count) {
        r->skin_count = 0;
        ++r->skin_revision;
    }
}

void af_vk_r_set_object_worlds(af_vk_renderer *r, const af_scene *scene, const af_mat4 *world,
                                   const af_animation_pose *const *morph_poses, size_t count) {
    if (r) {
        r->posed_scene = scene;
        r->posed_worlds = world;
        r->posed_morphs = morph_poses;
        r->posed_count = count;
    }
}

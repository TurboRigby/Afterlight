/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"

#define AF_MTL_RAY_MAX_INSTANCES 65536u
#define AF_MTL_RAY_MAX_MESHES 4096u
#define AF_MTL_RAY_MAX_ALLOCATION (UINT64_C(256) * 1024 * 1024)

typedef struct mtl_ray_mesh {
    uint64_t key, page_generation, serial;
    id<MTLAccelerationStructure> blas;
    id<MTLBuffer> scratch;
    id<MTLBuffer> vertices_buf;
    id<MTLBuffer> indices_buf;
    MTLPrimitiveAccelerationStructureDescriptor *descriptor;
    uint64_t vertices_addr, indices_addr;
    uint64_t blas_bytes, scratch_bytes;
    uint32_t first_index, index_count, frame_mask;
    bool transient, dirty;
} mtl_ray_mesh;

typedef struct mtl_ray_frame {
    id<MTLAccelerationStructure> tlas;
    id<MTLBuffer> scratch;
    id<MTLBuffer> instances;
    id<MTLBuffer> surfaces;
    MTLInstanceAccelerationStructureDescriptor *tlas_desc;
    NSArray<id<MTLAccelerationStructure>> *active_blas_array;
    id<MTLBuffer> *active_buffers;
    size_t active_buffer_count, active_buffer_capacity;
    uint64_t tlas_bytes, scratch_bytes, instances_bytes, surfaces_bytes;
    size_t capacity;
    uint64_t hash;
    bool dirty;
} mtl_ray_frame;

struct af_mtl_ray_query {
    id<MTLDevice> device;
    mtl_ray_mesh *meshes;
    size_t mesh_count, mesh_capacity, static_mesh_count;
    uint64_t mesh_serial;
    mtl_ray_frame *frames;
    unsigned frame_count, allocations;
    af_ray_stats stats;
};

static bool check_fault_inject(af_mtl_ray_query *r) {
    const char *fault = getenv("AFTERLIGHT_TEST_RAY_FAIL_ALLOC");
    if (fault && strtoul(fault, NULL, 10) == ++r->allocations)
        return false;
    return true;
}

static void free_ray_mesh(af_mtl_ray_query *r, mtl_ray_mesh *m) {
    r->stats.bytes -= m->blas_bytes + m->scratch_bytes;
    AF_MTL_RELEASE(m->blas);
    AF_MTL_RELEASE(m->scratch);
    AF_MTL_RELEASE(m->descriptor);
    *m = (mtl_ray_mesh){0};
}

static void free_ray_frame(af_mtl_ray_query *r, mtl_ray_frame *f) {
    r->stats.bytes -= f->tlas_bytes + f->scratch_bytes + f->instances_bytes + f->surfaces_bytes;
    AF_MTL_RELEASE(f->tlas);
    AF_MTL_RELEASE(f->scratch);
    AF_MTL_RELEASE(f->instances);
    AF_MTL_RELEASE(f->surfaces);
    AF_MTL_RELEASE(f->tlas_desc);
    AF_MTL_RELEASE(f->active_blas_array);
    free(f->active_buffers);
    *f = (mtl_ray_frame){0};
}

void af_mtl_ray_query_destroy(af_mtl_ray_query *r) {
    if (!r)
        return;
    if (r->frames)
        for (unsigned i = 0; i < r->frame_count; ++i)
            free_ray_frame(r, &r->frames[i]);
    for (size_t i = 0; i < r->mesh_count; ++i)
        free_ray_mesh(r, &r->meshes[i]);
    free(r->frames);
    free(r->meshes);
    free(r);
}

af_mtl_ray_query *af_mtl_ray_query_create(id<MTLDevice> device, unsigned frames) {
    if (!device || ![device supportsRaytracing] || !frames)
        return NULL;
    af_mtl_ray_query *r = calloc(1, sizeof(*r));
    if (!r)
        return NULL;
    r->device = device;
    r->frame_count = frames;
    r->frames = calloc(frames, sizeof(*r->frames));
    if (!r->frames) {
        free(r);
        return NULL;
    }
    return r;
}

size_t af_mtl_ray_query_instance_limit(const af_mtl_ray_query *r) {
    (void)r;
    return AF_MTL_RAY_MAX_INSTANCES;
}

static mtl_ray_mesh *mesh_get(af_mtl_ray_query *r, const af_mtl_ray_instance *in) {
    for (size_t i = 0; i < r->mesh_count; ++i) {
        mtl_ray_mesh *m = &r->meshes[i];
        if (m->key == in->mesh_key && m->first_index == in->first_index && m->index_count == in->index_count &&
            m->page_generation == in->page_generation)
            return m;
    }
    if ((!in->transient && r->static_mesh_count >= AF_MTL_RAY_MAX_MESHES) || !in->vertices || !in->indices ||
        in->index_count < 3)
        return NULL;
    if (r->mesh_count == r->mesh_capacity) {
        size_t cap = r->mesh_capacity ? r->mesh_capacity * 2u : 16u;
        mtl_ray_mesh *grown = realloc(r->meshes, cap * sizeof(*grown));
        if (!grown)
            return NULL;
        r->meshes = grown;
        r->mesh_capacity = cap;
    }

    mtl_ray_mesh m = {.key = in->mesh_key,
                      .page_generation = in->page_generation,
                      .serial = ++r->mesh_serial,
                      .vertices_buf = in->vertices,
                      .indices_buf = in->indices,
                      .first_index = in->first_index,
                      .index_count = in->index_count,
                      .transient = in->transient,
                      .dirty = true};

    m.vertices_addr = [in->vertices gpuAddress];
    m.indices_addr = [in->indices gpuAddress] + (uint64_t)in->first_index * sizeof(uint32_t);
    if (!m.vertices_addr || !m.indices_addr)
        return NULL;

    @autoreleasepool {
        MTLAccelerationStructureTriangleGeometryDescriptor *geom =
            [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
        geom.vertexBuffer = in->vertices;
        geom.vertexBufferOffset = offsetof(af_vertex, position);
        geom.vertexStride = sizeof(af_vertex);
        geom.vertexFormat = MTLAttributeFormatFloat3;
        geom.indexBuffer = in->indices;
        geom.indexBufferOffset = (NSUInteger)in->first_index * sizeof(uint32_t);
        geom.indexType = MTLIndexTypeUInt32;
        geom.triangleCount = in->index_count / 3u;
        geom.opaque = YES;

        MTLPrimitiveAccelerationStructureDescriptor *desc =
            [[MTLPrimitiveAccelerationStructureDescriptor alloc] init];
        desc.geometryDescriptors = @[geom];
        desc.usage = MTLAccelerationStructureUsageNone;

        MTLAccelerationStructureSizes sizes = [r->device accelerationStructureSizesWithDescriptor:desc];
        if (!check_fault_inject(r) || sizes.accelerationStructureSize == 0 ||
            sizes.accelerationStructureSize > AF_MTL_RAY_MAX_ALLOCATION) {
            [desc release];
            return NULL;
        }
        m.blas = [r->device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
        if (!m.blas) {
            [desc release];
            return NULL;
        }
        m.blas_bytes = (uint64_t)[m.blas size];
        if (m.blas_bytes == 0)
            m.blas_bytes = (uint64_t)sizes.accelerationStructureSize;

        NSUInteger scratch_sz = sizes.buildScratchBufferSize ? sizes.buildScratchBufferSize : 256u;
        if (!check_fault_inject(r) || scratch_sz > AF_MTL_RAY_MAX_ALLOCATION) {
            AF_MTL_RELEASE(m.blas);
            [desc release];
            return NULL;
        }
        m.scratch = [r->device newBufferWithLength:scratch_sz options:MTLResourceStorageModePrivate];
        if (!m.scratch) {
            AF_MTL_RELEASE(m.blas);
            [desc release];
            return NULL;
        }
        m.scratch_bytes = (uint64_t)[m.scratch allocatedSize];
        if (m.scratch_bytes == 0)
            m.scratch_bytes = (uint64_t)scratch_sz;
        m.descriptor = desc;
    }

    r->stats.bytes += m.blas_bytes + m.scratch_bytes;
    r->meshes[r->mesh_count] = m;
    ++r->mesh_count;
    r->static_mesh_count += !m.transient;
    return &r->meshes[r->mesh_count - 1];
}

static void retire_streamed_meshes(af_mtl_ray_query *r) {
    size_t i = 0;
    while (i < r->mesh_count) {
        mtl_ray_mesh *m = &r->meshes[i];
        if (m->transient && !m->frame_mask) {
            free_ray_mesh(r, m);
            *m = r->meshes[--r->mesh_count];
        } else {
            ++i;
        }
    }
}

static bool add_active_buffer(mtl_ray_frame *f, id<MTLBuffer> buf) {
    if (!buf)
        return true;
    for (size_t i = 0; i < f->active_buffer_count; ++i)
        if (f->active_buffers[i] == buf)
            return true;
    if (f->active_buffer_count == f->active_buffer_capacity) {
        size_t cap = f->active_buffer_capacity ? f->active_buffer_capacity * 2u : 32u;
        id<MTLBuffer> *grown = realloc(f->active_buffers, cap * sizeof(*grown));
        if (!grown)
            return false;
        f->active_buffers = grown;
        f->active_buffer_capacity = cap;
    }
    f->active_buffers[f->active_buffer_count++] = buf;
    return true;
}

bool af_mtl_ray_query_prepare(af_mtl_ray_query *r, unsigned frame, const af_mtl_ray_instance *in, size_t count,
                              bool reflections) {
    if (!r || frame >= r->frame_count || count > af_mtl_ray_query_instance_limit(r))
        return false;
    uint32_t frame_bit = 1u << frame;
    for (size_t i = 0; i < r->mesh_count; ++i)
        r->meshes[i].frame_mask &= ~frame_bit;

    mtl_ray_frame *f = &r->frames[frame];
    f->active_buffer_count = 0;

    size_t surface_count = (reflections && count) ? count : 1u;
    uint64_t surface_bytes = (uint64_t)surface_count * sizeof(af_ray_surface_record);
    if (!f->surfaces || f->surfaces_bytes < surface_bytes) {
        if (!check_fault_inject(r) || surface_bytes > AF_MTL_RAY_MAX_ALLOCATION)
            return false;
        r->stats.bytes -= f->surfaces_bytes;
        AF_MTL_RELEASE(f->surfaces);
        f->surfaces_bytes = 0;
        f->surfaces = [r->device newBufferWithLength:(NSUInteger)surface_bytes options:MTLResourceStorageModeShared];
        if (!f->surfaces)
            return false;
        f->surfaces_bytes = (uint64_t)[f->surfaces allocatedSize];
        if (f->surfaces_bytes == 0)
            f->surfaces_bytes = surface_bytes;
        r->stats.bytes += f->surfaces_bytes;
    }

    af_ray_surface_record *surfaces = (af_ray_surface_record *)[f->surfaces contents];
    surfaces[0] = (af_ray_surface_record){0};

    size_t inst_cap = count ? count : 1u;
    MTLAccelerationStructureUserIDInstanceDescriptor *cpu_instances = calloc(inst_cap, sizeof(*cpu_instances));
    if (!cpu_instances)
        return false;

    NSMutableArray<id<MTLAccelerationStructure>> *blas_list = [[NSMutableArray alloc] init];
    uint32_t *mesh_to_blas_idx = calloc(r->mesh_count + count + 1u, sizeof(*mesh_to_blas_idx));
    if (!mesh_to_blas_idx) {
        [blas_list release];
        free(cpu_instances);
        return false;
    }

    bool ok = true;
    uint64_t hash = af_r_hash(UINT64_C(14695981039346656037), &count, sizeof(count));

    for (size_t i = 0; i < count; ++i) {
        mtl_ray_mesh *m = mesh_get(r, &in[i]);
        if (!m) {
            ok = false;
            break;
        }
        m->frame_mask |= frame_bit;
        size_t mesh_idx = (size_t)(m - r->meshes);
        /* Note: mesh_to_blas_idx stores (blas_index + 1) so 0 means unassigned. */
        if (mesh_idx >= r->mesh_count) {
            ok = false;
            break;
        }
        uint32_t blas_idx;
        if (mesh_to_blas_idx[mesh_idx] == 0) {
            blas_idx = (uint32_t)[blas_list count];
            [blas_list addObject:m->blas];
            mesh_to_blas_idx[mesh_idx] = blas_idx + 1u;
            if (reflections) {
                if (!add_active_buffer(f, m->vertices_buf) || !add_active_buffer(f, m->indices_buf)) {
                    ok = false;
                    break;
                }
            }
        } else {
            blas_idx = mesh_to_blas_idx[mesh_idx] - 1u;
        }

        MTLAccelerationStructureUserIDInstanceDescriptor *d = &cpu_instances[i];
        /* af_mat4 is column-major: columns 0..3 each have (x, y, z). */
        for (unsigned col = 0; col < 4u; ++col) {
            d->transformationMatrix.columns[col].elements[0] = in[i].world.elements[col * 4u + 0u];
            d->transformationMatrix.columns[col].elements[1] = in[i].world.elements[col * 4u + 1u];
            d->transformationMatrix.columns[col].elements[2] = in[i].world.elements[col * 4u + 2u];
        }
        d->options = MTLAccelerationStructureInstanceOptionOpaque |
                     (in[i].single_sided
                          ? MTLAccelerationStructureInstanceOptionTriangleFrontFacingWindingCounterClockwise
                          : MTLAccelerationStructureInstanceOptionDisableTriangleCulling);
        d->mask = in[i].mask;
        d->intersectionFunctionTableOffset = 0;
        d->accelerationStructureIndex = blas_idx;
        d->userID = reflections ? (uint32_t)i : 0u;

        if (reflections) {
            surfaces[i] = (af_ray_surface_record){
                .color = in[i].color,
                .emission_unlit = in[i].emission_unlit,
                .addresses = {(uint32_t)m->vertices_addr, (uint32_t)(m->vertices_addr >> 32),
                              (uint32_t)m->indices_addr, (uint32_t)(m->indices_addr >> 32)}};
        }

        hash = af_r_hash(hash, d, sizeof(*d));
        hash = af_r_hash(hash, &m->serial, sizeof(m->serial));
    }

    free(mesh_to_blas_idx);
    if (!ok) {
        [blas_list release];
        free(cpu_instances);
        return false;
    }

    retire_streamed_meshes(r);

    if (inst_cap > f->capacity || !f->instances) {
        uint64_t inst_bytes = (uint64_t)inst_cap * sizeof(MTLAccelerationStructureUserIDInstanceDescriptor);
        if (!check_fault_inject(r) || inst_bytes > AF_MTL_RAY_MAX_ALLOCATION) {
            [blas_list release];
            free(cpu_instances);
            return false;
        }
        r->stats.bytes -= f->instances_bytes;
        AF_MTL_RELEASE(f->instances);
        f->instances_bytes = 0;
        f->instances = [r->device newBufferWithLength:(NSUInteger)inst_bytes options:MTLResourceStorageModeShared];
        if (!f->instances) {
            [blas_list release];
            free(cpu_instances);
            return false;
        }
        f->instances_bytes = (uint64_t)[f->instances allocatedSize];
        if (f->instances_bytes == 0)
            f->instances_bytes = inst_bytes;
        r->stats.bytes += f->instances_bytes;
        f->capacity = inst_cap;
        f->hash = 0;
    }

    bool rebuild_tlas = (!f->tlas || f->hash != hash || f->dirty);
    if (rebuild_tlas) {
        memcpy([f->instances contents], cpu_instances,
               inst_cap * sizeof(MTLAccelerationStructureUserIDInstanceDescriptor));
    }
    free(cpu_instances);

    AF_MTL_RELEASE(f->active_blas_array);
    f->active_blas_array = blas_list; /* transfers +1 ownership */

    MTLInstanceAccelerationStructureDescriptor *tdesc = [[MTLInstanceAccelerationStructureDescriptor alloc] init];
    tdesc.instanceDescriptorType = MTLAccelerationStructureInstanceDescriptorTypeUserID;
    tdesc.instanceDescriptorStride = sizeof(MTLAccelerationStructureUserIDInstanceDescriptor);
    tdesc.instanceDescriptorBuffer = f->instances;
    tdesc.instanceCount = count;
    tdesc.instancedAccelerationStructures = f->active_blas_array;
    tdesc.usage = MTLAccelerationStructureUsageNone;

    MTLAccelerationStructureSizes sizes = [r->device accelerationStructureSizesWithDescriptor:tdesc];
    if (!f->tlas || [f->tlas size] < sizes.accelerationStructureSize) {
        if (!check_fault_inject(r) || sizes.accelerationStructureSize > AF_MTL_RAY_MAX_ALLOCATION) {
            [tdesc release];
            return false;
        }
        r->stats.bytes -= f->tlas_bytes;
        AF_MTL_RELEASE(f->tlas);
        f->tlas_bytes = 0;
        NSUInteger tlas_sz = sizes.accelerationStructureSize ? sizes.accelerationStructureSize : 256u;
        f->tlas = [r->device newAccelerationStructureWithSize:tlas_sz];
        if (!f->tlas) {
            [tdesc release];
            return false;
        }
        f->tlas_bytes = (uint64_t)[f->tlas size];
        if (f->tlas_bytes == 0)
            f->tlas_bytes = (uint64_t)tlas_sz;
        r->stats.bytes += f->tlas_bytes;
        rebuild_tlas = true;
    }

    NSUInteger scratch_sz = sizes.buildScratchBufferSize ? sizes.buildScratchBufferSize : 256u;
    if (!f->scratch || [f->scratch length] < scratch_sz) {
        if (!check_fault_inject(r) || scratch_sz > AF_MTL_RAY_MAX_ALLOCATION) {
            [tdesc release];
            return false;
        }
        r->stats.bytes -= f->scratch_bytes;
        AF_MTL_RELEASE(f->scratch);
        f->scratch_bytes = 0;
        f->scratch = [r->device newBufferWithLength:scratch_sz options:MTLResourceStorageModePrivate];
        if (!f->scratch) {
            [tdesc release];
            return false;
        }
        f->scratch_bytes = (uint64_t)[f->scratch allocatedSize];
        if (f->scratch_bytes == 0)
            f->scratch_bytes = (uint64_t)scratch_sz;
        r->stats.bytes += f->scratch_bytes;
    }

    AF_MTL_RELEASE(f->tlas_desc);
    f->tlas_desc = tdesc;
    f->dirty = rebuild_tlas;
    f->hash = hash;
    r->stats.instances = count;
    r->stats.meshes = r->mesh_count;
    return true;
}

void af_mtl_ray_query_record(af_mtl_ray_query *r, unsigned frame, id<MTLCommandBuffer> cmd) {
    if (!r || !cmd || frame >= r->frame_count)
        return;
    mtl_ray_frame *f = &r->frames[frame];
    uint32_t frame_bit = 1u << frame;

    bool any_blas = false;
    for (size_t i = 0; i < r->mesh_count; ++i)
        if (r->meshes[i].dirty && (r->meshes[i].frame_mask & frame_bit)) {
            any_blas = true;
            break;
        }
    if (!any_blas && !f->dirty)
        return;

    id<MTLAccelerationStructureCommandEncoder> enc = [cmd accelerationStructureCommandEncoder];
    if (!enc)
        return;

    for (size_t i = 0; i < r->mesh_count; ++i) {
        mtl_ray_mesh *m = &r->meshes[i];
        if (m->dirty && (m->frame_mask & frame_bit)) {
            [enc buildAccelerationStructure:m->blas
                                 descriptor:m->descriptor
                              scratchBuffer:m->scratch
                        scratchBufferOffset:0];
            m->dirty = false;
            ++r->stats.blas_builds;
        }
    }

    if (f->dirty && f->tlas && f->tlas_desc) {
        [enc buildAccelerationStructure:f->tlas
                             descriptor:f->tlas_desc
                          scratchBuffer:f->scratch
                    scratchBufferOffset:0];
        f->dirty = false;
        ++r->stats.tlas_builds;
    }

    [enc endEncoding];
}

id<MTLAccelerationStructure> af_mtl_ray_query_tlas(const af_mtl_ray_query *r, unsigned frame) {
    return (r && frame < r->frame_count) ? r->frames[frame].tlas : nil;
}

id<MTLBuffer> af_mtl_ray_query_surfaces(const af_mtl_ray_query *r, unsigned frame) {
    return (r && frame < r->frame_count) ? r->frames[frame].surfaces : nil;
}

void af_mtl_ray_query_use_resources(const af_mtl_ray_query *r, unsigned frame, id<MTLRenderCommandEncoder> enc,
                                    bool reflections) {
    if (!r || !enc || frame >= r->frame_count)
        return;
    const mtl_ray_frame *f = &r->frames[frame];
    if (f->active_blas_array) {
        NSUInteger blas_count = [f->active_blas_array count];
        for (NSUInteger i = 0; i < blas_count; ++i) {
            id<MTLAccelerationStructure> blas = [f->active_blas_array objectAtIndex:i];
            [enc useResource:blas usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
        }
    }
    if (reflections && f->active_buffer_count > 0) {
        for (size_t i = 0; i < f->active_buffer_count; ++i)
            [enc useResource:f->active_buffers[i] usage:MTLResourceUsageRead stages:MTLRenderStageFragment];
    }
}

af_ray_stats af_mtl_ray_query_stats(const af_mtl_ray_query *r) {
    return r ? r->stats : (af_ray_stats){0};
}

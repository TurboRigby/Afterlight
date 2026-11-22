/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <metal_stdlib>
using namespace metal;

struct af_mtl_cluster_dispatch {
    uint count;
    uint cull;
    uint _pad0;
    uint _pad1;
};

struct af_mtl_cluster_candidate {
    packed_float3 minimum;
    uint first_index;
    packed_float3 maximum;
    uint index_count;
    uint4 object; /* x = object_index, y/z = 64-bit GPU address of index buffer, w = unused */
};

struct af_mtl_cluster_stats {
    atomic_uint triangles;
    atomic_uint visible;
};

struct af_mtl_indirect_draw {
    uint index_count;
    uint instance_count;
    uint index_start;
    int base_vertex;
    uint base_instance;
};

static inline bool cluster_visible(af_mtl_cluster_candidate cluster, const device float4x4 *clips) {
    float4x4 clip = clips[cluster.object.x];
    uint outside = 63u;
    float3 lo = float3(cluster.minimum);
    float3 hi = float3(cluster.maximum);
    for (uint i = 0u; i < 8u; ++i) {
        float3 p = float3((i & 1u) == 0u ? lo.x : hi.x, (i & 2u) == 0u ? lo.y : hi.y, (i & 4u) == 0u ? lo.z : hi.z);
        float4 q = clip * float4(p, 1.0f);
        uint sides = (q.x < -q.w ? 1u : 0u) | (q.x > q.w ? 2u : 0u) | (q.y < -q.w ? 4u : 0u) | (q.y > q.w ? 8u : 0u) |
                     (q.z < 0.0f ? 16u : 0u) | (q.z > q.w ? 32u : 0u);
        outside &= sides;
    }
    return outside == 0u;
}

kernel void af_cluster_icb_main(uint index [[thread_position_in_grid]],
                                constant af_mtl_cluster_dispatch &dispatch [[buffer(0)]],
                                const device af_mtl_cluster_candidate *candidates [[buffer(1)]],
                                const device float4x4 *clips [[buffer(2)]],
                                device af_mtl_cluster_stats *stats [[buffer(3)]],
                                device af_mtl_indirect_draw *draws [[buffer(4)]]) {
    if (index >= dispatch.count)
        return;
    af_mtl_cluster_candidate cluster = candidates[index];
    bool keep = (dispatch.cull == 0u) || cluster_visible(cluster, clips);
    if (!keep || cluster.index_count == 0u) {
        draws[index] = {0u, 0u, 0u, 0, 0u};
        return;
    }
    draws[index] = {cluster.index_count, 1u, cluster.first_index, 0, cluster.object.x};
    atomic_fetch_add_explicit(&stats->triangles, cluster.index_count / 3u, memory_order_relaxed);
    atomic_fetch_add_explicit(&stats->visible, 1u, memory_order_relaxed);
}

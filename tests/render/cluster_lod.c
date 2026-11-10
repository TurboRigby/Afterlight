/* SPDX-License-Identifier: MPL-2.0 */
#include "geometry/cluster.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REGION_INDICES (AF_CLUSTER_REGION_LEAVES * AF_CLUSTER_TRIANGLES * 3)

static int compare_edge(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static size_t boundary(const af_cluster_mesh *mesh, uint32_t first, uint32_t count, uint64_t *edges) {
    size_t size = 0;
    for (uint32_t i = first; i < first + count; ++i) {
        af_cluster c = mesh->clusters[i];
        const uint32_t *indices = mesh->mesh.indices + c.first_index;
        for (uint32_t k = 0; k < c.index_count; ++k) {
            uint32_t a = indices[k], b = indices[k % 3 == 2 ? k - 2 : k + 1];
            edges[size++] = a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
        }
    }
    qsort(edges, size, sizeof(*edges), compare_edge);
    size_t result = 0;
    for (size_t i = 0; i < size;) {
        size_t end = i + 1;
        while (end < size && edges[end] == edges[i])
            ++end;
        if (end - i == 1)
            edges[result++] = edges[i];
        i = end;
    }
    return result;
}

int main(int argc, char **argv) {
    af_cluster_mesh mesh;
    if (argc != 2 || !af_cluster_load(argv[1], &mesh))
        return 1;
    uint32_t simplified = 0;
    bool valid = true;
    for (uint32_t i = 0; i < mesh.region_count && valid; ++i) {
        const af_cluster_region *r = &mesh.regions[i];
        if (!r->coarse_count)
            continue;
        uint64_t fine[MAX_REGION_INDICES], coarse[MAX_REGION_INDICES];
        size_t a = boundary(&mesh, r->fine_first, r->fine_count, fine);
        size_t b = boundary(&mesh, r->coarse_first, r->coarse_count, coarse);
        valid = a == b && !memcmp(fine, coarse, a * sizeof(*fine));
        if (!valid)
            fprintf(stderr, "Region %u changed its boundary between LODs.\n", i);
        ++simplified;
    }
    printf("Checked exact boundary edges for %u simplified regions.\n", simplified);
    af_cluster_free(&mesh);
    return valid && simplified ? 0 : 1;
}

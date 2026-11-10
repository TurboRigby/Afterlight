/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_CLUSTER_H
#define AF_CLUSTER_H

#include <afterlight/asset.h>

#define AF_CLUSTER_TRIANGLES 128u

typedef struct af_cluster {
    af_vec3 minimum;
    uint32_t first_index;
    af_vec3 maximum;
    uint32_t index_count;
} af_cluster;

#define AF_CLUSTER_REGION_LEAVES 8u

typedef struct af_cluster_region {
    af_vec3 minimum;
    uint32_t fine_first;
    af_vec3 maximum;
    uint32_t fine_count;
    uint32_t coarse_first, coarse_count;
    float error;
} af_cluster_region;

typedef struct af_cluster_mesh {
    af_mesh_data mesh;
    af_cluster *clusters;
    uint32_t count, fine_count;
    af_cluster_region *regions;
    uint32_t region_count;
} af_cluster_mesh;

bool af_cluster_build(const af_mesh_data *, af_cluster_mesh *);
bool af_cluster_build_lods(af_cluster_mesh *);
void af_cluster_free(af_cluster_mesh *);
bool af_cluster_save(const char *path, const af_cluster_mesh *);
bool af_cluster_load(const char *path, af_cluster_mesh *);

#endif

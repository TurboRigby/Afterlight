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

typedef struct af_cluster_mesh {
    af_mesh_data mesh;
    af_cluster *clusters;
    uint32_t count;
} af_cluster_mesh;

bool af_cluster_build(const af_mesh_data *, af_cluster_mesh *);
void af_cluster_free(af_cluster_mesh *);
bool af_cluster_save(const char *path, const af_cluster_mesh *);
bool af_cluster_load(const char *path, af_cluster_mesh *);

#endif

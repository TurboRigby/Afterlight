/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_CLUSTER_STREAM_H
#define AF_CLUSTER_STREAM_H

#include "cluster.h"

#define AF_CLUSTER_RESIDENT_PAGES 24u
#define AF_CLUSTER_STREAM_ASSETS 8u

typedef struct af_cluster_stream_region {
    af_vec3 minimum;
    uint32_t coarse_first;
    af_vec3 maximum;
    uint32_t coarse_count;
    float error;
    uint32_t page_vertices, page_indices, page_clusters;
} af_cluster_stream_region;

typedef struct af_cluster_stream af_cluster_stream;

bool af_cluster_save_stream(const char *path, const af_cluster_mesh *);
af_cluster_stream *af_cluster_open_stream(const char *path, af_cluster_mesh *coarse);
void af_cluster_close_stream(af_cluster_stream *);
uint32_t af_cluster_stream_region_count(const af_cluster_stream *);
const af_cluster_stream_region *af_cluster_stream_region_at(const af_cluster_stream *, uint32_t);
bool af_cluster_request_page(af_cluster_stream *, uint32_t);
/* A returned result with zero clusters represents a read or validation failure. */
bool af_cluster_take_page(af_cluster_stream *, uint32_t *, af_cluster_mesh *);

#endif

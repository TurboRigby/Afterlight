/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_COLLISION_MESH_H
#define AFTERLIGHT_COLLISION_MESH_H
#include <afterlight/asset.h>
struct af_scene;
typedef struct af_collision_mesh af_collision_mesh;
typedef struct af_triangle {
    af_vec3 a, b, c;
} af_triangle;
typedef struct af_collision_mesh_stats {
    size_t meshes, triangles, nodes, bytes;
} af_collision_mesh_stats;
#define AF_COLLISION_MESH_MAX_TRIANGLES 262144u
#define AF_COLLISION_MESH_MAX_ASSETS 64u
/* Immutable CPU meshes, main-thread refcounts. Scene clones share storage. */
bool af_scene_set_collision_mesh(struct af_scene *, const char *name, const af_mesh_data *,
                                 af_asset_error *);
bool af_scene_prepare_collision_meshes(struct af_scene *, af_asset_error *);
const af_collision_mesh *af_scene_collision_mesh(const struct af_scene *, const char *name);
af_collision_mesh_stats af_scene_collision_mesh_stats(const struct af_scene *);
/* Local-space BVH query. Returns total candidates; writes up to capacity.
   Triangle indices are stable within this immutable mesh, not source indices. */
size_t af_collision_mesh_query(const af_collision_mesh *, af_vec3 low, af_vec3 high, uint32_t *indices,
                               size_t capacity);
bool af_collision_mesh_triangle(const af_collision_mesh *, uint32_t, af_triangle *);
bool af_collision_mesh_bounds(const af_collision_mesh *, af_vec3 *low, af_vec3 *high);
#endif

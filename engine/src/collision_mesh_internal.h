/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_COLLISION_MESH_INTERNAL_H
#define AF_COLLISION_MESH_INTERNAL_H
#include <afterlight/collision_mesh.h>
typedef struct cm_slot {char name[64],path[4096];af_collision_mesh *mesh;} cm_slot;
void cm_retain(af_collision_mesh*);
void cm_release(af_collision_mesh*);
#endif

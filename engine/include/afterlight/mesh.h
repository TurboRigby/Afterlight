/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_MESH_H
#define AFTERLIGHT_MESH_H
#include <stdint.h>
#include <afterlight/math.h>

#define AF_MESH_NAME_CAPACITY 64

typedef struct af_color { float r, g, b, a; } af_color;
/* Right-handed, Y-up positions, linear vertex RGBA, normals and image UVs.
   Zero normals fall back to a geometric normal. The scene pipeline is opaque. */
typedef struct af_vertex {
    af_vec3 position;
    af_color color;
    af_vec3 normal; /* Zero normals use a geometric normal in the lit shader. */
    af_vec2 uv;
    uint16_t joints[4];
    af_vec4 weights; /* Zero sum: static. Otherwise four normalized influences. */
} af_vertex;
typedef struct af_mesh_data {
    const af_vertex *vertices;
    uint32_t vertex_count;
    const uint32_t *indices;
    uint32_t index_count;
} af_mesh_data;
#endif

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_SANDBOX_CUBE_H
#define AFTERLIGHT_SANDBOX_CUBE_H
#include <afterlight/mesh.h>
#include <string.h>

/* Four vertices per face keep each face a single color; indices reuse them
   between its two triangles. Front faces are deliberately submitted first. */
static const af_vertex cube_vertices[] = {{.position = {-0.65f, -0.65f, 0.65f}, .color = {1.00f, 0.28f, 0.12f, 1}},
                                          {.position = {0.65f, -0.65f, 0.65f}, .color = {1.00f, 0.28f, 0.12f, 1}},
                                          {.position = {0.65f, 0.65f, 0.65f}, .color = {1.00f, 0.28f, 0.12f, 1}},
                                          {.position = {-0.65f, 0.65f, 0.65f}, .color = {1.00f, 0.28f, 0.12f, 1}},
                                          {.position = {0.65f, -0.65f, -0.65f}, .color = {0.28f, 0.12f, 0.50f, 1}},
                                          {.position = {-0.65f, -0.65f, -0.65f}, .color = {0.28f, 0.12f, 0.50f, 1}},
                                          {.position = {-0.65f, 0.65f, -0.65f}, .color = {0.28f, 0.12f, 0.50f, 1}},
                                          {.position = {0.65f, 0.65f, -0.65f}, .color = {0.28f, 0.12f, 0.50f, 1}},
                                          {.position = {-0.65f, -0.65f, -0.65f}, .color = {0.12f, 0.65f, 0.58f, 1}},
                                          {.position = {-0.65f, -0.65f, 0.65f}, .color = {0.12f, 0.65f, 0.58f, 1}},
                                          {.position = {-0.65f, 0.65f, 0.65f}, .color = {0.12f, 0.65f, 0.58f, 1}},
                                          {.position = {-0.65f, 0.65f, -0.65f}, .color = {0.12f, 0.65f, 0.58f, 1}},
                                          {.position = {0.65f, -0.65f, 0.65f}, .color = {0.50f, 0.22f, 0.85f, 1}},
                                          {.position = {0.65f, -0.65f, -0.65f}, .color = {0.50f, 0.22f, 0.85f, 1}},
                                          {.position = {0.65f, 0.65f, -0.65f}, .color = {0.50f, 0.22f, 0.85f, 1}},
                                          {.position = {0.65f, 0.65f, 0.65f}, .color = {0.50f, 0.22f, 0.85f, 1}},
                                          {.position = {-0.65f, 0.65f, 0.65f}, .color = {1.00f, 0.64f, 0.18f, 1}},
                                          {.position = {0.65f, 0.65f, 0.65f}, .color = {1.00f, 0.64f, 0.18f, 1}},
                                          {.position = {0.65f, 0.65f, -0.65f}, .color = {1.00f, 0.64f, 0.18f, 1}},
                                          {.position = {-0.65f, 0.65f, -0.65f}, .color = {1.00f, 0.64f, 0.18f, 1}},
                                          {.position = {-0.65f, -0.65f, -0.65f}, .color = {0.12f, 0.28f, 0.55f, 1}},
                                          {.position = {0.65f, -0.65f, -0.65f}, .color = {0.12f, 0.28f, 0.55f, 1}},
                                          {.position = {0.65f, -0.65f, 0.65f}, .color = {0.12f, 0.28f, 0.55f, 1}},
                                          {.position = {-0.65f, -0.65f, 0.65f}, .color = {0.12f, 0.28f, 0.55f, 1}}};
static const uint32_t cube_indices[] = {0,  1,  2,  2,  3,  0,  4,  5,  6,  6,  7,  4,  8,  9,  10, 10, 11, 8,
                                        12, 13, 14, 14, 15, 12, 16, 17, 18, 18, 19, 16, 20, 21, 22, 22, 23, 20};
static inline af_mesh_data cube_mesh_data(void) {
    static af_vertex vertices[24];
    memcpy(vertices, cube_vertices, sizeof(vertices));
    const af_vec3 normals[] = {{0, 0, 1}, {0, 0, -1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const af_vec2 uvs[] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    for (unsigned i = 0; i < 24; ++i) {
        vertices[i].normal = normals[i / 4];
        vertices[i].uv = uvs[i % 4];
    }
    return (af_mesh_data){vertices, 24, cube_indices, 36};
}
#endif

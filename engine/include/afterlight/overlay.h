/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_OVERLAY_H
#define AFTERLIGHT_OVERLAY_H
#include <stddef.h>
#include <stdint.h>
#include <afterlight/math.h>

typedef struct af_rect { float x, y, width, height; } af_rect;
typedef struct af_overlay_vertex {
    af_vec2 position;
    af_vec2 uv;
    uint8_t color[4];
} af_overlay_vertex;
typedef struct af_overlay_command {
    af_rect clip;
    uint32_t first_index;
    uint32_t index_count;
    uint32_t texture; /* 0 or 1: font atlas; >=2: uploaded overlay texture. */
} af_overlay_command;
typedef struct af_overlay_frame {
    const af_overlay_vertex *vertices;
    const uint32_t *indices;
    const af_overlay_command *commands;
    uint32_t vertex_count, index_count, command_count;
    float width, height; /* Logical window dimensions, not framebuffer pixels. */
} af_overlay_frame;
#endif

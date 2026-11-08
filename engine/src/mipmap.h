/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_MIPMAP_H
#define AF_MIPMAP_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define AF_MAX_MIPS 14
/* Packed RGBA8 levels; caller frees pixels. Offsets are four-byte aligned. */
typedef struct af_mip_chain {
    unsigned char *pixels;
    size_t bytes, offsets[AF_MAX_MIPS];
    uint32_t count, widths[AF_MAX_MIPS], heights[AF_MAX_MIPS];
} af_mip_chain;
bool af_mip_chain_build(const unsigned char *rgba, uint32_t width, uint32_t height, bool srgb, bool mipmaps,
                        af_mip_chain *out);
#endif

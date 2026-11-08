/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "mipmap.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static float decode(float x) {
    return x <= 0.04045f ? x / 12.92f : powf((x + 0.055f) / 1.055f, 2.4f);
}
static unsigned char encode(float x, bool srgb) {
    if (srgb)
        x = x <= 0.0031308f ? 12.92f * x : 1.055f * powf(x, 1 / 2.4f) - 0.055f;
    return (unsigned char)lroundf(fminf(1, fmaxf(0, x)) * 255);
}
bool af_mip_chain_build(const unsigned char *rgba, uint32_t width, uint32_t height, bool srgb, bool mipmaps,
                        af_mip_chain *out) {
    if (!out)
        return false;
    *out = (af_mip_chain){0};
    if (!rgba || !width || !height || width > 8192 || height > 8192)
        return false;
    uint32_t w = width, h = height;
    do {
        uint32_t i = out->count++;
        out->widths[i] = w;
        out->heights[i] = h;
        out->offsets[i] = out->bytes;
        out->bytes += (size_t)w * h * 4;
        if (!mipmaps || (w == 1 && h == 1))
            break;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    } while (out->count < AF_MAX_MIPS);
    out->pixels = malloc(out->bytes);
    if (!out->pixels) {
        *out = (af_mip_chain){0};
        return false;
    }
    memcpy(out->pixels, rgba, (size_t)width * height * 4);
    float table[256];
    for (unsigned i = 0; i < 256; ++i)
        table[i] = srgb ? decode((float)i / 255) : (float)i / 255;
    for (uint32_t level = 1; level < out->count; ++level) {
        uint32_t sw = out->widths[level - 1], sh = out->heights[level - 1];
        w = out->widths[level];
        h = out->heights[level];
        const unsigned char *source = out->pixels + out->offsets[level - 1];
        unsigned char *dest = out->pixels + out->offsets[level];
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x) {
                double left = (double)x * sw / w, right = (double)(x + 1) * sw / w, top = (double)y * sh / h,
                       bottom = (double)(y + 1) * sh / h;
                double sum[4] = {0}, weight = 0;
                for (uint32_t sy = (uint32_t)top; sy < (uint32_t)ceil(bottom) && sy < sh; ++sy)
                    for (uint32_t sx = (uint32_t)left; sx < (uint32_t)ceil(right) && sx < sw; ++sx) {
                        double a = (fmin(right, sx + 1.0) - fmax(left, sx)) * (fmin(bottom, sy + 1.0) - fmax(top, sy));
                        const unsigned char *p = source + ((size_t)sy * sw + sx) * 4;
                        for (unsigned c = 0; c < 4; ++c)
                            sum[c] += a * (c == 3 ? (double)p[c] / 255 : table[p[c]]);
                        weight += a;
                    }
                for (unsigned c = 0; c < 4; ++c)
                    dest[((size_t)y * w + x) * 4 + c] = encode((float)(sum[c] / weight), srgb && c < 3);
            }
    }
    return true;
}

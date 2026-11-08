/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "mipmap.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Mip check %d: %s\n", __LINE__, #c);                                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    unsigned char bw[] = {0, 0, 0, 255, 255, 255, 255, 255};
    af_mip_chain m;
    CHECK(af_mip_chain_build(bw, 2, 1, true, true, &m));
    CHECK(m.count == 2 && m.bytes == 12 && m.widths[1] == 1 && m.heights[1] == 1);
    CHECK(m.pixels[8] >= 187 && m.pixels[8] <= 188 && m.pixels[11] == 255);
    free(m.pixels);
    CHECK(af_mip_chain_build(bw, 2, 1, false, true, &m));
    CHECK(m.pixels[8] == 128);
    free(m.pixels);
    unsigned char odd[3 * 5 * 4];
    for (unsigned i = 0; i < sizeof(odd); i += 4) {
        odd[i] = odd[i + 1] = odd[i + 2] = 0;
        odd[i + 3] = 255;
    }
    odd[56] = 255;
    CHECK(af_mip_chain_build(odd, 3, 5, false, true, &m));
    CHECK(m.count == 3 && m.widths[1] == 1 && m.heights[1] == 2 && m.widths[2] == 1 && m.heights[2] == 1);
    CHECK(m.pixels[m.offsets[2]] >= 16 && m.pixels[m.offsets[2]] <= 18);
    free(m.pixels);
    CHECK(af_mip_chain_build(bw, 2, 1, true, false, &m) && m.count == 1 && m.bytes == 8);
    free(m.pixels);
    CHECK(!af_mip_chain_build(bw, 0, 1, true, true, &m));
    CHECK(!af_mip_chain_build(bw, 8193, 1, true, true, &m));
    puts("Mips: sRGB/linear averages, alpha, NPOT edges, one-dimensional levels and atlas isolation passed.");
    return 0;
}

/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/asset.h>
#include "render/ibl.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "HDR %d: %s\n", __LINE__, #c);                                                             \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_hdr_file hdr;
    af_asset_error error;
    char path[4096];
    snprintf(path, sizeof(path), "%s/panorama.hdr", argv[1]);
    CHECK(af_hdr_file_load(path, &hdr, &error) && hdr.width == 64 && hdr.height == 32);
    float peak = 0;
    for (size_t i = 0; i < (size_t)hdr.width * hdr.height * 4; ++i) {
        CHECK(isfinite(hdr.rgba[i]) && hdr.rgba[i] >= 0);
        peak = fmaxf(peak, hdr.rgba[i]);
    }
    CHECK(peak > 10);
    af_vec3 a = af_ibl_sample(hdr.rgba, hdr.width, hdr.height, (af_vec3){-1, 0, .000001f}),
            b = af_ibl_sample(hdr.rgba, hdr.width, hdr.height, (af_vec3){-1, 0, -.000001f});
    CHECK(af_vec3_length(af_vec3_sub(a, b)) < .001f);
    af_ibl_images images;
    CHECK(af_ibl_bake_hdr(hdr.rgba, hdr.width, hdr.height, &images));
    CHECK(images.specular && images.diffuse && images.brdf);
    af_ibl_free(&images);
    hdr.rgba[0] = NAN;
    CHECK(!af_ibl_bake_hdr(hdr.rgba, hdr.width, hdr.height, &images) && !images.specular);
    af_hdr_file_free(&hdr);
    CHECK(!hdr.rgba && !hdr.width);
    FILE *file = fopen("bad.hdr", "wb");
    CHECK(file && fputs("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4096 +X 8192\n", file) >= 0 && fclose(file) == 0);
    CHECK(!af_hdr_file_load("bad.hdr", &hdr, &error) && !hdr.rgba);
    file = fopen("bad.hdr", "wb");
    CHECK(file && fputs("#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n", file) >= 0 && fclose(file) == 0);
    CHECK(!af_hdr_file_load("bad.hdr", &hdr, &error));
    snprintf(path, sizeof(path), "%s/green.png", argv[1]);
    CHECK(!af_hdr_file_load(path, &hdr, &error));
    CHECK(!af_hdr_file_load(NULL, &hdr, &error));
    puts("HDR: RGBE values >1, seam sampling, convolution, invalid/truncated/oversized/non-HDR inputs and cleanup "
         "passed.");
    return 0;
}

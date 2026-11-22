/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "spv_to_msl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <input.spv> <output.metal> <vert|frag|comp> <scene|post> <entry_name>\n", argv[0]);
        return 1;
    }
    const char *in_path = argv[1];
    const char *out_path = argv[2];
    const char *stage_str = argv[3];
    const char *profile_str = argv[4];
    const char *entry_name = argv[5];

    af_msl_stage stage;
    if (!strcmp(stage_str, "vert"))
        stage = AF_MSL_STAGE_VERTEX;
    else if (!strcmp(stage_str, "frag"))
        stage = AF_MSL_STAGE_FRAGMENT;
    else if (!strcmp(stage_str, "comp"))
        stage = AF_MSL_STAGE_COMPUTE;
    else {
        fprintf(stderr, "Invalid stage: %s\n", stage_str);
        return 1;
    }

    af_msl_profile profile;
    if (!strcmp(profile_str, "scene"))
        profile = AF_MSL_PROFILE_SCENE;
    else if (!strcmp(profile_str, "post"))
        profile = AF_MSL_PROFILE_POST_UI;
    else {
        fprintf(stderr, "Invalid profile: %s\n", profile_str);
        return 1;
    }

    FILE *f = fopen(in_path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open SPIR-V input: %s\n", in_path);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 1;
    }
    long size = ftell(f);
    if (size <= 0 || (size % 4) != 0 || fseek(f, 0, SEEK_SET) != 0) {
        fprintf(stderr, "Invalid SPIR-V size (%ld): %s\n", size, in_path);
        fclose(f);
        return 1;
    }
    uint32_t *words = malloc((size_t)size);
    if (!words || fread(words, 1, (size_t)size, f) != (size_t)size) {
        free(words);
        fclose(f);
        return 1;
    }
    fclose(f);

    char *msl = NULL;
    char error[512] = {0};
    bool ok = af_spv_to_msl(words, (size_t)size / 4, stage, profile, entry_name, stage == AF_MSL_STAGE_VERTEX, &msl,
                            error, sizeof(error));
    free(words);
    if (!ok) {
        fprintf(stderr, "SPIR-V -> MSL failed for %s: %s\n", in_path, error);
        return 1;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "Cannot open MSL output: %s\n", out_path);
        free(msl);
        return 1;
    }
    size_t len = strlen(msl);
    bool wrote = fwrite(msl, 1, len, out) == len;
    fclose(out);
    free(msl);
    return wrote ? 0 : 1;
}

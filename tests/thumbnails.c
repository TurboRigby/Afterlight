/* SPDX-License-Identifier: MPL-2.0 */
#include "thumbnail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Thumbnail line %d: %s\n", __LINE__, #c);                                                  \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_scene *scene = af_scene_create();
    CHECK(scene);
    unsigned char rgba[64 * 64 * 4], other[sizeof(rgba)];
    af_scene_asset asset = {.name = "green", .kind = AF_ASSET_TEXTURE};
    snprintf(asset.path, sizeof(asset.path), "%s/green.png", argv[1]);
    CHECK(ed_thumbnail_generate(scene, &asset, NULL, rgba));
    size_t center = (32 * 64 + 32) * 4;
    CHECK(abs((int)rgba[center] - 20) <= 1 && abs((int)rgba[center + 1] - 245) <= 1 &&
          abs((int)rgba[center + 2] - 50) <= 1);
    CHECK(rgba[0] < 50 && rgba[3] == 255);
    asset.kind = AF_ASSET_MESH;
    snprintf(asset.name, sizeof(asset.name), "cube");
    snprintf(asset.path, sizeof(asset.path), "Built-in mesh");
    CHECK(ed_thumbnail_generate(scene, &asset, NULL, rgba));
    unsigned drawn = 0;
    for (unsigned i = 0; i < 64 * 64; ++i)
        if (rgba[i * 4] > 80)
            ++drawn;
    CHECK(drawn > 300 && drawn < 3000);
    memcpy(other, rgba, sizeof(rgba));
    snprintf(asset.path, sizeof(asset.path), "%s/triangle.gltf", argv[1]);
    CHECK(ed_thumbnail_generate(scene, &asset, NULL, rgba));
    CHECK(memcmp(other, rgba, sizeof(rgba)));
    af_material material = af_material_default();
    material.base_color = (af_color){.8f, .1f, .05f, 1};
    CHECK(ed_thumbnail_generate(scene, NULL, &material, rgba));
    CHECK(rgba[center] > rgba[center + 1] + 40);
    memcpy(other, rgba, sizeof(rgba));
    material.physically_based = true;
    material.metallic = 1;
    material.roughness = .1f;
    CHECK(ed_thumbnail_generate(scene, NULL, &material, rgba));
    CHECK(memcmp(other, rgba, sizeof(rgba)));
    material.emission = (af_vec3){0, 0, 1};
    material.emission_strength = 8;
    CHECK(ed_thumbnail_generate(scene, NULL, &material, rgba));
    CHECK(rgba[center + 2] > 200);
    strcpy(asset.path, "missing-thumbnail.obj");
    CHECK(!ed_thumbnail_generate(scene, &asset, NULL, rgba));
    CHECK(!ed_thumbnail_generate(scene, NULL, NULL, rgba));
    af_scene_destroy(scene);
    puts("Thumbnails: texture color/aspect, actual cube/glTF geometry, material changes/emission and failed source "
         "passed.");
    return 0;
}

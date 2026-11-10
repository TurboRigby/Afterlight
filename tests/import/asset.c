/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/asset.h>
#include <afterlight/scene.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Asset check %d: %s\n", __LINE__, #c);                                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static bool write_text(const char *text) {
    FILE *f = fopen("bad.obj", "wb");
    if (!f)
        return false;
    bool ok = fputs(text, f) >= 0;
    return fclose(f) == 0 && ok;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_asset_error error;
    af_mesh_file mesh;
    CHECK(af_mesh_file_load_obj(argv[1], &mesh, &error));
    CHECK(mesh.data.vertex_count == 6 && mesh.data.index_count == 6);
    CHECK(mesh.data.vertices[0].position.x == -1 && mesh.data.vertices[0].uv.y == 1 &&
          mesh.data.vertices[0].normal.z == 1);
    for (uint32_t i = 0; i < mesh.data.index_count; ++i)
        CHECK(mesh.data.indices[i] < mesh.data.vertex_count);
    af_mesh_file_free(&mesh);
    CHECK(!mesh.data.vertices);
    CHECK(write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    CHECK(af_mesh_file_load_obj("bad.obj", &mesh, &error));
    CHECK(mesh.data.vertices[0].normal.z == 1);
    af_mesh_file_free(&mesh);
    CHECK(write_text("v 0 0 0\nf 1 2 3\n"));
    CHECK(!af_mesh_file_load_obj("bad.obj", &mesh, &error) && error.message[0] && !mesh.data.vertices);
    CHECK(write_text("v nan 0 0\nf 1 1 1"));
    CHECK(!af_mesh_file_load_obj("bad.obj", &mesh, &error));
    CHECK(write_text("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 2 3"));
    CHECK(!af_mesh_file_load_obj("bad.obj", &mesh, &error));
    unsigned char tga[22] = {0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 32, 0x28, 30, 20, 10, 255};
    FILE *f = fopen("pixel.tga", "wb");
    CHECK(f && fwrite(tga, 1, sizeof(tga), f) == sizeof(tga) && fclose(f) == 0);
    af_image_file image;
    CHECK(af_image_file_load("pixel.tga", &image, &error));
    CHECK(image.width == 1 && image.height == 1 && image.rgba[0] == 10 && image.rgba[1] == 20);
    af_image_file_free(&image);
    CHECK(!af_image_file_load("bad.obj", &image, &error) && !image.rgba);
    af_scene *s = af_scene_create();
    CHECK(s && af_scene_set_asset(s, "quad", AF_ASSET_MESH, argv[1]));
    CHECK(!af_scene_set_asset(s, "", AF_ASSET_MESH, argv[1]));
    CHECK(af_scene_set_asset(s, "texture", AF_ASSET_TEXTURE, "pixel.tga"));
    CHECK(af_scene_asset_count(s) == 2 && af_scene_save(s, "assets.afterlight.json", NULL));
    af_scene *copy = af_scene_load("assets.afterlight.json", NULL);
    CHECK(copy && af_scene_asset_count(copy) == 2);
    CHECK(strstr(af_scene_find_asset(copy, "quad")->path, "quad.obj"));
    af_scene *clone = af_scene_clone(copy);
    af_scene_destroy(copy);
    CHECK(clone && af_scene_asset_count(clone) == 2);
    af_scene_destroy(clone);
    af_scene_destroy(s);
    puts("Assets: OBJ topology/indices/normals/UVs, invalid files, images, manifest round trip passed.");
    return 0;
}

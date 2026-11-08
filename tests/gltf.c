/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/asset.h>
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "glTF %d: %s\n", __LINE__, #c);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static bool save(const cJSON *json, const char *path) {
    char *s = cJSON_Print(json);
    FILE *f = fopen(path, "wb");
    bool ok = s && f && fputs(s, f) >= 0;
    if (f)
        ok = fclose(f) == 0 && ok;
    cJSON_free(s);
    return ok;
}
static char *read(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *s = calloc(32768, 1);
    size_t n = s ? fread(s, 1, 32767, f) : 0;
    fclose(f);
    if (!n) {
        free(s);
        return NULL;
    }
    return s;
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    const char *files[] = {"triangle.gltf", "triangle.glb", "triangle-embedded.gltf"};
    af_asset_error error;
    af_mesh_file mesh;
    char path[4096];
    for (unsigned i = 0; i < 3; ++i) {
        snprintf(path, sizeof(path), "%s/%s", argv[1], files[i]);
        CHECK(af_mesh_file_load(path, &mesh, &error));
        CHECK(mesh.data.vertex_count == 6 && mesh.data.index_count == 6);
        CHECK(mesh.data.vertices[0].position.x == 1 && mesh.data.vertices[0].position.y == 2 &&
              mesh.data.vertices[0].position.z == 3);
        CHECK(mesh.data.vertices[1].position.x == 3 && mesh.data.vertices[1].uv.x == 1);
        CHECK(fabsf(mesh.data.vertices[0].color.r - .5f) < .001f && mesh.data.vertices[0].normal.z > .99f);
        af_vec3 a = mesh.data.vertices[3].position, b = mesh.data.vertices[4].position,
                c = mesh.data.vertices[5].position;
        CHECK(af_vec3_cross(af_vec3_sub(b, a), af_vec3_sub(c, a)).z > 0);
        af_mesh_file_free(&mesh);
    }
    CHECK(af_mesh_file_load_gltf(argv[2], &mesh, &error) && mesh.data.vertex_count > 500);
    af_mesh_file_free(&mesh);
    snprintf(path, sizeof(path), "%s/triangle-embedded.gltf", argv[1]);
    char *text = read(path);
    CHECK(text);
    cJSON *root = cJSON_Parse(text);
    free(text);
    CHECK(root);
    cJSON *bad = cJSON_Duplicate(root, 1),
          *buffer = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(bad, "buffers"), 0);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(buffer, "byteLength"), 4294967296.0);
    CHECK(save(bad, "oversized.gltf") && !af_mesh_file_load_gltf("oversized.gltf", &mesh, &error) &&
          !mesh.data.vertices);
    cJSON_Delete(bad);
    bad = cJSON_Duplicate(root, 1);
    cJSON *node = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(bad, "nodes"), 1);
    cJSON_SetNumberValue(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(node, "scale"), 0), 0);
    CHECK(save(bad, "singular.gltf") && !af_mesh_file_load_gltf("singular.gltf", &mesh, &error));
    cJSON_Delete(bad);
    bad = cJSON_Duplicate(root, 1);
    buffer = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(bad, "buffers"), 0);
    cJSON_SetValuestring(cJSON_GetObjectItemCaseSensitive(buffer, "uri"), "does-not-exist.bin");
    CHECK(save(bad, "missing.gltf") && !af_mesh_file_load_gltf("missing.gltf", &mesh, &error));
    cJSON_Delete(bad);
    bad = cJSON_Duplicate(root, 1);
    cJSON *view = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(bad, "bufferViews"), 0);
    cJSON_AddNumberToObject(view, "byteOffset", 10000000);
    CHECK(save(bad, "bounds.gltf") && !af_mesh_file_load_gltf("bounds.gltf", &mesh, &error));
    cJSON_Delete(bad);
    cJSON_Delete(root);
    FILE *f = fopen("truncated.glb", "wb");
    CHECK(f && fputs("glTF", f) >= 0 && fclose(f) == 0 && !af_mesh_file_load_gltf("truncated.glb", &mesh, &error));
    CHECK(!af_mesh_file_load_gltf(NULL, &mesh, &error) && !mesh.data.vertices && !mesh.data.indices);
    puts("glTF: external/data URI/GLB buffers, nested/mirrored transforms, normals, UVs, normalized colors, material "
         "factors, real beacon and malformed/bounded-input rejection passed.");
    return 0;
}

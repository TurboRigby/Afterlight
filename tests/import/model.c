/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/model.h>
#include "document/document.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Model %d: %s (%s)\n", __LINE__, #c, error.message);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    af_asset_error error = {0};
    CHECK(argc == 2);
    CHECK(af_asset_is_model_path("File.GLB") && af_asset_is_model_path("File.GLTF#image=1") &&
          !af_asset_is_model_path("file.obj"));
    char path[4096];
    snprintf(path, sizeof(path), "%s/rich.gltf", argv[1]);
    af_model_file model;
    CHECK(af_model_file_load(path, &model, &error));
    CHECK(model.node_count == 3 && model.primitive_count == 2 && model.image_count == 3 && model.dependency_count == 5);
    CHECK(model.nodes[1].parent == 0 && model.nodes[1].transform.scale.x == 2 && model.nodes[2].transform.scale.x < 0);
    CHECK(model.primitives[0].base_image == 0 && model.primitives[0].normal_image == 1 &&
          model.primitives[0].surface_image == 2 && fabsf(model.primitives[0].material.roughness - .8f) < .001f &&
          model.primitives[1].material.metallic > .8f);
    CHECK(model.primitives[0].mesh.data.vertices[0].position.x == 0 && model.images[0].rgba[0] == 220 &&
          model.images[2].rgba[1] == 128);
    af_model_file_free(&model);
    af_scene *scene = af_scene_create();
    CHECK(scene);
    af_object_id root;
    CHECK(af_scene_import_model(scene, "imported", path, &root, &error));
    CHECK(af_scene_object_count(scene) == 8 && af_scene_asset_count(scene) == 5);
    const af_scene_object *part = NULL;
    for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
        const af_scene_object *o = af_scene_object_at(scene, i);
        if (!strcmp(o->mesh, "imported_m0_p0")) {
            part = o;
            break;
        }
    }
    CHECK(part && part->parent && part->material.texture[0] && part->material.normal_map[0] &&
          part->material.metallic_roughness_map[0]);
    af_mat4 matrix;
    CHECK(af_scene_world_matrix(scene, part->id, &matrix) && matrix.elements[12] == 1 && matrix.elements[13] == 2 &&
          matrix.elements[0] == 2);
    CHECK(af_scene_save(scene, "model.afterlight.json", NULL));
    af_scene *copy = af_scene_load("model.afterlight.json", NULL);
    CHECK(copy && af_scene_object_count(copy) == 8 && af_scene_asset_count(copy) == 5);
    af_scene_destroy(copy);
    const af_scene_asset *mesh = af_scene_find_asset(scene, "imported_m0_p0");
    af_mesh_file geometry;
    CHECK(mesh && af_mesh_file_load(mesh->path, &geometry, &error) && geometry.data.index_count == 3);
    af_mesh_file_free(&geometry);
    const af_scene_asset *image = af_scene_find_asset(scene, "imported_i0_0");
    af_image_file pixels;
    CHECK(image && af_image_file_load(image->path, &pixels, &error) && pixels.rgba[0] == 220);
    af_image_file_free(&pixels);
    size_t count = af_scene_object_count(scene);
    CHECK(!af_scene_import_model(scene, "bad", "missing.gltf", NULL, &error) && af_scene_object_count(scene) == count);
    af_scene_destroy(scene);
    snprintf(path, sizeof(path), "%s/rich.glb", argv[1]);
    CHECK(af_model_file_load(path, &model, &error) && model.images[1].rgba[2] == 255);
    af_model_file_free(&model);
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_import_mesh(&d, "rich", path));
    CHECK(af_scene_object_count(d.scene) == 8 && ed_document_undo(&d) && af_scene_object_count(d.scene) == 0 &&
          ed_document_redo(&d) && af_scene_object_count(d.scene) == 8);
    ed_document_destroy(&d);
    puts("Model: multiple materials, local hierarchy/mirroring, external/embedded maps, PBR factors, selectors, strict "
         "scene roundtrip, transactional import and undo passed.");
    return 0;
}

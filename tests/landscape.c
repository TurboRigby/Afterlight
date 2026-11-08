/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include <afterlight/landscape.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #cond); \
        return 1; \
    } \
} while (0)

int main(void) {
    puts("Running landscape system unit tests...");

    /* 1. Creation and dimensions */
    af_landscape *ls = af_landscape_create(4, 4, 2.0f);
    CHECK(ls != NULL);
    CHECK(ls->chunk_count_x == 4);
    CHECK(ls->chunk_count_z == 4);
    CHECK(ls->chunk_count == 16);
    CHECK(ls->vertex_spacing == 2.0f);
    CHECK(ls->layer_count == 4);

    /* Flat terrain initially at y = 0 */
    float h = af_landscape_get_height(ls, 0.0f, 0.0f);
    CHECK(fabsf(h) < 1e-4f);

    af_vec3 norm;
    CHECK(af_landscape_get_normal(ls, 0.0f, 0.0f, &norm));
    CHECK(fabsf(norm.x) < 1e-3f);
    CHECK(fabsf(norm.y - 1.0f) < 1e-3f);
    CHECK(fabsf(norm.z) < 1e-3f);

    float weights[4];
    CHECK(af_landscape_get_layer_weights(ls, 0.0f, 0.0f, weights));
    CHECK(weights[0] > 0.99f); /* 100% grass by default */
    CHECK(weights[1] < 0.01f);

    /* 2. Sculpt raise and lower */
    af_vec3 sculpt_center = {0.0f, 0.0f, 0.0f};
    CHECK(af_landscape_sculpt_raise_lower(ls, sculpt_center, 8.0f, 5.0f, AF_FALLOFF_SMOOTH));

    float h_center = af_landscape_get_height(ls, 0.0f, 0.0f);
    CHECK(fabsf(h_center - 5.0f) < 0.05f);

    /* Outside radius should remain at 0 */
    float h_outside = af_landscape_get_height(ls, 20.0f, 20.0f);
    CHECK(fabsf(h_outside) < 1e-3f);

    /* Normal on slope should tilt outward */
    af_vec3 n_slope;
    CHECK(af_landscape_get_normal(ls, 4.0f, 0.0f, &n_slope));
    CHECK(n_slope.x > 0.05f); /* Sloping down to the right */

    /* Sculpt lower */
    CHECK(af_landscape_sculpt_raise_lower(ls, sculpt_center, 8.0f, -2.0f, AF_FALLOFF_SMOOTH));
    float h_lowered = af_landscape_get_height(ls, 0.0f, 0.0f);
    CHECK(fabsf(h_lowered - 3.0f) < 0.05f);

    /* 3. Sculpt smooth */
    float before_smooth = af_landscape_get_height(ls, 0.0f, 0.0f);
    CHECK(af_landscape_sculpt_smooth(ls, sculpt_center, 8.0f, 0.8f, AF_FALLOFF_SMOOTH));
    float after_smooth = af_landscape_get_height(ls, 0.0f, 0.0f);
    CHECK(after_smooth < before_smooth); /* Peak pulled down by smoothing with flat surroundings */

    /* 4. Sculpt flatten */
    CHECK(af_landscape_sculpt_flatten(ls, sculpt_center, 8.0f, 10.0f, 1.0f, AF_FALLOFF_FLAT));
    float h_flattened = af_landscape_get_height(ls, 0.0f, 0.0f);
    CHECK(fabsf(h_flattened - 10.0f) < 0.1f);

    /* 5. Material layer painting */
    CHECK(af_landscape_paint_layer(ls, sculpt_center, 6.0f, 1, 1.0f, AF_FALLOFF_SMOOTH)); /* Layer 1 = Dirt */
    CHECK(af_landscape_get_layer_weights(ls, 0.0f, 0.0f, weights));
    CHECK(weights[1] > 0.4f); /* Dirt layer was painted */
    float sum_w = weights[0] + weights[1] + weights[2] + weights[3];
    CHECK(fabsf(sum_w - 1.0f) < 0.02f); /* Normalized */

    /* 6. Raycast */
    af_vec3 ray_orig = {0.0f, 50.0f, 0.0f};
    af_vec3 ray_dir = {0.0f, -1.0f, 0.0f};
    af_vec3 hit_pt, hit_norm;
    CHECK(af_landscape_raycast(ls, ray_orig, ray_dir, &hit_pt, &hit_norm));
    CHECK(fabsf(hit_pt.x) < 0.01f);
    CHECK(fabsf(hit_pt.z) < 0.01f);
    CHECK(fabsf(hit_pt.y - h_flattened) < 0.1f);

    /* Angled raycast */
    af_vec3 angled_orig = {-20.0f, 30.0f, 0.0f};
    af_vec3 angled_dir = af_vec3_normalize((af_vec3){1.0f, -1.0f, 0.0f});
    CHECK(af_landscape_raycast(ls, angled_orig, angled_dir, &hit_pt, &hit_norm));
    float expected_h = af_landscape_get_height(ls, hit_pt.x, hit_pt.z);
    CHECK(fabsf(hit_pt.y - expected_h) < 0.1f);

    /* Missing raycast */
    af_vec3 miss_orig = {0.0f, -50.0f, 0.0f};
    af_vec3 miss_dir = {0.0f, -1.0f, 0.0f};
    CHECK(!af_landscape_raycast(ls, miss_orig, miss_dir, &hit_pt, &hit_norm));

    /* 7. Chunk Mesh Generation */
    size_t vert_count = af_landscape_chunk_vertex_count();
    size_t index_count = af_landscape_chunk_index_count();
    CHECK(vert_count == 289);
    CHECK(index_count == 1536);

    af_vertex *vertices = malloc(vert_count * sizeof(af_vertex));
    uint32_t *indices = malloc(index_count * sizeof(uint32_t));
    CHECK(vertices && indices);
    CHECK(af_landscape_build_chunk_mesh(ls, 0, vertices, indices));
    CHECK(vertices[0].normal.y > 0.5f);
    free(vertices);
    free(indices);

    /* 8. Heightmap Import / Export */
    float export_map[16 * 16];
    CHECK(af_landscape_export_heightmap(ls, export_map, 16, 16));
    CHECK(af_landscape_import_heightmap(ls, export_map, 16, 16, 0.0f, 15.0f));

    /* Procedural hills */
    CHECK(af_landscape_generate_procedural_hills(ls, 30.0f, 8.0f, 42));
    float h_hill1 = af_landscape_get_height(ls, -15.0f, -15.0f);
    float h_hill2 = af_landscape_get_height(ls, 15.0f, 15.0f);
    CHECK(h_hill1 != h_hill2); /* Heights vary procedurally */

    /* 9. Foliage Layers & Mesh Painting */
    int tree_layer = af_landscape_add_foliage_layer(ls, "Pine Tree", "pine_tree.obj");
    CHECK(tree_layer == 0);
    CHECK(ls->foliage_layer_count == 1);
    CHECK(ls->foliage_layers[0].visible);

    /* Placement rules */
    ls->foliage_layers[0].rules.density = 2.0f;
    ls->foliage_layers[0].rules.min_spacing = 1.0f;
    ls->foliage_layers[0].rules.collision_avoidance = true;

    /* Paint foliage */
    CHECK(af_landscape_paint_foliage(ls, 0, (af_vec3){0, 0, 0}, 15.0f, 1.0f, 12345));
    CHECK(ls->foliage_layers[0].total_instances > 0);
    size_t painted_count = ls->foliage_layers[0].total_instances;

    /* Erase foliage */
    CHECK(af_landscape_erase_foliage(ls, 0, (af_vec3){0, 0, 0}, 8.0f));
    CHECK(ls->foliage_layers[0].total_instances < painted_count);

    /* Procedural foliage population */
    CHECK(af_landscape_populate_procedural_foliage(ls, 0, 999));
    CHECK(ls->foliage_layers[0].total_instances > 10);

    /* Query foliage batches */
    af_foliage_batch batches[8];
    af_foliage_instance pool[2048];
    size_t total_inst = 0;
    af_camera cam = af_camera_default();
    cam.position = (af_vec3){0, 10, 0};
    size_t batch_count = af_landscape_query_foliage_batches(ls, &cam, (af_rect){0, 0, 800, 600},
                                                            batches, 8, pool, 2048, &total_inst);
    CHECK(batch_count == 1);
    CHECK(total_inst > 0);
    CHECK(!strcmp(batches[0].mesh_name, "pine_tree.obj"));

    /* Layer visibility toggle */
    CHECK(af_landscape_set_foliage_visible(ls, 0, false));
    batch_count = af_landscape_query_foliage_batches(ls, &cam, (af_rect){0, 0, 800, 600},
                                                     batches, 8, pool, 2048, &total_inst);
    CHECK(batch_count == 0); /* Hidden layer produces 0 batches! */
    CHECK(af_landscape_set_foliage_visible(ls, 0, true));

    /* 10. Custom Layer Textures & UV Scaling */
    snprintf(ls->layers[0].diffuse_texture, sizeof(ls->layers[0].diffuse_texture), "grass_diffuse.png");
    snprintf(ls->layers[0].normal_texture, sizeof(ls->layers[0].normal_texture), "grass_normal.png");
    ls->layers[0].uv_scale = 2.0f;
    ls->layers[0].roughness = 0.72f;
    ls->layers[0].metalness = 0.05f;

    snprintf(ls->layers[1].diffuse_texture, sizeof(ls->layers[1].diffuse_texture), "dirt_diffuse.png");
    snprintf(ls->layers[1].normal_texture, sizeof(ls->layers[1].normal_texture), "dirt_normal.png");
    ls->layers[1].uv_scale = 1.5f;

    /* Verify chunk mesh UVs scale with dominant layer */
    af_vertex v_test[AF_LANDSCAPE_CHUNK_VERTICES * AF_LANDSCAPE_CHUNK_VERTICES];
    uint32_t i_test[AF_LANDSCAPE_CHUNK_QUADS * AF_LANDSCAPE_CHUNK_QUADS * 6];
    CHECK(af_landscape_build_chunk_mesh(ls, 0, v_test, i_test));
    /* Delta wx between adjacent vertices is spacing (2.0m). With uv_scale=2.0 (0.5/m), delta uv.x is 1.0 */
    CHECK(fabsf((v_test[1].uv.x - v_test[0].uv.x) - 1.0f) < 1e-4f);

    /* 11. Scene Integration & Serialization */
    af_scene *scene = af_scene_create();
    CHECK(scene != NULL);
    CHECK(af_scene_get_landscape(scene) == NULL);

    CHECK(af_scene_set_landscape(scene, ls));
    CHECK(af_scene_get_landscape(scene) == ls);

    /* Clone scene */
    af_scene *cloned_scene = af_scene_clone(scene);
    CHECK(cloned_scene != NULL);
    const af_landscape *cloned_ls = af_scene_get_landscape(cloned_scene);
    CHECK(cloned_ls != NULL);
    CHECK(cloned_ls != ls); /* Deep clone */
    CHECK(cloned_ls->chunk_count == ls->chunk_count);
    CHECK(cloned_ls->foliage_layer_count == ls->foliage_layer_count);
    CHECK(cloned_ls->total_foliage_instances == ls->total_foliage_instances);
    CHECK(!strcmp(cloned_ls->layers[0].diffuse_texture, "grass_diffuse.png"));
    CHECK(!strcmp(cloned_ls->layers[0].normal_texture, "grass_normal.png"));
    CHECK(fabsf(cloned_ls->layers[0].uv_scale - 2.0f) < 1e-4f);

    /* Save and Load scene */
    const char *test_path = "landscape_test_scene.afterlight.json";
    af_scene_error err = {0};
    CHECK(af_scene_save(scene, test_path, &err));

    af_scene *loaded_scene = af_scene_load(test_path, &err);
    if (!loaded_scene) fprintf(stderr, "Load error: code=%d message='%s'\n", err.code, err.message);
    CHECK(loaded_scene != NULL);
    const af_landscape *loaded_ls = af_scene_get_landscape(loaded_scene);
    CHECK(loaded_ls != NULL);
    CHECK(loaded_ls->chunk_count == 16);
    CHECK(loaded_ls->foliage_layer_count == 1);
    CHECK(!strcmp(loaded_ls->foliage_layers[0].name, "Pine Tree"));
    CHECK(loaded_ls->total_foliage_instances > 0);

    /* Verify custom layer textures and properties persisted */
    CHECK(!strcmp(loaded_ls->layers[0].diffuse_texture, "grass_diffuse.png"));
    CHECK(!strcmp(loaded_ls->layers[0].normal_texture, "grass_normal.png"));
    CHECK(fabsf(loaded_ls->layers[0].uv_scale - 2.0f) < 1e-4f);
    CHECK(fabsf(loaded_ls->layers[0].roughness - 0.72f) < 1e-4f);
    CHECK(fabsf(loaded_ls->layers[0].metalness - 0.05f) < 1e-4f);
    CHECK(!strcmp(loaded_ls->layers[1].diffuse_texture, "dirt_diffuse.png"));
    CHECK(!strcmp(loaded_ls->layers[1].normal_texture, "dirt_normal.png"));
    CHECK(fabsf(loaded_ls->layers[1].uv_scale - 1.5f) < 1e-4f);

    /* Verify heights loaded accurately */
    float h_orig = af_landscape_get_height(ls, 5.0f, 5.0f);
    float h_load = af_landscape_get_height(loaded_ls, 5.0f, 5.0f);
    CHECK(fabsf(h_orig - h_load) < 0.05f);

    /* Cleanup */
    remove(test_path);
    af_scene_destroy(loaded_scene);
    af_scene_destroy(cloned_scene);
    af_scene_destroy(scene); /* Also destroys attached landscape */

    puts("Landscape system tests PASSED successfully!");
    return 0;
}

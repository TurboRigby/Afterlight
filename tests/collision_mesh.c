/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/scene.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "BVH %d: %s\n", __LINE__, #x);                                                             \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_vertex v[12] = {0};
    uint32_t indices[12];
    for (unsigned i = 0; i < 4; ++i) {
        v[i * 3].position = (af_vec3){(float)i * 10, 0, 0};
        v[i * 3 + 1].position = (af_vec3){(float)i * 10 + 1, 0, 0};
        v[i * 3 + 2].position = (af_vec3){(float)i * 10, 0, 1};
    }
    for (unsigned i = 0; i < 12; ++i)
        indices[i] = i;
    af_mesh_data data = {v, 12, indices, 12};
    af_asset_error error;
    CHECK(af_scene_set_collision_mesh(s, "level", &data, &error));
    af_scene *clone = af_scene_clone(s);
    CHECK(clone && af_scene_collision_mesh(s, "level") == af_scene_collision_mesh(clone, "level"));
    uint32_t found[1];
    const af_collision_mesh *mesh = af_scene_collision_mesh(s, "level");
    CHECK(af_collision_mesh_query(mesh, (af_vec3){9, -1, -1}, (af_vec3){12, 1, 2}, found, 1) == 1);
    af_triangle triangle;
    CHECK(af_collision_mesh_triangle(mesh, found[0], &triangle) && triangle.a.x == 10);
    indices[0] = 99;
    CHECK(!af_scene_set_collision_mesh(s, "level", &data, &error) && af_scene_collision_mesh(s, "level") == mesh);
    af_scene_destroy(s);
    CHECK(af_scene_collision_mesh_stats(clone).triangles == 4);
    af_object_id id = af_scene_add_object(clone, "Level", "level");
    af_collider collider = af_collider_default();
    collider.type = AF_COLLIDER_MESH;
    CHECK(af_scene_set_object_collider(clone, id, collider));
    af_world_collider world;
    CHECK(af_scene_world_collider(clone, id, &world) && world.mesh == mesh);
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_KINEMATIC;
    CHECK(!af_scene_set_body(clone, id, b));
    af_scene_error scene_error;
    CHECK(af_scene_save(clone, "bvh.json", &scene_error));
    af_scene *loaded = af_scene_load("bvh.json", &scene_error);
    CHECK(loaded && af_scene_get_object(loaded, id)->collider.type == AF_COLLIDER_MESH);
    af_scene_destroy(loaded);
    remove("bvh.json");
    af_scene_destroy(clone);
    puts("Collision meshes: immutable BVH, sparse candidates, clone sharing, last-good rejection and "
         "static-only components passed.");
    return 0;
}

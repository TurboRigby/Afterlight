/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Contacts %d: %s\n", __LINE__, #x);                                         \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
static af_object_id add(af_scene *s, af_collider_type type, af_vec3 pos, bool dynamic) {
    af_object_id id = af_scene_add_object(s, "Shape", "cube");
    af_collider c = af_collider_default();
    c.type = type;
    af_scene_set_object_collider(s, id, c);
    af_transform t = af_transform_identity();
    t.position = pos;
    af_scene_set_object_transform(s, id, t);
    if (dynamic) {
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_DYNAMIC;
        b.gravity_scale = 0;
        af_scene_set_body(s, id, b);
    }
    return id;
}
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    add(s, AF_COLLIDER_BOX, (af_vec3){0}, false);
    af_object_id top = add(s, AF_COLLIDER_BOX, (af_vec3){0, .95f, 0}, true);
    CHECK(af_physics_step(p, s, 1.f / 60));
    af_physics_contact c[8];
    CHECK(af_physics_contacts(p, c, 8) == 4 && c[0].normal.y > .99f && c[0].penetration >= -.0061f);
    af_transform t = af_scene_get_object(s, top)->transform;
    t.position = (af_vec3){0, 0, 5};
    af_scene_set_object_transform(s, top, t);
    CHECK(af_physics_step(p, s, 1.f / 60) && af_physics_contacts(p, c, 8) == 0);
    add(s, AF_COLLIDER_SPHERE, (af_vec3){.8f, 0, 0}, true);
    CHECK(af_physics_step(p, s, 1.f / 60) && af_physics_contacts(p, c, 8) >= 1);
    bool sphere = false;
    for (size_t i = 0; i < af_physics_contacts(p, c, 8) && i < 8; ++i) {
        CHECK(isfinite(c[i].normal.x) && af_vec3_length(c[i].normal) > .99f);
        sphere |= fabsf(c[i].normal.x) > .99f;
    }
    CHECK(sphere);
    for (unsigned i = 0; i < 100; ++i)
        add(s, AF_COLLIDER_BOX, (af_vec3){(float)i * 4 + 10, 0, 0}, true);
    CHECK(af_physics_step(p, s, 1.f / 60) && af_physics_get_stats(p).broadphase_pairs < 40);
    af_physics_destroy(p);
    af_scene_destroy(s);
    puts("Contacts: face manifolds, separation, sphere/box normals and sparse broadphase passed.");
    return 0;
}

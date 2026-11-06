/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Solver %d: %s\n", __LINE__, #x);                                           \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
static af_object_id box(af_scene *s, af_vec3 p, af_vec3 half, bool dynamic) {
    af_object_id id = af_scene_add_object(s, "Box", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = half;
    af_scene_set_object_collider(s, id, c);
    af_transform t = af_transform_identity();
    t.position = p;
    af_scene_set_object_transform(s, id, t);
    if (dynamic) {
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_DYNAMIC;
        af_scene_set_body(s, id, b);
    }
    return id;
}
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    box(s, (af_vec3){0, -.5f, 0}, (af_vec3){20, .5f, 20}, false);
    af_object_id ids[4];
    for (unsigned i = 0; i < 4; ++i)
        ids[i] = box(s, (af_vec3){0, .5f + (float)i * 1.02f, 0}, (af_vec3){.5f, .5f, .5f}, true);
    for (unsigned i = 0; i < 600; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    for (unsigned i = 0; i < 4; ++i) {
        af_physics_state b = af_physics_get_state(p, ids[i]);
        printf("Stack %u: %.3f %.3f %.3f, speed %.4f\n", i, b.position.x, b.position.y, b.position.z,
               af_vec3_length(b.velocity));
        CHECK(fabsf(b.position.y - (.5f + (float)i)) < .08f && fabsf(b.position.x) < .1f &&
              af_vec3_length(b.velocity) < .1f);
    }
    af_object_id sliding = box(s, (af_vec3){4, .5f, 0}, (af_vec3){.5f, .5f, .5f}, true);
    CHECK(af_physics_apply_impulse(p, s, sliding, (af_vec3){4, 0, 0}, (af_vec3){4, .5f, 0}));
    for (unsigned i = 0; i < 180; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(fabsf(af_physics_get_state(p, sliding).velocity.x) < .1f);
    af_physics_destroy(p);
    af_scene_destroy(s);
    puts("Solver: clipped-face warm starting, four-body stack, penetration correction and friction "
         "passed.");
    return 0;
}

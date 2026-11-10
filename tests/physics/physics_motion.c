/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Motion %d: %s\n", __LINE__, #x);                                                          \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_object_id id = af_scene_add_object(s, "Body", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    CHECK(af_scene_set_object_collider(s, id, c));
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    b.mass = 2;
    b.linear_damping = b.angular_damping = 0;
    CHECK(af_scene_set_body(s, id, b));
    af_physics *p = af_physics_create();
    CHECK(p && af_physics_apply_impulse(p, s, id, (af_vec3){2, 0, 0}, (af_vec3){0, 1, 0}));
    CHECK(af_physics_get_state(p, id).angular_velocity.z < -5);
    for (unsigned i = 0; i < 60; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    af_physics_state state = af_physics_get_state(p, id);
    CHECK(fabsf(state.position.x - 1) < .001f && fabsf(state.position.y + 4.925f) < .03f &&
          fabsf(state.velocity.y + 9.81f) < .001f);
    CHECK(af_scene_get_object(s, id)->body.velocity.y == state.velocity.y);
    CHECK(!af_physics_step(p, s, NAN));
    CHECK(af_physics_get_state(p, id).position.y == state.position.y);
    af_physics_reset(p);
    CHECK(af_physics_step(p, s, 1.f / 60) && af_physics_get_state(p, id).velocity.y < state.velocity.y);
    af_scene_destroy(s);
    af_physics_reset(p);
    s = af_scene_create();
    id = af_scene_add_object(s, "Forced body", "cube");
    CHECK(af_scene_set_object_collider(s, id, c));
    b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    b.gravity_scale = 0;
    b.mass = 2;
    b.linear_damping = b.angular_damping = 0;
    CHECK(af_scene_set_body(s, id, b));
    CHECK(af_physics_apply_force(p, s, id, (af_vec3){12, 0, 0}, (af_vec3){0}));
    CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(fabsf(af_physics_get_state(p, id).velocity.x - .1f) < .0001f);
    CHECK(af_physics_step(p, s, 1.f / 60) && fabsf(af_physics_get_state(p, id).velocity.x - .1f) < .0001f);
    CHECK(!af_physics_apply_impulse(p, s, id, (af_vec3){NAN, 0, 0}, (af_vec3){0}));
    b = af_scene_get_object(s, id)->body;
    b.mass = 4;
    CHECK(af_scene_set_body(s, id, b));
    float before = af_physics_get_state(p, id).velocity.x;
    CHECK(af_physics_apply_impulse(p, s, id, (af_vec3){4, 0, 0}, af_scene_get_object(s, id)->transform.position) &&
          fabsf(af_physics_get_state(p, id).velocity.x - before - 1) < .0001f);
    b.type = AF_BODY_STATIC;
    CHECK(af_scene_set_body(s, id, b) && !af_physics_apply_impulse(p, s, id, (af_vec3){1, 0, 0}, (af_vec3){0}));
    CHECK(af_scene_remove_object(s, id) && !af_physics_apply_force(p, s, id, (af_vec3){1, 0, 0}, (af_vec3){0}));
    af_physics_destroy(p);
    af_scene_destroy(s);
    puts("Motion: fixed substeps, gravity, point impulse/spin, velocity persistence and invalid-delta "
         "isolation passed.");
    return 0;
}

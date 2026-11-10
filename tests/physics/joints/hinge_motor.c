/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Motor %d: %s\n", __LINE__, #x);                                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    af_object_id id = af_scene_add_object(s, "Motor rotor", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_SPHERE;
    c.radius = .2f;
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    b.gravity_scale = 0;
    CHECK(af_scene_set_object_collider(s, id, c) && af_scene_set_body(s, id, b));
    af_joint j = af_joint_default();
    j.a = id;
    j.type = AF_JOINT_HINGE;
    j.motor_enabled = true;
    j.motor_speed = 3;
    j.max_motor_torque = .5f;
    af_joint_id joint = af_scene_add_joint(s, j);
    CHECK(joint && af_physics_step(p, s, 1.f / 60));
    af_physics_joint_state state = af_physics_get_joint_state(p, joint);
    CHECK(state.speed > 0 && state.speed < .6f && fabsf(state.motor_torque) <= .501f);
    for (unsigned i = 0; i < 60; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    state = af_physics_get_joint_state(p, joint);
    CHECK(fabsf(state.speed - 3) < .1f && !state.sleeping);
    j.motor_speed = 0;
    CHECK(af_scene_set_joint(s, joint, j));
    for (unsigned i = 0; i < 120; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(fabsf(af_physics_get_joint_state(p, joint).speed) < .05f);
    j.motor_speed = 3;
    j.limits_enabled = true;
    j.lower_limit = -.4f;
    j.upper_limit = .4f;
    CHECK(af_scene_set_joint(s, joint, j));
    for (unsigned i = 0; i < 120; ++i) {
        CHECK(af_physics_step(p, s, 1.f / 60));
        state = af_physics_get_joint_state(p, joint);
        if (i > 30)
            CHECK(fabsf(state.angle) < .42f);
        CHECK(fabsf(state.motor_torque) <= .501f);
    }
    af_scene_error e;
    CHECK(af_scene_save(s, "motor.scene", &e));
    af_scene *copy = af_scene_load("motor.scene", &e);
    CHECK(copy && af_scene_joint_at(copy, 0)->motor_enabled && af_scene_joint_at(copy, 0)->motor_speed == 3);
    af_scene_destroy(copy);
    remove("motor.scene");
    j.max_motor_torque = -1;
    CHECK(!af_scene_set_joint(s, joint, j));
    af_scene_destroy(s);
    af_physics_destroy(p);
    puts("Hinge motor: acceleration/torque cap, target speed, braking, limits and scene persistence "
         "passed.");
    return 0;
}

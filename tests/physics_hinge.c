/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/joint.h>
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Hinge %d: %s\n", __LINE__, #x);                                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    af_object_id id = af_scene_add_object(s, "Door", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = (af_vec3){1, 1, .1f};
    CHECK(af_scene_set_object_collider(s, id, c));
    af_rigid_body b = af_rigid_body_default();
    b.type = AF_BODY_DYNAMIC;
    b.enabled = true;
    b.gravity_scale = 0;
    b.angular_velocity = (af_vec3){1, 4, .5f};
    CHECK(af_scene_set_body(s, id, b));
    af_joint j = af_joint_default();
    j.type = AF_JOINT_HINGE;
    j.a = id;
    j.anchor_a = j.anchor_b = (af_vec3){-1, 0, 0};
    af_joint_id joint = af_scene_add_joint(s, j);
    CHECK(joint);
    float largest = 0;
    for (unsigned i = 0; i < 240; ++i) {
        CHECK(af_physics_step(p, s, 1.f / 60));
        af_joint_frame f;
        CHECK(af_scene_joint_frame(s, j, &f));
        CHECK(af_vec3_length(af_vec3_sub(f.anchor_a, f.anchor_b)) < .035f && af_vec3_dot(f.axis_a, f.axis_b) > .999f);
        float angle = atan2f(af_vec3_dot(f.axis_a, af_vec3_cross(f.reference_a, f.reference_b)),
                             af_vec3_dot(f.reference_a, f.reference_b));
        largest = fmaxf(largest, fabsf(angle));
    }
    CHECK(largest > 1);
    j.limits_enabled = true;
    j.lower_limit = -.4f;
    j.upper_limit = .4f;
    CHECK(af_scene_set_joint(s, joint, j));
    for (unsigned i = 0; i < 240; ++i) {
        CHECK(af_physics_step(p, s, 1.f / 60));
        af_joint_frame f;
        CHECK(af_scene_joint_frame(s, j, &f));
        float angle = atan2f(af_vec3_dot(f.axis_a, af_vec3_cross(f.reference_a, f.reference_b)),
                             af_vec3_dot(f.reference_a, f.reference_b));
        if (i > 30)
            CHECK(fabsf(angle) < .42f);
    }
    af_scene_error e;
    CHECK(af_scene_save(s, "hinge.scene", &e));
    af_scene *copy = af_scene_load("hinge.scene", &e);
    CHECK(copy && af_scene_joint_at(copy, 0)->limits_enabled && af_scene_joint_at(copy, 0)->type == AF_JOINT_HINGE);
    af_scene_destroy(copy);
    remove("hinge.scene");
    j.axis_a = (af_vec3){0};
    CHECK(!af_scene_set_joint(s, joint, j));
    af_scene_destroy(s);
    af_physics_destroy(p);
    puts("Hinges: free rotation, anchor/angular alignment, limits and persistence passed.");
    return 0;
}

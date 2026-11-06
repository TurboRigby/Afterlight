/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Joint diagnostics %d: %s\n", __LINE__, #x);                                \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    af_object_id id = af_scene_add_object(s, "Suspended mass", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_SPHERE;
    c.radius = .2f;
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    b.mass = 2;
    CHECK(af_scene_set_object_collider(s, id, c) && af_scene_set_body(s, id, b));
    af_joint j = af_joint_default();
    j.a = id;
    j.anchor_b = (af_vec3){0, 1, 0};
    af_joint_id joint = af_scene_add_joint(s, j);
    CHECK(joint && !af_physics_get_joint_state(p, joint).available);
    for (unsigned tick = 0; tick < 20; ++tick)
        CHECK(af_physics_step(p, s, 1.f / 60));
    af_physics_joint_state state = af_physics_get_joint_state(p, joint);
    CHECK(state.available && state.linear_error < .005f && fabsf(state.force - 19.62f) < 1 &&
          isfinite(state.torque));
    CHECK(af_physics_get_stats(p).max_joint_force > 18);
    CHECK(af_scene_remove_joint(s, joint) && af_physics_step(p, s, 1.f / 60) &&
          !af_physics_get_joint_state(p, joint).available);
    af_scene_destroy(s);
    af_physics_destroy(p);
    puts("Joint diagnostics: reaction force matches suspended weight, errors and removed IDs passed.");
    return 0;
}

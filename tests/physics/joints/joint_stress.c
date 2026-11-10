/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/joint.h>
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Joint stress %d: %s\n", __LINE__, #x);                                                    \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    float rates[] = {30, 60, 144};
    for (unsigned run = 0; run < 3; ++run) {
        af_scene *s = af_scene_create();
        af_physics *p = af_physics_create();
        af_object_id previous = 0, last = 0;
        for (unsigned i = 0; i < 12; ++i) {
            af_object_id id = af_scene_add_object(s, "Chain link", "cube");
            af_transform t = af_transform_identity();
            t.position = (af_vec3){.04f * (float)i, 12 - .8f * (float)i, 0};
            af_collider c = af_collider_default();
            c.type = AF_COLLIDER_SPHERE;
            c.radius = .15f;
            af_rigid_body b = af_rigid_body_default();
            b.enabled = true;
            b.type = AF_BODY_DYNAMIC;
            b.mass = i == 11 ? 16 : 1;
            b.linear_damping = .2f;
            b.angular_damping = .2f;
            CHECK(af_scene_set_object_transform(s, id, t) && af_scene_set_object_collider(s, id, c) &&
                  af_scene_set_body(s, id, b));
            af_joint j = af_joint_default();
            j.a = id;
            j.b = previous;
            j.distance = .8f;
            if (!previous)
                j.anchor_b = (af_vec3){0, 12.8f, 0};
            CHECK(af_scene_add_joint(s, j));
            last = id;
            previous = id;
        }
        float maximum = 0;
        for (unsigned tick = 0; tick < (unsigned)(rates[run] * 6); ++tick) {
            if (tick == (unsigned)(rates[run] * 3))
                CHECK(af_physics_apply_impulse(p, s, last, (af_vec3){4, 0, 0},
                                               af_scene_get_object(s, last)->transform.position));
            CHECK(af_physics_step(p, s, 1 / rates[run]));
            for (size_t i = 0; i < af_scene_joint_count(s); ++i) {
                af_joint_frame f;
                af_joint j = *af_scene_joint_at(s, i);
                CHECK(af_scene_joint_frame(s, j, &f));
                float error = fabsf(af_vec3_length(af_vec3_sub(f.anchor_a, f.anchor_b)) - .8f);
                maximum = fmaxf(maximum, error);
                CHECK(isfinite(error) && error < .14f);
            }
        }
        printf("12-link 16:1 chain at %.0f Hz: max length error %.5f\n", (double)rates[run], (double)maximum);
        af_scene_destroy(s);
        af_physics_destroy(p);
    }
    return 0;
}

/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/joint.h>
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Mechanical scene %d: %s (%s)\n", __LINE__, #x, af_physics_error(p));       \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    af_physics *p = af_physics_create();
    for (unsigned scene = 0; scene < 2; ++scene) {
        af_scene_error e;
        af_scene *s = af_scene_load(argv[scene + 1], &e);
        CHECK(s);
        af_asset_error ae;
        CHECK(af_scene_prepare_collision_meshes(s, &ae));
        CHECK(af_scene_collision_mesh_stats(s).triangles > 40 &&
              af_scene_joint_count(s) >= (scene ? 10u : 1u));
        af_physics_reset(p);
        float maximum = 0;
        for (unsigned tick = 0; tick < 360; ++tick) {
            CHECK(af_physics_step(p, s, 1.f / 60));
            for (size_t i = 0; i < af_scene_object_count(s); ++i) {
                const af_scene_object *o = af_scene_object_at(s, i);
                if (!o->body.enabled || o->body.type != AF_BODY_DYNAMIC)
                    continue;
                af_physics_state state = af_physics_get_state(p, o->id);
                if (!(state.available && isfinite(state.position.y) && state.position.y > -.5f &&
                      fabsf(state.position.x) < 30))
                    fprintf(stderr, "tick %u %s: %.3f %.3f %.3f\n", tick, o->name, state.position.x,
                            state.position.y, state.position.z);
                CHECK(state.available && isfinite(state.position.y) && state.position.y > -.5f &&
                      fabsf(state.position.x) < 30);
            }
            for (size_t i = 0; i < af_scene_joint_count(s); ++i) {
                const af_joint *j = af_scene_joint_at(s, i);
                if (!j->motor_enabled)
                    continue;
                af_physics_joint_state state = af_physics_get_joint_state(p, j->id);
                CHECK(state.available && fabsf(state.motor_torque) <= j->max_motor_torque + .05f &&
                      !state.sleeping);
            }
            for (size_t i = 0; i < af_scene_joint_count(s); ++i) {
                af_joint j = *af_scene_joint_at(s, i);
                af_joint_frame f;
                CHECK(af_scene_joint_frame(s, j, &f));
                float error = fabsf(af_vec3_length(af_vec3_sub(f.anchor_a, f.anchor_b)) -
                                    (j.type == AF_JOINT_DISTANCE ? j.distance : 0));
                maximum = fmaxf(maximum, error);
                CHECK(error < .15f);
                if (j.type == AF_JOINT_HINGE) {
                    CHECK(af_vec3_dot(f.axis_a, f.axis_b) > .99f);
                    float angle =
                        atan2f(af_vec3_dot(f.axis_a, af_vec3_cross(f.reference_a, f.reference_b)),
                               af_vec3_dot(f.reference_a, f.reference_b));
                    CHECK(angle > j.lower_limit - .08f && angle < j.upper_limit + .08f);
                }
            }
        }
        printf("Mechanical scene %u: %zu joints, maximum anchor/length error %.5f.\n", scene,
               af_scene_joint_count(s), maximum);
        af_scene_destroy(s);
    }
    af_physics_destroy(p);
    return 0;
}

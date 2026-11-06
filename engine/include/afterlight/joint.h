/* SPDX-License-Identifier: MPL-2.0 */

#ifndef AFTERLIGHT_JOINT_H
#define AFTERLIGHT_JOINT_H
#include <afterlight/scene.h>
#define AF_SCENE_MAX_JOINTS 128u
typedef uint64_t af_joint_id;
typedef enum af_joint_type { AF_JOINT_DISTANCE, AF_JOINT_HINGE } af_joint_type;
typedef struct af_joint {
    af_joint_id id;
    char name[64];
    af_joint_type type;
    bool enabled, collide_connected;
    af_object_id a, b; /* b == 0 anchors to the world. */
    af_vec3 anchor_a, anchor_b;
    float distance;
    af_vec3 axis_a, axis_b, reference_a, reference_b;
    bool limits_enabled;
    bool motor_enabled;
    float motor_speed, max_motor_torque; /* Relative B-A rad/s; torque budget per second. */
    float lower_limit, upper_limit;      /* Radians: signed B reference relative to A. */
} af_joint;
/* Anchors use unscaled body axes about the world collider center. A world
   endpoint uses world coordinates. At least one endpoint must be dynamic. */
af_joint af_joint_default(void);
typedef struct af_joint_frame {
    af_vec3 anchor_a, anchor_b, axis_a, axis_b, reference_a, reference_b;
} af_joint_frame;
bool af_scene_joint_frame(const af_scene *, af_joint, af_joint_frame *);
bool af_joint_valid(af_joint);
af_joint_id af_scene_add_joint(af_scene *, af_joint);
bool af_scene_set_joint(af_scene *, af_joint_id, af_joint);
bool af_scene_remove_joint(af_scene *, af_joint_id);
size_t af_scene_joint_count(const af_scene *);
const af_joint *af_scene_joint_at(const af_scene *, size_t);
const af_joint *af_scene_get_joint(const af_scene *, af_joint_id);
#endif

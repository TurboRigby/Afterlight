/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_BODY_H
#define AFTERLIGHT_BODY_H
#include <afterlight/math.h>
#include <stdbool.h>
typedef enum af_body_type { AF_BODY_STATIC, AF_BODY_KINEMATIC, AF_BODY_DYNAMIC } af_body_type;
typedef struct af_rigid_body {
    bool enabled, allow_sleep, continuous;
    af_body_type type;
    float mass, friction, restitution, linear_damping, angular_damping, gravity_scale;
    af_vec3 velocity, angular_velocity;
} af_rigid_body;
af_rigid_body af_rigid_body_default(void);
bool af_rigid_body_valid(af_rigid_body);
#endif

/* SPDX-License-Identifier: MPL-2.0 */

#include "joint_gizmo.h"
#include <math.h>
static af_vec3 local(af_world_collider c, af_vec3 v) {
    return (af_vec3){af_vec3_dot(c.axes[0], v), af_vec3_dot(c.axes[1], v), af_vec3_dot(c.axes[2], v)};
}
static bool body_frame(const af_scene *s, af_joint j, bool b, af_world_collider *c) {
    af_object_id id = b ? j.b : j.a;
    if (id)
        return af_scene_world_collider(s, id, c);
    *c = (af_world_collider){0};
    c->axes[0] = (af_vec3){1, 0, 0};
    c->axes[1] = (af_vec3){0, 1, 0};
    c->axes[2] = (af_vec3){0, 0, 1};
    return true;
}
bool ed_joint_edit_pose(const af_scene *s, af_joint j, ed_joint_part part, af_transform *out) {
    if (!out || part < ED_JOINT_ANCHOR_A || part > ED_JOINT_AXIS_B)
        return false;
    af_joint_frame f;
    if (!af_scene_joint_frame(s, j, &f))
        return false;
    bool b = part == ED_JOINT_ANCHOR_B || part == ED_JOINT_AXIS_B;
    af_vec3 axis = b ? f.axis_b : f.axis_a, reference = b ? f.reference_b : f.reference_a,
            z = af_vec3_cross(reference, axis);
    *out = af_transform_identity();
    out->position = b ? f.anchor_b : f.anchor_a;
    float y = asinf(fmaxf(-1, fminf(1, -reference.z)));
    out->rotation = fabsf(cosf(y)) < 1e-5f
                        ? (af_vec3){atan2f(-z.y, axis.y), y, 0}
                        : (af_vec3){atan2f(axis.z, z.z), y, atan2f(reference.y, reference.x)};
    return true;
}
bool ed_joint_edit_apply(const af_scene *s, af_joint j, ed_joint_part part, af_transform pose,
                         af_joint *out) {
    if (!out || part < ED_JOINT_ANCHOR_A || part > ED_JOINT_AXIS_B)
        return false;
    bool b = part == ED_JOINT_ANCHOR_B || part == ED_JOINT_AXIS_B;
    af_world_collider c;
    if (!body_frame(s, j, b, &c))
        return false;
    if (part < ED_JOINT_AXIS_A) {
        af_vec3 anchor = local(c, af_vec3_sub(pose.position, c.center));
        if (b)
            j.anchor_b = anchor;
        else
            j.anchor_a = anchor;
    } else {
        pose.position = (af_vec3){0};
        pose.scale = (af_vec3){1, 1, 1};
        af_mat4 m = af_transform_matrix(pose);
        af_vec3 axis = local(c, (af_vec3){m.elements[4], m.elements[5], m.elements[6]}),
                reference = local(c, (af_vec3){m.elements[0], m.elements[1], m.elements[2]});
        if (b) {
            j.axis_b = axis;
            j.reference_b = reference;
        } else {
            j.axis_a = axis;
            j.reference_a = reference;
        }
    }
    if (!af_joint_valid(j))
        return false;
    *out = j;
    return true;
}

/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ED_JOINT_GIZMO_H
#define ED_JOINT_GIZMO_H
#include <afterlight/joint.h>
typedef enum ed_joint_part {ED_JOINT_ANCHOR_A,ED_JOINT_ANCHOR_B,ED_JOINT_AXIS_A,ED_JOINT_AXIS_B} ed_joint_part;
bool ed_joint_edit_pose(const af_scene*,af_joint,ed_joint_part,af_transform*);
bool ed_joint_edit_apply(const af_scene*,af_joint,ed_joint_part,af_transform,af_joint*);
#endif

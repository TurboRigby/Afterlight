/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_LOCAL_SHADOW_H
#define AF_LOCAL_SHADOW_H
#include <afterlight/material.h>
#define AF_LOCAL_SHADOW_LIGHTS 4u
#define AF_LOCAL_SHADOW_LAYERS 24u
unsigned af_local_shadow_face(af_vec3 direction);
af_mat4 af_local_shadow_matrix(af_local_light light,unsigned face);
#endif

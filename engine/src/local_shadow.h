/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_LOCAL_SHADOW_H
#define AF_LOCAL_SHADOW_H
#include <afterlight/material.h>
#include <afterlight/camera.h>
#include <stddef.h>
#define AF_LOCAL_SHADOW_LIGHTS 4u
#define AF_LOCAL_SHADOW_LAYERS 24u
unsigned af_local_shadow_face(af_vec3 direction);
af_mat4 af_local_shadow_matrix(af_local_light light,unsigned face);
/* Stable, conservative view-volume selection; returned entries are scene indices. */
unsigned af_local_shadow_select(const af_local_light *lights,size_t count,const af_camera *camera,float aspect,unsigned indices[AF_LOCAL_SHADOW_LIGHTS],unsigned *eligible);
#endif

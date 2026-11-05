/* SPDX-License-Identifier: MPL-2.0 */
#include "local_shadow.h"
#include <math.h>
unsigned af_local_shadow_face(af_vec3 d){
    float x=fabsf(d.x),y=fabsf(d.y),z=fabsf(d.z);
    if(x>=y&&x>=z)return d.x>=0?0u:1u;
    if(y>=z)return d.y>=0?2u:3u;
    return d.z>=0?4u:5u;
}
af_mat4 af_local_shadow_matrix(af_local_light light,unsigned face){
    static const af_vec3 directions[]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    af_vec3 d=light.type==AF_LIGHT_POINT?directions[face%6]:af_vec3_normalize(light.direction);
    af_vec3 up=fabsf(d.y)>.99f?(af_vec3){0,0,1}:(af_vec3){0,1,0};
    float angle=light.type==AF_LIGHT_POINT?AF_PI*.5f:fmaxf(.01f,light.outer_angle*2);
    float near=fminf(.05f,light.range*.01f);
    return af_mat4_mul(af_mat4_perspective(angle,1,near,light.range),af_mat4_look_at(light.position,af_vec3_add(light.position,d),up));
}

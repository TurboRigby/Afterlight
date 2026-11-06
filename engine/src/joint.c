/* SPDX-License-Identifier: MPL-2.0 */

#include "scene_internal.h"
#include <math.h>
#include <string.h>
af_joint af_joint_default(void){return (af_joint){.name="Distance joint",.enabled=true,.distance=1,.axis_a={0,1,0},.axis_b={0,1,0},.reference_a={1,0,0},.reference_b={1,0,0},.lower_limit=-AF_PI,.upper_limit=AF_PI};}
static bool vector(af_vec3 v){return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z)&&af_vec3_length(v)<=1000000;}
bool af_joint_valid(af_joint j){return j.type>=AF_JOINT_DISTANCE&&j.type<=AF_JOINT_HINGE&&memchr(j.name,0,sizeof(j.name))&&j.name[0]&&j.a&&j.a!=j.b&&vector(j.anchor_a)&&vector(j.anchor_b)&&isfinite(j.distance)&&j.distance>=0&&j.distance<=10000&&vector(j.axis_a)&&vector(j.axis_b)&&vector(j.reference_a)&&vector(j.reference_b)&&fabsf(af_vec3_length(j.axis_a)-1)<.001f&&fabsf(af_vec3_length(j.axis_b)-1)<.001f&&fabsf(af_vec3_length(j.reference_a)-1)<.001f&&fabsf(af_vec3_length(j.reference_b)-1)<.001f&&fabsf(af_vec3_dot(j.axis_a,j.reference_a))<.001f&&fabsf(af_vec3_dot(j.axis_b,j.reference_b))<.001f&&isfinite(j.lower_limit)&&isfinite(j.upper_limit)&&j.lower_limit>=-AF_PI&&j.upper_limit<=AF_PI&&j.lower_limit<=j.upper_limit;}
static bool endpoint(const af_scene *s,af_object_id id){const af_scene_object *o=af_scene_get_object(s,id);return o&&o->collider.enabled&&!o->collider.trigger&&(o->collider.type==AF_COLLIDER_BOX||o->collider.type==AF_COLLIDER_SPHERE||o->collider.type==AF_COLLIDER_MESH);}
static bool dynamic(const af_scene *s,af_object_id id){const af_scene_object *o=af_scene_get_object(s,id);return o&&o->body.enabled&&o->body.type==AF_BODY_DYNAMIC;}
static bool valid(const af_scene *s,af_joint j){return af_joint_valid(j)&&endpoint(s,j.a)&&(!j.b||endpoint(s,j.b))&&(dynamic(s,j.a)||dynamic(s,j.b));}
size_t af_scene_joint_count(const af_scene *s){return s?s->joint_count:0;}
const af_joint *af_scene_joint_at(const af_scene *s,size_t i){return s&&i<s->joint_count?&s->joints[i]:NULL;}
const af_joint *af_scene_get_joint(const af_scene *s,af_joint_id id){for(size_t i=0;s&&i<s->joint_count;++i)if(s->joints[i].id==id)return &s->joints[i];return NULL;}
af_joint_id af_scene_add_joint(af_scene *s,af_joint j){if(!s||s->joint_count==AF_SCENE_MAX_JOINTS||!s->next_joint_id||!valid(s,j))return 0;j.id=s->next_joint_id++;s->joints[s->joint_count++]=j;return j.id;}
bool af_scene_set_joint(af_scene *s,af_joint_id id,af_joint j){af_joint *old=(af_joint*)af_scene_get_joint(s,id);if(!old||!valid(s,j))return false;j.id=id;*old=j;return true;}
bool af_scene_remove_joint(af_scene *s,af_joint_id id){for(size_t i=0;s&&i<s->joint_count;++i)if(s->joints[i].id==id){memmove(&s->joints[i],&s->joints[i+1],(--s->joint_count-i)*sizeof(af_joint));return true;}return false;}

bool af_scene_joints_valid(const af_scene *s){for(size_t i=0;s&&i<s->joint_count;++i)if(!valid(s,s->joints[i]))return false;return s!=NULL;}

static af_vec3 frame_vector(af_world_collider c,af_vec3 v){return af_vec3_add(af_vec3_scale(c.axes[0],v.x),af_vec3_add(af_vec3_scale(c.axes[1],v.y),af_vec3_scale(c.axes[2],v.z)));}
bool af_scene_joint_frame(const af_scene *s,af_joint j,af_joint_frame *f){af_world_collider a,b={0};for(unsigned k=0;k<3;++k)b.axes[k]=(af_vec3){k==0,k==1,k==2};if(!f||!af_joint_valid(j)||!af_scene_world_collider(s,j.a,&a)||(j.b&&!af_scene_world_collider(s,j.b,&b)))return false;*f=(af_joint_frame){af_vec3_add(a.center,frame_vector(a,j.anchor_a)),af_vec3_add(b.center,frame_vector(b,j.anchor_b)),frame_vector(a,j.axis_a),frame_vector(b,j.axis_b),frame_vector(a,j.reference_a),frame_vector(b,j.reference_b)};return true;}

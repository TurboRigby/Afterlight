/* SPDX-License-Identifier: MPL-2.0 */

#include "scene_internal.h"
#include <math.h>
#include <string.h>
af_collider_part af_collider_part_default(void){af_collider c=af_collider_default();c.type=AF_COLLIDER_BOX;return (af_collider_part){.name="Collider part",.local={.scale={1,1,1}},.collider=c,.density=1000};}
static bool finite3(af_vec3 v){return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z)&&af_vec3_length(v)<=1000000;}
bool af_collider_part_valid(af_collider_part p){return memchr(p.name,0,sizeof(p.name))&&p.name[0]&&finite3(p.local.position)&&finite3(p.local.rotation)&&finite3(p.local.scale)&&p.local.scale.x>0&&p.local.scale.y>0&&p.local.scale.z>0&&p.local.scale.x<=10000&&p.local.scale.y<=10000&&p.local.scale.z<=10000&&af_collider_valid(p.collider)&&(p.collider.type==AF_COLLIDER_BOX||p.collider.type==AF_COLLIDER_SPHERE)&&!p.collider.trigger&&af_vec3_length(p.collider.center)<1e-6f&&isfinite(p.density)&&p.density>=.001f&&p.density<=1000000;}
size_t af_scene_collider_part_count(const af_scene *s,af_object_id id){size_t n=0;for(size_t i=0;s&&i<s->part_count;++i)n+=s->parts[i].owner==id;return n;}
const af_collider_part *af_scene_collider_part_at(const af_scene *s,af_object_id id,size_t index){for(size_t i=0;s&&i<s->part_count;++i)if(s->parts[i].owner==id){if(!index)return &s->parts[i];--index;}return NULL;}
const af_collider_part *af_scene_get_collider_part(const af_scene *s,af_collider_part_id id){for(size_t i=0;s&&i<s->part_count;++i)if(s->parts[i].id==id)return &s->parts[i];return NULL;}
af_collider_part_id af_scene_add_collider_part(af_scene *s,af_object_id owner,af_collider_part p){if(!s||!af_scene_get_object(s,owner)||s->part_count>=AF_SCENE_MAX_COLLIDER_PARTS||!s->next_part_id||af_scene_collider_part_count(s,owner)>=AF_COMPOUND_MAX_PARTS||!af_collider_part_valid(p))return 0;p.owner=owner;p.id=s->next_part_id++;s->parts[s->part_count++]=p;return p.id;}
bool af_scene_set_collider_part(af_scene *s,af_collider_part_id id,af_collider_part p){af_collider_part *old=(af_collider_part*)af_scene_get_collider_part(s,id);if(!old||!af_collider_part_valid(p))return false;p.owner=old->owner;p.id=id;af_collider_part previous=*old;*old=p;const af_scene_object *o=af_scene_get_object(s,p.owner);if(!o||!af_scene_set_body(s,o->id,o->body)){*old=previous;return false;}return true;}
bool af_scene_remove_collider_part(af_scene *s,af_collider_part_id id){for(size_t i=0;s&&i<s->part_count;++i)if(s->parts[i].id==id){const af_scene_object *o=af_scene_get_object(s,s->parts[i].owner);if(o&&o->collider.type==AF_COLLIDER_COMPOUND&&af_scene_collider_part_count(s,o->id)==1)return false;memmove(&s->parts[i],&s->parts[i+1],(--s->part_count-i)*sizeof(*s->parts));return true;}return false;}

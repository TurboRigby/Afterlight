/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/scene.h>
#include <afterlight/prefab.h>
#include <stdio.h>
#include <math.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Body %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){af_scene *s=af_scene_create();af_object_id id=af_scene_add_object(s,"Ball","cube");af_collider c=af_collider_default();c.type=AF_COLLIDER_SPHERE;CHECK(af_scene_set_object_collider(s,id,c));af_rigid_body b=af_rigid_body_default();CHECK(!b.enabled);b.enabled=true;b.type=AF_BODY_DYNAMIC;b.mass=2;b.velocity=(af_vec3){1,2,3};b.continuous=true;CHECK(af_scene_set_body(s,id,b));af_scene_error e;CHECK(af_scene_save(s,"body.json",&e));af_scene *copy=af_scene_load("body.json",&e);CHECK(copy&&af_scene_get_object(copy,id)->body.mass==2&&af_scene_get_object(copy,id)->body.velocity.y==2&&af_scene_get_object(copy,id)->body.continuous);b.mass=NAN;CHECK(!af_scene_set_body(s,id,b));b=af_rigid_body_default();b.enabled=true;b.type=AF_BODY_DYNAMIC;af_object_id group=af_scene_add_object(s,"Parent","__node");CHECK(!af_scene_set_parent(s,id,group)&&af_scene_get_object(s,id)->parent==0);af_scene_destroy(copy);af_scene_destroy(s);remove("body.json");puts("Bodies: defaults, bounds, dynamic shape/parent validation and scene persistence passed.");return 0;}

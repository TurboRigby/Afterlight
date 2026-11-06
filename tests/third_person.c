/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/third_person.h>
#include <afterlight/scene.h>
#include <afterlight/simulation.h>
#include <math.h>
#include <stdio.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Third person %d: %s\n",__LINE__,#c);return 1;}}while(0)
static af_object_id box(af_scene *s,af_vec3 center,af_vec3 half){af_object_id id=af_scene_add_object(s,"Box","cube");af_transform t=af_transform_identity();t.position=center;af_scene_set_object_transform(s,id,t);af_collider c=af_collider_default();c.type=AF_COLLIDER_BOX;c.half_extents=half;af_scene_set_object_collider(s,id,c);return id;}
int main(void){
    af_scene *s=af_scene_create();CHECK(s);box(s,(af_vec3){0,-.5f,0},(af_vec3){50,.5f,50});af_query_filter filter=af_query_filter_default();af_third_person_config config=af_third_person_config_default();af_third_person p=af_third_person_default((af_vec3){0,.902f,0});
    for(unsigned i=0;i<120;++i)CHECK(af_third_person_step(s,&p,config,(af_third_person_input){.move={0,1}},1.0f/60,filter));CHECK(p.body.grounded&&p.body.position.z< -3.9f&&fabsf(p.body.position.x)<.001f);
    p=af_third_person_default((af_vec3){0,.902f,0});p.orbit.yaw=0;for(unsigned i=0;i<60;++i)CHECK(af_third_person_step(s,&p,config,(af_third_person_input){.move={1,1},.run=true},1.0f/60,filter));CHECK(p.body.position.x>3.4f&&p.body.position.z>3.4f&&af_vec3_length((af_vec3){p.body.velocity.x,0,p.body.velocity.z})<=5.001f);
    p=af_third_person_default((af_vec3){0,.902f,0});for(unsigned i=0;i<5;++i)CHECK(af_third_person_step(s,&p,config,(af_third_person_input){0},1.0f/60,filter));CHECK(af_third_person_step(s,&p,config,(af_third_person_input){.jump=true},1.0f/60,filter)&&p.body.velocity.y>0&&!p.body.grounded);
    p=af_third_person_default((af_vec3){0,.902f,0});af_camera camera;CHECK(af_third_person_camera(s,&p,config,1.0f/60,filter,&camera));float clear=p.camera_distance;af_object_id wall=box(s,(af_vec3){0,2,2},(af_vec3){2,2,.1f});CHECK(af_third_person_camera(s,&p,config,1.0f/60,filter,&camera)&&p.camera_distance<2&&p.camera_distance<clear);af_collider trigger=af_scene_get_object(s,wall)->collider;trigger.trigger=true;CHECK(af_scene_set_object_collider(s,wall,trigger));CHECK(af_third_person_camera(s,&p,config,.1f,filter,&camera)&&p.camera_distance>2);
    af_third_person before=p;CHECK(!af_third_person_step(s,&p,config,(af_third_person_input){.move={NAN,0}},.01f,filter)&&p.body.position.z==before.body.position.z);CHECK(!af_third_person_camera(s,&p,config,NAN,filter,&camera));af_third_person_look(&p,config,(af_vec2){100,100});CHECK(p.orbit.yaw!=before.orbit.yaw&&p.orbit.pitch<before.orbit.pitch);
    p=af_third_person_default((af_vec3){0,.902f,0});for(unsigned i=0;i<5;++i)CHECK(af_third_person_step(s,&p,config,(af_third_person_input){0},1.f/60,filter));
    CHECK(af_third_person_step_root_motion(s,&p,config,(af_third_person_input){.move={0,1},.jump=true},(af_vec3){0},1.f/60,filter)&&p.body.velocity.z< -1.9f&&p.body.velocity.y>0);
    af_third_person results[2];unsigned rates[]={30,144};for(unsigned r=0;r<2;++r){af_fixed_clock clock={0};p=af_third_person_default((af_vec3){0,.902f,0});
        for(unsigned frame=0;frame<rates[r]*3;++frame){unsigned steps=af_fixed_clock_advance(&clock,1.0/rates[r],false,false);for(unsigned tick=0;tick<steps;++tick)CHECK(af_third_person_step(s,&p,config,(af_third_person_input){.move={.5f,1},.run=true,.jump=clock.ticks-steps+tick==60},(float)AF_FIXED_DELTA,filter));}results[r]=p;}
    CHECK(af_vec3_length(af_vec3_sub(results[0].body.position,results[1].body.position))<.0001f&&fabsf(results[0].facing-results[1].facing)<.0001f);
    af_scene_destroy(s);puts("Third person: camera-relative normalized walk/run, jump, collision boom, trigger filtering, recovery and invalid-state isolation passed.");return 0;
}

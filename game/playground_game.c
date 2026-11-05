/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/game_module.h>
#include <math.h>
typedef struct platform_state {af_object_id id;af_transform base;} platform_state;
typedef struct playground_state {
    af_character player;
    af_character_config config;
    af_camera camera,initial_camera;
    af_gameplay_status status;
    float sensitivity,eye_height;
    af_object_id body;
    af_vec3 spawn,checkpoint;
    platform_state platforms[16];unsigned platform_count;
    double time;
    bool goal_locked;
} playground_state;
static void present(playground_state *s,const af_game_host *host){
    const af_scene_object *body=host->get_object(host->scene,s->body);
    if(body){af_transform t=body->transform;t.position=s->player.position;host->set_transform(host->scene,s->body,t);}
    if(host->camera_enabled){s->camera.position=s->player.position;s->camera.position.y+=s->eye_height;host->set_camera(host->engine,&s->camera);}
    host->report_gameplay(host->engine,s->status);
}
static void restart(playground_state *s,const af_game_host *host,bool full){
    if(full){unsigned count=s->status.checkpoint_count;s->status=(af_gameplay_status){.checkpoint_count=count};s->checkpoint=s->spawn;s->camera=s->initial_camera;s->time=0;host->log("New run. Reach both checkpoints, then the finish arch.");}
    else{++s->status.deaths;host->log("Respawned at the latest checkpoint.");}
    s->player=(af_character){.position=s->checkpoint};s->goal_locked=false;host->reset_interpolation(host->engine);
}
static bool init(void *memory,const af_game_host *host){
    playground_state *s=memory;bool orders[1025]={0};
    for(size_t i=0;i<host->object_count(host->scene);++i){const af_scene_object *o=host->object_at(host->scene,i);
        if(o->gameplay.player.enabled){if(s->body){host->log("Playground requires exactly one player controller.");return false;}s->body=o->id;}
        if(o->gameplay.role==AF_ROLE_PLATFORM){if(o->parent||s->platform_count==16)return false;s->platforms[s->platform_count++]=(platform_state){o->id,o->transform};}
        if(o->gameplay.role==AF_ROLE_CHECKPOINT){unsigned order=o->gameplay.checkpoint_order;if(!order||order>1024||orders[order]||!o->collider.enabled||!o->collider.trigger)return false;orders[order]=true;++s->status.checkpoint_count;}
    }
    for(unsigned i=1;i<=s->status.checkpoint_count;++i)if(!orders[i]){host->log("Checkpoint orders must be unique and consecutive from 1.");return false;}
    const af_scene_object *body=host->get_object(host->scene,s->body);
    if(!body||body->parent||body->transform.scale.x!=1||body->transform.scale.y!=1||body->transform.scale.z!=1||body->collider.type!=AF_COLLIDER_CAPSULE||!body->collider.enabled||body->collider.trigger||!body->gameplay.camera.enabled||!body->gameplay.camera.primary){host->log("Player needs a root capsule, unit scale and a primary Camera component.");return false;}
    s->config=body->gameplay.player.controller;s->sensitivity=body->gameplay.player.mouse_sensitivity;s->eye_height=body->gameplay.camera.eye_height;
    if(fabsf(body->collider.radius-s->config.radius)>.00001f||fabsf(body->collider.half_height-s->config.half_height)>.00001f||body->collider.center.x!=0||body->collider.center.y!=0||body->collider.center.z!=0){host->log("Player capsule must match its controller dimensions and have a zero center.");return false;}
    s->spawn=body->transform.position;const af_scene_object *spawn=host->get_object(host->scene,body->gameplay.player.spawn);af_mat4 world;
    if(body->gameplay.player.spawn&&(!spawn||spawn->gameplay.role!=AF_ROLE_SPAWN)){host->log("The player's spawn reference must select a Spawn component.");return false;}
    if(spawn&&host->world_matrix(host->scene,spawn->id,&world))s->spawn=(af_vec3){world.elements[12],world.elements[13],world.elements[14]};
    s->checkpoint=s->spawn;s->player.position=s->spawn;
    if(!host->game_camera(host->scene,&s->camera))return false;s->initial_camera=s->camera;
    present(s,host);host->reset_interpolation(host->engine);host->capture_mouse(host->engine,true);host->log("Playground: WASD walk, Space jump, R new run, Escape release.");return true;
}
static void frame(void *memory,const af_game_host *host,double delta){
    (void)delta;playground_state *s=memory;
    if(host->input_enabled){
        if(host->key_pressed(host,AF_KEY_ESCAPE))host->capture_mouse(host->engine,false);
        else if(host->mouse_pressed(host->engine,AF_MOUSE_LEFT)&&host->mouse_in_viewport(host->engine))host->capture_mouse(host->engine,true);
        if(host->mouse_captured(host->engine)){af_vec2 motion=host->mouse_delta(host->engine);s->camera.yaw+=motion.x*s->sensitivity;s->camera.pitch=fminf(1.5f,fmaxf(-1.5f,s->camera.pitch-motion.y*s->sensitivity));}
    }
    /* A frame callback presents camera/status only; transforms belong to ticks. */
    if(host->camera_enabled){s->camera.position=s->player.position;s->camera.position.y+=s->eye_height;host->set_camera(host->engine,&s->camera);}
    host->report_gameplay(host->engine,s->status);
}
static void update(void *memory,const af_game_host *host,double delta){
    playground_state *s=memory;af_character_input input={0};bool controls=host->input_enabled&&host->mouse_captured(host->engine);
    if(controls&&host->key_pressed(host,AF_KEY_R)){restart(s,host,true);present(s,host);return;}
    s->time+=delta;
    for(unsigned i=0;i<s->platform_count;++i){platform_state *p=&s->platforms[i];if(host->get_object(host->scene,p->id)){af_transform t=p->base;t.position.x+=2*sinf((float)s->time*.9f);host->set_transform(host->scene,p->id,t);}}
    if(controls&&!s->status.finished){float forward=(float)host->key_down(host,AF_KEY_W)-(float)host->key_down(host,AF_KEY_S),right=(float)host->key_down(host,AF_KEY_D)-(float)host->key_down(host,AF_KEY_A);
        input.move=(af_vec3){cosf(s->camera.yaw)*forward-sinf(s->camera.yaw)*right,0,sinf(s->camera.yaw)*forward+cosf(s->camera.yaw)*right};input.jump=host->key_pressed(host,AF_KEY_SPACE);
        if(forward!=0||right!=0||input.jump)s->status.running=true;
    }
    if(s->status.running&&!s->status.finished)s->status.elapsed+=delta;
    af_query_filter filter={.ignore=s->body,.mask=UINT32_MAX};
    bool grounded=s->player.grounded;float fall_speed=s->player.velocity.y;
    if(!s->status.finished&&!host->move_character(host->scene,&s->player,s->config,input,(float)delta,filter))host->log("Character step rejected invalid state.");
    if(grounded&&input.jump&&!s->player.grounded)host->play_sound(host->engine,"jump",.45f);
    if(!grounded&&s->player.grounded&&fall_speed< -1)host->play_sound(host->engine,"land",.35f);
    filter.include_triggers=true;af_collision_hit hits[64];size_t count=host->overlap_capsule(host->scene,s->player.position,s->config.radius,s->config.half_height,filter,hits,64);bool in_goal=false;
    for(size_t i=0;i<count&&i<64&&!s->status.finished;++i){const af_scene_object *o=host->get_object(host->scene,hits[i].object);if(!o)continue;
        if(o->gameplay.role==AF_ROLE_CHECKPOINT&&o->gameplay.checkpoint_order==s->status.checkpoint+1){af_mat4 world;if(host->world_matrix(host->scene,o->id,&world)){s->status.checkpoint=o->gameplay.checkpoint_order;s->checkpoint=(af_vec3){world.elements[12],world.elements[13],world.elements[14]};host->log("Checkpoint reached.");host->play_sound(host->engine,"checkpoint",.45f);}}
        else if(o->gameplay.role==AF_ROLE_GOAL){in_goal=true;if(s->status.checkpoint==s->status.checkpoint_count){s->status.finished=true;s->status.finish_time=s->status.elapsed;s->status.running=false;host->log("Course complete! Press R for a new run.");host->play_sound(host->engine,"finish",.5f);}
            else if(!s->goal_locked){host->log("Reach every checkpoint before finishing.");s->goal_locked=true;}}
        else if(o->gameplay.role==AF_ROLE_RESTART){restart(s,host,false);break;}
    }
    if(!in_goal)s->goal_locked=false;
    if(s->player.position.y< -8)restart(s,host,false);
    present(s,host);
}
static void shutdown(void *memory,const af_game_host *host){(void)memory;host->capture_mouse(host->engine,false);}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t version){
    static const af_game_module api={.abi_version=AF_GAME_ABI_VERSION,.struct_size=sizeof(af_game_module),.host_size=sizeof(af_game_host),.object_size=sizeof(af_scene_object),.name="Afterlight obstacle course",.state_size=sizeof(playground_state),.init=init,.update=update,.shutdown=shutdown,.frame_update=frame};
    return version==AF_GAME_ABI_VERSION?&api:NULL;
}

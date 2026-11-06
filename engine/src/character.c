/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/character.h>
#include <afterlight/scene.h>
#include <math.h>
bool af_character_step_root_motion(const struct af_scene *scene,af_character *c,af_character_config config,af_vec3 displacement,bool jump,float dt,af_query_filter filter){
    if(!isfinite(dt)||dt<=0||dt>.1f||!isfinite(displacement.x)||!isfinite(displacement.y)||!isfinite(displacement.z))return false;
    displacement.y=0;float length=af_vec3_length(displacement);if(!isfinite(length)||length/dt>100)return false;
    config.speed=length/dt;return af_character_step(scene,c,config,(af_character_input){.move=length>0?af_vec3_scale(displacement,1/length):(af_vec3){0},.jump=jump},dt,filter);
}
#include <stdlib.h>
static bool finite3(af_vec3 p){return isfinite(p.x)&&isfinite(p.y)&&isfinite(p.z);}
static af_vec3 transformed(af_mat4 matrix,af_vec3 p){af_vec4 q=af_mat4_transform(matrix,(af_vec4){p.x,p.y,p.z,1});return (af_vec3){q.x,q.y,q.z};}
af_character_config af_character_config_default(void){return (af_character_config){.radius=0.35f,.half_height=0.55f,.speed=4.5f,.jump_speed=6.5f,.gravity=18,.slope_limit=45*AF_PI/180,.skin=0.002f,.ground_snap=0.15f};}
bool af_character_config_valid(af_character_config c){
    float values[]={c.radius,c.half_height,c.speed,c.jump_speed,c.gravity,c.slope_limit,c.skin,c.ground_snap};
    for(unsigned i=0;i<8;++i)if(!isfinite(values[i])||values[i]<0)return false;
    return c.radius>=0.01f&&c.radius<=10&&c.half_height<=10&&c.speed<=100&&c.jump_speed<=100&&c.gravity<=1000&&c.slope_limit<AF_PI/2&&c.skin<c.radius/2&&c.ground_snap<=2;
}
static void remember_ground(const af_scene *scene,af_character *character){
    af_mat4 matrix,inverse;
    if(character->grounded&&af_scene_world_matrix(scene,character->ground,&matrix)&&af_mat4_inverse(matrix,&inverse)){
        character->ground_local=transformed(inverse,character->position);character->ground_world=character->position;
    }else{character->grounded=false;character->ground=0;}
}
bool af_character_step(const af_scene *scene,af_character *character,af_character_config config,af_character_input input,float delta,af_query_filter filter){
    if(!scene||!character||!af_character_config_valid(config)||!finite3(character->position)||!finite3(character->velocity)||!finite3(input.move)||!isfinite(delta)||delta<=0||delta>0.1f)return false;
    af_character next=*character;bool was_grounded=next.grounded;filter.include_triggers=false;float walkable=cosf(config.slope_limit);
    if(was_grounded&&next.ground){af_mat4 matrix;af_world_collider ground;
        if(af_scene_world_collider(scene,next.ground,&ground)&&!ground.trigger&&af_scene_world_matrix(scene,next.ground,&matrix)){
            af_vec3 current=transformed(matrix,next.ground_local),carry=af_vec3_sub(current,next.ground_world);
            if(finite3(carry)){af_query_filter carry_filter=filter;carry_filter.ignore_also=next.ground;af_collision_hit obstruction;
                float fraction=af_scene_sweep_capsule(scene,next.position,config.radius,config.half_height,carry,config.skin,carry_filter,&obstruction)?obstruction.fraction:1;
                next.position=af_vec3_add(next.position,af_vec3_scale(carry,fraction));}
        }else was_grounded=false;
    }
    next.grounded=false;next.ground=0;
    /* Resolve starting overlaps before the sweep, including platform pushes. */
    for(unsigned pass=0;pass<8;++pass){af_collision_hit stack[64],deepest={0};af_collision_hit *hits=stack;size_t count=af_scene_overlap_capsule(scene,next.position,config.radius,config.half_height,filter,stack,64);
        if(count>64){hits=malloc(count*sizeof(*hits));if(!hits)return false;af_scene_overlap_capsule(scene,next.position,config.radius,config.half_height,filter,hits,count);}
        for(size_t i=0;i<count;++i)if(hits[i].penetration>deepest.penetration)deepest=hits[i];
        if(hits!=stack)free(hits);
        if(deepest.penetration<0.00001f)break;
        next.position=af_vec3_add(next.position,af_vec3_scale(deepest.normal,deepest.penetration+config.skin));
        float into=af_vec3_dot(next.velocity,deepest.normal);if(into<0)next.velocity=af_vec3_sub(next.velocity,af_vec3_scale(deepest.normal,into));
        if(deepest.normal.y>=walkable){next.grounded=true;next.ground=deepest.object;}
    }
    af_vec3 move={input.move.x,0,input.move.z};float length=af_vec3_length(move);if(!isfinite(length))return false;if(length>1)move=af_vec3_scale(move,1/length);
    float control=(was_grounded||next.grounded)?1:fminf(1,10*delta);
    if(was_grounded||next.grounded||length>0.00001f){next.velocity.x+=(move.x*config.speed-next.velocity.x)*control;next.velocity.z+=(move.z*config.speed-next.velocity.z)*control;}
    bool jumping=input.jump&&(was_grounded||next.grounded);
    if(jumping){next.velocity.y=config.jump_speed;next.grounded=false;next.ground=0;}else if(was_grounded&&next.velocity.y<0)next.velocity.y=0;
    next.velocity.y-=config.gravity*delta;
    af_vec3 remaining=af_vec3_scale(next.velocity,delta);
    for(unsigned pass=0;pass<8&&af_vec3_length(remaining)>0.000001f;++pass){af_collision_hit hit;
        if(!af_scene_sweep_capsule(scene,next.position,config.radius,config.half_height,remaining,config.skin,filter,&hit)){next.position=af_vec3_add(next.position,remaining);break;}
        next.position=af_vec3_add(next.position,af_vec3_scale(remaining,hit.fraction));remaining=af_vec3_scale(remaining,1-hit.fraction);
        float into=af_vec3_dot(remaining,hit.normal);if(into<0)remaining=af_vec3_sub(remaining,af_vec3_scale(hit.normal,into));
        into=af_vec3_dot(next.velocity,hit.normal);if(into<0)next.velocity=af_vec3_sub(next.velocity,af_vec3_scale(hit.normal,into));
        if(hit.normal.y>=walkable&&!jumping){next.grounded=true;next.ground=hit.object;}
        else if(hit.normal.y>0){remaining.y=fminf(remaining.y,0);next.velocity.y=fminf(next.velocity.y,0);}
    }
    if(!jumping&&next.velocity.y<=0&&(was_grounded||next.grounded)){
        af_collision_hit floor;
        if(af_scene_sweep_capsule(scene,next.position,config.radius,config.half_height,(af_vec3){0,-config.ground_snap,0},config.skin,filter,&floor)&&floor.normal.y>=walkable){
            next.position.y-=config.ground_snap*floor.fraction;next.grounded=true;next.ground=floor.object;
        }
    }
    if(next.grounded)next.velocity.y=0;
    if(!finite3(next.position)||!finite3(next.velocity))return false;
    remember_ground(scene,&next);*character=next;return true;
}

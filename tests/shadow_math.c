/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "shadow_math.h"
#include <math.h>
#include <stdio.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Shadow math %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void){
    af_shadow_settings s=af_shadow_settings_default();s.mode=AF_SHADOW_SINGLE;CHECK(af_shadow_settings_valid(s));
    af_camera camera=af_camera_default();camera.position=(af_vec3){0,2,5};af_shadow_view view;
    af_shadow_bounds caster=af_shadow_transform_bounds(af_mat4_identity(),(af_vec3){-1,-1,-1},(af_vec3){1,1,1});
    CHECK(af_shadow_build(&camera,1.5f,(af_vec3){-1,-1,-1},s,&caster,1,&view));CHECK(view.count==1&&view.splits[0]==s.distance);
    CHECK(af_shadow_intersects(view.matrices[0],&caster));
    af_shadow_bounds far=af_shadow_transform_bounds(af_mat4_translation((af_vec3){10000,0,0}),(af_vec3){-1,-1,-1},(af_vec3){1,1,1});CHECK(!af_shadow_intersects(view.matrices[0],&far));
    af_shadow_view vertical;CHECK(af_shadow_build(&camera,1,(af_vec3){0,-1,0},s,NULL,0,&vertical));
    camera.position.x+=view.texel_sizes[0]*0.1f;af_shadow_view moved;CHECK(af_shadow_build(&camera,1.5f,(af_vec3){-1,-1,-1},s,&caster,1,&moved));
    af_vec4 a=af_mat4_transform(view.matrices[0],(af_vec4){0,0,0,1}),b=af_mat4_transform(moved.matrices[0],(af_vec4){0,0,0,1});
    float dx=(b.x-a.x)*(float)s.resolution/2,dy=(b.y-a.y)*(float)s.resolution/2;
    CHECK(fabsf(dx-roundf(dx))<0.001f&&fabsf(dy-roundf(dy))<0.001f&&fabsf(dx)<=1.01f&&fabsf(dy)<=1.01f);
    s.mode=AF_SHADOW_CASCADED;af_shadow_view cascades;CHECK(af_shadow_build(&camera,1.5f,(af_vec3){-1,-1,-1},s,&caster,1,&cascades));
    CHECK(cascades.count==4&&cascades.splits[3]==s.distance);
    for(unsigned i=1;i<4;++i)CHECK(cascades.splits[i]>cascades.splits[i-1]&&cascades.texel_sizes[i]>cascades.texel_sizes[i-1]);
    s.split_lambda=0;CHECK(af_shadow_build(&camera,1,(af_vec3){0,-1,0},s,NULL,0,&cascades));
    CHECK(fabsf(cascades.splits[0]-(camera.near_clip+(s.distance-camera.near_clip)/4))<0.001f);
    s.split_lambda=1;CHECK(af_shadow_build(&camera,1,(af_vec3){0,-1,0},s,NULL,0,&cascades));
    CHECK(fabsf(cascades.splits[0]-camera.near_clip*powf(s.distance/camera.near_clip,0.25f))<0.001f);
    s.resolution=1000;CHECK(!af_shadow_settings_valid(s));s=af_shadow_settings_default();s.distance=NAN;CHECK(!af_shadow_settings_valid(s));
    s=af_shadow_settings_default();CHECK(!af_shadow_build(&camera,1,(af_vec3){0},s,NULL,0,&moved));
    puts("Shadow math: bounds, light fitting, vertical light, culling, texel snapping and invalid settings passed.");return 0;
}

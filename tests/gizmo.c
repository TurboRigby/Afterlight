/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "gizmo.h"
#include <math.h>
#include <stdio.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Gizmo check %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void){
    af_camera camera=af_camera_default();ed_gizmo g={0};af_rect viewport={100,50,800,600};
    ed_gizmo_project(&g,&camera,viewport,(af_vec3){0},af_mat4_identity(),(af_vec3){0});
    CHECK(g.projected&&fabsf(g.origin.x-500)<0.001f&&fabsf(g.origin.y-350)<0.001f);
    CHECK(ed_gizmo_hit(&g,g.ends[0])==0);CHECK(ed_gizmo_hit(&g,(af_vec2){0,0})==-1);
    af_transform t=af_transform_identity();ed_gizmo_begin(&g,0,1,t,g.origin);
    af_transform moved=ed_gizmo_drag(&g,g.ends[0]);CHECK(moved.position.x>0&&moved.position.y==0&&moved.position.z==0);
    g.snap=true;moved=ed_gizmo_drag(&g,g.ends[0]);CHECK(fabsf(moved.position.x*2-roundf(moved.position.x*2))<0.001f);
    g.dragging=false;g.snap=false;g.mode=ED_GIZMO_SCALE;ed_gizmo_begin(&g,0,1,t,g.origin);
    moved=ed_gizmo_drag(&g,g.ends[0]);CHECK(fabsf(moved.scale.x-2)<0.001f&&moved.scale.y==1);
    g.mode=ED_GIZMO_ROTATE;ed_gizmo_begin(&g,2,1,t,(af_vec2){g.origin.x+80,g.origin.y});
    moved=ed_gizmo_drag(&g,(af_vec2){g.origin.x,g.origin.y+80});CHECK(fabsf(moved.rotation.z+AF_PI/2)<0.001f);
    g.snap=true;moved=ed_gizmo_drag(&g,(af_vec2){g.origin.x-80,g.origin.y});CHECK(fabsf(moved.rotation.z+AF_PI)<0.001f);
    camera.position=(af_vec3){6,4,8};camera.yaw=atan2f(-8,-6);camera.pitch=atan2f(-4,10);
    af_mat4 parent=af_mat4_mul(af_mat4_rotation_z(0.4f),af_mat4_scale((af_vec3){2,1,0.7f}));
    g.dragging=false;g.snap=false;g.mode=ED_GIZMO_ROTATE;t.rotation=(af_vec3){0.2f,0.3f,0.4f};
    ed_gizmo_project(&g,&camera,viewport,(af_vec3){0},parent,t.rotation);
    ed_gizmo_begin(&g,1,1,t,g.rings[1][0]);moved=ed_gizmo_drag(&g,g.rings[1][ED_GIZMO_SEGMENTS/4]);
    CHECK(g.plane_valid&&fabsf(moved.rotation.y-t.rotation.y-AF_PI/2)<0.001f);
    camera=af_camera_default();
    af_vec2 point;CHECK(!ed_project(&camera,viewport,(af_vec3){0,0,4},&point));
    puts("Gizmos: projection, picking, constrained movement, rotation, scale, snapping passed.");return 0;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Simulation %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(void){
    const unsigned rates[]={30,60,144,240};
    for(unsigned r=0;r<4;++r){af_fixed_clock clock={0};double position=0;for(unsigned i=0;i<rates[r]*10;++i){unsigned steps=af_fixed_clock_advance(&clock,1.0/rates[r],false,false);for(unsigned n=0;n<steps;++n)position+=3*AF_FIXED_DELTA;CHECK(clock.alpha>=0&&clock.alpha<=1);}CHECK(clock.ticks==600&&fabs(position-30)<1e-8);}
    af_fixed_clock clock={0};CHECK(af_fixed_clock_advance(&clock,2,false,false)==8&&clock.dropped_seconds>1.8&&clock.alpha<1);
    CHECK(af_fixed_clock_advance(&clock,1,true,false)==0&&clock.alpha==1);uint64_t ticks=clock.ticks;CHECK(af_fixed_clock_advance(&clock,1,true,true)==1&&clock.ticks==ticks+1);CHECK(af_fixed_clock_advance(&clock,NAN,false,false)==0);
    af_scene *scene=af_scene_create();af_object_id parent=af_scene_add_object(scene,"Parent","cube"),child=af_scene_add_object(scene,"Child","cube");CHECK(af_scene_set_parent(scene,child,parent));
    af_transform t=af_transform_identity();t.position.x=2;CHECK(af_scene_set_object_transform(scene,child,t));af_scene *old=af_scene_clone(scene);t.position.x=6;CHECK(af_scene_set_object_transform(scene,child,t));t.position.x=10;CHECK(af_scene_set_object_transform(scene,parent,t));af_mat4 matrix;
    CHECK(af_scene_interpolated_world_matrix(scene,old,child,0.5f,&matrix)&&fabsf(matrix.elements[12]-9)<1e-5f);CHECK(af_scene_interpolated_world_matrix(scene,old,child,1,&matrix)&&fabsf(matrix.elements[12]-16)<1e-5f);
    af_scene_destroy(old);af_scene_destroy(scene);puts("Simulation: frame-rate independence, bounded catch-up, paused stepping and hierarchical interpolation passed.");return 0;
}

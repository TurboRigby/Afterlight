/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/locomotion.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Locomotion %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){af_locomotion m=af_locomotion_default();float dt=1.f/60;
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.grounded=true},dt)&&m.state==AF_LOCOMOTION_IDLE&&!m.landed);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.speed=.1f,.grounded=true},dt)&&m.state==AF_LOCOMOTION_IDLE);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.speed=2,.grounded=true},dt)&&m.state==AF_LOCOMOTION_WALK);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.speed=5,.grounded=true},dt)&&m.state==AF_LOCOMOTION_RUN);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.speed=3.5f,.grounded=true},dt)&&m.state==AF_LOCOMOTION_RUN);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.speed=2,.vertical_speed=5},dt)&&m.state==AF_LOCOMOTION_JUMP&&m.took_off);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.vertical_speed=-1},dt)&&m.state==AF_LOCOMOTION_FALL&&!m.took_off);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.grounded=true},dt)&&m.state==AF_LOCOMOTION_IDLE&&m.landed);
    CHECK(af_locomotion_update(&m,(af_locomotion_input){.grounded=true},dt)&&!m.landed);
    af_locomotion old=m;CHECK(!af_locomotion_update(&m,(af_locomotion_input){.speed=NAN},dt)&&!memcmp(&old,&m,sizeof(m)));
    puts("Locomotion: idle/walk/run/jump/fall, hysteresis, transition edges and invalid input passed.");return 0;}

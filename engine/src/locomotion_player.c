/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/locomotion.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
bool af_locomotion_player_init(af_locomotion_player *p,const af_animation_file *f){
    if(!p||!f||!f->node_count||f->node_count>AF_ANIMATION_MAX_NODES)return false;
    af_locomotion_player next={.motion=af_locomotion_default(),.node_count=f->node_count,.weights={1,0,0}};
    for(size_t i=0;i<5;++i){next.clips[i]=SIZE_MAX;for(size_t j=0;j<f->clip_count;++j)if(!strcmp(af_locomotion_state_name((af_locomotion_state)i),f->clips[j].name)){next.clips[i]=j;break;}if(next.clips[i]==SIZE_MAX||f->clips[next.clips[i]].duration<=0)return false;}
    next.scratch=calloc(f->node_count,sizeof(*next.scratch));next.frozen=calloc(f->node_count,sizeof(*next.frozen));if(!next.scratch||!next.frozen){free(next.scratch);free(next.frozen);return false;}
    memcpy(next.frozen,f->bind_pose,f->node_count*sizeof(*next.frozen));next.ready=true;*p=next;return true;
}
void af_locomotion_player_destroy(af_locomotion_player *p){if(p){free(p->scratch);free(p->frozen);*p=(af_locomotion_player){0};}}
static void mix(af_animation_pose *to,const af_animation_pose *from,size_t count,float t){for(size_t j=0;j<count;++j){to[j].translation=af_vec3_add(af_vec3_scale(from[j].translation,1-t),af_vec3_scale(to[j].translation,t));to[j].scale=af_vec3_add(af_vec3_scale(from[j].scale,1-t),af_vec3_scale(to[j].scale,t));to[j].rotation=af_quaternion_slerp(from[j].rotation,to[j].rotation,t);}}
bool af_locomotion_player_sample(af_locomotion_player *p,const af_animation_file *f,af_animation_pose *out,size_t capacity){
    if(!p||!p->ready||!f||p->node_count!=f->node_count||!out||capacity<f->node_count)return false;
    if(p->motion.state>=AF_LOCOMOTION_JUMP){size_t clip=p->clips[p->motion.state];if(!af_animation_sample(f,clip,p->motion.time,out,capacity))return false;}
    else {unsigned a=p->weights[0]>0?0:1,b=a+1;float total=p->weights[a]+p->weights[b];
        if(!af_animation_sample(f,p->clips[a],p->motion.phase*f->clips[p->clips[a]].duration,out,capacity))return false;
        if(p->weights[b]>0){if(!af_animation_sample(f,p->clips[b],p->motion.phase*f->clips[p->clips[b]].duration,p->scratch,p->node_count))return false;
            /* mix writes its destination: scratch -> out using the adjacent weight. */
            mix(p->scratch,out,p->node_count,p->weights[b]/total);memcpy(out,p->scratch,p->node_count*sizeof(*out));}}
    if(p->motion.transition<1)mix(out,p->frozen,p->node_count,p->motion.transition);return true;
}
bool af_locomotion_player_step(af_locomotion_player *p,const af_animation_file *f,af_locomotion_input in,float dt){
    if(!p||!p->ready||!f||f->node_count!=p->node_count)return false;af_locomotion next=p->motion;if(!af_locomotion_update(&next,in,dt))return false;
    if(next.state!=p->motion.state||!p->motion.initialized){
        /* Capture the current mixed pose on interruptions rather than a clip end. */
        af_animation_pose *capture=malloc(p->node_count*sizeof(*capture));if(!capture)return false;
        bool ok=af_locomotion_player_sample(p,f,capture,p->node_count);if(ok)memcpy(p->frozen,capture,p->node_count*sizeof(*capture));free(capture);if(!ok)return false;
    }
    float speed=in.speed;if(speed<=next.walk_speed){float t=fminf(1,speed/next.walk_speed);p->weights[0]=1-t;p->weights[1]=t;p->weights[2]=0;}else{float t=fminf(1,(speed-next.walk_speed)/(next.run_speed-next.walk_speed));p->weights[0]=0;p->weights[1]=1-t;p->weights[2]=t;}
    double rate=0;for(unsigned j=0;j<3;++j)rate+=p->weights[j]/f->clips[p->clips[j]].duration;
    p->events=(af_animation_events){0};
    if(in.grounded&&in.speed>.12f){unsigned dominant=p->weights[2]>.5f?2:1;const af_animation_clip *clip=&f->clips[p->clips[dominant]];
        if(!af_animation_collect_events(clip,next.phase*clip->duration,dt*rate*clip->duration,true,&p->events))return false;}
    if(next.landed&&p->events.count<AF_ANIMATION_EVENT_BATCH)p->events.items[p->events.count++]=(af_animation_event){.name="land"};
    next.phase=fmod(next.phase+dt*rate,1);p->motion=next;return true;
}

/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static af_vec4 add(af_vec4 a,af_vec4 b){return (af_vec4){a.x+b.x,a.y+b.y,a.z+b.z,a.w+b.w};}
static af_vec4 scale(af_vec4 q,float t){return (af_vec4){q.x*t,q.y*t,q.z*t,q.w*t};}
af_vec4 af_quaternion_normalize(af_vec4 q){float length=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);return length>1e-12f&&isfinite(length)?scale(q,1/length):(af_vec4){0,0,0,1};}
af_vec4 af_quaternion_slerp(af_vec4 a,af_vec4 b,float t){
    a=af_quaternion_normalize(a);b=af_quaternion_normalize(b);float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0){b=scale(b,-1);dot=-dot;}dot=fminf(1,dot);
    if(dot>.9995f)return af_quaternion_normalize(add(scale(a,1-t),scale(b,t)));
    float angle=acosf(dot),sine=sinf(angle);return af_quaternion_normalize(add(scale(a,sinf((1-t)*angle)/sine),scale(b,sinf(t*angle)/sine)));
}
af_vec4 af_quaternion_from_euler(af_vec3 r){
    float sx=sinf(r.x/2),cx=cosf(r.x/2),sy=sinf(r.y/2),cy=cosf(r.y/2),sz=sinf(r.z/2),cz=cosf(r.z/2);
    return (af_vec4){sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz};
}
af_mat4 af_animation_pose_matrix(af_animation_pose p){
    af_vec4 q=af_quaternion_normalize(p.rotation);float x=q.x,y=q.y,z=q.z,w=q.w;
    af_mat4 r=af_mat4_identity();float *m=r.elements;
    m[0]=1-2*(y*y+z*z);m[1]=2*(x*y+z*w);m[2]=2*(x*z-y*w);
    m[4]=2*(x*y-z*w);m[5]=1-2*(x*x+z*z);m[6]=2*(y*z+x*w);
    m[8]=2*(x*z+y*w);m[9]=2*(y*z-x*w);m[10]=1-2*(x*x+y*y);
    return af_mat4_mul(af_mat4_translation(p.translation),af_mat4_mul(r,af_mat4_scale(p.scale)));
}
void af_animation_file_free(af_animation_file *file){
    if(!file)return;for(size_t i=0;file->clips&&i<file->clip_count;++i){af_animation_clip *clip=&file->clips[i];for(size_t j=0;clip->channels&&j<clip->channel_count;++j){free(clip->channels[j].times);free(clip->channels[j].values);}free(clip->channels);}
    free(file->clips);free(file->bind_pose);free(file->parents);free(file->skins);*file=(af_animation_file){0};
}
static af_vec4 channel_value(const af_animation_channel *c,double time){
    size_t lo=0,hi=c->key_count-1;while(lo+1<hi){size_t mid=lo+(hi-lo)/2;if(time<c->times[mid])hi=mid;else lo=mid;}
    size_t stride=c->interpolation==AF_ANIM_CUBIC?3u:1u,offset=stride==3?1u:0u;
    if(time<=c->times[0])return c->values[offset];
    if(time>=c->times[c->key_count-1])return c->values[(c->key_count-1)*stride+offset];
    if(c->interpolation==AF_ANIM_STEP)return c->values[lo];
    float dt=c->times[hi]-c->times[lo],t=(float)((time-c->times[lo])/dt);
    af_vec4 a=c->values[lo*stride+offset],b=c->values[hi*stride+offset];
    if(c->interpolation==AF_ANIM_LINEAR)return c->path==AF_ANIM_ROTATION?af_quaternion_slerp(a,b,t):add(scale(a,1-t),scale(b,t));
    float t2=t*t,t3=t2*t;return add(add(scale(a,2*t3-3*t2+1),scale(c->values[lo*3+2],dt*(t3-2*t2+t))),add(scale(b,-2*t3+3*t2),scale(c->values[hi*3],dt*(t3-t2))));
}
bool af_animation_sample(const af_animation_file *file,size_t clip_index,double time,af_animation_pose *out,size_t capacity){
    if(!file||!out||capacity<file->node_count||clip_index>=file->clip_count||!isfinite(time))return false;
    memcpy(out,file->bind_pose,file->node_count*sizeof(*out));const af_animation_clip *clip=&file->clips[clip_index];
    for(size_t i=0;i<clip->channel_count;++i){const af_animation_channel *c=&clip->channels[i];af_vec4 v=channel_value(c,time);af_animation_pose *p=&out[c->node];
        if(c->path==AF_ANIM_TRANSLATION)p->translation=(af_vec3){v.x,v.y,v.z};else if(c->path==AF_ANIM_SCALE)p->scale=(af_vec3){v.x,v.y,v.z};else p->rotation=af_quaternion_normalize(v);
        float components[]={p->translation.x,p->translation.y,p->translation.z,p->scale.x,p->scale.y,p->scale.z,v.x,v.y,v.z,v.w};for(unsigned k=0;k<10;++k)if(!isfinite(components[k]))return false;
    }return true;
}
bool af_animation_root_delta(const af_animation_file *f,size_t clip,uint32_t node,double time,double delta,bool loop,af_vec3 *out){
    if(!f||!out||clip>=f->clip_count||node>=f->node_count||!isfinite(time)||fabs(time)>1e9||!isfinite(delta)||fabs(delta)>8640000)return false;
    const af_animation_clip *c=&f->clips[clip];double d=c->duration;if(!isfinite(d)||d<=0)return false;
    *out=(af_vec3){0};const af_animation_channel *track=NULL;for(size_t i=0;i<c->channel_count;++i)if(c->channels[i].node==node&&c->channels[i].path==AF_ANIM_TRANSLATION){track=&c->channels[i];break;}if(!track)return true;
    double a=time,b=time+delta,cycles=0;if(loop){double ca=floor(a/d),cb=floor(b/d);cycles=cb-ca;a-=ca*d;b-=cb*d;}else{a=fmax(0,fmin(d,a));b=fmax(0,fmin(d,b));}
    af_vec4 va=channel_value(track,a),vb=channel_value(track,b),start=channel_value(track,0),end=channel_value(track,d);
    *out=(af_vec3){(float)(vb.x-va.x+cycles*(end.x-start.x)),(float)(vb.y-va.y+cycles*(end.y-start.y)),(float)(vb.z-va.z+cycles*(end.z-start.z))};return isfinite(out->x)&&isfinite(out->y)&&isfinite(out->z);
}
static bool world_node(const af_animation_file *file,const af_animation_pose *poses,size_t node,af_mat4 *world,unsigned char *ready,unsigned depth){
    if(ready[node])return true;if(depth>=128)return false;af_mat4 local=af_animation_pose_matrix(poses[node]);int32_t parent=file->parents[node];
    if(parent>=0){if((size_t)parent>=file->node_count||!world_node(file,poses,(size_t)parent,world,ready,depth+1))return false;local=af_mat4_mul(world[parent],local);}for(unsigned i=0;i<16;++i)if(!isfinite(local.elements[i]))return false;world[node]=local;ready[node]=1;return true;
}
bool af_animation_world_matrices(const af_animation_file *file,const af_animation_pose *poses,af_mat4 *world,size_t capacity){
    if(!file||!poses||!world||capacity<file->node_count||file->node_count>AF_ANIMATION_MAX_NODES)return false;
    unsigned char ready[AF_ANIMATION_MAX_NODES]={0};for(size_t i=0;i<file->node_count;++i)if(!world_node(file,poses,i,world,ready,0))return false;return true;
}
bool af_animation_skin_palette(const af_animation_file *file,size_t index,uint32_t mesh_node,const af_mat4 *world,af_mat4 *palette,size_t capacity){
    if(!file||!world||!palette||index>=file->skin_count||mesh_node>=file->node_count||capacity<file->skins[index].joint_count)return false;
    af_mat4 inverse;if(!af_mat4_inverse(world[mesh_node],&inverse))return false;const af_animation_skin *skin=&file->skins[index];
    for(size_t i=0;i<skin->joint_count;++i){if(skin->joints[i]>=file->node_count)return false;palette[i]=af_mat4_mul(inverse,af_mat4_mul(world[skin->joints[i]],skin->inverse_bind[i]));for(unsigned j=0;j<16;++j)if(!isfinite(palette[i].elements[j]))return false;}return true;
}

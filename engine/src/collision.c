/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/collision.h>
#include <afterlight/scene.h>
#include <math.h>
#include <float.h>
#include "triangle_collision.h"
static float clamp(float x,float a,float b){return fminf(b,fmaxf(a,x));}
static bool finite3(af_vec3 v){return isfinite(v.x)&&isfinite(v.y)&&isfinite(v.z);}
static af_vec3 point(af_mat4 m,af_vec3 p){af_vec4 q=af_mat4_transform(m,(af_vec4){p.x,p.y,p.z,1});return (af_vec3){q.x,q.y,q.z};}
static af_vec3 axis(unsigned i){return (af_vec3){i==0?1:0,i==1?1:0,i==2?1:0};}
static af_vec3 safe_normal(af_vec3 v){float length=af_vec3_length(v);return length>1e-7f?af_vec3_scale(v,1/length):(af_vec3){0,1,0};}
af_collider af_collider_default(void){return (af_collider){.enabled=true,.half_extents={0.5f,0.5f,0.5f},.radius=0.5f,.half_height=0.5f,.layer=1};}
af_query_filter af_query_filter_default(void){return (af_query_filter){.mask=UINT32_MAX};}
bool af_collider_valid(af_collider c){
    if(c.type==AF_COLLIDER_MESH&&af_vec3_length(c.center)>1e-6f)return false;
    if(c.type<AF_COLLIDER_NONE||c.type>AF_COLLIDER_MESH||!finite3(c.center)||!finite3(c.half_extents)||!isfinite(c.radius)||!isfinite(c.half_height)||!c.layer)return false;
    if(c.half_extents.x<0||c.half_extents.y<0||c.half_extents.z<0||c.radius<0||c.half_height<0||c.radius>10000||c.half_height>10000||c.half_extents.x>10000||c.half_extents.y>10000||c.half_extents.z>10000)return false;
    if(c.type==AF_COLLIDER_BOX&&(c.half_extents.x<=0||c.half_extents.y<=0||c.half_extents.z<=0))return false;
    return (c.type!=AF_COLLIDER_SPHERE&&c.type!=AF_COLLIDER_CAPSULE)||c.radius>0;
}
bool af_scene_world_collider(const af_scene *scene,uint64_t id,af_world_collider *out){
    const af_scene_object *o=af_scene_get_object(scene,id);af_mat4 model;
    if(!out||!o||!o->collider.enabled||o->collider.type==AF_COLLIDER_NONE||!af_scene_world_matrix(scene,id,&model))return false;
    af_world_collider w={.object=id,.type=o->collider.type,.trigger=o->collider.trigger,.layer=o->collider.layer};float scale[3];
    for(unsigned i=0;i<3;++i){w.axes[i]=(af_vec3){model.elements[i*4],model.elements[i*4+1],model.elements[i*4+2]};scale[i]=af_vec3_length(w.axes[i]);
        if(!isfinite(scale[i])||scale[i]<1e-6f)return false;w.axes[i]=af_vec3_scale(w.axes[i],1/scale[i]);}
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j)if(fabsf(af_vec3_dot(w.axes[i],w.axes[j]))>0.001f)return false;
    if(w.type==AF_COLLIDER_MESH){w.mesh=af_scene_collision_mesh(scene,o->mesh);af_vec3 low,high;if(!w.mesh||!af_collision_mesh_bounds(w.mesh,&low,&high)||!af_mat4_inverse(model,&w.inverse))return false;w.matrix=model;af_vec3 center=af_vec3_scale(af_vec3_add(low,high),.5f),half=af_vec3_scale(af_vec3_sub(high,low),.5f);w.center=point(model,center);w.half_extents=(af_vec3){half.x*scale[0],half.y*scale[1],half.z*scale[2]};w.a=w.b=w.center;*out=w;return finite3(w.center);}
    w.center=point(model,o->collider.center);if(!finite3(w.center))return false;
    w.half_extents=(af_vec3){o->collider.half_extents.x*scale[0],o->collider.half_extents.y*scale[1],o->collider.half_extents.z*scale[2]};
    w.radius=o->collider.radius*fmaxf(scale[0],fmaxf(scale[1],scale[2]));
    af_vec3 length=af_vec3_scale(w.axes[1],o->collider.type==AF_COLLIDER_CAPSULE?o->collider.half_height*scale[1]:0);
    w.a=af_vec3_sub(w.center,length);w.b=af_vec3_add(w.center,length);*out=w;return true;
}
static bool matches(af_world_collider c,af_query_filter f){return c.object!=f.ignore&&c.object!=f.ignore_also&&(c.layer&f.mask)&&(!c.trigger||f.include_triggers);}
static af_vec3 closest_segment(af_vec3 a,af_vec3 b,af_vec3 p){af_vec3 d=af_vec3_sub(b,a);float length2=af_vec3_dot(d,d);return af_vec3_add(a,af_vec3_scale(d,length2>1e-12f?clamp(af_vec3_dot(af_vec3_sub(p,a),d)/length2,0,1):0));}
static void segment_pair(af_vec3 p,af_vec3 q,af_vec3 a,af_vec3 b,af_vec3 *one,af_vec3 *two){
    af_vec3 u=af_vec3_sub(q,p),v=af_vec3_sub(b,a),r=af_vec3_sub(p,a);float aa=af_vec3_dot(u,u),bb=af_vec3_dot(v,v),uv=af_vec3_dot(u,v),ur=af_vec3_dot(u,r),vr=af_vec3_dot(v,r),s=0,t=0;
    if(aa<=1e-12f)t=bb>1e-12f?clamp(vr/bb,0,1):0;
    else if(bb<=1e-12f)s=clamp(-ur/aa,0,1);
    else{float den=aa*bb-uv*uv;s=den>1e-12f?clamp((uv*vr-ur*bb)/den,0,1):0;t=(uv*s+vr)/bb;
        if(t<0){t=0;s=clamp(-ur/aa,0,1);}else if(t>1){t=1;s=clamp((uv-ur)/aa,0,1);}}
    *one=af_vec3_add(p,af_vec3_scale(u,s));*two=af_vec3_add(a,af_vec3_scale(v,t));
}
/* Minimize squared segment/AABB distance over its piecewise quadratic intervals. */
static void segment_box(af_vec3 a,af_vec3 b,af_vec3 half,af_vec3 *segment,af_vec3 *box){
    float p[]={a.x,a.y,a.z},d[]={b.x-a.x,b.y-a.y,b.z-a.z},h[]={half.x,half.y,half.z},breaks[8]={0,1};unsigned count=2;
    for(unsigned i=0;i<3;++i)if(fabsf(d[i])>1e-12f)for(int sign=-1;sign<=1;sign+=2){float t=((float)sign*h[i]-p[i])/d[i];if(t>0&&t<1)breaks[count++]=t;}
    for(unsigned i=1;i<count;++i){float t=breaks[i];unsigned j=i;while(j&&breaks[j-1]>t){breaks[j]=breaks[j-1];--j;}breaks[j]=t;}
    float best=FLT_MAX,best_t=0;
    for(unsigned k=1;k<count;++k){float low=breaks[k-1],high=breaks[k],middle=(low+high)/2,num=0,den=0;
        for(unsigned i=0;i<3;++i){float value=p[i]+d[i]*middle;if(value>h[i]||value< -h[i]){float bound=value>0?h[i]:-h[i];num+=d[i]*(p[i]-bound);den+=d[i]*d[i];}}
        float t=den>1e-12f?clamp(-num/den,low,high):middle,distance=0;
        for(unsigned i=0;i<3;++i){float value=p[i]+d[i]*t,error=value-clamp(value,-h[i],h[i]);distance+=error*error;}
        if(distance<best){best=distance;best_t=t;}
    }
    *segment=af_vec3_add(a,af_vec3_scale(af_vec3_sub(b,a),best_t));*box=(af_vec3){clamp(segment->x,-half.x,half.x),clamp(segment->y,-half.y,half.y),clamp(segment->z,-half.z,half.z)};
}
static af_vec3 box_local(af_world_collider box,af_vec3 p){p=af_vec3_sub(p,box.center);return (af_vec3){af_vec3_dot(p,box.axes[0]),af_vec3_dot(p,box.axes[1]),af_vec3_dot(p,box.axes[2])};}
static af_vec3 box_vector(af_world_collider box,af_vec3 p){return af_vec3_add(af_vec3_scale(box.axes[0],p.x),af_vec3_add(af_vec3_scale(box.axes[1],p.y),af_vec3_scale(box.axes[2],p.z)));}
static float capsule_distance(af_world_collider target,af_vec3 center,float radius,float half_height,af_collision_hit *hit){
    if(target.type==AF_COLLIDER_MESH)return tc_mesh_distance(target,center,radius,half_height,hit);
    af_vec3 a=af_vec3_add(center,(af_vec3){0,-half_height,0}),b=af_vec3_add(center,(af_vec3){0,half_height,0}),one,two,normal;float separation;
    if(target.type==AF_COLLIDER_BOX){
        af_vec3 la=box_local(target,a),lb=box_local(target,b);segment_box(la,lb,target.half_extents,&one,&two);af_vec3 delta=af_vec3_sub(one,two);float distance=af_vec3_length(delta);
        if(distance>1e-7f){normal=af_vec3_scale(delta,1/distance);separation=distance-radius;}
        else{float pa[]={la.x,la.y,la.z},pb[]={lb.x,lb.y,lb.z},h[]={target.half_extents.x,target.half_extents.y,target.half_extents.z};float depth=FLT_MAX;normal=(af_vec3){0,1,0};
            for(unsigned i=0;i<3;++i){float positive=h[i]-fminf(pa[i],pb[i])+radius,negative=fmaxf(pa[i],pb[i])+h[i]+radius;
                if(positive<depth){depth=positive;normal=axis(i);}if(negative<depth){depth=negative;normal=af_vec3_scale(axis(i),-1);}}
            separation=-depth;two=af_vec3_add(one,af_vec3_scale(normal,depth-radius));
        }
        normal=box_vector(target,normal);two=af_vec3_add(target.center,box_vector(target,two));
    }else{
        segment_pair(a,b,target.a,target.b,&one,&two);af_vec3 delta=af_vec3_sub(one,two);float distance=af_vec3_length(delta);normal=safe_normal(delta);separation=distance-radius-target.radius;two=af_vec3_add(two,af_vec3_scale(normal,target.radius));
    }
    *hit=(af_collision_hit){.object=target.object,.point=two,.normal=normal,.penetration=fmaxf(0,-separation),.trigger=target.trigger};return separation;
}
size_t af_scene_overlap_capsule(const af_scene *scene,af_vec3 center,float radius,float half_height,af_query_filter filter,af_collision_hit *hits,size_t capacity){
    if(!finite3(center)||!isfinite(radius)||radius<=0||!isfinite(half_height)||half_height<0||(!hits&&capacity))return 0;
    size_t count=0;
    for(size_t i=0;i<af_scene_object_count(scene);++i){af_world_collider target;af_collision_hit hit;
        if(!af_scene_world_collider(scene,af_scene_object_at(scene,i)->id,&target)||!matches(target,filter))continue;
        if(capsule_distance(target,center,radius,half_height,&hit)<=0){if(count<capacity)hits[count]=hit;++count;}}
    return count;
}
size_t af_scene_overlap_sphere(const af_scene *scene,af_vec3 center,float radius,af_query_filter filter,af_collision_hit *hits,size_t capacity){return af_scene_overlap_capsule(scene,center,radius,0,filter,hits,capacity);}
bool af_collider_sweep_sphere(af_world_collider target,af_vec3 center,float radius,af_vec3 displacement,float skin,af_collision_hit *out){float speed=af_vec3_length(displacement);if(!out||!finite3(center)||!finite3(displacement)||!isfinite(speed)||speed<1e-8f||!isfinite(radius)||radius<=0||!isfinite(skin)||skin<0)return false;if(target.type==AF_COLLIDER_MESH)return tc_mesh_sweep(target,center,radius,0,displacement,skin,out);float t=0;af_collision_hit hit={0};for(unsigned i=0;i<40&&t<=1;++i){float distance=capsule_distance(target,af_vec3_add(center,af_vec3_scale(displacement,t)),radius,0,&hit),approach=-af_vec3_dot(displacement,hit.normal);if(distance<=skin+.00001f){if(t==0&&approach<=1e-7f)return false;hit.fraction=t;hit.distance=t*speed;*out=hit;return true;}if(approach<=1e-8f)return false;t+=fmaxf((distance-skin)/approach,.000001f);}return false;}
bool af_scene_sweep_capsule(const af_scene *scene,af_vec3 center,float radius,float half_height,af_vec3 displacement,float skin,af_query_filter filter,af_collision_hit *out){
    float speed=af_vec3_length(displacement);
    if(!out||!finite3(center)||!finite3(displacement)||!isfinite(speed)||speed<1e-8f||!isfinite(radius)||radius<=0||!isfinite(half_height)||half_height<0||!isfinite(skin)||skin<0)return false;
    bool found=false;float closest=1.00001f;af_collision_hit best={0};
    for(size_t i=0;i<af_scene_object_count(scene);++i){af_world_collider target;if(!af_scene_world_collider(scene,af_scene_object_at(scene,i)->id,&target)||!matches(target,filter))continue;
        if(target.type==AF_COLLIDER_MESH){af_collision_hit hit;if(tc_mesh_sweep(target,center,radius,half_height,displacement,skin,&hit)&&hit.fraction<closest){best=hit;closest=hit.fraction;found=true;}continue;}
        float t=0;af_collision_hit hit={0};
        for(unsigned iteration=0;iteration<40&&t<=1&&t<closest;++iteration){
            float separation=capsule_distance(target,af_vec3_add(center,af_vec3_scale(displacement,t)),radius,half_height,&hit),approach=-af_vec3_dot(displacement,hit.normal);
            if(separation<=skin+0.00001f){if(t==0&&separation>=-0.0001f&&approach<=1e-7f)break;hit.fraction=t;hit.distance=t*speed;best=hit;closest=t;found=true;break;}
            if(approach<=1e-8f)break;
            float advance=(separation-skin)/approach;t+=fmaxf(advance,0.000001f);
            if(iteration==39&&t<=1&&t<closest){hit.fraction=t;hit.distance=t*speed;best=hit;closest=t;found=true;}
        }
    }if(found)*out=best;return found;
}
static bool ray_sphere(af_vec3 o,af_vec3 d,af_vec3 center,float radius,float *time){af_vec3 q=af_vec3_sub(o,center);float c=af_vec3_dot(q,q)-radius*radius,b=af_vec3_dot(q,d);if(c<=0){*time=0;return true;}float h=b*b-c;if(h<0)return false;float t=-b-sqrtf(h);if(t<0)return false;*time=t;return true;}
bool af_scene_raycast(const af_scene *scene,af_vec3 origin,af_vec3 direction,float maximum,af_query_filter filter,af_collision_hit *out){
    float length=af_vec3_length(direction);if(!out||!finite3(origin)||!finite3(direction)||!isfinite(length)||length<1e-7f||!isfinite(maximum)||maximum<0)return false;direction=af_vec3_scale(direction,1/length);bool found=false;
    for(size_t i=0;i<af_scene_object_count(scene);++i){af_world_collider target;if(!af_scene_world_collider(scene,af_scene_object_at(scene,i)->id,&target)||!matches(target,filter))continue;
        float t=FLT_MAX;af_vec3 normal=af_vec3_scale(direction,-1);bool hit=false;
        if(target.type==AF_COLLIDER_MESH){af_collision_hit mesh_hit;if(tc_mesh_ray(target,origin,direction,maximum,&mesh_hit)){maximum=mesh_hit.distance;*out=mesh_hit;found=true;}continue;}
        if(target.type==AF_COLLIDER_BOX){af_vec3 p=box_local(target,origin);float o[]={p.x,p.y,p.z},d[]={af_vec3_dot(direction,target.axes[0]),af_vec3_dot(direction,target.axes[1]),af_vec3_dot(direction,target.axes[2])},h[]={target.half_extents.x,target.half_extents.y,target.half_extents.z};float near=0,far=maximum;hit=true;
            for(unsigned k=0;k<3;++k){if(fabsf(d[k])<1e-9f){if(o[k]< -h[k]||o[k]>h[k])hit=false;continue;}float a=(-h[k]-o[k])/d[k],b=(h[k]-o[k])/d[k];af_vec3 n=af_vec3_scale(target.axes[k],d[k]>0?-1:1);if(a>b){float temp=a;a=b;b=temp;}if(a>near){near=a;normal=n;}far=fminf(far,b);if(near>far)hit=false;}t=near;
        }else if(target.type==AF_COLLIDER_SPHERE){hit=ray_sphere(origin,direction,target.center,target.radius,&t);if(hit&&t>0)normal=safe_normal(af_vec3_sub(af_vec3_add(origin,af_vec3_scale(direction,t)),target.center));}
        else{af_vec3 closest=closest_segment(target.a,target.b,origin);if(af_vec3_length(af_vec3_sub(origin,closest))<=target.radius){hit=true;t=0;}
            else{af_vec3 ba=af_vec3_sub(target.b,target.a),oa=af_vec3_sub(origin,target.a);float bb=af_vec3_dot(ba,ba),bd=af_vec3_dot(ba,direction),bo=af_vec3_dot(ba,oa),a=bb-bd*bd,b=bb*af_vec3_dot(direction,oa)-bo*bd,c=bb*af_vec3_dot(oa,oa)-bo*bo-target.radius*target.radius*bb,disc=b*b-a*c;
                if(a>1e-10f&&disc>=0){float candidate=(-b-sqrtf(disc))/a,y=bo+candidate*bd;if(candidate>=0&&y>=0&&y<=bb){t=candidate;hit=true;}}
                float cap;if(ray_sphere(origin,direction,target.a,target.radius,&cap)&&cap<t){t=cap;hit=true;}if(ray_sphere(origin,direction,target.b,target.radius,&cap)&&cap<t){t=cap;hit=true;}
                if(hit){af_vec3 p=af_vec3_add(origin,af_vec3_scale(direction,t));normal=safe_normal(af_vec3_sub(p,closest_segment(target.a,target.b,p)));}}
        }
        if(hit&&t<=maximum){maximum=t;*out=(af_collision_hit){.object=target.object,.point=af_vec3_add(origin,af_vec3_scale(direction,t)),.normal=normal,.distance=t,.trigger=target.trigger};found=true;}
    }return found;
}

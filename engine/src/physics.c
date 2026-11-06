/* SPDX-License-Identifier: MPL-2.0 */
#include "physics_internal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
af_physics *af_physics_create(void){return calloc(1,sizeof(af_physics));}
void af_physics_destroy(af_physics *p){free(p);}
void af_physics_reset(af_physics *p){if(p)memset(p,0,sizeof(*p));}
bool af_physics_active(const af_scene *s){for(size_t i=0;i<af_scene_object_count(s);++i){const af_rigid_body *b=&af_scene_object_at(s,i)->body;if(b->enabled&&b->type!=AF_BODY_STATIC)return true;}return false;}
const char *af_physics_error(const af_physics *p){return p?p->error:"No physics world.";}
static bool fail(af_physics *p,const char *text){snprintf(p->error,sizeof(p->error),"%s",text);return false;}
static ph_body *find(af_physics *p,af_object_id id){for(size_t i=0;i<p->count;++i)if(p->bodies[i].id==id)return &p->bodies[i];return NULL;}
af_vec3 ph_vector(const ph_body *b,af_vec3 v){return af_vec3_add(af_vec3_scale(b->shape.axes[0],v.x),af_vec3_add(af_vec3_scale(b->shape.axes[1],v.y),af_vec3_scale(b->shape.axes[2],v.z)));}
af_vec3 ph_local(const ph_body *b,af_vec3 v){return (af_vec3){af_vec3_dot(v,b->shape.axes[0]),af_vec3_dot(v,b->shape.axes[1]),af_vec3_dot(v,b->shape.axes[2])};}
af_vec3 ph_inverse_inertia(const ph_body *b,af_vec3 v){if(!b->inv_mass)return (af_vec3){0};af_vec3 local=ph_local(b,v);return ph_vector(b,(af_vec3){local.x*b->inertia.x,local.y*b->inertia.y,local.z*b->inertia.z});}
static af_vec3 euler(af_vec4 q){af_mat4 m=af_animation_pose_matrix((af_animation_pose){.rotation=q,.scale={1,1,1}});float y=asinf(fmaxf(-1,fminf(1,-m.elements[2])));if(fabsf(cosf(y))<1e-5f)return (af_vec3){atan2f(-m.elements[9],m.elements[5]),y,0};return (af_vec3){atan2f(m.elements[6],m.elements[10]),y,atan2f(m.elements[1],m.elements[0])};}
void ph_shape(ph_body *b){b->shape.center=b->p;af_mat4 m=af_animation_pose_matrix((af_animation_pose){.rotation=b->q,.scale={1,1,1}});for(unsigned i=0;i<3;++i)b->shape.axes[i]=(af_vec3){m.elements[i*4],m.elements[i*4+1],m.elements[i*4+2]};b->shape.a=b->shape.b=b->p;}
bool ph_sync(af_physics *p,af_scene *s,float dt){size_t count=0;if(!p||!s)return false;
    for(size_t i=0;i<af_scene_object_count(s);++i){const af_scene_object *o=af_scene_object_at(s,i);if(o->body.enabled&&!af_rigid_body_valid(o->body))return fail(p,"Invalid rigid body fields.");
        if(o->body.enabled&&o->body.type==AF_BODY_DYNAMIC&&(o->parent||af_vec3_length(o->collider.center)>1e-6f||o->collider.trigger||(o->collider.type!=AF_COLLIDER_BOX&&o->collider.type!=AF_COLLIDER_SPHERE)||!o->collider.enabled||o->transform.scale.x<=0||o->transform.scale.y<=0||o->transform.scale.z<=0))return fail(p,"Dynamic bodies require centered root sphere/box colliders with positive scale.");
        af_world_collider shape;if(!af_scene_world_collider(s,o->id,&shape)||shape.trigger||(shape.type!=AF_COLLIDER_BOX&&shape.type!=AF_COLLIDER_SPHERE))continue;
        if(count==AF_PHYSICS_MAX_BODIES||!ph_finite(shape.center)||af_vec3_length(shape.center)>1000000)return fail(p,"Physics exceeds 1024 shapes or the 1,000,000 unit world bound.");
        ph_body *old=p->scene==s?find(p,o->id):NULL;ph_body b=old?*old:(ph_body){.id=o->id,.p=shape.center,.v=o->body.velocity,.w=o->body.angular_velocity,.q=af_quaternion_from_euler(o->transform.rotation),.published=o->transform};b.scene_index=i;
        af_body_type type=o->body.enabled?o->body.type:AF_BODY_STATIC;bool moved=old&&memcmp(&o->transform,&old->published,sizeof(o->transform));
        if(type==AF_BODY_DYNAMIC){if(moved){b.p=shape.center;b.q=af_quaternion_from_euler(o->transform.rotation);b.sleeping=false;b.sleep_time=0;}if(old&&memcmp(&o->body.velocity,&old->config.velocity,sizeof(af_vec3))){b.v=o->body.velocity;b.sleeping=false;b.sleep_time=0;}if(old&&memcmp(&o->body.angular_velocity,&old->config.angular_velocity,sizeof(af_vec3))){b.w=o->body.angular_velocity;b.sleeping=false;b.sleep_time=0;}}
        else{b.p=shape.center;b.v=type==AF_BODY_KINEMATIC&&old&&dt>0?af_vec3_scale(af_vec3_sub(shape.center,old->shape.center),1/dt):(af_vec3){0};b.w=(af_vec3){0};if(type==AF_BODY_KINEMATIC&&old&&dt>0)for(unsigned k=0;k<3;++k)b.w=af_vec3_add(b.w,af_vec3_scale(af_vec3_cross(old->shape.axes[k],shape.axes[k]),.5f/dt));}
        if(old&&memcmp(&o->body,&old->config,sizeof(o->body))){b.sleeping=false;b.sleep_time=0;}b.config=o->body;b.config.type=type;b.shape=shape;b.published=o->transform;b.inv_mass=type==AF_BODY_DYNAMIC?1/o->body.mass:0;
        if(b.inv_mass){float mass=o->body.mass;af_vec3 h=shape.half_extents;if(shape.type==AF_COLLIDER_SPHERE){float inverse=2.5f/(mass*shape.radius*shape.radius);b.inertia=(af_vec3){inverse,inverse,inverse};}else b.inertia=(af_vec3){3/(mass*(h.y*h.y+h.z*h.z)),3/(mass*(h.x*h.x+h.z*h.z)),3/(mass*(h.x*h.x+h.y*h.y))};ph_shape(&b);}
        p->scratch[count++]=b;
    }
    if(p->scene!=s){p->contact_count=p->cache_count=0;p->stats=(af_physics_stats){0};}memcpy(p->bodies,p->scratch,count*sizeof(ph_body));p->count=count;p->scene=s;p->error[0]=0;return true;
}
void ph_impulse(ph_body *b,af_vec3 impulse,af_vec3 point){if(!b->inv_mass)return;b->v=ph_limit(af_vec3_add(b->v,af_vec3_scale(impulse,b->inv_mass)),200);b->w=ph_limit(af_vec3_add(b->w,ph_inverse_inertia(b,af_vec3_cross(af_vec3_sub(point,b->p),impulse))),100);}
static bool act(af_physics *p,af_scene *s,af_object_id id,af_vec3 value,af_vec3 point,bool impulse){if(!p||!s||!ph_finite(value)||!ph_finite(point)||af_vec3_length(value)>1000000)return false;ph_body *b=p->scene==s?find(p,id):NULL;if(!b){if(!ph_sync(p,s,0))return false;b=find(p,id);}if(!b||!b->inv_mass)return false;b->sleeping=false;b->sleep_time=0;if(impulse)ph_impulse(b,value,point);else{b->force=ph_limit(af_vec3_add(b->force,value),1000000);b->torque=ph_limit(af_vec3_add(b->torque,af_vec3_cross(af_vec3_sub(point,b->p),value)),1000000);}return true;}
bool af_physics_apply_force(af_physics *p,af_scene *s,af_object_id id,af_vec3 f,af_vec3 at){return act(p,s,id,f,at,false);}
bool af_physics_apply_impulse(af_physics *p,af_scene *s,af_object_id id,af_vec3 j,af_vec3 at){return act(p,s,id,j,at,true);}
bool af_physics_wake(af_physics *p,af_object_id id){ph_body *b=p?find(p,id):NULL;if(!b||!b->inv_mass)return false;b->sleeping=false;b->sleep_time=0;return true;}
af_physics_state af_physics_get_state(const af_physics *p,af_object_id id){if(p)for(size_t i=0;i<p->count;++i){const ph_body *b=&p->bodies[i];if(b->id==id)return (af_physics_state){.available=true,.sleeping=b->sleeping,.type=b->config.type,.position=b->p,.velocity=b->v,.angular_velocity=b->w,.orientation=b->q,.mass=b->config.mass};}return (af_physics_state){0};}
af_physics_stats af_physics_get_stats(const af_physics *p){return p?p->stats:(af_physics_stats){0};}
size_t af_physics_contacts(const af_physics *p,af_physics_contact *out,size_t cap){if(!p)return 0;for(size_t i=0;out&&i<p->contact_count&&i<cap;++i)out[i]=p->contacts[i].view;return p->contact_count;}
static void rotate(ph_body *b,float dt){af_vec4 q=b->q;af_vec3 v={q.x,q.y,q.z},cross=af_vec3_cross(b->w,v);af_vec3 xyz=af_vec3_add(af_vec3_scale(b->w,q.w),cross);b->q=af_quaternion_normalize((af_vec4){q.x+xyz.x*dt*.5f,q.y+xyz.y*dt*.5f,q.z+xyz.z*dt*.5f,q.w-af_vec3_dot(b->w,v)*dt*.5f});}
bool af_physics_step(af_physics *p,af_scene *s,float dt){if(!p||!s||!isfinite(dt)||dt<=0||dt>.1f)return false;if(!ph_sync(p,s,dt))return false;uint64_t ticks=p->stats.ticks+1;p->stats=(af_physics_stats){.bodies=p->count,.ticks=ticks};unsigned steps=(unsigned)ceilf(dt*240-1e-5f);if(!steps)steps=1;float h=dt/(float)steps;p->stats.substeps=steps;
    for(unsigned sub=0;sub<steps;++sub){for(size_t i=0;i<p->count;++i){ph_body *b=&p->bodies[i];if(!b->inv_mass||b->sleeping)continue;af_vec3 acceleration=af_vec3_add(af_vec3_scale(b->force,b->inv_mass),(af_vec3){0,-9.81f*b->config.gravity_scale,0});b->v=ph_limit(af_vec3_scale(af_vec3_add(b->v,af_vec3_scale(acceleration,h)),expf(-b->config.linear_damping*h)),200);b->w=ph_limit(af_vec3_scale(af_vec3_add(b->w,af_vec3_scale(ph_inverse_inertia(b,b->torque),h)),expf(-b->config.angular_damping*h)),100);b->p=af_vec3_add(b->p,af_vec3_scale(b->v,h));rotate(b,h);ph_shape(b);}
        /* Collision generation and solving are attached in the next milestones. */
    }
    for(size_t i=0;i<p->count;++i){ph_body *b=&p->bodies[i];b->force=b->torque=(af_vec3){0};if(b->config.type==AF_BODY_KINEMATIC)++p->stats.kinematic_bodies;if(!b->inv_mass)continue;++p->stats.dynamic_bodies;if(b->sleeping)++p->stats.sleeping;af_scene_object *o=(af_scene_object*)af_scene_object_at(s,b->scene_index);o->transform.position=b->p;o->transform.rotation=euler(b->q);o->body.velocity=b->v;o->body.angular_velocity=b->w;b->published=o->transform;b->config=o->body;}
    return true;
}

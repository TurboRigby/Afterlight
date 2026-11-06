/* SPDX-License-Identifier: MPL-2.0 */

#include "physics_internal.h"
#include <string.h>
static af_vec3 add(af_vec3 a,af_vec3 b){return af_vec3_add(a,b);}static af_vec3 sub(af_vec3 a,af_vec3 b){return af_vec3_sub(a,b);}static af_vec3 mul(af_vec3 a,float b){return af_vec3_scale(a,b);}static float dot(af_vec3 a,af_vec3 b){return af_vec3_dot(a,b);}
static size_t index_of(af_physics *p,af_object_id id){for(size_t i=0;i<p->count;++i)if(p->bodies[i].id==id)return i;return SIZE_MAX;}
bool ph_joints_sync(af_physics *p){p->joint_count=0;p->world=(ph_body){.q={0,0,0,1}};for(unsigned k=0;k<3;++k)p->world.shape.axes[k]=ph_axis(k);
    for(size_t i=0;i<af_scene_joint_count(p->scene);++i){af_joint j=*af_scene_joint_at(p->scene,i);if(!j.enabled)continue;size_t a=index_of(p,j.a),b=index_of(p,j.b);if(!af_joint_valid(j)||a==SIZE_MAX||(j.b&&b==SIZE_MAX)||(p->bodies[a].inv_mass==0&&(b==SIZE_MAX||p->bodies[b].inv_mass==0)))return false;
        size_t n=p->joint_count++;p->joints[n].config=j;p->joints[n].a=a;p->joints[n].b=b;
        /* Constraint islands do not yet sleep; this keeps tension and moving
           supports responsive without stale cached impulses. */
        p->bodies[a].sleeping=false;p->bodies[a].sleep_time=0;
        if(b!=SIZE_MAX){p->bodies[b].sleeping=false;p->bodies[b].sleep_time=0;}
    }return true;
}
bool ph_collision_allowed(const af_physics *p,af_object_id a,af_object_id b){for(size_t i=0;i<p->joint_count;++i){const af_joint *j=&p->joints[i].config;if(!j->collide_connected&&((j->a==a&&j->b==b)||(j->a==b&&j->b==a)))return false;}return true;}
static af_vec3 velocity(ph_body *b,af_vec3 r){return add(b->v,af_vec3_cross(b->w,r));}
static void turn(ph_body *b,af_vec3 angle){float length=af_vec3_length(angle);if(length<1e-9f)return;angle=mul(angle,fminf(length,.2f)/length);af_vec3 q={b->q.x,b->q.y,b->q.z},d=mul(add(mul(angle,b->q.w),af_vec3_cross(angle,q)),.5f);b->q=af_quaternion_normalize((af_vec4){b->q.x+d.x,b->q.y+d.y,b->q.z+d.z,b->q.w-.5f*dot(angle,q)});ph_shape(b);}
static void position(ph_body *b,af_vec3 j,af_vec3 r){if(!b->inv_mass)return;b->p=add(b->p,mul(j,b->inv_mass));turn(b,ph_inverse_inertia(b,af_vec3_cross(r,j)));ph_shape(b);}
static void linear(ph_body *a,ph_body *b,af_vec3 ra,af_vec3 rb,af_vec3 n,float error,float dt,bool pos){float mass=a->inv_mass+b->inv_mass+dot(n,add(af_vec3_cross(ph_inverse_inertia(a,af_vec3_cross(ra,n)),ra),af_vec3_cross(ph_inverse_inertia(b,af_vec3_cross(rb,n)),rb)));if(mass<1e-10f)return;
    float speed=dot(sub(velocity(b,rb),velocity(a,ra)),n);float value=pos?-.3f*fmaxf(-.2f,fminf(.2f,error))/mass:-(speed+.1f*error/dt)/mass;af_vec3 j=mul(n,fmaxf(-10000,fminf(10000,value)));if(pos){position(a,mul(j,-1),ra);position(b,j,rb);}else{ph_impulse(a,mul(j,-1),add(a->p,ra));ph_impulse(b,j,add(b->p,rb));}}
void ph_joints_solve(af_physics *p,float dt,bool pos){for(size_t i=0;i<p->joint_count;++i){const af_joint *j=&p->joints[i].config;ph_body *a=&p->bodies[p->joints[i].a],*b=p->joints[i].b==SIZE_MAX?&p->world:&p->bodies[p->joints[i].b];af_vec3 ra=ph_vector(a,j->anchor_a),rb=ph_vector(b,j->anchor_b),delta=sub(add(b->p,rb),add(a->p,ra));float length=af_vec3_length(delta);
    if(j->distance>1e-6f){af_vec3 n=length>1e-6f?mul(delta,1/length):(af_vec3){0,1,0};linear(a,b,ra,rb,n,length-j->distance,dt,pos);}else for(unsigned k=0;k<3;++k)linear(a,b,ra,rb,ph_axis(k),ph_get(delta,k),dt,pos);
}}

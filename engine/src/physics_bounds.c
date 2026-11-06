/* SPDX-License-Identifier: MPL-2.0 */

#include "physics_internal.h"
#include <stdlib.h>
static int compare(const void *one,const void *two){const ph_bound *a=one,*b=two;return a->key_low<b->key_low?-1:a->key_low>b->key_low?1:a->body<b->body?-1:a->body>b->body;}
bool ph_bounds_overlap(ph_bound a,ph_bound b){return a.low.x<=b.high.x&&a.high.x>=b.low.x&&a.low.y<=b.high.y&&a.high.y>=b.low.y&&a.low.z<=b.high.z&&a.high.z>=b.low.z;}
void ph_bounds(af_physics *p,ph_bound *out,float dt,bool swept){af_vec3 lo={1e30f,1e30f,1e30f},hi={-1e30f,-1e30f,-1e30f};for(size_t i=0;i<p->count;++i){ph_body *b=&p->bodies[i];af_vec3 extent={b->shape.radius,b->shape.radius,b->shape.radius};bool moving=b->inv_mass>0&&!b->sleeping;
    if(swept&&moving){float r=b->shape.type==AF_COLLIDER_SPHERE?b->shape.radius:af_vec3_length(b->shape.half_extents);extent=(af_vec3){r,r,r};}else if(b->shape.type!=AF_COLLIDER_SPHERE){extent=(af_vec3){0};for(unsigned k=0;k<3;++k){af_vec3 axis=b->shape.axes[k];float h=ph_get(b->shape.half_extents,k);extent=af_vec3_add(extent,(af_vec3){fabsf(axis.x)*h,fabsf(axis.y)*h,fabsf(axis.z)*h});}}
    extent=af_vec3_add(extent,(af_vec3){.006f,.006f,.006f});af_vec3 low=af_vec3_sub(b->p,extent),high=af_vec3_add(b->p,extent),end=swept&&moving?af_vec3_scale(b->v,dt):(af_vec3){0};low=af_vec3_add(low,(af_vec3){fminf(0,end.x),fminf(0,end.y),fminf(0,end.z)});high=af_vec3_add(high,(af_vec3){fmaxf(0,end.x),fmaxf(0,end.y),fmaxf(0,end.z)});out[i]=(ph_bound){.body=i,.low=low,.high=high};lo=(af_vec3){fminf(lo.x,b->p.x),fminf(lo.y,b->p.y),fminf(lo.z,b->p.z)};hi=(af_vec3){fmaxf(hi.x,b->p.x),fmaxf(hi.y,b->p.y),fmaxf(hi.z,b->p.z)};
    }af_vec3 spread=af_vec3_sub(hi,lo);unsigned axis=spread.y>spread.x?1:0;if(spread.z>ph_get(spread,axis))axis=2;for(size_t i=0;i<p->count;++i){out[i].key_low=ph_get(out[i].low,axis);out[i].key_high=ph_get(out[i].high,axis);}if(p->count>1)qsort(out,p->count,sizeof(*out),compare);
}

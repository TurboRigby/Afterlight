/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "collider_view.h"
#include <math.h>
static void line(ed_line3 *out,size_t capacity,size_t *count,af_vec3 a,af_vec3 b){if(*count<capacity)out[*count]=(ed_line3){a,b};++*count;}
static af_vec3 ring(af_vec3 center,af_vec3 x,af_vec3 y,float radius,float angle){return af_vec3_add(center,af_vec3_scale(af_vec3_add(af_vec3_scale(x,cosf(angle)),af_vec3_scale(y,sinf(angle))),radius));}
size_t ed_collider_lines(af_world_collider c,ed_line3 *out,size_t capacity){
    if(!out&&capacity)return 0;size_t count=0;
    if(c.type==AF_COLLIDER_BOX){af_vec3 corners[8];float half[]={c.half_extents.x,c.half_extents.y,c.half_extents.z};
        for(unsigned i=0;i<8;++i){corners[i]=c.center;for(unsigned k=0;k<3;++k)corners[i]=af_vec3_add(corners[i],af_vec3_scale(c.axes[k],half[k]*(i&(1u<<k)?1:-1)));}
        for(unsigned i=0;i<8;++i)for(unsigned k=0;k<3;++k)if(!(i&(1u<<k)))line(out,capacity,&count,corners[i],corners[i|(1u<<k)]);
    }else if(c.type==AF_COLLIDER_SPHERE){for(unsigned plane=0;plane<3;++plane)for(unsigned i=0;i<32;++i)
        line(out,capacity,&count,ring(c.center,c.axes[plane],c.axes[(plane+1)%3],c.radius,2*AF_PI*(float)i/32),ring(c.center,c.axes[plane],c.axes[(plane+1)%3],c.radius,2*AF_PI*(float)(i+1)/32));
    }else if(c.type==AF_COLLIDER_CAPSULE){
        for(unsigned i=0;i<32;++i)for(unsigned cap=0;cap<2;++cap){af_vec3 center=cap?c.b:c.a;line(out,capacity,&count,ring(center,c.axes[0],c.axes[2],c.radius,2*AF_PI*(float)i/32),ring(center,c.axes[0],c.axes[2],c.radius,2*AF_PI*(float)(i+1)/32));}
        for(unsigned side=0;side<4;++side){af_vec3 radial=af_vec3_add(af_vec3_scale(c.axes[0],cosf((float)side*AF_PI/2)),af_vec3_scale(c.axes[2],sinf((float)side*AF_PI/2)));
            line(out,capacity,&count,af_vec3_add(c.a,af_vec3_scale(radial,c.radius)),af_vec3_add(c.b,af_vec3_scale(radial,c.radius)));
            for(unsigned i=0;i<16;++i){float a=(float)i*AF_PI/32,b=(float)(i+1)*AF_PI/32;
                line(out,capacity,&count,ring(c.b,radial,c.axes[1],c.radius,a),ring(c.b,radial,c.axes[1],c.radius,b));
                line(out,capacity,&count,ring(c.a,radial,af_vec3_scale(c.axes[1],-1),c.radius,a),ring(c.a,radial,af_vec3_scale(c.axes[1],-1),c.radius,b));}
        }
    }return count;
}

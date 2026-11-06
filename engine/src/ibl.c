/* SPDX-License-Identifier: MPL-2.0 */
#include "ibl.h"
#include <math.h>
#include <stdlib.h>

static float radical_inverse(uint32_t bits){
    bits=(bits<<16)|(bits>>16);bits=((bits&0x55555555u)<<1)|((bits&0xaaaaaaaau)>>1);
    bits=((bits&0x33333333u)<<2)|((bits&0xccccccccu)>>2);bits=((bits&0x0f0f0f0fu)<<4)|((bits&0xf0f0f0f0u)>>4);
    bits=((bits&0x00ff00ffu)<<8)|((bits&0xff00ff00u)>>8);return (float)bits*2.3283064365386963e-10f;
}
af_vec3 af_ibl_direction(float u,float v){float theta=v*AF_PI,phi=(u-.5f)*2*AF_PI;return (af_vec3){sinf(theta)*cosf(phi),cosf(theta),sinf(theta)*sinf(phi)};}
af_vec3 af_ibl_radiance(af_vec3 n,uint32_t profile){
    n=af_vec3_normalize(n);float sky=fmaxf(0,n.y),ground=fmaxf(0,-n.y);
    af_vec3 value={.12f+.28f*sky-.08f*ground,.14f+.35f*sky-.10f*ground,.18f+.50f*sky-.14f*ground};
    if(profile==1){value=(af_vec3){.5f+.2f*sky-.4f*ground,.19f+.18f*sky-.15f*ground,.1f+.3f*sky-.07f*ground};
        float sun=powf(fmaxf(0,af_vec3_dot(n,af_vec3_normalize((af_vec3){1,.18f,.3f}))),180);
        value=af_vec3_add(value,af_vec3_scale((af_vec3){14,5,1.2f},sun));
    }else if(profile==2)value=(af_vec3){.18f+.55f*sky-.13f*ground,.2f+.58f*sky-.14f*ground,.24f+.65f*sky-.17f*ground};
    else{
        float key=powf(fmaxf(0,af_vec3_dot(n,af_vec3_normalize((af_vec3){-1,1.5f,1}))),48);
        float rim=powf(fmaxf(0,af_vec3_dot(n,af_vec3_normalize((af_vec3){1,.4f,-1}))),72);
        value=af_vec3_add(value,af_vec3_add(af_vec3_scale((af_vec3){10,8,6},key),af_vec3_scale((af_vec3){2,4,9},rim)));
    }return value;
}
static af_vec3 hemisphere(af_vec3 n,float x,float y,float roughness,bool ggx){
    float phi=2*AF_PI*x,alpha=roughness*roughness;
    float cosine=ggx?sqrtf((1-y)/(1+(alpha*alpha-1)*y)):sqrtf(1-y);
    float sine=sqrtf(fmaxf(0,1-cosine*cosine));af_vec3 up=fabsf(n.y)<.99f?(af_vec3){0,1,0}:(af_vec3){1,0,0};
    af_vec3 t=af_vec3_normalize(af_vec3_cross(up,n)),b=af_vec3_cross(n,t);
    return af_vec3_add(af_vec3_scale(n,cosine),af_vec3_add(af_vec3_scale(t,cosf(phi)*sine),af_vec3_scale(b,sinf(phi)*sine)));
}
static void store(float *pixels,size_t index,af_vec3 value){pixels[index*4]=value.x;pixels[index*4+1]=value.y;pixels[index*4+2]=value.z;pixels[index*4+3]=1;}
void af_ibl_free(af_ibl_images *images){if(!images)return;free(images->specular);free(images->diffuse);free(images->brdf);*images=(af_ibl_images){0};}
af_vec3 af_ibl_sample(const float *rgba,uint32_t width,uint32_t height,af_vec3 d){
    if(!rgba||!width||!height)return (af_vec3){0};d=af_vec3_normalize(d);
    float u=atan2f(d.z,d.x)/(2*AF_PI)+.5f,v=acosf(fmaxf(-1,fminf(1,d.y)))/AF_PI;
    float px=u*(float)width-.5f,py=v*(float)height-.5f;int x0=(int)floorf(px),y0=(int)floorf(py);float tx=px-floorf(px),ty=py-floorf(py);af_vec3 result={0};
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){int sx=((x0+x)%(int)width+(int)width)%(int)width,sy=y0+y;sy=sy<0?0:sy>=(int)height?(int)height-1:sy;
        const float *p=rgba+((size_t)sy*width+(unsigned)sx)*4;float weight=(x?tx:1-tx)*(y?ty:1-ty);result=af_vec3_add(result,af_vec3_scale((af_vec3){p[0],p[1],p[2]},weight));}
    return result;
}
static af_vec3 radiance(const float *source,uint32_t width,uint32_t height,af_vec3 direction,uint32_t profile){return source?af_ibl_sample(source,width,height,direction):af_ibl_radiance(direction,profile);}
static bool bake(uint32_t profile,const float *source,uint32_t width,uint32_t height,af_ibl_images *out,af_ibl_progress progress,void *user){
    if(!out)return false;*out=(af_ibl_images){0};if(profile>2)return false;
    size_t pixels=0;uint32_t w=AF_IBL_WIDTH,h=AF_IBL_HEIGHT;
    for(unsigned level=0;level<AF_IBL_LEVELS;++level){pixels+=(size_t)w*h;w=w>1?w/2:1;h=h>1?h/2:1;}
    out->specular=calloc(pixels*4,sizeof(float));out->diffuse=calloc(AF_IBL_DIFFUSE_WIDTH*AF_IBL_DIFFUSE_HEIGHT*4,sizeof(float));out->brdf=calloc(AF_IBL_LUT_SIZE*AF_IBL_LUT_SIZE*4,sizeof(float));
    if(!out->specular||!out->diffuse||!out->brdf){af_ibl_free(out);return false;}
    unsigned rows=0,total_rows=AF_IBL_DIFFUSE_HEIGHT+AF_IBL_LUT_SIZE;for(unsigned level=0,hh=AF_IBL_HEIGHT;level<AF_IBL_LEVELS;++level,hh=hh>1?hh/2:1)total_rows+=hh;
    size_t offset=0;w=AF_IBL_WIDTH;h=AF_IBL_HEIGHT;
    for(unsigned level=0;level<AF_IBL_LEVELS;++level){float roughness=(float)level/(float)(AF_IBL_LEVELS-1);
        for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x){if(!x&&progress&&!progress(user,(float)rows++/(float)total_rows)){af_ibl_free(out);return false;}af_vec3 n=af_ibl_direction(((float)x+.5f)/(float)w,((float)y+.5f)/(float)h),value={0};float weight=0;
            if(level==0)value=radiance(source,width,height,n,profile);
            else for(uint32_t sample=0;sample<128;++sample){af_vec3 half=hemisphere(n,(float)sample/128,radical_inverse(sample),roughness,true);
                af_vec3 l=af_vec3_sub(af_vec3_scale(half,2*af_vec3_dot(n,half)),n);float nl=fmaxf(0,af_vec3_dot(n,l));
                value=af_vec3_add(value,af_vec3_scale(radiance(source,width,height,l,profile),nl));weight+=nl;}
            if(level)value=af_vec3_scale(value,1/fmaxf(weight,.0001f));store(out->specular,offset+(size_t)y*w+x,value);
        }
        offset+=(size_t)w*h;w=w>1?w/2:1;h=h>1?h/2:1;
    }
    for(uint32_t y=0;y<AF_IBL_DIFFUSE_HEIGHT;++y)for(uint32_t x=0;x<AF_IBL_DIFFUSE_WIDTH;++x){if(!x&&progress&&!progress(user,(float)rows++/(float)total_rows)){af_ibl_free(out);return false;}af_vec3 n=af_ibl_direction(((float)x+.5f)/(float)AF_IBL_DIFFUSE_WIDTH,((float)y+.5f)/(float)AF_IBL_DIFFUSE_HEIGHT),value={0};
        for(uint32_t sample=0;sample<256;++sample)value=af_vec3_add(value,radiance(source,width,height,hemisphere(n,(float)sample/256,radical_inverse(sample),1,false),profile));
        store(out->diffuse,(size_t)y*AF_IBL_DIFFUSE_WIDTH+x,af_vec3_scale(value,1.0f/256));
    }
    for(uint32_t y=0;y<AF_IBL_LUT_SIZE;++y)for(uint32_t x=0;x<AF_IBL_LUT_SIZE;++x){if(!x&&progress&&!progress(user,(float)rows++/(float)total_rows)){af_ibl_free(out);return false;}float nv=((float)x+.5f)/(float)AF_IBL_LUT_SIZE,roughness=((float)y+.5f)/(float)AF_IBL_LUT_SIZE;
        af_vec3 n={0,0,1},v={sqrtf(1-nv*nv),0,nv};float scale=0,bias=0;
        for(uint32_t sample=0;sample<256;++sample){af_vec3 half=hemisphere(n,(float)sample/256,radical_inverse(sample),roughness,true);
            float vh=fmaxf(0,af_vec3_dot(v,half));af_vec3 l=af_vec3_sub(af_vec3_scale(half,2*vh),v);float nl=fmaxf(0,l.z),nh=fmaxf(half.z,.00001f);
            if(nl>0){float k=roughness*roughness*.5f;float geometry=nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);float visibility=geometry*vh/(nh*nv);float fresnel=powf(1-vh,5);scale+=(1-fresnel)*visibility;bias+=fresnel*visibility;}}
        store(out->brdf,(size_t)y*AF_IBL_LUT_SIZE+x,(af_vec3){scale/256,bias/256,0});
    }
    return true;
}

bool af_ibl_bake(uint32_t profile,af_ibl_images *out){return bake(profile,NULL,0,0,out,NULL,NULL);}
bool af_ibl_bake_hdr(const float *rgba,uint32_t width,uint32_t height,af_ibl_images *out){return af_ibl_bake_hdr_progress(rgba,width,height,out,NULL,NULL);}
bool af_ibl_bake_hdr_progress(const float *rgba,uint32_t width,uint32_t height,af_ibl_images *out,af_ibl_progress progress,void *user){
    if(!out)return false;*out=(af_ibl_images){0};if(!rgba||!width||!height||width>4096||height>2048)return false;
    for(size_t i=0;i<(size_t)width*height*4;++i)if(!isfinite(rgba[i])||rgba[i]<0||rgba[i]>65504)return false;
    return bake(0,rgba,width,height,out,progress,user);
}

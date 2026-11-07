#version 450
#extension GL_GOOGLE_include_directive : require
// SPDX-License-Identifier: MPL-2.0
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
layout(set=0,binding=0) uniform sampler2D source_image;
layout(set=0,binding=1) uniform sampler2D bloom_image;
layout(set=0,binding=2) uniform sampler2D lookup_image;
layout(push_constant) uniform Post {vec4 settings;vec4 blur_params;vec4 viewport;vec4 aa;vec4 temporal;} post;
#include "fxaa.glsl"
#include "smaa.glsl"
#include "taa.glsl"
vec3 sample_source(vec2 position){
    vec2 half_texel=0.5/vec2(textureSize(source_image,0));
    vec2 midpoint=(post.viewport.xy+post.viewport.zw)*0.5;
    return texture(source_image,clamp(position,min(post.viewport.xy+half_texel,midpoint),max(post.viewport.zw-half_texel,midpoint))).rgb;
}
void main(){
    int mode=int(post.blur_params.z);
    if(mode>=11){
        bool inside=all(greaterThanEqual(uv,post.viewport.xy))&&all(lessThan(uv,post.viewport.zw));if(!inside){out_color=vec4(0,0,0,1);return;}
        vec4 movement=texture(lookup_image,uv);
        if(mode==11){float d=log2(1+max(movement.w,0))/10;out_color=vec4(vec3(clamp(d,0,1)),1);}
        else if(mode==12)out_color=vec4(movement.z<0?vec3(1,0,1):vec3(clamp(.5+movement.xy*vec2(textureSize(source_image,0))*.05,vec2(0),vec2(1)),.5),1);
        else if(mode==13){vec3 c=max(texture(bloom_image,uv).rgb,vec3(0));out_color=vec4(c/(1+c),1);}
        else{vec2 p=uv+movement.xy;bool valid=post.aa.w>.5&&movement.z>0&&all(greaterThanEqual(p,post.viewport.xy))&&all(lessThan(p,post.viewport.zw));float old=texture(bloom_image,clamp(p,vec2(0),vec2(1))).a;valid=valid&&abs(old-movement.z)<=max(.02,.015*movement.z);out_color=vec4(valid?vec3(0,.5,0):vec3(1,0,0),1);}
        return;
    }
    if(mode==10){out_color=taa_resolve();return;}
    if(mode>=7){out_color=smaa_pass(mode);return;}
    if(mode==6){
        bool inside=all(greaterThanEqual(uv,post.viewport.xy))&&all(lessThan(uv,post.viewport.zw));
        vec3 perceptual=inside?(post.aa.x==3?taa_finish():fxaa(uv)):texture(source_image,uv).rgb;
        out_color=vec4(perceptual*perceptual,1);return;
    }
    if(mode==0||mode==5){
        vec3 color=max(texture(source_image,uv).rgb,vec3(0));
        if(post.settings.z>0&&all(greaterThanEqual(uv,post.viewport.xy))&&all(lessThan(uv,post.viewport.zw)))
            color+=texture(bloom_image,uv).rgb*post.settings.z;
        color*=post.settings.x;
        if(post.settings.y>0.5)color=color/(vec3(1)+color);
        out_color=vec4(mode==5?sqrt(max(color,vec3(0))):color,1);return;
    }
    if(any(lessThan(uv,post.viewport.xy))||any(greaterThanEqual(uv,post.viewport.zw))){out_color=vec4(0);return;}
    vec3 color=vec3(0);
    if(mode==1){
        for(int y=0;y<2;++y)for(int x=0;x<2;++x){
            vec3 value=sample_source(uv+(vec2(x,y)-0.5)*post.blur_params.xy);
            float brightness=max(value.r,max(value.g,value.b));
            float knee=max(post.settings.w*0.5,0.0001);
            float soft=clamp(brightness-post.settings.w+knee,0.0,2.0*knee);
            soft=soft*soft/(4.0*knee);
            color+=value*(max(brightness-post.settings.w,soft)/max(brightness,0.0001))*0.25;
        }
    }else{
        vec2 step_uv=(mode==2?vec2(post.blur_params.x,0):vec2(0,post.blur_params.y))*post.blur_params.w;
        // Pair adjacent Gaussian weights using the linear sampler.
        color=sample_source(uv)*0.227027027;
        color+=(sample_source(uv+step_uv*1.384615385)+sample_source(uv-step_uv*1.384615385))*0.316216217;
        color+=(sample_source(uv+step_uv*3.230769231)+sample_source(uv-step_uv*3.230769231))*0.070270270;
    }
    out_color=vec4(color,1);
}

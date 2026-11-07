// SPDX-License-Identifier: MPL-2.0
// HDR temporal resolve. Alpha stores linear eye depth; negative previous depth
// marks reactive/new geometry. History is always on the unjittered output grid.
vec2 taa_clamp(vec2 p){
    vec2 half_pixel=.5/vec2(textureSize(source_image,0));
    return clamp(p,post.viewport.xy+half_pixel,post.viewport.zw-half_pixel);
}
vec3 taa_ycocg(vec3 c){return vec3(dot(c,vec3(.25,.5,.25)),dot(c,vec3(.5,0,-.5)),dot(c,vec3(-.25,.5,-.25)));}
vec3 taa_rgb(vec3 c){return vec3(c.x+c.y-c.z,c.x+c.z,c.x-c.y-c.z);}
vec4 taa_resolve(){
    if(any(lessThan(uv,post.viewport.xy))||any(greaterThanEqual(uv,post.viewport.zw)))return vec4(texture(source_image,uv).rgb,65504);
    vec2 step_uv=1.0/vec2(textureSize(source_image,0));
    vec2 current_uv=taa_clamp(uv+post.temporal.xy);
    vec3 current=texture(source_image,current_uv).rgb;
    // Nearest foreground motion avoids interpolating velocity/depth across edges.
    ivec2 pixel=ivec2(current_uv/step_uv);
    vec4 movement=texelFetch(lookup_image,pixel,0);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        ivec2 p=ivec2(taa_clamp(current_uv+vec2(x,y)*step_uv)/step_uv);
        vec4 candidate=texelFetch(lookup_image,p,0);
        if(candidate.w<movement.w)movement=candidate;
    }
    vec2 old_uv=uv+movement.xy;
    bool valid=post.aa.w>.5&&movement.z>0&&all(greaterThanEqual(old_uv,post.viewport.xy+step_uv*.5))&&all(lessThanEqual(old_uv,post.viewport.zw-step_uv*.5));
    vec4 history=texture(bloom_image,clamp(old_uv,vec2(0),vec2(1)));
    // F16 depth storage needs a relative tolerance. Reject disoccluded surfaces.
    valid=valid&&abs(history.a-movement.z)<=max(.02,.015*movement.z);
    vec3 lo=vec3(1e30),hi=vec3(-1e30),mean=vec3(0),square_mean=vec3(0);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec3 c=taa_ycocg(texture(source_image,taa_clamp(current_uv+vec2(x,y)*step_uv)).rgb);
        lo=min(lo,c);hi=max(hi,c);mean+=c/9.0;square_mean+=c*c/9.0;
    }
    vec3 sigma=sqrt(max(square_mean-mean*mean,vec3(0)));
    lo=max(lo,mean-1.5*sigma);hi=min(hi,mean+1.5*sigma);
    vec3 previous=taa_rgb(clamp(taa_ycocg(history.rgb),lo,hi));
    float speed=length(movement.xy/step_uv);
    float weight=valid?post.aa.z*mix(1.0,.75,clamp(speed/16.0,0,1)):0;
    float depth=texelFetch(lookup_image,pixel,0).w;
    return vec4(clamp(mix(current,previous,weight),vec3(0),vec3(60000)),depth);
}
vec3 taa_finish(){
    vec3 color=texture(source_image,uv).rgb;
    vec2 size=vec2(textureSize(source_image,0));
    vec2 p=clamp(uv+post.temporal.xy,post.viewport.xy+.5/size,post.viewport.zw-.5/size);
    if(texture(lookup_image,p).z<0)return fxaa(uv);
    vec3 lo=color,hi=color,sum=vec3(0);
    for(int i=0;i<4;++i){vec2 d=i<2?vec2(i==0?-1:1,0):vec2(0,i==2?-1:1);vec3 c=texture(source_image,taa_clamp(uv+d/size)).rgb;lo=min(lo,c);hi=max(hi,c);sum+=c;}
    return clamp(color+(color-sum*.25)*post.aa.y,lo,hi);
}

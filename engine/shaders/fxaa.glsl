// SPDX-License-Identifier: MPL-2.0
// Fast approximate edge filtering in perceptual color, following Lottes' FXAA.
// All taps stay within the scene viewport so editor panels cannot bleed into it.
vec3 aa_sample(vec2 p){
    vec2 half_texel=.5/vec2(textureSize(source_image,0));
    vec2 middle=(post.viewport.xy+post.viewport.zw)*.5;
    return texture(source_image,clamp(p,min(post.viewport.xy+half_texel,middle),max(post.viewport.zw-half_texel,middle))).rgb;
}
float aa_luma(vec3 c){return dot(c,vec3(.299,.587,.114));}
vec3 fxaa(vec2 p){
    vec2 pixel=1.0/vec2(textureSize(source_image,0));
    vec3 center=aa_sample(p),nw=aa_sample(p+pixel*vec2(-1,-1)),ne=aa_sample(p+pixel*vec2(1,-1));
    vec3 sw=aa_sample(p+pixel*vec2(-1,1)),se=aa_sample(p+pixel*vec2(1,1));
    float m=aa_luma(center),a=aa_luma(nw),b=aa_luma(ne),c=aa_luma(sw),d=aa_luma(se);
    float low=min(m,min(min(a,b),min(c,d))),high=max(m,max(max(a,b),max(c,d)));
    if(high-low<max(.0312,high*.125))return center;
    vec2 direction=vec2(-((a+b)-(c+d)),(a+c)-(b+d));
    float reduction=max((a+b+c+d)*(.25*.125),1.0/128.0);
    direction=clamp(direction/(min(abs(direction.x),abs(direction.y))+reduction),vec2(-8),vec2(8))*pixel;
    vec3 first=.5*(aa_sample(p+direction*(1.0/3.0-.5))+aa_sample(p+direction*(2.0/3.0-.5)));
    vec3 second=first*.5+.25*(aa_sample(p-direction*.5)+aa_sample(p+direction*.5));
    float value=aa_luma(second);
    vec3 filtered=value<low||value>high?first:second;
    vec3 sharpened=filtered+post.aa.y*(center-(nw+ne+sw+se)*.25);
    return clamp(sharpened,min(center,min(min(nw,ne),min(sw,se))),max(center,max(max(nw,ne),max(sw,se))));
}

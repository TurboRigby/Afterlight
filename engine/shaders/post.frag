#version 450
#extension GL_GOOGLE_include_directive : require
// SPDX-License-Identifier: MPL-2.0
layout(location=0) in vec2 uv;
layout(location=0) out vec4 out_color;
layout(set=0,binding=0) uniform sampler2D source_image;
layout(set=0,binding=1) uniform sampler2D bloom_image;
layout(set=0,binding=2) uniform sampler2D lookup_image;
layout(push_constant) uniform Post {
    vec4 settings;      // [0]: exposure, tone_mapper (0=off, 1=reinhard, 2=aces, 3=agx), bloom_strength, bloom_threshold
    vec4 blur_params;   // [1]: step_x, step_y, mode, bloom_radius
    vec4 viewport;      // [2]: x0, y0, x1, y1
    vec4 aa;            // [3]: aa_mode, sharpness, taa_history_weight, history_valid
    vec4 temporal;      // [4]: jitter_x, jitter_y, time, motion_blur_strength
    vec4 grading;       // [5]: saturation, contrast, tint_r, tint_g
    vec4 lens;          // [6]: tint_b, vignette_intensity, vignette_smoothness, chromatic_aberration
    vec4 dof_grain;     // [7]: dof_focus_distance, dof_focus_range, dof_blur_radius, film_grain_intensity
} post;
#include "fxaa.glsl"
#include "smaa.glsl"
#include "taa.glsl"

vec3 sample_source(vec2 position){
    vec2 half_texel=0.5/vec2(textureSize(source_image,0));
    vec2 midpoint=(post.viewport.xy+post.viewport.zw)*0.5;
    return texture(source_image,clamp(position,min(post.viewport.xy+half_texel,midpoint),max(post.viewport.zw-half_texel,midpoint))).rgb;
}

// Interleaved gradient noise for per-pixel rotation and jitter
float interleaved_gradient_noise(vec2 screen_coord) {
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(screen_coord, magic.xy)));
}

// 16-tap Vogel spiral (Fermat\x27s spiral) distribution for smooth bokeh
const vec2 VOGEL_DISK[16] = vec2[](
    vec2( 0.176777,  0.000000),
    vec2(-0.225772,  0.206826),
    vec2( 0.034558, -0.393771),
    vec2( 0.284571,  0.371173),
    vec2(-0.522223, -0.092374),
    vec2( 0.494695, -0.314685),
    vec2(-0.165466,  0.615525),
    vec2(-0.315561, -0.607594),
    vec2( 0.684642,  0.250030),
    vec2(-0.712256,  0.294009),
    vec2( 0.343355, -0.733729),
    vec2( 0.253730,  0.808932),
    vec2(-0.764746, -0.443186),
    vec2( 0.897134, -0.197232),
    vec2(-0.547507,  0.778772),
    vec2(-0.126487, -0.976090)
);

vec3 sample_filtered_source(vec2 p){
    float dof_radius = post.dof_grain.z;
    float mb_strength = post.temporal.w;

    float coc = 0.0;
    if (dof_radius > 0.0001) {
        float depth = texture(lookup_image, p).w;
        if (depth <= 0.0) depth = 1000.0;
        float dist = abs(depth - post.dof_grain.x);
        float blur_amount = smoothstep(0.0, 1.0, clamp(dist / max(post.dof_grain.y, 0.01), 0.0, 1.0));
        coc = blur_amount * dof_radius;
    }

    vec2 vel = vec2(0.0);
    if (mb_strength > 0.001) {
        vel = texture(lookup_image, p).xy * mb_strength;
        float vel_len = length(vel);
        float max_vel = 0.08;
        if (vel_len > max_vel) {
            vel *= (max_vel / vel_len);
        }
    }

    bool has_dof = coc > 0.0002;
    bool has_mb = length(vel) > 0.0002;

    if (!has_dof && !has_mb) {
        return sample_source(p);
    }

    vec3 col = sample_source(p);

    // Screen-space aspect ratio correction so bokeh is circular
    vec2 img_size = vec2(textureSize(source_image, 0));
    float aspect = img_size.x / max(img_size.y, 1.0);
    vec2 aspect_scale = vec2(1.0 / aspect, 1.0);

    // Stochastic rotation & jitter
    float noise = interleaved_gradient_noise(gl_FragCoord.xy);
    float angle = noise * 6.283185307;
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    mat2 rot = mat2(cos_a, -sin_a, sin_a, cos_a);

    vec3 acc = col;
    float total_weight = 1.0;

    if (has_dof && has_mb) {
        for (int i = 0; i < 16; ++i) {
            float t = (float(i) + noise - 7.5) / 16.0;
            vec2 dof_offset = (rot * VOGEL_DISK[i]) * aspect_scale * coc;
            acc += sample_source(p + vel * t + dof_offset);
            total_weight += 1.0;
        }
    } else if (has_dof) {
        for (int i = 0; i < 16; ++i) {
            vec2 offset = (rot * VOGEL_DISK[i]) * aspect_scale * coc;
            acc += sample_source(p + offset);
            total_weight += 1.0;
        }
    } else if (has_mb) {
        for (int i = 0; i < 16; ++i) {
            float t = (float(i) + noise - 7.5) / 16.0;
            acc += sample_source(p + vel * t);
            total_weight += 1.0;
        }
    }

    return acc / total_weight;
}

vec3 sample_with_chromatic_aberration(vec2 p){
    float ca_strength = post.lens.w;
    if (ca_strength <= 0.0001) {
        return sample_filtered_source(p);
    }
    vec2 center = (post.viewport.xy + post.viewport.zw) * 0.5;
    vec2 diff = p - center;
    float dist = length(diff);
    vec2 dir = dist > 1e-5 ? (diff / dist) : vec2(0.0);
    vec2 chr = dir * (dist * dist) * ca_strength;

    vec3 col;
    col.r = sample_filtered_source(p + chr).r;
    col.g = sample_filtered_source(p).g;
    col.b = sample_filtered_source(p - chr).b;
    return col;
}

vec3 aces_tonemap(vec3 x){
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

const mat3 AGX_INSET = mat3(
    0.842479062253094, 0.0423282422610123, 0.0423756549057051,
    0.0784335999999992, 0.878468636469772, 0.0784336,
    0.0792237455858004, 0.0791661274605434, 0.879142973798484
);
const mat3 AGX_OUT = mat3(
    1.19687900512017, -0.0528968517590771, -0.0529716355084724,
    -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
    -0.0990297470705267, -0.0989611768448433, 1.15107367262634
);
vec3 agx_tonemap(vec3 color){
    color = max(color, vec3(1e-6));
    vec3 val = AGX_INSET * color;
    const float min_ev = -10.0;
    const float max_ev = 6.5;
    val = clamp((log2(val) - min_ev) / (max_ev - min_ev), 0.0, 1.0);
    val = val * val * val * (val * (val * 6.0 - 15.0) + 10.0);
    val = AGX_OUT * val;
    return clamp(val, 0.0, 1.0);
}

vec3 apply_tonemap(vec3 color, float tm_mode){
    if (tm_mode >= 2.5) return agx_tonemap(color);
    if (tm_mode >= 1.5) return aces_tonemap(color);
    if (tm_mode >= 0.5) return color / (vec3(1.0) + color);
    return color;
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
        vec3 color=max(sample_with_chromatic_aberration(uv),vec3(0));
        if(post.settings.z>0&&all(greaterThanEqual(uv,post.viewport.xy))&&all(lessThan(uv,post.viewport.zw)))
            color+=texture(bloom_image,uv).rgb*post.settings.z;
        color*=post.settings.x;

        // Color grading
        bool has_grading = abs(post.grading.x - 1.0) > 0.001 ||
                           abs(post.grading.y - 1.0) > 0.001 ||
                           abs(post.grading.z - 1.0) > 0.001 ||
                           abs(post.grading.w - 1.0) > 0.001 ||
                           abs(post.lens.x - 1.0) > 0.001;
        if (has_grading) {
            float luma=dot(color,vec3(0.2126,0.7152,0.0722));
            color=max(mix(vec3(luma),color,post.grading.x),vec3(0));
            color=max((color-vec3(0.5))*post.grading.y+vec3(0.5),vec3(0));
            color*=max(vec3(post.grading.z,post.grading.w,post.lens.x),vec3(0));
        }

        // Tonemapping (Off, Reinhard, ACES, AgX)
        if (post.settings.y > 0.5) {
            color=apply_tonemap(color,post.settings.y);
        }

        // Vignette
        if(post.lens.y>0.0001){
            vec2 center=(post.viewport.xy+post.viewport.zw)*0.5;
            vec2 norm_coord=(uv-center)/max((post.viewport.zw-post.viewport.xy)*0.5,vec2(0.001));
            float d=length(norm_coord);
            float v=smoothstep(1.0,1.0-post.lens.z,d*post.lens.y*1.5);
            color*=clamp(v,0.0,1.0);
        }

        // Film grain
        if(post.dof_grain.w>0.0001){
            float noise=fract(sin(dot(uv*1000.0+fract(post.temporal.z*43.17),vec2(12.9898,78.233)))*43758.5453);
            float lum=dot(color,vec3(0.2126,0.7152,0.0722));
            float grain=(noise-0.5)*post.dof_grain.w*(1.0-clamp(lum,0.0,1.0)*0.6);
            color=max(color+vec3(grain),vec3(0));
        }

        out_color=vec4(mode==5?sqrt(max(color,vec3(0))):color,1);
        return;
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

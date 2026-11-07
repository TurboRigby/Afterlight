// SPDX-License-Identifier: MPL-2.0
// Layered, warped travelling waves form a continuous world-space normal texture.
// High frequencies fade with pixel footprint instead of shimmering at a distance.
vec3 water_ripple_normal(vec3 normal){
    float speed=length(frame.wind_time.xy);
    float amount=draw.surface.w*clamp(speed*.12+draw.params.w*20,0.0,1.0);
    if(amount<=0||normal.y<.4)return normal;
    vec2 direction=speed>.001?frame.wind_time.xy/speed:normalize(vec2(.8,.6));
    vec2 across=vec2(-direction.y,direction.x);
    vec2 p=vec2(dot(world_position.xz,direction),dot(world_position.xz,across))/draw.surface.y;
    vec2 gradient=vec2(0);
    float time=frame.wind_time.z*(.7+speed*.09);
    float frequency=6.0,amplitude=1.0;
    for(int i=0;i<3;++i){
        vec2 k=normalize(vec2(1.0,float(i-1)*.43));
        float warp=p.y*frequency*.47+float(i);
        float phase=dot(p,k)*frequency+.75*sin(warp)-time*(1.0+float(i)*.61);
        float visible=1.0-smoothstep(1.5,3.14,fwidth(phase));
        gradient+=(k+vec2(0,.3525*cos(warp)))*cos(phase)*visible*amplitude;
        frequency*=2.13;amplitude*=.48;
    }
    vec2 slope=(direction*gradient.x+across*gradient.y)*amount*.4;
    return normalize(normal-vec3(slope.x,0,slope.y));
}

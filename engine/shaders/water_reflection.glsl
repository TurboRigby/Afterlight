/* SPDX-License-Identifier: MPL-2.0 */
struct ReflectionSurface {
    vec4 color;
    vec4 emission_unlit;
    uvec4 addresses;
};
layout(std430,set=0,binding=11) readonly buffer ReflectionSurfaces {
    ReflectionSurface surfaces[];
} reflection;
layout(buffer_reference,std430,buffer_reference_align=4) readonly buffer MeshWords {
    uint words[];
};
vec3 vertex_attribute(MeshWords vertices,uint vertex,uint offset){
    uint start=vertex*18u+offset;
    return uintBitsToFloat(uvec3(vertices.words[start],vertices.words[start+1],vertices.words[start+2]));
}
vec3 shade_reflection_hit(rayQueryEXT query,vec3 direction){
    ReflectionSurface surface=reflection.surfaces[rayQueryGetIntersectionInstanceCustomIndexEXT(query,true)];
    MeshWords vertices=MeshWords(surface.addresses.xy);
    MeshWords indices=MeshWords(surface.addresses.zw);
    uint triangle=3u*uint(rayQueryGetIntersectionPrimitiveIndexEXT(query,true));
    uvec3 index=uvec3(indices.words[triangle],indices.words[triangle+1],indices.words[triangle+2]);
    vec2 uv_hit=rayQueryGetIntersectionBarycentricsEXT(query,true);
    vec3 weights=vec3(1.0-uv_hit.x-uv_hit.y,uv_hit);
    vec3 base=surface.color.rgb*(vertex_attribute(vertices,index.x,3)*weights.x+
                                vertex_attribute(vertices,index.y,3)*weights.y+
                                vertex_attribute(vertices,index.z,3)*weights.z);
    if(surface.emission_unlit.w>.5)return base+surface.emission_unlit.rgb;

    vec3 normal=vertex_attribute(vertices,index.x,7)*weights.x+
                vertex_attribute(vertices,index.y,7)*weights.y+
                vertex_attribute(vertices,index.z,7)*weights.z;
    if(dot(normal,normal)<1e-12){
        vec3 a=vertex_attribute(vertices,index.x,0);
        normal=cross(vertex_attribute(vertices,index.y,0)-a,vertex_attribute(vertices,index.z,0)-a);
    }
    normal=transpose(mat3(rayQueryGetIntersectionWorldToObjectEXT(query,true)))*normal;
    normal=dot(normal,normal)>1e-12?normalize(normal):-direction;
    if(dot(normal,direction)>0.0)normal=-normal;
    vec3 sun=frame.color_ambient.rgb*frame.direction_intensity.w*
             max(dot(normal,-frame.direction_intensity.xyz),0.0);
    vec3 sky=textureLod(irradiance_map,environment_uv(normal),0).rgb*frame.render_params.z;
    return base*(frame.color_ambient.w+sun+sky)+surface.emission_unlit.rgb;
}

bool trace_water_reflection(vec3 n,vec3 direction,float roughness,out vec3 radiance){
    // Trace the upper surface of pools and rivers. Droplets retain environment lighting.
    if(frame.sky_params.w<.5 || (uint(draw.params.x)&(1u<<18u))==0u ||
       n.y<=.15 || direction.y<=0.0 || roughness>=.6)return false;
    rayQueryEXT query;
    rayQueryInitializeEXT(query,scene_acceleration,gl_RayFlagsOpaqueEXT,
                          2,world_position+vec3(0,.003,0),.002,direction,150.0);
    while(rayQueryProceedEXT(query)){}
    if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT)return false;
    radiance=shade_reflection_hit(query,direction);
    return true;
}

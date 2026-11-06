/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Animation %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(int argc,char **argv){
    if(argc==3&&!strcmp(argv[1],"--reject")){af_animation_file bad;af_asset_error error;CHECK(!af_animation_file_load(argv[2],&bad,&error)&&!bad.clips&&!bad.bind_pose);return 0;}
    CHECK(argc==2);char path[4096];snprintf(path,sizeof(path),"%s/animated.gltf",argv[1]);af_animation_file file;af_asset_error error;
    CHECK(af_animation_file_load(path,&file,&error)&&file.clip_count==3&&file.node_count==2&&file.parents[0]==1);
    af_animation_pose poses[2];af_mat4 world[2];CHECK(af_animation_sample(&file,0,.5,poses,2));CHECK(fabsf(poses[0].translation.x-1)<.0001f&&fabsf(poses[0].scale.x-1.5f)<.0001f);
    af_vec4 p=af_mat4_transform(af_animation_pose_matrix(poses[0]),(af_vec4){0,1,0,1});CHECK(fabsf(p.x-(1-sqrtf(.5f)))<.0001f&&fabsf(p.y-sqrtf(.5f))<.0001f);
    CHECK(af_animation_world_matrices(&file,poses,world,2));CHECK(fabsf(world[0].elements[13]-1)<.0001f);
    CHECK(af_animation_sample(&file,1,.99,poses,2)&&poses[0].translation.x==0);CHECK(af_animation_sample(&file,1,1,poses,2)&&poses[0].translation.x==2);
    CHECK(af_animation_sample(&file,2,.5,poses,2)&&fabsf(poses[0].translation.x-1.25f)<.0001f);
    CHECK(af_animation_sample(&file,0,-1,poses,2)&&poses[0].translation.x==0);CHECK(af_animation_sample(&file,0,100,poses,2)&&poses[0].translation.x==0);
    CHECK(!af_animation_sample(&file,3,0,poses,2)&&!af_animation_sample(&file,0,NAN,poses,2)&&!af_animation_sample(&file,0,0,poses,1));
    af_vec4 q=af_quaternion_slerp((af_vec4){0,0,0,1},(af_vec4){0,0,0,-1},.5f);CHECK(fabsf(q.w-1)<.0001f);
    af_transform transform=af_transform_identity();transform.rotation=(af_vec3){.3f,-.7f,1.2f};transform.scale=(af_vec3){1,-2,3};af_mat4 a=af_transform_matrix(transform),b=af_animation_pose_matrix((af_animation_pose){.translation=transform.position,.scale=transform.scale,.rotation=af_quaternion_from_euler(transform.rotation)});for(unsigned i=0;i<16;++i)CHECK(fabsf(a.elements[i]-b.elements[i])<.0001f);
    af_animation_file_free(&file);CHECK(!file.clips&&!file.bind_pose);
    snprintf(path,sizeof(path),"%s/animated-invalid.gltf",argv[1]);CHECK(!af_animation_file_load(path,&file,&error)&&!file.clips);
    puts("Animation: glTF TRS, source-node hierarchy, quaternion SLERP, STEP/CUBIC, time clamping, duplicate rejection and cleanup passed.");return 0;
}

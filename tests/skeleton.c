/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include <afterlight/model.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Skeleton %d: %s\n",__LINE__,#c);return 1;}}while(0)
int main(int argc,char **argv){
    if(argc==3){af_asset_error error;if(!strcmp(argv[1],"--reject-model")){af_model_file m;CHECK(!af_model_file_load(argv[2],&m,&error)&&!m.nodes&&!m.primitives);}else{af_animation_file a;CHECK(!af_animation_file_load(argv[2],&a,&error)&&!a.clips&&!a.skins);}return 0;}
    CHECK(argc==2);af_animation_file file;af_model_file model;af_asset_error error;CHECK(af_animation_file_load(argv[1],&file,&error)&&file.skin_count==1&&file.skins[0].joint_count==2);
    CHECK(af_model_file_load(argv[1],&model,&error)&&model.primitives[0].mesh.data.vertices[2].weights.x==1);
    af_animation_pose poses[4];af_mat4 world[4],palette[64];CHECK(af_animation_sample(&file,0,0,poses,4)&&af_animation_world_matrices(&file,poses,world,4)&&af_animation_skin_palette(&file,0,0,world,palette,64));
    af_mat4 identity=af_mat4_identity();for(unsigned j=0;j<2;++j)for(unsigned i=0;i<16;++i)CHECK(fabsf(palette[j].elements[i]-identity.elements[i])<.0001f);
    CHECK(af_animation_sample(&file,0,1,poses,4)&&af_animation_world_matrices(&file,poses,world,4)&&af_animation_skin_palette(&file,0,0,world,palette,64));
    af_vec4 p=af_mat4_transform(palette[1],(af_vec4){0,2,0,1});CHECK(fabsf(p.x+sqrtf(.75f))<.0001f&&fabsf(p.y-1.5f)<.0001f);
    world[0]=af_mat4_scale((af_vec3){0,1,1});CHECK(!af_animation_skin_palette(&file,0,0,world,palette,64));CHECK(!af_animation_skin_palette(&file,1,0,world,palette,64));
    af_model_file_free(&model);af_animation_file_free(&file);puts("Skeleton: joint hierarchy, inverse bind, identity rest palette, animated pivot and singular/invalid rejection passed.");return 0;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/asset.h>
#include <afterlight/model.h>
#include <afterlight/animation.h>
#include "cgltf.h"
#include <ctype.h>
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VERTEX_LIMIT 1048576u
#define MEMORY_LIMIT (128u*1024u*1024u)
#define MODEL_MESH_LIMIT 1024u
#define SKIN_SET_LIMIT 8u
typedef union allocation {max_align_t alignment;size_t size;} allocation;
typedef struct memory_budget {size_t bytes;} memory_budget;
static void *allocate(void *user,cgltf_size size){memory_budget *budget=user;if(size>MEMORY_LIMIT||budget->bytes>MEMORY_LIMIT-size)return NULL;allocation *p=malloc(sizeof(*p)+size);if(!p)return NULL;p->size=size;budget->bytes+=size;return p+1;}
static void release(void *user,void *pointer){if(pointer){allocation *p=(allocation*)pointer-1;((memory_budget*)user)->bytes-=p->size;free(p);}}
static cgltf_result read_file(const cgltf_memory_options *memory,const cgltf_file_options *file_options,const char *path,cgltf_size *size,void **data){
    (void)file_options;FILE *file=fopen(path,"rb");if(!file)return cgltf_result_file_not_found;long length=0;void *bytes=NULL;cgltf_result result=cgltf_result_io_error;
    if(!fseek(file,0,SEEK_END)&&(length=ftell(file))>=0&&(unsigned long)length<=AF_ASSET_FILE_MAX_BYTES&&!fseek(file,0,SEEK_SET)){
        size_t count=*size?*size:(size_t)length;if(count<=(size_t)length){bytes=memory->alloc_func(memory->user_data,count);if(!bytes)result=cgltf_result_out_of_memory;
            else if(fread(bytes,1,count,file)==count){*size=count;*data=bytes;result=cgltf_result_success;}}}
    if(fclose(file))result=cgltf_result_io_error;if(result!=cgltf_result_success&&bytes)memory->free_func(memory->user_data,bytes);return result;
}
static void release_file(const cgltf_memory_options *memory,const cgltf_file_options *options,void *data,cgltf_size size){(void)options;(void)size;memory->free_func(memory->user_data,data);}
typedef struct builder {af_vertex *vertices;size_t count,capacity,visits;const char *error;} builder;
static bool reject(builder *b,const char *error){b->error=error;return false;}
static bool reserve(builder *b,size_t extra){if(extra>VERTEX_LIMIT||b->count>VERTEX_LIMIT-extra)return reject(b,"glTF exceeds one million triangle vertices.");
    size_t needed=b->count+extra;if(needed<=b->capacity)return true;size_t capacity=b->capacity?b->capacity*2:1024;if(capacity<needed)capacity=needed;if(capacity>VERTEX_LIMIT)capacity=VERTEX_LIMIT;
    af_vertex *vertices=realloc(b->vertices,capacity*sizeof(*vertices));if(!vertices)return reject(b,"Allocating glTF mesh failed.");b->vertices=vertices;b->capacity=capacity;return true;
}
static const cgltf_accessor *attribute(const cgltf_primitive *primitive,cgltf_attribute_type type){for(size_t i=0;i<primitive->attributes_count;++i)if(primitive->attributes[i].type==type&&primitive->attributes[i].index==0)return primitive->attributes[i].data;return NULL;}
static bool values(const cgltf_accessor *accessor,size_t index,float *out,size_t count){if(index>=accessor->count||!cgltf_accessor_read_float(accessor,index,out,count))return false;for(size_t i=0;i<count;++i)if(!isfinite(out[i]))return false;return true;}
typedef struct skin_set {
    const cgltf_accessor *joints, *weights;
} skin_set;

static bool unsigned_skin_component(cgltf_component_type type)
{
    return type == cgltf_component_type_r_8u || type == cgltf_component_type_r_16u;
}

static bool collect_skin_sets(const cgltf_primitive *primitive, skin_set sets[SKIN_SET_LIMIT],
                              size_t *count, size_t vertices)
{
    for (size_t i = 0; i < primitive->attributes_count; ++i) {
        const cgltf_attribute *attribute = &primitive->attributes[i];
        if (attribute->type != cgltf_attribute_type_joints &&
            attribute->type != cgltf_attribute_type_weights) continue;
        size_t index = (size_t)attribute->index;
        if (index >= SKIN_SET_LIMIT) return false;
        const cgltf_accessor **slot = attribute->type == cgltf_attribute_type_joints ?
                                      &sets[index].joints : &sets[index].weights;
        if (*slot) return false;
        *slot = attribute->data;
        if (*count <= index) *count = index + 1;
    }
    for (size_t i = 0; i < *count; ++i) {
        const cgltf_accessor *joints = sets[i].joints, *weights = sets[i].weights;
        if (!joints || !weights || joints->type != cgltf_type_vec4 || weights->type != cgltf_type_vec4 ||
            joints->count != vertices || weights->count != vertices ||
            joints->normalized || !unsigned_skin_component(joints->component_type) ||
            (weights->component_type != cgltf_component_type_r_32f &&
             (!weights->normalized || !unsigned_skin_component(weights->component_type)))) return false;
    }
    return true;
}

static bool skin_vertex(af_vertex *vertex, const skin_set *sets, size_t count, size_t index,
                        size_t joint_limit)
{
    float strongest[4] = {0};
    for (size_t set = 0; set < count; ++set) {
        cgltf_uint joints[4];
        float weights[4];
        if (!cgltf_accessor_read_uint(sets[set].joints, index, joints, 4) ||
            !values(sets[set].weights, index, weights, 4)) return false;
        for (unsigned i = 0; i < 4; ++i) {
            if (joints[i] >= joint_limit || weights[i] < 0) return false;
            if (count == 1) {
                vertex->joints[i] = (uint16_t)joints[i];
                strongest[i] = weights[i];
                continue;
            }
            for (unsigned slot = 0; slot < 4; ++slot) {
                if (weights[i] <= strongest[slot]) continue;
                for (unsigned j = 3; j > slot; --j) {
                    vertex->joints[j] = vertex->joints[j - 1];
                    strongest[j] = strongest[j - 1];
                }
                vertex->joints[slot] = (uint16_t)joints[i];
                strongest[slot] = weights[i];
                break;
            }
        }
    }
    float sum = strongest[0] + strongest[1] + strongest[2] + strongest[3];
    if (!isfinite(sum) || sum < 1e-8f) return false;
    vertex->weights = (af_vec4){strongest[0] / sum, strongest[1] / sum,
                                strongest[2] / sum, strongest[3] / sum};
    return true;
}

static bool primitive(builder *b,const cgltf_primitive *p,af_mat4 world,size_t joint_limit){
    if(p->type!=cgltf_primitive_type_triangles||p->targets_count||p->has_draco_mesh_compression)return reject(b,"glTF import requires triangle meshes without morph targets or Draco compression.");
    const cgltf_accessor *position=attribute(p,cgltf_attribute_type_position),*normal=attribute(p,cgltf_attribute_type_normal),*uv=attribute(p,cgltf_attribute_type_texcoord),*color=attribute(p,cgltf_attribute_type_color);
    if(!position||position->type!=cgltf_type_vec3||(normal&&normal->type!=cgltf_type_vec3)||(uv&&uv->type!=cgltf_type_vec2)||(color&&color->type!=cgltf_type_vec3&&color->type!=cgltf_type_vec4))return reject(b,"Invalid glTF position/normal/UV/color accessor.");
    skin_set sets[SKIN_SET_LIMIT]={0};size_t skin_sets=0;
    if(!collect_skin_sets(p,sets,&skin_sets,position->count))
        return reject(b,"glTF skinning requires up to eight consecutive, matching JOINTS and WEIGHTS sets.");
    size_t count=p->indices?p->indices->count:position->count;if(!count||count%3||!reserve(b,count))return reject(b,"Invalid or oversized glTF triangle indices.");
    af_mat4 inverse;if(!af_mat4_inverse(world,&inverse))return reject(b,"glTF contains a singular node transform.");
    float *m=world.elements;float determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[4]*(m[1]*m[10]-m[2]*m[9])+m[8]*(m[1]*m[6]-m[2]*m[5]);
    for(size_t triangle=0;triangle<count;triangle+=3){af_vertex vertices[3];
        for(unsigned corner=0;corner<3;++corner){size_t element=triangle+(determinant<0&&corner?3-corner:corner);size_t index=p->indices?cgltf_accessor_read_index(p->indices,element):element;
            af_vertex *v=&vertices[corner];*v=(af_vertex){.color={1,1,1,1}};float data[4]={0};if(!values(position,index,data,3))return reject(b,"glTF contains invalid/out-of-range positions.");
            af_vec4 transformed=af_mat4_transform(world,(af_vec4){data[0],data[1],data[2],1});v->position=(af_vec3){transformed.x,transformed.y,transformed.z};
            if(!isfinite(transformed.x)||!isfinite(transformed.y)||!isfinite(transformed.z))return reject(b,"glTF transform overflows.");
            if(normal){if(!values(normal,index,data,3))return reject(b,"Invalid glTF normal.");float *n=inverse.elements;v->normal=af_vec3_normalize((af_vec3){n[0]*data[0]+n[1]*data[1]+n[2]*data[2],n[4]*data[0]+n[5]*data[1]+n[6]*data[2],n[8]*data[0]+n[9]*data[1]+n[10]*data[2]});}
            if(uv){if(!values(uv,index,data,2))return reject(b,"Invalid glTF UV.");v->uv=(af_vec2){data[0],data[1]};}
            if(color){size_t channels=color->type==cgltf_type_vec4?4:3;data[3]=1;if(!values(color,index,data,channels))return reject(b,"Invalid glTF vertex color.");v->color=(af_color){data[0],data[1],data[2],data[3]};}
            if(skin_sets&&!skin_vertex(v,sets,skin_sets,index,joint_limit))
                return reject(b,"glTF skin contains an out-of-range joint or invalid weights.");
            if(p->material&&p->material->has_pbr_metallic_roughness){const float *factor=p->material->pbr_metallic_roughness.base_color_factor;v->color.r*=factor[0];v->color.g*=factor[1];v->color.b*=factor[2];v->color.a*=factor[3];}
            float rgba[]={v->color.r,v->color.g,v->color.b,v->color.a};for(unsigned i=0;i<4;++i)if(!isfinite(rgba[i])||rgba[i]<0||rgba[i]>1)return reject(b,"glTF color factor outside [0,1].");
        }
        af_vec3 face=af_vec3_normalize(af_vec3_cross(af_vec3_sub(vertices[1].position,vertices[0].position),af_vec3_sub(vertices[2].position,vertices[0].position)));
        for(unsigned i=0;i<3;++i){if(af_vec3_length(vertices[i].normal)<.5f)vertices[i].normal=face;b->vertices[b->count++]=vertices[i];}
    }return true;
}
static bool node(builder *b,const cgltf_node *n,af_mat4 parent,unsigned depth){
    if(depth>128||++b->visits>65536)return reject(b,"glTF node hierarchy too deep or large.");
    if(n->skin)return reject(b,"Import this rigged file as a model to preserve its skeleton.");
    if(n->has_mesh_gpu_instancing)return reject(b,"glTF GPU instancing requires an uninstanced export.");
    af_mat4 local;cgltf_node_transform_local(n,local.elements);af_mat4 world=af_mat4_mul(parent,local);
    if(n->mesh)for(size_t i=0;i<n->mesh->primitives_count;++i)if(!primitive(b,&n->mesh->primitives[i],world,AF_ANIMATION_MAX_JOINTS))return false;
    for(size_t i=0;i<n->children_count;++i)if(!node(b,n->children[i],world,depth+1))return false;return true;
}
bool af_mesh_file_load_gltf(const char *path,af_mesh_file *out,af_asset_error *error){
    if(error)*error=(af_asset_error){0};if(out)*out=(af_mesh_file){0};if(!out||!path){if(error)snprintf(error->message,sizeof(error->message),"glTF path and output required.");return false;}
    memory_budget budget={0};cgltf_options options={.memory={allocate,release,&budget},.file={read_file,release_file,NULL}};cgltf_data *data=NULL;builder b={0};bool ok=false;
    if(cgltf_parse_file(&options,path,&data)!=cgltf_result_success){b.error="Invalid/oversized glTF 2.0 or GLB file.";goto done;}
    size_t bytes=0;for(size_t i=0;i<data->buffers_count;++i){if(data->buffers[i].size>AF_ASSET_FILE_MAX_BYTES-bytes){b.error="glTF buffers exceed 64 MiB.";goto done;}bytes+=data->buffers[i].size;}
    for(size_t i=0;i<data->buffer_views_count;++i)if(data->buffer_views[i].has_meshopt_compression){b.error="Meshopt-compressed glTF needs an uncompressed export.";goto done;}
    for(size_t i=0;i<data->extensions_required_count;++i)if(strcmp(data->extensions_required[i],"KHR_mesh_quantization")){b.error="Unsupported required glTF extension.";goto done;}
    if(cgltf_load_buffers(&options,data,path)!=cgltf_result_success||cgltf_validate(data)!=cgltf_result_success){b.error="Invalid/missing glTF buffers, indices or hierarchy.";goto done;}
    const cgltf_scene *scene=data->scene?data->scene:(data->scenes_count?&data->scenes[0]:NULL);
    if(scene){for(size_t i=0;i<scene->nodes_count;++i)if(!node(&b,scene->nodes[i],af_mat4_identity(),0))goto done;}
    else for(size_t i=0;i<data->nodes_count;++i)if(!data->nodes[i].parent&&!node(&b,&data->nodes[i],af_mat4_identity(),0))goto done;
    if(!b.count){b.error="glTF scene has no triangle geometry.";goto done;}
    uint32_t *indices=malloc(b.count*sizeof(*indices));if(!indices){b.error="Allocating glTF indices failed.";goto done;}
    for(size_t i=0;i<b.count;++i)indices[i]=(uint32_t)i;
    out->data=(af_mesh_data){b.vertices,(uint32_t)b.count,indices,(uint32_t)b.count};b.vertices=NULL;ok=true;
done:
    free(b.vertices);cgltf_free(data);if(!ok&&error)snprintf(error->message,sizeof(error->message),"%s",b.error?b.error:"glTF import failed.");return ok;
}
bool af_asset_is_model_path(const char *path){
    if(!path)return false;size_t size=strcspn(path,"#");const char *extension=NULL;for(size_t i=0;i<size;++i)if(path[i]=='.')extension=path+i;
    if(!extension||size-(size_t)(extension-path)>5)return false;char lower[6]={0};for(size_t i=0;i<size-(size_t)(extension-path);++i)lower[i]=(char)tolower((unsigned char)extension[i]);return !strcmp(lower,".gltf")||!strcmp(lower,".glb");
}
bool af_mesh_file_load(const char *path,af_mesh_file *out,af_asset_error *error){
    if(path&&strstr(path,"#mesh="))return af_model_mesh_load(path,out,error);
    const char *extension=path?strrchr(path,'.'):NULL;
    char lower[6]={0};if(extension&&strlen(extension)<sizeof(lower))for(size_t i=0;extension[i];++i)lower[i]=(char)tolower((unsigned char)extension[i]);
    return !strcmp(lower,".gltf")||!strcmp(lower,".glb")?af_mesh_file_load_gltf(path,out,error):af_mesh_file_load_obj(path,out,error);
}

/* Hierarchical model import shares the bounded parser and geometry reader above. */
#include <afterlight/model.h>
#include "scene_internal.h"
#include "stb_image.h"
#include <limits.h>
static bool error_message(af_asset_error *error,const char *text){if(error)snprintf(error->message,sizeof(error->message),"%s",text);return false;}
static bool parse_model(const char *path,memory_budget *budget,cgltf_data **out){
    cgltf_options options={.memory={allocate,release,budget},.file={read_file,release_file,NULL}};
    if(cgltf_parse_file(&options,path,out)!=cgltf_result_success)return false;size_t bytes=0;
    for(size_t i=0;i<(*out)->buffers_count;++i){size_t size=(*out)->buffers[i].size;if(size>AF_ASSET_FILE_MAX_BYTES-bytes)return false;bytes+=size;}
    for(size_t i=0;i<(*out)->extensions_required_count;++i)if(strcmp((*out)->extensions_required[i],"KHR_mesh_quantization")&&strcmp((*out)->extensions_required[i],"KHR_materials_unlit"))return false;
    for(size_t i=0;i<(*out)->buffer_views_count;++i)if((*out)->buffer_views[i].has_meshopt_compression)return false;
    return cgltf_load_buffers(&options,*out,path)==cgltf_result_success&&cgltf_validate(*out)==cgltf_result_success;
}
static bool dependency(af_model_file *model,const char *path){
    for(size_t i=0;i<model->dependency_count;++i)if(!strcmp(model->dependencies[i],path))return true;
    if(model->dependency_count>=1024||strlen(path)>=AF_ASSET_PATH_CAPACITY)return false;
    void *p=realloc(model->dependencies,(model->dependency_count+1)*sizeof(*model->dependencies));if(!p)return false;model->dependencies=p;
    snprintf(model->dependencies[model->dependency_count++],AF_ASSET_PATH_CAPACITY,"%s",path);return true;
}
static bool decode_image(const cgltf_image *image,const char *path,const cgltf_options *options,af_model_file *model,af_image_file *out){
    const unsigned char *bytes=NULL;size_t count=0;void *owned=NULL;
    if(image->buffer_view){count=image->buffer_view->size;bytes=cgltf_buffer_view_data(image->buffer_view);}
    else if(image->uri&&!strncmp(image->uri,"data:",5)){const char *comma=strchr(image->uri,',');if(!comma||comma-image->uri<7||strncmp(comma-7,";base64",7))return false;
        size_t length=strlen(comma+1);if(!length||length%4)return false;count=length/4*3;if(comma[length]=='=')--count;if(length>1&&comma[length-1]=='=')--count;
        if(count>AF_ASSET_FILE_MAX_BYTES||cgltf_load_buffer_base64(options,count,comma+1,&owned)!=cgltf_result_success)return false;bytes=owned;
    }else if(image->uri){char reference[AF_ASSET_PATH_CAPACITY],resolved[AF_ASSET_PATH_CAPACITY];if(strlen(image->uri)>=sizeof(reference)||strstr(image->uri,"://"))return false;snprintf(reference,sizeof(reference),"%s",image->uri);cgltf_decode_uri(reference);
        if(!af_asset_resolve_path(path,reference,resolved,sizeof(resolved))||!dependency(model,resolved))return false;cgltf_size length=0;if(read_file(&options->memory,&options->file,resolved,&length,&owned)!=cgltf_result_success)return false;count=length;bytes=owned;}
    int width=0,height=0,channels=0;unsigned char *rgba=NULL;
    if(bytes&&count<=INT_MAX&&stbi_info_from_memory(bytes,(int)count,&width,&height,&channels)&&width>0&&height>0&&width<=8192&&height<=8192)rgba=stbi_load_from_memory(bytes,(int)count,&width,&height,&channels,4);
    if(owned)options->memory.free_func(options->memory.user_data,owned);if(!rgba)return false;*out=(af_image_file){rgba,(uint32_t)width,(uint32_t)height};return true;
}
static int image_index(const cgltf_data *data,const cgltf_texture_view *view){
    if(!view->texture)return -1;if(view->texcoord!=0||view->has_transform||!view->texture->image||view->texture->has_basisu||view->texture->has_webp)return -2;
    /* The engine's current material sampler uses trilinear repeat. Reject
       incompatible wrapping instead of silently rendering a different model. */
    const cgltf_sampler *sampler=view->texture->sampler;
    if(sampler&&((sampler->wrap_s&&sampler->wrap_s!=cgltf_wrap_mode_repeat)||(sampler->wrap_t&&sampler->wrap_t!=cgltf_wrap_mode_repeat)))return -2;
    return (int)(view->texture->image-data->images);
}
static bool local_transform(const cgltf_node *node,af_transform *out){
    af_mat4 matrix;cgltf_node_transform_local(node,matrix.elements);float *m=matrix.elements;af_vec3 columns[]={{m[0],m[1],m[2]},{m[4],m[5],m[6]},{m[8],m[9],m[10]}};
    af_transform t=af_transform_identity();t.position=(af_vec3){m[12],m[13],m[14]};t.scale=(af_vec3){af_vec3_length(columns[0]),af_vec3_length(columns[1]),af_vec3_length(columns[2])};
    if(t.scale.x<1e-7f||t.scale.y<1e-7f||t.scale.z<1e-7f)return false;
    if(af_vec3_dot(af_vec3_cross(columns[0],columns[1]),columns[2])<0)t.scale.x=-t.scale.x;
    columns[0]=af_vec3_scale(columns[0],1/t.scale.x);columns[1]=af_vec3_scale(columns[1],1/t.scale.y);columns[2]=af_vec3_scale(columns[2],1/t.scale.z);
    if(fabsf(af_vec3_dot(columns[0],columns[1]))>1e-4f||fabsf(af_vec3_dot(columns[0],columns[2]))>1e-4f||fabsf(af_vec3_dot(columns[1],columns[2]))>1e-4f)return false;
    t.rotation.y=asinf(fmaxf(-1,fminf(1,-columns[0].z)));
    if(fabsf(cosf(t.rotation.y))>1e-5f){t.rotation.x=atan2f(columns[1].z,columns[2].z);t.rotation.z=atan2f(columns[0].y,columns[0].x);}
    else{t.rotation.x=atan2f(-columns[2].y,columns[1].y);t.rotation.z=0;}
    af_mat4 roundtrip=af_transform_matrix(t);for(unsigned i=0;i<16;++i)if(!isfinite(m[i])||fabsf(m[i]-roundtrip.elements[i])>1e-3f*fmaxf(1,fabsf(m[i])))return false;*out=t;return true;
}
static bool add_node(af_model_file *model,const cgltf_node *source,const cgltf_data *data,int parent,unsigned depth){
    if(depth>=AF_SCENE_MAX_DEPTH-2||model->node_count>=16384||source->has_mesh_gpu_instancing)return false;
    size_t index=model->node_count;af_model_node value={.parent=parent,.mesh=source->mesh?(int)(source->mesh-data->meshes):-1,.skin=source->skin?(int)(source->skin-data->skins):-1,.source_node=(uint32_t)(source-data->nodes)};
    if(!local_transform(source,&value.transform))return false;snprintf(value.name,sizeof(value.name),"%.63s",source->name&&source->name[0]?source->name:"Node");
    void *p=realloc(model->nodes,(index+1)*sizeof(*model->nodes));if(!p)return false;model->nodes=p;model->nodes[model->node_count++]=value;
    for(size_t i=0;i<source->children_count;++i)if(!add_node(model,source->children[i],data,(int)index,depth+1))return false;return true;
}
void af_model_file_free(af_model_file *model){if(!model)return;for(size_t i=0;model->primitives&&i<model->primitive_count;++i)af_mesh_file_free(&model->primitives[i].mesh);for(size_t i=0;model->images&&i<model->image_count;++i)af_image_file_free(&model->images[i]);free(model->primitives);free(model->images);free(model->nodes);free(model->dependencies);*model=(af_model_file){0};}
bool af_model_file_load(const char *path,af_model_file *out,af_asset_error *error){
    if(error)*error=(af_asset_error){0};if(out)*out=(af_model_file){0};if(!path||!out)return error_message(error,"Model path/output required.");
    memory_budget budget={0};cgltf_data *data=NULL;af_model_file result={0};bool ok=false;const char *message="Invalid/unsupported glTF model or allocation limit exceeded.";
    cgltf_options options={.memory={allocate,release,&budget},.file={read_file,release_file,NULL}};
    if(!parse_model(path,&budget,&data))goto done;
    if(data->meshes_count>MODEL_MESH_LIMIT||data->nodes_count>AF_ANIMATION_MAX_NODES||data->images_count>128)goto done;
    size_t joint_limits[MODEL_MESH_LIMIT];
    for(size_t i=0;i<data->meshes_count;++i)joint_limits[i]=AF_ANIMATION_MAX_JOINTS;
    for(size_t i=0;i<data->nodes_count;++i){
        const cgltf_node *node=&data->nodes[i];
        if(!node->skin||!node->mesh)continue;
        if(!node->skin->joints_count||node->skin->joints_count>AF_ANIMATION_MAX_JOINTS){
            message="A glTF skin must have between 1 and 256 joints.";goto done;
        }
        size_t mesh=(size_t)(node->mesh-data->meshes);
        if(node->skin->joints_count<joint_limits[mesh])joint_limits[mesh]=node->skin->joints_count;
    }
    if(!dependency(&result,path))goto done;
    for(size_t i=0;i<data->buffers_count;++i)if(data->buffers[i].uri&&strncmp(data->buffers[i].uri,"data:",5)){char uri[AF_ASSET_PATH_CAPACITY],resolved[AF_ASSET_PATH_CAPACITY];if(strlen(data->buffers[i].uri)>=sizeof(uri))goto done;snprintf(uri,sizeof(uri),"%s",data->buffers[i].uri);cgltf_decode_uri(uri);if(!af_asset_resolve_path(path,uri,resolved,sizeof(resolved))||!dependency(&result,resolved))goto done;}
    result.image_count=data->images_count;result.images=calloc(result.image_count?result.image_count:1,sizeof(*result.images));if(!result.images)goto done;size_t image_bytes=0;
    for(size_t i=0;i<result.image_count;++i){if(!decode_image(&data->images[i],path,&options,&result,&result.images[i])){message="Invalid/missing glTF image (PNG/JPEG required).";goto done;}size_t bytes=(size_t)result.images[i].width*result.images[i].height*4;if(bytes>256u*1024u*1024u-image_bytes)goto done;image_bytes+=bytes;}
    size_t vertices=0;
    for(size_t mi=0;mi<data->meshes_count;++mi)for(size_t pi=0;pi<data->meshes[mi].primitives_count;++pi){
        if(result.primitive_count>=1024)goto done;cgltf_primitive source=data->meshes[mi].primitives[pi];cgltf_material *material=source.material;source.material=NULL;builder b={0};
        if(!primitive(&b,&source,af_mat4_identity(),joint_limits[mi])){message=b.error;free(b.vertices);goto done;}
        vertices+=b.count;if(vertices>VERTEX_LIMIT){free(b.vertices);goto done;}
        uint32_t *indices=malloc(b.count*sizeof(*indices));if(!indices){free(b.vertices);goto done;}for(size_t i=0;i<b.count;++i)indices[i]=(uint32_t)i;
        af_model_primitive value={.mesh={{b.vertices,(uint32_t)b.count,indices,(uint32_t)b.count}},.material=af_material_default(),.base_image=-1,.normal_image=-1,.surface_image=-1,.source_mesh=(unsigned)mi,.source_primitive=(unsigned)pi};
        value.material.physically_based=true;value.material.roughness=1;value.material.metallic=1;
        if(material){
            if(material->emissive_texture.texture||material->has_transmission||material->has_clearcoat){af_mesh_file_free(&value.mesh);message="Only core PBR/unlit glTF materials without emissive textures/extensions are supported.";goto done;}
            value.material.alpha_mode=material->alpha_mode==cgltf_alpha_mode_mask?AF_ALPHA_MASK:material->alpha_mode==cgltf_alpha_mode_blend?AF_ALPHA_BLEND:AF_ALPHA_OPAQUE;value.material.alpha_cutoff=material->alpha_cutoff;
            value.material.physically_based=true;value.material.unlit=material->unlit!=0;
            if(material->has_pbr_metallic_roughness){cgltf_pbr_metallic_roughness *p=&material->pbr_metallic_roughness;value.material.base_color=(af_color){p->base_color_factor[0],p->base_color_factor[1],p->base_color_factor[2],p->base_color_factor[3]};value.material.roughness=p->roughness_factor;value.material.metallic=p->metallic_factor;
                value.base_image=image_index(data,&p->base_color_texture);value.surface_image=image_index(data,&p->metallic_roughness_texture);}
            value.normal_image=image_index(data,&material->normal_texture);value.material.normal_strength=material->normal_texture.texture?material->normal_texture.scale:1;
            value.material.emission=(af_vec3){material->emissive_factor[0],material->emissive_factor[1],material->emissive_factor[2]};value.material.emission_strength=material->has_emissive_strength?material->emissive_strength.emissive_strength:1;
            if(value.base_image< -1||value.normal_image< -1||value.surface_image< -1){af_mesh_file_free(&value.mesh);message="glTF textures require UV0, repeat wrapping and no texture transform.";goto done;}
        }
        void *p=realloc(result.primitives,(result.primitive_count+1)*sizeof(*result.primitives));if(!p){af_mesh_file_free(&value.mesh);goto done;}result.primitives=p;result.primitives[result.primitive_count++]=value;
    }
    const cgltf_scene *scene=data->scene?data->scene:(data->scenes_count?&data->scenes[0]:NULL);
    if(scene){for(size_t i=0;i<scene->nodes_count;++i)if(!add_node(&result,scene->nodes[i],data,-1,0)){message="Invalid glTF transform or hierarchy. Shear and GPU instancing are unsupported.";goto done;}}
    else for(size_t i=0;i<data->nodes_count;++i)if(!data->nodes[i].parent&&!add_node(&result,&data->nodes[i],data,-1,0))goto done;
    bool imported_nodes[AF_ANIMATION_MAX_NODES]={0};
    for(size_t i=0;i<result.node_count;++i)imported_nodes[result.nodes[i].source_node]=true;
    for(size_t i=0;i<result.node_count;++i){
        const af_model_node *node=&result.nodes[i];
        if(node->skin<0)continue;
        const cgltf_skin *skin=&data->skins[node->skin];
        for(size_t j=0;j<skin->joints_count;++j)if(!imported_nodes[skin->joints[j]-data->nodes]){
            message="A glTF skin references a joint outside the imported scene.";goto done;
        }
        if(node->mesh<0)continue;
        const cgltf_mesh *mesh=&data->meshes[node->mesh];
        for(size_t j=0;j<mesh->primitives_count;++j)
            if(!attribute(&mesh->primitives[j],cgltf_attribute_type_joints)){
                message="A skinned glTF primitive is missing joint and weight attributes.";goto done;
            }
    }
    if(!result.node_count||!result.primitive_count)goto done;*out=result;result=(af_model_file){0};ok=true;
done:
    cgltf_free(data);af_model_file_free(&result);if(!ok)error_message(error,message);return ok;
}
static bool selector(const char *reference,char *path,size_t capacity,const char **fragment){const char *hash=strrchr(reference,'#');if(!hash||hash==reference||(size_t)(hash-reference)>=capacity)return false;memcpy(path,reference,(size_t)(hash-reference));path[hash-reference]=0;*fragment=hash+1;return true;}
bool af_model_mesh_load(const char *reference,af_mesh_file *out,af_asset_error *error){
    if(out)*out=(af_mesh_file){0};char path[AF_ASSET_PATH_CAPACITY];const char *fragment;unsigned mesh,primitive;int length=0;
    if(!reference||!out||!selector(reference,path,sizeof(path),&fragment)||sscanf(fragment,"mesh=%u,primitive=%u%n",&mesh,&primitive,&length)!=2||fragment[length])return error_message(error,"Invalid glTF primitive selector.");
    af_model_file model;if(!af_model_file_load(path,&model,error))return false;bool ok=false;
    for(size_t i=0;i<model.primitive_count;++i)if(model.primitives[i].source_mesh==mesh&&model.primitives[i].source_primitive==primitive){*out=model.primitives[i].mesh;model.primitives[i].mesh=(af_mesh_file){0};ok=true;break;}
    af_model_file_free(&model);return ok||error_message(error,"glTF primitive no longer exists.");
}
bool af_model_image_load(const char *reference,af_image_file *out,af_asset_error *error){
    if(out)*out=(af_image_file){0};char path[AF_ASSET_PATH_CAPACITY];const char *fragment;unsigned image;int length=0;
    if(!reference||!out||!selector(reference,path,sizeof(path),&fragment)||sscanf(fragment,"image=%u%n",&image,&length)!=1||fragment[length])return error_message(error,"Invalid glTF image selector.");
    af_model_file model;if(!af_model_file_load(path,&model,error))return false;bool ok=image<model.image_count;if(ok){*out=model.images[image];model.images[image]=(af_image_file){0};}af_model_file_free(&model);return ok||error_message(error,"glTF image no longer exists.");
}
bool af_scene_import_model(af_scene *scene,const char *prefix,const char *path,af_object_id *root_out,af_asset_error *error){
    if(!scene||!prefix||!prefix[0]||strlen(prefix)>36||!path)return error_message(error,"Model needs a unique prefix (up to 36 bytes) and source path.");
    char absolute[AF_ASSET_PATH_CAPACITY];if(!af_asset_resolve_path(NULL,path,absolute,sizeof(absolute)))return error_message(error,"Model path is too long.");path=absolute;
    af_model_file model;if(!af_model_file_load(path,&model,error))return false;af_animation_file animation;if(!af_animation_file_load(path,&animation,error)){af_model_file_free(&model);return false;}bool animated=animation.clip_count||animation.skin_count;af_scene *next=af_scene_clone(scene);af_object_id *nodes=calloc(model.node_count,sizeof(*nodes));bool ok=false;af_object_id root=0;
    if(!next||!nodes)goto done;root=af_scene_add_object(next,prefix,"__node");if(!root)goto done;
    if(animated){char name[64];snprintf(name,sizeof(name),"%s_animation",prefix);af_animation_component component=af_animation_component_default();snprintf(component.asset,sizeof(component.asset),"%s",name);component.autoplay=animation.clip_count>0;
        if(!af_scene_set_asset(next,name,AF_ASSET_ANIMATION,path)||!af_scene_set_object_animation(next,root,component))goto done;}
    for(size_t i=0;i<model.node_count;++i){af_model_node *node=&model.nodes[i];nodes[i]=af_scene_add_object(next,node->name,"__node");if(!nodes[i]||!af_scene_set_object_transform(next,nodes[i],node->transform)||!af_scene_set_parent(next,nodes[i],node->parent<0?root:nodes[node->parent]))goto done;
        if(animated&&!af_scene_set_model_binding(next,nodes[i],(af_model_binding){.root=root,.node=node->source_node,.skin=-1}))goto done;
        for(size_t j=0;j<model.primitive_count;++j){af_model_primitive *primitive=&model.primitives[j];if(node->mesh<0||primitive->source_mesh!=(unsigned)node->mesh)continue;
            char name[64],reference[AF_ASSET_PATH_CAPACITY];snprintf(name,sizeof(name),"%s_m%u_p%u",prefix,primitive->source_mesh,primitive->source_primitive);
            if(snprintf(reference,sizeof(reference),"%s#mesh=%u,primitive=%u",path,primitive->source_mesh,primitive->source_primitive)>=(int)sizeof(reference))goto done;
            if(!af_scene_set_asset(next,name,AF_ASSET_MESH,reference))goto done;af_object_id object=af_scene_add_object(next,name,name);if(!object||!af_scene_set_parent(next,object,nodes[i]))goto done;
            if(animated&&!af_scene_set_model_binding(next,object,(af_model_binding){.root=root,.node=node->source_node,.skin=node->skin,.primitive=true}))goto done;
            af_material material=primitive->material;int images[]={primitive->base_image,primitive->normal_image,primitive->surface_image};char *names[]={material.texture,material.normal_map,material.metallic_roughness_map};af_asset_kind kinds[]={AF_ASSET_TEXTURE,AF_ASSET_NORMAL_MAP,AF_ASSET_METALLIC_ROUGHNESS};
            for(unsigned t=0;t<3;++t)if(images[t]>=0){snprintf(names[t],64,"%s_i%d_%u",prefix,images[t],t);if(snprintf(reference,sizeof(reference),"%s#image=%d",path,images[t])>=(int)sizeof(reference)||!af_scene_set_asset(next,names[t],kinds[t],reference))goto done;}
            if(!af_scene_set_object_material(next,object,material))goto done;
        }
    }
    {af_scene old=*scene;*scene=*next;*next=old;}if(root_out)*root_out=root;ok=true;
done:
    af_scene_destroy(next);free(nodes);af_model_file_free(&model);af_animation_file_free(&animation);if(!ok)error_message(error,"Cannot instantiate glTF hierarchy/materials (scene/asset limits or invalid values).");return ok;
}

/* Animation data retains source node indices, independent of import preorder. */
#include <afterlight/animation.h>
static bool load_events(const cgltf_animation *a,af_animation_clip *clip){
    if(!a->extras.data)return true;cJSON *json=cJSON_Parse(a->extras.data);if(!json)return false;
    const cJSON *events=cJSON_GetObjectItemCaseSensitive(json,"afterlight_events");bool ok=true;
    if(events){if(!cJSON_IsArray(events)||cJSON_GetArraySize(events)>(int)AF_ANIMATION_MAX_EVENTS)ok=false;
        else for(const cJSON *e=events->child;e;e=e->next){const cJSON *name=cJSON_GetObjectItemCaseSensitive(e,"name"),*time=cJSON_GetObjectItemCaseSensitive(e,"time");
            if(!cJSON_IsString(name)||!name->valuestring[0]||strlen(name->valuestring)>=32||!cJSON_IsNumber(time)||!isfinite(time->valuedouble)||time->valuedouble<0||time->valuedouble>=clip->duration){ok=false;break;}
            af_animation_event *out=&clip->events[clip->event_count++];snprintf(out->name,sizeof(out->name),"%s",name->valuestring);out->time=time->valuedouble;}}
    cJSON_Delete(json);return ok;
}
bool af_animation_file_load(const char *path,af_animation_file *out,af_asset_error *error){
    if(error)*error=(af_asset_error){0};if(out)*out=(af_animation_file){0};if(!path||!out)return error_message(error,"Animation path/output required.");
    memory_budget budget={0};cgltf_data *data=NULL;af_animation_file result={0};bool ok=false;size_t bytes=0,total_channels=0;
    if(!parse_model(path,&budget,&data)||data->nodes_count>AF_ANIMATION_MAX_NODES||!data->nodes_count||data->animations_count>AF_ANIMATION_MAX_CLIPS)goto done;
    result.node_count=data->nodes_count;result.bind_pose=calloc(result.node_count,sizeof(*result.bind_pose));result.parents=calloc(result.node_count,sizeof(*result.parents));
    result.clip_count=data->animations_count;result.clips=calloc(result.clip_count?result.clip_count:1,sizeof(*result.clips));if(!result.bind_pose||!result.parents||!result.clips)goto done;
    for(size_t i=0;i<result.node_count;++i){cgltf_node *n=&data->nodes[i];af_transform t;if(!local_transform(n,&t))goto done;
        result.bind_pose[i]=(af_animation_pose){.translation=t.position,.scale=n->has_matrix?t.scale:(af_vec3){n->scale[0],n->scale[1],n->scale[2]},.rotation=n->has_matrix?af_quaternion_from_euler(t.rotation):(af_vec4){n->rotation[0],n->rotation[1],n->rotation[2],n->rotation[3]}};
        result.parents[i]=n->parent?(int32_t)(n->parent-data->nodes):-1;
    }
    if(data->skins_count>128)goto done;result.skin_count=data->skins_count;result.skins=calloc(result.skin_count?result.skin_count:1,sizeof(*result.skins));if(!result.skins)goto done;
    for(size_t i=0;i<result.skin_count;++i){const cgltf_skin *skin=&data->skins[i];af_animation_skin *out_skin=&result.skins[i];
        if(!skin->joints_count||skin->joints_count>AF_ANIMATION_MAX_JOINTS)goto done;out_skin->joint_count=skin->joints_count;
        if(skin->inverse_bind_matrices&&(skin->inverse_bind_matrices->type!=cgltf_type_mat4||skin->inverse_bind_matrices->component_type!=cgltf_component_type_r_32f||skin->inverse_bind_matrices->count<skin->joints_count))goto done;
        for(size_t j=0;j<skin->joints_count;++j){out_skin->joints[j]=(uint32_t)(skin->joints[j]-data->nodes);out_skin->inverse_bind[j]=af_mat4_identity();
            if(skin->inverse_bind_matrices&&!values(skin->inverse_bind_matrices,j,out_skin->inverse_bind[j].elements,16))goto done;
            float *m=out_skin->inverse_bind[j].elements;if(fabsf(m[3])+fabsf(m[7])+fabsf(m[11])+fabsf(m[15]-1)>1e-5f)goto done;
            for(size_t k=0;k<j;++k)if(out_skin->joints[k]==out_skin->joints[j])goto done;}
    }
    for(size_t i=0;i<result.clip_count;++i){cgltf_animation *a=&data->animations[i];af_animation_clip *clip=&result.clips[i];
        if(!a->channels_count||a->channels_count>1024||total_channels>4096-a->channels_count)goto done;total_channels+=a->channels_count;
        snprintf(clip->name,sizeof(clip->name),"%.63s",a->name&&a->name[0]?a->name:"Clip");clip->channel_count=a->channels_count;clip->channels=calloc(clip->channel_count,sizeof(*clip->channels));if(!clip->channels)goto done;
        for(size_t j=0;j<clip->channel_count;++j){cgltf_animation_channel *source=&a->channels[j];cgltf_animation_sampler *sampler=source->sampler;af_animation_channel *c=&clip->channels[j];
            if(!source->target_node||source->target_node->has_matrix||!sampler||!sampler->input||!sampler->output)goto done;
            c->node=(uint32_t)(source->target_node-data->nodes);
            if(source->target_path==cgltf_animation_path_type_translation)c->path=AF_ANIM_TRANSLATION;else if(source->target_path==cgltf_animation_path_type_rotation)c->path=AF_ANIM_ROTATION;else if(source->target_path==cgltf_animation_path_type_scale)c->path=AF_ANIM_SCALE;else goto done;
            for(size_t k=0;k<j;++k)if(clip->channels[k].node==c->node&&clip->channels[k].path==c->path)goto done;
            if(sampler->interpolation!=cgltf_interpolation_type_linear&&sampler->interpolation!=cgltf_interpolation_type_step&&sampler->interpolation!=cgltf_interpolation_type_cubic_spline)goto done;
            c->interpolation=sampler->interpolation==cgltf_interpolation_type_step?AF_ANIM_STEP:sampler->interpolation==cgltf_interpolation_type_cubic_spline?AF_ANIM_CUBIC:AF_ANIM_LINEAR;
            c->key_count=sampler->input->count;size_t stride=c->interpolation==AF_ANIM_CUBIC?3u:1u,components=c->path==AF_ANIM_ROTATION?4u:3u;
            if(!c->key_count||c->key_count>65536||(stride==3&&c->key_count<2)||sampler->input->type!=cgltf_type_scalar||sampler->input->component_type!=cgltf_component_type_r_32f||sampler->output->component_type!=cgltf_component_type_r_32f||sampler->output->type!=(components==4?cgltf_type_vec4:cgltf_type_vec3)||sampler->output->count!=c->key_count*stride)goto done;
            size_t allocation_bytes=c->key_count*(sizeof(float)+stride*sizeof(af_vec4));if(allocation_bytes>AF_ASSET_FILE_MAX_BYTES-bytes)goto done;bytes+=allocation_bytes;
            c->times=calloc(c->key_count,sizeof(*c->times));c->values=calloc(c->key_count*stride,sizeof(*c->values));if(!c->times||!c->values)goto done;
            for(size_t k=0;k<c->key_count;++k){if(!values(sampler->input,k,&c->times[k],1)||c->times[k]<0||(k&&c->times[k]<=c->times[k-1]))goto done;}
            clip->duration=fmax(clip->duration,c->times[c->key_count-1]);
            for(size_t k=0;k<c->key_count*stride;++k){float v[4]={0};if(!values(sampler->output,k,v,components))goto done;c->values[k]=(af_vec4){v[0],v[1],v[2],v[3]};
                if(components==4&&(stride==1||k%3==1)){float length=sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]+v[3]*v[3]);if(fabsf(length-1)>.01f)goto done;}}
        }
        if(!load_events(a,clip))goto done;
    }
    *out=result;result=(af_animation_file){0};ok=true;
done:
    cgltf_free(data);af_animation_file_free(&result);return ok||error_message(error,"Invalid/unsupported glTF animation: TRS channels, finite increasing times and bounded data required (no morph tracks).");
}

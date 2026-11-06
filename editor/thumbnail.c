/* SPDX-License-Identifier: MPL-2.0 */
#include "thumbnail.h"
#include "cube.h"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_mutex.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static float clamp01(float x){return fmaxf(0,fminf(1,x));}
static float decode(float x){return x<=.04045f?x/12.92f:powf((x+.055f)/1.055f,2.4f);}
static unsigned char encode(float x){x=clamp01(x);return (unsigned char)lrintf(255*(x<=.0031308f?12.92f*x:1.055f*powf(x,1/2.4f)-.055f));}
static void background(unsigned char *rgba){for(unsigned y=0;y<ED_THUMBNAIL_SIZE;++y)for(unsigned x=0;x<ED_THUMBNAIL_SIZE;++x){size_t i=((size_t)y*ED_THUMBNAIL_SIZE+x)*4;unsigned char c=(x/8+y/8)%2?43:32;rgba[i]=c;rgba[i+1]=c;rgba[i+2]=(unsigned char)(c+5);rgba[i+3]=255;}}
static af_vec4 image_sample(const af_image_file *image,float u,float v,bool srgb){
    if(!image->rgba)return (af_vec4){1,1,1,1};
    u-=floorf(u);v-=floorf(v);uint32_t x=(uint32_t)(u*(float)image->width),y=(uint32_t)(v*(float)image->height);
    const unsigned char *p=image->rgba+((size_t)y*image->width+x)*4;
    return (af_vec4){srgb?decode((float)p[0]/255):(float)p[0]/255,srgb?decode((float)p[1]/255):(float)p[1]/255,srgb?decode((float)p[2]/255):(float)p[2]/255,(float)p[3]/255};
}
static void load_map(const af_scene *scene,const char *name,af_image_file *image){const af_scene_asset *asset=af_scene_find_asset(scene,name);af_asset_error error;if(asset)(void)af_image_file_load(asset->path,image,&error);}
static bool texture_preview(const af_scene_asset *asset,unsigned char *rgba){
    af_image_file image;af_asset_error error;if(!af_image_file_load(asset->path,&image,&error))return false;
    float scale=fminf(60.0f/(float)image.width,60.0f/(float)image.height),width=(float)image.width*scale,height=(float)image.height*scale;
    for(unsigned y=0;y<ED_THUMBNAIL_SIZE;++y)for(unsigned x=0;x<ED_THUMBNAIL_SIZE;++x){float u=((float)x+.5f-(64-width)*.5f)/width,v=((float)y+.5f-(64-height)*.5f)/height;if(u<0||u>=1||v<0||v>=1)continue;
        af_vec4 pixel=image_sample(&image,u,v,true);size_t at=((size_t)y*64+x)*4;
        rgba[at]=encode(pixel.x*pixel.w+decode((float)rgba[at]/255)*(1-pixel.w));rgba[at+1]=encode(pixel.y*pixel.w+decode((float)rgba[at+1]/255)*(1-pixel.w));rgba[at+2]=encode(pixel.z*pixel.w+decode((float)rgba[at+2]/255)*(1-pixel.w));
    }
    af_image_file_free(&image);return true;
}
static bool material_preview(const af_scene *scene,const af_material *material,unsigned char *rgba){
    af_image_file base={0},normal={0},surface={0};load_map(scene,material->texture,&base);load_map(scene,material->normal_map,&normal);load_map(scene,material->metallic_roughness_map,&surface);
    af_vec3 light=af_vec3_normalize((af_vec3){-.6f,.8f,1});
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){af_vec3 n={((float)x+.5f-32)/28,(32-(float)y-.5f)/28,0};float squared=n.x*n.x+n.y*n.y;if(squared>1)continue;n.z=sqrtf(1-squared);
        float u=.5f+atan2f(n.z,n.x)/(2*AF_PI),v=acosf(n.y)/AF_PI;af_vec4 texel=image_sample(&base,u,v,true),mr=image_sample(&surface,u,v,false);
        af_vec3 color={material->base_color.r*texel.x,material->base_color.g*texel.y,material->base_color.b*texel.z};
        if(normal.rgba){af_vec4 map=image_sample(&normal,u,v,false);af_vec3 t=af_vec3_normalize(af_vec3_cross((af_vec3){0,1,0},n));if(af_vec3_length(t)>.01f){af_vec3 b=af_vec3_cross(n,t);n=af_vec3_normalize(af_vec3_add(af_vec3_scale(n,map.z*2-1),af_vec3_add(af_vec3_scale(t,(map.x*2-1)*material->normal_strength),af_vec3_scale(b,(map.y*2-1)*material->normal_strength))));}}
        float diffuse=.18f+.85f*fmaxf(0,af_vec3_dot(n,light));
        if(!material->unlit){float rough=fmaxf(.04f,material->roughness*mr.y),metal=material->physically_based?material->metallic*mr.z:0;
            af_vec3 half=af_vec3_normalize(af_vec3_add(light,(af_vec3){0,0,1}));float highlight=powf(fmaxf(0,af_vec3_dot(n,half)),2/(rough*rough))*(1-rough)*3;
            color=(af_vec3){color.x*diffuse*(1-metal)+(.04f*(1-metal)+color.x*metal)*(.2f+highlight),color.y*diffuse*(1-metal)+(.04f*(1-metal)+color.y*metal)*(.2f+highlight),color.z*diffuse*(1-metal)+(.04f*(1-metal)+color.z*metal)*(.2f+highlight)};
        }
        color=af_vec3_add(color,af_vec3_scale(material->emission,material->emission_strength));size_t at=((size_t)y*64+x)*4;
        rgba[at]=encode(color.x/(1+color.x));rgba[at+1]=encode(color.y/(1+color.y));rgba[at+2]=encode(color.z/(1+color.z));
    }
    af_image_file_free(&base);af_image_file_free(&normal);af_image_file_free(&surface);return true;
}
static float edge(af_vec3 a,af_vec3 b,float x,float y){return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);}
static bool mesh_preview(const af_scene_asset *asset,unsigned char *rgba){
    af_mesh_file file={0};af_asset_error error;bool builtin=!strcmp(asset->path,"Built-in mesh");af_mesh_data mesh;
    if(builtin)mesh=cube_mesh_data();else{if(!af_mesh_file_load(asset->path,&file,&error))return false;mesh=file.data;}
    af_mat4 rotation=af_mat4_mul(af_mat4_rotation_x(-.38f),af_mat4_rotation_y(.65f));af_vec3 lo={FLT_MAX,FLT_MAX,FLT_MAX},hi={-FLT_MAX,-FLT_MAX,-FLT_MAX};
    for(size_t i=0;i<mesh.vertex_count;++i){af_vec3 p=mesh.vertices[i].position;lo.x=fminf(lo.x,p.x);lo.y=fminf(lo.y,p.y);lo.z=fminf(lo.z,p.z);hi.x=fmaxf(hi.x,p.x);hi.y=fmaxf(hi.y,p.y);hi.z=fmaxf(hi.z,p.z);}
    af_vec3 center=af_vec3_scale(af_vec3_add(lo,hi),.5f);float scale=48/fmaxf(.0001f,af_vec3_length(af_vec3_sub(hi,lo)));float depth[64*64];for(unsigned i=0;i<64*64;++i)depth[i]=-FLT_MAX;
    size_t triangles=mesh.index_count/3,step=triangles>65536?(triangles+65535)/65536:1;
    af_vec3 light=af_vec3_normalize((af_vec3){-.5f,.8f,1});
    for(size_t tri=0;tri<triangles;tri+=step){af_vec3 p[3],normals[3];af_color colors[3];
        for(unsigned k=0;k<3;++k){const af_vertex *vertex=&mesh.vertices[mesh.indices[tri*3+k]];af_vec3 pos=af_vec3_sub(vertex->position,center);af_vec4 r=af_mat4_transform(rotation,(af_vec4){pos.x,pos.y,pos.z,1});p[k]=(af_vec3){32+r.x*scale,32-r.y*scale,r.z};r=af_mat4_transform(rotation,(af_vec4){vertex->normal.x,vertex->normal.y,vertex->normal.z,0});normals[k]=(af_vec3){r.x,r.y,r.z};colors[k]=vertex->color;}
        float area=edge(p[0],p[1],p[2].x,p[2].y);if(fabsf(area)<.00001f)continue;
        int x0=(int)fmaxf(0,floorf(fminf(p[0].x,fminf(p[1].x,p[2].x)))),x1=(int)fminf(63,ceilf(fmaxf(p[0].x,fmaxf(p[1].x,p[2].x))));
        int y0=(int)fmaxf(0,floorf(fminf(p[0].y,fminf(p[1].y,p[2].y)))),y1=(int)fminf(63,ceilf(fmaxf(p[0].y,fmaxf(p[1].y,p[2].y))));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){float a=edge(p[1],p[2],(float)x+.5f,(float)y+.5f)/area,b=edge(p[2],p[0],(float)x+.5f,(float)y+.5f)/area,c=1-a-b;if(a<0||b<0||c<0)continue;
            float z=a*p[0].z+b*p[1].z+c*p[2].z;size_t at=(size_t)y*64+(size_t)x;if(z<=depth[at])continue;depth[at]=z;
            af_vec3 n=af_vec3_normalize(af_vec3_add(af_vec3_scale(normals[0],a),af_vec3_add(af_vec3_scale(normals[1],b),af_vec3_scale(normals[2],c))));
            float shade=.25f+.75f*fmaxf(0,af_vec3_dot(n,light));
            rgba[at*4]=encode((a*colors[0].r+b*colors[1].r+c*colors[2].r)*shade);rgba[at*4+1]=encode((a*colors[0].g+b*colors[1].g+c*colors[2].g)*shade);rgba[at*4+2]=encode((a*colors[0].b+b*colors[1].b+c*colors[2].b)*shade);
        }
    }af_mesh_file_free(&file);return true;
}
bool ed_thumbnail_generate(const af_scene *scene,const af_scene_asset *asset,const af_material *material,unsigned char rgba[64*64*4]){
    if(!rgba||(!asset&&!material))return false;background(rgba);
    if(material)return material_preview(scene,material,rgba);
    if(asset->kind==AF_ASSET_ENVIRONMENT){af_hdr_file hdr;af_asset_error error;if(!af_hdr_file_load(asset->path,&hdr,&error))return false;
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<64;++x){uint32_t sx=x*hdr.width/64,sy=y*hdr.height/32;const float *p=hdr.rgba+((size_t)sy*hdr.width+sx)*4;size_t at=((size_t)(y+16)*64+x)*4;for(unsigned c=0;c<3;++c)rgba[at+c]=encode(p[c]/(1+p[c]));}af_hdr_file_free(&hdr);return true;}
    if(asset->kind==AF_ASSET_MESH)return mesh_preview(asset,rgba);
    if(asset->kind==AF_ASSET_AUDIO){for(unsigned x=5;x<59;++x){int height=(int)(5+14*fabsf(sinf((float)x*.7f)*sinf((float)x*.13f)));for(int y=32-height;y<=32+height;++y){size_t i=((size_t)y*64+x)*4;rgba[i]=105;rgba[i+1]=190;rgba[i+2]=215;}}return true;}
    return texture_preview(asset,rgba);
}
static uint64_t hash(uint64_t h,const void *data,size_t size){const unsigned char *p=data;for(size_t i=0;i<size;++i){h^=p[i];h*=UINT64_C(1099511628211);}return h;}
static uint64_t path_signature(uint64_t signature,const char *reference){
    signature=hash(signature,reference,strlen(reference));char path[AF_ASSET_PATH_CAPACITY];size_t length=strcspn(reference,"#");if(length>=sizeof(path))return signature;memcpy(path,reference,length);path[length]=0;
    SDL_PathInfo info;if(SDL_GetPathInfo(path,&info)){signature=hash(signature,&info.size,sizeof(info.size));signature=hash(signature,&info.modify_time,sizeof(info.modify_time));}return signature;
}
typedef struct thumbnail_job {
    af_scene *scene;af_scene_asset asset;af_material material;bool is_material,success;
    unsigned index;uint64_t ticket;unsigned char pixels[64*64*4];
} thumbnail_job;
typedef struct ed_thumbnail_worker {
    SDL_Thread *thread;SDL_Mutex *mutex;SDL_Condition *condition;
    thumbnail_job *jobs[8],*results[8];unsigned job_count,result_count,total;bool stop;
} ed_thumbnail_worker;
static void free_job(thumbnail_job *job){if(job){af_scene_destroy(job->scene);free(job);}}
static int SDLCALL thumbnail_worker(void *data){
    ed_thumbnail_worker *worker=data;
    for(;;){SDL_LockMutex(worker->mutex);while(!worker->job_count&&!worker->stop)SDL_WaitCondition(worker->condition,worker->mutex);
        if(worker->stop){SDL_UnlockMutex(worker->mutex);return 0;}
        thumbnail_job *job=worker->jobs[0];memmove(worker->jobs,worker->jobs+1,(--worker->job_count)*sizeof(*worker->jobs));SDL_UnlockMutex(worker->mutex);
        job->success=ed_thumbnail_generate(job->scene,job->is_material?NULL:&job->asset,job->is_material?&job->material:NULL,job->pixels);
        SDL_LockMutex(worker->mutex);worker->results[worker->result_count++]=job;SDL_UnlockMutex(worker->mutex);
    }
}
static bool start_worker(ed_thumbnails *cache){
    if(cache->worker)return true;ed_thumbnail_worker *worker=calloc(1,sizeof(*worker));if(!worker)return false;
    worker->mutex=SDL_CreateMutex();worker->condition=SDL_CreateCondition();
    if(worker->mutex&&worker->condition)worker->thread=SDL_CreateThread(thumbnail_worker,"Afterlight thumbnails",worker);
    if(!worker->thread){SDL_DestroyCondition(worker->condition);SDL_DestroyMutex(worker->mutex);free(worker);return false;}cache->worker=worker;return true;
}
void ed_thumbnails_destroy(ed_thumbnails *cache){
    if(!cache||!cache->worker)return;ed_thumbnail_worker *worker=cache->worker;
    SDL_LockMutex(worker->mutex);worker->stop=true;SDL_SignalCondition(worker->condition);SDL_UnlockMutex(worker->mutex);SDL_WaitThread(worker->thread,NULL);
    for(unsigned i=0;i<worker->job_count;++i)free_job(worker->jobs[i]);for(unsigned i=0;i<worker->result_count;++i)free_job(worker->results[i]);
    SDL_DestroyCondition(worker->condition);SDL_DestroyMutex(worker->mutex);free(worker);*cache=(ed_thumbnails){0};
}
void ed_thumbnails_poll(ed_thumbnails *cache,af_engine *engine){
    ed_thumbnail_worker *worker=cache->worker;if(!worker||!engine||!cache->budget)return;
    SDL_LockMutex(worker->mutex);thumbnail_job *job=worker->result_count?worker->results[0]:NULL;
    if(job){memmove(worker->results,worker->results+1,(--worker->result_count)*sizeof(*worker->results));--worker->total;}SDL_UnlockMutex(worker->mutex);
    if(!job)return;ed_thumbnail_entry *entry=&cache->entries[job->index];
    if(entry->ticket==job->ticket&&entry->pending){entry->pending=false;
        if(job->success&&af_engine_upload_overlay_texture(engine,job->index+2,job->pixels,64,64))entry->ready=true;
        ++cache->completed;--cache->budget;
    }else ++cache->discarded;free_job(job);
}
void ed_thumbnails_begin(ed_thumbnails *cache){++cache->frame;cache->budget=1;}
void ed_thumbnails_invalidate(ed_thumbnails *cache){for(unsigned i=0;i<ED_THUMBNAIL_CACHE_SIZE;++i){cache->entries[i].signature=0;cache->entries[i].ticket=++cache->next_ticket;cache->entries[i].pending=false;}}
uint32_t ed_thumbnails_get(ed_thumbnails *cache,af_engine *engine,const af_scene *scene,const af_scene_asset *asset,const af_material *material){
    if(!cache||!engine||(!asset&&!material))return 0;
    const char *name=material?"@material":asset->name;uint64_t signature=UINT64_C(14695981039346656037);unsigned index=ED_THUMBNAIL_CACHE_SIZE;
    if(material){signature=hash(signature,material,sizeof(*material));const char *maps[]={material->texture,material->normal_map,material->metallic_roughness_map};for(unsigned i=0;i<3;++i){const af_scene_asset *map=af_scene_find_asset(scene,maps[i]);if(map)signature=path_signature(signature,map->path);}}
    else{signature=path_signature(signature,asset->path);signature=hash(signature,&asset->kind,sizeof(asset->kind));}
    for(unsigned i=0;i<ED_THUMBNAIL_CACHE_SIZE;++i)if(cache->entries[i].occupied&&cache->entries[i].material==(material!=NULL)&&!strcmp(cache->entries[i].name,name)){index=i;break;}
    bool same=index<ED_THUMBNAIL_CACHE_SIZE;
    if(same){ed_thumbnail_entry *entry=&cache->entries[index];entry->last_used=cache->frame;if(entry->signature==signature)return entry->ready?index+2:0;}
    if(!start_worker(cache))return same&&cache->entries[index].ready?index+2:0;
    if(!same){uint64_t oldest=UINT64_MAX;for(unsigned i=0;i<ED_THUMBNAIL_CACHE_SIZE;++i){ed_thumbnail_entry *entry=&cache->entries[i];if(!entry->occupied){index=i;break;}if(!entry->pending&&entry->last_used<cache->frame&&entry->last_used<oldest){oldest=entry->last_used;index=i;}}}
    if(index==ED_THUMBNAIL_CACHE_SIZE)return 0;
    ed_thumbnail_worker *worker=cache->worker;SDL_LockMutex(worker->mutex);bool full=worker->total>=8;SDL_UnlockMutex(worker->mutex);if(full)return same&&cache->entries[index].ready?index+2:0;
    thumbnail_job *job=calloc(1,sizeof(*job));if(!job)return same&&cache->entries[index].ready?index+2:0;
    job->scene=af_scene_create();if(!job->scene){free(job);return 0;}
    if(material){job->is_material=true;job->material=*material;const char *maps[]={material->texture,material->normal_map,material->metallic_roughness_map};
        for(unsigned i=0;i<3;++i){const af_scene_asset *map=af_scene_find_asset(scene,maps[i]);if(map&&!af_scene_set_asset(job->scene,map->name,map->kind,map->path)){free_job(job);return 0;}}}
    else job->asset=*asset;
    ed_thumbnail_entry *entry=&cache->entries[index];bool ready=same&&entry->ready;
    *entry=(ed_thumbnail_entry){.signature=signature,.last_used=cache->frame,.ticket=++cache->next_ticket,.occupied=true,.ready=ready,.material=material!=NULL,.pending=true};memcpy(entry->name,name,strlen(name)+1);
    job->index=index;job->ticket=entry->ticket;
    SDL_LockMutex(worker->mutex);worker->jobs[worker->job_count++]=job;++worker->total;SDL_SignalCondition(worker->condition);SDL_UnlockMutex(worker->mutex);++cache->submitted;
    return ready?index+2:0;
}

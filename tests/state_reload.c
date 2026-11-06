/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime.h"
#include <SDL3/SDL_filesystem.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"State reload %d: %s (%s)\n",__LINE__,#x,r.status);return 1;}}while(0)
int main(int argc,char **argv){ed_runtime r={0};CHECK(argc==3);af_scene *s=af_scene_create();af_object_id id=af_scene_add_object(s,"Counter","cube");CHECK(SDL_CopyFile(argv[1],"state.module")&&ed_runtime_start_file(&r,s,NULL,"state.module"));ed_runtime_update(&r,.01,false);ed_runtime_update(&r,.01,false);r.paused=true;CHECK(ed_runtime_reload(&r)&&r.paused&&strstr(r.status,"restored"));r.paused=false;ed_runtime_update(&r,.01,false);CHECK(af_scene_get_object(r.scene,id)->transform.position.x==3);CHECK(SDL_CopyFile(argv[2],"state.module")&&!ed_runtime_reload(&r));CHECK(af_scene_get_object(r.scene,id)->transform.position.x==3);ed_runtime_update(&r,.01,false);CHECK(af_scene_get_object(r.scene,id)->transform.position.x==4&&af_scene_get_object(s,id)->transform.position.x==0);ed_runtime_stop(&r);af_scene_destroy(s);remove("state.module");puts("State reload: serialized progress, paused state, incompatible-load rollback and last-good continuation passed.");return 0;}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "runtime.h"
#include "document.h"
#include <stdio.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"Runtime check %d: %s\n",__LINE__,#c);return 1;}}while(0)
static unsigned shutdown_count;
static bool fail_init(void *state,const af_game_host *host){(void)state;host->add_object(host->scene,"Failed init object","cube");return false;}
static void failure_shutdown(void *state,const af_game_host *host){(void)state;(void)host;++shutdown_count;}
int main(void){
    ed_document d;CHECK(ed_document_init(&d)&&ed_document_add_cube(&d));af_object_id id=d.selection;
    uint64_t revision=d.revision;ed_runtime runtime={0};const af_game_module *api=af_game_get_api(AF_GAME_ABI_VERSION);
    CHECK(api&&ed_runtime_start(&runtime,d.scene,NULL,api));CHECK(runtime.playing&&runtime.scene!=d.scene&&!ed_runtime_start(&runtime,d.scene,NULL,api));
    ed_runtime_update(&runtime,0.1,false);CHECK(af_scene_get_object(runtime.scene,id)->transform.rotation.y>0);
    CHECK(af_scene_get_object(d.scene,id)->transform.rotation.y==0&&d.revision==revision);
    float rotation=af_scene_get_object(runtime.scene,id)->transform.rotation.y;runtime.paused=true;ed_runtime_update(&runtime,0.1,false);
    CHECK(af_scene_get_object(runtime.scene,id)->transform.rotation.y==rotation);
    ed_runtime_stop(&runtime);CHECK(!runtime.playing&&!runtime.scene&&d.revision==revision&&af_scene_get_object(d.scene,id)->transform.rotation.y==0);
    ed_runtime_stop(&runtime);af_game_module invalid=*api;invalid.abi_version=999;CHECK(!ed_runtime_start(&runtime,d.scene,NULL,&invalid));
    invalid=*api;invalid.init=fail_init;invalid.shutdown=failure_shutdown;
    CHECK(!ed_runtime_start(&runtime,d.scene,NULL,&invalid)&&shutdown_count==1&&af_scene_object_count(d.scene)==1);
    CHECK(ed_runtime_start(&runtime,d.scene,NULL,api));ed_runtime_stop(&runtime);ed_document_destroy(&d);
    puts("Runtime: scene isolation, pause, stop/restore, ABI rejection, failed-init cleanup passed.");return 0;
}

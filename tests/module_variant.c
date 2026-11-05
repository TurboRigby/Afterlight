/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/game_module.h>
static bool init(void *state,const af_game_host *host){(void)state;host->add_object(host->scene,"Failed candidate","cube");return false;}
static void update(void *state,const af_game_host *host,double delta){(void)state;(void)host;(void)delta;}
static void shutdown(void *state,const af_game_host *host){(void)state;(void)host;}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t version){
    (void)version;
    static const af_game_module api={
#ifdef AF_TEST_BAD_ABI
        999,
#else
        AF_GAME_ABI_VERSION,
#endif
        sizeof(af_game_module),sizeof(af_game_host),sizeof(af_scene_object),"Rejected candidate",1,init,update,shutdown,NULL};
    return &api;
}

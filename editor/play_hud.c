/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "play_hud.h"
#include <stdio.h>
static void line(struct nk_context *ctx,const char *text){nk_layout_row_dynamic(ctx,24,1);nk_label(ctx,text,NK_TEXT_LEFT);}
void ed_play_hud(struct nk_context *ctx,af_engine *engine,af_rect r,bool paused,bool free_camera,bool editor,bool player){
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,nk_style_item_hide());
    if(nk_begin(ctx,"Play HUD",nk_rect(r.x,r.y,r.width,r.height),NK_WINDOW_NO_INPUT|NK_WINDOW_NO_SCROLLBAR)){
        bool animated=false;const af_scene *scene=af_engine_get_scene(engine);for(size_t i=0;i<af_scene_object_count(scene);++i){const af_scene_object *o=af_scene_object_at(scene,i);const af_scene_object *parent=af_scene_get_object(scene,o->parent);if(o->animation.asset[0]&&parent&&parent->gameplay.player.enabled){animated=true;break;}}
        if(paused)line(ctx,"PAUSED | Step 1/60 to advance");
        else if(animated){line(ctx,"WASD walk | Shift run | Space jump");line(ctx,"E toggle root motion | R restart");if(af_physics_active(scene))line(ctx,"F grab/release | V throw | Q crate | N room");}
        else line(ctx,player?"WASD walk | Space jump | R new run":"C game demo | Hold right mouse + WASD for camera");
        if(player){af_gameplay_status status=af_engine_gameplay_status(engine);char text[160];
            snprintf(text,sizeof(text),"%s %.2f s | Checkpoint %u/%u | Falls %u",status.finished?"FINISHED":"Time",status.finished?status.finish_time:status.elapsed,status.checkpoint,status.checkpoint_count,status.deaths);line(ctx,text);
            if(status.finished)line(ctx,"Course complete! R starts a new run.");
            line(ctx,free_camera?"EDITOR CAMERA | Release right mouse to return":af_engine_mouse_captured(engine)?(editor?"PLAYER | Escape releases mouse | F6 maximize":"PLAYER | Escape releases mouse"):"MOUSE RELEASED | Click viewport to play");
            if(!free_camera&&!animated){struct nk_command_buffer *canvas=nk_window_get_canvas(ctx);float x=r.x+r.width/2,y=r.y+r.height/2;
                nk_stroke_line(canvas,x-6,y,x+6,y,2,nk_rgba(245,245,250,220));nk_stroke_line(canvas,x,y-6,x,y+6,2,nk_rgba(245,245,250,220));}
        }
    }nk_end(ctx);nk_style_pop_style_item(ctx);
}

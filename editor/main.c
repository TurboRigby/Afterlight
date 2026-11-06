/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include <afterlight/project.h>
#include <afterlight/asset_watch.h>
#include "document.h"
#include "asset_browser.h"
#include "thumbnail.h"
#include "gizmo.h"
#include "light_gizmo.h"
#include "collider_view.h"
#include "runtime.h"
#include "gui.h"
#include "play_hud.h"
#include "dialog.h"
#include "platform_sdl.h"
#include "render_test.h"
#include "cube.h"
#include <SDL3/SDL_process.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum ed_action { ED_NONE, ED_NEW, ED_OPEN, ED_OPEN_PROJECT, ED_CLOSE } ed_action;
typedef struct editor {
    ed_document document;
    ed_gui gui;
    ed_thumbnails thumbnails;
    ed_gizmo gizmo;
    ed_runtime runtime;
    af_asset_watch *asset_watch;
    bool watch_assets;
    af_project project;
    SDL_Process *tool_process;
    int tool_action;
    bool wizard_test,tool_success,thumbnail_test,light_test;unsigned light_test_step;af_vec3 light_test_position;
    char wizard_name[128],wizard_folder[AF_ASSET_PATH_CAPACITY],tool_project[AF_ASSET_PATH_CAPACITY];
    char project_path[AF_ASSET_PATH_CAPACITY];
    const char *initial_project;
    bool show_project,module_override,project_is_template;
    af_camera edit_camera;
    char module_path[AF_ASSET_PATH_CAPACITY];
    char runtime_status[256];
    af_camera camera;
    af_engine *engine;
    af_rect viewport;
    ed_file_dialog *dialog;
    bool dialog_save;
    int dialog_asset;
    ed_action after_save;
    ed_action pending;
    bool modal;
    bool refresh_fields;
    bool fields_active;
    bool free_camera,resume_game_capture,maximize_play;
    bool invalid_name;
    bool failed;
    bool show_performance,show_assets,show_colliders;
    bool environment_pending;char environment_name[AF_MESH_NAME_CAPACITY],environment_path[ED_PATH_CAPACITY];uint64_t environment_document_revision;
    bool show_animation,animation_preview,show_skeleton;af_object_id animation_target;float animation_blend;
    const char *animation_test_path;unsigned animation_test_step;uint64_t animation_test_revision;double animation_test_time;af_rect animation_buttons[3];
    char asset_search[128],selected_asset[AF_MESH_NAME_CAPACITY];
    int asset_filter;
    size_t selected_light;bool light_selected,light_dragging;size_t drag_light;af_local_light initial_light;uint64_t light_drag_revision;
    unsigned light_test_focus;uint64_t light_test_focus_losses;
    bool document_ready;
    char name[AF_OBJECT_NAME_CAPACITY];
    char mesh[AF_MESH_NAME_CAPACITY];
    char texture[AF_MESH_NAME_CAPACITY];
    char normal_map[AF_MESH_NAME_CAPACITY];
    char title[512];
    const char *initial_scene;
    bool playground,play_controls_test;
    const char *smoke_path;
    uint64_t frame_limit;
    unsigned updates;
    uint64_t smoke_ticks;
    unsigned play_test_started,play_test_focus;
    uint64_t play_test_focus_losses;
} editor;

static void reset_camera(editor *app)
{
    app->camera=af_camera_default();
    app->camera.position=(af_vec3){6,4,8};
    app->camera.yaw=atan2f(-8,-6);
    app->camera.pitch=atan2f(-4,10);
}
static bool inside(af_rect r, af_vec2 p)
{
    return p.x>=r.x&&p.y>=r.y&&p.x<r.x+r.width&&p.y<r.y+r.height;
}
static void set_status(editor *app,const char *text)
{
    snprintf(app->document.status,sizeof(app->document.status),"%s",text);
}
static void refresh(editor *app) { app->refresh_fields=true;ed_document_end_edit(&app->document); }
static void stop_play(editor *app)
{
    if(!app->runtime.playing)return;
    af_engine_set_scene(app->engine,app->document.scene);ed_runtime_stop(&app->runtime);
    app->camera=app->edit_camera;(void)af_engine_set_mouse_capture(app->engine,false);
    app->free_camera=app->resume_game_capture=app->maximize_play=false;
    set_status(app,app->runtime.status);refresh(app);
}
static void toggle_play(editor *app)
{
    if(app->runtime.playing){stop_play(app);return;}
    if(app->modal||app->dialog||app->invalid_name)return;
    ed_gui_unfocus(&app->gui);app->fields_active=false;
    ed_document_end_edit(&app->document);app->gizmo.dragging=false;app->edit_camera=app->camera;
    app->free_camera=app->resume_game_capture=false;
    ed_runtime_start_file(&app->runtime,app->document.scene,app->engine,app->module_path);
    set_status(app,app->runtime.status);
}
static void start_dialog(editor *app,bool save)
{
    if(app->dialog)return;
    (void)af_engine_set_mouse_capture(app->engine,false);
    const char *path=app->document.path[0]?app->document.path:(save?"Untitled.afterlight.json":NULL);
    app->dialog=ed_dialog_begin(app->engine,save,path);
    app->dialog_save=save;app->dialog_asset=0;
    if(!app->dialog)set_status(app,"Cannot open the file dialog.");
}
static void start_import(editor *app,af_asset_kind kind)
{
    if(app->dialog||app->modal)return;
    app->dialog=ed_dialog_begin_asset(app->engine,kind);app->dialog_asset=kind==AF_ASSET_ENVIRONMENT?9:kind==AF_ASSET_MESH?1:kind==AF_ASSET_AUDIO?7:kind==AF_ASSET_METALLIC_ROUGHNESS?8:kind==AF_ASSET_NORMAL_MAP?4:2;
    app->dialog_save=false;
    if(!app->dialog)set_status(app,"Cannot open import dialog.");
}
static void load_assets(editor *app)
{
    af_asset_error error;
    if(!af_engine_load_scene_assets(app->engine,app->document.scene,&error))set_status(app,error.message);
}
static bool open_project(editor *app,const char *path,bool untitled)
{
    af_project project;af_asset_error error;
    if(!af_project_load(path,&project,&error)){set_status(app,error.message);return false;}
    if(!ed_document_load(&app->document,project.startup_scene,untitled))return false;
    app->project=project;app->project_is_template=untitled;snprintf(app->project_path,sizeof(app->project_path),"%s",path);
    if(!app->module_override)snprintf(app->module_path,sizeof(app->module_path),"%s",project.game_module);
    refresh(app);reset_camera(app);return true;
}
static void perform_action(editor *app,ed_action action)
{
    app->pending=ED_NONE;app->modal=false;
    if(action==ED_NEW){if(ed_document_new(&app->document))refresh(app);}
    if(action==ED_OPEN)start_dialog(app,false);
    if(action==ED_OPEN_PROJECT){app->dialog=ed_dialog_begin_project(app->engine,false,NULL);app->dialog_asset=5;}
    if(action==ED_CLOSE)af_engine_request_quit(app->engine);
}
static void request_action(editor *app,ed_action action)
{
    if(app->dialog||app->modal||app->tool_process)return;
    stop_play(app);
    ed_document_end_edit(&app->document);
    if(ed_document_dirty(&app->document)){
        app->pending=action;app->modal=true;(void)af_engine_set_mouse_capture(app->engine,false);
    }else perform_action(app,action);
}
static void save_document(editor *app,bool save_as,ed_action after)
{
    if(app->invalid_name){set_status(app,"Enter a non-empty object name and mesh name before saving.");return;}
    ed_document_end_edit(&app->document);
    if(save_as||!app->document.path[0]){
        app->after_save=after;app->modal=false;start_dialog(app,true);
    }else if(ed_document_save(&app->document,app->document.path))perform_action(app,after);
}
static void poll_dialog(editor *app)
{
    if(!app->dialog)return;
    char path[ED_PATH_CAPACITY],error[256];
    int result=ed_dialog_poll(app->dialog,path,sizeof(path),error,sizeof(error));
    if(!result)return;
    ed_dialog_release(app->dialog);app->dialog=NULL;
    if(result<0){set_status(app,error);app->after_save=ED_NONE;return;}
    if(result==2){set_status(app,"File dialog canceled.");app->after_save=ED_NONE;app->pending=ED_NONE;return;}
    if(app->dialog_asset==5){
        app->module_override=false;if(open_project(app,path,false))load_assets(app);app->dialog_asset=0;
    }else if(app->dialog_asset==6){
        af_project project=app->project;af_asset_error project_error;
        if(!project.name[0])snprintf(project.name,sizeof(project.name),"%s",ed_path_name(path));
        snprintf(project.startup_scene,sizeof(project.startup_scene),"%s",app->document.path);
        snprintf(project.game_module,sizeof(project.game_module),"%s",app->module_path);
        if(af_project_save(path,&project,&project_error)){app->project=project;app->project_is_template=false;snprintf(app->project_path,sizeof(app->project_path),"%s",path);set_status(app,"Project saved.");}
        else set_status(app,project_error.message);app->dialog_asset=0;
    }else if(app->dialog_asset==3){
        snprintf(app->module_path,sizeof(app->module_path),"%s",path);set_status(app,"C game selected. Press Play to load it.");app->dialog_asset=0;
    }else if(app->dialog_asset){
        uint64_t hash=UINT64_C(14695981039346656037);for(const unsigned char *p=(const unsigned char*)path;*p;++p){hash^=*p;hash*=UINT64_C(1099511628211);}
        char name[AF_MESH_NAME_CAPACITY];snprintf(name,sizeof(name),"%s_%016" PRIx64,app->dialog_asset==9?"environment":app->dialog_asset==1?"mesh":app->dialog_asset==7?"sound":app->dialog_asset==4?"normal":"texture",hash);
        af_asset_error asset_error;
        if(app->dialog_asset==1){
            bool model=af_asset_is_model_path(path);
            if(model){if(ed_document_import_mesh(&app->document,name,path)){load_assets(app);refresh(app);}}
            else if(af_engine_load_mesh(app->engine,name,path,&asset_error)){ed_document_import_mesh(&app->document,name,path);refresh(app);}
            else set_status(app,asset_error.message);
        }else if(app->dialog_asset==9){
            if(af_engine_start_environment(app->engine,name,path,&asset_error)){app->environment_pending=true;snprintf(app->environment_name,sizeof(app->environment_name),"%s",name);snprintf(app->environment_path,sizeof(app->environment_path),"%s",path);app->environment_document_revision=app->document.revision;}else set_status(app,asset_error.message);
        }else if(app->dialog_asset==7){
            if(af_engine_load_sound(app->engine,name,path,&asset_error)){ed_document_import_sound(&app->document,name,path);refresh(app);}
            else set_status(app,asset_error.message);
        }else if(app->dialog_asset==8){
            if(af_engine_load_normal_map(app->engine,name,path,&asset_error)){ed_document_import_surface(&app->document,name,path);refresh(app);}else set_status(app,asset_error.message);
        }else if(app->dialog_asset==4){
            if(af_engine_load_normal_map(app->engine,name,path,&asset_error)){ed_document_import_normal_map(&app->document,name,path);refresh(app);}
            else set_status(app,asset_error.message);
        }else{
            if(af_engine_load_texture(app->engine,name,path,&asset_error)){ed_document_import_texture(&app->document,name,path);refresh(app);}
            else set_status(app,asset_error.message);
        }
        app->dialog_asset=0;
    }else if(app->dialog_save){
        ed_action after=app->after_save;app->after_save=ED_NONE;
        if(ed_document_save(&app->document,path))perform_action(app,after);
        else if(after!=ED_NONE){app->pending=after;app->modal=true;}
    }else if(ed_document_load(&app->document,path,false)){refresh(app);reset_camera(app);load_assets(app);}
}
static bool should_close(af_engine *engine,void *userdata)
{
    (void)engine;editor *app=userdata;
    if(app->dialog||app->tool_process)return false;
    stop_play(app);
    if(!app->document_ready||!ed_document_dirty(&app->document))return true;
    app->pending=ED_CLOSE;app->modal=true;
    (void)af_engine_set_mouse_capture(app->engine,false);
    return false;
}
static void select_object(editor *app,af_object_id id)
{
    app->light_selected=false;
    if(app->document.selection!=id){app->document.selection=id;refresh(app);}
}
static void focus_selection(editor *app)
{
    const af_scene_object *object=af_scene_get_object(app->document.scene,app->document.selection);
    if(app->light_selected){const af_local_light *light=af_scene_local_light_at(app->document.scene,app->selected_light);if(light)app->camera.position=af_vec3_sub(light->position,af_vec3_scale(af_camera_forward(&app->camera),fmaxf(3,light->range*.3f)));return;}
    if(!object)return;
    af_mat4 world;if(!af_scene_world_matrix(app->document.scene,object->id,&world))return;
    float distance=fmaxf(3.5f,af_vec3_length(object->transform.scale)*1.8f);
    af_vec3 center={world.elements[12],world.elements[13],world.elements[14]};
    app->camera.position=af_vec3_sub(center,af_vec3_scale(af_camera_forward(&app->camera),distance));
}
static void keyboard(editor *app,double delta)
{
    af_engine *engine=app->engine;
    if(app->free_camera&&(!af_engine_mouse_down(engine,AF_MOUSE_RIGHT)||!af_engine_mouse_captured(engine))){
        /* Focus loss already clears engine capture; never restore it in that case. */
        bool resume=app->resume_game_capture&&app->runtime.playing&&af_engine_mouse_captured(engine);
        app->free_camera=app->resume_game_capture=false;
        (void)af_engine_set_mouse_capture(engine,resume);
    }
    if(app->modal||app->dialog)return;
    if(af_engine_key_pressed(engine,AF_KEY_F5))toggle_play(app);
    if(app->runtime.playing&&af_engine_key_pressed(engine,AF_KEY_F6))app->maximize_play=!app->maximize_play;
    bool modifier=af_engine_key_down(engine,AF_KEY_LEFT_CONTROL)||af_engine_key_down(engine,AF_KEY_LEFT_SUPER)||
        af_engine_key_pressed(engine,AF_KEY_LEFT_CONTROL)||af_engine_key_pressed(engine,AF_KEY_LEFT_SUPER);
    bool shift=af_engine_key_down(engine,AF_KEY_LEFT_SHIFT)||af_engine_key_pressed(engine,AF_KEY_LEFT_SHIFT);
    if(modifier&&!app->runtime.playing){
        if(af_engine_key_pressed(engine,AF_KEY_N)){if(shift)app->show_project=true;else request_action(app,ED_NEW);}
        if(af_engine_key_pressed(engine,AF_KEY_O))request_action(app,ED_OPEN);
        if(af_engine_key_pressed(engine,AF_KEY_S))save_document(app,shift,ED_NONE);
    }
    if(modifier&&!app->fields_active&&!app->runtime.playing){
        if(af_engine_key_pressed(engine,AF_KEY_Z)){if(shift)ed_document_redo(&app->document);else ed_document_undo(&app->document);refresh(app);}
        if(af_engine_key_pressed(engine,AF_KEY_Y)){ed_document_redo(&app->document);refresh(app);}
        if(af_engine_key_pressed(engine,AF_KEY_D)){ed_document_duplicate(&app->document);refresh(app);}
    }
    if(!app->runtime.playing&&!app->fields_active&&!modifier&&af_engine_key_pressed(engine,AF_KEY_DELETE)){ed_document_delete(&app->document);refresh(app);}
    if(!app->fields_active&&!modifier&&af_engine_key_pressed(engine,AF_KEY_F))focus_selection(app);
    if(af_engine_key_pressed(engine,AF_KEY_ESCAPE)){
        app->free_camera=app->resume_game_capture=false;
        (void)af_engine_set_mouse_capture(engine,false);
        ed_gui_unfocus(&app->gui);
    }
    if(modifier)return;
    af_vec2 mouse=af_engine_mouse_position(engine);
    const char *floating[]={"Performance","Assets","Project","Animation","HDR import"};bool open[]={app->show_performance,app->show_assets,app->show_project,app->show_animation,app->environment_pending};
    for(unsigned i=0;i<5;++i)if(open[i]){struct nk_window *panel=nk_window_find(&app->gui.context,floating[i]);
        if(panel&&nk_input_is_mouse_hovering_rect(&app->gui.context.input,panel->bounds))return;}
    if(!app->runtime.playing&&inside(app->viewport,mouse)&&af_engine_mouse_pressed(engine,AF_MOUSE_LEFT)&&!af_engine_mouse_captured(engine)){
        ed_gui_unfocus(&app->gui);app->fields_active=false;
        int axis=ed_gizmo_hit(&app->gizmo,mouse);
        const af_scene_object *object=af_scene_get_object(app->document.scene,app->document.selection);
        const af_local_light *light=app->light_selected?af_scene_local_light_at(app->document.scene,app->selected_light):NULL;
        if(axis>=0&&light){ed_document_end_edit(&app->document);app->initial_light=*light;app->drag_light=app->selected_light;app->light_drag_revision=app->document.revision;app->light_dragging=true;ed_gizmo_begin(&app->gizmo,axis,0,ed_light_transform(*light),mouse);}
        else if(axis>=0&&object){ed_document_end_edit(&app->document);ed_gizmo_begin(&app->gizmo,axis,object->id,object->transform,mouse);}
        else{int picked=ed_light_pick(app->document.scene,&app->camera,app->viewport,mouse);if(picked>=0){ed_document_end_edit(&app->document);app->selected_light=(size_t)picked;app->light_selected=true;app->document.selection=0;app->gizmo.dragging=false;struct nk_window *inspector_window=nk_window_find(&app->gui.context,"Inspector");if(inspector_window)inspector_window->scrollbar.y=0;}else select_object(app,af_engine_pick_object(engine,mouse));}
    }
    if(inside(app->viewport,mouse)&&af_engine_mouse_pressed(engine,AF_MOUSE_RIGHT)){
        ed_gui_unfocus(&app->gui);app->fields_active=false;
    }
    if(app->gizmo.dragging&&app->light_dragging){
        if(af_engine_key_pressed(engine,AF_KEY_ESCAPE)){if(app->document.revision!=app->light_drag_revision)ed_document_undo(&app->document);app->light_dragging=app->gizmo.dragging=false;ed_document_end_edit(&app->document);}
        else if(af_engine_mouse_down(engine,AF_MOUSE_LEFT)){af_transform next=ed_gizmo_drag(&app->gizmo,mouse);ed_document_local_light(&app->document,app->drag_light,ed_light_apply(app->initial_light,next,app->gizmo.mode,app->gizmo.axis));}
        else{if(SDL_GetWindowFlags(af_engine_sdl_window(engine))&SDL_WINDOW_INPUT_FOCUS){af_transform next=ed_gizmo_drag(&app->gizmo,mouse);ed_document_local_light(&app->document,app->drag_light,ed_light_apply(app->initial_light,next,app->gizmo.mode,app->gizmo.axis));}app->light_dragging=app->gizmo.dragging=false;ed_document_end_edit(&app->document);}return;
    }
    if(app->gizmo.dragging){
        if(af_engine_key_pressed(engine,AF_KEY_ESCAPE)){
            const af_scene_object *object=af_scene_get_object(app->document.scene,app->gizmo.object);
            if(object&&memcmp(&object->transform,&app->gizmo.initial,sizeof(af_transform)))ed_document_undo(&app->document);
            app->gizmo.dragging=false;ed_document_end_edit(&app->document);
        }else if(af_engine_mouse_down(engine,AF_MOUSE_LEFT))ed_document_transform(&app->document,ed_gizmo_drag(&app->gizmo,mouse));
        else{if(SDL_GetWindowFlags(af_engine_sdl_window(engine))&SDL_WINDOW_INPUT_FOCUS)ed_document_transform(&app->document,ed_gizmo_drag(&app->gizmo,mouse));app->gizmo.dragging=false;ed_document_end_edit(&app->document);}
        return;
    }
    if(app->fields_active)return;
    if(!app->runtime.playing&&!af_engine_mouse_captured(engine)){
        if(af_engine_key_pressed(engine,AF_KEY_W))app->gizmo.mode=ED_GIZMO_MOVE;
        if(af_engine_key_pressed(engine,AF_KEY_E))app->gizmo.mode=ED_GIZMO_ROTATE;
        if(af_engine_key_pressed(engine,AF_KEY_R))app->gizmo.mode=ED_GIZMO_SCALE;
    }
    bool navigate=af_engine_mouse_pressed(engine,AF_MOUSE_RIGHT)&&
        (inside(app->viewport,mouse)||(app->runtime.playing&&af_engine_mouse_captured(engine)));
    if(navigate&&!app->free_camera&&!af_engine_key_pressed(engine,AF_KEY_ESCAPE)){
        app->resume_game_capture=app->runtime.playing&&af_engine_mouse_captured(engine);
        app->free_camera=af_engine_set_mouse_capture(engine,true);
    }
    if(app->free_camera&&af_engine_mouse_captured(engine)){
        af_vec2 motion=af_engine_mouse_delta(engine);
        af_camera_rotate(&app->camera,motion.x*0.0025f,-motion.y*0.0025f);
        af_vec3 movement={
            (float)af_engine_key_down(engine,AF_KEY_D)-(float)af_engine_key_down(engine,AF_KEY_A),
            (float)af_engine_key_down(engine,AF_KEY_E)-(float)af_engine_key_down(engine,AF_KEY_Q),
            (float)af_engine_key_down(engine,AF_KEY_W)-(float)af_engine_key_down(engine,AF_KEY_S)};
        af_camera_move(&app->camera,af_vec3_scale(af_vec3_normalize(movement),(shift?9.0f:3.0f)*(float)delta));
    }
}
static nk_flags panel_flags(editor *app)
{
    return NK_WINDOW_BORDER | ((app->modal||app->dialog||app->runtime.playing)?NK_WINDOW_NO_INPUT:0);
}
static void label(struct nk_context *context,const char *text,float height)
{
    nk_layout_row_dynamic(context,height,1);nk_label(context,text,NK_TEXT_LEFT);
}
static bool vector_fields(struct nk_context *context,const char *title,af_vec3 *value,float step,float increment)
{
    label(context,title,24);
    nk_layout_row_dynamic(context,34,3);
    bool changed=nk_property_float(context,"#X",-1000000,&value->x,1000000,step,increment)!=0;
    changed|=nk_property_float(context,"#Y",-1000000,&value->y,1000000,step,increment)!=0;
    changed|=nk_property_float(context,"#Z",-1000000,&value->z,1000000,step,increment)!=0;
    return changed;
}
static void toolbar(editor *app,float width)
{
    struct nk_context *context=&app->gui.context;
    if(nk_begin(context,"Toolbar",nk_rect(0,0,width,64),(NK_WINDOW_BORDER|((app->modal||app->dialog)?NK_WINDOW_NO_INPUT:0))|NK_WINDOW_NO_SCROLLBAR)){
        if(app->runtime.playing){
            nk_layout_row_dynamic(context,34,8);if(nk_button_label(context,app->maximize_play?"Restore (F6)":"Maximize (F6)"))app->maximize_play=!app->maximize_play;
            if(nk_button_label(context,app->runtime.paused?"Resume":"Pause"))app->runtime.paused=!app->runtime.paused;
            if(nk_button_label(context,"Step 1/60")){app->runtime.paused=true;af_engine_set_simulation_paused(app->engine,true);af_engine_step_simulation(app->engine);}
            if(nk_button_label(context,"Stop (F5)"))stop_play(app);
            if(nk_button_label(context,"Reload game"))ed_runtime_reload(&app->runtime);
            nk_bool automatic=app->runtime.auto_reload;nk_checkbox_label(context,"Auto reload",&automatic);app->runtime.auto_reload=automatic!=0;
            if(nk_button_label(context,"Performance"))app->show_performance=!app->show_performance;
            if(nk_button_label(context,"Assets"))app->show_assets=!app->show_assets;
        }else{
        nk_layout_row_begin(context,NK_STATIC,34,11);
        nk_layout_row_push(context,132);nk_label_colored(context,"Afterlight",NK_TEXT_LEFT,nk_rgb(245,190,112));
        nk_layout_row_push(context,64);if(nk_button_label(context,"New"))request_action(app,ED_NEW);
        nk_layout_row_push(context,64);if(nk_button_label(context,"Open"))request_action(app,ED_OPEN);
        nk_layout_row_push(context,64);if(nk_button_label(context,"Save"))save_document(app,false,ED_NONE);
        nk_layout_row_push(context,80);if(nk_button_label(context,"Save As"))save_document(app,true,ED_NONE);
        nk_layout_row_push(context,64);if(nk_button_label(context,"Undo")){ed_document_undo(&app->document);refresh(app);}
        nk_layout_row_push(context,64);if(nk_button_label(context,"Redo")){ed_document_redo(&app->document);refresh(app);}
        nk_layout_row_push(context,80);if(nk_button_label(context,"Play"))toggle_play(app);
        nk_layout_row_push(context,120);if(nk_button_label(context,"Performance"))app->show_performance=!app->show_performance;
        nk_layout_row_push(context,70);if(nk_button_label(context,"Assets"))app->show_assets=!app->show_assets;
        nk_layout_row_push(context,fmaxf(60,width-950));
        char title[512];snprintf(title,sizeof(title),"%s%s",app->document.path[0]?ed_path_name(app->document.path):"Untitled scene",ed_document_dirty(&app->document)?" *":"");
        nk_label(context,title,NK_TEXT_RIGHT);nk_layout_row_end(context);
        }
    }nk_end(context);
}
static void object_rows(editor *app,af_object_id parent,unsigned depth)
{
    if(depth>=AF_SCENE_MAX_DEPTH)return;
    struct nk_context *context=&app->gui.context;
    size_t count=af_scene_object_count(app->document.scene);
    for(size_t i=0;i<count;++i){af_scene_object object=*af_scene_object_at(app->document.scene,i);if(object.parent!=parent)continue;
        float indent=fminf((float)depth*12,100);
        nk_layout_row_begin(context,NK_STATIC,34,3);
        nk_layout_row_push(context,22);nk_bool visible=object.visible;
        if(nk_checkbox_label(context,"",&visible)){af_object_id previous=app->document.selection;app->document.selection=object.id;
            ed_document_visible(&app->document,visible!=0);app->document.selection=previous;}
        nk_layout_row_push(context,indent);nk_spacing(context,1);
        nk_layout_row_push(context,164-indent);
        if(nk_select_label(context,object.name,NK_TEXT_LEFT,app->document.selection==object.id))select_object(app,object.id);
        nk_layout_row_end(context);object_rows(app,object.id,depth+1);
    }
}
static void object_list(editor *app,float height)
{
    struct nk_context *context=&app->gui.context;
    if(nk_begin(context,"Objects",nk_rect(0,64,252,height-96),panel_flags(app)|NK_WINDOW_NO_SCROLLBAR)){
        char title[64];snprintf(title,sizeof(title),"Objects  /  %zu",af_scene_object_count(app->document.scene));label(context,title,28);
        nk_layout_row_dynamic(context,32,2);
        if(nk_button_label(context,"Add cube")){ed_document_add_cube(&app->document);refresh(app);}
        if(nk_button_label(context,"Duplicate")){ed_document_duplicate(&app->document);refresh(app);}
        nk_layout_row_dynamic(context,30,1);if(nk_button_label(context,"Import mesh (OBJ/glTF)"))start_import(app,AF_ASSET_MESH);
        nk_layout_row_dynamic(context,30,1);
        if(nk_button_label(context,"Choose C game module")){app->dialog=ed_dialog_begin_module(app->engine);app->dialog_asset=3;app->dialog_save=false;}
        nk_layout_row_dynamic(context,fmaxf(60,height-338),1);
        if(nk_group_begin(context,"Object list",0)){
            if(!af_scene_object_count(app->document.scene)){nk_layout_row_dynamic(context,50,1);nk_label_wrap(context,"No objects yet. Add a cube to begin.");}
            object_rows(app,AF_OBJECT_NONE,0);
            nk_group_end(context);
        }
        nk_layout_row_dynamic(context,32,1);
        if(nk_button_label(context,"Delete selected")){ed_document_delete(&app->document);refresh(app);}
    }nk_end(context);
}
static void local_light_controls(editor *app)
{
    struct nk_context *ctx=&app->gui.context;label(ctx,"Point and spot lights",30);
    nk_layout_row_dynamic(ctx,30,2);
    if(nk_button_label(ctx,"Add point")){if(ed_document_add_light(&app->document,AF_LIGHT_POINT)){app->selected_light=af_scene_local_light_count(app->document.scene)-1;app->light_selected=true;app->document.selection=0;}}
    if(nk_button_label(ctx,"Add spot")){if(ed_document_add_light(&app->document,AF_LIGHT_SPOT)){app->selected_light=af_scene_local_light_count(app->document.scene)-1;app->light_selected=true;app->document.selection=0;}}
    size_t count=af_scene_local_light_count(app->document.scene);if(!count){label(ctx,"No local lights (maximum 16).",24);return;}
    if(app->selected_light>=count)app->selected_light=count-1;
    const char *names[AF_SCENE_MAX_LOCAL_LIGHTS];for(size_t i=0;i<count;++i)names[i]=af_scene_local_light_at(app->document.scene,i)->name;
    nk_layout_row_dynamic(ctx,30,1);int selected=nk_combo(ctx,names,(int)count,(int)app->selected_light,26,nk_vec2(290,180));
    if((size_t)selected!=app->selected_light){app->light_selected=true;app->document.selection=0;app->selected_light=(size_t)selected;ed_document_end_edit(&app->document);}
    af_local_light local=*af_scene_local_light_at(app->document.scene,app->selected_light);af_local_light before=local;
    label(ctx,"Light name",22);nk_layout_row_dynamic(ctx,30,1);nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,local.name,sizeof(local.name),nk_filter_default);
    nk_layout_row_dynamic(ctx,26,1);nk_bool enabled=local.enabled;nk_checkbox_label(ctx,"Light enabled",&enabled);local.enabled=enabled!=0;
    nk_layout_row_dynamic(ctx,30,1);static const char *types[]={"Point light","Spot light"};local.type=(af_light_type)nk_combo(ctx,types,2,(int)local.type,26,nk_vec2(290,90));
    vector_fields(ctx,"Light position",&local.position,0.1f,0.01f);
    label(ctx,"Light color (linear)",22);nk_layout_row_dynamic(ctx,30,3);
    nk_property_float(ctx,"#R",0,&local.color.x,100,0.1f,0.01f);nk_property_float(ctx,"#G",0,&local.color.y,100,0.1f,0.01f);nk_property_float(ctx,"#B",0,&local.color.z,100,0.1f,0.01f);
    nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Light intensity",0,&local.intensity,10000,1,0.1f);
    nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Light range",0.01f,&local.range,10000,0.5f,0.05f);
    if(local.type==AF_LIGHT_SPOT){
        vector_fields(ctx,"Spot direction",&local.direction,0.1f,0.01f);
        float inner=local.inner_angle*180/AF_PI,outer=local.outer_angle*180/AF_PI;
        nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Inner cone (deg)",0,&inner,89,1,0.1f);
        nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Outer cone (deg)",inner,&outer,89,1,0.1f);
        if(inner!=before.inner_angle*180/AF_PI)local.inner_angle=inner*AF_PI/180;
        if(outer!=before.outer_angle*180/AF_PI)local.outer_angle=outer*AF_PI/180;
    }
    nk_layout_row_dynamic(ctx,26,1);nk_bool cast=local.cast_shadows;nk_checkbox_label(ctx,"Cast local shadows",&cast);local.cast_shadows=cast!=0;
    if(local.cast_shadows){nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Shadow depth bias",0,&local.shadow_bias,.1f,.0001f,.00001f);label(ctx,"First 4 enabled shadow lights share the budget.",24);}
    if(memcmp(&local,&before,sizeof(local)))ed_document_local_light(&app->document,app->selected_light,local);
    nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Select light in viewport")){app->light_selected=true;app->document.selection=0;ed_document_end_edit(&app->document);}
    nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Delete light")){ed_document_remove_light(&app->document,app->selected_light);app->light_selected=false;}
}
static void collider_controls(editor *app,af_scene_object object){
    struct nk_context *ctx=&app->gui.context;label(ctx,"Collider",30);af_collider value=object.collider,before=value;
    static const char *types[]={"No collider","Box","Sphere","Capsule"};nk_layout_row_dynamic(ctx,30,1);
    value.type=(af_collider_type)nk_combo(ctx,types,4,(int)value.type,26,nk_vec2(290,150));
    if(before.type==AF_COLLIDER_NONE&&value.type==AF_COLLIDER_BOX&&!strcmp(object.mesh,"cube"))value.half_extents=(af_vec3){0.65f,0.65f,0.65f};
    if(value.type!=AF_COLLIDER_NONE){nk_layout_row_dynamic(ctx,26,2);nk_bool enabled=value.enabled,trigger=value.trigger;
        nk_checkbox_label(ctx,"Enabled",&enabled);nk_checkbox_label(ctx,"Trigger",&trigger);value.enabled=enabled!=0;value.trigger=trigger!=0;
        vector_fields(ctx,"Collider center",&value.center,0.1f,0.01f);
        if(value.type==AF_COLLIDER_BOX){label(ctx,"Half extents",22);nk_layout_row_dynamic(ctx,30,3);
            nk_property_float(ctx,"#X",0.001f,&value.half_extents.x,10000,0.1f,0.01f);nk_property_float(ctx,"#Y",0.001f,&value.half_extents.y,10000,0.1f,0.01f);nk_property_float(ctx,"#Z",0.001f,&value.half_extents.z,10000,0.1f,0.01f);
        }else{nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Radius",0.001f,&value.radius,10000,0.05f,0.005f);
            if(value.type==AF_COLLIDER_CAPSULE){nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Segment half height",0,&value.half_height,10000,0.1f,0.01f);}}
        nk_layout_row_dynamic(ctx,30,1);int layer=0;uint32_t bit=value.layer;while(bit>1){bit>>=1;++layer;}int next=layer;nk_property_int(ctx,"Layer bit",0,&next,31,1,1);if(next!=layer)value.layer=UINT32_C(1)<<(unsigned)next;
        char mask[96];snprintf(mask,sizeof(mask),"Mask 0x%08" PRIx32 "; selector assigns one bit.",value.layer);label(ctx,mask,24);
        label(ctx,"Collider visibility is independent of mesh visibility.",32);
        af_world_collider world;if(value.enabled&&!af_scene_world_collider(app->document.scene,object.id,&world)){nk_layout_row_dynamic(ctx,42,1);nk_label_wrap(ctx,"Collider skipped: zero scale or sheared world transform.");}
    }
    if(memcmp(&value,&before,sizeof(value)))ed_document_collider(&app->document,value);
}
static void gameplay_controls(editor *app,af_scene_object object)
{
    struct nk_context *ctx=&app->gui.context;af_gameplay_component g=object.gameplay;bool changed=false;
    label(ctx,"Gameplay components",30);nk_layout_row_dynamic(ctx,26,1);nk_bool player=g.player.enabled;
    if(nk_checkbox_label(ctx,"Player controller",&player)){g.player.enabled=player!=0;if(player){g.camera.enabled=true;g.camera.primary=true;}changed=true;}
    if(g.player.enabled){
        if(object.parent||object.transform.scale.x!=1||object.transform.scale.y!=1||object.transform.scale.z!=1)label(ctx,"Player requires root and unit scale.",24);
        nk_layout_row_dynamic(ctx,30,1);
        const af_scene_object *spawn=af_scene_get_object(app->document.scene,g.player.spawn);
        if(nk_combo_begin_label(ctx,spawn?spawn->name:"Spawn at player position",nk_vec2(290,180))){
            nk_layout_row_dynamic(ctx,26,1);if(nk_combo_item_label(ctx,"Spawn at player position",NK_TEXT_LEFT)){g.player.spawn=0;changed=true;}
            for(size_t i=0;i<af_scene_object_count(app->document.scene);++i){const af_scene_object *candidate=af_scene_object_at(app->document.scene,i);
                if(candidate->gameplay.role==AF_ROLE_SPAWN&&nk_combo_item_label(ctx,candidate->name,NK_TEXT_LEFT)){g.player.spawn=candidate->id;changed=true;}}
            nk_combo_end(ctx);
        }
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Walk speed",0,&g.player.controller.speed,100,.25f,.025f)!=0;
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Gravity",.1f,&g.player.controller.gravity,1000,.5f,.05f)!=0;
        float jump=g.player.controller.jump_speed*g.player.controller.jump_speed/(2*fmaxf(.1f,g.player.controller.gravity));
        nk_layout_row_dynamic(ctx,30,1);if(nk_property_float(ctx,"Jump height",0,&jump,20,.1f,.01f)){g.player.controller.jump_speed=sqrtf(2*g.player.controller.gravity*jump);changed=true;}
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Capsule radius",.05f,&g.player.controller.radius,10,.05f,.005f)!=0;
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Capsule half segment",0,&g.player.controller.half_height,10,.05f,.005f)!=0;
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Mouse sensitivity",.0001f,&g.player.mouse_sensitivity,.05f,.0001f,.00001f)!=0;
    }
    nk_layout_row_dynamic(ctx,26,1);nk_bool camera=g.camera.enabled;if(nk_checkbox_label(ctx,"Camera",&camera)){g.camera.enabled=camera!=0;changed=true;}
    if(g.camera.enabled){
        nk_layout_row_dynamic(ctx,26,1);nk_bool primary=g.camera.primary;if(nk_checkbox_label(ctx,"Primary game camera",&primary)){g.camera.primary=primary!=0;changed=true;}
        float fov=g.camera.vertical_fov*180/AF_PI;nk_layout_row_dynamic(ctx,30,1);if(nk_property_float(ctx,"Field of view",10,&fov,150,1,.1f)){g.camera.vertical_fov=fov*AF_PI/180;changed=true;}
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Eye height",-10,&g.camera.eye_height,10,.05f,.005f)!=0;
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Near clip",.001f,&g.camera.near_clip,1,.01f,.001f)!=0;
        nk_layout_row_dynamic(ctx,30,1);changed|=nk_property_float(ctx,"Far clip",1,&g.camera.far_clip,100000,10,1)!=0;
    }
    static const char *const roles[]={"No course role","Spawn","Checkpoint","Goal trigger","Restart trigger","Moving platform"};
    label(ctx,"Course role",24);nk_layout_row_dynamic(ctx,30,1);int role=(int)g.role;int next=nk_combo(ctx,roles,AF_ROLE_COUNT,role,26,nk_vec2(290,190));
    if(next!=role){g.role=(af_gameplay_role)next;changed=true;}
    if(g.role==AF_ROLE_CHECKPOINT){int order=(int)g.checkpoint_order;nk_layout_row_dynamic(ctx,30,1);if(nk_property_int(ctx,"Checkpoint order",1,&order,1024,1,1)){g.checkpoint_order=(unsigned)order;changed=true;}}
    if(changed)ed_document_gameplay(&app->document,g);
}
static void thumbnail_widget(editor *app,const af_scene_asset *asset,const af_material *material){
    struct nk_context *ctx=&app->gui.context;struct nk_rect bounds;
    enum nk_widget_layout_states visibility=nk_widget(&bounds,ctx);if(visibility==NK_WIDGET_INVALID)return;
    float side=fminf(bounds.w,bounds.h);bounds.x+=(bounds.w-side)*.5f;bounds.w=side;bounds.h=side;
    uint32_t texture=ed_thumbnails_get(&app->thumbnails,app->engine,app->document.scene,asset,material);
    struct nk_command_buffer *canvas=nk_window_get_canvas(ctx);
    if(texture){struct nk_image image=nk_image_id((int)texture);nk_draw_image(canvas,bounds,&image,nk_rgba(255,255,255,255));}
    else nk_fill_rect(canvas,bounds,4,nk_rgb(48,45,57));
    nk_stroke_rect(canvas,bounds,3,1,nk_rgb(82,75,91));
    if(asset&&visibility==NK_WIDGET_VALID&&nk_input_is_mouse_hovering_rect(&ctx->input,bounds)&&nk_input_is_mouse_pressed(&ctx->input,NK_BUTTON_LEFT))snprintf(app->selected_asset,sizeof(app->selected_asset),"%s",asset->name);
}
static void asset_entry(editor *app,const af_scene_asset *asset){
    struct nk_context *ctx=&app->gui.context;char title[128];
    snprintf(title,sizeof(title),"%s  /  %s",asset->name,asset->kind==AF_ASSET_ANIMATION?"animation":asset->kind==AF_ASSET_ENVIRONMENT?"HDR environment":asset->kind==AF_ASSET_METALLIC_ROUGHNESS?"metal/rough":asset->kind==AF_ASSET_AUDIO?"audio":asset->kind==AF_ASSET_MESH?"mesh":asset->kind==AF_ASSET_NORMAL_MAP?"normal map":"color texture");
    nk_layout_row_begin(ctx,NK_DYNAMIC,64,2);nk_layout_row_push(ctx,.17f);thumbnail_widget(app,asset,NULL);
    nk_layout_row_push(ctx,.83f);if(nk_select_label(ctx,title,NK_TEXT_LEFT,!strcmp(app->selected_asset,asset->name)))snprintf(app->selected_asset,sizeof(app->selected_asset),"%s",asset->name);nk_layout_row_end(ctx);
}
static void inspector(editor *app,float width,float height)
{
    struct nk_context *context=&app->gui.context;
    if(nk_begin(context,"Inspector",nk_rect(width-334,64,334,height-96),panel_flags(app))){
        label(context,"Inspector",28);
        if(app->light_selected){local_light_controls(app);label(context,"W move / E rotate / R range / F focus",26);nk_end(context);return;}
        const af_scene_object *found=af_scene_get_object(app->document.scene,app->document.selection);
        if(!found){label(context,"Select an object to edit it.",36);app->invalid_name=false;}
        else{
            af_scene_object object=*found;
            if(app->refresh_fields){snprintf(app->name,sizeof(app->name),"%s",object.name);snprintf(app->mesh,sizeof(app->mesh),"%s",object.mesh);snprintf(app->texture,sizeof(app->texture),"%s",object.material.texture);snprintf(app->normal_map,sizeof(app->normal_map),"%s",object.material.normal_map);app->refresh_fields=false;}
            char id[64];snprintf(id,sizeof(id),"Object ID  %" PRIu64,object.id);label(context,id,22);
            if(object.animation.asset[0]||object.model.root){nk_layout_row_dynamic(context,30,1);if(nk_button_label(context,"Animation clips")){app->show_animation=true;app->animation_target=object.animation.asset[0]?object.id:object.model.root;}}
            label(context,"Name",23);nk_layout_row_dynamic(context,34,1);
            nk_edit_string_zero_terminated(context,(nk_flags)NK_EDIT_FIELD|(nk_flags)NK_EDIT_SIG_ENTER,app->name,sizeof(app->name),nk_filter_default);
            if(app->name[0]&&strcmp(app->name,object.name))ed_document_name(&app->document,app->name);
            label(context,"Mesh asset",23);nk_layout_row_dynamic(context,34,1);
            nk_edit_string_zero_terminated(context,(nk_flags)NK_EDIT_FIELD|(nk_flags)NK_EDIT_SIG_ENTER,app->mesh,sizeof(app->mesh),nk_filter_default);
            if(app->mesh[0]&&strcmp(app->mesh,object.mesh))ed_document_mesh(&app->document,app->mesh);
            app->invalid_name=!app->name[0]||!app->mesh[0];
            if(app->invalid_name)label(context,"Name and mesh name cannot be empty.",24);
            if(strcmp(app->mesh,"cube")&&strcmp(app->mesh,"__node")&&!af_scene_find_asset(app->document.scene,app->mesh))label(context,"Unresolved mesh reference.",24);
            nk_layout_row_dynamic(context,28,1);nk_bool visible=object.visible;
            if(nk_checkbox_label(context,"Visible",&visible))ed_document_visible(&app->document,visible!=0);
            af_transform transform=object.transform;
            bool changed=vector_fields(context,"Position",&transform.position,0.1f,0.01f);
            af_vec3 degrees=af_vec3_scale(transform.rotation,180.0f/AF_PI);
            if(vector_fields(context,"Rotation (degrees)",&degrees,1,0.2f)){transform.rotation=af_vec3_scale(degrees,AF_PI/180);changed=true;}
            changed|=vector_fields(context,"Scale",&transform.scale,0.1f,0.01f);
            if(changed)ed_document_transform(&app->document,transform);
            nk_layout_row_dynamic(context,32,2);
            if(nk_button_label(context,"Focus"))focus_selection(app);
            if(nk_button_label(context,"Reset transform")){ed_document_transform(&app->document,af_transform_identity());ed_document_end_edit(&app->document);}
            label(context,"Parent (keeps local transform)",26);
            const af_scene_object *parent=af_scene_get_object(app->document.scene,object.parent);
            nk_layout_row_dynamic(context,30,1);
            if(nk_combo_begin_label(context,parent?parent->name:"Scene root",nk_vec2(290,240))){
                nk_layout_row_dynamic(context,28,1);
                if(nk_combo_item_label(context,"Scene root",NK_TEXT_LEFT))ed_document_parent(&app->document,AF_OBJECT_NONE);
                for(size_t i=0;i<af_scene_object_count(app->document.scene);++i){af_scene_object candidate=*af_scene_object_at(app->document.scene,i);
                    if(candidate.id==object.id||af_scene_is_descendant(app->document.scene,candidate.id,object.id))continue;
                    char text[96];snprintf(text,sizeof(text),"%s (#%" PRIu64 ")",candidate.name,candidate.id);
                    if(nk_combo_item_label(context,text,NK_TEXT_LEFT))ed_document_parent(&app->document,candidate.id);
                }nk_combo_end(context);
            }
            collider_controls(app,object);
            gameplay_controls(app,object);
            label(context,"Material",30);af_material material=object.material;
            nk_layout_row_dynamic(context,80,1);thumbnail_widget(app,NULL,&material);
            label(context,"Metallic/roughness texture asset",22);nk_layout_row_dynamic(context,30,1);
            char surface[AF_MESH_NAME_CAPACITY];snprintf(surface,sizeof(surface),"%s",material.metallic_roughness_map);
            nk_edit_string_zero_terminated(context,NK_EDIT_FIELD,surface,sizeof(surface),nk_filter_default);
            if(strcmp(surface,material.metallic_roughness_map)){snprintf(material.metallic_roughness_map,sizeof(material.metallic_roughness_map),"%s",surface);ed_document_material(&app->document,material);}
            nk_layout_row_dynamic(context,30,3);
            bool material_changed=nk_property_float(context,"#R",0,&material.base_color.r,1,0.05f,0.005f)!=0;
            material_changed|=nk_property_float(context,"#G",0,&material.base_color.g,1,0.05f,0.005f)!=0;
            material_changed|=nk_property_float(context,"#B",0,&material.base_color.b,1,0.05f,0.005f)!=0;
            nk_layout_row_dynamic(context,26,1);nk_bool pbr=material.physically_based;
            if(nk_checkbox_label(context,"Physically based",&pbr)){material.physically_based=pbr!=0;material_changed=true;}
            nk_layout_row_dynamic(context,30,1);material_changed|=nk_property_float(context,"Roughness",0,&material.roughness,1,0.05f,0.005f)!=0;
            nk_layout_row_dynamic(context,30,1);material_changed|=nk_property_float(context,"Metallic",0,&material.metallic,1,0.05f,0.005f)!=0;
            label(context,"Emission (linear RGB)",22);nk_layout_row_dynamic(context,30,3);
            material_changed|=nk_property_float(context,"#R",0,&material.emission.x,1,0.05f,0.005f)!=0;
            material_changed|=nk_property_float(context,"#G",0,&material.emission.y,1,0.05f,0.005f)!=0;
            material_changed|=nk_property_float(context,"#B",0,&material.emission.z,1,0.05f,0.005f)!=0;
            nk_layout_row_dynamic(context,30,1);material_changed|=nk_property_float(context,"Emission strength",0,&material.emission_strength,1000,0.1f,0.01f)!=0;
            nk_layout_row_dynamic(context,26,1);nk_bool unlit=material.unlit;
            if(nk_checkbox_label(context,"Unlit",&unlit)){material.unlit=unlit!=0;material_changed=true;}
            nk_layout_row_dynamic(context,26,1);nk_bool casts=material.cast_shadows,receives=material.receive_shadows;
            if(nk_checkbox_label(context,"Cast shadows",&casts)){material.cast_shadows=casts!=0;material_changed=true;}
            nk_layout_row_dynamic(context,26,1);
            if(nk_checkbox_label(context,"Receive shadows",&receives)){material.receive_shadows=receives!=0;material_changed=true;}
            label(context,"Texture asset (empty = white)",22);nk_layout_row_dynamic(context,30,1);
            nk_edit_string_zero_terminated(context,NK_EDIT_FIELD,app->texture,sizeof(app->texture),nk_filter_default);
            if(strcmp(material.texture,app->texture)){snprintf(material.texture,sizeof(material.texture),"%s",app->texture);material_changed=true;}
            label(context,"Normal map (linear)",22);nk_layout_row_dynamic(context,30,1);
            nk_edit_string_zero_terminated(context,NK_EDIT_FIELD,app->normal_map,sizeof(app->normal_map),nk_filter_default);
            if(strcmp(material.normal_map,app->normal_map)){snprintf(material.normal_map,sizeof(material.normal_map),"%s",app->normal_map);material_changed=true;}
            nk_layout_row_dynamic(context,30,1);material_changed|=nk_property_float(context,"Normal strength",0,&material.normal_strength,4,0.1f,0.01f)!=0;
            if(material_changed)ed_document_material(&app->document,material);
            nk_layout_row_dynamic(context,30,1);if(nk_button_label(context,"Import texture"))start_import(app,AF_ASSET_TEXTURE);
            nk_layout_row_dynamic(context,30,1);if(nk_button_label(context,"Import normal map"))start_import(app,AF_ASSET_NORMAL_MAP);
        }
        label(context,"Directional light",30);af_directional_light light=af_scene_get_light(app->document.scene);
        bool light_changed=vector_fields(context,"Direction",&light.direction,0.1f,0.01f);
        nk_layout_row_dynamic(context,30,1);
        light_changed|=nk_property_float(context,"Ambient",0,&light.ambient,10,0.05f,0.005f)!=0;
        nk_layout_row_dynamic(context,30,1);
        light_changed|=nk_property_float(context,"Intensity",0,&light.intensity,100,0.1f,0.01f)!=0;
        if(light_changed)ed_document_light(&app->document,light);
        local_light_controls(app);
        label(context,"Exposure and tone mapping",30);af_render_settings render=af_scene_get_render_settings(app->document.scene);
        nk_layout_row_dynamic(context,30,1);bool render_changed=nk_property_float(context,"Exposure (stops)",-10,&render.exposure,10,0.25f,0.025f)!=0;
        nk_layout_row_dynamic(context,26,1);nk_bool tone=render.tone_mapping;
        if(nk_checkbox_label(context,"Tone mapping",&tone)){render.tone_mapping=tone!=0;render_changed=true;}
        label(context,"Environment lighting",30);nk_layout_row_dynamic(context,26,1);nk_bool environment=render.environment_enabled;
        if(nk_checkbox_label(context,"Image-based lighting",&environment)){render.environment_enabled=environment!=0;render_changed=true;}
        nk_layout_row_dynamic(context,30,1);if(nk_button_label(context,"Import HDR environment"))start_import(app,AF_ASSET_ENVIRONMENT);
        label(context,"Environment source",22);nk_layout_row_dynamic(context,30,1);
        if(nk_combo_begin_label(context,render.environment_asset[0]?render.environment_asset:"Procedural profile",nk_vec2(290,180))){
            nk_layout_row_dynamic(context,26,1);if(nk_combo_item_label(context,"Procedural profile",NK_TEXT_LEFT)){render.environment_asset[0]=0;render_changed=true;}
            for(size_t i=0;i<af_scene_asset_count(app->document.scene);++i){const af_scene_asset *asset=af_scene_asset_at(app->document.scene,i);if(asset->kind==AF_ASSET_ENVIRONMENT&&nk_combo_item_label(context,asset->name,NK_TEXT_LEFT)){snprintf(render.environment_asset,sizeof(render.environment_asset),"%s",asset->name);render_changed=true;}}nk_combo_end(context);
        }
        nk_layout_row_dynamic(context,26,1);nk_bool sky=render.skybox;if(nk_checkbox_label(context,"Show environment skybox",&sky)){render.skybox=sky!=0;render_changed=true;}
        if(render.skybox){nk_layout_row_dynamic(context,30,1);render_changed|=nk_property_float(context,"Background brightness",0,&render.skybox_intensity,100,.1f,.01f)!=0;}
        if(render.environment_enabled||render.skybox){
            static const char *profiles[]={"Studio softboxes","Sunset sky","Overcast sky"};nk_layout_row_dynamic(context,30,1);
            int profile=nk_combo(context,profiles,3,(int)render.environment_profile,26,nk_vec2(290,120));if((uint32_t)profile!=render.environment_profile){render.environment_profile=(uint32_t)profile;render_changed=true;}
            nk_layout_row_dynamic(context,30,1);render_changed|=nk_property_float(context,"Environment intensity",0,&render.environment_intensity,100,.1f,.01f)!=0;
            float degrees=render.environment_rotation*180/AF_PI;nk_layout_row_dynamic(context,30,1);
            if(nk_property_float(context,"Environment rotation",-360,&degrees,360,5,.5f)){render.environment_rotation=degrees*AF_PI/180;render_changed=true;}
        }
        label(context,"Bloom",30);nk_layout_row_dynamic(context,26,1);nk_bool bloom=render.bloom;
        if(nk_checkbox_label(context,"Bloom enabled",&bloom)){render.bloom=bloom!=0;render_changed=true;}
        if(render.bloom){
            nk_layout_row_dynamic(context,30,1);render_changed|=nk_property_float(context,"Bloom strength",0,&render.bloom_strength,5,.05f,.005f)!=0;
            nk_layout_row_dynamic(context,30,1);render_changed|=nk_property_float(context,"Bloom threshold",0,&render.bloom_threshold,100,.1f,.01f)!=0;
            nk_layout_row_dynamic(context,30,1);render_changed|=nk_property_float(context,"Bloom radius",0,&render.bloom_radius,4,.1f,.01f)!=0;
        }
        label(context,"Local shadow quality",28);static const char *local_resolutions[]={"256 px / face","512 px / face","1024 px / face"};
        int local_quality=render.local_shadow_resolution==256?0:render.local_shadow_resolution==1024?2:1;
        nk_layout_row_dynamic(context,30,1);int next_quality=nk_combo(context,local_resolutions,3,local_quality,26,nk_vec2(290,120));
        if(next_quality!=local_quality){render.local_shadow_resolution=256u<<(unsigned)next_quality;render_changed=true;}
        if(render_changed)ed_document_render_settings(&app->document,render);
        label(context,"Shadows",30);af_shadow_settings shadows=af_scene_get_shadows(app->document.scene);
        nk_layout_row_dynamic(context,26,1);nk_bool enabled=shadows.enabled;bool shadow_changed=nk_checkbox_label(context,"Enabled",&enabled)!=0;shadows.enabled=enabled!=0;
        nk_layout_row_dynamic(context,30,1);static const char *modes[]={"Single map (simple)","4 cascades"};
        int mode=shadows.mode==AF_SHADOW_CASCADED?1:0,next_mode=nk_combo(context,modes,2,mode,28,nk_vec2(290,90));
        if(next_mode!=mode){shadows.mode=next_mode?AF_SHADOW_CASCADED:AF_SHADOW_SINGLE;shadow_changed=true;}
        label(context,"Resolution per map",22);
        nk_layout_row_dynamic(context,30,1);static const char *sizes[]={"512","1024","2048","4096"};int size=shadows.resolution==512?0:shadows.resolution==1024?1:shadows.resolution==2048?2:3;
        int next_size=nk_combo(context,sizes,4,size,28,nk_vec2(290,150));if(next_size!=size){shadows.resolution=512u<<(unsigned)next_size;shadow_changed=true;}
        nk_layout_row_dynamic(context,30,1);shadow_changed|=nk_property_float(context,"Shadow distance",1,&shadows.distance,1000,1,0.2f)!=0;
        nk_layout_row_dynamic(context,30,1);shadow_changed|=nk_property_float(context,"Constant bias",0,&shadows.constant_bias,16,0.1f,0.01f)!=0;
        nk_layout_row_dynamic(context,30,1);shadow_changed|=nk_property_float(context,"Slope bias",0,&shadows.slope_bias,16,0.1f,0.01f)!=0;
        nk_layout_row_dynamic(context,30,1);shadow_changed|=nk_property_float(context,"Normal offset",0,&shadows.normal_bias,1,0.005f,0.001f)!=0;
        nk_layout_row_dynamic(context,30,1);static const char *filters[]={"Hard edges","Soft edges","Softer edges"};int filter=nk_combo(context,filters,3,(int)shadows.filter_radius,28,nk_vec2(290,120));
        if(filter!=(int)shadows.filter_radius){shadows.filter_radius=(uint32_t)filter;shadow_changed=true;}
        if(shadows.mode==AF_SHADOW_CASCADED){
            nk_layout_row_dynamic(context,30,1);shadow_changed|=nk_property_float(context,"Near detail",0,&shadows.split_lambda,1,0.05f,0.005f)!=0;
            nk_layout_row_dynamic(context,30,1);shadow_changed|=nk_property_float(context,"Cascade blend",0,&shadows.blend_fraction,0.3f,0.01f,0.001f)!=0;
            nk_layout_row_dynamic(context,26,1);nk_bool debug=shadows.debug_cascades;
            if(nk_checkbox_label(context,"Show cascade regions",&debug)){shadows.debug_cascades=debug!=0;shadow_changed=true;}
        }
        if(shadow_changed)ed_document_shadows(&app->document,shadows);
    }nk_end(context);
}
static void draw_colliders(editor *app){
    if(!app->show_colliders)return;struct nk_context *ctx=&app->gui.context;af_rect r=app->viewport;
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,nk_style_item_color(nk_rgba(0,0,0,0)));
    if(nk_begin(ctx,"Collision shapes",nk_rect(r.x,r.y,r.width,r.height),NK_WINDOW_NO_INPUT|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BACKGROUND)){
        struct nk_command_buffer *canvas=nk_window_get_canvas(ctx);const af_scene *scene=app->runtime.playing?app->runtime.scene:app->document.scene;
        for(size_t i=0;i<af_scene_object_count(scene);++i){af_world_collider collider;if(!af_scene_world_collider(scene,af_scene_object_at(scene,i)->id,&collider))continue;
            ed_line3 lines[ED_COLLIDER_MAX_LINES];size_t count=ed_collider_lines(collider,lines,ED_COLLIDER_MAX_LINES);struct nk_color color=collider.object==app->document.selection?nk_rgb(255,211,115):collider.trigger?nk_rgb(195,135,255):nk_rgb(92,225,204);
            for(size_t j=0;j<count&&j<ED_COLLIDER_MAX_LINES;++j){af_vec2 a,b;if(ed_project(&app->camera,r,lines[j].a,&a)&&ed_project(&app->camera,r,lines[j].b,&b))nk_stroke_line(canvas,a.x,a.y,b.x,b.y,1.5f,color);}
        }
    }nk_end(ctx);nk_style_pop_style_item(ctx);
}
static void draw_skeleton(editor *app){
    if(!app->show_skeleton||!app->animation_target)return;struct nk_context *ctx=&app->gui.context;af_rect r=app->viewport;af_camera camera=app->runtime.playing?af_engine_get_camera(app->engine):app->camera;
    af_animation_bone bones[256];size_t count=af_engine_animation_bones(app->engine,app->animation_target,bones,256);
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,nk_style_item_color(nk_rgba(0,0,0,0)));
    if(nk_begin(ctx,"Posed skeleton",nk_rect(r.x,r.y,r.width,r.height),NK_WINDOW_NO_INPUT|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BACKGROUND)){
        struct nk_command_buffer *canvas=nk_window_get_canvas(ctx);for(size_t i=0;i<count&&i<256;++i){af_vec2 a,b;if(!ed_project(&camera,r,bones[i].position,&a))continue;
            if(bones[i].has_parent&&ed_project(&camera,r,bones[i].parent_position,&b))nk_stroke_line(canvas,a.x,a.y,b.x,b.y,2,nk_rgb(255,194,84));nk_fill_circle(canvas,nk_rect(a.x-3,a.y-3,6,6),nk_rgb(130,240,235));}
    }nk_end(ctx);nk_style_pop_style_item(ctx);
}
static void draw_lights(editor *app){
    if(app->runtime.playing)return;struct nk_context *ctx=&app->gui.context;af_rect r=app->viewport;
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,nk_style_item_color(nk_rgba(0,0,0,0)));
    if(nk_begin(ctx,"Light shapes",nk_rect(r.x,r.y,r.width,r.height),NK_WINDOW_NO_INPUT|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BACKGROUND)){
        struct nk_command_buffer *canvas=nk_window_get_canvas(ctx);
        for(size_t i=0;i<af_scene_local_light_count(app->document.scene);++i){const af_local_light *light=af_scene_local_light_at(app->document.scene,i);af_vec2 screen;
            struct nk_color color=!light->enabled?nk_rgb(120,120,130):app->light_selected&&i==app->selected_light?nk_rgb(255,218,120):nk_rgb(240,170,75);
            if(ed_project(&app->camera,r,light->position,&screen)){nk_stroke_circle(canvas,nk_rect(screen.x-7,screen.y-7,14,14),2,color);nk_stroke_line(canvas,screen.x-11,screen.y,screen.x+11,screen.y,1,color);nk_stroke_line(canvas,screen.x,screen.y-11,screen.x,screen.y+11,1,color);}
            if(app->light_selected&&i==app->selected_light){ed_line3 lines[ED_LIGHT_MAX_LINES];size_t count=ed_light_lines(*light,lines,ED_LIGHT_MAX_LINES);
                for(size_t j=0;j<count&&j<ED_LIGHT_MAX_LINES;++j){af_vec2 a,b;if(ed_project(&app->camera,r,lines[j].a,&a)&&ed_project(&app->camera,r,lines[j].b,&b))nk_stroke_line(canvas,a.x,a.y,b.x,b.y,1,color);}}
        }
    }nk_end(ctx);nk_style_pop_style_item(ctx);
}
static void draw_gizmo(editor *app)
{
    ed_gizmo *g=&app->gizmo;if(!g->projected||app->runtime.playing)return;
    struct nk_context *ctx=&app->gui.context;
    nk_style_push_style_item(ctx,&ctx->style.window.fixed_background,nk_style_item_color(nk_rgba(0,0,0,0)));
    af_rect r=app->viewport;
    if(nk_begin(ctx,"Gizmos",nk_rect(r.x,r.y,r.width,r.height),NK_WINDOW_NO_INPUT|NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_BACKGROUND)){
        struct nk_command_buffer *canvas=nk_window_get_canvas(ctx);
        struct nk_color colors[]={nk_rgb(240,95,91),nk_rgb(107,211,138),nk_rgb(104,161,255)};
        for(int a=0;a<3;++a){struct nk_color c=g->dragging&&a==g->axis?nk_rgb(255,220,125):colors[a];
            if(g->mode==ED_GIZMO_ROTATE){for(int i=1;i<=ED_GIZMO_SEGMENTS;++i)nk_stroke_line(canvas,g->rings[a][i-1].x,g->rings[a][i-1].y,g->rings[a][i].x,g->rings[a][i].y,2,c);}
            else{nk_stroke_line(canvas,g->origin.x,g->origin.y,g->ends[a].x,g->ends[a].y,3,c);
                struct nk_rect end=nk_rect(g->ends[a].x-5,g->ends[a].y-5,10,10);
                if(g->mode==ED_GIZMO_SCALE)nk_fill_rect(canvas,end,1,c);else nk_fill_circle(canvas,end,c);}
        }
        nk_fill_circle(canvas,nk_rect(g->origin.x-4,g->origin.y-4,8,8),nk_rgb(245,225,180));
    }nk_end(ctx);nk_style_pop_style_item(ctx);
}
static void performance_panel(editor *app,float width,float height)
{
    if(!app->show_performance)return;
    struct nk_context *ctx=&app->gui.context;af_engine_stats stats=af_engine_get_stats(app->engine);char text[256];
    if(nk_begin(ctx,"Performance",nk_rect(fmaxf(20,width-760),110,420,fminf(540,height-160)),NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE)){
        snprintf(text,sizeof(text),"%.1f FPS / %.2f ms (paced)",stats.frames_per_second,stats.average_frame_ms);label(ctx,text,26);
        snprintf(text,sizeof(text),"CPU events %.3f ms",stats.cpu_events_ms);label(ctx,text,24);
        snprintf(text,sizeof(text),"CPU update %.3f ms",stats.cpu_update_ms);label(ctx,text,24);
        snprintf(text,sizeof(text),"CPU animation %.3f ms",stats.cpu_animation_ms);label(ctx,text,24);
        snprintf(text,sizeof(text),"CPU submit/wait %.3f ms",stats.cpu_render_ms);label(ctx,text,24);
        snprintf(text,sizeof(text),"Shadow passes %u / reused %u",stats.shadow_passes,stats.shadow_cached_passes);label(ctx,text,24);
        if(stats.gpu_timing_valid){
            snprintf(text,sizeof(text),"GPU frame %.3f ms",stats.gpu_frame_ms);label(ctx,text,28);
            snprintf(text,sizeof(text),"Shadows %.3f / Scene %.3f ms",stats.gpu_shadow_ms,stats.gpu_scene_ms);label(ctx,text,24);
            snprintf(text,sizeof(text),"Post %.3f / UI %.3f ms",stats.gpu_post_ms,stats.gpu_overlay_ms);label(ctx,text,24);
            snprintf(text,sizeof(text),"CPU fence %.3f / acquire %.3f ms",stats.cpu_fence_ms,stats.cpu_acquire_ms);label(ctx,text,24);
        }else label(ctx,stats.gpu_timing_supported?"GPU sample pending":"GPU timestamps unavailable",26);
        snprintf(text,sizeof(text),"Simulation %" PRIu64 " ticks / %u steps / %.3f alpha",stats.simulation_ticks,stats.simulation_steps,stats.simulation_alpha);label(ctx,text,24);
        snprintf(text,sizeof(text),"Dropped catch-up %.3f s",stats.simulation_dropped_seconds);label(ctx,text,24);
        snprintf(text,sizeof(text),"Draws %zu + %zu shadow",stats.draw_calls,stats.shadow_draw_calls);label(ctx,text,26);
        snprintf(text,sizeof(text),"Textures %zu / %.2f MiB / %u mip levels",stats.loaded_textures,(double)stats.texture_bytes/1048576,stats.texture_mip_levels);label(ctx,text,24);
        snprintf(text,sizeof(text),"Anisotropy %.0fx / Shadow %u x %u",stats.texture_anisotropy,stats.shadow_resolution,stats.shadow_cascades);label(ctx,text,24);
        snprintf(text,sizeof(text),"Missing meshes %zu / textures %zu",stats.missing_meshes,stats.missing_textures);label(ctx,text,24);
        snprintf(text,sizeof(text),"HDR: %s / %u local lights / %u shadow faces",stats.hdr_enabled?"RGBA16F":"unavailable",stats.local_shadow_lights,stats.local_shadow_maps);label(ctx,text,24);
        if(stats.local_shadow_overflow){snprintf(text,sizeof(text),"%u extra local lights render without shadows",stats.local_shadow_overflow);label(ctx,text,24);}
        label(ctx,"GPU samples are delayed; presentation is excluded.",24);
        nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Close performance"))app->show_performance=false;
    }nk_end(ctx);
}
static void asset_browser(editor *app,float width,float height)
{
    if(!app->show_assets)return;struct nk_context *ctx=&app->gui.context;
    nk_flags flags=NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_MOVABLE|NK_WINDOW_SCALABLE;
    if(app->runtime.playing||app->modal||app->dialog)flags|=NK_WINDOW_ROM;
    if(nk_begin(ctx,"Assets",nk_rect(270,120,fminf(620,width-310),fminf(650,height-170)),flags)){
        nk_layout_row_dynamic(ctx,26,1);nk_bool watch=app->watch_assets;nk_checkbox_label(ctx,"Reload changed assets",&watch);app->watch_assets=watch!=0;
        label(ctx,"Search names or paths",22);nk_layout_row_dynamic(ctx,30,1);
        nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,app->asset_search,sizeof(app->asset_search),nk_filter_default);
        static const char *filters[]={"All assets","Meshes","Color textures","Normal maps","Audio","Metal/rough maps","HDR environments","Animations"};
        nk_layout_row_dynamic(ctx,30,1);app->asset_filter=nk_combo(ctx,filters,8,app->asset_filter,26,nk_vec2(220,150));
        nk_layout_row_dynamic(ctx,30,3);
        if(nk_button_label(ctx,"Import mesh"))start_import(app,AF_ASSET_MESH);
        if(nk_button_label(ctx,"Import texture"))start_import(app,AF_ASSET_TEXTURE);
        if(nk_button_label(ctx,"Import normal"))start_import(app,AF_ASSET_NORMAL_MAP);
        nk_layout_row_dynamic(ctx,28,2);if(nk_button_label(ctx,"Import WAV sound"))start_import(app,AF_ASSET_AUDIO);
        if(nk_button_label(ctx,"Import metal/rough"))start_import(app,AF_ASSET_METALLIC_ROUGHNESS);
        nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Import HDR panorama"))start_import(app,AF_ASSET_ENVIRONMENT);
        size_t matches=0;nk_layout_row_dynamic(ctx,240,1);
        if(nk_group_begin(ctx,"Asset results",NK_WINDOW_BORDER)){
            af_scene_asset builtin={.name="cube",.path="Built-in mesh",.kind=AF_ASSET_MESH};
            if(!af_scene_find_asset(app->document.scene,"cube")&&ed_asset_matches(&builtin,app->asset_search,app->asset_filter-1)){
                asset_entry(app,&builtin);++matches;}
            for(size_t i=0;i<af_scene_asset_count(app->document.scene);++i){const af_scene_asset *asset=af_scene_asset_at(app->document.scene,i);
                if(!ed_asset_matches(asset,app->asset_search,app->asset_filter-1))continue;++matches;
                asset_entry(app,asset);
            }
            if(!matches)label(ctx,"No matching assets.",28);nk_group_end(ctx);
        }
        char count[80];snprintf(count,sizeof(count),"%zu matches / %zu imported assets",matches,af_scene_asset_count(app->document.scene));label(ctx,count,24);
        const af_scene_asset *selected=af_scene_find_asset(app->document.scene,app->selected_asset);
        bool builtin=!strcmp(app->selected_asset,"cube")&&!selected;
        if(selected||builtin){af_scene_asset asset=selected?*selected:(af_scene_asset){.name="cube",.path="Built-in mesh",.kind=AF_ASSET_MESH};
            nk_layout_row_dynamic(ctx,46,1);nk_label_wrap(ctx,asset.path);
            af_texture_info info;if(af_engine_texture_info(app->engine,asset.name,&info)){char text[160];snprintf(text,sizeof(text),"%u x %u / %u mips / %.2f MiB / %.0fx / %s",info.width,info.height,info.mip_levels,(double)info.allocation_bytes/1048576,info.anisotropy,info.srgb?"sRGB":"linear");label(ctx,text,24);}
            if(asset.kind==AF_ASSET_AUDIO){nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Preview sound"))af_engine_play_sound(app->engine,asset.name,.5f);}
            else if(asset.kind==AF_ASSET_ANIMATION){label(ctx,"Clip data for the matching imported model.",26);}
            else if(asset.kind==AF_ASSET_ENVIRONMENT){nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Use environment lighting"))ed_document_assign_asset(&app->document,asset.name);}
            else if(af_scene_get_object(app->document.scene,app->document.selection)){nk_layout_row_dynamic(ctx,30,1);
                if(nk_button_label(ctx,"Assign to selected object")){if(ed_document_assign_asset(&app->document,asset.name))refresh(app);}}
            else label(ctx,"Select an object to assign this asset.",24);
            if(asset.kind==AF_ASSET_MESH){nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Create mesh object")){ed_document_create_mesh_object(&app->document,asset.name);refresh(app);}}
            if(!builtin){nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Reload asset from disk")){af_asset_error error;
                char source[AF_ASSET_PATH_CAPACITY];size_t n=strcspn(asset.path,"#");memcpy(source,asset.path,n);source[n]=0;
                bool loaded=af_engine_reload_asset_source(app->engine,app->document.scene,source,&error);
                if(loaded)ed_thumbnails_invalidate(&app->thumbnails);
                set_status(app,loaded?"Asset reloaded.":error.message);}}
        }
        nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Close assets"))app->show_assets=false;
    }nk_end(ctx);
}
static void poll_project_tool(editor *app)
{
    if(!app->tool_process)return;int result;
    if(!SDL_WaitProcess(app->tool_process,false,&result))return;
    size_t size=0;void *output=SDL_ReadProcess(app->tool_process,&size,&result);
    if(output){if(size)fprintf(stdout,"Project tool: %.*s\n",(int)(size>4096?4096:size),(const char*)output);SDL_free(output);}
    SDL_DestroyProcess(app->tool_process);app->tool_process=NULL;
    app->tool_success=result==0;
    if(result){set_status(app,"Project tool failed. Check the application log for details.");return;}
    if(app->tool_action==1){set_status(app,"Project created. Configure/build its CMake project, then open it to Play.");}
    else set_status(app,"Export completed.");
}
static void start_project_tool(editor *app,bool export)
{
    if(app->tool_process||app->runtime.playing||app->dialog||app->modal)return;
    if(!app->wizard_folder[0]){set_status(app,"Enter a new destination directory.");return;}
    if(!export&&!app->wizard_name[0]){set_status(app,"Enter a project name.");return;}
    if(export&&(!app->project_path[0]||app->project_is_template||ed_document_dirty(&app->document))){set_status(app,"Save the scene and project before exporting.");return;}
    char tool[AF_ASSET_PATH_CAPACITY],player[AF_ASSET_PATH_CAPACITY];const char *base=af_engine_get_base_path(app->engine);SDL_PathInfo info;
    snprintf(tool,sizeof(tool),"%stools/scripts/afterlight.py",base?base:"");if(!SDL_GetPathInfo(tool,&info))snprintf(tool,sizeof(tool),"%s",AF_PROJECT_TOOL_PATH);
    snprintf(player,sizeof(player),"%stools/afterlight_player.app",base?base:"");if(!SDL_GetPathInfo(player,&info))snprintf(player,sizeof(player),"%s",AF_PLAYER_PACKAGE_PATH);
    const char *create[]={AF_PYTHON_PATH,tool,"new",app->wizard_folder,"--name",app->wizard_name,NULL};
    const char *package[]={AF_PYTHON_PATH,tool,"export",app->project_path,"--output",app->wizard_folder,"--player",player,NULL};
    app->tool_success=false;app->tool_process=SDL_CreateProcess(export?package:create,true);app->tool_action=export?2:1;
    set_status(app,app->tool_process?(export?"Exporting project...":"Creating project..."):SDL_GetError());
}
static void project_panel(editor *app,float width,float height)
{
    if(!app->show_project)return;struct nk_context *ctx=&app->gui.context;
    if(nk_begin(ctx,"Project",nk_rect(fmaxf(20,(width-620)/2),70,620,fminf(680,height-100)),NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_MOVABLE)){
        label(ctx,app->project.name[0]?app->project.name:"Create a project from this scene",28);
        nk_layout_row_dynamic(ctx,42,1);nk_label_wrap(ctx,app->project_path[0]?app->project_path:"No project file selected.");
        label(ctx,"Startup scene",24);nk_layout_row_dynamic(ctx,54,1);nk_label_wrap(ctx,app->project.startup_scene);
        label(ctx,"C game module for Play",24);nk_layout_row_dynamic(ctx,54,1);nk_label_wrap(ctx,app->module_path);
        nk_layout_row_dynamic(ctx,34,2);
        if(nk_button_label(ctx,"Open project")&&!app->runtime.playing)request_action(app,ED_OPEN_PROJECT);
        if(nk_button_label(ctx,"Save project as")&&!app->runtime.playing&&!app->dialog){
            if(!app->document.path[0]||ed_document_dirty(&app->document))set_status(app,"Save the startup scene before saving its project.");
            else{app->dialog=ed_dialog_begin_project(app->engine,true,!app->project_is_template&&app->project_path[0]?app->project_path:"Untitled.afterlight.project");app->dialog_asset=6;}
        }
        label(ctx,"New Project wizard / Export",28);
        label(ctx,"Project name",22);nk_layout_row_dynamic(ctx,30,1);nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,app->wizard_name,sizeof(app->wizard_name),nk_filter_default);
        label(ctx,"New destination directory (must not exist)",22);nk_layout_row_dynamic(ctx,30,1);nk_edit_string_zero_terminated(ctx,NK_EDIT_FIELD,app->wizard_folder,sizeof(app->wizard_folder),nk_filter_default);
        nk_layout_row_dynamic(ctx,34,2);if(nk_button_label(ctx,app->tool_process?"Working...":"Create starter project")&&!app->tool_process)start_project_tool(app,false);
        if(nk_button_label(ctx,"Export saved project")&&!app->tool_process)start_project_tool(app,true);
        nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Close project panel"))app->show_project=false;
    }nk_end(ctx);
}
static void play_hud(editor *app)
{
    if(!app->runtime.playing)return;bool player=false;
    for(size_t i=0;i<af_scene_object_count(app->runtime.scene);++i)if(af_scene_object_at(app->runtime.scene,i)->gameplay.player.enabled){player=true;break;}
    ed_play_hud(&app->gui.context,app->engine,app->viewport,app->runtime.paused,app->free_camera,true,player);
}
static bool animation_button(editor *app,unsigned index,const char *text){struct nk_rect b=nk_widget_bounds(&app->gui.context);app->animation_buttons[index]=(af_rect){b.x,b.y,b.w,b.h};return nk_button_label(&app->gui.context,text)!=0;}
static void animation_panel(editor *app,float width,float height)
{
    if(!app->show_animation)return;const af_scene *scene=app->runtime.playing?app->runtime.scene:app->document.scene;struct nk_context *ctx=&app->gui.context;
    const af_scene_object *target=af_scene_get_object(scene,app->animation_target);
    if(!target||!target->animation.asset[0]){app->animation_target=0;for(size_t i=0;i<af_scene_object_count(scene);++i){const af_scene_object *o=af_scene_object_at(scene,i);if(o->animation.asset[0]){app->animation_target=o->id;target=o;break;}}}
    nk_flags flags=NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_MOVABLE;if(app->modal||app->dialog)flags|=NK_WINDOW_ROM;
    if(nk_begin(ctx,"Animation",nk_rect(fmaxf(20,(width-560)/2),110,560,fminf(590,height-150)),flags)){
        nk_layout_row_dynamic(ctx,30,1);if(nk_combo_begin_label(ctx,target?target->name:"No animated model selected",nk_vec2(500,180))){nk_layout_row_dynamic(ctx,26,1);
            for(size_t i=0;i<af_scene_object_count(scene);++i){const af_scene_object *o=af_scene_object_at(scene,i);if(o->animation.asset[0]&&nk_combo_item_label(ctx,o->name,NK_TEXT_LEFT)){app->animation_target=o->id;target=o;}}nk_combo_end(ctx);}
        if(target&&target->animation.asset[0]){
            af_animation_component component=target->animation;af_object_id root=target->id;af_animation_status status=af_engine_animation_status(app->engine,root);bool changed=false;
            label(ctx,component.asset,24);
            if(!app->runtime.playing){af_character_rig rig=target->rig;nk_layout_row_dynamic(ctx,26,1);nk_bool enabled=rig.enabled;bool rig_changed=nk_checkbox_label(ctx,"Character animation rig",&enabled)!=0;rig.enabled=enabled!=0;
                if(rig.enabled){for(unsigned state=0;state<5;++state){char title[100];snprintf(title,sizeof(title),"%s clip",af_locomotion_state_name((af_locomotion_state)state));label(ctx,title,22);nk_layout_row_dynamic(ctx,28,1);af_animation_clip_info info;const char *name=af_engine_animation_clip(app->engine,root,rig.clips[state],&info)?info.name:"Missing clip";if(nk_combo_begin_label(ctx,name,nk_vec2(400,160))){nk_layout_row_dynamic(ctx,26,1);for(size_t clip=0;clip<status.clip_count;++clip)if(af_engine_animation_clip(app->engine,root,clip,&info)&&nk_combo_item_label(ctx,info.name,NK_TEXT_LEFT)){rig.clips[state]=(uint32_t)clip;rig_changed=true;}nk_combo_end(ctx);}}
                    int node=(int)rig.root_node;nk_layout_row_dynamic(ctx,28,1);if(nk_property_int(ctx,"Root node",0,&node,16383,1,1)){rig.root_node=(uint32_t)node;rig_changed=true;}
                    float *v[]={&rig.walk_speed,&rig.run_speed,&rig.camera_distance,&rig.camera_height,&rig.turn_speed};const char *names[]={"Walk model speed","Run model speed","Camera distance","Camera height","Turn speed"};float minimum[]={.01f,.02f,.2f,0,0},maximum[]={99,100,30,10,100};for(unsigned i=0;i<5;++i){nk_layout_row_dynamic(ctx,28,1);rig_changed|=nk_property_float(ctx,names[i],minimum[i],v[i],maximum[i],.1f,.01f)!=0;}
                    nk_layout_row_dynamic(ctx,26,1);nk_bool motion=rig.root_motion;if(nk_checkbox_label(ctx,"Use root motion",&motion)){rig.root_motion=motion!=0;rig_changed=true;}}
                if(rig_changed&&af_character_rig_valid(rig))ed_document_rig(&app->document,root,rig);
            }

            nk_layout_row_dynamic(ctx,26,1);nk_bool skeleton=app->show_skeleton;if(nk_checkbox_label(ctx,"Show posed skeleton",&skeleton))app->show_skeleton=skeleton!=0;
            af_animation_debug debug=af_engine_animation_debug(app->engine,root);char diagnostic[160];
            if(debug.driven){snprintf(diagnostic,sizeof(diagnostic),"%s | speed %.2f | phase %.2f",af_locomotion_state_name(debug.motion.state),debug.motion.speed,debug.motion.phase);label(ctx,diagnostic,24);
                snprintf(diagnostic,sizeof(diagnostic),"Idle %.2f  Walk %.2f  Run %.2f",debug.weights[0],debug.weights[1],debug.weights[2]);label(ctx,diagnostic,24);
                snprintf(diagnostic,sizeof(diagnostic),"Root motion %s | delta %.3f, %.3f",debug.root_motion?"on":"off",debug.root_delta.x,debug.root_delta.z);label(ctx,diagnostic,24);}
            snprintf(diagnostic,sizeof(diagnostic),"Events %llu | queued %zu | dropped %llu | %s",(unsigned long long)debug.emitted_events,debug.pending_events,(unsigned long long)debug.dropped_events,debug.last_event);label(ctx,diagnostic,24);

            if(!status.available)label(ctx,"Animation asset unavailable. Reload its source.",26);
            else if(!status.clip_count)label(ctx,"Skeleton bind pose (no animation clips).",26);
            else{
                if(debug.driven)nk_widget_disable_begin(ctx);
                nk_layout_row_dynamic(ctx,30,1);if(nk_combo_begin_label(ctx,status.name,nk_vec2(500,180))){nk_layout_row_dynamic(ctx,26,1);for(size_t i=0;i<status.clip_count;++i){af_animation_clip_info info;if(af_engine_animation_clip(app->engine,root,i,&info)&&nk_combo_item_label(ctx,info.name,NK_TEXT_LEFT)){
                        if(!app->runtime.playing)app->animation_preview=true;af_engine_animation_play(app->engine,root,i,app->animation_blend);}}
                    nk_combo_end(ctx);}
                nk_layout_row_dynamic(ctx,32,3);
                if(animation_button(app,0,app->runtime.playing?"Restart":"Preview")){if(!app->runtime.playing)app->animation_preview=true;af_engine_animation_play(app->engine,root,status.clip,app->animation_blend);}
                if(animation_button(app,1,status.playing?"Pause":"Resume"))af_engine_animation_pause(app->engine,root,status.playing);
                if(animation_button(app,2,"Stop")){af_engine_animation_stop(app->engine,root);if(!app->runtime.playing)app->animation_preview=false;}
                float time=(float)status.time;nk_layout_row_dynamic(ctx,30,1);if(nk_property_float(ctx,"Time",0,&time,(float)status.duration,.05f,.005f)){if(!app->runtime.playing)app->animation_preview=true;af_engine_animation_pause(app->engine,root,true);af_engine_animation_seek(app->engine,root,time);}
                char text[128];snprintf(text,sizeof(text),"%.3f / %.3f s%s",status.time,status.duration,status.fading?"  (blending)":"");label(ctx,text,24);
                float speed=status.speed;nk_bool loop=status.loop;
                nk_layout_row_dynamic(ctx,30,1);bool options=nk_property_float(ctx,"Speed",-4,&speed,4,.1f,.01f)!=0;nk_layout_row_dynamic(ctx,26,1);options|=nk_checkbox_label(ctx,"Loop",&loop)!=0;
                if(options)af_engine_animation_options(app->engine,root,speed,loop!=0);
                if(!app->runtime.playing){nk_layout_row_dynamic(ctx,26,1);nk_bool autoplay=component.autoplay;if(nk_checkbox_label(ctx,"Autoplay in game",&autoplay)){component.autoplay=autoplay!=0;changed=true;}}
                if(!app->runtime.playing){nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Use clip, speed and loop as startup defaults")){component.clip=status.clip;component.speed=speed;component.loop=loop!=0;changed=true;}}
                nk_layout_row_dynamic(ctx,30,1);nk_property_float(ctx,"Blend seconds",0,&app->animation_blend,5,.1f,.01f);
                if(debug.driven)nk_widget_disable_end(ctx);
                label(ctx,debug.driven?"Character gameplay controls this animation.":app->runtime.playing?"Playback follows simulation pause/step.":"Preview leaves authored transforms and history unchanged.",28);
            }
            if(changed&&!app->runtime.playing){if(ed_document_animation(&app->document,root,component)){af_engine_set_scene(app->engine,app->document.scene);if(app->animation_preview)af_engine_animation_play(app->engine,root,component.clip,app->animation_blend);}}
        }else label(ctx,"Import a glTF model containing animation or a skin.",30);
        nk_layout_row_dynamic(ctx,30,1);if(nk_button_label(ctx,"Close animation panel"))app->show_animation=false;
    }nk_end(ctx);
}
static void build_ui(editor *app,float width,float height)
{
    struct nk_context *context=&app->gui.context;
    ed_thumbnails_begin(&app->thumbnails);ed_thumbnails_poll(&app->thumbnails,app->engine);
    draw_colliders(app);draw_skeleton(app);draw_lights(app);if(!app->animation_preview)draw_gizmo(app);
    toolbar(app,width);
    if(!app->runtime.playing||!app->maximize_play){object_list(app,height);inspector(app,width,height);}
    nk_style_push_vec2(context,&context->style.window.padding,nk_vec2(14,5));
    if(!app->runtime.playing||!app->maximize_play){
    if(nk_begin(context,"Viewport tools",nk_rect(252,64,width-586,40),panel_flags(app)|NK_WINDOW_NO_SCROLLBAR)){
        nk_layout_row_dynamic(context,22,7);
        if(nk_button_label(context,app->gizmo.mode==ED_GIZMO_MOVE?"[Move]":"Move"))app->gizmo.mode=ED_GIZMO_MOVE;
        if(nk_button_label(context,app->gizmo.mode==ED_GIZMO_ROTATE?"[Rotate]":"Rotate"))app->gizmo.mode=ED_GIZMO_ROTATE;
        if(nk_button_label(context,app->gizmo.mode==ED_GIZMO_SCALE?"[Scale]":"Scale"))app->gizmo.mode=ED_GIZMO_SCALE;
        nk_bool snap=app->gizmo.snap;nk_checkbox_label(context,"Snap",&snap);app->gizmo.snap=snap!=0;
        if(nk_button_label(context,app->show_colliders?"[Colliders]":"Colliders"))app->show_colliders=!app->show_colliders;
        if(nk_button_label(context,"Reset view"))reset_camera(app);
        if(nk_button_label(context,"Project"))app->show_project=!app->show_project;
    }nk_end(context);
    }
    play_hud(app);
    if(app->environment_pending){af_environment_job_status job=af_engine_environment_status(app->engine);
        if(nk_begin(context,"HDR import",nk_rect(fmaxf(20,(width-480)/2),120,480,150),NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_NO_SCROLLBAR)){
            char progress[128];snprintf(progress,sizeof(progress),"%s: %.0f%%",job.state==AF_ENV_LOADING?"Reading panorama":job.state==AF_ENV_BAKING?"Baking environment":"Publishing environment",job.progress*100);
            nk_layout_row_dynamic(context,30,1);nk_label(context,progress,NK_TEXT_LEFT);
            nk_layout_row_dynamic(context,32,1);if(nk_button_label(context,"Cancel import"))af_engine_cancel_environment(app->engine);
        }nk_end(context);
    }
    if(nk_begin(context,"Status",nk_rect(0,height-32,width,32),NK_WINDOW_NO_SCROLLBAR|NK_WINDOW_NO_INPUT)){
        nk_layout_row_dynamic(context,18,1);nk_label(context,app->document.status,NK_TEXT_LEFT);
    }nk_end(context);
    nk_style_pop_vec2(context);
    performance_panel(app,width,height);asset_browser(app,width,height);project_panel(app,width,height);
    af_engine_set_scene(app->engine,app->runtime.playing?app->runtime.scene:app->document.scene);
    animation_panel(app,width,height);
    if(app->modal){
        if(nk_begin(context,"Unsaved changes",nk_rect((width-500)/2,(height-205)/2,500,205),NK_WINDOW_BORDER|NK_WINDOW_TITLE|NK_WINDOW_NO_SCROLLBAR)){
            nk_layout_row_dynamic(context,58,1);nk_label_wrap(context,"Save your changes before continuing? Discard keeps the last saved file unchanged.");
            nk_layout_row_dynamic(context,36,3);
            if(nk_button_label(context,"Save"))save_document(app,false,app->pending);
            if(nk_button_label(context,"Discard"))perform_action(app,app->pending);
            if(nk_button_label(context,"Cancel")){app->modal=false;app->pending=ED_NONE;}
        }nk_end(context);nk_window_set_focus(context,"Unsaved changes");
    }
    app->fields_active=context->active&&(context->active->edit.active||context->active->property.active);
    if(!ed_gui_set_text_input(&app->gui,app->engine,app->fields_active&&!app->runtime.playing&&!app->modal&&!app->dialog)){
        app->failed=true;af_engine_request_quit(app->engine);
    }
    if(!af_engine_mouse_down(app->engine,AF_MOUSE_LEFT)&&!app->fields_active)ed_document_end_edit(&app->document);
}
static bool init(af_engine *engine,void *userdata)
{
    editor *app=userdata;app->engine=engine;af_engine_set_animation_playback(engine,false,false);
    if(!ed_document_init(&app->document))return false;
    app->document_ready=true;
    const char *base=af_engine_get_base_path(engine);
    if(!base)return false;
    char font[ED_PATH_CAPACITY],scene[ED_PATH_CAPACITY];
    if(snprintf(font,sizeof(font),"%sfonts/FiraSans-Regular.ttf",base)>=(int)sizeof(font)||
        snprintf(scene,sizeof(scene),"%sscenes/%s",base,app->playground?"obstacle.afterlight.json":"demo.afterlight.json")>=(int)sizeof(scene))return false;
    char bundled_project[AF_ASSET_PATH_CAPACITY];
    if(snprintf(bundled_project,sizeof(bundled_project),"%s%s.afterlight.project",base,app->playground?"playground":"demo")>=(int)sizeof(bundled_project))return false;
    const char *project=app->initial_project?app->initial_project:(!app->initial_scene?bundled_project:NULL);
    if(!(project?open_project(app,project,app->initial_project==NULL):ed_document_load(&app->document,app->initial_scene?app->initial_scene:scene,app->initial_scene==NULL))){
        fprintf(stderr,"Editor scene: %s\n",app->document.status);return false;
    }
    /* Development watches the build output. A moved bundle uses its own module. */
    if(!app->initial_project&&!app->module_override&&!app->initial_scene){SDL_PathInfo info;const char *development=app->playground?AF_PLAYGROUND_MODULE_PATH:AF_GAME_MODULE_PATH;
        if(SDL_GetPathInfo(development,&info)&&info.type==SDL_PATHTYPE_FILE)snprintf(app->module_path,sizeof(app->module_path),"%s",development);}
    af_mesh_data cube=cube_mesh_data();
    if(!af_engine_upload_mesh(engine,"cube",&cube)||!ed_gui_init(&app->gui,engine,font))return false;
    if(app->animation_test_path){if(!ed_document_new(&app->document)||!ed_document_import_mesh(&app->document,"actor",app->animation_test_path))return false;app->animation_target=app->document.selection;af_animation_component c=af_scene_get_object(app->document.scene,app->animation_target)->animation;c.autoplay=false;if(!af_scene_set_object_animation(app->document.scene,app->animation_target,c))return false;app->show_animation=true;app->animation_test_revision=app->document.revision;}
    load_assets(app);app->asset_watch=af_asset_watch_create();app->watch_assets=true;
    SDL_SetWindowMinimumSize(af_engine_sdl_window(engine),1000,640);
    reset_camera(app);refresh(app);
    af_engine_set_scene(engine,app->document.scene);
    af_engine_set_clear_color(engine,(af_color){0.018f,0.022f,0.033f,1});
    return true;
}
static void smoke_click(editor *app,float x,float y)
{
    SDL_Event event={0};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));
    event.button.button=SDL_BUTTON_LEFT;event.button.down=true;event.button.x=x;event.button.y=y;
    app->failed|=!SDL_PushEvent(&event);
    event.type=SDL_EVENT_MOUSE_BUTTON_UP;event.button.down=false;
    app->failed|=!SDL_PushEvent(&event);
}
static void smoke_button(editor *app,Uint8 button,bool down)
{
    SDL_Event event={0};event.type=down?SDL_EVENT_MOUSE_BUTTON_DOWN:SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));event.button.button=button;event.button.down=down;
    event.button.x=550;event.button.y=350;app->failed|=!SDL_PushEvent(&event);
}
static void smoke_key(editor *app,SDL_Scancode key)
{
    SDL_Event event={0};event.type=SDL_EVENT_KEY_DOWN;event.key.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));event.key.scancode=key;event.key.down=true;app->failed|=!SDL_PushEvent(&event);
    event.type=SDL_EVENT_KEY_UP;event.key.down=false;app->failed|=!SDL_PushEvent(&event);
}
#define SMOKE_CHECK(app,condition) do{if(!(condition)){fprintf(stderr,"Editor smoke frame %u: %s\n",(app)->updates,#condition);(app)->failed=true;}}while(0)
static void animation_tools_smoke(editor *app){
    if(!app->animation_test_path||app->updates<8)return;
    if(!(SDL_GetWindowFlags(af_engine_sdl_window(app->engine))&SDL_WINDOW_INPUT_FOCUS)&&app->animation_test_step==0){SDL_RaiseWindow(af_engine_sdl_window(app->engine));return;}
    af_animation_status status=af_engine_animation_status(app->engine,app->animation_target);int click=-1;
    switch(app->animation_test_step++){
    case 0:SMOKE_CHECK(app,nk_window_find(&app->gui.context,"Animation")&&status.clip_count==3&&!app->animation_preview);click=0;break;
    case 1:SMOKE_CHECK(app,app->animation_preview);break;
    case 2:SMOKE_CHECK(app,status.time>0&&status.playing);click=1;break;
    case 3:SMOKE_CHECK(app,!status.playing);app->animation_test_time=status.time;break;
    case 4:SMOKE_CHECK(app,status.time==app->animation_test_time);click=1;break;
    case 5:SMOKE_CHECK(app,status.playing);break;
    case 6:SMOKE_CHECK(app,status.time>app->animation_test_time);click=2;break;
    case 7:SMOKE_CHECK(app,!app->animation_preview&&status.time==0);break;
    case 8:SMOKE_CHECK(app,app->document.revision==app->animation_test_revision&&app->document.selection==app->animation_target);toggle_play(app);SMOKE_CHECK(app,app->runtime.playing&&af_engine_animation_play(app->engine,app->animation_target,0,0));break;
    case 9:case 10:break;
    case 11:SMOKE_CHECK(app,af_engine_animation_status(app->engine,app->animation_target).time>0);stop_play(app);break;
    case 12:SMOKE_CHECK(app,!app->runtime.playing&&app->document.revision==app->animation_test_revision&&af_scene_get_object(app->document.scene,app->animation_target)->animation.clip==0);puts("Editor animation: actual Preview/Pause/Resume/Stop clicks, clean history and Play isolation passed.");af_engine_request_quit(app->engine);return;
    default:break;
    }
    if(click>=0){af_rect b=app->animation_buttons[click];smoke_click(app,b.x+b.width*.5f,b.y+b.height*.5f);}
}
static void light_tools_smoke(editor *app){
    if(!app->light_test||app->updates<8)return;
    uint64_t losses=af_engine_test_focus_losses(app->engine);bool focused=(SDL_GetWindowFlags(af_engine_sdl_window(app->engine))&SDL_WINDOW_INPUT_FOCUS)!=0;
    if(app->light_test_step&&(losses!=app->light_test_focus_losses||!focused)){app->light_test_step=app->light_test_focus=0;app->light_dragging=app->gizmo.dragging=false;fputs("Native focus changed; restarting scripted light controls.\n",stderr);}
    if(!app->light_test_step){app->light_test_focus_losses=losses;if(!focused){app->light_test_focus=0;SDL_RaiseWindow(af_engine_sdl_window(app->engine));return;}if(++app->light_test_focus<8)return;}
    SDL_WindowID window=SDL_GetWindowID(af_engine_sdl_window(app->engine));
    switch(app->light_test_step++){
    case 0:{SMOKE_CHECK(app,ed_document_new(&app->document));SMOKE_CHECK(app,ed_document_add_light(&app->document,AF_LIGHT_POINT));af_local_light light=*af_scene_local_light_at(app->document.scene,0);light.position=(af_vec3){0};SMOKE_CHECK(app,ed_document_local_light(&app->document,0,light));ed_document_end_edit(&app->document);app->light_selected=false;app->gizmo.mode=ED_GIZMO_MOVE;app->fields_active=false;app->show_assets=false;break;}
    case 1:{af_vec2 position;SMOKE_CHECK(app,ed_project(&app->camera,app->viewport,(af_vec3){0},&position));smoke_click(app,position.x,position.y);break;}
    case 2:SMOKE_CHECK(app,app->light_selected&&app->selected_light==0);break;
    case 3:case 9:{af_vec2 p={app->gizmo.origin.x+(app->gizmo.ends[0].x-app->gizmo.origin.x)*.7f,app->gizmo.origin.y+(app->gizmo.ends[0].y-app->gizmo.origin.y)*.7f};SDL_Event event={0};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=window;event.button.button=SDL_BUTTON_LEFT;event.button.down=true;event.button.x=p.x;event.button.y=p.y;SMOKE_CHECK(app,SDL_PushEvent(&event));break;}
    case 4:case 10:SMOKE_CHECK(app,app->light_dragging&&app->gizmo.dragging);break;
    case 5:case 11:{SDL_Event event={0};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=window;event.motion.x=app->gizmo.start.x+app->gizmo.direction.x*.5f;event.motion.y=app->gizmo.start.y+app->gizmo.direction.y*.5f;SMOKE_CHECK(app,SDL_PushEvent(&event));break;}
    case 6:case 13:{SDL_Event event={0};event.type=SDL_EVENT_MOUSE_BUTTON_UP;event.button.windowID=window;event.button.button=SDL_BUTTON_LEFT;event.button.x=app->gizmo.start.x+app->gizmo.direction.x*.5f;event.button.y=app->gizmo.start.y+app->gizmo.direction.y*.5f;SMOKE_CHECK(app,SDL_PushEvent(&event));break;}
    case 7:{const af_local_light *light=af_scene_local_light_at(app->document.scene,0);SMOKE_CHECK(app,light&&light->position.x>0&&!app->gizmo.dragging);SMOKE_CHECK(app,ed_document_undo(&app->document));SMOKE_CHECK(app,af_scene_local_light_at(app->document.scene,0)->position.x==0);SMOKE_CHECK(app,ed_document_redo(&app->document));app->light_test_position=af_scene_local_light_at(app->document.scene,0)->position;break;}
    case 8:break;
    case 12:smoke_key(app,SDL_SCANCODE_ESCAPE);break;
    case 14:SMOKE_CHECK(app,!app->light_dragging&&!app->gizmo.dragging);SMOKE_CHECK(app,af_vec3_length(af_vec3_sub(af_scene_local_light_at(app->document.scene,0)->position,app->light_test_position))<.0001f);break;
    case 15:puts("Editor light tools: viewport pick, axis drag, grouped undo/redo and Escape cancellation passed.");af_engine_request_quit(app->engine);return;
    default:break;
    }
}
static void play_controls_smoke(editor *app)
{
    if(!app->play_controls_test)return;
    uint64_t focus_losses=af_engine_test_focus_losses(app->engine);
    if(app->play_test_started&&(focus_losses!=app->play_test_focus_losses||!(SDL_GetWindowFlags(af_engine_sdl_window(app->engine))&SDL_WINDOW_INPUT_FOCUS))){
        /* Native focus loss clears all held/injected buttons in the engine.
           Restart this sequence instead of treating OS focus as a game failure. */
        stop_play(app);app->play_test_started=app->play_test_focus=0;app->play_test_focus_losses=focus_losses;
        fputs("Native focus changed; restarting scripted Play controls.\n",stderr);return;
    }
    /* Play normally follows a foreground user click. Wait for that same native
       focus state before injecting events, rather than racing Cocoa startup. */
    if(!app->play_test_started){
        app->play_test_focus_losses=focus_losses;
        if(SDL_GetWindowFlags(af_engine_sdl_window(app->engine))&SDL_WINDOW_INPUT_FOCUS){if(++app->play_test_focus>=8)app->play_test_started=app->updates;}
        else{app->play_test_focus=0;if(app->updates%10==1)SDL_RaiseWindow(af_engine_sdl_window(app->engine));}
        return;
    }
    unsigned stage=app->updates-app->play_test_started;
    if(stage==3){toggle_play(app);smoke_button(app,SDL_BUTTON_RIGHT,true);}
    if(stage==4){
        SMOKE_CHECK(app,af_engine_mouse_captured(app->engine)&&fabsf(app->camera.position.z-7.0f)<.01f);
    }
    if(stage==5){
        SMOKE_CHECK(app,app->free_camera&&af_engine_mouse_captured(app->engine)&&!app->runtime.host.camera_enabled);
        app->camera.position.x+=10;smoke_button(app,SDL_BUTTON_RIGHT,false);
    }
    if(stage==7){
        SMOKE_CHECK(app,!app->free_camera&&af_engine_mouse_captured(app->engine)&&fabsf(app->camera.position.x)<.01f);
        smoke_key(app,SDL_SCANCODE_ESCAPE);
    }
    if(stage==9){SMOKE_CHECK(app,!af_engine_mouse_captured(app->engine));smoke_click(app,550,350);smoke_key(app,SDL_SCANCODE_SPACE);}
    if(stage==11){SMOKE_CHECK(app,af_engine_mouse_captured(app->engine));
        SMOKE_CHECK(app,af_engine_audio_stats(app->engine).clips==4&&af_engine_get_stats(app->engine).loaded_meshes>=2&&af_engine_get_stats(app->engine).missing_meshes==0);
    }
    if(stage==13){SMOKE_CHECK(app,af_engine_audio_stats(app->engine).plays>0&&af_engine_gameplay_status(app->engine).elapsed>0);
        app->runtime.paused=true;af_engine_set_simulation_paused(app->engine,true);}
    if(stage==14){app->smoke_ticks=af_engine_get_stats(app->engine).simulation_ticks;af_engine_step_simulation(app->engine);}
    if(stage==15){SMOKE_CHECK(app,af_engine_get_stats(app->engine).simulation_ticks==app->smoke_ticks+1);smoke_key(app,SDL_SCANCODE_F6);}
    if(stage==18){SMOKE_CHECK(app,app->maximize_play&&app->viewport.x==0&&app->runtime.paused&&nk_window_find(&app->gui.context,"Play HUD")!=NULL);smoke_key(app,SDL_SCANCODE_F6);}
    if(stage==21){SMOKE_CHECK(app,!app->maximize_play&&app->viewport.x==252);stop_play(app);
        SMOKE_CHECK(app,!af_engine_mouse_captured(app->engine)&&memcmp(&app->camera,&app->edit_camera,sizeof(af_camera))==0);}
    if(stage==22){
        SMOKE_CHECK(app,!app->runtime.playing&&af_scene_object_count(app->document.scene)==21&&!ed_document_dirty(&app->document));
        af_engine_request_quit(app->engine);
    }
}
static void smoke(editor *app)
{
    if(!app->smoke_path)return;
    ed_document *d=&app->document;
    if(app->updates==152){app->show_colliders=true;
        for(size_t i=0;i<af_scene_object_count(d->scene)&&i<3;++i){af_collider collider=af_collider_default();collider.type=(af_collider_type)(i+1);af_scene_set_object_collider(d->scene,af_scene_object_at(d->scene,i)->id,collider);}}
    if(app->updates==155){app->show_performance=true;app->show_assets=true;}
    if(app->updates==158){app->failed|=nk_window_find(&app->gui.context,"Performance")==NULL||nk_window_find(&app->gui.context,"Assets")==NULL;app->show_performance=false;app->show_assets=false;}
    if(app->updates==3)toggle_play(app);
    if(app->updates==7){const af_scene_object *o=af_scene_get_object(app->runtime.scene,1);app->failed|=!app->runtime.playing||!o||o->transform.rotation.y==0||af_scene_get_object(d->scene,1)->transform.rotation.y!=0;}
    if(app->updates==8){app->runtime.paused=true;af_engine_set_simulation_paused(app->engine,true);}
    if(app->updates==9){app->smoke_ticks=af_engine_get_stats(app->engine).simulation_ticks;af_engine_step_simulation(app->engine);}
    if(app->updates==10){app->failed|=af_engine_get_stats(app->engine).simulation_ticks!=app->smoke_ticks+1;stop_play(app);}
    if(app->updates==10){af_transform t=af_scene_get_object(d->scene,d->selection)->transform;t.position.x=-3;app->failed|=!ed_document_transform(d,t);ed_document_end_edit(d);}
    if(app->updates==15)app->failed|=!ed_document_undo(d);
    if(app->updates==20)app->failed|=!ed_document_redo(d);
    if(app->updates==21)smoke_click(app,1020,195);
    if(app->updates==23){
        app->failed|=!SDL_TextInputActive(af_engine_sdl_window(app->engine));
        SDL_Event event={0};event.type=SDL_EVENT_TEXT_INPUT;event.text.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));event.text.text="x";app->failed|=!SDL_PushEvent(&event);
    }
    if(app->updates==25){app->failed|=strchr(af_scene_get_object(d->scene,1)->name,'x')==NULL;app->failed|=!ed_document_name(d,"Editor smoke cube");ed_document_end_edit(d);}
    if(app->updates==26||app->updates==28||app->updates==29){
        if(app->updates==28)app->failed|=strlen(af_scene_get_object(d->scene,1)->name)!=16;
        SDL_Event event={0};event.type=app->updates==29?SDL_EVENT_KEY_UP:SDL_EVENT_KEY_DOWN;
        event.key.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));event.key.scancode=SDL_SCANCODE_BACKSPACE;event.key.down=app->updates!=29;event.key.repeat=app->updates==28;app->failed|=!SDL_PushEvent(&event);
    }
    if(app->updates==30){app->failed|=strlen(af_scene_get_object(d->scene,1)->name)!=15;app->failed|=!ed_document_duplicate(d);}
    if(app->updates==35)app->failed|=!ed_document_delete(d);
    if(app->updates==40)app->failed|=!ed_document_undo(d);
    if(app->updates==45)app->failed|=!ed_document_save(d,app->smoke_path);
    if(app->updates==50)app->failed|=!ed_document_load(d,app->smoke_path,false);
    if(app->updates==55){uint64_t revision=d->revision;app->failed|=ed_document_load(d,"missing-smoke-scene.json",false)||d->revision!=revision;}
    if(app->updates==65)app->failed|=!af_engine_set_window_size(app->engine,1100,720);
    if(app->updates==95)app->failed|=!af_engine_set_window_size(app->engine,1280,800);
    if(app->updates<=55)refresh(app);
    if(app->updates==100){
        app->gizmo.mode=ED_GIZMO_MOVE;
        af_vec2 p={app->gizmo.origin.x+(app->gizmo.ends[0].x-app->gizmo.origin.x)*0.7f,app->gizmo.origin.y+(app->gizmo.ends[0].y-app->gizmo.origin.y)*0.7f};
        SDL_Event event={0};event.type=SDL_EVENT_MOUSE_BUTTON_DOWN;event.button.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));event.button.button=SDL_BUTTON_LEFT;event.button.down=true;event.button.x=p.x;event.button.y=p.y;app->failed|=!SDL_PushEvent(&event);
    }
    if(app->updates==102){SDL_Event event={0};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));event.motion.x=app->gizmo.start.x+app->gizmo.direction.x*0.4f;event.motion.y=app->gizmo.start.y+app->gizmo.direction.y*0.4f;app->failed|=!SDL_PushEvent(&event);}
    if(app->updates==104){SDL_Event event={0};event.type=SDL_EVENT_MOUSE_BUTTON_UP;event.button.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));event.button.button=SDL_BUTTON_LEFT;app->failed|=!SDL_PushEvent(&event);}
    if(app->updates==107){const af_scene_object *o=af_scene_get_object(d->scene,d->selection);app->failed|=!o||o->transform.position.x<=app->gizmo.initial.position.x||app->gizmo.dragging;app->failed|=!ed_document_undo(d);refresh(app);}
    if(app->updates==115)smoke_click(app,75,132); /* Add cube through the UI. */
    if(app->updates==118)app->failed|=af_scene_object_count(d->scene)!=6;
    if(app->updates==120)smoke_click(app,175,132);
    if(app->updates==123)app->failed|=af_scene_object_count(d->scene)!=7;
    if(app->updates==125)smoke_click(app,490,33);
    if(app->updates==128)app->failed|=af_scene_object_count(d->scene)!=6;
    if(app->updates==130)smoke_click(app,560,33);
    if(app->updates==133)app->failed|=af_scene_object_count(d->scene)!=7;
    if(app->updates==135)smoke_click(app,971,310);
    if(app->updates==138){const af_scene_object *o=af_scene_get_object(d->scene,d->selection);app->failed|=!o||o->visible;}
    if(app->updates==140)smoke_click(app,1042,384);
    if(app->updates==143){const af_scene_object *o=af_scene_get_object(d->scene,d->selection);app->failed|=!o||fabsf(o->transform.position.x-1.6f)>0.001f;}
    if(app->updates==145)app->failed|=!ed_document_save(d,app->smoke_path);
    if(app->updates==150)app->failed|=!ed_document_load(d,app->smoke_path,false);
    if(app->updates==151)refresh(app);
    if(app->updates==155){
        ed_document_name(d,"Unsaved modal test");ed_document_end_edit(d);
        SDL_Event close={0};close.type=SDL_EVENT_WINDOW_CLOSE_REQUESTED;
        close.window.windowID=SDL_GetWindowID(af_engine_sdl_window(app->engine));
        app->failed|=!SDL_PushEvent(&close);
    }
    if(app->updates==157)app->failed|=!app->modal;
    if(app->updates==158)smoke_click(app,800,430); /* Cancel window close. */
    if(app->updates==161)app->failed|=app->modal||af_scene_object_count(d->scene)!=7;
    if(app->updates==165)request_action(app,ED_NEW);
    if(app->updates==168)smoke_click(app,640,430); /* Discard into a new scene. */
    if(app->updates==171)app->failed|=app->modal||af_scene_object_count(d->scene)!=0||ed_document_dirty(d);
}
static void fixed_update(af_engine *engine,double delta,void *userdata){
    editor *app=userdata;if(!app->runtime.playing)return;
    if(app->runtime.paused)ed_runtime_step(&app->runtime,delta);
    else ed_runtime_update(&app->runtime,delta,!af_engine_mouse_down(engine,AF_MOUSE_RIGHT)&&!app->fields_active&&!app->modal&&!app->dialog);
    af_engine_set_scene(engine,app->runtime.scene);
}
static void update(af_engine *engine,double delta,void *userdata)
{
    editor *app=userdata;++app->updates;
    if(app->frame_limit&&af_engine_get_stats(engine).rendered_frames>=app->frame_limit){
        if(app->play_controls_test||app->wizard_test||app->thumbnail_test||app->light_test||app->animation_test_path){app->failed=true;fputs("Editor test did not complete within its frame budget.\n",stderr);}
        af_engine_request_quit(engine);return;}
    poll_dialog(app);poll_project_tool(app);play_controls_smoke(app);smoke(app);
    if(app->environment_pending){af_environment_job_status job=af_engine_environment_status(engine);
        if(job.state==AF_ENV_COMPLETE){
            if(app->document.revision==app->environment_document_revision&&!app->runtime.playing){if(ed_document_import_environment(&app->document,app->environment_name,app->environment_path))refresh(app);}
            else set_status(app,"HDR prepared, but the document changed; import again to assign it.");app->environment_pending=false;
        }else if(job.state==AF_ENV_FAILED||job.state==AF_ENV_CANCELED){set_status(app,job.state==AF_ENV_FAILED?job.error:"HDR import canceled; previous environment retained.");app->environment_pending=false;}
    }
    if(app->thumbnail_test){app->show_assets=true;if(app->updates==12)app->asset_filter=2;}
    if(app->wizard_test){
        if(app->updates==3){app->show_project=true;snprintf(app->wizard_name,sizeof(app->wizard_name),"Editor wizard check");start_project_tool(app,false);if(!app->tool_process){app->failed=true;af_engine_request_quit(engine);}}
        if(app->updates>3&&!app->tool_process){char path[AF_ASSET_PATH_CAPACITY];af_project project;af_asset_error error;
            bool valid=snprintf(path,sizeof(path),"%s/game.afterlight.project",app->wizard_folder)<(int)sizeof(path)&&af_project_load(path,&project,&error);
            app->failed|=!app->tool_success||!valid||af_scene_object_count(app->document.scene)!=4||ed_document_dirty(&app->document)||nk_window_find(&app->gui.context,"Project")==NULL;af_engine_request_quit(engine);}
    }
    if(app->selected_light>=af_scene_local_light_count(app->document.scene))app->light_selected=false;
    if(!app->runtime.playing)af_engine_set_scene(engine,app->document.scene);
    int w=0,h=0;af_engine_get_window_size(engine,&w,&h);
    float width=(float)w,height=(float)h;
    app->viewport=app->runtime.playing&&app->maximize_play?(af_rect){0,64,width,fmaxf(1,height-96)}:(af_rect){252,104,fmaxf(1,width-586),fmaxf(1,height-136)};
    af_engine_set_viewport(engine,&app->viewport);
    const af_scene_object *selected=af_scene_get_object(app->document.scene,app->document.selection);
    if(app->light_selected){const af_local_light *light=af_scene_local_light_at(app->document.scene,app->selected_light);if(light)ed_gizmo_project(&app->gizmo,&app->camera,app->viewport,light->position,af_mat4_identity(),ed_light_transform(*light).rotation);}
    else if(selected){af_mat4 world,parent=af_mat4_identity();
        if(selected->parent)af_scene_world_matrix(app->document.scene,selected->parent,&parent);
        if(af_scene_world_matrix(app->document.scene,selected->id,&world))ed_gizmo_project(&app->gizmo,&app->camera,app->viewport,(af_vec3){world.elements[12],world.elements[13],world.elements[14]},parent,selected->transform.rotation);
        else app->gizmo.projected=false;
    }
    else app->gizmo.projected=false;
    if(app->watch_assets){unsigned before=af_asset_watch_get_stats(app->asset_watch).reloads;af_asset_error message;af_asset_watch_update(app->asset_watch,engine,app->runtime.playing?app->runtime.scene:app->document.scene,delta,&message);if(af_asset_watch_get_stats(app->asset_watch).reloads!=before)ed_thumbnails_invalidate(&app->thumbnails);if(message.message[0])set_status(app,message.message);}
    ed_gui_input(&app->gui,engine);
    keyboard(app,delta);
    build_ui(app,width,height);
    af_engine_set_camera(engine,&app->camera);
    ed_runtime_frame_update(&app->runtime,delta,!af_engine_mouse_down(engine,AF_MOUSE_RIGHT)&&!app->fields_active&&!app->modal&&!app->dialog);
    if(app->runtime.playing)app->camera=af_engine_get_camera(engine);
    if(app->runtime.playing&&strcmp(app->runtime_status,app->runtime.status)){snprintf(app->runtime_status,sizeof(app->runtime_status),"%s",app->runtime.status);set_status(app,app->runtime.status);}
    light_tools_smoke(app);
    animation_tools_smoke(app);
    af_engine_set_scene(engine,app->runtime.playing?app->runtime.scene:app->document.scene);
    af_engine_set_animation_playback(engine,app->runtime.playing||app->animation_preview,!app->runtime.playing&&app->animation_preview);
    af_engine_highlight_object(engine,app->runtime.playing?AF_OBJECT_NONE:app->document.selection);
    af_engine_set_simulation_paused(engine,!app->runtime.playing||app->runtime.paused);
    char title[512];snprintf(title,sizeof(title),"Afterlight Editor | %s%s",app->document.path[0]?ed_path_name(app->document.path):"Untitled scene",ed_document_dirty(&app->document)?" *":"");
    if(strcmp(title,app->title)){snprintf(app->title,sizeof(app->title),"%s",title);af_engine_set_title(engine,title);}
    if(!ed_gui_render(&app->gui,engine,width,height)){app->failed=true;af_engine_request_quit(engine);}
    if(app->thumbnail_test&&app->updates==14)ed_thumbnails_invalidate(&app->thumbnails);
    if(app->thumbnail_test&&app->updates>=50){unsigned images=0,ready=0;
        for(uint32_t i=0;i<app->gui.draw_count;++i)if(app->gui.draws[i].texture>=2)++images;
        for(unsigned i=0;i<ED_THUMBNAIL_CACHE_SIZE;++i)if(app->thumbnails.entries[i].ready)++ready;
        app->failed|=images<1||ready<3||app->thumbnails.completed<3||!app->thumbnails.worker||ed_document_dirty(&app->document);
        printf("Editor thumbnails: %u cached / %u image draws / %u asynchronous completions; document %s.\n",ready,images,app->thumbnails.completed,ed_document_dirty(&app->document)?"dirty":"clean");
        af_scene_asset shutdown_asset={.name="shutdown-job",.kind=AF_ASSET_MESH,.path="Built-in mesh"};(void)ed_thumbnails_get(&app->thumbnails,engine,app->document.scene,&shutdown_asset,NULL);af_engine_request_quit(engine);
    }
}
static void shutdown(af_engine *engine,void *userdata)
{
    editor *app=userdata;
    ed_dialog_release(app->dialog);app->dialog=NULL;
    if(app->tool_process)SDL_DestroyProcess(app->tool_process);
    af_engine_set_scene(engine,NULL);af_engine_set_overlay(engine,NULL);
    ed_runtime_stop(&app->runtime);
    af_asset_watch_destroy(app->asset_watch);
    ed_thumbnails_destroy(&app->thumbnails);
    ed_gui_destroy(&app->gui);
    if(app->document_ready)ed_document_destroy(&app->document);
    af_engine_stats stats=af_engine_get_stats(engine);
    if((app->smoke_path||app->play_controls_test)&&app->failed)fputs("Editor smoke checks failed.\n",stderr);
    printf("Editor: %" PRIu64 " frames; %u validation errors.\n",stats.rendered_frames,stats.validation_errors);
}
int main(int argc,char **argv)
{
    editor app={0};af_engine_config config=af_engine_config_default();
    snprintf(app.module_path,sizeof(app.module_path),"%s",AF_GAME_MODULE_PATH);
    config.title="Afterlight Editor";config.width=1280;config.height=800;config.show_fps=true;
    for(int i=1;i<argc;++i){
        if(!strcmp(argv[i],"--help")){puts("Usage: afterlight_editor [--playground] [--project PATH] [--scene PATH] [--game LIBRARY] [--frames N] [--smoke-test PATH] [--play-controls-test] [--validation | --no-validation]");return 0;}
        if(!strcmp(argv[i],"--playground")){app.playground=true;snprintf(app.module_path,sizeof(app.module_path),"%s",AF_PLAYGROUND_MODULE_PATH);}
        else if(!strcmp(argv[i],"--light-tools-test")){app.light_test=true;if(!app.frame_limit)app.frame_limit=600;}
        else if(!strcmp(argv[i],"--show-assets"))app.show_assets=true;
        else if(!strcmp(argv[i],"--thumbnail-test")){app.thumbnail_test=true;app.show_assets=true;if(!app.frame_limit)app.frame_limit=600;}
        else if(!strcmp(argv[i],"--play-controls-test")){app.play_controls_test=true;if(!app.frame_limit)app.frame_limit=300;}
        else if(!strcmp(argv[i],"--project")&&i+1<argc)app.initial_project=argv[++i];
        else if(!strcmp(argv[i],"--wizard-test")&&i+1<argc){app.wizard_test=true;snprintf(app.wizard_folder,sizeof(app.wizard_folder),"%s-%" PRIu64,argv[++i],SDL_GetTicksNS());if(!app.frame_limit)app.frame_limit=600;}
        else if(!strcmp(argv[i],"--animation-tools-test")&&i+1<argc){app.animation_test_path=argv[++i];if(!app.frame_limit)app.frame_limit=600;}
        else if(!strcmp(argv[i],"--scene")&&i+1<argc)app.initial_scene=argv[++i];
        else if(!strcmp(argv[i],"--game")&&i+1<argc){app.module_override=true;const char *path=argv[++i];if(strlen(path)>=sizeof(app.module_path)){fputs("Game module path too long.\n",stderr);return 1;}snprintf(app.module_path,sizeof(app.module_path),"%s",path);}
        else if(!strcmp(argv[i],"--smoke-test")&&i+1<argc){app.smoke_path=argv[++i];if(!app.frame_limit)app.frame_limit=180;}
        else if(!strcmp(argv[i],"--frames")&&i+1<argc){
            const char *value=argv[++i];char *end=NULL;errno=0;unsigned long long count=strtoull(value,&end,10);
            if(errno||!value[0]||value[0]<'0'||value[0]>'9'||*end||!count){fputs("--frames needs a positive integer.\n",stderr);return 1;}
            app.frame_limit=(uint64_t)count;
        }else if(!strcmp(argv[i],"--validation"))config.enable_validation=true;
        else if(!strcmp(argv[i],"--no-validation"))config.enable_validation=false;
        else{fprintf(stderr,"Unknown/incomplete argument: %s\n",argv[i]);return 1;}
    }
    af_engine *engine=af_engine_create(&config);if(!engine)return 1;
    const char *require_validation=getenv("AFTERLIGHT_REQUIRE_VALIDATION");
    if((app.smoke_path||app.play_controls_test)&&require_validation&&!strcmp(require_validation,"1")&&!af_engine_get_stats(engine).validation_enabled){
        fputs("Editor smoke test requires validation.\n",stderr);af_engine_destroy(engine);return 1;
    }
    af_game game={.init=init,.update=update,.shutdown=shutdown,.user_data=&app,.should_close=should_close,.fixed_update=fixed_update};
    int result=af_engine_run(engine,&game);af_engine_destroy(engine);
    return app.failed?1:result;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include "log.h"
#include "renderer.h"
#include "environment_job.h"
#include "render_test.h"
#include "platform_sdl.h"
#include <math.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <string.h>

struct af_engine {
    SDL_Window *window;
    af_audio *audio;
    af_environment_job *environment_job;
    af_renderer *renderer;
    char *base_title;
    bool show_fps;
    bool has_fps_sample;
    Uint64 fps_started;
    uint64_t fps_frames;
    double frames_per_second;
    double average_frame_ms;
    double cpu_events_ms,cpu_update_ms,cpu_render_ms;
    af_color clear_color;
    const af_scene *scene;
    af_scene *previous_scene;af_camera previous_camera;af_vec3 fixed_camera_position;bool has_fixed_camera;
    uint64_t fixed_ticks;
    af_fixed_clock clock;bool simulation_paused,step_pending,in_fixed;
    bool fixed_pressed[AF_KEY_COUNT];
    af_camera camera;
    af_gameplay_status gameplay_status;
    af_rect viewport;
    af_overlay_frame overlay;
    af_object_id highlighted;
    af_vec2 mouse_position, mouse_scroll;
    bool mouse_buttons_down[AF_MOUSE_BUTTON_COUNT];
    char text_input[512];
    char *clipboard;
    bool keys[AF_KEY_COUNT];
    bool pressed[AF_KEY_COUNT];
    bool repeated[AF_KEY_COUNT];
    bool mouse_pressed[AF_MOUSE_BUTTON_COUNT];
    bool mouse_captured;
    af_vec2 mouse_delta;
    bool running;
    bool has_run;
    uint64_t focus_losses;
};

static bool engine_active;
static const SDL_Scancode key_scancodes[AF_KEY_COUNT] = {
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_SPACE, SDL_SCANCODE_R,
    SDL_SCANCODE_W, SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D,
    SDL_SCANCODE_Q, SDL_SCANCODE_E, SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_LCTRL, SDL_SCANCODE_LGUI, SDL_SCANCODE_RETURN, SDL_SCANCODE_TAB,
    SDL_SCANCODE_BACKSPACE, SDL_SCANCODE_DELETE, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_HOME, SDL_SCANCODE_END,
    SDL_SCANCODE_Z, SDL_SCANCODE_Y, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_X,
    SDL_SCANCODE_N, SDL_SCANCODE_O, SDL_SCANCODE_F, SDL_SCANCODE_F5, SDL_SCANCODE_F6
};
static const Uint8 mouse_buttons[AF_MOUSE_BUTTON_COUNT] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

static void update_fps_title(af_engine *engine)
{
    if (!engine->show_fps) return;
    char *title = NULL;
    int length;
    if (!engine->has_fps_sample)
        length = SDL_asprintf(&title, "%s | -- FPS | -- ms", engine->base_title);
    else if (engine->frames_per_second == 0)
        length = SDL_asprintf(&title, "%s | 0.0 FPS | -- ms", engine->base_title);
    else
        length = SDL_asprintf(&title, "%s | %.1f FPS | %.2f ms", engine->base_title,
            engine->frames_per_second, engine->average_frame_ms);
    if (length >= 0 && !SDL_SetWindowTitle(engine->window, title))
        af_log("warning", "FPS window title: %s", SDL_GetError());
    SDL_free(title);
}

static void update_frame_stats(af_engine *engine, double frequency, bool rendered)
{
    if (rendered) ++engine->fps_frames;
    Uint64 now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - engine->fps_started) / frequency;
    if (elapsed < 1.0) return;
    /* Use actual wall time, not the delta capped for game simulation. Skipped
       and minimized frames contribute time but never inflate the frame count. */
    engine->frames_per_second = (double)engine->fps_frames / elapsed;
    engine->average_frame_ms = engine->fps_frames ? elapsed * 1000.0 / (double)engine->fps_frames : 0;
    engine->has_fps_sample = true;
    update_fps_title(engine);
    engine->fps_frames = 0;
    engine->fps_started = now;
}

af_engine_config af_engine_config_default(void)
{
    return (af_engine_config){
        .title = "Afterlight", .width = 1280, .height = 720,
#ifdef AF_DEBUG
        .enable_validation = true
#endif
    };
}

af_engine *af_engine_create(const af_engine_config *config)
{
    af_engine_config defaults = af_engine_config_default();
    if (!config) config = &defaults;
    if (engine_active || !config->title || config->width <= 0 || config->height <= 0) {
        af_log("error", "Invalid configuration or an engine is already active.");
        return NULL;
    }
    af_engine *engine = calloc(1, sizeof(*engine));
    if (!engine) return NULL;
    engine_active = true;
    SDL_SetMainReady();
#ifdef __APPLE__
    /* Afterlight uses held keys for navigation; suppress Cocoa's accent picker. */
    SDL_SetHint(SDL_HINT_MAC_PRESS_AND_HOLD,"0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        af_log("error", "SDL initialization: %s", SDL_GetError());
        af_engine_destroy(engine);
        return NULL;
    }
#ifdef __APPLE__
    /* A self-contained export carries its Vulkan loader and MoltenVK ICD. */
    {const char *base=SDL_GetBasePath();char path[AF_ASSET_PATH_CAPACITY];SDL_PathInfo info;
        if(base&&snprintf(path,sizeof(path),"%svulkan/icd.json",base)<(int)sizeof(path)&&SDL_GetPathInfo(path,&info)&&info.type==SDL_PATHTYPE_FILE){
            if(!SDL_getenv("VK_DRIVER_FILES")&&!SDL_getenv("VK_ICD_FILENAMES"))SDL_setenv_unsafe("VK_DRIVER_FILES",path,0);
            if(snprintf(path,sizeof(path),"%s../Frameworks/libvulkan.1.dylib",base)<(int)sizeof(path))SDL_SetHint(SDL_HINT_VULKAN_LIBRARY,path);
        }}
#endif
    engine->base_title = SDL_strdup(config->title);
    if (!engine->base_title) {
        af_log("error", "Allocating window title: %s", SDL_GetError());
        af_engine_destroy(engine);
        return NULL;
    }
    engine->show_fps = config->show_fps;
    engine->window = SDL_CreateWindow(config->title, config->width, config->height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!engine->window) {
        af_log("error", "Window creation: %s", SDL_GetError());
        af_engine_destroy(engine);
        return NULL;
    }
    engine->renderer = af_renderer_create(engine->window, config->enable_validation);
    if (!engine->renderer) {
        af_engine_destroy(engine);
        return NULL;
    }
    engine->clear_color = (af_color){0.035f, 0.025f, 0.07f, 1.0f};
    engine->camera = af_camera_default();
    update_fps_title(engine);
    return engine;
}

int af_engine_run(af_engine *engine, const af_game *game)
{
    if (!engine || !game || engine->has_run) return 1;
    engine->has_run = true;
    engine->running = true;
    int result = 0;
    if (game->init && !game->init(engine, game->user_data)) {
        result = 1;
        engine->running = false;
    }
    /* Compile pipelines and upload game geometry before exposing the window.
       Show before acquisition: hidden surfaces need not guarantee progress. */
    if (engine->running && !SDL_ShowWindow(engine->window)) {
        af_log("error", "Showing window: %s", SDL_GetError());
        result = 1;
        engine->running = false;
    }
    Uint64 previous = SDL_GetPerformanceCounter();
    const double frequency = (double)SDL_GetPerformanceFrequency();
    engine->fps_started = previous;
    while (engine->running) {
        Uint64 event_start=SDL_GetPerformanceCounter();
        memset(engine->pressed, 0, sizeof(engine->pressed));
        memset(engine->repeated, 0, sizeof(engine->repeated));
        memset(engine->mouse_pressed, 0, sizeof(engine->mouse_pressed));
        engine->mouse_delta = (af_vec2){0};
        engine->mouse_scroll = (af_vec2){0};
        engine->text_input[0] = 0;
        af_vec2 button_position = {0};
        bool button_position_changed = false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                for (int i = 0; i < AF_KEY_COUNT; ++i) {
                    if (event.key.scancode == key_scancodes[i]) {
                        engine->keys[i] = event.type == SDL_EVENT_KEY_DOWN;
                        if (event.type == SDL_EVENT_KEY_DOWN) {
                            if(event.key.repeat)engine->repeated[i]=true;else {engine->pressed[i]=true;engine->fixed_pressed[i]=true;}
                        }
                    }
                }
            }
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                size_t used = strlen(engine->text_input), added = strlen(event.text.text);
                if (used + added < sizeof(engine->text_input)) memcpy(engine->text_input + used, event.text.text, added + 1);
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                engine->mouse_scroll.x += event.wheel.x;
                engine->mouse_scroll.y += event.wheel.y;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION)
                engine->mouse_position = (af_vec2){event.motion.x,event.motion.y};
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    button_position = (af_vec2){event.button.x,event.button.y};
                    button_position_changed = true;
                }
                for (int i = 0; i < AF_MOUSE_BUTTON_COUNT; ++i)
                    if (event.button.button == mouse_buttons[i]) engine->mouse_buttons_down[i] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                for (int i = 0; i < AF_MOUSE_BUTTON_COUNT; ++i)
                    if (event.button.button == mouse_buttons[i]) engine->mouse_pressed[i] = true;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && engine->mouse_captured) {
                engine->mouse_delta.x += event.motion.xrel;
                engine->mouse_delta.y += event.motion.yrel;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                ++engine->focus_losses;
                memset(engine->mouse_buttons_down, 0, sizeof(engine->mouse_buttons_down));
                memset(engine->keys, 0, sizeof(engine->keys));
                memset(engine->pressed, 0, sizeof(engine->pressed));
                memset(engine->repeated, 0, sizeof(engine->repeated));
                memset(engine->fixed_pressed,0,sizeof(engine->fixed_pressed));
                memset(engine->mouse_pressed, 0, sizeof(engine->mouse_pressed));
                (void)af_engine_set_mouse_capture(engine, false);
                af_engine_stop_sounds(engine);
            }
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (!game->should_close || game->should_close(engine, game->user_data)) engine->running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_RESTORED)
                af_renderer_resize(engine->renderer);
        }
        if (!engine->running) break;
        if (button_position_changed) engine->mouse_position = button_position;
        engine->overlay = (af_overlay_frame){0};
        Uint64 now = SDL_GetPerformanceCounter();
        double delta = (double)(now - previous) / frequency;
        previous = now;
        double elapsed=delta;
        if (delta > 0.1) delta = 0.1;
        Uint64 update_start=SDL_GetPerformanceCounter();
        engine->cpu_events_ms=(double)(update_start-event_start)*1000/frequency;
        const char *environment_name;const af_hdr_file *environment_source;const af_ibl_images *environment_images;
        if(af_environment_job_result(engine->environment_job,&environment_name,&environment_source,&environment_images)){
            Uint64 started=SDL_GetPerformanceCounter();bool uploaded=af_renderer_upload_environment_baked(engine->renderer,environment_name,environment_source,environment_images);
            af_environment_job_finish(engine->environment_job,uploaded,(double)(SDL_GetPerformanceCounter()-started)*1000/frequency);
        }
        if (game->update) game->update(engine, delta, game->user_data);
        if(game->fixed_update){unsigned steps=af_fixed_clock_advance(&engine->clock,elapsed,engine->simulation_paused,engine->step_pending);engine->step_pending=false;
            for(unsigned tick=0;tick<steps&&engine->running;++tick){
                af_scene *snapshot=engine->scene?af_scene_clone(engine->scene):NULL;
                if(engine->scene&&!snapshot){result=1;engine->running=false;break;}
                af_scene_destroy(engine->previous_scene);engine->previous_scene=snapshot;engine->previous_camera=engine->camera;
                engine->in_fixed=true;game->fixed_update(engine,AF_FIXED_DELTA,game->user_data);engine->in_fixed=false;++engine->fixed_ticks;engine->fixed_camera_position=engine->camera.position;engine->has_fixed_camera=true;
                memset(engine->fixed_pressed,0,sizeof(engine->fixed_pressed));
            }
        }
        Uint64 render_start=SDL_GetPerformanceCounter();
        engine->cpu_update_ms=(double)(render_start-update_start)*1000/frequency;
        engine->cpu_render_ms=0;
        if (!engine->running) break;
        if (SDL_GetWindowFlags(engine->window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(16);
            update_frame_stats(engine, frequency, false);
            continue;
        }
        bool rendered = false;
        float alpha=game->fixed_update&&!engine->simulation_paused?(float)engine->clock.alpha:1;
        af_camera render_camera=engine->camera;
        if(engine->previous_scene&&engine->has_fixed_camera&&af_vec3_length(af_vec3_sub(engine->camera.position,engine->fixed_camera_position))<0.000001f)render_camera.position=af_vec3_add(engine->previous_camera.position,af_vec3_scale(af_vec3_sub(engine->camera.position,engine->previous_camera.position),alpha));
        af_renderer_set_interpolation(engine->renderer,engine->previous_scene,alpha);
        if (!af_renderer_draw(engine->renderer, engine->clear_color, engine->scene, &render_camera, engine->viewport, engine->highlighted, &engine->overlay, &rendered)) {
            result = 1;
            break;
        }
        engine->cpu_render_ms=(double)(SDL_GetPerformanceCounter()-render_start)*1000/frequency;
        if (!rendered) SDL_Delay(16);
        update_frame_stats(engine, frequency, rendered);
    }
    engine->running = false;
    (void)af_engine_set_mouse_capture(engine, false);
    if (!af_renderer_wait_idle(engine->renderer)) result = 1;
    if (game->shutdown) game->shutdown(engine, game->user_data);
    if (af_renderer_stats(engine->renderer).validation_errors != 0) result = 1;
    return result;
}

void af_engine_destroy(af_engine *engine)
{
    if (!engine) return;
    af_environment_job_destroy(engine->environment_job);
    af_scene_destroy(engine->previous_scene);
    af_audio_destroy(engine->audio);
    af_renderer_destroy(engine->renderer);
    SDL_DestroyWindow(engine->window);
    SDL_free(engine->base_title);
    SDL_free(engine->clipboard);
    SDL_Quit();
    free(engine);
    engine_active = false;
}

void af_engine_request_quit(af_engine *engine)
{
    if (engine) engine->running = false;
}

const char *af_engine_get_base_path(const af_engine *engine)
{
    return engine ? SDL_GetBasePath() : NULL;
}

void af_engine_set_clear_color(af_engine *engine, af_color color)
{
    if (engine) engine->clear_color = color;
}

bool af_engine_upload_mesh(af_engine *engine, const char *name, const af_mesh_data *mesh)
{
    return engine && af_renderer_upload_mesh(engine->renderer, name, mesh);
}

bool af_engine_remove_mesh(af_engine *engine, const char *name)
{
    return engine && af_renderer_remove_mesh(engine->renderer, name);
}

void af_engine_set_scene(af_engine *engine, const af_scene *scene)
{
    if (engine) {if(engine->scene!=scene)af_engine_reset_interpolation(engine);engine->scene = scene;}
}

bool af_engine_set_window_size(af_engine *engine, int width, int height)
{
    if (!engine || width <= 0 || height <= 0) return false;
    if (!SDL_SetWindowSize(engine->window, width, height)) return false;
    af_renderer_resize(engine->renderer);
    return true;
}

bool af_engine_set_camera(af_engine *engine, const af_camera *camera)
{
    if (!engine || !af_camera_is_valid(camera)) return false;
    engine->camera = *camera;
    return true;
}

bool af_engine_key_down(const af_engine *engine, af_key key)
{
    return engine && key >= 0 && key < AF_KEY_COUNT && engine->keys[key];
}

bool af_engine_key_pressed(const af_engine *engine, af_key key)
{
    return engine && key >= 0 && key < AF_KEY_COUNT && (engine->in_fixed?engine->fixed_pressed[key]:engine->pressed[key]);
}

bool af_engine_mouse_pressed(const af_engine *engine, af_mouse_button button)
{
    return engine && button >= 0 && button < AF_MOUSE_BUTTON_COUNT && engine->mouse_pressed[button];
}

af_vec2 af_engine_mouse_delta(const af_engine *engine)
{
    return engine ? engine->mouse_delta : (af_vec2){0};
}

bool af_engine_set_mouse_capture(af_engine *engine, bool enabled)
{
    if (!engine) return false;
    if(engine->mouse_captured==enabled)return true;
    if (!SDL_SetWindowRelativeMouseMode(engine->window, enabled)) {
        af_log("error", "Mouse capture: %s", SDL_GetError());
        return false;
    }
    engine->mouse_captured = enabled;
    engine->mouse_delta = (af_vec2){0};
    return true;
}

bool af_engine_mouse_captured(const af_engine *engine)
{
    return engine && engine->mouse_captured;
}

af_engine_stats af_engine_get_stats(const af_engine *engine)
{
    if (!engine) return (af_engine_stats){0};
    af_engine_stats stats = af_renderer_stats(engine->renderer);
    stats.frames_per_second = engine->frames_per_second;
    stats.average_frame_ms = engine->average_frame_ms;
    stats.simulation_ticks=engine->fixed_ticks;stats.simulation_steps=engine->clock.steps;stats.simulation_alpha=engine->clock.alpha;stats.simulation_dropped_seconds=engine->clock.dropped_seconds;stats.simulation_paused=engine->simulation_paused;
    stats.cpu_events_ms=engine->cpu_events_ms;stats.cpu_update_ms=engine->cpu_update_ms;stats.cpu_render_ms=engine->cpu_render_ms;
    return stats;
}

SDL_Window *af_engine_sdl_window(af_engine *engine) { return engine ? engine->window : NULL; }

bool af_engine_set_title(af_engine *engine, const char *title)
{
    if (!engine || !title) return false;
    char *copy = SDL_strdup(title);
    if (!copy) return false;
    SDL_free(engine->base_title);
    engine->base_title = copy;
    if (engine->show_fps) { update_fps_title(engine); return true; }
    return SDL_SetWindowTitle(engine->window, copy);
}

void af_engine_get_window_size(const af_engine *engine, int *width, int *height)
{
    int w = 0, h = 0;
    if (engine) (void)SDL_GetWindowSize(engine->window, &w, &h);
    if (width) *width = w;
    if (height) *height = h;
}

void af_engine_set_viewport(af_engine *engine, const af_rect *viewport)
{
    if (engine) engine->viewport = viewport ? *viewport : (af_rect){0};
}

bool af_engine_upload_overlay_atlas(af_engine *engine, const unsigned char *rgba, uint32_t width, uint32_t height)
{
    return engine && af_renderer_upload_overlay_atlas(engine->renderer, rgba, width, height);
}

bool af_engine_set_overlay(af_engine *engine, const af_overlay_frame *frame)
{
    if (!engine) return false;
    if (!frame) { engine->overlay = (af_overlay_frame){0}; return true; }
    if (!frame->vertices || !frame->indices || !frame->commands || frame->width <= 0 || frame->height <= 0 ||
        !isfinite(frame->width) || !isfinite(frame->height) || frame->vertex_count > 1048576 ||
        frame->index_count > 3145728 || frame->command_count > 65536) return false;
    for (uint32_t i = 0; i < frame->index_count; ++i) if (frame->indices[i] >= frame->vertex_count) return false;
    for (uint32_t i = 0; i < frame->command_count; ++i) {
        const af_overlay_command *command = &frame->commands[i];
        if (command->first_index > frame->index_count || command->index_count > frame->index_count - command->first_index ||
            !isfinite(command->clip.x) || !isfinite(command->clip.y) || !isfinite(command->clip.width) || !isfinite(command->clip.height)) return false;
    }
    engine->overlay = *frame;
    return true;
}

af_vec2 af_engine_mouse_position(const af_engine *engine) { return engine ? engine->mouse_position : (af_vec2){0}; }
af_vec2 af_engine_mouse_scroll(const af_engine *engine) { return engine ? engine->mouse_scroll : (af_vec2){0}; }
bool af_engine_mouse_down(const af_engine *engine, af_mouse_button button)
{
    return engine && button >= 0 && button < AF_MOUSE_BUTTON_COUNT && engine->mouse_buttons_down[button];
}
bool af_engine_set_text_input(af_engine *engine, bool enabled)
{
    return engine && (enabled ? SDL_StartTextInput(engine->window) : SDL_StopTextInput(engine->window));
}
const char *af_engine_text_input(const af_engine *engine) { return engine ? engine->text_input : ""; }
bool af_engine_set_clipboard_text(af_engine *engine, const char *text)
{
    return engine && text && SDL_SetClipboardText(text);
}
const char *af_engine_get_clipboard_text(af_engine *engine)
{
    if (!engine) return "";
    SDL_free(engine->clipboard);
    engine->clipboard = SDL_GetClipboardText();
    return engine->clipboard ? engine->clipboard : "";
}
af_object_id af_engine_pick_object(af_engine *engine, af_vec2 point)
{
    return engine ? af_renderer_pick_object(engine->renderer, engine->scene, &engine->camera, engine->viewport, point) : AF_OBJECT_NONE;
}
void af_engine_highlight_object(af_engine *engine, af_object_id id) { if (engine) engine->highlighted = id; }

bool af_engine_load_mesh(af_engine *engine,const char *name,const char *path,af_asset_error *error){
    af_mesh_file mesh;if(!af_mesh_file_load(path,&mesh,error))return false;
    bool result=af_engine_upload_mesh(engine,name,&mesh.data);af_mesh_file_free(&mesh);
    if(!result&&error)snprintf(error->message,sizeof(error->message),"GPU mesh upload failed.");return result;
}
bool af_engine_load_scene_assets(af_engine *engine,const af_scene *scene,af_asset_error *error){
    if(!engine||!scene){if(error)snprintf(error->message,sizeof(error->message),"An engine and scene are required.");return false;}
    for(size_t i=0;i<af_scene_asset_count(scene);++i)if(!af_engine_load_asset(engine,af_scene_asset_at(scene,i),error))return false;return true;
}

bool af_engine_upload_texture(af_engine *engine,const char *name,const unsigned char *rgba,uint32_t width,uint32_t height){return engine&&af_renderer_upload_texture(engine->renderer,name,rgba,width,height);}
bool af_engine_load_texture(af_engine *engine,const char *name,const char *path,af_asset_error *error){
    af_image_file image;if(!af_image_file_load(path,&image,error))return false;
    bool result=af_engine_upload_texture(engine,name,image.rgba,image.width,image.height);af_image_file_free(&image);
    if(!result&&error)snprintf(error->message,sizeof(error->message),"GPU texture upload failed.");return result;
}

bool af_engine_remove_texture(af_engine *engine,const char *name){return engine&&af_renderer_remove_texture(engine->renderer,name);}

bool af_engine_key_repeated(const af_engine *engine,af_key key){return engine&&key>=0&&key<AF_KEY_COUNT&&engine->repeated[key];}

bool af_engine_test_capture_pixel(af_engine *engine,af_vec2 position){return engine&&af_renderer_test_capture_pixel(engine->renderer,position);}
bool af_engine_test_read_pixel(af_engine *engine,unsigned char rgba[4]){return engine&&af_renderer_test_read_pixel(engine->renderer,rgba);}

bool af_engine_texture_info(const af_engine *engine,const char *name,af_texture_info *out){return engine&&af_renderer_texture_info(engine->renderer,name,out);}

bool af_engine_upload_normal_map(af_engine *engine,const char *name,const unsigned char *rgba,uint32_t width,uint32_t height){return engine&&af_renderer_upload_normal_map(engine->renderer,name,rgba,width,height);}
bool af_engine_load_normal_map(af_engine *engine,const char *name,const char *path,af_asset_error *error){
    af_image_file image;if(!af_image_file_load(path,&image,error))return false;
    bool result=af_engine_upload_normal_map(engine,name,image.rgba,image.width,image.height);af_image_file_free(&image);
    if(!result&&error)snprintf(error->message,sizeof(error->message),"GPU normal-map upload failed.");return result;
}

void af_engine_reset_interpolation(af_engine *engine){if(!engine)return;af_scene_destroy(engine->previous_scene);engine->previous_scene=NULL;engine->previous_camera=engine->camera;engine->has_fixed_camera=false;if(engine->renderer)af_renderer_set_interpolation(engine->renderer,NULL,1);}
void af_engine_set_simulation_paused(af_engine *engine,bool paused){if(!engine||engine->simulation_paused==paused)return;engine->simulation_paused=paused;if(paused)af_engine_stop_sounds(engine);engine->clock.accumulator=0;memset(engine->fixed_pressed,0,sizeof(engine->fixed_pressed));af_engine_reset_interpolation(engine);}
bool af_engine_step_simulation(af_engine *engine){if(!engine||!engine->simulation_paused)return false;engine->step_pending=true;return true;}

bool af_engine_mouse_in_viewport(const af_engine *engine){
    if(!engine)return false;int width,height;SDL_GetWindowSize(engine->window,&width,&height);af_rect area=engine->viewport;
    if(area.width<=0||area.height<=0)area=(af_rect){0,0,(float)width,(float)height};af_vec2 p=engine->mouse_position;
    return p.x>=area.x&&p.y>=area.y&&p.x<area.x+area.width&&p.y<area.y+area.height;
}

af_camera af_engine_get_camera(const af_engine *engine){return engine?engine->camera:af_camera_default();}

af_gameplay_status af_engine_gameplay_status(const af_engine *engine){return engine?engine->gameplay_status:(af_gameplay_status){0};}
void af_engine_set_gameplay_status(af_engine *engine,af_gameplay_status status){if(engine)engine->gameplay_status=status;}

bool af_engine_load_sound(af_engine *engine,const char *name,const char *path,af_asset_error *error){if(!engine)return false;if(!engine->audio)engine->audio=af_audio_create();return af_audio_load(engine->audio,name,path,error);}
bool af_engine_play_sound(af_engine *engine,const char *name,float gain){return engine&&af_audio_play(engine->audio,name,gain);}
void af_engine_stop_sounds(af_engine *engine){if(engine)af_audio_stop(engine->audio);}
af_audio_stats af_engine_audio_stats(const af_engine *engine){return af_audio_get_stats(engine?engine->audio:NULL);}
uint64_t af_engine_test_focus_losses(const af_engine *engine){return engine?engine->focus_losses:0;}

bool af_engine_load_asset(af_engine *engine,const af_scene_asset *asset,af_asset_error *error){
    if(!engine||!asset)return false;
    switch(asset->kind){case AF_ASSET_MESH:return af_engine_load_mesh(engine,asset->name,asset->path,error);
        case AF_ASSET_TEXTURE:return af_engine_load_texture(engine,asset->name,asset->path,error);
        case AF_ASSET_NORMAL_MAP:case AF_ASSET_METALLIC_ROUGHNESS:return af_engine_load_normal_map(engine,asset->name,asset->path,error);
        case AF_ASSET_ENVIRONMENT:return af_engine_load_environment(engine,asset->name,asset->path,error);
        case AF_ASSET_AUDIO:return af_engine_load_sound(engine,asset->name,asset->path,error);default:return false;}
}
bool af_engine_reload_asset_source(af_engine *engine,const af_scene *scene,const char *source,af_asset_error *error){
    if(!engine||!scene||!source)return false;
    typedef struct staged_asset {char temporary[64];af_scene_asset asset;} staged_asset;
    staged_asset *staged=calloc(af_scene_asset_count(scene)?af_scene_asset_count(scene):1,sizeof(*staged));if(!staged)return false;size_t count=0;bool ok=true,matched=false;static unsigned sequence;
    for(size_t i=0;i<af_scene_asset_count(scene);++i){const af_scene_asset *a=af_scene_asset_at(scene,i);size_t size=strcspn(a->path,"#");if(strlen(source)!=size||strncmp(source,a->path,size))continue;
        matched=true;
        if(a->kind==AF_ASSET_AUDIO){ok=af_engine_load_asset(engine,a,error);if(!ok)break;continue;}
        staged_asset *entry=&staged[count++];entry->asset=*a;do{snprintf(entry->temporary,sizeof(entry->temporary),"__reload_%u_%zu",++sequence,i);}while(af_scene_find_asset(scene,entry->temporary));af_scene_asset temporary=*a;snprintf(temporary.name,sizeof(temporary.name),"%s",entry->temporary);
        if(!af_engine_load_asset(engine,&temporary,error)){ok=false;break;}
    }
    if(ok)for(size_t i=0;i<count;++i)if(!af_renderer_commit_asset(engine->renderer,staged[i].temporary,staged[i].asset.name,staged[i].asset.kind==AF_ASSET_MESH)){ok=false;break;}
    for(size_t i=0;i<count;++i){if(staged[i].asset.kind==AF_ASSET_MESH)af_engine_remove_mesh(engine,staged[i].temporary);else af_engine_remove_texture(engine,staged[i].temporary);}
    if(!matched&&error)snprintf(error->message,sizeof(error->message),"No assets reference this source.");free(staged);return ok&&matched;
}

bool af_engine_upload_overlay_texture(af_engine *engine,uint32_t id,const unsigned char *rgba,uint32_t width,uint32_t height){return engine&&af_renderer_upload_overlay_texture(engine->renderer,id,rgba,width,height);}
bool af_engine_remove_overlay_texture(af_engine *engine,uint32_t id){return engine&&af_renderer_remove_overlay_texture(engine->renderer,id);}

bool af_engine_load_environment(af_engine *engine,const char *name,const char *path,af_asset_error *error){
    af_hdr_file image;if(!engine||!af_hdr_file_load(path,&image,error))return false;
    bool result=af_renderer_upload_environment(engine->renderer,name,image.rgba,image.width,image.height);af_hdr_file_free(&image);
    if(!result&&error)snprintf(error->message,sizeof(error->message),"Environment upload/convolution failed; previous data retained.");return result;
}

bool af_engine_start_environment(af_engine *engine,const char *name,const char *path,af_asset_error *error){
    if(error)*error=(af_asset_error){0};if(!engine)return false;
    af_environment_job_state state=af_environment_job_status_get(engine->environment_job).state;
    if(state==AF_ENV_LOADING||state==AF_ENV_BAKING||state==AF_ENV_UPLOADING){if(error)snprintf(error->message,sizeof(error->message),"An HDR import is already running. Cancel it first.");return false;}
    af_environment_job *job=af_environment_job_create(name,path);if(!job){if(error)snprintf(error->message,sizeof(error->message),"Cannot start HDR worker (invalid name/path or thread allocation).");return false;}
    af_environment_job_destroy(engine->environment_job);engine->environment_job=job;return true;
}
void af_engine_cancel_environment(af_engine *engine){if(engine)af_environment_job_cancel(engine->environment_job);}
af_environment_job_status af_engine_environment_status(const af_engine *engine){return af_environment_job_status_get(engine?engine->environment_job:NULL);}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/afterlight.h>
#include <afterlight/extension_vulkan.h>
#include "log.h"
#include "render/renderer.h"
#include "render/environment_job.h"
#include "extension_runtime.h"
#include "animation/animation_runtime.h"
#include "render/render_test.h"
#include "platform_sdl.h"
#include "scene/scene_internal.h"
#include <afterlight/prefab.h>
#include <math.h>

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdlib.h>
#include <string.h>

struct af_engine {
    SDL_Gamepad *gamepad;
    af_input_map input_map;
    float action_values[32];
    bool action_pressed[32], fixed_actions[32];
    SDL_Window *window;
    af_physics *physics;
    double cpu_physics_ms;
    bool fixed_active;
    af_audio *audio;
    bool audio_focus_lost;
    af_environment_job *environment_job;
    af_animation_runtime *animations;
    bool animation_enabled, animation_real_time;
    af_renderer *renderer;
    af_extensions extensions;
    char *base_title;
    bool show_fps;
    bool has_fps_sample;
    Uint64 fps_started;
    uint64_t fps_frames;
    double frames_per_second;
    double average_frame_ms;
    double cpu_events_ms, cpu_update_ms, cpu_render_ms, cpu_animation_ms;
    af_color clear_color;
    const af_scene *scene;
    af_scene *previous_scene;
    af_camera previous_camera;
    af_vec3 fixed_camera_position;
    bool has_fixed_camera;
    uint64_t fixed_ticks;
    af_fixed_clock clock;
    bool simulation_paused, step_pending, in_fixed;
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
    SDL_SCANCODE_ESCAPE, SDL_SCANCODE_SPACE, SDL_SCANCODE_R,      SDL_SCANCODE_W,   SDL_SCANCODE_A,
    SDL_SCANCODE_S,      SDL_SCANCODE_D,     SDL_SCANCODE_Q,      SDL_SCANCODE_E,   SDL_SCANCODE_LSHIFT,
    SDL_SCANCODE_LCTRL,  SDL_SCANCODE_LGUI,  SDL_SCANCODE_RETURN, SDL_SCANCODE_TAB, SDL_SCANCODE_BACKSPACE,
    SDL_SCANCODE_DELETE, SDL_SCANCODE_LEFT,  SDL_SCANCODE_RIGHT,  SDL_SCANCODE_UP,  SDL_SCANCODE_DOWN,
    SDL_SCANCODE_HOME,   SDL_SCANCODE_END,   SDL_SCANCODE_Z,      SDL_SCANCODE_Y,   SDL_SCANCODE_C,
    SDL_SCANCODE_V,      SDL_SCANCODE_X,     SDL_SCANCODE_N,      SDL_SCANCODE_O,   SDL_SCANCODE_F,
    SDL_SCANCODE_F5,     SDL_SCANCODE_F6};
static const Uint8 mouse_buttons[AF_MOUSE_BUTTON_COUNT] = {SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE};

static void update_fps_title(af_engine *engine) {
    if (!engine->show_fps)
        return;
    char *title = NULL;
    int length;
    if (!engine->has_fps_sample)
        length = SDL_asprintf(&title, "%s | -- FPS | -- ms", engine->base_title);
    else if (engine->frames_per_second == 0)
        length = SDL_asprintf(&title, "%s | 0.0 FPS | -- ms", engine->base_title);
    else
        length = SDL_asprintf(&title, "%s | %.1f FPS | %.2f ms", engine->base_title, engine->frames_per_second,
                              engine->average_frame_ms);
    if (length >= 0 && !SDL_SetWindowTitle(engine->window, title))
        af_log("warning", "FPS window title: %s", SDL_GetError());
    SDL_free(title);
}

static void update_frame_stats(af_engine *engine, double frequency, bool rendered) {
    if (rendered)
        ++engine->fps_frames;
    Uint64 now = SDL_GetPerformanceCounter();
    double elapsed = (double)(now - engine->fps_started) / frequency;
    if (elapsed < 1.0)
        return;
    /* Use actual wall time, not the delta capped for game simulation. Skipped
       and minimized frames contribute time but never inflate the frame count. */
    engine->frames_per_second = (double)engine->fps_frames / elapsed;
    engine->average_frame_ms = engine->fps_frames ? elapsed * 1000.0 / (double)engine->fps_frames : 0;
    engine->has_fps_sample = true;
    update_fps_title(engine);
    engine->fps_frames = 0;
    engine->fps_started = now;
}

af_engine_config af_engine_config_default(void) {
    return (af_engine_config){.title = "Afterlight",
                              .width = 1280,
                              .height = 720,
#ifdef AF_DEBUG
                              .enable_validation = true
#endif
    };
}

af_engine *af_engine_create_with_extensions(const af_engine_config *config, const char *const *paths, size_t count) {
    af_engine_config defaults = af_engine_config_default();
    if (!config)
        config = &defaults;
    if (engine_active || !config->title || config->width <= 0 || config->height <= 0 || count > AF_EXTENSION_LIMIT ||
        (count && !paths)) {
        af_log("error", "Invalid configuration or an engine is already active.");
        return NULL;
    }
    af_engine *engine = calloc(1, sizeof(*engine));
    if (!engine)
        return NULL;
    engine_active = true;
    SDL_SetMainReady();
#ifdef __APPLE__
    /* Afterlight uses held keys for navigation; suppress Cocoa's accent picker. */
    SDL_SetHint(SDL_HINT_MAC_PRESS_AND_HOLD, "0");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        af_log("error", "SDL initialization: %s", SDL_GetError());
        af_engine_destroy(engine);
        return NULL;
    }
#ifdef __APPLE__
    /* A self-contained export carries its Vulkan loader and MoltenVK ICD. */
    {
        const char *base = SDL_GetBasePath();
        char path[AF_ASSET_PATH_CAPACITY];
        SDL_PathInfo info;
        if (base && snprintf(path, sizeof(path), "%svulkan/icd.json", base) < (int)sizeof(path) &&
            SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE) {
            if (!SDL_getenv("VK_DRIVER_FILES") && !SDL_getenv("VK_ICD_FILENAMES"))
                SDL_setenv_unsafe("VK_DRIVER_FILES", path, 0);
            if (snprintf(path, sizeof(path), "%s../Frameworks/libvulkan.1.dylib", base) < (int)sizeof(path))
                SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, path);
        }
    }
#endif
    engine->base_title = SDL_strdup(config->title);
    if (!engine->base_title) {
        af_log("error", "Allocating window title: %s", SDL_GetError());
        af_engine_destroy(engine);
        return NULL;
    }
    engine->show_fps = config->show_fps;
    engine->window =
        SDL_CreateWindow(config->title, config->width, config->height,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if (!engine->window) {
        af_log("error", "Window creation: %s", SDL_GetError());
        af_engine_destroy(engine);
        return NULL;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!af_extensions_load(&engine->extensions, paths[i])) {
            af_engine_destroy(engine);
            return NULL;
        }
    }
    engine->renderer = af_renderer_create(engine->window, config->enable_validation, &engine->extensions);
    if (!engine->renderer) {
        af_engine_destroy(engine);
        return NULL;
    }
    engine->physics = af_physics_create();
    if (!engine->physics) {
        af_engine_destroy(engine);
        return NULL;
    }
    af_physics_set_profiling(engine->physics, true);
    engine->input_map = af_input_map_default();
    engine->animations = af_animation_runtime_create(engine->renderer);
    if (!engine->animations) {
        af_engine_destroy(engine);
        return NULL;
    }
    engine->animation_enabled = true;
    engine->clear_color = (af_color){0.035f, 0.025f, 0.07f, 1.0f};
    engine->camera = af_camera_default();
    for (size_t i = 0; i < count; ++i)
        (void)af_engine_set_extension_enabled(engine, i, true);
    update_fps_title(engine);
    return engine;
}

af_engine *af_engine_create(const af_engine_config *config) {
    return af_engine_create_with_extensions(config, NULL, 0);
}

static void update_actions(af_engine *e, double dt) {
    if (e->gamepad && !SDL_GamepadConnected(e->gamepad)) {
        SDL_CloseGamepad(e->gamepad);
        e->gamepad = NULL;
    }
    if (!e->gamepad) {
        int count = 0;
        SDL_JoystickID *ids = SDL_GetGamepads(&count);
        if (count)
            e->gamepad = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }
    af_input_sample sample = {0};
    bool focused = (SDL_GetWindowFlags(e->window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (focused) {
        memcpy(sample.keys, e->keys, sizeof(sample.keys));
        sample.mouse = e->mouse_delta;
        if (e->gamepad) {
            for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT && i < 32; ++i)
                sample.buttons[i] = SDL_GetGamepadButton(e->gamepad, (SDL_GamepadButton)i);
            for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT && i < 8; ++i) {
                Sint16 axis = SDL_GetGamepadAxis(e->gamepad, (SDL_GamepadAxis)i);
                sample.axes[i] = (float)axis / (axis < 0 ? 32768.f : 32767.f);
            }
        }
    }
    for (size_t i = 0; i < e->input_map.count; ++i) {
        af_action_binding b = e->input_map.bindings[i];
        float value = af_input_value(&e->input_map, &sample, b.name);
        if (b.mouse_axis >= 0 && (b.mouse_axis == 0 ? sample.mouse.x : sample.mouse.y) == 0)
            value *= (float)dt * 900;
        bool tap = focused && b.positive_key >= 0 && e->pressed[b.positive_key];
        e->action_pressed[i] = (fabsf(e->action_values[i]) < .5f && fabsf(value) >= .5f) || tap;
        if (e->action_pressed[i])
            e->fixed_actions[i] = true;
        e->action_values[i] = value;
        if (!focused)
            e->fixed_actions[i] = false;
    }
}

bool af_engine_load_extension(af_engine *engine, const char *path) {
    if (!engine || engine->in_fixed || (engine->has_run && !engine->running)) {
        af_log("error", "Load engine extensions at startup or from a frame update.");
        return false;
    }
    if (!af_extensions_load(&engine->extensions, path))
        return false;
    if (engine->extensions.modules[engine->extensions.count - 1].startup)
        return true;
    return af_engine_set_extension_enabled(engine, engine->extensions.count - 1, true);
}

size_t af_engine_extension_count(const af_engine *engine) {
    return engine ? engine->extensions.count : 0;
}

bool af_engine_get_extension(const af_engine *engine, size_t index, af_extension_info *info) {
    if (!engine || !info || index >= engine->extensions.count)
        return false;
    const af_loaded_extension *module = &engine->extensions.modules[index];
    *info = (af_extension_info){
        .id = module->api->id,
        .path = module->path,
        .enabled = module->enabled,
        .startup_status = module->startup_status,
        .vulkan_upscaler = module->upscaler != NULL,
        .upscaler_active = module->enabled && module->upscaler && af_renderer_upscaler_active(engine->renderer),
    };
    return true;
}

bool af_engine_set_extension_enabled(af_engine *engine, size_t index, bool enabled) {
    if (!engine || engine->in_fixed || index >= engine->extensions.count)
        return false;
    af_loaded_extension *module = &engine->extensions.modules[index];
    if (enabled && module->startup && module->startup_status != AF_EXTENSION_STARTUP_READY)
        return false;
    if (module->enabled == enabled)
        return true;
    if (module->upscaler && enabled) {
        for (size_t i = 0; i < engine->extensions.count; ++i)
            if (engine->extensions.modules[i].upscaler)
                engine->extensions.modules[i].enabled = false;
        af_renderer_set_upscaler(engine->renderer, module->upscaler, module->state);
    } else if (module->upscaler) {
        (void)af_renderer_set_upscaler_enabled(engine->renderer, false);
    }
    module->enabled = enabled;
    return true;
}

bool af_engine_set_upscaler_enabled(af_engine *engine, bool enabled) {
    return engine && af_renderer_set_upscaler_enabled(engine->renderer, enabled);
}

uint32_t af_engine_upscaler_quality_count(const af_engine *engine) {
    return engine ? af_renderer_upscaler_quality_count(engine->renderer) : 0;
}

const char *af_engine_upscaler_quality_name(const af_engine *engine, uint32_t index) {
    return engine ? af_renderer_upscaler_quality_name(engine->renderer, index) : NULL;
}

uint32_t af_engine_upscaler_quality(const af_engine *engine) {
    return engine ? af_renderer_upscaler_quality(engine->renderer) : 0;
}

bool af_engine_set_upscaler_quality(af_engine *engine, uint32_t index) {
    return engine && af_renderer_set_upscaler_quality(engine->renderer, index);
}

int af_engine_run(af_engine *engine, const af_game *game) {
    if (!engine || !game || engine->has_run)
        return 1;
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
    const SDL_WindowID window_id = SDL_GetWindowID(engine->window);
    /* Wayland may report desktop minimization through occlusion or only by
       withdrawing input focus. Once focused, wait for activation again. */
    const bool wayland = !strcmp(SDL_GetCurrentVideoDriver(), "wayland");
    const SDL_WindowFlags suspend_flags = SDL_WINDOW_MINIMIZED | (wayland ? SDL_WINDOW_OCCLUDED : 0);
    SDL_WindowFlags suspended = 0;
    bool had_wayland_focus = false;
    bool was_suspended = false;
    while (engine->running) {
        Uint64 event_start = SDL_GetPerformanceCounter();
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
                            if (event.key.repeat)
                                engine->repeated[i] = true;
                            else {
                                engine->pressed[i] = true;
                                engine->fixed_pressed[i] = true;
                            }
                        }
                    }
                }
            }
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                size_t used = strlen(engine->text_input), added = strlen(event.text.text);
                if (used + added < sizeof(engine->text_input))
                    memcpy(engine->text_input + used, event.text.text, added + 1);
            }
            if (event.type == SDL_EVENT_MOUSE_WHEEL) {
                engine->mouse_scroll.x += event.wheel.x;
                engine->mouse_scroll.y += event.wheel.y;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION)
                engine->mouse_position = (af_vec2){event.motion.x, event.motion.y};
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                    button_position = (af_vec2){event.button.x, event.button.y};
                    button_position_changed = true;
                }
                for (int i = 0; i < AF_MOUSE_BUTTON_COUNT; ++i)
                    if (event.button.button == mouse_buttons[i])
                        engine->mouse_buttons_down[i] = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            }
            if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                for (int i = 0; i < AF_MOUSE_BUTTON_COUNT; ++i)
                    if (event.button.button == mouse_buttons[i])
                        engine->mouse_pressed[i] = true;
            }
            if (event.type == SDL_EVENT_MOUSE_MOTION && engine->mouse_captured) {
                engine->mouse_delta.x += event.motion.xrel;
                engine->mouse_delta.y += event.motion.yrel;
            }
            SDL_WindowFlags suspending = 0;
            if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST &&
                event.window.windowID == window_id) {
                if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
                    suspending = SDL_WINDOW_MINIMIZED;
                if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED || event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    engine->audio_focus_lost = event.type == SDL_EVENT_WINDOW_FOCUS_LOST;
                if (wayland && event.type == SDL_EVENT_WINDOW_OCCLUDED)
                    suspending = SDL_WINDOW_OCCLUDED;
                if (event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_MAXIMIZED)
                    suspended &= ~(SDL_WindowFlags)SDL_WINDOW_MINIMIZED;
                if (wayland && event.type == SDL_EVENT_WINDOW_EXPOSED)
                    suspended &= ~(SDL_WindowFlags)SDL_WINDOW_OCCLUDED;
                suspended |= suspending;
                if (suspending)
                    was_suspended = true;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST || suspending) {
                if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
                    ++engine->focus_losses;
                memset(engine->mouse_buttons_down, 0, sizeof(engine->mouse_buttons_down));
                memset(engine->keys, 0, sizeof(engine->keys));
                memset(engine->pressed, 0, sizeof(engine->pressed));
                memset(engine->repeated, 0, sizeof(engine->repeated));
                memset(engine->fixed_pressed, 0, sizeof(engine->fixed_pressed));
                memset(engine->mouse_pressed, 0, sizeof(engine->mouse_pressed));
                (void)af_engine_set_mouse_capture(engine, false);
                af_audio_suspend(engine->audio, true);
            }
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                if (!game->should_close || game->should_close(engine, game->user_data))
                    engine->running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESTORED)
                af_renderer_resize(engine->renderer);
        }
        if (!engine->running)
            break;
        SDL_WindowFlags flags = SDL_GetWindowFlags(engine->window);
        if (wayland && (flags & SDL_WINDOW_INPUT_FOCUS))
            had_wayland_focus = true;
        if (suspended || (flags & suspend_flags) || (had_wayland_focus && !(flags & SDL_WINDOW_INPUT_FOCUS))) {
            was_suspended = true;
            SDL_Delay(16);
            update_frame_stats(engine, frequency, false);
            continue;
        }
        if (was_suspended) {
            /* Presentation and simulation share this clock. Discard time
               spent hidden and present the latest solved pose on resume. */
            engine->clock.accumulator = 0;
            memset(engine->fixed_pressed, 0, sizeof(engine->fixed_pressed));
            memset(engine->fixed_actions, 0, sizeof(engine->fixed_actions));
            engine->mouse_delta = (af_vec2){0};
            af_engine_reset_interpolation(engine);
        }
        if (button_position_changed)
            engine->mouse_position = button_position;
        engine->overlay = (af_overlay_frame){0};
        Uint64 now = SDL_GetPerformanceCounter();
        double delta = was_suspended ? 0 : (double)(now - previous) / frequency;
        was_suspended = false;
        previous = now;
        double elapsed = delta;
        if (delta > 0.1)
            delta = 0.1;
        Uint64 update_start = SDL_GetPerformanceCounter();
        engine->cpu_events_ms = (double)(update_start - event_start) * 1000 / frequency;
        const char *environment_name;
        const af_hdr_file *environment_source;
        const af_ibl_images *environment_images;
        if (af_environment_job_result(engine->environment_job, &environment_name, &environment_source,
                                      &environment_images)) {
            Uint64 started = SDL_GetPerformanceCounter();
            bool uploaded = af_renderer_upload_environment_baked(engine->renderer, environment_name, environment_source,
                                                                 environment_images);
            af_environment_job_finish(engine->environment_job, uploaded,
                                      (double)(SDL_GetPerformanceCounter() - started) * 1000 / frequency);
        }
        af_audio_suspend(engine->audio, engine->simulation_paused || engine->audio_focus_lost || suspended);
        af_audio_update(engine->audio, engine->camera.position, af_camera_forward(&engine->camera));
        update_actions(engine, delta);
        if (game->update)
            game->update(engine, delta, game->user_data);
        unsigned animation_steps = 0;
        engine->fixed_active = game->fixed_update || af_physics_active(engine->scene);
        engine->cpu_physics_ms = 0;
        if (engine->fixed_active) {
            unsigned steps =
                af_fixed_clock_advance(&engine->clock, elapsed, engine->simulation_paused, engine->step_pending);
            engine->step_pending = false;
            animation_steps = steps;
            for (unsigned tick = 0; tick < steps && engine->running; ++tick) {
                af_scene *snapshot = engine->scene ? af_scene_clone(engine->scene) : NULL;
                if (engine->scene && !snapshot) {
                    result = 1;
                    engine->running = false;
                    break;
                }
                af_scene_destroy(engine->previous_scene);
                engine->previous_scene = snapshot;
                engine->previous_camera = engine->camera;
                engine->in_fixed = true;
                if (game->fixed_update)
                    game->fixed_update(engine, AF_FIXED_DELTA, game->user_data);
                if (af_physics_active(engine->scene)) {
                    Uint64 physics_start = SDL_GetPerformanceCounter();
                    if (!af_physics_step(engine->physics, (af_scene *)engine->scene, (float)AF_FIXED_DELTA)) {
                        af_log("error", "Physics: %s", af_physics_error(engine->physics));
                        engine->running = false;
                        result = 1;
                    }
                    engine->cpu_physics_ms += (double)(SDL_GetPerformanceCounter() - physics_start) * 1000 / frequency;
                }
                engine->in_fixed = false;
                ++engine->fixed_ticks;
                engine->fixed_camera_position = engine->camera.position;
                engine->has_fixed_camera = true;
                memset(engine->fixed_pressed, 0, sizeof(engine->fixed_pressed));
                memset(engine->fixed_actions, 0, sizeof(engine->fixed_actions));
            }
        }
        Uint64 render_start = SDL_GetPerformanceCounter();
        engine->cpu_update_ms = (double)(render_start - update_start) * 1000 / frequency;
        engine->cpu_render_ms = 0;
        if (!engine->running)
            break;
        if (SDL_GetWindowFlags(engine->window) & suspend_flags) {
            was_suspended = true;
            SDL_Delay(16);
            update_frame_stats(engine, frequency, false);
            continue;
        }
        bool rendered = false;
        float alpha = engine->fixed_active && !engine->simulation_paused ? (float)engine->clock.alpha : 1;
        af_camera render_camera = engine->camera;
        if (engine->previous_scene && engine->has_fixed_camera &&
            af_vec3_length(af_vec3_sub(engine->camera.position, engine->fixed_camera_position)) < 0.000001f)
            render_camera.position = af_vec3_add(
                engine->previous_camera.position,
                af_vec3_scale(af_vec3_sub(engine->camera.position, engine->previous_camera.position), alpha));
        af_renderer_set_interpolation(engine->renderer, engine->previous_scene, alpha);
        double animation_delta =
            game->fixed_update && !engine->animation_real_time ? (double)animation_steps * AF_FIXED_DELTA : delta;
        Uint64 animation_start = SDL_GetPerformanceCounter();
        if (!af_animation_runtime_update(engine->animations, engine->previous_scene, alpha, animation_delta,
                                         engine->animation_enabled)) {
            result = 1;
            break;
        }
        engine->cpu_animation_ms = (double)(SDL_GetPerformanceCounter() - animation_start) * 1000 / frequency;
        if (!af_renderer_update_water(engine->renderer, engine->physics, engine->scene,
                                      engine->previous_scene ? alpha : 1)) {
            result = 1;
            break;
        }
        if (!af_renderer_update_landscape(engine->renderer, engine->scene)) {
            result = 1;
            break;
        }
        if (!af_renderer_draw(engine->renderer, engine->clear_color, engine->scene, &render_camera, engine->viewport,
                              engine->highlighted, &engine->overlay, (float)(delta * 1000.0), &rendered)) {
            result = 1;
            break;
        }
        engine->cpu_render_ms = (double)(SDL_GetPerformanceCounter() - render_start) * 1000 / frequency;
        if (rendered)
            af_extensions_present(&engine->extensions, engine->fixed_ticks, delta);
        if (!rendered)
            SDL_Delay(16);
        update_frame_stats(engine, frequency, rendered);
    }
    engine->running = false;
    (void)af_engine_set_mouse_capture(engine, false);
    if (!af_renderer_wait_idle(engine->renderer))
        result = 1;
    if (game->shutdown)
        game->shutdown(engine, game->user_data);
    if (af_renderer_stats(engine->renderer).validation_errors != 0)
        result = 1;
    return result;
}

void af_engine_destroy(af_engine *engine) {
    if (!engine)
        return;
    af_renderer_release_upscaler(engine->renderer);
    af_extensions_destroy(&engine->extensions);
    af_environment_job_destroy(engine->environment_job);
    af_animation_runtime_destroy(engine->animations);
    af_scene_destroy(engine->previous_scene);
    af_physics_destroy(engine->physics);
    af_audio_destroy(engine->audio);
    af_renderer_destroy(engine->renderer);
    SDL_CloseGamepad(engine->gamepad);
    SDL_DestroyWindow(engine->window);
    SDL_free(engine->base_title);
    SDL_free(engine->clipboard);
    SDL_Quit();
    free(engine);
    engine_active = false;
}

void af_engine_request_quit(af_engine *engine) {
    if (engine)
        engine->running = false;
}

const char *af_engine_get_base_path(const af_engine *engine) {
    return engine ? SDL_GetBasePath() : NULL;
}

void af_engine_set_clear_color(af_engine *engine, af_color color) {
    if (engine)
        engine->clear_color = color;
}

bool af_engine_upload_mesh(af_engine *engine, const char *name, const af_mesh_data *mesh) {
    return engine && af_renderer_upload_mesh(engine->renderer, name, mesh);
}

bool af_engine_remove_mesh(af_engine *engine, const char *name) {
    return engine && af_renderer_remove_mesh(engine->renderer, name);
}

void af_engine_set_scene(af_engine *engine, const af_scene *scene) {
    if (engine) {
        if (engine->scene != scene) {
            af_engine_reset_interpolation(engine);
            af_physics_reset(engine->physics);
            af_renderer_clear_skin_palettes(engine->renderer);
            af_animation_runtime_scene(engine->animations, scene);
        }
        engine->scene = scene;
    }
}
const af_scene *af_engine_get_scene(const af_engine *engine) {
    return engine ? engine->scene : NULL;
}

bool af_engine_set_window_size(af_engine *engine, int width, int height) {
    if (!engine || width <= 0 || height <= 0)
        return false;
    if (!SDL_SetWindowSize(engine->window, width, height))
        return false;
    af_renderer_resize(engine->renderer);
    return true;
}

bool af_engine_set_camera(af_engine *engine, const af_camera *camera) {
    if (!engine || !af_camera_is_valid(camera))
        return false;
    engine->camera = *camera;
    return true;
}

bool af_engine_key_down(const af_engine *engine, af_key key) {
    return engine && key >= 0 && key < AF_KEY_COUNT && engine->keys[key];
}

bool af_engine_key_pressed(const af_engine *engine, af_key key) {
    return engine && key >= 0 && key < AF_KEY_COUNT &&
           (engine->in_fixed ? engine->fixed_pressed[key] : engine->pressed[key]);
}

bool af_engine_mouse_pressed(const af_engine *engine, af_mouse_button button) {
    return engine && button >= 0 && button < AF_MOUSE_BUTTON_COUNT && engine->mouse_pressed[button];
}

af_vec2 af_engine_mouse_delta(const af_engine *engine) {
    return engine ? engine->mouse_delta : (af_vec2){0};
}

bool af_engine_set_mouse_capture(af_engine *engine, bool enabled) {
    if (!engine)
        return false;
    if (engine->mouse_captured == enabled)
        return true;
    if (!SDL_SetWindowRelativeMouseMode(engine->window, enabled)) {
        af_log("error", "Mouse capture: %s", SDL_GetError());
        return false;
    }
    engine->mouse_captured = enabled;
    engine->mouse_delta = (af_vec2){0};
    return true;
}

bool af_engine_mouse_captured(const af_engine *engine) {
    return engine && engine->mouse_captured;
}

bool af_engine_set_debug_view(af_engine *e, af_debug_view view) {
    return e && af_renderer_set_debug_view(e->renderer, view);
}
void af_engine_set_visibility_culling(af_engine *e, bool enabled) {
    if (e)
        af_renderer_set_visibility_culling(e->renderer, enabled);
}
void af_engine_set_prop_instancing(af_engine *e, bool enabled) {
    if (e)
        af_renderer_set_prop_instancing(e->renderer, enabled);
}
bool af_engine_set_render_budget(af_engine *e, double ms, uint64_t bytes) {
    return e && af_renderer_set_render_budget(e->renderer, ms, bytes);
}
af_engine_stats af_engine_get_stats(const af_engine *engine) {
    if (!engine)
        return (af_engine_stats){0};
    af_engine_stats stats = af_renderer_stats(engine->renderer);
    stats.frames_per_second = engine->frames_per_second;
    stats.average_frame_ms = engine->average_frame_ms;
    stats.simulation_ticks = engine->fixed_ticks;
    stats.simulation_steps = engine->clock.steps;
    stats.simulation_alpha = engine->clock.alpha;
    stats.simulation_dropped_seconds = engine->clock.dropped_seconds;
    stats.simulation_paused = engine->simulation_paused;
    stats.cpu_physics_ms = engine->cpu_physics_ms;
    stats.cpu_events_ms = engine->cpu_events_ms;
    stats.cpu_update_ms = engine->cpu_update_ms;
    stats.cpu_animation_ms = engine->cpu_animation_ms;
    stats.cpu_render_ms = engine->cpu_render_ms;
    return stats;
}

SDL_Window *af_engine_sdl_window(af_engine *engine) {
    return engine ? engine->window : NULL;
}

bool af_engine_set_title(af_engine *engine, const char *title) {
    if (!engine || !title)
        return false;
    char *copy = SDL_strdup(title);
    if (!copy)
        return false;
    SDL_free(engine->base_title);
    engine->base_title = copy;
    if (engine->show_fps) {
        update_fps_title(engine);
        return true;
    }
    return SDL_SetWindowTitle(engine->window, copy);
}

void af_engine_get_window_size(const af_engine *engine, int *width, int *height) {
    int w = 0, h = 0;
    if (engine)
        (void)SDL_GetWindowSize(engine->window, &w, &h);
    if (width)
        *width = w;
    if (height)
        *height = h;
}

void af_engine_set_viewport(af_engine *engine, const af_rect *viewport) {
    if (engine)
        engine->viewport = viewport ? *viewport : (af_rect){0};
}

bool af_engine_upload_overlay_atlas(af_engine *engine, const unsigned char *rgba, uint32_t width, uint32_t height) {
    return engine && af_renderer_upload_overlay_atlas(engine->renderer, rgba, width, height);
}

bool af_engine_set_overlay(af_engine *engine, const af_overlay_frame *frame) {
    if (!engine)
        return false;
    if (!frame) {
        engine->overlay = (af_overlay_frame){0};
        return true;
    }
    if (!frame->vertices || !frame->indices || !frame->commands || frame->width <= 0 || frame->height <= 0 ||
        !isfinite(frame->width) || !isfinite(frame->height) || frame->vertex_count > 1048576 ||
        frame->index_count > 3145728 || frame->command_count > 65536)
        return false;
    for (uint32_t i = 0; i < frame->index_count; ++i)
        if (frame->indices[i] >= frame->vertex_count)
            return false;
    for (uint32_t i = 0; i < frame->command_count; ++i) {
        const af_overlay_command *command = &frame->commands[i];
        if (command->first_index > frame->index_count ||
            command->index_count > frame->index_count - command->first_index || !isfinite(command->clip.x) ||
            !isfinite(command->clip.y) || !isfinite(command->clip.width) || !isfinite(command->clip.height))
            return false;
    }
    engine->overlay = *frame;
    return true;
}

af_vec2 af_engine_mouse_position(const af_engine *engine) {
    return engine ? engine->mouse_position : (af_vec2){0};
}
af_vec2 af_engine_mouse_scroll(const af_engine *engine) {
    return engine ? engine->mouse_scroll : (af_vec2){0};
}
bool af_engine_mouse_down(const af_engine *engine, af_mouse_button button) {
    return engine && button >= 0 && button < AF_MOUSE_BUTTON_COUNT && engine->mouse_buttons_down[button];
}
bool af_engine_set_text_input(af_engine *engine, bool enabled) {
    return engine && (enabled ? SDL_StartTextInput(engine->window) : SDL_StopTextInput(engine->window));
}
const char *af_engine_text_input(const af_engine *engine) {
    return engine ? engine->text_input : "";
}
bool af_engine_set_clipboard_text(af_engine *engine, const char *text) {
    return engine && text && SDL_SetClipboardText(text);
}
const char *af_engine_get_clipboard_text(af_engine *engine) {
    if (!engine)
        return "";
    SDL_free(engine->clipboard);
    engine->clipboard = SDL_GetClipboardText();
    return engine->clipboard ? engine->clipboard : "";
}
af_object_id af_engine_pick_object(af_engine *engine, af_vec2 point) {
    return engine ? af_renderer_pick_object(engine->renderer, engine->scene, &engine->camera, engine->viewport, point)
                  : AF_OBJECT_NONE;
}
void af_engine_highlight_object(af_engine *engine, af_object_id id) {
    if (engine)
        engine->highlighted = id;
}

bool af_engine_load_mesh(af_engine *engine, const char *name, const char *path, af_asset_error *error) {
    af_mesh_file mesh;
    af_mesh_sidedness sidedness;
    if (!af_mesh_file_load_with_sidedness(path, &mesh, &sidedness, error))
        return false;
    bool result = engine && af_renderer_upload_mesh_file(engine->renderer, name, &mesh, sidedness);
    af_mesh_file_free(&mesh);
    if (!result && error)
        snprintf(error->message, sizeof(error->message), "GPU mesh upload failed.");
    return result;
}
bool af_engine_load_cluster_mesh(af_engine *engine, const char *name, const char *path, af_asset_error *error) {
    af_cluster_mesh mesh;
    af_cluster_stream *stream = NULL;
    if (!engine || (!af_cluster_load(path, &mesh) && !(stream = af_cluster_open_stream(path, &mesh)))) {
        if (error)
            snprintf(error->message, sizeof(error->message), "Invalid or unreadable clustered mesh.");
        return false;
    }
    bool uploaded = stream ? af_renderer_upload_cluster_stream(engine->renderer, name, &mesh, stream)
                           : af_renderer_upload_clusters(engine->renderer, name, &mesh);
    if (!uploaded)
        af_cluster_close_stream(stream);
    af_cluster_free(&mesh);
    if (error)
        snprintf(error->message, sizeof(error->message), "%s", uploaded ? "" : "GPU mesh upload failed.");
    return uploaded;
}

void af_engine_set_cluster_rendering(af_engine *engine, bool enabled, bool debug_colors) {
    if (engine)
        af_renderer_set_cluster_rendering(engine->renderer, enabled, debug_colors);
}

void af_engine_set_cluster_lod(af_engine *engine, bool enabled) {
    if (engine)
        af_renderer_set_cluster_lod(engine->renderer, enabled);
}

void af_engine_set_cluster_gpu_culling(af_engine *engine, bool enabled) {
    if (engine)
        af_renderer_set_cluster_gpu_culling(engine->renderer, enabled);
}

bool af_engine_set_cluster_cache_budget(af_engine *engine, uint64_t bytes) {
    return engine && af_renderer_set_cluster_cache_budget(engine->renderer, bytes);
}

af_cluster_stats af_engine_cluster_stats(const af_engine *engine) {
    return engine ? af_renderer_cluster_stats(engine->renderer) : (af_cluster_stats){0};
}

static void asset_error_context(af_asset_error *error, const af_scene_asset *asset) {
    if (!error || !asset)
        return;
    char reason[256];
    snprintf(reason, sizeof(reason), "%s", error->message);
    snprintf(error->message, sizeof(error->message), "Asset '%.48s' [%.100s]: %.85s", asset->name, asset->path, reason);
}
bool af_engine_load_scene_assets(af_engine *engine, const af_scene *scene, af_asset_error *error) {
    if (!af_scene_prepare_collision_meshes((af_scene *)scene, error))
        return false;
    if (!engine || !scene) {
        if (error)
            snprintf(error->message, sizeof(error->message), "An engine and scene are required.");
        return false;
    }
    for (size_t i = 0; i < af_scene_asset_count(scene); ++i)
        if (!af_engine_load_asset(engine, af_scene_asset_at(scene, i), error)) {
            asset_error_context(error, af_scene_asset_at(scene, i));
            return false;
        }
    return true;
}

bool af_engine_upload_texture(af_engine *engine, const char *name, const unsigned char *rgba, uint32_t width,
                              uint32_t height) {
    return engine && af_renderer_upload_texture(engine->renderer, name, rgba, width, height);
}
bool af_engine_load_texture(af_engine *engine, const char *name, const char *path, af_asset_error *error) {
    af_image_file image;
    if (!af_image_file_load(path, &image, error))
        return false;
    bool result = af_engine_upload_texture(engine, name, image.rgba, image.width, image.height);
    af_image_file_free(&image);
    if (!result && error)
        snprintf(error->message, sizeof(error->message), "GPU texture upload failed.");
    return result;
}

bool af_engine_remove_texture(af_engine *engine, const char *name) {
    return engine && af_renderer_remove_texture(engine->renderer, name);
}

bool af_engine_key_repeated(const af_engine *engine, af_key key) {
    return engine && key >= 0 && key < AF_KEY_COUNT && engine->repeated[key];
}

af_temporal_test_stats af_engine_test_temporal(const af_engine *e) {
    return e ? af_renderer_test_temporal(e->renderer) : (af_temporal_test_stats){0};
}
bool af_engine_test_capture_motion(af_engine *e, af_vec2 p) {
    return e && af_renderer_test_capture_motion(e->renderer, p);
}
bool af_engine_test_read_motion(af_engine *e, af_vec4 *p) {
    return e && af_renderer_test_read_motion(e->renderer, p);
}
bool af_engine_test_capture_region(af_engine *e, af_vec2 p, uint32_t w, uint32_t h) {
    return e && af_renderer_test_capture_region(e->renderer, p, w, h);
}
bool af_engine_test_read_pixels(af_engine *e, unsigned char *p, size_t n, uint32_t *w, uint32_t *h) {
    return e && af_renderer_test_read_pixels(e->renderer, p, n, w, h);
}
bool af_engine_test_capture_pixel(af_engine *engine, af_vec2 position) {
    return engine && af_renderer_test_capture_pixel(engine->renderer, position);
}
bool af_engine_test_read_pixel(af_engine *engine, unsigned char rgba[4]) {
    return engine && af_renderer_test_read_pixel(engine->renderer, rgba);
}

bool af_engine_texture_info(const af_engine *engine, const char *name, af_texture_info *out) {
    return engine && af_renderer_texture_info(engine->renderer, name, out);
}

bool af_engine_upload_normal_map(af_engine *engine, const char *name, const unsigned char *rgba, uint32_t width,
                                 uint32_t height) {
    return engine && af_renderer_upload_normal_map(engine->renderer, name, rgba, width, height);
}
bool af_engine_load_normal_map(af_engine *engine, const char *name, const char *path, af_asset_error *error) {
    af_image_file image;
    if (!af_image_file_load(path, &image, error))
        return false;
    bool result = af_engine_upload_normal_map(engine, name, image.rgba, image.width, image.height);
    af_image_file_free(&image);
    if (!result && error)
        snprintf(error->message, sizeof(error->message), "GPU normal-map upload failed.");
    return result;
}

void af_engine_reset_interpolation(af_engine *engine) {
    if (!engine)
        return;
    af_scene_destroy(engine->previous_scene);
    engine->previous_scene = NULL;
    engine->previous_camera = engine->camera;
    engine->has_fixed_camera = false;
    if (engine->renderer) {
        af_renderer_set_interpolation(engine->renderer, NULL, 1);
        af_renderer_reset_temporal(engine->renderer);
    }
}
void af_engine_set_simulation_paused(af_engine *engine, bool paused) {
    if (!engine || engine->simulation_paused == paused)
        return;
    engine->simulation_paused = paused;
    if (paused)
        af_audio_suspend(engine->audio, true);
    engine->clock.accumulator = 0;
    memset(engine->fixed_pressed, 0, sizeof(engine->fixed_pressed));
    af_engine_reset_interpolation(engine);
}
bool af_engine_step_simulation(af_engine *engine) {
    if (!engine || !engine->simulation_paused)
        return false;
    engine->step_pending = true;
    return true;
}

bool af_engine_mouse_in_viewport(const af_engine *engine) {
    if (!engine)
        return false;
    int width, height;
    SDL_GetWindowSize(engine->window, &width, &height);
    af_rect area = engine->viewport;
    if (area.width <= 0 || area.height <= 0)
        area = (af_rect){0, 0, (float)width, (float)height};
    af_vec2 p = engine->mouse_position;
    return p.x >= area.x && p.y >= area.y && p.x < area.x + area.width && p.y < area.y + area.height;
}

af_camera af_engine_get_camera(const af_engine *engine) {
    return engine ? engine->camera : af_camera_default();
}

af_gameplay_status af_engine_gameplay_status(const af_engine *engine) {
    return engine ? engine->gameplay_status : (af_gameplay_status){0};
}
void af_engine_set_gameplay_status(af_engine *engine, af_gameplay_status status) {
    if (engine) {
        status.hint[sizeof(status.hint) - 1] = 0;
        engine->gameplay_status = status;
    }
}

bool af_engine_load_sound(af_engine *engine, const char *name, const char *path, af_asset_error *error) {
    if (!engine)
        return false;
    if (!engine->audio)
        engine->audio = af_audio_create();
    return af_audio_load(engine->audio, name, path, error);
}
bool af_engine_play_sound(af_engine *engine, const char *name, float gain) {
    return engine && af_audio_play(engine->audio, name, gain);
}
af_sound_loop af_engine_start_sound_loop(af_engine *engine, const char *clip, af_sound_emitter emitter,
                                         float fade_seconds) {
    return engine ? af_audio_start_loop(engine->audio, clip, emitter, fade_seconds) : 0;
}
bool af_engine_move_sound_loop(af_engine *engine, af_sound_loop loop, af_vec3 position) {
    return engine && af_audio_move_loop(engine->audio, loop, position);
}
bool af_engine_fade_sound_loop(af_engine *engine, af_sound_loop loop, float gain, float seconds) {
    return engine && af_audio_fade_loop(engine->audio, loop, gain, seconds);
}
bool af_engine_stop_sound_loop(af_engine *engine, af_sound_loop loop, float seconds) {
    return engine && af_audio_stop_loop(engine->audio, loop, seconds);
}
void af_engine_stop_sounds(af_engine *engine) {
    if (engine)
        af_audio_stop(engine->audio);
}
af_audio_stats af_engine_audio_stats(const af_engine *engine) {
    return af_audio_get_stats(engine ? engine->audio : NULL);
}
uint64_t af_engine_test_focus_losses(const af_engine *engine) {
    return engine ? engine->focus_losses : 0;
}

bool af_engine_load_asset(af_engine *engine, const af_scene_asset *asset, af_asset_error *error) {
    if (!engine || !asset)
        return false;
    switch (asset->kind) {
    case AF_ASSET_MESH:
        return af_engine_load_mesh(engine, asset->name, asset->path, error);
    case AF_ASSET_TEXTURE:
        return af_engine_load_texture(engine, asset->name, asset->path, error);
    case AF_ASSET_NORMAL_MAP:
    case AF_ASSET_METALLIC_ROUGHNESS:
        return af_engine_load_normal_map(engine, asset->name, asset->path, error);
    case AF_ASSET_ANIMATION:
        return af_engine_load_animation(engine, asset->name, asset->path, error);
    case AF_ASSET_ENVIRONMENT:
        return af_engine_load_environment(engine, asset->name, asset->path, error);
    case AF_ASSET_AUDIO:
        return af_engine_load_sound(engine, asset->name, asset->path, error);
    default:
        return false;
    }
}
bool af_engine_reload_asset_source(af_engine *engine, const af_scene *scene, const char *source,
                                   af_asset_error *error) {
    if (!engine || !scene || !source)
        return false;
    typedef struct staged_asset {
        char temporary[64];
        af_scene_asset asset;
    } staged_asset;

    af_scene *collision_stage = af_scene_clone(scene);
    if (!collision_stage)
        return false;
    for (size_t i = 0; i < collision_stage->collision_mesh_count; ++i) {
        cm_slot *slot = &collision_stage->collision_meshes[i];
        size_t length = strcspn(slot->path, "#");
        if (strlen(source) == length && !strncmp(source, slot->path, length))
            slot->path[0] = 0;
    }
    if (!af_scene_prepare_collision_meshes(collision_stage, error)) {
        af_scene_destroy(collision_stage);
        return false;
    }
    const size_t asset_count = af_scene_asset_count(scene);
    staged_asset *staged = calloc(asset_count ? asset_count : 1, sizeof(*staged));
    if (!staged) {
        af_scene_destroy(collision_stage);
        return false;
    }
    size_t count = 0;
    bool ok = true, matched = false;
    static unsigned sequence;
    for (size_t i = 0; i < asset_count; ++i) {
        const af_scene_asset *a = af_scene_asset_at(scene, i);
        size_t size = strcspn(a->path, "#");
        if (strlen(source) != size || strncmp(source, a->path, size))
            continue;
        matched = true;
        if (a->kind == AF_ASSET_AUDIO) {
            ok = af_engine_load_asset(engine, a, error);
            if (!ok)
                break;
            continue;
        }
        staged_asset *entry = &staged[count++];
        entry->asset = *a;
        do {
            snprintf(entry->temporary, sizeof(entry->temporary), "__reload_%u_%zu", ++sequence, i);
        } while (af_scene_find_asset(scene, entry->temporary));
        af_scene_asset temporary = *a;
        snprintf(temporary.name, sizeof(temporary.name), "%s", entry->temporary);
        if (!af_engine_load_asset(engine, &temporary, error)) {
            asset_error_context(error, a);
            ok = false;
            break;
        }
    }
    if (ok)
        for (size_t i = 0; i < count; ++i)
            if (!(staged[i].asset.kind == AF_ASSET_ANIMATION
                      ? af_animation_runtime_commit(engine->animations, staged[i].temporary, staged[i].asset.name)
                      : af_renderer_commit_asset(engine->renderer, staged[i].temporary, staged[i].asset.name,
                                                 staged[i].asset.kind == AF_ASSET_MESH))) {
                ok = false;
                break;
            }
    for (size_t i = 0; i < count; ++i) {
        if (staged[i].asset.kind == AF_ASSET_ANIMATION)
            af_animation_runtime_remove(engine->animations, staged[i].temporary);
        else if (staged[i].asset.kind == AF_ASSET_MESH)
            af_engine_remove_mesh(engine, staged[i].temporary);
        else
            af_engine_remove_texture(engine, staged[i].temporary);
    }

    if (ok && matched) {
        af_scene *target = (af_scene *)scene;
        for (size_t i = 0; i < target->collision_mesh_count; ++i)
            cm_release(target->collision_meshes[i].mesh);
        memcpy(target->collision_meshes, collision_stage->collision_meshes, sizeof(target->collision_meshes));
        target->collision_mesh_count = collision_stage->collision_mesh_count;
        collision_stage->collision_mesh_count = 0;
    }
    af_scene_destroy(collision_stage);
    if (!matched && error)
        snprintf(error->message, sizeof(error->message), "No assets reference this source.");
    free(staged);
    return ok && matched;
}

bool af_engine_upload_overlay_texture(af_engine *engine, uint32_t id, const unsigned char *rgba, uint32_t width,
                                      uint32_t height) {
    return engine && af_renderer_upload_overlay_texture(engine->renderer, id, rgba, width, height);
}
bool af_engine_remove_overlay_texture(af_engine *engine, uint32_t id) {
    return engine && af_renderer_remove_overlay_texture(engine->renderer, id);
}

bool af_engine_load_environment(af_engine *engine, const char *name, const char *path, af_asset_error *error) {
    af_hdr_file image;
    if (!engine || !af_hdr_file_load(path, &image, error))
        return false;
    bool result = af_renderer_upload_environment(engine->renderer, name, image.rgba, image.width, image.height);
    af_hdr_file_free(&image);
    if (!result && error)
        snprintf(error->message, sizeof(error->message),
                 "Environment upload/convolution failed; previous data retained.");
    return result;
}

bool af_engine_start_environment(af_engine *engine, const char *name, const char *path, af_asset_error *error) {
    if (error)
        *error = (af_asset_error){0};
    if (!engine)
        return false;
    af_environment_job_state state = af_environment_job_status_get(engine->environment_job).state;
    if (state == AF_ENV_LOADING || state == AF_ENV_BAKING || state == AF_ENV_UPLOADING) {
        if (error)
            snprintf(error->message, sizeof(error->message), "An HDR import is already running. Cancel it first.");
        return false;
    }
    af_environment_job *job = af_environment_job_create(name, path);
    if (!job) {
        if (error)
            snprintf(error->message, sizeof(error->message),
                     "Cannot start HDR worker (invalid name/path or thread allocation).");
        return false;
    }
    af_environment_job_destroy(engine->environment_job);
    engine->environment_job = job;
    return true;
}
void af_engine_cancel_environment(af_engine *engine) {
    if (engine)
        af_environment_job_cancel(engine->environment_job);
}
af_environment_job_status af_engine_environment_status(const af_engine *engine) {
    return af_environment_job_status_get(engine ? engine->environment_job : NULL);
}

bool af_engine_set_skin_palette(af_engine *engine, af_object_id object, const af_mat4 *matrices, size_t count) {
    return engine && (!count || af_scene_get_object(engine->scene, object)) &&
           af_renderer_set_skin_palette(engine->renderer, object, matrices, count);
}

void af_engine_set_animation_playback(af_engine *e, bool enabled, bool real_time) {
    if (e) {
        e->animation_enabled = enabled;
        e->animation_real_time = real_time;
    }
}
bool af_engine_load_animation(af_engine *e, const char *name, const char *path, af_asset_error *error) {
    return e && af_animation_runtime_load(e->animations, name, path, error);
}
af_object_id af_engine_animation_root(const af_engine *e, af_object_id object) {
    return e ? af_animation_runtime_root(e->animations, object) : 0;
}
af_animation_status af_engine_animation_status(const af_engine *e, af_object_id object) {
    return e ? af_animation_runtime_status(e->animations, object) : (af_animation_status){0};
}
bool af_engine_animation_clip(const af_engine *e, af_object_id object, size_t clip, af_animation_clip_info *info) {
    return e && af_animation_runtime_clip(e->animations, object, clip, info);
}
bool af_engine_animation_play(af_engine *e, af_object_id object, size_t clip, float blend) {
    return e && af_animation_runtime_play(e->animations, object, clip, blend);
}
bool af_engine_animation_pause(af_engine *e, af_object_id object, bool paused) {
    return e && af_animation_runtime_pause(e->animations, object, paused);
}
bool af_engine_animation_stop(af_engine *e, af_object_id object) {
    return e && af_animation_runtime_stop(e->animations, object);
}
bool af_engine_animation_seek(af_engine *e, af_object_id object, double seconds) {
    return e && af_animation_runtime_seek(e->animations, object, seconds);
}
bool af_engine_animation_options(af_engine *e, af_object_id object, float speed, bool loop) {
    return e && af_animation_runtime_options(e->animations, object, speed, loop);
}
bool af_engine_animated_world_matrix(const af_engine *e, af_object_id object, af_mat4 *world) {
    return e &&
           (af_animation_runtime_world(e->animations, object, world) || af_scene_world_matrix(e->scene, object, world));
}

bool af_engine_locomotion_step(af_engine *e, af_object_id object, af_locomotion_input input, float delta,
                               af_locomotion *status) {
    return e && af_animation_runtime_locomotion(e->animations, object, input, delta, status);
}

bool af_engine_animation_events(af_engine *e, af_object_id object, af_animation_events *out) {
    return e && af_animation_runtime_events(e->animations, object, out);
}

bool af_engine_locomotion_root_motion(af_engine *e, af_object_id object, uint32_t node, bool enabled) {
    return e && af_animation_runtime_root_motion(e->animations, object, node, enabled);
}
bool af_engine_locomotion_root_delta(af_engine *e, af_object_id object, af_vec3 *out) {
    return e && af_animation_runtime_root_delta(e->animations, object, out);
}

af_animation_debug af_engine_animation_debug(const af_engine *e, af_object_id o) {
    return e ? af_animation_runtime_debug(e->animations, o) : (af_animation_debug){0};
}
size_t af_engine_animation_bones(const af_engine *e, af_object_id o, af_animation_bone *out, size_t n) {
    return e ? af_animation_runtime_bones(e->animations, o, out, n) : 0;
}

bool af_engine_bind_action(af_engine *e, af_action_binding b) {
    if (!e || !af_input_bind(&e->input_map, b))
        return false;
    memset(e->action_values, 0, sizeof(e->action_values));
    memset(e->fixed_actions, 0, sizeof(e->fixed_actions));
    return true;
}
bool af_engine_load_input_map(af_engine *e, const char *path, af_asset_error *error) {
    if (!e)
        return false;
    af_input_map next;
    if (!af_input_map_load(path, &next, error))
        return false;
    e->input_map = next;
    memset(e->action_values, 0, sizeof(e->action_values));
    memset(e->action_pressed, 0, sizeof(e->action_pressed));
    memset(e->fixed_actions, 0, sizeof(e->fixed_actions));
    return true;
}
float af_engine_action_value(const af_engine *e, const char *name) {
    if (e && name)
        for (size_t i = 0; i < e->input_map.count; ++i)
            if (!strcmp(e->input_map.bindings[i].name, name))
                return e->action_values[i];
    return 0;
}
bool af_engine_action_pressed(const af_engine *e, const char *name) {
    if (e && name)
        for (size_t i = 0; i < e->input_map.count; ++i)
            if (!strcmp(e->input_map.bindings[i].name, name))
                return e->in_fixed ? e->fixed_actions[i] : e->action_pressed[i];
    return false;
}

af_input_map af_engine_input_map(const af_engine *e) {
    return e ? e->input_map : (af_input_map){0};
}
bool af_engine_save_input_map(const af_engine *e, const char *path, af_asset_error *error) {
    return e && af_input_map_save(path, &e->input_map, error);
}

bool af_engine_play_sound_at(af_engine *e, const char *name, af_sound_emitter emitter) {
    return e && af_audio_play_at(e->audio, name, emitter);
}

bool af_engine_spawn_prefab(af_engine *e, af_scene *s, const char *path, af_object_id parent, af_transform placement,
                            af_object_id *out, af_scene_error *error) {
    if (out)
        *out = 0;
    if (!e || !s)
        return false;
    af_scene *next = af_scene_clone(s);
    if (!next)
        return false;
    af_object_id root;
    bool ok = af_prefab_instantiate(next, path, parent, placement, &root, error);
    if (ok && !af_physics_validate_scene(next, error ? error->message : NULL, error ? sizeof(error->message) : 0))
        ok = false;
    af_asset_error asset_error;
    if (ok && !af_engine_load_scene_assets(e, next, &asset_error)) {
        ok = false;
        if (error) {
            error->code = AF_SCENE_ERROR_IO;
            snprintf(error->message, sizeof(error->message), "%.255s", asset_error.message);
        }
    }
    if (ok) {
        af_scene previous = *s;
        *s = *next;
        *next = previous;
        if (out)
            *out = root;
    }
    af_scene_destroy(next);
    return ok;
}

bool af_engine_apply_force(af_engine *e, af_object_id id, af_vec3 force, af_vec3 point) {
    return e && af_physics_apply_force(e->physics, (af_scene *)e->scene, id, force, point);
}
bool af_engine_apply_impulse(af_engine *e, af_object_id id, af_vec3 impulse, af_vec3 point) {
    return e && af_physics_apply_impulse(e->physics, (af_scene *)e->scene, id, impulse, point);
}
bool af_engine_wake_body(af_engine *e, af_object_id id) {
    return e && af_physics_wake(e->physics, id);
}
af_physics_state af_engine_body_state(const af_engine *e, af_object_id id) {
    return af_physics_get_state(e ? e->physics : NULL, id);
}
af_physics_joint_state af_engine_joint_state(const af_engine *e, af_joint_id id) {
    return af_physics_get_joint_state(e ? e->physics : NULL, id);
}
af_physics_stats af_engine_physics_stats(const af_engine *e) {
    return af_physics_get_stats(e ? e->physics : NULL);
}
size_t af_engine_physics_contacts(const af_engine *e, af_physics_contact *out, size_t cap) {
    return af_physics_contacts(e ? e->physics : NULL, out, cap);
}

size_t af_engine_push_character(af_engine *e, af_object_id id, const af_character *c, af_character_config config,
                                af_vec3 velocity, float force, float dt) {
    return e ? af_physics_push_character(e->physics, (af_scene *)e->scene, id, c, config, velocity, force, dt) : 0;
}
bool af_engine_grab_body(af_engine *e, af_object_id id, af_vec3 point) {
    return e && af_physics_grab(e->physics, (af_scene *)e->scene, id, point);
}
bool af_engine_move_grab(af_engine *e, af_vec3 point) {
    return e && af_physics_move_grab(e->physics, point);
}
bool af_engine_release_body(af_engine *e, af_vec3 velocity) {
    return e && af_physics_release(e->physics, (af_scene *)e->scene, velocity);
}
af_object_id af_engine_held_body(const af_engine *e) {
    return af_physics_held(e ? e->physics : NULL);
}

af_water_sample af_engine_sample_water(const af_engine *e, af_vec3 p) {
    return af_physics_sample_water(e ? e->physics : NULL, e ? e->scene : NULL, p);
}
bool af_engine_disturb_water(af_engine *e, af_object_id id, af_vec3 p, float radius, float velocity) {
    return e && af_physics_disturb_water(e->physics, e->scene, id, p, radius, velocity);
}
af_water_stats af_engine_water_stats(const af_engine *e) {
    return af_physics_water_stats(e ? e->physics : NULL);
}

size_t af_engine_water_particles(const af_engine *e, af_object_id id, af_water_particle *out, size_t cap) {
    return af_physics_water_particles(e ? e->physics : NULL, id, out, cap);
}

bool af_engine_swim_character(af_engine *e, af_character *c, af_character_config cc, af_swim_state *s,
                              af_swim_config sc, af_swim_input i, float dt, af_query_filter f) {
    return e && af_character_swim_step(e->physics, e->scene, c, cc, s, sc, i, dt, f);
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "bundle.h"
#include "cube.h"
#include "gui.h"
#include "play_hud.h"
#include "preferences.h"
#include "runtime.h"
#include <SDL3/SDL_filesystem.h>
#include <afterlight/asset_watch.h>
#include <afterlight/extension.h>
#include <afterlight/extension_vulkan.h>
#include <afterlight/project.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct player {
  char project_path[4096];
  af_project project;
  af_scene *source;
  ed_runtime runtime;
  ed_gui gui;
  af_asset_watch *watch;
  uint64_t frames;
  bool failed, hot_reload, menu, started, finished_recorded, preferences_loaded;
  af_player_preferences preferences;
  char preferences_path[4096], save_path[4096], message[256], bundle_directory[4096];
  uint64_t menu_test;
} player;
static int check_project(const af_project *project) {
  af_scene_error error;
  af_scene *scene = af_scene_load(project->startup_scene, &error);
  if (!scene) {
    fprintf(stderr, "Scene: %s\n", error.message);
    return 1;
  }
  af_audio *audio = af_audio_create();
  bool ok = audio != NULL;
  af_asset_error asset_error = {0};
  for (size_t i = 0; ok && i < af_scene_asset_count(scene); ++i) {
    const af_scene_asset *a = af_scene_asset_at(scene, i);
    if (a->kind == AF_ASSET_MESH) {
      af_mesh_file mesh;
      ok = af_mesh_file_load(a->path, &mesh, &asset_error);
      if (ok)
        af_mesh_file_free(&mesh);
    } else if (a->kind == AF_ASSET_ANIMATION) {
      af_animation_file animation;
      ok = af_animation_file_load(a->path, &animation, &asset_error);
      if (ok)
        af_animation_file_free(&animation);
    } else if (a->kind == AF_ASSET_ENVIRONMENT) {
      af_hdr_file image;
      ok = af_hdr_file_load(a->path, &image, &asset_error);
      if (ok)
        af_hdr_file_free(&image);
    } else if (a->kind == AF_ASSET_AUDIO)
      ok = af_audio_load(audio, a->name, a->path, &asset_error);
    else {
      af_image_file image;
      ok = af_image_file_load(a->path, &image, &asset_error);
      if (ok)
        af_image_file_free(&image);
    }
    if (!ok)
      fprintf(stderr, "Asset %s: %s\n", a->name, asset_error.message);
  }
  for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
    const af_scene_object *o = af_scene_object_at(scene, i);
    const char *names[] = {o->mesh, o->material.texture, o->material.normal_map,
                           o->material.metallic_roughness_map,
                           o->animation.asset};
    af_asset_kind kinds[] = {AF_ASSET_MESH, AF_ASSET_TEXTURE,
                             AF_ASSET_NORMAL_MAP, AF_ASSET_METALLIC_ROUGHNESS,
                             AF_ASSET_ANIMATION};
    for (unsigned k = 0; k < 5; ++k) {
      if (!names[k][0] || (k == 0 && (!strcmp(names[k], "cube") ||
                                      !strcmp(names[k], "__node"))))
        continue;
      const af_scene_asset *asset = af_scene_find_asset(scene, names[k]);
      if (!asset || asset->kind != kinds[k]) {
        fprintf(stderr, "Object '%s': missing or wrong-kind asset '%s'.\n",
                o->name, names[k]);
        ok = false;
      }
    }
  }
  SDL_SharedObject *library = ok ? SDL_LoadObject(project->game_module) : NULL;
  if (ok && !library) {
    fprintf(stderr, "Module: %s\n", SDL_GetError());
    ok = false;
  }
  if (library) {
    SDL_FunctionPointer symbol = SDL_LoadFunction(library, "af_game_get_api");
    af_game_get_api_fn get = NULL;
    memcpy(&get, &symbol, sizeof(get));
    const af_game_module *api = get ? get(AF_GAME_ABI_VERSION) : NULL;
    ok = api && api->abi_version == AF_GAME_ABI_VERSION &&
         api->struct_size == sizeof(*api) &&
         api->host_size == sizeof(af_game_host) &&
         api->object_size == sizeof(af_scene_object) &&
         api->state_size <= AF_GAME_STATE_MAX_BYTES && api->init &&
         api->update && api->shutdown;
    if (!ok)
      fputs("Module ABI/callback validation failed. Rebuild with current "
            "headers.\n",
            stderr);
    SDL_UnloadObject(library);
  }
  af_audio_destroy(audio);
  af_scene_destroy(scene);
  if (ok)
    puts("Project scene, assets and C module ABI are valid.");
  return ok ? 0 : 1;
}
static void apply_preferences(player *app) {
  if (!app->runtime.scene)
    return;
  af_render_settings r = af_scene_get_render_settings(app->runtime.scene);
  r.anti_aliasing = app->preferences.aa;
  r.aa_sharpness = app->preferences.sharpness;
  r.taa_history_weight = app->preferences.history;
  r.exposure = app->preferences.exposure;
  r.ray_traced_shadows = app->preferences.ray_traced_shadows;
  r.ray_traced_water_reflections =
      app->preferences.ray_traced_water_reflections;
  af_scene_set_render_settings(app->runtime.scene, r);
  for (size_t i = 0; i < af_scene_object_count(app->runtime.scene); ++i) {
    const af_scene_object *o = af_scene_object_at(app->runtime.scene, i);
    if (o->gameplay.player.enabled) {
      af_gameplay_component g = o->gameplay;
      g.player.mouse_sensitivity = .0025f * app->preferences.sensitivity;
      af_scene_set_object_gameplay(app->runtime.scene, o->id, g);
    }
  }
}
static void store_preferences(player *app) {
  af_asset_error error;
  if (app->preferences_path[0] &&
      !af_player_preferences_save(app->preferences_path, &app->preferences,
                                  &error))
    snprintf(app->message, sizeof(app->message), "%s", error.message);
}
static void apply_upscaler_preference(player *app, af_engine *engine) {
  size_t count = af_engine_extension_count(engine);
  if (!count) return;

  if (!strcmp(app->preferences.upscaler, "off")) {
    for (size_t i = 0; i < count; ++i) {
      af_extension_info info;
      if (af_engine_get_extension(engine, i, &info) && info.vulkan_upscaler && info.enabled)
        af_engine_set_extension_enabled(engine, i, false);
    }
  } else if (app->preferences.upscaler[0]) {
    for (size_t i = 0; i < count; ++i) {
      af_extension_info info;
      if (af_engine_get_extension(engine, i, &info) && info.vulkan_upscaler &&
          !strcmp(info.id, app->preferences.upscaler)) {
        af_engine_set_extension_enabled(engine, i, true);
        break;
      }
    }
  }

  uint32_t qualities = af_engine_upscaler_quality_count(engine);
  if (qualities) {
    if (app->preferences.upscaler_quality >= qualities)
      app->preferences.upscaler_quality = 0;
    af_engine_set_upscaler_quality(engine, app->preferences.upscaler_quality);
  }
}
static void set_menu(player *app, af_engine *engine, bool menu) {
  app->menu = menu;
  app->runtime.paused = menu;
  af_engine_set_mouse_capture(engine, !menu);
  af_engine_set_simulation_paused(engine, menu);
}
static bool init(af_engine *engine, void *memory) {
  player *app = memory;
  af_scene_error scene_error;
  af_asset_error error = {0};
  app->source = af_scene_load(app->project.startup_scene, &scene_error);
  if (!app->source) {
    fprintf(stderr, "Player scene: %s\n", scene_error.message);
    return false;
  }
  af_mesh_data cube = cube_mesh_data();
  if (!af_engine_upload_mesh(engine, "cube", &cube) ||
      !af_engine_load_scene_assets(engine, app->source, &error)) {
    fprintf(stderr, "Player assets: %s\n", error.message);
    return false;
  }
  char font[AF_ASSET_PATH_CAPACITY];
  const char *base = af_engine_get_base_path(engine);
  bool font_loaded = false;
  if (app->bundle_directory[0]) {
    snprintf(font, sizeof(font), "%s/fonts/FiraSans-Regular.ttf", app->bundle_directory);
    font_loaded = ed_gui_init(&app->gui, engine, font);
  }
  if (!font_loaded) {
    if (!base ||
        snprintf(font, sizeof(font), "%sfonts/FiraSans-Regular.ttf", base) >=
            (int)sizeof(font) ||
        !ed_gui_init(&app->gui, engine, font))
      return false;
  }
  if (!ed_runtime_set_project(&app->runtime, app->project_path,
                              app->project.startup_scene))
    return false;
  char input_path[4096];
  if (af_asset_resolve_path(app->project_path, "input.json", input_path,
                            sizeof(input_path))) {
    FILE *input = fopen(input_path, "rb");
    if (input) {
      fclose(input);
      if (!af_engine_load_input_map(engine, input_path, &error))
        return false;
    }
  }
  if (!ed_runtime_start_file(&app->runtime, app->source, engine,
                             app->project.game_module)) {
    fprintf(stderr, "Player game: %s\n", app->runtime.status);
    return false;
  }
  if (app->hot_reload)
    app->watch = af_asset_watch_create();
  app->runtime.auto_reload = app->hot_reload;
  af_engine_set_scene(engine, app->runtime.scene);
  app->preferences = af_player_preferences_default(
      af_scene_get_render_settings(app->runtime.scene));
  if (!app->frames || app->menu_test) {
    char *directory = SDL_GetPrefPath(
        "Afterlight", app->menu_test ? "PlayerTests" : "Player");
    if (directory) {
      uint64_t key = UINT64_C(14695981039346656037);
      for (const unsigned char *p = (const unsigned char *)app->project.name;
           *p; ++p) {
        key ^= *p;
        key *= UINT64_C(1099511628211);
      }
      snprintf(app->preferences_path, sizeof(app->preferences_path),
               "%s%016" PRIx64 ".json", directory, key);
      snprintf(app->save_path, sizeof(app->save_path), "%s%016" PRIx64 ".save",
               directory, key);
      SDL_free(directory);
      af_asset_error preferences_error;
      app->preferences_loaded = af_player_preferences_load(
          app->preferences_path, &app->preferences, &preferences_error);
    }
  }
  apply_preferences(app);
  apply_upscaler_preference(app, engine);
  set_menu(app, engine, (!app->frames || app->menu_test));
  return true;
}
static void update(af_engine *engine, double delta, void *memory) {
  player *app = memory;
  if (app->frames &&
      af_engine_get_stats(engine).rendered_frames >= app->frames) {
    af_engine_request_quit(engine);
    return;
  }
  if (app->menu_test) {
    uint64_t stage = app->menu_test++;
    bool ok = true;
    if (stage == 8) {
      ok = app->menu && app->runtime.paused &&
           af_engine_get_stats(engine).simulation_ticks == 0 &&
           nk_window_find(&app->gui.context, "Game menu");
      app->started = true;
      set_menu(app, engine, false);
    }
    if (stage == 24) {
      ok = af_engine_get_stats(engine).simulation_ticks > 0 &&
           !app->runtime.paused;
      app->preferences.aa = AF_AA_TAA;
      apply_preferences(app);
      store_preferences(app);
      ok = ok && ed_runtime_save_game(&app->runtime, app->save_path) &&
           ed_runtime_load_game(&app->runtime, app->save_path);
      apply_preferences(app);
      set_menu(app, engine, true);
    }
    if (stage == 44) {
      af_gameplay_status finish = af_engine_gameplay_status(engine);
      finish.finished = true;
      finish.running = false;
      finish.finish_time = 12.34;
      af_engine_set_gameplay_status(engine, finish);
    }
    if (stage == 50)
      ok =
          app->menu && app->finished_recorded && app->preferences.best_time > 0;
    if (stage == 52) {
      ok = ed_runtime_load_game(&app->runtime, app->save_path);
      apply_preferences(app);
      set_menu(app, engine, false);
    }
    if (stage == 56)
      ok = !app->finished_recorded &&
           !af_engine_gameplay_status(engine).finished;
    if (stage == 40) {
      af_player_preferences saved;
      af_asset_error error;
      ok = app->menu && app->runtime.paused &&
           af_player_preferences_load(app->preferences_path, &saved, &error) &&
           saved.aa == AF_AA_TAA;
      set_menu(app, engine, false);
    }
    if (!ok) {
      fprintf(stderr, "Player menu test failed at stage %" PRIu64 "\n", stage);
      app->failed = true;
      af_engine_request_quit(engine);
      return;
    }
  }
  if (app->watch) {
    af_asset_error error;
    af_asset_watch_update(app->watch, engine, app->runtime.scene, delta,
                          &error);
    if (error.message[0])
      fprintf(stdout, "%s\n", error.message);
  }
  bool escape = af_engine_key_pressed(engine, AF_KEY_ESCAPE),
       toggle = af_engine_key_pressed(engine, AF_KEY_F6);
  if (escape || toggle)
    set_menu(app, engine, !app->menu);
  uint64_t transition = app->runtime.transition_count;
  ed_runtime_frame_update(&app->runtime, delta,
                          !app->menu && !escape && !toggle);
  af_engine_set_scene(engine, app->runtime.scene);
  if (app->runtime.transition_count != transition) {
    apply_preferences(app);
    app->finished_recorded = false;
  }
  af_engine_set_simulation_paused(engine, app->runtime.paused);
  af_gameplay_status status = af_engine_gameplay_status(engine);
  if (!status.finished)
    app->finished_recorded = false;
  if (status.finished && !app->finished_recorded) {
    app->finished_recorded = true;
    if (!app->frames || app->menu_test)
      set_menu(app, engine, true);
    if (status.finish_time > 0 &&
        (app->preferences.best_time == 0 ||
         strcmp(app->preferences.best_scene, app->runtime.current_scene) ||
         status.finish_time < app->preferences.best_time)) {
      app->preferences.best_time = status.finish_time;
      snprintf(app->preferences.best_scene, sizeof(app->preferences.best_scene),
               "%s", app->runtime.current_scene);
      store_preferences(app);
    }
  }
  int w, h;
  af_engine_get_window_size(engine, &w, &h);
  ed_gui_input(&app->gui, engine);
  bool character = false;
  for (size_t i = 0; i < af_scene_object_count(app->runtime.scene); ++i)
    character |=
        af_scene_object_at(app->runtime.scene, i)->gameplay.player.enabled;
  if (!app->menu)
    ed_play_hud(&app->gui.context, engine, (af_rect){0, 0, (float)w, (float)h},
                false, false, false, character);
  else {
    struct nk_context *ctx = &app->gui.context;
    float width = fminf(460, (float)w - 24), height = fminf(760, (float)h - 24);
    if (nk_begin(ctx, "Game menu",
                 nk_rect(((float)w - width) / 2, ((float)h - height) / 2, width,
                         height),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
      nk_layout_row_dynamic(ctx, 30, 1);
      nk_label(ctx, app->project.name, NK_TEXT_LEFT);
      nk_label(ctx,
               status.finished ? "Course complete"
               : app->started  ? "Paused"
                               : "Ready to play",
               NK_TEXT_LEFT);
      if (nk_button_label(ctx, status.finished ? "Continue exploring"
                               : app->started  ? "Resume"
                                               : "Start")) {
        app->started = true;
        set_menu(app, engine, false);
      }
      if (app->save_path[0] &&
          nk_button_label(ctx, "Continue saved progress")) {
        if (ed_runtime_load_game(&app->runtime, app->save_path)) {
          apply_preferences(app);
          app->started = true;
          set_menu(app, engine, false);
        }
        snprintf(app->message, sizeof(app->message), "%s", app->runtime.status);
      }
      if (app->save_path[0] && nk_button_label(ctx, "Save progress")) {
        ed_runtime_save_game(&app->runtime, app->save_path);
        snprintf(app->message, sizeof(app->message), "%s", app->runtime.status);
      }
      if (nk_button_label(ctx, "Restart course")) {
        if (ed_runtime_request_scene(&app->runtime,
                                     app->runtime.current_scene) &&
            ed_runtime_transition(&app->runtime)) {
          apply_preferences(app);
          app->started = true;
          app->finished_recorded = false;
          set_menu(app, engine, false);
        }
      }
      if (!strcmp(app->runtime.current_scene, "water-course") ||
          !strcmp(app->runtime.current_scene, "water-expedition")) {
        if (nk_button_label(ctx, "Next course")) {
          const char *next = !strcmp(app->runtime.current_scene, "water-course")
                                 ? "water-expedition"
                                 : "water-course";
          if (ed_runtime_request_scene(&app->runtime, next) &&
              ed_runtime_transition(&app->runtime)) {
            apply_preferences(app);
            app->started = true;
            app->finished_recorded = false;
            set_menu(app, engine, false);
          }
        }
      }
      static const char *modes[] = {"Off", "FXAA", "SMAA", "TAA"};
      nk_label(ctx, "Anti-aliasing", NK_TEXT_LEFT);
      int aa = nk_combo(ctx, modes, 4, (int)app->preferences.aa, 26,
                        nk_vec2(width - 32, 150));
      bool changed = aa != (int)app->preferences.aa;
      app->preferences.aa = (af_anti_aliasing)aa;
      changed |=
          nk_property_float(ctx, "Sharpening", 0, &app->preferences.sharpness,
                            1, .05f, .005f) != 0;
      if (app->preferences.aa == AF_AA_TAA)
        changed |= nk_property_float(ctx, "History weight", 0,
                                     &app->preferences.history, .98f, .05f,
                                     .005f) != 0;
      changed |=
          nk_property_float(ctx, "Exposure", -10, &app->preferences.exposure,
                            10, .25f, .025f) != 0;
      changed |=
          nk_property_float(ctx, "Mouse sensitivity", .1f,
                            &app->preferences.sensitivity, 4, .1f, .01f) != 0;
      size_t upscaler_count = 0;
      size_t upscaler_indices[16];
      const char *upscaler_names[17];
      upscaler_names[0] = "Off";
      int current_upscaler = 0;
      for (size_t i = 0; i < af_engine_extension_count(engine) && upscaler_count < 16; ++i) {
        af_extension_info info;
        if (af_engine_get_extension(engine, i, &info) && info.vulkan_upscaler) {
          upscaler_indices[upscaler_count] = i;
          upscaler_names[upscaler_count + 1] = info.id;
          if (info.enabled)
            current_upscaler = (int)upscaler_count + 1;
          upscaler_count++;
        }
      }
      if (upscaler_count > 0) {
        nk_label(ctx, "Upscaler", NK_TEXT_LEFT);
        int selected_upscaler = nk_combo(ctx, upscaler_names, (int)upscaler_count + 1, current_upscaler, 26,
                                         nk_vec2(width - 32, 150));
        if (selected_upscaler != current_upscaler) {
          if (selected_upscaler == 0) {
            for (size_t u = 0; u < upscaler_count; ++u)
              af_engine_set_extension_enabled(engine, upscaler_indices[u], false);
            snprintf(app->preferences.upscaler, sizeof(app->preferences.upscaler), "off");
          } else {
            size_t chosen_idx = upscaler_indices[selected_upscaler - 1];
            af_engine_set_extension_enabled(engine, chosen_idx, true);
            af_extension_info info;
            if (af_engine_get_extension(engine, chosen_idx, &info))
              snprintf(app->preferences.upscaler, sizeof(app->preferences.upscaler), "%s", info.id);
          }
          current_upscaler = selected_upscaler;
          store_preferences(app);
        }

        if (current_upscaler > 0) {
          uint32_t quality_count = af_engine_upscaler_quality_count(engine);
          if (quality_count) {
            const char *names[16];
            if (quality_count > 16)
              quality_count = 16;
            for (uint32_t i = 0; i < quality_count; ++i) {
              names[i] = af_engine_upscaler_quality_name(engine, i);
              if (!names[i])
                names[i] = "Default";
            }
            const char *active_name = upscaler_names[current_upscaler];
            const char *quality_label = strstr(active_name, "DLSS") ? "DLSS quality" :
                                        strstr(active_name, "FSR") ? "FSR quality" : "Upscaler quality";
            nk_label(ctx, quality_label, NK_TEXT_LEFT);
            int current = (int)af_engine_upscaler_quality(engine);
            int selected = nk_combo(ctx, names, (int)quality_count, current, 26,
                                    nk_vec2(width - 32, 150));
            if (selected != current &&
                af_engine_set_upscaler_quality(engine, (uint32_t)selected)) {
              app->preferences.upscaler_quality = (uint32_t)selected;
              store_preferences(app);
            }
          }
        }
      }
      nk_bool ray_tracing = app->preferences.ray_traced_shadows ||
                            app->preferences.ray_traced_water_reflections;
      if (nk_checkbox_label(ctx, "Ray tracing", &ray_tracing)) {
        app->preferences.ray_traced_shadows = ray_tracing != 0;
        app->preferences.ray_traced_water_reflections = ray_tracing != 0;
        changed = true;
      }
      if (changed) {
        apply_preferences(app);
        store_preferences(app);
      }
      if (app->preferences.best_time > 0 &&
          !strcmp(app->preferences.best_scene, app->runtime.current_scene)) {
        char best[96];
        snprintf(best, sizeof(best), "Best time: %.2f seconds",
                 app->preferences.best_time);
        nk_label(ctx, best, NK_TEXT_LEFT);
      }
      nk_label_wrap(ctx, "WASD move | Space jump/swim | Ctrl dive | F/V "
                         "grab/throw | E pour | N change course");
      if (app->message[0])
        nk_label_wrap(ctx, app->message);
      if (nk_button_label(ctx, "Quit"))
        af_engine_request_quit(engine);
    }
    nk_end(ctx);
  }
  if (!ed_gui_render(&app->gui, engine, (float)w, (float)h)) {
    app->failed = true;
    af_engine_request_quit(engine);
  }
}
static void fixed(af_engine *engine, double delta, void *memory) {
  player *app = memory;
  ed_runtime_update(&app->runtime, delta, true);
  af_engine_set_scene(engine, app->runtime.scene);
}
static void shutdown(af_engine *engine, void *memory) {
  player *app = memory;
  af_render_settings render=af_scene_get_render_settings(app->runtime.scene);
  af_engine_set_scene(engine, NULL);
  ed_runtime_stop(&app->runtime);
  af_asset_watch_destroy(app->watch);
  af_scene_destroy(app->source);
  ed_gui_destroy(&app->gui);
  if (app->menu_test) {
    remove(app->preferences_path);
    remove(app->save_path);
  }
  af_engine_stats stats = af_engine_get_stats(engine);
  printf("Player: %" PRIu64 " frames; %u validation errors.\n",
         stats.rendered_frames, stats.validation_errors);
  if(render.ray_traced_shadows)printf("Ray shadows: %s; %zu instances; %llu retained bytes.\n",stats.ray_traced_shadows_active?"hardware ray queries":"raster fallback",stats.ray_instances,(unsigned long long)stats.ray_tracing_bytes);
  if (render.ray_traced_water_reflections)
    printf("Ray water reflections: %s.\n",
           stats.ray_traced_water_reflections_active ? "hardware ray queries"
                                                    : "environment fallback");
  af_bundle_cleanup();
}
typedef struct ext_discovery {
  const char **paths;
  size_t *count;
  char storage[AF_EXTENSION_LIMIT][AF_ASSET_PATH_CAPACITY];
} ext_discovery;

static SDL_EnumerationResult SDLCALL scan_ext_file(void *userdata, const char *dirname, const char *fname) {
  ext_discovery *d = userdata;
  if (*d->count >= AF_EXTENSION_LIMIT) return SDL_ENUM_SUCCESS;
  const char *dot = strrchr(fname, '.');
  if (dot && (!strcmp(dot, ".so") || !strcmp(dot, ".dylib") || !strcmp(dot, ".dll"))) {
    char candidate[AF_ASSET_PATH_CAPACITY];
    int len = snprintf(candidate, sizeof(candidate), "%s/%s", dirname, fname);
    if (len <= 0 || len >= AF_ASSET_PATH_CAPACITY) return SDL_ENUM_CONTINUE;
    for (size_t i = 0; i < *d->count; ++i) {
      if (!strcmp(d->paths[i], candidate)) return SDL_ENUM_CONTINUE;
      const char *existing_base = strrchr(d->paths[i], '/');
      existing_base = existing_base ? existing_base + 1 : d->paths[i];
      if (!strcmp(existing_base, fname)) return SDL_ENUM_CONTINUE;
    }
    size_t idx = *d->count;
    memcpy(d->storage[idx], candidate, (size_t)len + 1);
    d->paths[idx] = d->storage[idx];
    (*d->count)++;
  }
  return SDL_ENUM_CONTINUE;
}

static void discover_ext_in_dir(const char *dir, ext_discovery *d) {
  if (!dir || *d->count >= AF_EXTENSION_LIMIT) return;
  SDL_PathInfo info;
  if (SDL_GetPathInfo(dir, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
    SDL_EnumerateDirectory(dir, scan_ext_file, d);
  }
}

int main(int argc, char **argv) {
  player app = {0};
  bool check = false;
  const char *project = NULL;
  const char *extensions[AF_EXTENSION_LIMIT];
  size_t extension_count = 0;
  af_engine_config config = af_engine_config_default();
  config.show_fps = true;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--project") && i + 1 < argc)
      project = argv[++i];
    else if (!strcmp(argv[i], "--extension") && i + 1 < argc &&
             extension_count < AF_EXTENSION_LIMIT)
      extensions[extension_count++] = argv[++i];
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
      const char *value = argv[++i];
      char *end;
      errno = 0;
      unsigned long long n = strtoull(value, &end, 10);
      if (errno || value[0] < '0' || value[0] > '9' || *end || !n)
        return 1;
      app.frames = (uint64_t)n;
    } else if (!strcmp(argv[i], "--menu-test")) {
      app.menu_test = 1;
      app.frames = 60;
    } else if (!strcmp(argv[i], "--check"))
      check = true;
    else if (!strcmp(argv[i], "--hot-reload"))
      app.hot_reload = true;
    else if (!strcmp(argv[i], "--validation"))
      config.enable_validation = true;
    else if (!strcmp(argv[i], "--no-validation"))
      config.enable_validation = false;
    else if (!strcmp(argv[i], "--help")) {
      puts("afterlight_player [--project PATH] [--frames N] [--hot-reload] "
           "[--extension LIBRARY] "
           "[--validation | --no-validation]\nEscape/F6 opens the "
           "pause/settings menu. Progress and preferences save in the user "
           "data directory. Without --project, loads game.afterlight.project "
           "beside app resources.");
      return 0;
    } else
      return 1;
  }
  char bundled[AF_ASSET_PATH_CAPACITY];
  if (!project && af_bundle_extract(app.bundle_directory, sizeof(app.bundle_directory))) {
    snprintf(bundled, sizeof(bundled), "%s/game.afterlight.project", app.bundle_directory);
    project = bundled;
  }
  if (!project) {
    const char *base = SDL_GetBasePath();
    if (!base || snprintf(bundled, sizeof(bundled), "%sgame.afterlight.project",
                          base) >= (int)sizeof(bundled))
      return 1;
    project = bundled;
  }
  af_asset_error error;
  if (!af_project_load(project, &app.project, &error)) {
    fprintf(stderr, "Player project: %s\n", error.message);
    return 1;
  }
  snprintf(app.project_path, sizeof(app.project_path), "%s", project);
  config.title = app.project.name;
  if (check && extension_count) {
    fputs("--check cannot load engine extensions.\n", stderr);
    return 1;
  }
  if (check)
    return check_project(&app.project);

  ext_discovery disc = {.paths = extensions, .count = &extension_count};
  if (extension_count == 0) {
    if (app.bundle_directory[0]) {
      char b_ext[AF_ASSET_PATH_CAPACITY];
      snprintf(b_ext, sizeof(b_ext), "%s/extensions", app.bundle_directory);
      discover_ext_in_dir(b_ext, &disc);
    }
    char proj_dir[AF_ASSET_PATH_CAPACITY];
    if (af_asset_resolve_path(project, "extensions", proj_dir, sizeof(proj_dir))) {
      discover_ext_in_dir(proj_dir, &disc);
    }
    const char *base = SDL_GetBasePath();
    if (base) {
      char base_ext[AF_ASSET_PATH_CAPACITY];
      snprintf(base_ext, sizeof(base_ext), "%sextensions", base);
      discover_ext_in_dir(base_ext, &disc);
      snprintf(base_ext, sizeof(base_ext), "%s../extensions", base);
      discover_ext_in_dir(base_ext, &disc);
    }
  }
  af_engine *engine = af_engine_create_with_extensions(&config, extensions, extension_count);
  if (!engine)
    return 1;
  const char *required = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
  if (required && !strcmp(required, "1") &&
      !af_engine_get_stats(engine).validation_enabled) {
    af_engine_destroy(engine);
    return 1;
  }
  af_game game = {.init = init,
                  .update = update,
                  .fixed_update = fixed,
                  .shutdown = shutdown,
                  .user_data = &app};
  int result = af_engine_run(engine, &game);
  af_engine_destroy(engine);
  return app.failed ? 1 : result;
}

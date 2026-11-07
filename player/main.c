/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "cube.h"
#include "gui.h"
#include "play_hud.h"
#include "preferences.h"
#include "runtime.h"
#include <SDL3/SDL_filesystem.h>
#include <afterlight/asset_watch.h>
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
  char preferences_path[4096], save_path[4096], message[256];
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
  if (!base ||
      snprintf(font, sizeof(font), "%sfonts/FiraSans-Regular.ttf", base) >=
          (int)sizeof(font) ||
      !ed_gui_init(&app->gui, engine, font))
    return false;
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
  if (status.finished && !app->finished_recorded) {
    app->finished_recorded = true;
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
    float width = fminf(460, (float)w - 24), height = fminf(680, (float)h - 24);
    if (nk_begin(ctx, "Game menu",
                 nk_rect(((float)w - width) / 2, ((float)h - height) / 2, width,
                         height),
                 NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
      nk_layout_row_dynamic(ctx, 30, 1);
      nk_label(ctx, app->project.name, NK_TEXT_LEFT);
      nk_label(ctx, app->started ? "Paused" : "Ready to play", NK_TEXT_LEFT);
      if (nk_button_label(ctx, app->started ? "Resume" : "Start")) {
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
}
int main(int argc, char **argv) {
  player app = {0};
  bool check = false;
  const char *project = NULL;
  af_engine_config config = af_engine_config_default();
  config.show_fps = true;
  for (int i = 1; i < argc; ++i) {
    if (!strcmp(argv[i], "--project") && i + 1 < argc)
      project = argv[++i];
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
           "[--validation | --no-validation]\nEscape/F6 opens the "
           "pause/settings menu. Progress and preferences save in the user "
           "data directory. Without --project, loads game.afterlight.project "
           "beside app resources.");
      return 0;
    } else
      return 1;
  }
  char bundled[AF_ASSET_PATH_CAPACITY];
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
  if (check)
    return check_project(&app.project);
  af_engine *engine = af_engine_create(&config);
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

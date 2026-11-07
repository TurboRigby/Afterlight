/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <SDL3/SDL.h>
#include <afterlight/afterlight.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (!(condition)) {                                                        \
      fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__,       \
              #condition);                                                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static const af_vertex triangles[] = {
    {.position = {-0.80f, 0.50f, 0.0f}, .color = {1.0f, 0.0f, 0.0f, 1.0f}},
    {.position = {-0.15f, 0.50f, 0.0f}, .color = {0.0f, 1.0f, 0.0f, 1.0f}},
    {.position = {-0.45f, -0.50f, 0.0f}, .color = {0.0f, 0.0f, 1.0f, 1.0f}},
    {.position = {0.15f, 0.50f, 0.0f}, .color = {0.0f, 0.0f, 1.0f, 1.0f}},
    {.position = {0.80f, 0.50f, 0.0f}, .color = {1.0f, 0.0f, 0.0f, 1.0f}},
    {.position = {0.45f, -0.50f, 0.0f}, .color = {0.0f, 1.0f, 0.0f, 1.0f}}};

static const uint32_t indices[] = {0, 1, 2, 3, 4, 5};
static const af_mesh_data mesh = {triangles, 6, indices, 6};
static const af_mesh_data small_mesh = {triangles, 3, indices, 3};

typedef struct smoke_state {
  unsigned init_count;
  unsigned updates;
  unsigned shutdown_count;
  bool fail_init;
  bool quit_in_init;
  bool failed;
  SDL_Window *window;
  af_camera camera;
  uint64_t rebuilds_before_duplicate;
  af_scene *scene;
  af_object_id objects[3];
  af_object_id removed_object;
} smoke_state;

static bool push_key(SDL_Window *window, Uint32 type, SDL_Scancode scancode) {
  SDL_Event event = {.key = {.type = type,
                             .windowID = SDL_GetWindowID(window),
                             .scancode = scancode,
                             .down = type == SDL_EVENT_KEY_DOWN}};
  return SDL_PushEvent(&event);
}

static bool game_init(af_engine *engine, void *user_data) {
  smoke_state *state = user_data;
  ++state->init_count;
#ifdef __APPLE__
  const char *press_and_hold = SDL_GetHint(SDL_HINT_MAC_PRESS_AND_HOLD);
  if (!press_and_hold || strcmp(press_and_hold, "0"))
    state->failed = true;
#endif
  if (!(SDL_GetWindowFlags(state->window) & SDL_WINDOW_HIDDEN))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  af_mesh_data invalid = mesh;
  invalid.index_count = 2;
  af_mesh_data out_of_range = mesh;
  out_of_range.vertex_count = 3;
  state->scene = af_scene_create();
  if (!state->scene || !af_engine_upload_mesh(engine, "primary", &mesh) ||
      !af_engine_upload_mesh(engine, "secondary", &small_mesh) ||
      af_engine_upload_mesh(engine, "primary", &invalid) ||
      af_engine_upload_mesh(engine, "primary", &out_of_range)) {
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
    return false;
  }
  /* Grow and compact the GPU asset registry; each stack name must be copied. */
  for (unsigned i = 0; i < 9; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "temporary-%u", i);
    if (!af_engine_upload_mesh(engine, name, &small_mesh)) {
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
      return false;
    }
  }
  for (unsigned i = 0; i < 9; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "temporary-%u", i);
    if (!af_engine_remove_mesh(engine, name)) {
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
      return false;
    }
  }
  state->objects[0] = af_scene_add_object(state->scene, "First", "primary");
  state->objects[1] = af_scene_add_object(state->scene, "Shared", "primary");
  state->objects[2] =
      af_scene_add_object(state->scene, "Other mesh", "secondary");
  if (!state->objects[0] || !state->objects[1] || !state->objects[2] ||
      af_engine_get_stats(engine).loaded_meshes != 2) {
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
    return false;
  }
  const unsigned char texels[] = {255, 255, 255, 255, 100, 40,  20,  255,
                                  30,  90,  180, 255, 255, 255, 255, 255};
  if (!af_engine_upload_texture(engine, "checker", texels, 2, 2) ||
      af_engine_upload_texture(engine, "checker", texels, 0, 2) ||
      af_engine_get_stats(engine).loaded_textures != 1) {
    state->failed = true;
    return false;
  }
  af_material material = af_material_default();
  snprintf(material.texture, sizeof(material.texture), "checker");
  if (!af_scene_set_object_material(state->scene, state->objects[0],
                                    material)) {
    state->failed = true;
    return false;
  }
  af_engine_set_scene(engine, state->scene);
  state->camera = af_camera_default();
  af_camera invalid_camera = state->camera;
  invalid_camera.near_clip = -1;
  if (!af_engine_set_camera(engine, &state->camera) ||
      af_engine_set_camera(engine, &invalid_camera)) {
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
    return false;
  }
  int window_width = 0, window_height = 0;
  af_engine_get_window_size(engine, &window_width, &window_height);
  af_vec2 center = {(float)window_width / 2, (float)window_height / 2};
  if (af_engine_pick_object(engine, center) != state->objects[0])
    state->failed = true;
  af_scene_set_object_visible(state->scene, state->objects[0], false);
  if (af_engine_pick_object(engine, center) != state->objects[1])
    state->failed = true;
  af_scene_set_object_visible(state->scene, state->objects[0], true);
  af_transform closer = af_transform_identity();
  closer.position.z = 1;
  closer.position.x = -0.45f;
  closer.scale.x = -1;
  af_scene_set_object_transform(state->scene, state->objects[2], closer);
  if (af_engine_pick_object(engine, center) != state->objects[2])
    state->failed = true;
  closer.scale.x = 0;
  af_scene_set_object_transform(state->scene, state->objects[2], closer);
  if (af_engine_pick_object(engine, center) != state->objects[0])
    state->failed = true;
  af_scene_set_object_transform(state->scene, state->objects[2],
                                af_transform_identity());
  af_rect viewport = {20, 20, (float)window_width - 40,
                      (float)window_height - 40};
  af_engine_set_viewport(engine, &viewport);
  if (af_engine_pick_object(engine, (af_vec2){0, 0}) != AF_OBJECT_NONE ||
      af_engine_pick_object(engine, center) != state->objects[0])
    state->failed = true;
  af_engine_set_viewport(engine, NULL);
  if (state->quit_in_init)
    af_engine_request_quit(engine);
  return !state->fail_init;
}

static void game_update(af_engine *engine, double delta, void *user_data) {
  smoke_state *state = user_data;
  ++state->updates;
  if (delta < 0 || delta > 0.1)
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (SDL_GetWindowFlags(state->window) & SDL_WINDOW_HIDDEN)
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 16) {
    const unsigned char texel[] = {30, 150, 100, 255};
    if (!af_engine_upload_texture(engine, "checker", texel, 1, 1))
      state->failed = true;
  }
  if (state->updates == 18 && !af_engine_remove_texture(engine, "checker"))
    state->failed = true;
  if (state->updates == 19 && af_engine_get_stats(engine).missing_textures != 1)
    state->failed = true;
  if (state->updates == 13) {
    state->rebuilds_before_duplicate =
        af_engine_get_stats(engine).swapchain_rebuilds;
    SDL_Event duplicate = {
        .window = {.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
                   .windowID = SDL_GetWindowID(state->window)}};
    if (!SDL_PushEvent(&duplicate))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 15 && af_engine_get_stats(engine).swapchain_rebuilds !=
                                  state->rebuilds_before_duplicate)
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 5 &&
      !push_key(state->window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 6) {
    if (!af_engine_key_down(engine, AF_KEY_W) ||
        !af_engine_key_pressed(engine, AF_KEY_W))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
    SDL_Event repeat = {.key = {.type = SDL_EVENT_KEY_DOWN,
                                .windowID = SDL_GetWindowID(state->window),
                                .scancode = SDL_SCANCODE_W,
                                .down = true,
                                .repeat = true}};
    if (!SDL_PushEvent(&repeat) ||
        !push_key(state->window, SDL_EVENT_KEY_UP, SDL_SCANCODE_W))
      state->failed = true;
  }
  if (state->updates == 7) {
    if (af_engine_key_down(engine, AF_KEY_W) ||
        af_engine_key_pressed(engine, AF_KEY_W) ||
        !af_engine_key_repeated(engine, AF_KEY_W))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
    if (!push_key(state->window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_R) ||
        !push_key(state->window, SDL_EVENT_KEY_UP, SDL_SCANCODE_R))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 8 && (!af_engine_key_pressed(engine, AF_KEY_R) ||
                              af_engine_key_down(engine, AF_KEY_R)))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 9 && af_engine_key_pressed(engine, AF_KEY_R))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 10) {
    SDL_Event click = {.button = {.type = SDL_EVENT_MOUSE_BUTTON_DOWN,
                                  .windowID = SDL_GetWindowID(state->window),
                                  .button = SDL_BUTTON_LEFT,
                                  .down = true}};
    if (!SDL_PushEvent(&click))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 11 && !af_engine_mouse_pressed(engine, AF_MOUSE_LEFT))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 12 && af_engine_mouse_pressed(engine, AF_MOUSE_LEFT))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 105 && !af_engine_set_mouse_capture(engine, true))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 106) {
    if (!af_engine_mouse_captured(engine))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
    if (!push_key(state->window, SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
    SDL_Event focus_lost = {
        .window = {.type = SDL_EVENT_WINDOW_FOCUS_LOST,
                   .windowID = SDL_GetWindowID(state->window)}};
    if (!SDL_PushEvent(&focus_lost))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 107 && (af_engine_mouse_captured(engine) ||
                                af_engine_key_down(engine, AF_KEY_W) ||
                                af_engine_key_pressed(engine, AF_KEY_W) ||
                                SDL_GetWindowRelativeMouseMode(state->window)))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  af_camera_rotate(&state->camera, (float)delta * 0.05f, 0);
  if (!af_engine_set_camera(engine, &state->camera))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  af_transform transform = af_transform_identity();
  transform.rotation.y = (float)state->updates * 0.002f;
  if (!af_scene_set_object_transform(state->scene, state->objects[0],
                                     transform))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  transform.position.x = 0.7f;
  if (!af_scene_set_object_transform(state->scene, state->objects[2],
                                     transform))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  /* Replace buffers while frames are in flight, clear the draw list, then
     restore it after resize/minimize to check geometry survives recreation. */
  if (state->updates == 5 || state->updates == 42 || state->updates == 97 ||
      state->updates == 129) {
    af_engine_stats stats = af_engine_get_stats(engine);
    if (stats.loaded_meshes != 2 || stats.draw_calls != 3 ||
        stats.missing_meshes != 0)
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 40 &&
      !af_engine_upload_mesh(engine, "primary", &small_mesh))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 50 &&
      !af_scene_set_object_visible(state->scene, state->objects[1], false))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 52) {
    if (af_engine_get_stats(engine).draw_calls != 2)
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
    if (!af_scene_set_object_visible(state->scene, state->objects[1], true))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 90 && !af_engine_remove_mesh(engine, "primary"))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 92) {
    af_engine_stats stats = af_engine_get_stats(engine);
    if (stats.loaded_meshes != 1 || stats.draw_calls != 1 ||
        stats.missing_meshes != 2)
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 95 && !af_engine_upload_mesh(engine, "primary", &mesh))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 100) {
    state->removed_object = state->objects[1];
    if (!af_scene_remove_object(state->scene, state->removed_object))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 102 && af_engine_get_stats(engine).draw_calls != 2)
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 110) {
    state->objects[1] =
        af_scene_add_object(state->scene, "New shared instance", "primary");
    if (!state->objects[1] || state->objects[1] == state->removed_object ||
        af_scene_get_object(state->scene, state->removed_object))
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 112 && af_engine_get_stats(engine).draw_calls != 3)
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 115 &&
      !af_scene_set_object_mesh(state->scene, state->objects[2], "not-loaded"))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 117 &&
      (af_engine_get_stats(engine).draw_calls != 2 ||
       af_engine_get_stats(engine).missing_meshes != 1))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 120 &&
      !af_scene_set_object_mesh(state->scene, state->objects[2], "secondary"))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 125)
    af_engine_set_scene(engine, NULL);
  if (state->updates == 127) {
    if (af_engine_get_stats(engine).draw_calls != 0 ||
        af_engine_get_stats(engine).missing_meshes != 0)
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
    af_engine_set_scene(engine, state->scene);
  }
  if (state->updates == 20 && !af_engine_set_window_size(engine, 800, 450))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 45 && !af_engine_set_window_size(engine, 320, 480))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 35 || state->updates == 55 || state->updates == 115) {
    int width = 0, height = 0;
    int expected_width =
        state->updates == 35 ? 800 : (state->updates == 55 ? 320 : 640);
    int expected_height =
        state->updates == 35 ? 450 : (state->updates == 55 ? 480 : 360);
    if (!SDL_GetWindowSize(state->window, &width, &height) ||
        width != expected_width || height != expected_height)
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
  }
  if (state->updates == 140 &&
      !(SDL_GetWindowFlags(state->window) & SDL_WINDOW_MINIMIZED))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 160 &&
      (SDL_GetWindowFlags(state->window) & SDL_WINDOW_MINIMIZED)) {
    if (!strcmp(SDL_GetCurrentVideoDriver(), "wayland"))
      fputs("Wayland compositor denied programmatic unminimize; native restore "
            "requires desktop activation. All rendering checks ran before "
            "minimization.\n",
            stdout);
    else
      state->failed = true;
  }
  if (state->updates == 135 && !SDL_MinimizeWindow(state->window))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 145 &&
      (!SDL_RestoreWindow(state->window) || !SDL_RaiseWindow(state->window) ||
       !SDL_SyncWindow(state->window)))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  if (state->updates == 100 && !af_engine_set_window_size(engine, 640, 360))
    do {
      fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
              __LINE__);
      state->failed = true;
    } while (0);
  af_engine_set_clear_color(
      engine,
      (af_color){(float)(state->updates % 60) / 60.0f, 0.05f, 0.15f, 1.0f});
  if (state->updates >= 160 &&
      af_engine_get_stats(engine).frames_per_second > 0) {
    SDL_Event quit = {.type = SDL_EVENT_QUIT};
    if (!SDL_PushEvent(&quit)) {
      do {
        fprintf(stderr, "Smoke update %u line %d failed\n", state->updates,
                __LINE__);
        state->failed = true;
      } while (0);
      af_engine_request_quit(engine);
    }
  }
}

static void game_shutdown(af_engine *engine, void *user_data) {
  smoke_state *state = user_data;
  af_engine_set_scene(engine, NULL);
  af_scene_destroy(state->scene);
  state->scene = NULL; /* Uploaded assets are released by renderer teardown. */
  ++state->shutdown_count;
}

static int exercise_lifecycle(bool fail_init, bool quit_in_init) {
  af_engine_config config = af_engine_config_default();
  config.title = "Afterlight | Vulkan smoke test";
  config.width = 640;
  config.height = 360;
  config.enable_validation = true;
  config.show_fps = true;
  af_engine *engine = af_engine_create(&config);
  CHECK(engine);
  smoke_state state = {.fail_init = fail_init, .quit_in_init = quit_in_init};
  int window_count = 0;
  SDL_Window **windows = SDL_GetWindows(&window_count);
  CHECK(windows && window_count == 1);
  state.window = windows[0];
  SDL_free(windows);
  af_game game = {.init = game_init,
                  .update = game_update,
                  .shutdown = game_shutdown,
                  .user_data = &state};
  int result = af_engine_run(engine, &game);
  CHECK(af_engine_run(engine, &game) != 0); /* A run cannot restart. */
  af_engine_stats stats = af_engine_get_stats(engine);
  if (!fail_init && !quit_in_init) {
    const char *title = SDL_GetWindowTitle(state.window);
    CHECK(strstr(title, "Afterlight | Vulkan smoke test | ") == title);
    CHECK(strstr(title, " FPS | ") && strstr(title, " ms"));
    CHECK(stats.frames_per_second > 0 && stats.average_frame_ms > 0);
    CHECK(fabs(stats.frames_per_second * stats.average_frame_ms - 1000.0) <
          0.001);
  }
  /* Check after destruction too: CTest rejects validation-error output. */
  af_engine_destroy(engine);
  CHECK(result == (fail_init ? 1 : 0));
  CHECK(state.init_count == 1 && state.shutdown_count == 1 && !state.failed);
  CHECK(stats.validation_errors == 0);
  if (!fail_init && !quit_in_init) {
    CHECK(state.updates >= 140 && stats.rendered_frames >= 100);
    printf("Smoke: %" PRIu64
           " presented frames; shared scene meshes, visibility, missing "
           "assets, stable removal, transforms, depth resize, input/focus "
           "reset, minimize/restore requests and quit passed.\n",
           stats.rendered_frames);
  } else
    CHECK(state.updates == 0 && stats.rendered_frames == 0);
  const char *require_validation = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
  if (require_validation && strcmp(require_validation, "1") == 0)
    CHECK(stats.validation_enabled);
  return 0;
}

int main(void) {
  af_engine_config invalid = af_engine_config_default();
  invalid.width = 0;
  CHECK(af_engine_create(&invalid) == NULL);
  CHECK(af_engine_run(NULL, NULL) != 0);
  CHECK(!af_engine_key_down(NULL, AF_KEY_SPACE));
  CHECK(!af_engine_set_window_size(NULL, 0, 0));
  CHECK(!af_engine_upload_mesh(NULL, "mesh", &mesh));
  CHECK(!af_engine_remove_mesh(NULL, "mesh"));
  CHECK(!af_engine_set_camera(NULL, NULL));
  CHECK(!af_engine_key_pressed(NULL, AF_KEY_W));
  CHECK(!af_engine_set_mouse_capture(NULL, true));
  af_engine_destroy(NULL);
  CHECK(exercise_lifecycle(false, false) == 0);
  CHECK(exercise_lifecycle(true, false) == 0);
  CHECK(exercise_lifecycle(false, true) == 0);
  puts("All Afterlight smoke checks passed.");
  return 0;
}

/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
  af_scene *scene;
  size_t objects;
  unsigned ticks, phase;
  double gpu, cpu;
  unsigned samples;
  bool pending, failed;
  unsigned char reference[64 * 64 * 4];
} state;
static bool init(af_engine *e, void *data) {
  state *s = data;
  s->scene = af_scene_create();
  af_mesh_data mesh = cube_mesh_data();
  if (!s->scene || !af_engine_upload_mesh(e, "cube", &mesh))
    return false;
  af_shadow_settings shadows = af_shadow_settings_default();
  shadows.enabled = false;
  shadows.resolution = 512;
  af_scene_set_shadows(s->scene, shadows);
  for (size_t i = 0; i <= s->objects; ++i) {
    af_object_id id = af_scene_add_object(s->scene, "Box", "cube");
    af_transform t = af_transform_identity();
    if (i)
      t.position = (af_vec3){1000 + (float)i, 0, -10};
    if (!af_scene_set_object_transform(s->scene, id, t))
      return false;
  }
  af_engine_set_scene(e, s->scene);
  af_engine_set_simulation_paused(e, true);
  af_engine_set_visibility_culling(e, false);
  return true;
}
static void update(af_engine *e, double dt, void *data) {
  (void)dt;
  state *s = data;
  ++s->ticks;
  af_engine_stats stats = af_engine_get_stats(e);
  if (s->ticks > 8 && s->ticks < 40) {
    s->gpu += stats.gpu_scene_ms;
    s->cpu += stats.cpu_render_ms - stats.cpu_fence_ms - stats.cpu_acquire_ms;
    ++s->samples;
  }
  if (s->ticks == 40) {
    printf("%s,%zu,%zu,%zu,%.6f,%.6f,%llu\n", s->phase ? "culled" : "unculled",
           s->objects + 1, stats.draw_calls, stats.culled_objects,
           s->gpu / s->samples, s->cpu / s->samples,
           (unsigned long long)stats.tracked_gpu_bytes);
    bool good =
        s->phase
            ? (stats.draw_calls == 1 && stats.culled_objects == s->objects)
            : (stats.draw_calls == s->objects + 1 && stats.culled_objects == 0);
    if (!good ||
        !af_engine_test_capture_region(e, (af_vec2){184, 134}, 64, 64)) {
      s->failed = true;
      af_engine_request_quit(e);
    }
    s->pending = true;
    return;
  }
  if (s->pending) {
    unsigned char pixels[64 * 64 * 4];
    if (!af_engine_test_read_pixels(e, pixels, sizeof(pixels), NULL, NULL) ||
        (s->phase && memcmp(s->reference, pixels, sizeof(pixels)))) {
      s->failed = true;
      af_engine_request_quit(e);
      return;
    }
    if (s->phase) {
      s->phase = 2;
      af_engine_request_quit(e);
      return;
    }
    memcpy(s->reference, pixels, sizeof(pixels));
    af_engine_set_visibility_culling(e, true);
    s->phase = 1;
    s->ticks = s->samples = 0;
    s->gpu = s->cpu = 0;
    s->pending = false;
  }
}
static void shutdown(af_engine *e, void *data) {
  state *s = data;
  af_engine_set_scene(e, NULL);
  af_scene_destroy(s->scene);
}
int main(int argc, char **argv) {
  state s = {.objects = 1000};
  bool validation = true;
  for (int argument = 1; argument < argc; ++argument) {
    if (!strcmp(argv[argument], "--no-validation")) {
      validation = false;
      continue;
    }
    if (!strcmp(argv[argument], "--help")) {
      puts("afterlight_renderer_benchmark [object_count] [--no-validation]");
      return 0;
    }
    char *end;
    unsigned long n = strtoul(argv[argument], &end, 10);
    if (*end || !n || n > 60000)
      return 1;
    s.objects = n;
  }
  af_engine_config c = af_engine_config_default();
  c.width = 400;
  c.height = 300;
  c.enable_validation = validation;
  c.title = "Afterlight visibility benchmark";
  af_engine *e = af_engine_create(&c);
  if (!e)
    return 1;
  puts(
      "mode,objects,draws,culled,gpu_scene_ms,cpu_render_less_fence_acquire_ms,"
      "tracked_gpu_bytes");
  af_game game = {
      .init = init, .update = update, .shutdown = shutdown, .user_data = &s};
  int r = af_engine_run(e, &game);
  af_engine_stats stats = af_engine_get_stats(e);
  af_engine_destroy(e);
  return r || s.failed || s.phase != 2 || stats.validation_errors ? 1 : 0;
}

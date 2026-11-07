/* SPDX-License-Identifier: MPL-2.0 */
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct fixture {
  af_scene *scene;
  af_object_id panel;
  unsigned phase, frames;
  bool failed;
  unsigned char fallback[4];
} fixture;

static bool init(af_engine *engine, void *data) {
  fixture *f = data;
  f->scene = af_scene_create();
  if (!f->scene)
    return false;
  af_object_id pool = af_scene_add_object(f->scene, "Pool", "__node");
  af_water water = af_water_default();
  water.half_size = (af_vec2){3, 3};
  water.ripple_strength = 0;
  water.wind = 0;
  water.roughness = .04f;
  if (!af_scene_set_water(f->scene, pool, water))
    return false;

  // This panel is outside the camera frustum but on the center pixel's
  // reflected ray. It cannot affect the pixel via the refraction snapshot.
  static const af_vertex vertices[] = {
      {.position = {-2, .5f, -5}, .color = {1, 1, 1, 1}},
      {.position = {2, .5f, -5}, .color = {1, 1, 1, 1}},
      {.position = {2, 3.f, -5}, .color = {1, 1, 1, 1}},
      {.position = {-2, 3.f, -5}, .color = {1, 1, 1, 1}}};
  static const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
  af_mesh_data mesh = {vertices, 4, indices, 6};
  if (!af_engine_upload_mesh(engine, "panel", &mesh))
    return false;
  f->panel = af_scene_add_object(f->scene, "Off-screen reflector", "panel");
  af_material material = af_material_default();
  material.unlit = true;
  material.cast_shadows = false;
  material.base_color = (af_color){1, 0, 0, 1};
  if (!af_scene_set_object_material(f->scene, f->panel, material))
    return false;

  af_directional_light light = af_directional_light_default();
  light.intensity = light.ambient = 0;
  af_scene_set_light(f->scene, light);
  af_shadow_settings shadows = af_shadow_settings_default();
  shadows.enabled = false;
  shadows.resolution = 512;
  if (!af_scene_set_shadows(f->scene, shadows))
    return false;
  af_render_settings settings = af_scene_get_render_settings(f->scene);
  settings.environment_enabled = true;
  settings.environment_intensity = .1f;
  settings.refraction = true;
  if (!af_scene_set_render_settings(f->scene, settings))
    return false;

  af_camera camera = af_camera_default();
  camera.position = (af_vec3){0, 1.4f, 5};
  camera.yaw = -AF_PI / 2;
  camera.pitch = -atan2f(1.4f, 5);
  camera.vertical_fov = .27f;
  af_engine_set_camera(engine, &camera);
  af_engine_set_clear_color(engine, (af_color){0, 0, 0, 1});
  af_engine_set_scene(engine, f->scene);
  af_engine_set_simulation_paused(engine, true);
  return true;
}

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "Water reflection phase %u: %s (pixel %u/%u/%u)\n",      \
              f->phase, #x, p[0], p[1], p[2]);                                 \
      f->failed = true;                                                        \
      af_engine_request_quit(engine);                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

static void update(af_engine *engine, double delta, void *data) {
  (void)delta;
  fixture *f = data;
  unsigned char p[4] = {0};
  if (++f->frames < 5)
    return;
  if (f->frames == 5) {
    int w, h;
    af_engine_get_window_size(engine, &w, &h);
    CHECK(af_engine_test_capture_pixel(engine,
                                       (af_vec2){(float)w / 2, (float)h / 2}));
    return;
  }
  CHECK(af_engine_test_read_pixel(engine, p));
  af_engine_stats stats = af_engine_get_stats(engine);
  bool hardware = stats.ray_query_supported;
  if (getenv("AFTERLIGHT_REQUIRE_RAY_QUERY"))
    CHECK(hardware);
  CHECK(stats.ray_traced_shadows_active == (hardware && f->phase == 2));
  printf(
      "Reflection phase %u: %u/%u/%u, hardware %d, active %d, instances %zu\n",
      f->phase, p[0], p[1], p[2], hardware,
      stats.ray_traced_water_reflections_active, stats.ray_instances);

  af_render_settings settings = af_scene_get_render_settings(f->scene);
  af_material material = af_scene_get_object(f->scene, f->panel)->material;
  bool environment_pixel = abs(p[0] - f->fallback[0]) < 4 &&
                           abs(p[1] - f->fallback[1]) < 4 &&
                           abs(p[2] - f->fallback[2]) < 4;
  switch (f->phase) {
  case 0:
    CHECK(!stats.ray_traced_water_reflections_active);
    for (unsigned i = 0; i < 4; ++i)
      f->fallback[i] = p[i];
    settings.ray_traced_water_reflections = true;
    CHECK(af_scene_set_render_settings(f->scene, settings));
    break;
  case 1:
    CHECK(stats.ray_traced_water_reflections_active == hardware);
    if (hardware) {
      CHECK(stats.ray_instances == 1 && stats.ray_tracing_bytes > 0);
      CHECK(p[0] > p[1] + 20 && p[0] > p[2] + 20);
    } else
      CHECK(environment_pixel);
    material.base_color = (af_color){0, 1, 0, 1};
    CHECK(af_scene_set_object_material(f->scene, f->panel, material));
    settings.ray_traced_shadows = true;
    CHECK(af_scene_set_render_settings(f->scene, settings));
    {
      af_shadow_settings shadows = af_scene_get_shadows(f->scene);
      shadows.enabled = true;
      CHECK(af_scene_set_shadows(f->scene, shadows));
    }
    break;
  case 2:
    settings.ray_traced_shadows = false;
    CHECK(af_scene_set_render_settings(f->scene, settings));
    if (hardware)
      CHECK(p[1] > p[0] + 20 && p[1] > p[2] + 20);
    else
      CHECK(environment_pixel);
    {
      af_transform transform = af_transform_identity();
      transform.position.x = 8;
      CHECK(af_scene_set_object_transform(f->scene, f->panel, transform));
    }
    break;
  case 3:
    CHECK(environment_pixel);
    CHECK(af_scene_set_object_transform(f->scene, f->panel,
                                        af_transform_identity()));
    CHECK(af_scene_set_object_visible(f->scene, f->panel, false));
    break;
  case 4:
    CHECK(environment_pixel && stats.ray_instances == 0);
    CHECK(af_scene_set_object_visible(f->scene, f->panel, true));
    material.alpha_mode = AF_ALPHA_MASK;
    CHECK(af_scene_set_object_material(f->scene, f->panel, material));
    break;
  case 5:
    CHECK(environment_pixel && stats.ray_instances == 0);
    settings.ray_traced_water_reflections = false;
    CHECK(af_scene_set_render_settings(f->scene, settings));
    break;
  case 6:
    CHECK(environment_pixel && !stats.ray_traced_water_reflections_active &&
          stats.ray_tracing_bytes == 0);
    af_engine_request_quit(engine);
    break;
  }
  ++f->phase;
  f->frames = 0;
}

static void shutdown(af_engine *engine, void *data) {
  fixture *f = data;
  af_engine_set_scene(engine, NULL);
  af_scene_destroy(f->scene);
}

int main(void) {
  af_engine_config config = af_engine_config_default();
  config.title = "Afterlight water reflection regression";
  config.width = 480;
  config.height = 360;
  config.enable_validation = true;
  af_engine *engine = af_engine_create(&config);
  if (!engine)
    return 1;
  fixture f = {0};
  af_game game = {
      .init = init, .update = update, .shutdown = shutdown, .user_data = &f};
  int result = af_engine_run(engine, &game);
  af_engine_stats stats = af_engine_get_stats(engine);
  af_engine_destroy(engine);
  return result || f.failed || f.phase != 7 || stats.validation_errors;
}

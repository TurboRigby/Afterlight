/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct state {
  af_scene *scene;
  af_object_id water;
  unsigned sample, stage, ticks;
  bool pending, failed;
  unsigned char baseline[64][4];
} state;
static bool init(af_engine *e, void *data) {
  state *s = data;
  s->scene = af_scene_create();
  if (!s->scene)
    return false;
  af_vertex vertices[24];
  memcpy(vertices, cube_vertices, sizeof(vertices));
  for (unsigned i = 0; i < 24; ++i)
    vertices[i].color = (af_color){.9f, .03f, .01f, 1};
  af_mesh_data mesh = {vertices, 24, cube_indices, 36};
  if (!af_engine_upload_mesh(e, "contact-cube", &mesh))
    return false;
  af_object_id wall = af_scene_add_object(s->scene, "Wall", "contact-cube"),
               floor = af_scene_add_object(s->scene, "Floor", "contact-cube");
  af_transform t = af_transform_identity();
  t.position = (af_vec3){4.2f, -1, 0};
  t.scale = (af_vec3){.2f / .65f, 2 / .65f, 5 / .65f};
  if (!af_scene_set_object_transform(s->scene, wall, t))
    return false;
  t.position = (af_vec3){0, -2.12f, 0};
  t.scale = (af_vec3){4.3f / .65f, .12f / .65f, 5.3f / .65f};
  af_material material = af_material_default();
  material.unlit = true;
  if (!af_scene_set_object_transform(s->scene, floor, t) ||
      !af_scene_set_object_material(s->scene, wall, material) ||
      !af_scene_set_object_material(s->scene, floor, material))
    return false;
  s->water = af_scene_add_object(s->scene, "Water", "__node");
  af_water w = af_water_default();
  w.wind = w.ripple_strength = 0;
  w.half_size = (af_vec2){4, 5};
  w.enabled = false;
  w.absorption = (af_vec3){2, 2, 2};
  w.scattering = (af_vec3){0, .2f, .8f};
  if (!af_scene_set_water(s->scene, s->water, w))
    return false;
  af_directional_light light = af_directional_light_default();
  light.intensity = 0;
  light.ambient = .5f;
  af_scene_set_light(s->scene, light);
  af_render_settings settings = af_render_settings_default();
  settings.underwater = false;
  settings.refraction = false;
  af_scene_set_render_settings(s->scene, settings);
  af_engine_set_scene(e, s->scene);
  af_engine_set_simulation_paused(e, true);
  return true;
}
static void update(af_engine *e, double dt, void *data) {
  (void)dt;
  state *s = data;
  if (++s->ticks < 4)
    return;
  if (s->pending) {
    unsigned char p[4];
    if (!af_engine_test_read_pixel(e, p)) {
      s->failed = true;
      af_engine_request_quit(e);
      return;
    }
    if (!(s->stage & 1u))
      memcpy(s->baseline[s->sample], p, 4);
    else
      for (unsigned c = 0; c < 3; ++c)
        if (abs((int)p[c] - s->baseline[s->sample][c]) > 3) {
          fprintf(stderr,
                  "Water/solid depth overlap stage %u sample %u: baseline "
                  "%u,%u,%u, water %u,%u,%u\n",
                  s->stage, s->sample, s->baseline[s->sample][0],
                  s->baseline[s->sample][1], s->baseline[s->sample][2], p[0],
                  p[1], p[2]);
          s->failed = true;
          af_engine_request_quit(e);
          return;
        }
    if (++s->sample == 64) {
      s->sample = 0;
      ++s->stage;
    }
  }
  if (s->stage == 8) {
    puts("Water/solid contact: 64 wall/floor pixels stay stable with "
         "refraction on/off, without boundary z-fighting.");
    af_engine_request_quit(e);
    return;
  }
  af_water w = *af_scene_get_water(s->scene, s->water);
  w.enabled = (s->stage & 1u) != 0;
  af_scene_set_water(s->scene, s->water, w);
  af_render_settings settings = af_scene_get_render_settings(s->scene);
  settings.refraction = s->stage >= 4;
  af_scene_set_render_settings(s->scene, settings);
  af_camera camera = af_camera_default();
  camera.position = (af_vec3){0, -1, 1};
  camera.yaw = (s->stage % 4) < 2 ? 0 : -AF_PI / 2;
  camera.pitch = (s->stage % 4) < 2 ? -.08f : -.7f;
  af_engine_set_camera(e, &camera);
  af_vec2 point = {160 + (float)(s->sample % 8) * 10,
                   115 + (float)(s->sample / 8) * 10};
  if (!af_engine_test_capture_pixel(e, point)) {
    s->failed = true;
    af_engine_request_quit(e);
    return;
  }
  s->pending = true;
}
static void shutdown(af_engine *e, void *data) {
  state *s = data;
  af_engine_set_scene(e, NULL);
  af_scene_destroy(s->scene);
}
int main(void) {
  state s = {0};
  af_engine_config c = af_engine_config_default();
  c.width = 400;
  c.height = 300;
  c.enable_validation = true;
  c.title = "Afterlight water/solid boundary depth";
  af_engine *e = af_engine_create(&c);
  if (!e)
    return 1;
  af_game game = {
      .init = init, .update = update, .shutdown = shutdown, .user_data = &s};
  int result = af_engine_run(e, &game);
  af_engine_stats stats = af_engine_get_stats(e);
  af_engine_destroy(e);
  return result || s.failed || s.stage != 8 || stats.validation_errors ? 1 : 0;
}

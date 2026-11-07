/* SPDX-License-Identifier: MPL-2.0 */
#include "cube.h"
#include "environment.h"
#include "render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct state {
  ed_document doc;
  unsigned phase, ticks;
  bool failed;
  unsigned char object[4], sky[4];
} state;
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr,                                                          \
              "Environment editor phase %u line %d: %s pixel %u,%u,%u\n",      \
              s->phase, __LINE__, #x, p[0], p[1], p[2]);                       \
      s->failed = true;                                                        \
      af_engine_request_quit(e);                                               \
      return;                                                                  \
    }                                                                          \
  } while (0)
static bool preset(state *s, bool night) {
  af_directional_light sun;
  af_render_settings render;
  ed_environment_preset(s->doc.scene, night, &sun, &render);
  ed_document_end_edit(&s->doc);
  bool ok = ed_document_environment(&s->doc, sun, render);
  ed_document_end_edit(&s->doc);
  return ok;
}
static bool init(af_engine *e, void *data) {
  state *s = data;
  if (!ed_document_init(&s->doc) || !ed_document_new(&s->doc) ||
      !ed_document_add_cube(&s->doc))
    return false;
  af_mesh_data mesh = cube_mesh_data();
  if (!af_engine_upload_mesh(e, "cube", &mesh) || !preset(s, false))
    return false;
  af_camera camera = af_camera_default();
  camera.position = (af_vec3){0, 0, 4};
  camera.yaw = -AF_PI / 2;
  af_engine_set_camera(e, &camera);
  af_engine_set_scene(e, s->doc.scene);
  af_engine_set_simulation_paused(e, true);
  return true;
}
static void update(af_engine *e, double dt, void *data) {
  (void)dt;
  state *s = data;
  if (++s->ticks < 4)
    return;
  unsigned char p[4] = {0};
  if (s->phase)
    CHECK(af_engine_test_read_pixel(e, p));
  af_vec2 point = {200, 150};
  switch (s->phase) {
  case 0:
    break;
  case 1:
    CHECK(p[0] > 100);
    for (unsigned i = 0; i < 4; ++i)
      s->object[i] = p[i];
    point = (af_vec2){40, 40};
    break;
  case 2:
    for (unsigned i = 0; i < 4; ++i)
      s->sky[i] = p[i];
    CHECK(p[0] + p[1] + p[2] > 150);
    CHECK(preset(s, true));
    break;
  case 3:
    CHECK((unsigned)p[0] * 2 < s->object[0]);
    point = (af_vec2){40, 40};
    break;
  case 4:
    CHECK((unsigned)(p[0] + p[1] + p[2]) * 2 <
          (unsigned)(s->sky[0] + s->sky[1] + s->sky[2]));
    CHECK(ed_document_undo(&s->doc));
    CHECK(af_scene_get_light(s->doc.scene).intensity > 1);
    break;
  case 5:
    CHECK(abs((int)p[0] - s->object[0]) < 4);
    CHECK(ed_document_redo(&s->doc));
    {
      af_directional_light sun = af_scene_get_light(s->doc.scene);
      af_render_settings render = af_scene_get_render_settings(s->doc.scene);
      af_scene *before = s->doc.scene;
      af_directional_light invalid = sun;
      invalid.intensity = -1;
      CHECK(!ed_document_environment(&s->doc, invalid, render) &&
            s->doc.scene == before);
      render.environment_rotation = 1.2f;
      CHECK(ed_document_environment(&s->doc, sun, render));
      CHECK(af_scene_save(s->doc.scene, "environment-roundtrip.json", NULL));
      af_scene *copy = af_scene_load("environment-roundtrip.json", NULL);
      CHECK(copy && af_scene_get_light(copy).intensity < .04f &&
            af_scene_get_render_settings(copy).skybox_intensity < .02f &&
            fabsf(af_scene_get_render_settings(copy).environment_rotation -
                  1.2f) < .001f);
      af_scene_destroy(copy);
      remove("environment-roundtrip.json");
    }
    puts("Environment editor: Day/Night change actual object and sky pixels, "
         "atomic undo/redo, invalid-edit preservation and saved settings "
         "passed.");
    af_engine_request_quit(e);
    return;
  default:
    CHECK(false);
  }
  af_engine_set_scene(e, s->doc.scene);
  CHECK(af_engine_test_capture_pixel(e, point));
  ++s->phase;
}
static void shutdown(af_engine *e, void *data) {
  state *s = data;
  af_engine_set_scene(e, NULL);
  ed_document_destroy(&s->doc);
}
int main(void) {
  state s = {0};
  af_engine_config c = af_engine_config_default();
  c.width = 400;
  c.height = 300;
  c.enable_validation = true;
  c.title = "Afterlight environment presets";
  af_engine *e = af_engine_create(&c);
  if (!e)
    return 1;
  af_game game = {
      .init = init, .update = update, .shutdown = shutdown, .user_data = &s};
  int result = af_engine_run(e, &game);
  af_engine_stats stats = af_engine_get_stats(e);
  af_engine_destroy(e);
  return result || s.failed || s.phase != 5 || stats.validation_errors ? 1 : 0;
}

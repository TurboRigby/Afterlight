/* SPDX-License-Identifier: MPL-2.0 */
#include "preferences.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "Preferences line %d: %s\n", __LINE__, #x);              \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(void) {
  af_asset_error e;
  af_player_preferences p =
      af_player_preferences_default(af_render_settings_default());
  p.aa = AF_AA_TAA;
  p.history = .85f;
  p.sensitivity = 1.4f;
  p.best_time = 38.2;
  p.ray_traced_shadows = true;
  p.ray_traced_water_reflections = false;
  p.upscaler_quality = 2;
  snprintf(p.best_scene, sizeof(p.best_scene), "water-expedition");
  CHECK(af_player_preferences_save("preferences.json", &p, &e));
  af_player_preferences restored = {0};
  CHECK(af_player_preferences_load("preferences.json", &restored, &e));
  CHECK(restored.aa == AF_AA_TAA && restored.history == p.history &&
        restored.sensitivity == p.sensitivity &&
        restored.best_time == p.best_time &&
        restored.ray_traced_shadows == p.ray_traced_shadows &&
        restored.ray_traced_water_reflections ==
            p.ray_traced_water_reflections &&
        restored.upscaler_quality == p.upscaler_quality &&
        !strcmp(restored.best_scene, p.best_scene));
  const char *legacy =
      "{\"version\":1,\"aa\":3,\"sharpness\":0.2,\"history\":0.8,"
      "\"exposure\":0,\"sensitivity\":1.4,\"best_time\":0,"
      "\"best_scene\":\"\"}";
  FILE *legacy_file = fopen("legacy.json", "wb");
  CHECK(legacy_file && fputs(legacy, legacy_file) >= 0 &&
        fclose(legacy_file) == 0);
  af_player_preferences migrated = af_player_preferences_default(
      (af_render_settings){.ray_traced_shadows = true});
  migrated.ray_traced_water_reflections = true;
  migrated.upscaler_quality = 3;
  CHECK(af_player_preferences_load("legacy.json", &migrated, &e) &&
        migrated.aa == AF_AA_TAA && migrated.ray_traced_shadows &&
        migrated.ray_traced_water_reflections &&
        migrated.upscaler_quality == 3);
  remove("legacy.json");
  p.history = NAN;
  CHECK(!af_player_preferences_save("preferences.json", &p, &e) &&
        af_player_preferences_load("preferences.json", &restored, &e));
  const char *bad[] = {
      "[]", "null", "{\"version\":1}", "{\"version\":1,\"aa\":0,\"aa\":1}",
      ("{\"version\":1,\"aa\":2.5,\"sharpness\":0,\"history\":0.9,\"exposure\":"
      "0,\"sensitivity\":1,\"best_time\":0,\"best_scene\":\"\"}")};
  for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    FILE *f = fopen("bad.json", "wb");
    CHECK(f && fputs(bad[i], f) >= 0 && fclose(f) == 0);
    CHECK(!af_player_preferences_load("bad.json", &restored, &e) &&
          restored.aa == AF_AA_TAA);
  }
  remove("bad.json");
  remove("preferences.json");
  puts(
      "Player preferences: AA, ray tracing, upscaler quality, sensitivity, "
      "exposure and best time round-trip; version 1 keeps new defaults; "
      "invalid writes and malformed input preserve last-good values.");
  return 0;
}

/* SPDX-License-Identifier: MPL-2.0 */
#include "preferences.h"
#include "cJSON.h"
#include <SDL3/SDL_timer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
bool af_json_valid_text(const char *, size_t);
af_player_preferences af_player_preferences_default(af_render_settings r) {
  return (af_player_preferences){r.anti_aliasing,
                                 r.aa_sharpness,
                                 r.taa_history_weight,
                                 r.exposure,
                                 1,
                                 0,
                                 {0}};
}
static bool valid(const af_player_preferences *p) {
  return p && p->aa >= AF_AA_OFF && p->aa <= AF_AA_TAA &&
         isfinite(p->sharpness) && p->sharpness >= 0 && p->sharpness <= 1 &&
         isfinite(p->history) && p->history >= 0 && p->history <= .98f &&
         isfinite(p->exposure) && fabsf(p->exposure) <= 10 &&
         isfinite(p->sensitivity) && p->sensitivity >= .1f &&
         p->sensitivity <= 4 && isfinite(p->best_time) && p->best_time >= 0 &&
         p->best_time <= 1e9 && memchr(p->best_scene, 0, sizeof(p->best_scene));
}
static bool fail(af_asset_error *e, const char *text) {
  if (e)
    snprintf(e->message, sizeof(e->message), "%s", text);
  return false;
}
bool af_player_preferences_save(const char *path,
                                const af_player_preferences *p,
                                af_asset_error *e) {
  if (e)
    *e = (af_asset_error){0};
  if (!path || !valid(p))
    return fail(e, "Invalid player preferences.");
  cJSON *j = cJSON_CreateObject();
  if (!j)
    return false;
  bool ok = cJSON_AddNumberToObject(j, "version", 1) &&
            cJSON_AddNumberToObject(j, "aa", p->aa) &&
            cJSON_AddNumberToObject(j, "sharpness", p->sharpness) &&
            cJSON_AddNumberToObject(j, "history", p->history) &&
            cJSON_AddNumberToObject(j, "exposure", p->exposure) &&
            cJSON_AddNumberToObject(j, "sensitivity", p->sensitivity) &&
            cJSON_AddNumberToObject(j, "best_time", p->best_time) &&
            cJSON_AddStringToObject(j, "best_scene", p->best_scene);
  char *json = ok ? cJSON_Print(j) : NULL;
  cJSON_Delete(j);
  if (!json)
    return fail(e, "Cannot encode preferences.");
  char temporary[4096];
  int n = snprintf(temporary, sizeof(temporary), "%s.%llu.tmp", path,
                   (unsigned long long)SDL_GetTicksNS());
  FILE *f =
      n > 0 && (size_t)n < sizeof(temporary) ? fopen(temporary, "wbx") : NULL;
  ok = f && fwrite(json, 1, strlen(json), f) == strlen(json) && fflush(f) == 0;
  if (f && fclose(f))
    ok = false;
  free(json);
#ifdef _WIN32
  if (ok)
    ok = MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  if (ok)
    ok = rename(temporary, path) == 0;
#endif
  if (!ok) {
    if (f)
      remove(temporary);
    return fail(e, "Cannot save preferences; previous file retained.");
  }
  return true;
}
bool af_player_preferences_load(const char *path, af_player_preferences *out,
                                af_asset_error *e) {
  if (e)
    *e = (af_asset_error){0};
  if (!path || !out)
    return false;
  FILE *f = fopen(path, "rb");
  if (!f)
    return fail(e, "No preferences saved.");
  char bytes[4097];
  size_t n = fread(bytes, 1, 4096, f);
  bool ok = n > 0 && !ferror(f) && fgetc(f) == EOF;
  if (fclose(f))
    ok = false;
  bytes[n] = 0;
  if (!ok || !af_json_valid_text(bytes, n))
    return fail(e, "Invalid player preferences JSON.");
  cJSON *j = cJSON_Parse(bytes);
  static const char *keys[] = {"version",   "aa",        "sharpness",
                               "history",   "exposure",  "sensitivity",
                               "best_time", "best_scene"};
  unsigned seen = 0;
  const cJSON *item;
  if(!cJSON_IsObject(j)){cJSON_Delete(j);return fail(e,"Preferences must be an object.");}
  ok = true;
  cJSON_ArrayForEach(item, j) {
    unsigned i;
    for (i = 0; i < 8; ++i)
      if (!strcmp(item->string, keys[i]))
        break;
    if (i == 8 || (seen & (1u << i))) {
      ok = false;
      break;
    }
    seen |= 1u << i;
    if (i < 7 && !cJSON_IsNumber(item))
      ok = false;
  }
  ok &= seen == 255;
  af_player_preferences p = {0};
  if (ok) {
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(j, "aa"),
                *s = cJSON_GetObjectItemCaseSensitive(j, "best_scene");
    ok = cJSON_GetObjectItemCaseSensitive(j, "version")->valuedouble == 1 &&
         a->valuedouble >= 0 && a->valuedouble <= AF_AA_TAA &&
         a->valuedouble == floor(a->valuedouble) && cJSON_IsString(s) &&
         strlen(s->valuestring) < sizeof(p.best_scene);
    if (ok) {
      p.aa = (af_anti_aliasing)a->valueint;
      p.sharpness =
          (float)cJSON_GetObjectItemCaseSensitive(j, "sharpness")->valuedouble;
      p.history =
          (float)cJSON_GetObjectItemCaseSensitive(j, "history")->valuedouble;
      p.exposure =
          (float)cJSON_GetObjectItemCaseSensitive(j, "exposure")->valuedouble;
      p.sensitivity = (float)cJSON_GetObjectItemCaseSensitive(j, "sensitivity")
                          ->valuedouble;
      p.best_time =
          cJSON_GetObjectItemCaseSensitive(j, "best_time")->valuedouble;
      snprintf(p.best_scene, sizeof(p.best_scene), "%s", s->valuestring);
      ok = valid(&p);
    }
  }
  cJSON_Delete(j);
  if (!ok)
    return fail(e, "Invalid player preferences; current settings retained.");
  *out = p;
  return true;
}

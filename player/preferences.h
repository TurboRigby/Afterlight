/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_PLAYER_PREFERENCES_H
#define AF_PLAYER_PREFERENCES_H
#include <afterlight/afterlight.h>
typedef struct af_player_preferences {
    af_anti_aliasing aa;
    float sharpness, history, exposure, sensitivity;
    double best_time;
    char best_scene[64];
    bool ray_traced_shadows, ray_traced_water_reflections;
    uint32_t upscaler_quality;
    char upscaler[64];
} af_player_preferences;
af_player_preferences af_player_preferences_default(af_render_settings);
/* Versions 1 and 2 keep upscaler and ray-tracing fields from *out if missing. */
bool af_player_preferences_load(const char *, af_player_preferences *, af_asset_error *);
bool af_player_preferences_save(const char *, const af_player_preferences *, af_asset_error *);
#endif

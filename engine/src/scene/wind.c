/* SPDX-License-Identifier: MPL-2.0 */
#include "scene_internal.h"
#include <afterlight/wind.h>
#include <math.h>
static bool range(float v, float lo, float hi) {
    return isfinite(v) && v >= lo && v <= hi;
}
af_wind af_wind_default(void) {
    return (af_wind){
        .direction = {1, 0, 0}, .speed = 5, .gust_strength = .25f, .gust_frequency = .2f, .air_density = 1.225f};
}
bool af_wind_valid(af_wind w) {
    return range(w.direction.x, -1, 1) && range(w.direction.z, -1, 1) && w.direction.y == 0 &&
           hypotf(w.direction.x, w.direction.z) > .001f && range(w.speed, 0, 60) && range(w.gust_strength, 0, 1) &&
           range(w.gust_frequency, 0, 2) && range(w.air_density, .01f, 10);
}
af_vec3 af_wind_velocity(af_wind w, double seconds) {
    if (!af_wind_valid(w) || !w.enabled || !isfinite(seconds) || seconds < 0)
        return (af_vec3){0};
    double phase =
        w.gust_frequency > 0 ? fmod(seconds, 100.0 / w.gust_frequency) * w.gust_frequency * 2 * 3.141592653589793 : 0;
    float gust = w.gust_frequency > 0 ? (float)(.65 * sin(phase) + .35 * sin(phase * 1.73)) : 0;
    return af_vec3_scale(af_vec3_normalize(w.direction), w.speed * (1 + w.gust_strength * gust));
}
bool af_scene_set_wind(af_scene *s, af_wind w) {
    if (!s || !af_wind_valid(w))
        return false;
    s->wind = w;
    return true;
}
af_wind af_scene_get_wind(const af_scene *s) {
    return s ? s->wind : af_wind_default();
}

/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_WIND_H
#define AFTERLIGHT_WIND_H
#include <afterlight/math.h>
#include <stdbool.h>
/* Direction points downwind in world XZ. Speed is m/s, frequency Hz and
   density kg/m3. Gust strength is a fraction of the base speed. */
typedef struct af_wind {
    bool enabled;
    af_vec3 direction;
    float speed, gust_strength, gust_frequency, air_density;
} af_wind;
af_wind af_wind_default(void);
bool af_wind_valid(af_wind);
af_vec3 af_wind_velocity(af_wind, double seconds);
#endif

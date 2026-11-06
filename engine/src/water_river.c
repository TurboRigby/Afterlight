/* SPDX-License-Identifier: MPL-2.0 */

#include "water_internal.h"
#include <float.h>
#include <math.h>
float wa_river_length(const af_water *w) {
    float length = 0;
    for (unsigned i = 1; i < w->path_count; ++i)
        length += hypotf(w->path[i].x - w->path[i - 1].x, w->path[i].z - w->path[i - 1].z);
    return length;
}
af_vec3 wa_river_point(const af_water *w, float distance, af_vec3 *tangent) {
    distance = fmaxf(0, fminf(wa_river_length(w), distance));
    for (unsigned i = 1; i < w->path_count; ++i) {
        af_vec3 d = af_vec3_sub(w->path[i], w->path[i - 1]);
        float length = hypotf(d.x, d.z);
        if (distance <= length || i + 1 == w->path_count) {
            *tangent = af_vec3_scale(d, 1 / length);
            return af_vec3_add(w->path[i - 1], af_vec3_scale(d, distance / length));
        }
        distance -= length;
    }
    *tangent = (af_vec3){0, 0, 1};
    return (af_vec3){0};
}
bool wa_river_coordinates(const af_water *w, af_vec3 p, float *side, float *along, float *height,
                          af_vec3 *tangent) {
    float best = FLT_MAX, total = 0;
    bool found = false;
    for (unsigned i = 1; i < w->path_count; ++i) {
        af_vec3 a = w->path[i - 1], d = af_vec3_sub(w->path[i], a);
        float length = hypotf(d.x, d.z),
              raw = ((p.x - a.x) * d.x + (p.z - a.z) * d.z) / (length * length);
        if ((i == 1 && raw < -.0001f) || (i + 1 == w->path_count && raw > 1.0001f)) {
            total += length;
            continue;
        }
        float t = fmaxf(0, fminf(1, raw));
        af_vec3 q = af_vec3_add(a, af_vec3_scale(d, t));
        float dx = p.x - q.x, dz = p.z - q.z, dist = dx * dx + dz * dz;
        if (dist < best) {
            best = dist;
            *tangent = af_vec3_scale(d, 1 / length);
            *height = q.y;
            *side = dx * tangent->z - dz * tangent->x;
            *along = total + t * length;
            found = true;
        }
        total += length;
    }
    return found && best <= (w->half_size.x + 1e-4f) * (w->half_size.x + 1e-4f);
}

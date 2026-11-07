/* SPDX-License-Identifier: MPL-2.0 */
#include "physics_internal.h"
#include <afterlight/water.h>
void ph_wind_step(af_physics *p, float dt) {
  af_wind settings = af_scene_get_wind(p->scene);
  if (!settings.enabled)
    return;
  af_vec3 air = af_wind_velocity(settings, p->simulation_time);
  for (size_t i = 0; i < p->count; ++i) {
    ph_body *b = &p->bodies[i];
    if (b->inv_mass <= 0 || b->character_proxy)
      continue;
    for (size_t j = 0; j < (b->part_count ? b->part_count : 1); ++j) {
      af_world_collider shape = ph_part_world(b, j);
      af_vec3 point = shape.center,
              relative = af_vec3_sub(
                  air, af_vec3_add(b->v, af_vec3_cross(
                                             b->w, af_vec3_sub(point, b->p))));
      float speed = af_vec3_length(relative);
      if (speed < .0001f)
        continue;
      af_vec3 direction = af_vec3_scale(relative, 1 / speed);
      float area, half_y;
      if (shape.type == AF_COLLIDER_BOX) {
        af_vec3 h = shape.half_extents;
        area = 4 * (fabsf(af_vec3_dot(direction, shape.axes[0])) * h.y * h.z +
                    fabsf(af_vec3_dot(direction, shape.axes[1])) * h.x * h.z +
                    fabsf(af_vec3_dot(direction, shape.axes[2])) * h.x * h.y);
        half_y = fabsf(shape.axes[0].y) * h.x + fabsf(shape.axes[1].y) * h.y +
                 fabsf(shape.axes[2].y) * h.z;
      } else {
        area = AF_PI * shape.radius * shape.radius;
        half_y = shape.radius;
        if (shape.type == AF_COLLIDER_CAPSULE) {
          af_vec3 segment = af_vec3_sub(shape.b, shape.a);
          float length = af_vec3_length(segment),
                along = length > .0001f
                            ? af_vec3_dot(direction,
                                          af_vec3_scale(segment, 1 / length))
                            : 0;
          area +=
              2 * shape.radius * length * sqrtf(fmaxf(0, 1 - along * along));
          half_y += fabsf(segment.y) * .5f;
        }
      }
      af_water_sample water = af_physics_sample_water(p, p->scene, point);
      float lo = water.inside ? fmaxf(point.y - half_y, water.bottom) : point.y,
            hi = water.inside ? fminf(point.y + half_y, water.height) : point.y;
      float submerged = fmaxf(0, hi - lo) / (2 * half_y),
            exposed = fmaxf(0, 1 - submerged);
      if (exposed <= 0)
        continue;
      point.y -= ((lo + hi) * .5f - point.y) * submerged / exposed;
      area *= exposed;
      relative = af_vec3_sub(
          air,
          af_vec3_add(b->v, af_vec3_cross(b->w, af_vec3_sub(point, b->p))));
      speed = af_vec3_length(relative);
      if (speed < .0001f)
        continue;
      direction = af_vec3_scale(relative, 1 / speed);
      af_collision_hit hit;
      af_query_filter filter = {.ignore = b->id, .mask = UINT32_MAX};
      if (af_scene_raycast(p->scene, point, af_vec3_scale(direction, -1), 20,
                           filter, &hit))
        continue;
      float drag = .5f * settings.air_density * area * speed;
      if (b->sleeping && drag * speed * b->inv_mass < .01f)
        continue;
      if (b->sleeping)
        ph_wake_island(p, i);
      af_vec3 r = af_vec3_sub(point, b->p);
      float inverse =
          b->inv_mass +
          af_vec3_dot(
              direction,
              af_vec3_cross(ph_inverse_inertia(b, af_vec3_cross(r, direction)),
                            r));
      if (inverse > 0)
        ph_impulse(
            b, af_vec3_scale(relative, -expm1f(-drag * inverse * dt) / inverse),
            point);
    }
  }
}

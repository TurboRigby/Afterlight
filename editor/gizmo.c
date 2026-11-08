/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "gizmo.h"
#include <math.h>
static af_vec2 sub(af_vec2 a, af_vec2 b) {
    return (af_vec2){a.x - b.x, a.y - b.y};
}
static float dot(af_vec2 a, af_vec2 b) {
    return a.x * b.x + a.y * b.y;
}
static float distance(af_vec2 p, af_vec2 a, af_vec2 b) {
    af_vec2 ab = sub(b, a), ap = sub(p, a);
    float d = dot(ab, ab);
    float t = d > 0 ? fmaxf(0, fminf(1, dot(ap, ab) / d)) : 0;
    return hypotf(ap.x - t * ab.x, ap.y - t * ab.y);
}
static af_vec3 axis_vector(int axis) {
    af_vec3 v = {0};
    if (axis == 0)
        v.x = 1;
    else if (axis == 1)
        v.y = 1;
    else
        v.z = 1;
    return v;
}
static af_vec3 direction(af_mat4 m, af_vec3 v) {
    af_vec4 p = af_mat4_transform(m, (af_vec4){v.x, v.y, v.z, 0});
    return (af_vec3){p.x, p.y, p.z};
}
bool ed_project(const af_camera *camera, af_rect viewport, af_vec3 point, af_vec2 *screen) {
    if (!camera || !screen || viewport.width <= 0 || viewport.height <= 0)
        return false;
    af_mat4 vp = af_mat4_mul(af_mat4_perspective(camera->vertical_fov, viewport.width / viewport.height,
                                                 camera->near_clip, camera->far_clip),
                             af_camera_view(camera));
    af_vec4 p = af_mat4_transform(vp, (af_vec4){point.x, point.y, point.z, 1});
    if (!isfinite(p.w) || p.w <= camera->near_clip || p.z < 0 || p.z > p.w)
        return false;
    *screen = (af_vec2){viewport.x + (p.x / p.w + 1) * viewport.width / 2,
                        viewport.y + (p.y / p.w + 1) * viewport.height / 2};
    return true;
}
void ed_gizmo_project(ed_gizmo *g, const af_camera *camera, af_rect viewport, af_vec3 center, af_mat4 parent,
                      af_vec3 rotation) {
    g->camera = *camera;
    g->viewport = viewport;
    g->center = center;
    if (!af_mat4_inverse(parent, &g->parent_inverse)) {
        g->projected = false;
        return;
    }
    g->projected = ed_project(camera, viewport, center, &g->origin);
    if (!g->projected)
        return;
    float depth = af_vec3_dot(af_vec3_sub(center, camera->position), af_camera_forward(camera));
    float size = fmaxf(0.001f, depth * tanf(camera->vertical_fov / 2) * 160 / viewport.height);
    for (int a = 0; a < 3; ++a) {
        af_vec3 parent_axis = direction(parent, axis_vector(a));
        af_vec3 v = af_vec3_normalize(parent_axis);
        g->axis_units[a] = size / fmaxf(0.000001f, af_vec3_length(parent_axis));
        if (!ed_project(camera, viewport, af_vec3_add(center, af_vec3_scale(v, size)), &g->ends[a]))
            g->ends[a] = g->origin;
        af_mat4 euler = af_mat4_identity();
        if (a == 0)
            euler = af_mat4_mul(af_mat4_rotation_z(rotation.z), af_mat4_rotation_y(rotation.y));
        else if (a == 1)
            euler = af_mat4_rotation_z(rotation.z);
        af_vec3 local_axis = af_vec3_normalize(direction(euler, axis_vector(a)));
        g->local_axes[a] = local_axis;
        af_vec3 helper = fabsf(local_axis.y) < 0.9f ? (af_vec3){0, 1, 0} : (af_vec3){1, 0, 0};
        af_vec3 local_u = af_vec3_normalize(af_vec3_cross(local_axis, helper)),
                local_v = af_vec3_cross(local_axis, local_u);
        af_vec3 u = direction(parent, local_u), w = direction(parent, local_v);
        g->plane_normals[a] = af_vec3_normalize(af_vec3_cross(u, w));
        float ring_scale = fmaxf(af_vec3_length(u), af_vec3_length(w));
        u = af_vec3_scale(u, 1 / fmaxf(ring_scale, 0.000001f));
        w = af_vec3_scale(w, 1 / fmaxf(ring_scale, 0.000001f));
        for (int i = 0; i <= ED_GIZMO_SEGMENTS; ++i) {
            float t = (float)i * 2 * AF_PI / ED_GIZMO_SEGMENTS;
            af_vec3 p = af_vec3_add(
                center, af_vec3_scale(af_vec3_add(af_vec3_scale(u, cosf(t)), af_vec3_scale(w, sinf(t))), size));
            if (!ed_project(camera, viewport, p, &g->rings[a][i]))
                g->rings[a][i] = g->origin;
        }
    }
    /* Translation changes parent-local units. */
    if (!g->dragging)
        g->units = size;
}
int ed_gizmo_hit(const ed_gizmo *g, af_vec2 point) {
    if (!g->projected)
        return -1;
    float nearest = 9;
    int axis = -1;
    for (int a = 0; a < 3; ++a) {
        if (g->mode == ED_GIZMO_ROTATE) {
            if (fabsf(af_vec3_dot(g->plane_normals[a], af_camera_forward(&g->camera))) < 0.08f)
                continue;
            for (int i = 1; i <= ED_GIZMO_SEGMENTS; ++i) {
                float d = distance(point, g->rings[a][i - 1], g->rings[a][i]);
                if (d < nearest) {
                    nearest = d;
                    axis = a;
                }
            }
        } else {
            af_vec2 start = {g->origin.x + (g->ends[a].x - g->origin.x) * 0.2f,
                             g->origin.y + (g->ends[a].y - g->origin.y) * 0.2f};
            if (hypotf(g->ends[a].x - g->origin.x, g->ends[a].y - g->origin.y) < 12)
                continue;
            float d = distance(point, start, g->ends[a]);
            if (d < nearest) {
                nearest = d;
                axis = a;
            }
        }
    }
    return axis;
}
static bool plane_vector(const ed_gizmo *g, af_vec2 mouse, af_vec3 *out) {
    af_vec3 forward = af_camera_forward(&g->camera),
            right = af_vec3_normalize(af_vec3_cross(forward, (af_vec3){0, 1, 0})), up = af_vec3_cross(right, forward);
    float spread = tanf(g->camera.vertical_fov / 2),
          x = ((mouse.x - g->viewport.x) / g->viewport.width * 2 - 1) * spread * g->viewport.width / g->viewport.height,
          y = (1 - (mouse.y - g->viewport.y) / g->viewport.height * 2) * spread;
    af_vec3 ray = af_vec3_add(forward, af_vec3_add(af_vec3_scale(right, x), af_vec3_scale(up, y)));
    float denominator = af_vec3_dot(ray, g->plane_normals[g->axis]);
    if (fabsf(denominator) < 1e-6f)
        return false;
    float t = af_vec3_dot(af_vec3_sub(g->center, g->camera.position), g->plane_normals[g->axis]) / denominator;
    if (t <= 0)
        return false;
    af_vec3 local =
        direction(g->parent_inverse, af_vec3_sub(af_vec3_add(g->camera.position, af_vec3_scale(ray, t)), g->center));
    if (af_vec3_length(local) < 1e-6f)
        return false;
    *out = af_vec3_normalize(local);
    return true;
}
void ed_gizmo_begin(ed_gizmo *g, int axis, af_object_id id, af_transform initial, af_vec2 mouse) {
    g->dragging = true;
    g->axis = axis;
    g->object = id;
    g->initial = initial;
    g->start = mouse;
    g->units = g->axis_units[axis];
    g->direction = sub(g->ends[axis], g->origin);
    g->angle = atan2f(mouse.y - g->origin.y, mouse.x - g->origin.x);
    g->accumulated = 0;
    g->plane_valid = g->mode == ED_GIZMO_ROTATE && plane_vector(g, mouse, &g->previous_vector);
}
af_transform ed_gizmo_drag(ed_gizmo *g, af_vec2 mouse) {
    af_transform t = g->initial;
    float amount = 0;
    if (g->mode == ED_GIZMO_ROTATE) {
        af_vec3 vector;
        if (g->plane_valid && plane_vector(g, mouse, &vector)) {
            float delta = atan2f(af_vec3_dot(g->local_axes[g->axis], af_vec3_cross(g->previous_vector, vector)),
                                 af_vec3_dot(g->previous_vector, vector));
            g->accumulated += delta;
            g->previous_vector = vector;
        }
        amount = g->accumulated;
        if (g->snap)
            amount = roundf(amount / (AF_PI / 12)) * (AF_PI / 12);
    } else {
        float d = dot(g->direction, g->direction);
        amount = d > 1 ? dot(sub(mouse, g->start), g->direction) / d : 0;
        if (g->mode == ED_GIZMO_MOVE)
            amount *= g->units;
        if (g->snap) {
            float step = g->mode == ED_GIZMO_MOVE ? 0.5f : 0.1f;
            amount = roundf(amount / step) * step;
        }
    }
    af_vec3 *value = g->mode == ED_GIZMO_MOVE ? &t.position : g->mode == ED_GIZMO_ROTATE ? &t.rotation : &t.scale;
    if (g->axis == 0)
        value->x += amount;
    else if (g->axis == 1)
        value->y += amount;
    else
        value->z += amount;
    return t;
}

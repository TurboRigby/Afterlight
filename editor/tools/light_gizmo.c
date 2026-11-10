/* SPDX-License-Identifier: MPL-2.0 */
#include "light_gizmo.h"
#include <math.h>
static af_vec3 ring(af_vec3 center, af_vec3 u, af_vec3 v, float radius, unsigned index) {
    float angle = 2 * AF_PI * (float)index / 32;
    return af_vec3_add(
        center, af_vec3_scale(af_vec3_add(af_vec3_scale(u, cosf(angle)), af_vec3_scale(v, sinf(angle))), radius));
}
static void line(ed_line3 *out, size_t capacity, size_t *count, af_vec3 a, af_vec3 b) {
    if (*count < capacity)
        out[*count] = (ed_line3){a, b};
    ++*count;
}
size_t ed_light_lines(af_local_light light, ed_line3 *out, size_t capacity) {
    if (!out && capacity)
        return 0;
    size_t count = 0;
    af_vec3 p = light.position;
    if (light.type == AF_LIGHT_POINT) {
        const af_vec3 axes[] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
        for (unsigned plane = 0; plane < 3; ++plane)
            for (unsigned i = 0; i < 32; ++i)
                line(out, capacity, &count, ring(p, axes[plane], axes[(plane + 1) % 3], light.range, i),
                     ring(p, axes[plane], axes[(plane + 1) % 3], light.range, i + 1));
    } else {
        af_vec3 d = af_vec3_normalize(light.direction),
                up = fabsf(d.y) < .99f ? (af_vec3){0, 1, 0} : (af_vec3){1, 0, 0},
                u = af_vec3_normalize(af_vec3_cross(d, up)), v = af_vec3_cross(d, u);
        af_vec3 end = af_vec3_add(p, af_vec3_scale(d, light.range));
        for (unsigned cone = 0; cone < 2; ++cone) {
            float radius = tanf(cone ? light.outer_angle : light.inner_angle) * light.range;
            for (unsigned i = 0; i < 32; ++i)
                line(out, capacity, &count, ring(end, u, v, radius, i), ring(end, u, v, radius, i + 1));
            for (unsigned i = 0; i < 4; ++i)
                line(out, capacity, &count, p, ring(end, u, v, radius, i * 8));
        }
        line(out, capacity, &count, p, end);
    }
    return count;
}
int ed_light_pick(const af_scene *scene, const af_camera *camera, af_rect viewport, af_vec2 mouse) {
    if (mouse.x < viewport.x || mouse.y < viewport.y || mouse.x >= viewport.x + viewport.width ||
        mouse.y >= viewport.y + viewport.height)
        return -1;
    float best = 13, depth = INFINITY;
    int selected = -1;
    for (size_t i = 0; i < af_scene_local_light_count(scene); ++i) {
        const af_local_light *light = af_scene_local_light_at(scene, i);
        af_vec2 screen;
        if (!ed_project(camera, viewport, light->position, &screen))
            continue;
        float distance = hypotf(screen.x - mouse.x, screen.y - mouse.y),
              z = af_vec3_length(af_vec3_sub(light->position, camera->position));
        if (distance < best - .1f || (fabsf(distance - best) < .1f && z < depth)) {
            best = distance;
            depth = z;
            selected = (int)i;
        }
    }
    return selected;
}
af_transform ed_light_transform(af_local_light light) {
    af_transform t = af_transform_identity();
    t.position = light.position;
    af_vec3 d = af_vec3_normalize(light.direction);
    t.rotation = (af_vec3){asinf(fmaxf(-1, fminf(1, d.y))), atan2f(-d.x, -d.z), 0};
    t.scale = (af_vec3){light.range, light.range, light.range};
    return t;
}
af_local_light ed_light_apply(af_local_light light, af_transform t, ed_gizmo_mode mode, int axis) {
    if (mode == ED_GIZMO_MOVE)
        light.position = t.position;
    else if (mode == ED_GIZMO_ROTATE) {
        t.position = (af_vec3){0};
        t.scale = (af_vec3){1, 1, 1};
        af_vec4 d = af_mat4_transform(af_transform_matrix(t), (af_vec4){0, 0, -1, 0});
        light.direction = af_vec3_normalize((af_vec3){d.x, d.y, d.z});
    } else {
        float values[] = {t.scale.x, t.scale.y, t.scale.z};
        if (axis >= 0 && axis < 3)
            light.range = fmaxf(.01f, fminf(10000, values[axis]));
    }
    return light;
}

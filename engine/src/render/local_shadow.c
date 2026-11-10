/* SPDX-License-Identifier: MPL-2.0 */
#include "local_shadow.h"
#include <math.h>
#include "shadow_math.h"
unsigned af_local_shadow_face(af_vec3 d) {
    float x = fabsf(d.x), y = fabsf(d.y), z = fabsf(d.z);
    if (x >= y && x >= z)
        return d.x >= 0 ? 0u : 1u;
    if (y >= z)
        return d.y >= 0 ? 2u : 3u;
    return d.z >= 0 ? 4u : 5u;
}
af_mat4 af_local_shadow_matrix(af_local_light light, unsigned face) {
    static const af_vec3 directions[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    af_vec3 d = light.type == AF_LIGHT_POINT ? directions[face % 6] : af_vec3_normalize(light.direction);
    af_vec3 up = fabsf(d.y) > .99f ? (af_vec3){0, 0, 1} : (af_vec3){0, 1, 0};
    float angle = light.type == AF_LIGHT_POINT ? AF_PI * .5f : fmaxf(.01f, light.outer_angle * 2);
    float near = fminf(.05f, light.range * .01f);
    return af_mat4_mul(af_mat4_perspective(angle, 1, near, light.range),
                       af_mat4_look_at(light.position, af_vec3_add(light.position, d), up));
}
unsigned af_local_shadow_select(const af_local_light *lights, size_t count, const af_camera *camera, float aspect,
                                unsigned indices[AF_LOCAL_SHADOW_LIGHTS], unsigned *eligible) {
    unsigned selected = 0, total = 0;
    float scores[AF_LOCAL_SHADOW_LIGHTS] = {0};
    af_mat4 view = af_mat4_mul(af_mat4_perspective(camera->vertical_fov, aspect, camera->near_clip, camera->far_clip),
                               af_camera_view(camera));
    for (size_t i = 0; i < count; ++i) {
        const af_local_light *l = &lights[i];
        if (!l->enabled || !l->cast_shadows || l->intensity <= 0)
            continue;
        af_vec3 r = {l->range, l->range, l->range};
        af_shadow_bounds bounds =
            af_shadow_transform_bounds(af_mat4_identity(), af_vec3_sub(l->position, r), af_vec3_add(l->position, r));
        if (!af_shadow_intersects(view, &bounds))
            continue;
        ++total;
        float score = l->range / (1 + af_vec3_length(af_vec3_sub(l->position, camera->position)));
        unsigned at = 0;
        while (at < selected && scores[at] >= score)
            ++at;
        if (at >= AF_LOCAL_SHADOW_LIGHTS)
            continue;
        if (selected < AF_LOCAL_SHADOW_LIGHTS)
            ++selected;
        for (unsigned j = selected - 1; j > at; --j) {
            scores[j] = scores[j - 1];
            indices[j] = indices[j - 1];
        }
        scores[at] = score;
        indices[at] = (unsigned)i;
    }
    if (eligible)
        *eligible = total;
    return selected;
}

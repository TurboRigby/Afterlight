/* SPDX-License-Identifier: MPL-2.0 */

#include "physics_internal.h"
af_vec3 ph_shape_local(const ph_body *b, af_vec3 v) {
    return (af_vec3){af_vec3_dot(v, b->shape.axes[0]), af_vec3_dot(v, b->shape.axes[1]),
                     af_vec3_dot(v, b->shape.axes[2])};
}
af_vec3 ph_shape_vector(const ph_body *b, af_vec3 v) {
    return af_vec3_add(
        af_vec3_scale(b->shape.axes[0], v.x),
        af_vec3_add(af_vec3_scale(b->shape.axes[1], v.y), af_vec3_scale(b->shape.axes[2], v.z)));
}
af_world_collider ph_part_world(const ph_body *b, size_t index) {
    if (!b->part_count)
        return b->shape;
    af_world_collider w = b->parts[index];
    w.center = af_vec3_add(b->p, ph_vector(b, w.center));
    for (unsigned i = 0; i < 3; ++i)
        w.axes[i] = ph_vector(b, w.axes[i]);
    w.a = w.b = w.center;
    return w;
}

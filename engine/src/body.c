/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/scene.h>
#include <math.h>
af_rigid_body af_rigid_body_default(void) {
    return (af_rigid_body){.type = AF_BODY_STATIC,
                           .mass = 1,
                           .friction = .6f,
                           .restitution = .1f,
                           .linear_damping = .05f,
                           .angular_damping = .1f,
                           .gravity_scale = 1,
                           .allow_sleep = true};
}
bool af_rigid_body_valid(af_rigid_body b) {
    float v[] = {b.mass,
                 b.friction,
                 b.restitution,
                 b.linear_damping,
                 b.angular_damping,
                 b.gravity_scale,
                 b.velocity.x,
                 b.velocity.y,
                 b.velocity.z,
                 b.angular_velocity.x,
                 b.angular_velocity.y,
                 b.angular_velocity.z};
    for (unsigned i = 0; i < 12; ++i)
        if (!isfinite(v[i]))
            return false;
    return b.type >= AF_BODY_STATIC && b.type <= AF_BODY_DYNAMIC && b.mass >= .001f && b.mass <= 10000 &&
           b.friction >= 0 && b.friction <= 2 && b.restitution >= 0 && b.restitution <= 1 &&
           b.linear_damping >= 0 && b.linear_damping <= 20 && b.angular_damping >= 0 &&
           b.angular_damping <= 20 && fabsf(b.gravity_scale) <= 10 &&
           af_vec3_length(b.velocity) <= 200 && af_vec3_length(b.angular_velocity) <= 100;
}
bool af_scene_set_body(af_scene *scene, af_object_id id, af_rigid_body body) {
    af_scene_object *o = (af_scene_object *)af_scene_get_object(scene, id);
    if (!o || !af_rigid_body_valid(body)||(body.enabled&&body.type!=AF_BODY_STATIC&&o->collider.type==AF_COLLIDER_MESH))
        return false;
    if (body.enabled && body.type == AF_BODY_DYNAMIC &&
        (o->parent || !o->collider.enabled || o->collider.trigger ||
         (o->collider.type != AF_COLLIDER_BOX && o->collider.type != AF_COLLIDER_SPHERE) ||
         af_vec3_length(o->collider.center) > 1e-6f || o->transform.scale.x <= 0 ||
         o->transform.scale.y <= 0 || o->transform.scale.z <= 0))
        return false;
    o->body = body;
    return true;
}

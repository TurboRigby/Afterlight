/* SPDX-License-Identifier: MPL-2.0 */
#include "physics_internal.h"
size_t af_physics_push_character(af_physics *p, af_scene *s, af_object_id ignore, const af_character *character,
                                 af_character_config config, af_vec3 desired, float force, float dt) {
    if (!p || !s || !character || !af_character_config_valid(config) || !ph_finite(character->position) ||
        !ph_finite(desired) || !isfinite(force) || force < 0 || force > 10000 || !isfinite(dt) || dt <= 0 || dt > .1f)
        return 0;
    af_collision_hit hits[64];
    af_query_filter filter = {.ignore = ignore, .ignore_also = p->held, .mask = UINT32_MAX};
    size_t count = af_scene_overlap_capsule(s, character->position, config.radius + .08f, config.half_height, filter,
                                            hits, 64),
           pushed = 0;
    af_object_id seen[64];
    size_t seen_count = 0;
    for (size_t i = 0; i < count && i < 64; ++i) {
        const af_scene_object *o = af_scene_get_object(s, hits[i].object);
        if (!o || !o->body.enabled || o->body.type != AF_BODY_DYNAMIC || fabsf(hits[i].normal.y) > .6f)
            continue;
        bool duplicate = false;
        for (size_t k = 0; k < seen_count; ++k)
            duplicate |= seen[k] == o->id;
        if (duplicate)
            continue;
        seen[seen_count++] = o->id;
        af_vec3 direction = {0}, point = {0};
        size_t samples = 0;
        for (size_t k = i; k < count && k < 64; ++k)
            if (hits[k].object == o->id && fabsf(hits[k].normal.y) <= .6f) {
                direction = af_vec3_add(direction, (af_vec3){-hits[k].normal.x, 0, -hits[k].normal.z});
                point = af_vec3_add(point, hits[k].point);
                ++samples;
            }
        direction = af_vec3_normalize(direction);
        point = af_vec3_scale(point, 1.f / (float)samples);
        af_physics_state state = af_physics_get_state(p, o->id);
        af_vec3 velocity = state.available ? state.velocity : o->body.velocity;
        float speed = af_vec3_dot(af_vec3_sub(desired, velocity), direction),
              j = fminf(force * dt, fmaxf(0, speed * o->body.mass));
        if (j > 0 && af_physics_apply_impulse(p, s, o->id, af_vec3_scale(direction, j), point))
            ++pushed;
    }
    return pushed;
}
af_object_id af_physics_held(const af_physics *p) {
    return p ? p->held : 0;
}
bool af_physics_grab(af_physics *p, af_scene *s, af_object_id id, af_vec3 point) {
    if (!p || !s || !ph_finite(point))
        return false;
    if (!ph_sync(p, s, 0))
        return false;
    for (size_t i = 0; i < p->count; ++i) {
        ph_body *b = &p->bodies[i];
        if (b->id != id)
            continue;
        if (b->inv_mass == 0 || b->config.mass > 50 || af_vec3_length(af_vec3_sub(point, b->p)) > 10)
            return false;
        p->held = id;
        p->grab_local = ph_local(b, af_vec3_sub(point, b->p));
        p->grab_target = point;
        af_physics_wake(p, id);
        return true;
    }
    return false;
}
bool af_physics_move_grab(af_physics *p, af_vec3 target) {
    if (!p || !p->held || !ph_finite(target) || af_vec3_length(target) > 1000000)
        return false;
    p->grab_target = target;
    return true;
}
bool af_physics_release(af_physics *p, af_scene *s, af_vec3 velocity) {
    if (!p || !s || !p->held || !ph_finite(velocity) || af_vec3_length(velocity) > 40)
        return false;
    af_object_id held = p->held;
    p->held = 0;
    af_physics_state state = af_physics_get_state(p, held);
    if (!state.available)
        return false;
    return af_physics_apply_impulse(p, s, held, af_vec3_scale(velocity, state.mass), state.position);
}
void ph_hold(af_physics *p, float dt) {
    if (!p->held)
        return;
    for (size_t i = 0; i < p->count; ++i) {
        ph_body *b = &p->bodies[i];
        if (b->id != p->held)
            continue;
        if (b->inv_mass == 0) {
            p->held = 0;
            return;
        }
        b->sleeping = false;
        b->sleep_time = 0;
        af_vec3 point = af_vec3_add(b->p, ph_vector(b, p->grab_local)),
                velocity = af_vec3_add(b->v, af_vec3_cross(b->w, af_vec3_sub(point, b->p))),
                acceleration = ph_limit(
                    af_vec3_sub(af_vec3_scale(af_vec3_sub(p->grab_target, point), 36), af_vec3_scale(velocity, 12)),
                    80);
        ph_impulse(b, af_vec3_scale(acceleration, b->config.mass * dt), point);
        return;
    }
    p->held = 0;
}

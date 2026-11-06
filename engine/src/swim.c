/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/swim.h>
#include <math.h>
#include <stdlib.h>
static bool finite3(af_vec3 p) { return isfinite(p.x) && isfinite(p.y) && isfinite(p.z); }
static float clamp(float v, float a, float b) { return fmaxf(a, fminf(b, v)); }
af_swim_config af_swim_config_default(void) {
    return (af_swim_config){.speed = 3,
                            .vertical_speed = 3,
                            .drag = 4,
                            .neutral_fraction = .65f,
                            .entry_fraction = .35f,
                            .exit_fraction = .18f};
}
bool af_swim_config_valid(af_swim_config c) {
    float v[] = {c.speed,          c.vertical_speed, c.drag, c.neutral_fraction,
                 c.entry_fraction, c.exit_fraction};
    for (unsigned i = 0; i < 6; ++i)
        if (!isfinite(v[i]))
            return false;
    return c.speed > 0 && c.speed <= 30 && c.vertical_speed > 0 && c.vertical_speed <= 20 &&
           c.drag >= .1f && c.drag <= 30 && c.neutral_fraction >= .2f && c.neutral_fraction <= .95f &&
           c.exit_fraction >= 0 && c.exit_fraction < c.entry_fraction && c.entry_fraction < .9f;
}
static float immersion(af_water_sample s, af_character c, af_character_config config) {
    if (!s.inside)
        return 0;
    float extent = config.radius + config.half_height;
    return clamp((fminf(c.position.y + extent, s.height) - fmaxf(c.position.y - extent, s.bottom)) /
                     (2 * extent),
                 0, 1);
}
static bool slide(const af_scene *s, af_character *c, af_character_config config, float dt,
                  af_query_filter filter) {
    filter.include_triggers = false;
    c->grounded = false;
    c->ground = 0;
    for (unsigned pass = 0; pass < 8; ++pass) {
        af_collision_hit stack[64], *hits = stack, deepest = {0};
        size_t count = af_scene_overlap_capsule(s, c->position, config.radius, config.half_height,
                                                filter, stack, 64);
        if (count > 64) {
            hits = malloc(count * sizeof(*hits));
            if (!hits)
                return false;
            af_scene_overlap_capsule(s, c->position, config.radius, config.half_height, filter, hits,
                                     count);
        }
        for (size_t i = 0; i < count; ++i)
            if (hits[i].penetration > deepest.penetration)
                deepest = hits[i];
        if (hits != stack)
            free(hits);
        if (deepest.penetration < 1e-5f)
            break;
        c->position =
            af_vec3_add(c->position, af_vec3_scale(deepest.normal, deepest.penetration + config.skin));
        float v = af_vec3_dot(c->velocity, deepest.normal);
        if (v < 0)
            c->velocity = af_vec3_sub(c->velocity, af_vec3_scale(deepest.normal, v));
    }
    af_vec3 remaining = af_vec3_scale(c->velocity, dt);
    for (unsigned i = 0; i < 8 && af_vec3_length(remaining) > 1e-6f; ++i) {
        af_collision_hit hit;
        if (!af_scene_sweep_capsule(s, c->position, config.radius, config.half_height, remaining,
                                    config.skin, filter, &hit)) {
            c->position = af_vec3_add(c->position, remaining);
            break;
        }
        c->position = af_vec3_add(c->position, af_vec3_scale(remaining, hit.fraction));
        remaining = af_vec3_scale(remaining, 1 - hit.fraction);
        float into = af_vec3_dot(remaining, hit.normal);
        if (into < 0)
            remaining = af_vec3_sub(remaining, af_vec3_scale(hit.normal, into));
        into = af_vec3_dot(c->velocity, hit.normal);
        if (into < 0)
            c->velocity = af_vec3_sub(c->velocity, af_vec3_scale(hit.normal, into));
        if (hit.normal.y >= cosf(config.slope_limit)) {
            c->grounded = true;
            c->ground = hit.object;
        }
    }
    return finite3(c->position) && finite3(c->velocity);
}
bool af_character_swim_step(const af_physics *p, const af_scene *s, af_character *body,
                            af_character_config config, af_swim_state *state, af_swim_config swim,
                            af_swim_input input, float dt, af_query_filter filter) {
    if (!s || !body || !state || !af_character_config_valid(config) || !af_swim_config_valid(swim) ||
        !finite3(body->position) || !finite3(body->velocity) || !finite3(input.move) ||
        !isfinite(input.vertical) || fabsf(input.vertical) > 1 || !isfinite(dt) || dt <= 0 || dt > .1f ||
        !isfinite(state->exit_time) || state->exit_time < 0 || state->exit_time > 1)
        return false;
    af_character next = *body;
    af_swim_state status = *state;
    status.exit_time = fmaxf(0, status.exit_time - dt);
    af_water_sample water = af_physics_sample_water(p, s, next.position);
    status.immersion = immersion(water, next, config);
    bool breach = status.exit_time > 0 && next.velocity.y > 0;
    status.swimming = !breach && (status.swimming ? status.immersion > swim.exit_fraction
                                                  : status.immersion >= swim.entry_fraction);
    if (status.swimming) {
        af_vec3 direction = {input.move.x, 0, input.move.z};
        float length = af_vec3_length(direction);
        if (!isfinite(length))
            return false;
        if (length > 1)
            direction = af_vec3_scale(direction, 1 / length);
        float blend = -expm1f(-swim.drag * dt);
        next.velocity.x += (direction.x * swim.speed + water.velocity.x - next.velocity.x) * blend;
        next.velocity.z += (direction.z * swim.speed + water.velocity.z - next.velocity.z) * blend;
        if (fabsf(input.vertical) > .001f)
            next.velocity.y +=
                (input.vertical * swim.vertical_speed + water.velocity.y - next.velocity.y) * blend;
        else
            next.velocity.y =
                (next.velocity.y + config.gravity * (status.immersion / swim.neutral_fraction - 1) * dt +
                 water.velocity.y * swim.drag * dt) /
                (1 + swim.drag * dt);
        if (input.jump && status.immersion < .8f) {
            next.velocity.y = config.jump_speed;
            status.exit_time = .25f;
            status.swimming = false;
        }
        if (!slide(s, &next, config, dt, filter))
            return false;
    } else {
        af_character_config walking = config;
        if (status.immersion > .05f)
            walking.speed *= 1 - .4f * status.immersion;
        if (!af_character_step(s, &next, walking,
                               (af_character_input){.move = input.move, .jump = input.jump}, dt, filter))
            return false;
    }
    water = af_physics_sample_water(p, s, next.position);
    status.immersion = immersion(water, next, config);
    status.water = status.immersion > 0 ? water.owner : 0;
    float head = next.position.y + config.radius + config.half_height;
    status.head_underwater = water.inside && head < water.height && head > water.bottom;
    if (status.immersion <= swim.exit_fraction)
        status.swimming = false;
    *body = next;
    *state = status;
    return true;
}

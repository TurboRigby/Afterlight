/* SPDX-License-Identifier: MPL-2.0 */
#include "physics_internal.h"
void ph_move(af_physics *p, size_t at, float dt) {
    ph_body *b = &p->bodies[at];
    if (!b->config.continuous) {
        b->p = af_vec3_add(b->p, af_vec3_scale(b->v, dt));
        return;
    }
    float radius =
        b->shape.type == AF_COLLIDER_SPHERE
            ? b->shape.radius
            : fminf(b->shape.half_extents.x, fminf(b->shape.half_extents.y, b->shape.half_extents.z));
    float remaining = dt;
    for (unsigned pass = 0; pass < 4 && remaining > 1e-7f; ++pass) {
        af_vec3 move = af_vec3_scale(b->v, remaining);
        if (af_vec3_length(move) < 1e-7f)
            break;
        float nearest = 1;
        af_collision_hit hit = {0};
        ph_body *target = NULL;
        for (size_t i = 0; i < p->count; ++i) {
            ph_body *other = &p->bodies[i];
            if (i == at || other->inv_mass > 0 || !(b->shape.layer & other->shape.layer))
                continue;
            af_collision_hit candidate;
            if (af_collider_sweep_sphere(other->shape, b->p, radius, move, .001f, &candidate) &&
                candidate.fraction < nearest) {
                nearest = candidate.fraction;
                hit = candidate;
                target = other;
            }
        }
        b->p = af_vec3_add(b->p, af_vec3_scale(move, nearest));
        if (!target)
            break;
        float closing = af_vec3_dot(af_vec3_sub(b->v, target->v), hit.normal);
        if (closing < 0)
            b->v = af_vec3_sub(b->v, af_vec3_scale(hit.normal, (1 + fmaxf(b->config.restitution,
                                                                          target->config.restitution)) *
                                                                   closing));
        ++p->stats.ccd_hits;
        remaining *= 1 - nearest;
    }
}
void ph_sleep(af_physics *p, float dt) {
    for (size_t i = 0; i < p->count; ++i)
        p->bodies[i].supported = p->bodies[i].config.gravity_scale == 0;
    for (size_t i = 0; i < p->contact_count; ++i) {
        ph_contact *c = &p->contacts[i];
        if (c->view.normal.y > .5f)
            p->bodies[c->b].supported = true;
        if (c->view.normal.y < -.5f)
            p->bodies[c->a].supported = true;
    }
    for (size_t i = 0; i < p->count; ++i) {
        ph_body *b = &p->bodies[i];
        if (b->inv_mass == 0)
            continue;
        if (!b->supported || !b->config.allow_sleep) {
            b->sleeping = false;
            b->sleep_time = 0;
            continue;
        }
        if (b->sleeping)
            continue;
        if (af_vec3_length(b->v) < .06f && af_vec3_length(b->w) < .08f &&
            af_vec3_length(b->force) < 1e-6f && af_vec3_length(b->torque) < 1e-6f)
            b->sleep_time += dt;
        else
            b->sleep_time = 0;
        if (b->sleep_time >= .6f) {
            b->sleeping = true;
            b->v = b->w = (af_vec3){0};
        }
    }
}

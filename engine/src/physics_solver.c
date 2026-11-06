/* SPDX-License-Identifier: MPL-2.0 */
#include "physics_internal.h"
static af_vec3 add(af_vec3 a, af_vec3 b) {
    return af_vec3_add(a, b);
}
static af_vec3 sub(af_vec3 a, af_vec3 b) {
    return af_vec3_sub(a, b);
}
static af_vec3 mul(af_vec3 a, float b) {
    return af_vec3_scale(a, b);
}
static float dot(af_vec3 a, af_vec3 b) {
    return af_vec3_dot(a, b);
}
static float inverse_mass(ph_body *b) {
    return b->sleeping ? 0 : b->inv_mass;
}
static bool resting(ph_body *a, ph_body *b) {
    return (a->inv_mass == 0 || a->sleeping) && (b->inv_mass == 0 || b->sleeping);
}
static void wake_contact(ph_body *a, ph_body *b, float penetration) {
    bool moving_a = af_vec3_length(a->v) > .08f || af_vec3_length(a->w) > .08f,
         moving_b = af_vec3_length(b->v) > .08f || af_vec3_length(b->w) > .08f;
    if (a->sleeping && (moving_b || penetration > .012f)) {
        a->sleeping = false;
        a->sleep_time = 0;
    }
    if (b->sleeping && (moving_a || penetration > .012f)) {
        b->sleeping = false;
        b->sleep_time = 0;
    }
}
static af_vec3 velocity(ph_body *b, af_vec3 point) {
    return add(b->v, af_vec3_cross(b->w, sub(point, b->p)));
}
static float effective(ph_body *a, ph_body *b, af_vec3 point, af_vec3 n) {
    af_vec3 ra = sub(point, a->p), rb = sub(point, b->p);
    return inverse_mass(a) + inverse_mass(b) +
           dot(n, add(af_vec3_cross(ph_inverse_inertia(a, af_vec3_cross(ra, n)), ra),
                      af_vec3_cross(ph_inverse_inertia(b, af_vec3_cross(rb, n)), rb)));
}
static void impulse(ph_body *a, ph_body *b, af_vec3 point, af_vec3 j) {
    ph_impulse(a, mul(j, -1), point);
    ph_impulse(b, j, point);
}
void ph_solve(af_physics *p, float dt) {
    for (size_t i = 0; i < p->contact_count; ++i) {
        ph_contact *c = &p->contacts[i];
        ph_body *a = &p->bodies[c->a], *b = &p->bodies[c->b];
        wake_contact(a, b, c->view.penetration);
        if (resting(a, b))
            continue;
        af_vec3 n = c->view.normal;
        float speed = dot(sub(velocity(b, c->view.point), velocity(a, c->view.point)), n);
        c->bounce =
            c->view.penetration < 0
                ? c->view.penetration / dt
                : (speed < -1 ? -fmaxf(a->config.restitution, b->config.restitution) * speed : 0);
        for (size_t j = 0; j < p->cache_count; ++j) {
            ph_contact *old = &p->cache[j];
            if (old->view.a != c->view.a || old->view.b != c->view.b || dot(old->view.normal, n) < .95f)
                continue;
            af_vec3 da = sub(old->local_a, c->local_a), db = sub(old->local_b, c->local_b);
            if (dot(da, da) > .0025f || dot(db, db) > .0025f)
                continue;
            float ratio = p->last_dt > 0 ? fminf(2, fmaxf(.5f, dt / p->last_dt)) : 1;
            c->normal = old->normal * ratio;
            c->tangent = mul(sub(old->tangent, mul(n, dot(old->tangent, n))), ratio);
            break;
        }
        impulse(a, b, c->view.point, add(mul(n, c->normal), c->tangent));
    }
    for (unsigned iteration = 0; iteration < 20; ++iteration) {
        ph_joints_solve(p,dt,false);
        for (size_t i = 0; i < p->contact_count; ++i) {
            ph_contact *c = &p->contacts[i];
            ph_body *a = &p->bodies[c->a], *b = &p->bodies[c->b];
            if (resting(a, b))
                continue;
            af_vec3 point = c->view.point, n = c->view.normal;
            float mass = effective(a, b, point, n);
            if (mass < 1e-12f)
                continue;
            af_vec3 relative = sub(velocity(b, point), velocity(a, point));
            float value = fmaxf(0, c->normal + (c->bounce - dot(relative, n)) / mass),
                  delta = value - c->normal;
            c->normal = value;
            impulse(a, b, point, mul(n, delta));
            relative = sub(velocity(b, point), velocity(a, point));
            af_vec3 tangent = sub(relative, mul(n, dot(relative, n)));
            float length = af_vec3_length(tangent);
            if (length > 1e-7f) {
                af_vec3 direction = mul(tangent, 1 / length);
                float inverse = effective(a, b, point, direction);
                if (inverse > 1e-12f) {
                    af_vec3 next = ph_limit(sub(c->tangent, mul(direction, length / inverse)),
                                            sqrtf(a->config.friction * b->config.friction) * c->normal);
                    impulse(a, b, point, sub(next, c->tangent));
                    c->tangent = next;
                }
            }
        }
    }
    /* Split positional correction avoids injecting penetration energy into v. */
    for (unsigned iteration = 0; iteration < 8; ++iteration) {
        ph_joints_solve(p,dt,true);
        for (size_t i = 0; i < p->contact_count; ++i) {
            ph_contact *c = &p->contacts[i];
            ph_body *a = &p->bodies[c->a], *b = &p->bodies[c->b];
            float mass = inverse_mass(a) + inverse_mass(b);
            if (mass <= 0)
                continue;
            af_vec3 one = add(a->p, ph_vector(a, c->local_a)), two = add(b->p, ph_vector(b, c->local_b));
            float error = c->view.penetration - dot(sub(two, one), c->view.normal) - .002f;
            if (error <= 0)
                continue;
            af_vec3 shift = mul(c->view.normal, .4f * fminf(.2f, error) / mass);
            a->p = sub(a->p, mul(shift, inverse_mass(a)));
            b->p = add(b->p, mul(shift, inverse_mass(b)));
        }
    }
    for (size_t i = 0; i < p->count; ++i)
        if (p->bodies[i].inv_mass > 0)
            ph_shape(&p->bodies[i]);
    for (size_t i = 0; i < p->contact_count; ++i)
        p->contacts[i].view.impulse = p->contacts[i].normal;
    p->stats.iterations = 20;
    p->last_dt = dt;
}

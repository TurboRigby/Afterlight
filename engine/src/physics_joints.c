/* SPDX-License-Identifier: MPL-2.0 */

#include "physics_internal.h"
#include <string.h>
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
static size_t index_of(af_physics *p, af_object_id id) {
    for (size_t i = 0; i < p->count; ++i)
        if (p->bodies[i].id == id)
            return i;
    return SIZE_MAX;
}
bool ph_joints_sync(af_physics *p) {
    ph_joint old[AF_SCENE_MAX_JOINTS];
    size_t old_count = p->joint_count;
    memcpy(old, p->joints, old_count * sizeof(*old));
    p->joint_count = 0;
    p->world = (ph_body){.q = {0, 0, 0, 1}};
    for (unsigned k = 0; k < 3; ++k)
        p->world.axes[k]=p->world.shape.axes[k] = ph_axis(k);
    for (size_t i = 0; i < af_scene_joint_count(p->scene); ++i) {
        af_joint j = *af_scene_joint_at(p->scene, i);
        if (!j.enabled)
            continue;
        size_t a = index_of(p, j.a), b = index_of(p, j.b);
        if (!af_joint_valid(j) || a == SIZE_MAX || (j.b && b == SIZE_MAX) ||
            (p->bodies[a].inv_mass == 0 && (b == SIZE_MAX || p->bodies[b].inv_mass == 0)))
            return false;
        size_t n = p->joint_count++;
        bool changed = true;
        p->joints[n] = (ph_joint){0};
        for (size_t k = 0; k < old_count; ++k)
            if (old[k].config.id == j.id && !memcmp(&old[k].config, &j, sizeof(j))) {
                changed = false;
                if (!p->bodies[a].external && (b == SIZE_MAX || !p->bodies[b].external))
                    p->joints[n] = old[k];
                break;
            }
        p->joints[n].config = j;
        p->joints[n].a = a;
        p->joints[n].b = b;
        if (changed) {
            p->bodies[a].sleeping = false;
            p->bodies[a].sleep_time = 0;
            if (b != SIZE_MAX) {
                p->bodies[b].sleeping = false;
                p->bodies[b].sleep_time = 0;
            }
        }
    }
    for (size_t k = 0; k < old_count; ++k) {
        bool found = false;
        for (size_t i = 0; i < p->joint_count; ++i)
            found |= p->joints[i].config.id == old[k].config.id;
        if (found)
            continue;
        af_object_id ids[] = {old[k].config.a, old[k].config.b};
        for (unsigned n = 0; n < 2; ++n) {
            size_t at = index_of(p, ids[n]);
            if (at != SIZE_MAX) {
                p->bodies[at].sleeping = false;
                p->bodies[at].sleep_time = 0;
            }
        }
    }
    return true;
}
bool ph_collision_allowed(const af_physics *p, af_object_id a, af_object_id b) {
    for (size_t i = 0; i < p->joint_count; ++i) {
        const af_joint *j = &p->joints[i].config;
        if (!j->collide_connected && ((j->a == a && j->b == b) || (j->a == b && j->b == a)))
            return false;
    }
    return true;
}
static af_vec3 velocity(ph_body *b, af_vec3 r) {
    return add(b->v, af_vec3_cross(b->w, r));
}
static void turn(ph_body *b, af_vec3 angle) {
    float length = af_vec3_length(angle);
    if (length < 1e-9f)
        return;
    angle = mul(angle, fminf(length, .2f) / length);
    af_vec3 q = {b->q.x, b->q.y, b->q.z}, d = mul(add(mul(angle, b->q.w), af_vec3_cross(angle, q)), .5f);
    b->q = af_quaternion_normalize(
        (af_vec4){b->q.x + d.x, b->q.y + d.y, b->q.z + d.z, b->q.w - .5f * dot(angle, q)});
    ph_shape(b);
}
static void position(ph_body *b, af_vec3 j, af_vec3 r) {
    if (b->inv_mass == 0)
        return;
    b->p = add(b->p, mul(j, b->inv_mass));
    turn(b, ph_inverse_inertia(b, af_vec3_cross(r, j)));
    ph_shape(b);
}
static void linear(ph_body *a, ph_body *b, af_vec3 ra, af_vec3 rb, af_vec3 n, float error, float dt,
                   bool pos) {
    float mass = a->inv_mass + b->inv_mass +
                 dot(n, add(af_vec3_cross(ph_inverse_inertia(a, af_vec3_cross(ra, n)), ra),
                            af_vec3_cross(ph_inverse_inertia(b, af_vec3_cross(rb, n)), rb)));
    if (mass < 1e-10f)
        return;
    float speed = dot(sub(velocity(b, rb), velocity(a, ra)), n);
    float value =
        pos ? -.3f * fmaxf(-.2f, fminf(.2f, error)) / mass : -(speed + .1f * error / dt) / mass;
    af_vec3 j = mul(n, fmaxf(-10000, fminf(10000, value)));
    if (pos) {
        position(a, mul(j, -1), ra);
        position(b, j, rb);
    } else {
        ph_impulse(a, mul(j, -1), add(a->p, ra));
        ph_impulse(b, j, add(b->p, rb));
    }
}

static void angular(ph_body *a, ph_body *b, af_vec3 n, float error, float dt, bool pos) {
    float mass = dot(n, add(ph_inverse_inertia(a, n), ph_inverse_inertia(b, n)));
    if (mass < 1e-10f)
        return;
    float value = pos ? -.3f * fmaxf(-.2f, fminf(.2f, error)) / mass
                      : -(dot(sub(b->w, a->w), n) + .1f * error / dt) / mass;
    af_vec3 j = mul(n, fmaxf(-10000, fminf(10000, value)));
    if (pos) {
        turn(a, ph_inverse_inertia(a, mul(j, -1)));
        turn(b, ph_inverse_inertia(b, j));
    } else {
        a->w = ph_limit(sub(a->w, ph_inverse_inertia(a, j)), 100);
        b->w = ph_limit(add(b->w, ph_inverse_inertia(b, j)), 100);
    }
}
static void hinge(ph_body *a, ph_body *b, const af_joint *j, float dt, bool pos) {
    af_vec3 x = ph_vector(a, j->axis_a), y = ph_vector(b, j->axis_b), u = ph_vector(a, j->reference_a),
            v = af_vec3_cross(x, u), cross = af_vec3_cross(x, y);
    float length = af_vec3_length(cross), angle = atan2f(length, dot(x, y));
    af_vec3 error = length > 1e-6f ? mul(cross, angle / length) : mul(u, angle);
    angular(a, b, u, dot(error, u), dt, pos);
    angular(a, b, v, dot(error, v), dt, pos);
    if (j->limits_enabled) {
        x = ph_vector(a, j->axis_a);
        u = ph_vector(a, j->reference_a);
        af_vec3 reference = ph_vector(b, j->reference_b);
        reference = af_vec3_sub(reference, mul(x, dot(reference, x)));
        float len = af_vec3_length(reference);
        if (len < 1e-6f)
            return;
        reference = mul(reference, 1 / len);
        angle = atan2f(dot(x, af_vec3_cross(u, reference)), dot(u, reference));
        float target = fmaxf(j->lower_limit, fminf(j->upper_limit, angle));
        if (fabsf(angle - target) > 1e-5f)
            angular(a, b, x, angle - target, dt, pos);
    }
}

static void row_apply(ph_body *a, ph_body *b, ph_joint_row *r, float lambda, bool angular_row) {
    af_vec3 j = mul(r->n, lambda);
    if (angular_row) {
        a->w = ph_limit(sub(a->w, ph_inverse_inertia(a, j)), 100);
        b->w = ph_limit(add(b->w, ph_inverse_inertia(b, j)), 100);
    } else {
        ph_impulse(a, mul(j, -1), add(a->p, r->ra));
        ph_impulse(b, j, add(b->p, r->rb));
    }
}
static void row_prepare(ph_joint_row *r, ph_body *a, ph_body *b, af_vec3 ra, af_vec3 rb, af_vec3 n,
                        float error, float dt, bool angular_row, float low, float high, float ratio) {
    ph_joint_row old = *r;
    float mass = angular_row
                     ? dot(n, add(ph_inverse_inertia(a, n), ph_inverse_inertia(b, n)))
                     : a->inv_mass + b->inv_mass +
                           dot(n, add(af_vec3_cross(ph_inverse_inertia(a, af_vec3_cross(ra, n)), ra),
                                      af_vec3_cross(ph_inverse_inertia(b, af_vec3_cross(rb, n)), rb)));
    *r = (ph_joint_row){.n = n,
                        .ra = ra,
                        .rb = rb,
                        .inverse = mass > 1e-10f ? 1 / mass : 0,
                        .bias = .1f * error / dt,
                        .lo = low,
                        .hi = high};
    float alignment = dot(old.n, n);
    if (alignment > .9f)
        r->lambda = fmaxf(low, fminf(high, old.lambda * ratio * alignment));
}
static float hinge_angle(ph_body *a, ph_body *b, const af_joint *j) {
    af_vec3 axis = ph_vector(a, j->axis_a), u = ph_vector(a, j->reference_a),
            v = ph_vector(b, j->reference_b);
    v = sub(v, mul(axis, dot(axis, v)));
    v = af_vec3_normalize(v);
    return atan2f(dot(axis, af_vec3_cross(u, v)), dot(u, v));
}
void ph_joints_warm_start(af_physics *p, float dt) {
    float ratio = p->last_dt > 0 ? fminf(2, fmaxf(.5f, dt / p->last_dt)) : 1;
    for (size_t i = 0; i < p->joint_count; ++i) {
        ph_joint *state = &p->joints[i];
        const af_joint *j = &state->config;
        ph_body *a = &p->bodies[state->a], *b = state->b == SIZE_MAX ? &p->world : &p->bodies[state->b];
        if ((a->inv_mass == 0 || a->sleeping) && (b->inv_mass == 0 || b->sleeping))
            continue;
        af_vec3 ra = ph_vector(a, j->anchor_a), rb = ph_vector(b, j->anchor_b),
                delta = sub(add(b->p, rb), add(a->p, ra));
        float length = af_vec3_length(delta);
        bool distance = j->type == AF_JOINT_DISTANCE && j->distance > 1e-6f;
        for (unsigned k = 0; k < 3; ++k) {
            if (distance && k > 0) {
                state->rows[k] = (ph_joint_row){0};
                continue;
            }
            af_vec3 n =
                distance ? (length > 1e-6f ? mul(delta, 1 / length) : (af_vec3){0, 1, 0}) : ph_axis(k);
            row_prepare(&state->rows[k], a, b, ra, rb, n,
                        distance ? length - j->distance : ph_get(delta, k), dt, false, -10000, 10000,
                        ratio);
        }
        if (j->type == AF_JOINT_HINGE) {
            af_vec3 x = ph_vector(a, j->axis_a), y = ph_vector(b, j->axis_b),
                    u = ph_vector(a, j->reference_a), v = af_vec3_cross(x, u),
                    cross = af_vec3_cross(x, y);
            float n = af_vec3_length(cross), angle = atan2f(n, dot(x, y));
            af_vec3 error = n > 1e-6f ? mul(cross, angle / n) : mul(u, angle);
            row_prepare(&state->rows[3], a, b, ra, rb, u, dot(error, u), dt, true, -10000, 10000, ratio);
            row_prepare(&state->rows[4], a, b, ra, rb, v, dot(error, v), dt, true, -10000, 10000, ratio);
            angle = hinge_angle(a, b, j);
            if (j->limits_enabled &&
                (angle < j->lower_limit + .001f || angle > j->upper_limit - .001f)) {
                bool lock = j->upper_limit - j->lower_limit < 1e-6f,
                     lower = angle < j->lower_limit + .001f;
                af_vec3 axis = lower || lock ? x : mul(x, -1);
                float c = lock    ? angle - j->lower_limit
                          : lower ? angle - j->lower_limit
                                  : j->upper_limit - angle;
                row_prepare(&state->rows[5], a, b, ra, rb, axis, c, dt, true, lock ? -10000 : 0, 10000,
                            ratio);
            } else
                state->rows[5] = (ph_joint_row){0};
        } else
            for (unsigned k = 3; k < 6; ++k)
                state->rows[k] = (ph_joint_row){0};

        if (j->type == AF_JOINT_HINGE && j->motor_enabled && j->max_motor_torque > 0) {
            float budget = j->max_motor_torque * dt;
            row_prepare(&state->rows[6], a, b, ra, rb, ph_vector(a, j->axis_a), 0, dt, true, -budget,
                        budget, ratio);
            state->rows[6].bias = -j->motor_speed;
        } else
            state->rows[6] = (ph_joint_row){0};
        for (unsigned k = 0; k < 7; ++k)
            if (state->rows[k].inverse > 0)
                row_apply(a, b, &state->rows[k], state->rows[k].lambda, k >= 3);
    }
}
void ph_joints_solve(af_physics *p, float dt, bool pos) {
    for (size_t i = 0; i < p->joint_count; ++i) {
        const af_joint *j = &p->joints[i].config;
        ph_body *a = &p->bodies[p->joints[i].a],
                *b = p->joints[i].b == SIZE_MAX ? &p->world : &p->bodies[p->joints[i].b];
        if ((a->inv_mass == 0 || a->sleeping) && (b->inv_mass == 0 || b->sleeping))
            continue;

        if (!pos) {
            ph_joint *state = &p->joints[i];
            for (unsigned k = 0; k < 7; ++k) {
                ph_joint_row *r = &state->rows[k];
                if (r->inverse <= 0)
                    continue;
                float speed = k >= 3 ? dot(sub(b->w, a->w), r->n)
                                     : dot(sub(velocity(b, r->rb), velocity(a, r->ra)), r->n);
                float next = fmaxf(r->lo, fminf(r->hi, r->lambda - (speed + r->bias) * r->inverse));
                row_apply(a, b, r, next - r->lambda, k >= 3);
                r->lambda = next;
            }
            continue;
        }
        af_vec3 ra = ph_vector(a, j->anchor_a), rb = ph_vector(b, j->anchor_b),
                delta = sub(add(b->p, rb), add(a->p, ra));
        float length = af_vec3_length(delta);
        if (j->type == AF_JOINT_DISTANCE && j->distance > 1e-6f) {
            af_vec3 n = length > 1e-6f ? mul(delta, 1 / length) : (af_vec3){0, 1, 0};
            linear(a, b, ra, rb, n, length - j->distance, dt, pos);
        } else
            for (unsigned k = 0; k < 3; ++k)
                linear(a, b, ra, rb, ph_axis(k), ph_get(delta, k), dt, pos);
        if (j->type == AF_JOINT_HINGE)
            hinge(a, b, j, dt, pos);
    }
}

af_physics_joint_state af_physics_get_joint_state(const af_physics *p, af_joint_id id) {
    for (size_t i = 0; p && i < p->joint_count; ++i) {
        const ph_joint *state = &p->joints[i];
        if (state->config.id != id)
            continue;
        const af_joint *j = &state->config;
        const ph_body *a = &p->bodies[state->a],
                      *b = state->b == SIZE_MAX ? &p->world : &p->bodies[state->b];
        af_vec3 one = add(a->p, ph_vector(a, j->anchor_a)), two = add(b->p, ph_vector(b, j->anchor_b)),
                axis = ph_vector(a, j->axis_a), u = ph_vector(a, j->reference_a),
                v = ph_vector(b, j->reference_b);
        v = af_vec3_normalize(sub(v, mul(axis, dot(axis, v))));
        float angle = atan2f(dot(axis, af_vec3_cross(u, v)), dot(u, v)),
              alignment = acosf(fmaxf(-1, fminf(1, dot(axis, ph_vector(b, j->axis_b))))),
              limit = j->limits_enabled
                          ? fabsf(angle - fmaxf(j->lower_limit, fminf(j->upper_limit, angle)))
                          : 0;
        af_vec3 linear_j = {0}, angular_j = {0};
        for (unsigned k = 0; k < 6; ++k) {
            af_vec3 value = mul(state->rows[k].n, state->rows[k].lambda);
            if (k < 3)
                linear_j = add(linear_j, value);
            else
                angular_j = add(angular_j, value);
        }
        float inverse = p->last_dt > 0 ? 1 / p->last_dt : 0;
        return (af_physics_joint_state){
            .available = true,
            .sleeping = (a->inv_mass == 0 || a->sleeping) && (b->inv_mass == 0 || b->sleeping),
            .id = id,
            .anchor_a = one,
            .anchor_b = two,
            .linear_error =
                fabsf(af_vec3_length(sub(two, one)) - (j->type == AF_JOINT_DISTANCE ? j->distance : 0)),
            .angular_error = j->type == AF_JOINT_HINGE ? fmaxf(alignment, limit) : 0,
            .angle = angle,
            .speed = dot(sub(b->w, a->w), axis),
            .force = af_vec3_length(linear_j) * inverse,
            .torque = af_vec3_length(angular_j) * inverse,
            .motor_torque = state->rows[6].lambda * inverse};
    }
    return (af_physics_joint_state){0};
}
void ph_joints_report(af_physics *p) {
    for (size_t i = 0; i < p->joint_count; ++i) {
        af_physics_joint_state state = af_physics_get_joint_state(p, p->joints[i].config.id);
        p->stats.max_joint_error = fmaxf(p->stats.max_joint_error, state.linear_error);
        p->stats.max_joint_force = fmaxf(p->stats.max_joint_force, state.force);
        p->stats.max_joint_torque = fmaxf(p->stats.max_joint_torque, state.torque);
    }
}

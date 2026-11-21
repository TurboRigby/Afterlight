/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "core/jobs.h"
#include "physics_internal.h"
#include <string.h>

enum {
    PH_VELOCITY_ITERATIONS = 20u,
    PH_POSITION_ITERATIONS = 8u,
    PH_PARALLEL_CONSTRAINT_THRESHOLD = 32u
};

static float active_inverse_mass(const ph_body *body) {
    return body->sleeping ? 0.0f : body->inv_mass;
}

static bool pair_is_resting(const ph_body *body_a, const ph_body *body_b) {
    return (body_a->inv_mass == 0.0f || body_a->sleeping) && (body_b->inv_mass == 0.0f || body_b->sleeping);
}

static af_vec3 point_velocity(const ph_body *body, af_vec3 world_point) {
    return af_vec3_add(body->v, af_vec3_cross(body->w, af_vec3_sub(world_point, body->p)));
}

static float effective_inverse_mass(const ph_body *body_a, const ph_body *body_b, af_vec3 world_point,
                                    af_vec3 direction) {
    af_vec3 arm_a = af_vec3_sub(world_point, body_a->p);
    af_vec3 arm_b = af_vec3_sub(world_point, body_b->p);
    af_vec3 ang_a = af_vec3_cross(ph_inverse_inertia(body_a, af_vec3_cross(arm_a, direction)), arm_a);
    af_vec3 ang_b = af_vec3_cross(ph_inverse_inertia(body_b, af_vec3_cross(arm_b, direction)), arm_b);
    return active_inverse_mass(body_a) + active_inverse_mass(body_b) +
           af_vec3_dot(direction, af_vec3_add(ang_a, ang_b));
}

static void apply_pair_impulse(ph_body *body_a, ph_body *body_b, af_vec3 world_point, af_vec3 impulse) {
    ph_impulse(body_a, af_vec3_scale(impulse, -1.0f), world_point);
    ph_impulse(body_b, impulse, world_point);
}

static unsigned contact_cache_bucket(af_object_id id_a, af_object_id id_b, size_t mask) {
    uint64_t hash = (id_a * UINT64_C(0x9e3779b97f4a7c15)) ^ (id_b * UINT64_C(0xbf58476d1ce4e5b9));
    hash ^= hash >> 32;
    return (unsigned)(hash & mask);
}

static size_t find_island_root_readonly(const af_physics *world, size_t index) {
    while (world->parents[index] != index)
        index = world->parents[index];
    return index;
}

static void warm_start_contact(af_physics *world, size_t contact_index, float dt, float dt_ratio, bool have_cache,
                               size_t cache_mask, size_t *probes_out) {
    ph_contact *contact = &world->contacts[contact_index];
    ph_body *body_a = &world->bodies[contact->a];
    ph_body *body_b = &world->bodies[contact->b];
    if (pair_is_resting(body_a, body_b))
        return;

    af_vec3 normal = contact->view.normal;
    float approach_speed =
        af_vec3_dot(af_vec3_sub(point_velocity(body_b, contact->view.point), point_velocity(body_a, contact->view.point)),
                    normal);
    contact->bounce = contact->view.penetration < 0.0f
                          ? contact->view.penetration / dt
                          : (approach_speed < -1.0f
                                 ? -fmaxf(body_a->config.restitution, body_b->config.restitution) * approach_speed
                                 : 0.0f);

    if (have_cache) {
        unsigned key = contact_cache_bucket(contact->view.a, contact->view.b, cache_mask);
        for (uint32_t slot = world->cache_heads[key]; slot != 0u; slot = world->cache_next[slot - 1u]) {
            const ph_contact *cached = &world->cache[slot - 1u];
            ++(*probes_out);
            if (cached->view.a != contact->view.a || cached->view.b != contact->view.b ||
                cached->view.part_a != contact->view.part_a || cached->view.part_b != contact->view.part_b ||
                af_vec3_dot(cached->view.normal, normal) < 0.95f)
                continue;
            af_vec3 delta_a = af_vec3_sub(cached->local_a, contact->local_a);
            af_vec3 delta_b = af_vec3_sub(cached->local_b, contact->local_b);
            if (af_vec3_dot(delta_a, delta_a) > 0.0025f || af_vec3_dot(delta_b, delta_b) > 0.0025f)
                continue;
            contact->normal = cached->normal * dt_ratio;
            af_vec3 projected_tangent =
                af_vec3_sub(cached->tangent, af_vec3_scale(normal, af_vec3_dot(cached->tangent, normal)));
            contact->tangent = af_vec3_scale(projected_tangent, dt_ratio);
            break;
        }
    }

    af_vec3 warm_impulse = af_vec3_add(af_vec3_scale(normal, contact->normal), contact->tangent);
    apply_pair_impulse(body_a, body_b, contact->view.point, warm_impulse);
}

static void solve_contact_velocity(af_physics *world, size_t contact_index) {
    ph_contact *contact = &world->contacts[contact_index];
    ph_body *body_a = &world->bodies[contact->a];
    ph_body *body_b = &world->bodies[contact->b];
    if (pair_is_resting(body_a, body_b))
        return;

    af_vec3 point = contact->view.point;
    af_vec3 normal = contact->view.normal;
    float inv_mass = effective_inverse_mass(body_a, body_b, point, normal);
    if (inv_mass < 1e-12f)
        return;

    af_vec3 rel_vel = af_vec3_sub(point_velocity(body_b, point), point_velocity(body_a, point));
    float next_normal = fmaxf(0.0f, contact->normal + (contact->bounce - af_vec3_dot(rel_vel, normal)) / inv_mass);
    float delta_normal = next_normal - contact->normal;
    contact->normal = next_normal;
    apply_pair_impulse(body_a, body_b, point, af_vec3_scale(normal, delta_normal));

    rel_vel = af_vec3_sub(point_velocity(body_b, point), point_velocity(body_a, point));
    af_vec3 tangent_vel = af_vec3_sub(rel_vel, af_vec3_scale(normal, af_vec3_dot(rel_vel, normal)));
    float tangent_speed = af_vec3_length(tangent_vel);
    if (tangent_speed > 1e-7f) {
        af_vec3 tangent_dir = af_vec3_scale(tangent_vel, 1.0f / tangent_speed);
        float tangent_inv_mass = effective_inverse_mass(body_a, body_b, point, tangent_dir);
        if (tangent_inv_mass > 1e-12f) {
            float friction_limit = sqrtf(body_a->config.friction * body_b->config.friction) * contact->normal;
            af_vec3 candidate_tangent =
                af_vec3_sub(contact->tangent, af_vec3_scale(tangent_dir, tangent_speed / tangent_inv_mass));
            af_vec3 next_tangent = ph_limit(candidate_tangent, friction_limit);
            apply_pair_impulse(body_a, body_b, point, af_vec3_sub(next_tangent, contact->tangent));
            contact->tangent = next_tangent;
        }
    }
}

static void solve_contact_position(af_physics *world, size_t contact_index) {
    ph_contact *contact = &world->contacts[contact_index];
    ph_body *body_a = &world->bodies[contact->a];
    ph_body *body_b = &world->bodies[contact->b];
    float inv_mass_a = active_inverse_mass(body_a);
    float inv_mass_b = active_inverse_mass(body_b);
    float total_inv_mass = inv_mass_a + inv_mass_b;
    if (total_inv_mass <= 0.0f)
        return;

    af_vec3 anchor_a = af_vec3_add(body_a->p, ph_vector(body_a, contact->local_a));
    af_vec3 anchor_b = af_vec3_add(body_b->p, ph_vector(body_b, contact->local_b));
    float error = contact->view.penetration - af_vec3_dot(af_vec3_sub(anchor_b, anchor_a), contact->view.normal) - 0.002f;
    if (error <= 0.0f)
        return;

    af_vec3 shift = af_vec3_scale(contact->view.normal, 0.4f * fminf(0.2f, error) / total_inv_mass);
    if (inv_mass_a > 0.0f)
        body_a->p = af_vec3_sub(body_a->p, af_vec3_scale(shift, inv_mass_a));
    if (inv_mass_b > 0.0f)
        body_b->p = af_vec3_add(body_b->p, af_vec3_scale(shift, inv_mass_b));
}

typedef struct island_solve_context {
    af_physics *world;
    float dt;
    float dt_ratio;
    bool have_cache;
    size_t cache_mask;
} island_solve_context;

static void solve_island_range(size_t start, size_t end, void *user_data) {
    const island_solve_context *ctx = user_data;
    af_physics *world = ctx->world;

    for (size_t island = start; island < end; ++island) {
        size_t c_begin = world->island_contact_offset[island];
        size_t c_end = c_begin + world->island_contact_count[island];
        size_t j_begin = world->island_joint_offset[island];
        size_t j_end = j_begin + world->island_joint_count[island];
        size_t probes = 0;

        for (size_t ji = j_begin; ji < j_end; ++ji)
            ph_joint_warm_start_one(world, world->island_joints[ji], ctx->dt, ctx->dt_ratio);
        for (size_t ci = c_begin; ci < c_end; ++ci)
            warm_start_contact(world, world->island_contacts[ci], ctx->dt, ctx->dt_ratio, ctx->have_cache,
                               ctx->cache_mask, &probes);

        for (unsigned iter = 0; iter < PH_VELOCITY_ITERATIONS; ++iter) {
            for (size_t ji = j_begin; ji < j_end; ++ji)
                ph_joint_solve_one(world, world->island_joints[ji], ctx->dt, false);
            for (size_t ci = c_begin; ci < c_end; ++ci)
                solve_contact_velocity(world, world->island_contacts[ci]);
        }

        /* Split positional correction avoids injecting penetration energy into velocity. */
        for (unsigned iter = 0; iter < PH_POSITION_ITERATIONS; ++iter) {
            for (size_t ji = j_begin; ji < j_end; ++ji)
                ph_joint_solve_one(world, world->island_joints[ji], ctx->dt, true);
            for (size_t ci = c_begin; ci < c_end; ++ci)
                solve_contact_position(world, world->island_contacts[ci]);
        }

        world->island_probes[island] = probes;
    }
}

void ph_solve(af_physics *world, float dt) {
    ph_islands_build(world);

    bool have_cache = world->cache_count > 0 && ph_reserve_cache_heads(world, world->cache_count);
    size_t cache_mask = have_cache ? world->cache_head_capacity - 1u : 0u;
    if (have_cache) {
        memset(world->cache_heads, 0, world->cache_head_capacity * sizeof(*world->cache_heads));
        for (size_t i = world->cache_count; i > 0; --i) {
            unsigned key = contact_cache_bucket(world->cache[i - 1u].view.a, world->cache[i - 1u].view.b, cache_mask);
            world->cache_next[i - 1u] = world->cache_heads[key];
            world->cache_heads[key] = (uint32_t)i;
        }
    }

    float dt_ratio = world->last_dt > 0.0f ? fminf(2.0f, fmaxf(0.5f, dt / world->last_dt)) : 1.0f;
    size_t total_constraints = world->contact_count + world->joint_count;

    if (world->stats.awake_islands <= 1u || total_constraints < PH_PARALLEL_CONSTRAINT_THRESHOLD) {
        ph_joints_warm_start(world, dt);
        for (size_t i = 0; i < world->contact_count; ++i)
            warm_start_contact(world, i, dt, dt_ratio, have_cache, cache_mask, &world->stats.cache_probes);

        for (unsigned iter = 0; iter < PH_VELOCITY_ITERATIONS; ++iter) {
            ph_joints_solve(world, dt, false);
            for (size_t i = 0; i < world->contact_count; ++i)
                solve_contact_velocity(world, i);
        }

        /* Split positional correction avoids injecting penetration energy into velocity. */
        for (unsigned iter = 0; iter < PH_POSITION_ITERATIONS; ++iter) {
            ph_joints_solve(world, dt, true);
            for (size_t i = 0; i < world->contact_count; ++i)
                solve_contact_position(world, i);
        }
    } else {
        /* Partition awake islands into disjoint constraint slices and solve deterministically in parallel. */
        size_t awake_count = 0;
        for (size_t i = 0; i < world->count; ++i) {
            if (world->island_sizes[i] > 0 && world->island_wake[i]) {
                world->island_ids[i] = awake_count;
                world->island_contact_count[awake_count] = 0;
                world->island_joint_count[awake_count] = 0;
                world->island_probes[awake_count] = 0;
                ++awake_count;
            } else {
                world->island_ids[i] = SIZE_MAX;
            }
        }

        for (size_t i = 0; i < world->contact_count; ++i) {
            const ph_contact *contact = &world->contacts[i];
            const ph_body *body_a = &world->bodies[contact->a];
            const ph_body *body_b = &world->bodies[contact->b];
            if (pair_is_resting(body_a, body_b))
                continue;
            size_t dyn = body_a->inv_mass > 0.0f ? contact->a : contact->b;
            size_t island = world->island_ids[find_island_root_readonly(world, dyn)];
            if (island < awake_count)
                ++world->island_contact_count[island];
        }
        for (size_t i = 0; i < world->joint_count; ++i) {
            const ph_joint *joint = &world->joints[i];
            const ph_body *body_a = &world->bodies[joint->a];
            const ph_body *body_b = joint->b == SIZE_MAX ? &world->world : &world->bodies[joint->b];
            if (pair_is_resting(body_a, body_b))
                continue;
            size_t dyn = body_a->inv_mass > 0.0f ? joint->a : joint->b;
            size_t island = world->island_ids[find_island_root_readonly(world, dyn)];
            if (island < awake_count)
                ++world->island_joint_count[island];
        }

        size_t c_offset = 0;
        size_t j_offset = 0;
        for (size_t island = 0; island < awake_count; ++island) {
            world->island_contact_offset[island] = c_offset;
            c_offset += world->island_contact_count[island];
            world->island_contact_count[island] = 0;

            world->island_joint_offset[island] = j_offset;
            j_offset += world->island_joint_count[island];
            world->island_joint_count[island] = 0;
        }

        for (size_t i = 0; i < world->contact_count; ++i) {
            const ph_contact *contact = &world->contacts[i];
            const ph_body *body_a = &world->bodies[contact->a];
            const ph_body *body_b = &world->bodies[contact->b];
            if (pair_is_resting(body_a, body_b))
                continue;
            size_t dyn = body_a->inv_mass > 0.0f ? contact->a : contact->b;
            size_t island = world->island_ids[find_island_root_readonly(world, dyn)];
            if (island < awake_count) {
                size_t dst = world->island_contact_offset[island] + world->island_contact_count[island]++;
                world->island_contacts[dst] = i;
            }
        }
        for (size_t i = 0; i < world->joint_count; ++i) {
            const ph_joint *joint = &world->joints[i];
            const ph_body *body_a = &world->bodies[joint->a];
            const ph_body *body_b = joint->b == SIZE_MAX ? &world->world : &world->bodies[joint->b];
            if (pair_is_resting(body_a, body_b))
                continue;
            size_t dyn = body_a->inv_mass > 0.0f ? joint->a : joint->b;
            size_t island = world->island_ids[find_island_root_readonly(world, dyn)];
            if (island < awake_count) {
                size_t dst = world->island_joint_offset[island] + world->island_joint_count[island]++;
                world->island_joints[dst] = i;
            }
        }

        island_solve_context ctx = {
            .world = world, .dt = dt, .dt_ratio = dt_ratio, .have_cache = have_cache, .cache_mask = cache_mask};
        af_jobs_parallel_for(af_jobs_global(), awake_count, 1u, solve_island_range, &ctx);

        for (size_t island = 0; island < awake_count; ++island)
            world->stats.cache_probes += world->island_probes[island];
    }

    for (size_t i = 0; i < world->count; ++i) {
        if (world->bodies[i].inv_mass > 0.0f)
            ph_shape(&world->bodies[i]);
    }
    for (size_t i = 0; i < world->contact_count; ++i)
        world->contacts[i].view.impulse = world->contacts[i].normal;
    world->stats.iterations = PH_VELOCITY_ITERATIONS;
    world->last_dt = dt;
}

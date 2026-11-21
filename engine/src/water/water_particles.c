/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/* Bounded CPU position-based fluid. Equations follow Macklin/Mueller's PBF
 * density projection with non-negative pressure, Poly6 and Spiky kernels.
 * https://matthias-research.github.io/pages/publications/pbf_sig_preprint.pdf
 */
#include "core/jobs.h"
#include "physics/collision_internal.h"
#include "physics/physics_internal.h"
#include "water_particles_internal.h"

#include <stdlib.h>
#include <string.h>

static af_vec3 rotate_yaw(af_transform transform, af_vec3 local_point) {
    float cos_y = cosf(transform.rotation.y);
    float sin_y = sinf(transform.rotation.y);
    return (af_vec3){cos_y * local_point.x + sin_y * local_point.z, local_point.y,
                     -sin_y * local_point.x + cos_y * local_point.z};
}

static uint32_t hash_cell(int cell_x, int cell_y, int cell_z) {
    return ((uint32_t)cell_x * 73856093u ^ (uint32_t)cell_y * 19349663u ^ (uint32_t)cell_z * 83492791u) &
           (WA_HASH_SIZE - 1u);
}

static void rebuild_spatial_grid(wa_particles *fluid) {
    for (unsigned bucket = 0; bucket < WA_HASH_SIZE; ++bucket)
        fluid->heads[bucket] = UINT32_MAX;
    float cell_size = 2.0f * fluid->spacing;
    for (size_t i = 0; i < fluid->count; ++i) {
        wa_particle *particle = &fluid->values[i];
        particle->cell[0] = (int)floorf(particle->q.x / cell_size);
        particle->cell[1] = (int)floorf(particle->q.y / cell_size);
        particle->cell[2] = (int)floorf(particle->q.z / cell_size);
        uint32_t bucket = hash_cell(particle->cell[0], particle->cell[1], particle->cell[2]);
        particle->next = fluid->heads[bucket];
        fluid->heads[bucket] = (uint32_t)i;
    }
}

static bool matches_cell(const wa_particle *particle, int cell_x, int cell_y, int cell_z) {
    return particle->cell[0] == cell_x && particle->cell[1] == cell_y && particle->cell[2] == cell_z;
}

static float poly6_kernel(float distance_sq, float kernel_radius) {
    float diff = kernel_radius * kernel_radius - distance_sq;
    if (diff <= 0.0f)
        return 0.0f;
    float h3 = kernel_radius * kernel_radius * kernel_radius;
    return 315.0f / (64.0f * AF_PI * h3 * h3 * h3) * diff * diff * diff;
}

static af_vec3 spiky_gradient(af_vec3 offset, float kernel_radius, size_t index_a, size_t index_b) {
    float dist = af_vec3_length(offset);
    if (dist >= kernel_radius)
        return (af_vec3){0};
    if (dist < 1e-6f) {
        offset = ph_axis((unsigned)((index_a + index_b) % 3u));
        if (index_a < index_b)
            offset = af_vec3_scale(offset, -1.0f);
        dist = 1e-4f * kernel_radius;
        offset = af_vec3_scale(offset, dist);
    }
    float h3 = kernel_radius * kernel_radius * kernel_radius;
    return af_vec3_scale(offset, -45.0f / (AF_PI * h3 * h3) * (kernel_radius - dist) * (kernel_radius - dist) / dist);
}

static void emit_particles(wa_slot *slot, size_t count, bool initial_fill) {
    wa_particles *fluid = slot->particles;
    af_water config = slot->config;
    unsigned grid_x = (unsigned)fmaxf(1.0f, floorf(2.0f * config.half_size.x / fluid->spacing));
    unsigned grid_z = (unsigned)fmaxf(1.0f, floorf(2.0f * config.half_size.y / fluid->spacing));
    if (grid_x > 64u)
        grid_x = 64u;
    if (grid_z > 64u)
        grid_z = 64u;

    for (size_t n = 0; n < count && fluid->count < fluid->capacity; ++n) {
        uint64_t serial = fluid->births++;
        unsigned ix = (unsigned)(serial % grid_x);
        unsigned iz = (unsigned)((serial / grid_x) % grid_z);
        unsigned layer = (unsigned)(serial / (grid_x * grid_z));
        af_vec3 local_pos = {((float)ix + 0.5f - (float)grid_x * 0.5f) * fluid->spacing,
                             initial_fill ? -(float)layer * fluid->spacing : 0.0f,
                             ((float)iz + 0.5f - (float)grid_z * 0.5f) * fluid->spacing};
        af_vec3 world_pos = af_vec3_add(slot->transform.position, rotate_yaw(slot->transform, local_pos));
        if (!initial_fill) {
            bool blocked = false;
            for (size_t i = 0; i < fluid->count; ++i) {
                if (af_vec3_length(af_vec3_sub(fluid->values[i].p, world_pos)) < fluid->spacing * 0.8f) {
                    blocked = true;
                    break;
                }
            }
            if (blocked) {
                --fluid->births;
                break;
            }
        }
        wa_particle *particle = &fluid->values[fluid->count++];
        *particle = (wa_particle){.p = world_pos,
                                  .previous = world_pos,
                                  .q = world_pos,
                                  .v = rotate_yaw(slot->transform, config.emission_velocity),
                                  .hit_body = SIZE_MAX};
        fluid->emitted += fluid->volume;
    }
}

void wa_particles_free(wa_particles *particles) {
    free(particles);
}

bool wa_particles_sync(wa_slot *slot) {
    size_t capacity = 256u << (2u * (unsigned)slot->config.quality);
    if (slot->particles &&
        (slot->particles->capacity != capacity || slot->particles->spacing != slot->config.particle_spacing)) {
        wa_particles_free(slot->particles);
        slot->particles = NULL;
    }
    if (!slot->particles) {
        slot->particles = calloc(1, sizeof(*slot->particles));
        if (!slot->particles)
            return false;
        wa_particles *fluid = slot->particles;
        fluid->capacity = capacity;
        fluid->spacing = slot->config.particle_spacing;
        fluid->volume = fluid->spacing * fluid->spacing * fluid->spacing;
        fluid->initial_volume = slot->config.initial_volume;
        size_t initial_count =
            (size_t)fminf((float)capacity, floorf(slot->config.initial_volume / fluid->volume + 0.0001f));
        emit_particles(slot, initial_count, true);
    }
    return true;
}

size_t af_physics_water_particles(const af_physics *physics, af_object_id owner_id, af_water_particle *out,
                                  size_t capacity) {
    const af_water_runtime *runtime = ph_water_runtime(physics);
    for (size_t k = 0; runtime && k < runtime->count; ++k) {
        if (runtime->slots[k].config.owner == owner_id) {
            const wa_particles *fluid = runtime->slots[k].particles;
            if (!fluid)
                return 0;
            for (size_t i = 0; out && i < fluid->count && i < capacity; ++i)
                out[i] = (af_water_particle){fluid->values[i].p, fluid->values[i].v, fluid->volume};
            return fluid->count;
        }
    }
    return 0;
}

static bool near_collider_aabb(af_world_collider collider, af_vec3 seg_start, af_vec3 seg_end, float radius) {
    af_vec3 half = {collider.radius, collider.radius, collider.radius};
    if (collider.type == AF_COLLIDER_MESH)
        return true;
    if (collider.type == AF_COLLIDER_BOX) {
        half = (af_vec3){0};
        for (unsigned axis = 0; axis < 3u; ++axis) {
            float extent = ph_get(collider.half_extents, axis);
            half.x += fabsf(collider.axes[axis].x) * extent;
            half.y += fabsf(collider.axes[axis].y) * extent;
            half.z += fabsf(collider.axes[axis].z) * extent;
        }
    } else if (collider.type == AF_COLLIDER_CAPSULE) {
        half.x += fabsf(collider.a.x - collider.b.x) * 0.5f;
        half.y += fabsf(collider.a.y - collider.b.y) * 0.5f;
        half.z += fabsf(collider.a.z - collider.b.z) * 0.5f;
    }
    return fminf(seg_start.x, seg_end.x) <= collider.center.x + half.x + radius &&
           fmaxf(seg_start.x, seg_end.x) >= collider.center.x - half.x - radius &&
           fminf(seg_start.y, seg_end.y) <= collider.center.y + half.y + radius &&
           fmaxf(seg_start.y, seg_end.y) >= collider.center.y - half.y - radius &&
           fminf(seg_start.z, seg_end.z) <= collider.center.z + half.z + radius &&
           fmaxf(seg_start.z, seg_end.z) >= collider.center.z - half.z - radius;
}

static af_world_collider interpolated_body_shape(const ph_body *body, size_t part, float fraction) {
    if (!body->water_motion)
        return ph_part_world(body, part);
    ph_body interpolated = *body;
    interpolated.p = af_vec3_add(body->water_previous_p,
                                 af_vec3_scale(af_vec3_sub(body->p, body->water_previous_p), fraction));
    for (unsigned axis = 0; axis < 2u; ++axis) {
        interpolated.axes[axis] =
            af_vec3_add(body->water_previous_axes[axis],
                        af_vec3_scale(af_vec3_sub(body->axes[axis], body->water_previous_axes[axis]), fraction));
        if (af_vec3_length(interpolated.axes[axis]) < 1e-5f)
            interpolated.axes[axis] = body->axes[axis];
    }
    interpolated.axes[0] = af_vec3_normalize(interpolated.axes[0]);
    interpolated.axes[1] = af_vec3_sub(
        interpolated.axes[1], af_vec3_scale(interpolated.axes[0], af_vec3_dot(interpolated.axes[0], interpolated.axes[1])));
    if (af_vec3_length(interpolated.axes[1]) < 1e-5f)
        interpolated.axes[1] = body->axes[1];
    interpolated.axes[1] = af_vec3_normalize(interpolated.axes[1]);
    interpolated.axes[2] = af_vec3_cross(interpolated.axes[0], interpolated.axes[1]);
    interpolated.shape.center = interpolated.shape.a = interpolated.shape.b = interpolated.p;
    for (unsigned axis = 0; axis < 3u; ++axis)
        interpolated.shape.axes[axis] = interpolated.axes[axis];
    return ph_part_world(&interpolated, part);
}

static void record_particle_contact(wa_particle *particle, size_t body_index, af_collision_hit hit) {
    particle->normal = hit.normal;
    particle->point = hit.point;
    particle->hit_body = body_index;
    for (unsigned i = 0; i < particle->contact_count; ++i) {
        if (particle->contacts[i].body == body_index && af_vec3_dot(particle->contacts[i].normal, hit.normal) > 0.97f) {
            particle->contacts[i] = (wa_fluid_contact){body_index, hit.normal, hit.point};
            return;
        }
    }
    if (particle->contact_count < 4u)
        particle->contacts[particle->contact_count++] = (wa_fluid_contact){body_index, hit.normal, hit.point};
}

static void collide_particle_with_world(const af_physics *world, wa_particle *particle, float radius, bool sweep) {
    for (size_t body_idx = 0; body_idx < world->count; ++body_idx) {
        const ph_body *body = &world->bodies[body_idx];
        if (body->shape.trigger || body->character_proxy)
            continue;
        size_t part_count = body->part_count ? body->part_count : 1u;
        for (size_t part = 0; part < part_count; ++part) {
            float substep_start = (float)world->water_substep / (float)world->stats.substeps;
            float substep_end = (float)(world->water_substep + 1u) / (float)world->stats.substeps;
            af_world_collider target_shape = interpolated_body_shape(body, part, substep_end);
            af_vec3 origin = particle->p;
            if (body->water_motion) {
                af_world_collider previous_shape = interpolated_body_shape(body, part, substep_start);
                af_vec3 local_delta = af_vec3_sub(particle->p, previous_shape.center);
                origin = target_shape.center;
                for (unsigned axis = 0; axis < 3u; ++axis)
                    origin = af_vec3_add(
                        origin, af_vec3_scale(target_shape.axes[axis], af_vec3_dot(local_delta, previous_shape.axes[axis])));
            }
            if (!near_collider_aabb(target_shape, origin, particle->q, radius))
                continue;
            af_collision_hit hit;
            if (sweep &&
                af_collider_sweep_sphere(target_shape, origin, radius, af_vec3_sub(particle->q, origin), 0.0002f, &hit)) {
                af_vec3 displacement = af_vec3_sub(particle->q, origin);
                af_vec3 remaining = af_vec3_scale(displacement, 1.0f - hit.fraction);
                float into_surface = af_vec3_dot(remaining, hit.normal);
                if (into_surface < 0.0f)
                    remaining = af_vec3_sub(remaining, af_vec3_scale(hit.normal, into_surface));
                particle->q = af_vec3_add(af_vec3_add(origin, af_vec3_scale(displacement, hit.fraction)),
                                          af_vec3_add(remaining, af_vec3_scale(hit.normal, 0.0003f)));
                record_particle_contact(particle, body_idx, hit);
            }
            float separation = af_collision_capsule_distance(target_shape, particle->q, radius, 0.0f, &hit);
            if (separation < 0.0002f) {
                particle->q =
                    af_vec3_add(particle->q, af_vec3_scale(hit.normal, fminf(radius * 4.0f, 0.0002f - separation)));
                record_particle_contact(particle, body_idx, hit);
            }
        }
    }
}

static void couple_particle_with_bodies(af_physics *world, wa_particle *particle, af_vec3 incoming_velocity,
                                        float particle_mass) {
    for (unsigned k = 0; k < particle->contact_count; ++k) {
        wa_fluid_contact contact = particle->contacts[k];
        ph_body *body = &world->bodies[contact.body];
        af_vec3 lever_arm = af_vec3_sub(contact.point, body->p);
        af_vec3 surface_vel = af_vec3_add(body->v, af_vec3_cross(body->w, lever_arm));
        float approach_speed = af_vec3_dot(af_vec3_sub(incoming_velocity, surface_vel), contact.normal);
        float projected_speed = af_vec3_dot(af_vec3_sub(particle->v, surface_vel), contact.normal);
        if (approach_speed >= 0.0f) {
            if (projected_speed < 0.0f)
                particle->v = af_vec3_sub(particle->v, af_vec3_scale(contact.normal, projected_speed));
            continue;
        }
        if (body->inv_mass > 0.0f) {
            body->sleeping = false;
            body->sleep_time = 0.0f;
            body->wet = true;
            body->water_calm = false;
        }
        float body_inv_eff =
            body->inv_mass +
            af_vec3_dot(contact.normal,
                        af_vec3_cross(ph_inverse_inertia(body, af_vec3_cross(lever_arm, contact.normal)), lever_arm));
        float impulse_mag = fminf(-approach_speed, 30.0f) / (1.0f / particle_mass + fmaxf(0.0f, body_inv_eff));
        af_vec3 reaction = af_vec3_scale(contact.normal, impulse_mag);
        ph_impulse(body, af_vec3_scale(reaction, -1.0f), contact.point);
        incoming_velocity = af_vec3_add(incoming_velocity, af_vec3_scale(reaction, 1.0f / particle_mass));
        float target_normal_speed = af_vec3_dot(af_vec3_sub(incoming_velocity, surface_vel), contact.normal);
        particle->v = af_vec3_add(particle->v, af_vec3_scale(contact.normal, target_normal_speed - projected_speed));

        surface_vel = af_vec3_add(body->v, af_vec3_cross(body->w, lever_arm));
        af_vec3 tangent_vel = af_vec3_sub(particle->v, surface_vel);
        tangent_vel = af_vec3_sub(tangent_vel, af_vec3_scale(contact.normal, af_vec3_dot(tangent_vel, contact.normal)));
        float tangent_speed = af_vec3_length(tangent_vel);
        if (tangent_speed > 1e-7f) {
            af_vec3 tangent_dir = af_vec3_scale(tangent_vel, 1.0f / tangent_speed);
            float effective_inv_mass =
                1.0f / particle_mass + body->inv_mass +
                af_vec3_dot(tangent_dir,
                            af_vec3_cross(ph_inverse_inertia(body, af_vec3_cross(lever_arm, tangent_dir)), lever_arm));
            float friction_mag = fminf(tangent_speed / fmaxf(effective_inv_mass, 1e-7f),
                                       impulse_mag * fminf(0.3f, body->config.friction * 0.2f));
            af_vec3 friction_impulse = af_vec3_scale(tangent_dir, -friction_mag);
            particle->v = af_vec3_add(particle->v, af_vec3_scale(friction_impulse, 1.0f / particle_mass));
            incoming_velocity = af_vec3_add(incoming_velocity, af_vec3_scale(friction_impulse, 1.0f / particle_mass));
            ph_impulse(body, af_vec3_scale(friction_impulse, -1.0f), contact.point);
        }
    }
    particle->v = ph_limit(particle->v, 30.0f);
}

typedef struct pbf_job_context {
    const af_physics *world;
    wa_particles *fluid;
    float dt;
    float spacing;
    float kernel_radius;
    float particle_radius;
    float volume;
    float viscosity;
    float scorr_denom;
} pbf_job_context;

static void job_predict_and_collide(size_t start, size_t end, void *user_data) {
    const pbf_job_context *ctx = user_data;
    wa_particles *fluid = ctx->fluid;
    for (size_t i = start; i < end; ++i) {
        wa_particle *particle = &fluid->values[i];
        particle->v.y -= 9.81f * ctx->dt;
        particle->q = af_vec3_add(particle->p, af_vec3_scale(particle->v, ctx->dt));
        particle->normal = (af_vec3){0};
        particle->hit_body = SIZE_MAX;
        particle->contact_count = 0;
        collide_particle_with_world(ctx->world, particle, ctx->particle_radius, true);
    }
}

static void job_compute_lambda(size_t start, size_t end, void *user_data) {
    const pbf_job_context *ctx = user_data;
    wa_particles *fluid = ctx->fluid;
    float h = ctx->kernel_radius;
    float volume = ctx->volume;
    float self_density = volume * poly6_kernel(0.0f, h);
    float relaxation = 0.001f / (ctx->spacing * ctx->spacing);

    for (size_t i = start; i < end; ++i) {
        wa_particle *particle = &fluid->values[i];
        float density = self_density;
        float grad_sq_sum = 0.0f;
        af_vec3 self_gradient = {0};

        for (int cz = particle->cell[2] - 1; cz <= particle->cell[2] + 1; ++cz) {
            for (int cy = particle->cell[1] - 1; cy <= particle->cell[1] + 1; ++cy) {
                for (int cx = particle->cell[0] - 1; cx <= particle->cell[0] + 1; ++cx) {
                    for (uint32_t j = fluid->heads[hash_cell(cx, cy, cz)]; j != UINT32_MAX;
                         j = fluid->values[j].next) {
                        const wa_particle *neighbor = &fluid->values[j];
                        if (j == i || !matches_cell(neighbor, cx, cy, cz))
                            continue;
                        af_vec3 offset = af_vec3_sub(particle->q, neighbor->q);
                        density += volume * poly6_kernel(af_vec3_dot(offset, offset), h);
                        af_vec3 grad = af_vec3_scale(spiky_gradient(offset, h, i, j), volume);
                        self_gradient = af_vec3_add(self_gradient, grad);
                        grad_sq_sum += af_vec3_dot(grad, grad);
                    }
                }
            }
        }
        particle->lambda = -fmaxf(0.0f, density - 1.0f) /
                           (grad_sq_sum + af_vec3_dot(self_gradient, self_gradient) + relaxation);
    }
}

static void job_compute_position_delta(size_t start, size_t end, void *user_data) {
    const pbf_job_context *ctx = user_data;
    wa_particles *fluid = ctx->fluid;
    float h = ctx->kernel_radius;
    float volume = ctx->volume;
    float scorr_scale = -0.0001f * ctx->spacing * ctx->spacing;
    float max_step = ctx->spacing * 0.15f;

    for (size_t i = start; i < end; ++i) {
        wa_particle *particle = &fluid->values[i];
        af_vec3 correction = {0};

        for (int cz = particle->cell[2] - 1; cz <= particle->cell[2] + 1; ++cz) {
            for (int cy = particle->cell[1] - 1; cy <= particle->cell[1] + 1; ++cy) {
                for (int cx = particle->cell[0] - 1; cx <= particle->cell[0] + 1; ++cx) {
                    for (uint32_t j = fluid->heads[hash_cell(cx, cy, cz)]; j != UINT32_MAX;
                         j = fluid->values[j].next) {
                        const wa_particle *neighbor = &fluid->values[j];
                        if (j == i || !matches_cell(neighbor, cx, cy, cz))
                            continue;
                        af_vec3 offset = af_vec3_sub(particle->q, neighbor->q);
                        float ratio = poly6_kernel(af_vec3_dot(offset, offset), h) / ctx->scorr_denom;
                        float ratio2 = ratio * ratio;
                        float tensile_corr = scorr_scale * ratio2 * ratio2;
                        correction = af_vec3_add(
                            correction,
                            af_vec3_scale(spiky_gradient(offset, h, i, j),
                                          volume * (particle->lambda + neighbor->lambda + tensile_corr)));
                    }
                }
            }
        }
        particle->delta = ph_limit(correction, max_step);
    }
}

static void job_apply_delta_and_collide(size_t start, size_t end, void *user_data) {
    const pbf_job_context *ctx = user_data;
    wa_particles *fluid = ctx->fluid;
    for (size_t i = start; i < end; ++i) {
        wa_particle *particle = &fluid->values[i];
        particle->q = af_vec3_add(particle->q, particle->delta);
        collide_particle_with_world(ctx->world, particle, ctx->particle_radius, false);
    }
}

static void job_compute_viscosity(size_t start, size_t end, void *user_data) {
    const pbf_job_context *ctx = user_data;
    wa_particles *fluid = ctx->fluid;
    float h = ctx->kernel_radius;
    float volume = ctx->volume;

    for (size_t i = start; i < end; ++i) {
        wa_particle *particle = &fluid->values[i];
        af_vec3 velocity_delta = {0};
        float weight_sum = 0.0f;

        for (int cz = particle->cell[2] - 1; cz <= particle->cell[2] + 1; ++cz) {
            for (int cy = particle->cell[1] - 1; cy <= particle->cell[1] + 1; ++cy) {
                for (int cx = particle->cell[0] - 1; cx <= particle->cell[0] + 1; ++cx) {
                    for (uint32_t j = fluid->heads[hash_cell(cx, cy, cz)]; j != UINT32_MAX;
                         j = fluid->values[j].next) {
                        const wa_particle *neighbor = &fluid->values[j];
                        if (j == i || !matches_cell(neighbor, cx, cy, cz))
                            continue;
                        af_vec3 offset = af_vec3_sub(particle->p, neighbor->p);
                        float weight = volume * poly6_kernel(af_vec3_dot(offset, offset), h);
                        weight_sum += weight;
                        velocity_delta = af_vec3_add(
                            velocity_delta, af_vec3_scale(af_vec3_sub(neighbor->v, particle->v), weight));
                    }
                }
            }
        }
        particle->delta = af_vec3_scale(velocity_delta, ctx->viscosity / fmaxf(1.0f, weight_sum));
    }
}

void ph_particles_step(af_physics *world, wa_slot *slot, float dt) {
    wa_particles *fluid = slot->particles;
    if (!fluid)
        return;

    uint64_t geometry_hash = UINT64_C(1469598103934665603);
    for (size_t i = 0; i < world->count; ++i) {
        const ph_body *body = &world->bodies[i];
        geometry_hash = (geometry_hash ^ body->id ^ body->geometry_key) * UINT64_C(1099511628211);
        const unsigned char *bytes = (const unsigned char *)&body->p;
        for (size_t k = 0; k < sizeof(body->p); ++k)
            geometry_hash = (geometry_hash ^ bytes[k]) * UINT64_C(1099511628211);
        bytes = (const unsigned char *)body->axes;
        for (size_t k = 0; k < sizeof(body->axes); ++k)
            geometry_hash = (geometry_hash ^ bytes[k]) * UINT64_C(1099511628211);
    }
    if (fluid->sleeping && geometry_hash == fluid->geometry_key && slot->config.emission_rate == 0.0f)
        return;

    fluid->sleeping = false;
    fluid->geometry_key = geometry_hash;
    af_water config = slot->config;
    float spacing = fluid->spacing;
    float kernel_radius = spacing * 2.0f;
    float particle_radius = spacing * 0.42f;
    float volume = fluid->volume;

    size_t count_before = fluid->count;
    fluid->pending =
        config.emission_rate > 0.0f ? fminf(fluid->pending + config.emission_rate * dt, volume * 64.0f) : 0.0f;
    size_t desired_births = (size_t)floorf(fluid->pending / volume);
    emit_particles(slot, desired_births, false);
    fluid->pending -= volume * (float)(fluid->count - count_before);

    af_jobs *jobs = af_jobs_global();
    const size_t particle_grain = 256u;
    pbf_job_context ctx = {.world = world,
                           .fluid = fluid,
                           .dt = dt,
                           .spacing = spacing,
                           .kernel_radius = kernel_radius,
                           .particle_radius = particle_radius,
                           .volume = volume,
                           .viscosity = -expm1f(-config.viscosity * dt),
                           .scorr_denom = poly6_kernel(0.09f * kernel_radius * kernel_radius, kernel_radius)};

    af_jobs_parallel_for(jobs, fluid->count, particle_grain, job_predict_and_collide, &ctx);

    unsigned solver_iterations = 3u + 2u * (unsigned)config.quality;
    for (unsigned iter = 0; iter < solver_iterations; ++iter) {
        rebuild_spatial_grid(fluid);
        af_jobs_parallel_for(jobs, fluid->count, particle_grain, job_compute_lambda, &ctx);
        af_jobs_parallel_for(jobs, fluid->count, particle_grain, job_compute_position_delta, &ctx);
        af_jobs_parallel_for(jobs, fluid->count, particle_grain, job_apply_delta_and_collide, &ctx);
    }

    /* Rigid-body impulse coupling remains sequential to preserve exact body impulse order. */
    float particle_mass = config.density * volume;
    for (size_t i = 0; i < fluid->count; ++i) {
        wa_particle *particle = &fluid->values[i];
        af_vec3 incoming_velocity = particle->v;
        particle->v = ph_limit(af_vec3_scale(af_vec3_sub(particle->q, particle->p), 1.0f / dt), 30.0f);
        couple_particle_with_bodies(world, particle, incoming_velocity, particle_mass);
        particle->p = particle->q;
    }

    rebuild_spatial_grid(fluid);
    af_jobs_parallel_for(jobs, fluid->count, particle_grain, job_compute_viscosity, &ctx);

    size_t surviving = 0;
    for (size_t i = 0; i < fluid->count; ++i) {
        wa_particle particle = fluid->values[i];
        particle.v = af_vec3_add(particle.v, particle.delta);
        af_vec3 relative = af_vec3_sub(particle.p, slot->transform.position);
        if (!ph_finite(particle.p) || fabsf(relative.x) > 50.0f || fabsf(relative.z) > 50.0f ||
            relative.y < -30.0f || relative.y > 50.0f) {
            fluid->escaped += volume;
            continue;
        }
        fluid->values[surviving++] = particle;
    }
    fluid->count = surviving;

    float max_speed = 0.0f;
    bool supported = false;
    for (size_t i = 0; i < fluid->count; ++i) {
        max_speed = fmaxf(max_speed, af_vec3_length(fluid->values[i].v));
        supported |= fluid->values[i].hit_body < world->count;
    }
    fluid->quiet_time = config.emission_rate == 0.0f && supported && max_speed < 0.12f ? fluid->quiet_time + dt : 0.0f;
    if (fluid->quiet_time > 1.0f) {
        fluid->sleeping = true;
        for (size_t i = 0; i < fluid->count; ++i)
            fluid->values[i].v = (af_vec3){0};
    }
    slot->time += dt;
}

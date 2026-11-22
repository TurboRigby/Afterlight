/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "metal_internal.h"

static bool prepare_shadows(af_mtl_renderer *r, const af_scene *scene, const af_camera *camera, af_rect area,
                            af_frame_uniform *uniform, af_shadow_view *view) {
    af_shadow_settings settings = af_scene_get_shadows(scene);
    uint32_t layers = settings.mode == AF_SHADOW_CASCADED ? 4u : 1u;
    if (settings.resolution != r->shadow_resolution || layers != r->shadow_layers) {
        if (!af_mtl_r_wait_idle(r))
            return false;
        af_mtl_shadows *replacement =
            af_mtl_shadows_create(r->device, r->shadow_vert_fn, r->shadow_frag_fn, settings.resolution, layers);
        if (!replacement)
            return false;
        af_mtl_shadows_destroy(r->shadows);
        r->shadows = replacement;
        r->shadow_resolution = settings.resolution;
        r->shadow_layers = layers;
        for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
            memset(r->frames[f].shadow_signatures, 0, sizeof(r->frames[f].shadow_signatures));
    }

    bool local_cast = false;
    for (size_t i = 0; i < af_scene_local_light_count(scene); ++i) {
        const af_local_light *l = af_scene_local_light_at(scene, i);
        if (l->enabled && l->cast_shadows && l->intensity > 0)
            local_cast = true;
    }

    size_t count = af_scene_object_count(scene);
    const af_landscape *ls = af_scene_get_landscape(scene);
    if ((settings.enabled || local_cast) && ls && ls->enabled) {
        count += ls->chunk_count;
        for (size_t l = 0; l < ls->foliage_layer_count; ++l)
            if (ls->foliage_layers[l].cast_shadows && ls->foliage_layers[l].visible)
                count += ls->foliage_layers[l].total_instances;
    }
    if (count > r->shadow_capacity) {
        af_mtl_shadow_draw *draws = realloc(r->shadow_draws, count * sizeof(*draws));
        if (!draws)
            return false;
        r->shadow_draws = draws;
        af_shadow_bounds *bounds = realloc(r->shadow_bounds, count * sizeof(*bounds));
        if (!bounds)
            return false;
        r->shadow_bounds = bounds;
        r->shadow_capacity = count;
    }

    r->shadow_count = 0;
    for (size_t i = 0; (settings.enabled || local_cast) && i < af_scene_object_count(scene); ++i) {
        const af_scene_object *object = af_scene_object_at(scene, i);
        if (!object->material.cast_shadows || object->material.alpha_mode == AF_ALPHA_BLEND ||
            !af_scene_object_visible(scene, object->id))
            continue;
        size_t mesh = af_mtl_find_mesh_index(r, object->mesh);
        af_mat4 model;
        if (mesh == r->mesh_count || !af_mtl_object_world(r, scene, i, &model))
            continue;
        size_t n = r->shadow_count++;
        af_mtl_texture *texture = r->white;
        for (size_t t = 0; t < r->texture_count; ++t)
            if (!r->textures[t].normal && !strcmp(r->textures[t].name, object->material.texture)) {
                texture = r->textures[t].texture;
                break;
            }
        r->shadow_draws[n] = (af_mtl_shadow_draw){
            model,
            mesh,
            af_mtl_skin_base(r, object->id, &r->meshes[mesh].gpu),
            texture,
            object->material.base_color.a,
            object->material.alpha_cutoff,
            af_rc_shadow_material_flags(object->material.alpha_mode, r->meshes[mesh].gpu.sidedness, model),
            false,
            i};
        r->shadow_bounds[n] =
            af_mtl_object_bounds(r, object->id, model, &r->meshes[mesh].gpu, af_mtl_morph_draw_for(r, scene, i));
    }

    if ((settings.enabled || local_cast) && ls && ls->enabled) {
        for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
            const af_terrain_chunk *c = &ls->chunks[c_idx];
            size_t mesh = af_mtl_find_mesh_index(r, c->mesh_name);
            if (mesh == r->mesh_count)
                continue;
            size_t n = r->shadow_count++;
            r->shadow_draws[n] = (af_mtl_shadow_draw){
                af_mat4_identity(), mesh, 0, r->white, 1.0f, 0.5f, (uint32_t)AF_ALPHA_OPAQUE, false, SIZE_MAX};
            r->shadow_bounds[n] = af_shadow_transform_bounds(af_mat4_identity(), c->bounds_min, c->bounds_max);
        }
        for (size_t l = 0; l < ls->foliage_layer_count; ++l) {
            const af_foliage_layer *fl = &ls->foliage_layers[l];
            if (!fl->visible || !fl->cast_shadows || fl->total_instances == 0 || fl->mesh[0] == '\0')
                continue;
            size_t mesh = af_mtl_find_mesh_index(r, fl->mesh);
            if (mesh == r->mesh_count)
                continue;
            const af_mtl_gpu_mesh *m = &r->meshes[mesh].gpu;
            af_mtl_texture *texture = r->white;
            for (size_t t = 0; t < r->texture_count; ++t) {
                if (!r->textures[t].normal && !strcmp(r->textures[t].name, fl->material.texture)) {
                    texture = r->textures[t].texture;
                    break;
                }
            }
            for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
                const af_foliage_chunk *fc = &ls->chunks[c_idx].foliage[l];
                for (size_t inst_idx = 0; inst_idx < fc->count; ++inst_idx) {
                    af_mat4 model = af_transform_matrix(fc->instances[inst_idx].transform);
                    size_t n = r->shadow_count++;
                    r->shadow_draws[n] = (af_mtl_shadow_draw){
                        model,
                        mesh,
                        0,
                        texture,
                        fl->material.base_color.a,
                        fl->material.alpha_cutoff,
                        af_rc_shadow_material_flags(fl->material.alpha_mode, m->sidedness, model),
                        false,
                        SIZE_MAX};
                    r->shadow_bounds[n] = af_shadow_transform_bounds(model, m->minimum, m->maximum);
                }
            }
        }
    }

    af_directional_light light = af_scene_get_light(scene);
    af_vec3 direction = af_vec3_normalize(light.direction), forward = af_camera_forward(camera);
    if (!af_shadow_build(camera, area.width / area.height, direction, settings, r->shadow_bounds, r->shadow_count, view))
        return false;

    *uniform = (af_frame_uniform){
        .view_projection = af_mat4_mul(
            af_mat4_perspective(camera->vertical_fov, area.width / area.height, camera->near_clip, camera->far_clip),
            af_camera_view(camera)),
        .direction_intensity = {direction.x, direction.y, direction.z, light.intensity},
        .color_ambient = {light.color.x, light.color.y, light.color.z, light.ambient},
        .cascade_splits = {view->splits[0], view->splits[1], view->splits[2], view->splits[3]},
        .camera_position = {camera->position.x, camera->position.y, camera->position.z, camera->near_clip},
        .camera_forward = {forward.x, forward.y, forward.z, 0},
        .shadow_params = {settings.enabled ? 1.0f : 0.0f, 1.0f / (float)settings.resolution, 0.00015f,
                          settings.normal_bias},
        .cascade_params = {(float)view->count, (float)settings.filter_radius, settings.blend_fraction,
                           settings.debug_cascades ? 1.0f : 0.0f}};
    af_render_settings render_settings = af_scene_get_render_settings(scene);
    uniform->render_params = (af_vec4){exp2f(render_settings.exposure), render_settings.tone_mapping ? 1.0f : 0.0f,
                                       render_settings.environment_enabled ? render_settings.environment_intensity : 0,
                                       render_settings.environment_rotation};
    if (!af_mat4_inverse(uniform->view_projection, &uniform->inverse_view_projection))
        return false;
    uniform->sky_params = (af_vec4){render_settings.skybox_intensity, 0, 0, 0};

    int logical_width = 1, logical_height = 1;
    SDL_GetWindowSize(r->window, &logical_width, &logical_height);
    if (logical_width <= 0)
        logical_width = 1;
    if (logical_height <= 0)
        logical_height = 1;
    uniform->water_screen = (af_vec4){(float)r->swapchain.render_width, (float)r->swapchain.render_height,
                                      render_settings.refraction_strength, render_settings.refraction ? 1.f : 0};
    uniform->water_viewport =
        (af_vec4){area.x / (float)logical_width, area.y / (float)logical_height,
                  (area.x + area.width) / (float)logical_width, (area.y + area.height) / (float)logical_height};

    double water_time = af_physics_time(r->water_physics);
    af_vec3 air = af_wind_velocity(af_scene_get_wind(scene), water_time);
    uniform->wind_time = (af_vec4){air.x, air.z, (float)water_time, (float)(r->rendered_frames & 255u)};

    af_water_sample underwater = af_physics_sample_water(r->water_physics, scene, camera->position);
    const af_water *water = af_scene_get_water(scene, underwater.owner);
    if (render_settings.underwater && water && underwater.inside && camera->position.y > underwater.bottom &&
        camera->position.y < underwater.height + .1f) {
        const af_scene_object *o = af_scene_get_object(scene, water->owner);
        float blend = fmaxf(0, fminf(1, (underwater.height + .1f - camera->position.y) / .2f));
        blend = blend * blend * (3 - 2 * blend);
        float yaw = o->transform.rotation.y;
        af_vec3 origin = o->transform.position;
        af_vec2 size = water->half_size;
        if (water->mode == AF_WATER_RIVER) {
            float c = cosf(yaw), s = sinf(yaw);
            af_vec3 d = af_vec3_sub(camera->position, origin), local = {c * d.x - s * d.z, d.y, s * d.x + c * d.z},
                    tangent;
            float side, along, height;
            if (wa_river_coordinates(water, local, &side, &along, &height, &tangent)) {
                float length = wa_river_length(water);
                local = af_vec3_add(local, af_vec3_add((af_vec3){-side * tangent.z, 0, side * tangent.x},
                                                       af_vec3_scale(tangent, length * .5f - along)));
                origin.x += c * local.x + s * local.z;
                origin.z += -s * local.x + c * local.z;
                yaw += atan2f(tangent.x, tangent.z);
                size.y = length * .5f;
            }
        }
        af_vec3 illumination = af_vec3_add((af_vec3){light.ambient, light.ambient, light.ambient},
                                           af_vec3_scale(light.color, light.intensity * .12f));
        uniform->water_absorption = (af_vec4){water->absorption.x, water->absorption.y, water->absorption.z, blend};
        uniform->water_scattering =
            (af_vec4){water->scattering.x * illumination.x, water->scattering.y * illumination.y,
                      water->scattering.z * illumination.z, underwater.height};
        uniform->water_bounds = (af_vec4){origin.x, origin.z, size.x, size.y};
        uniform->water_axes = (af_vec4){cosf(yaw), sinf(yaw), underwater.bottom, 0};
    }

    uint32_t local_layers = 0, shadow_lights = 0;
    unsigned requested = 0, selected[AF_LOCAL_SHADOW_LIGHTS];
    af_local_light local_lights[AF_SCENE_MAX_LOCAL_LIGHTS];
    size_t local_count = af_scene_local_light_count(scene);
    for (size_t i = 0; i < local_count; ++i)
        local_lights[i] = *af_scene_local_light_at(scene, i);
    unsigned selected_count =
        af_local_shadow_select(local_lights, local_count, camera, area.width / area.height, selected, &requested);
    r->local_light_staging_count = 0;
    for (size_t i = 0; i < af_scene_local_light_count(scene); ++i) {
        const af_local_light *local = af_scene_local_light_at(scene, i);
        if (!local->enabled)
            continue;
        af_vec3 ray = af_vec3_normalize(local->direction);
        unsigned n = r->local_light_staging_count++;
        r->local_light_staging[n] =
            (af_gpu_light){.position_range = {local->position.x, local->position.y, local->position.z, local->range},
                           .color_intensity = {local->color.x, local->color.y, local->color.z, local->intensity},
                           .direction_inner = {ray.x, ray.y, ray.z, cosf(local->inner_angle)},
                           .params = {cosf(local->outer_angle), local->type == AF_LIGHT_SPOT ? 1.0f : 0.0f, 0, 0}};
        uniform->light_count.x = (float)r->local_light_staging_count;
        bool chosen = false;
        for (unsigned j = 0; j < selected_count; ++j)
            if (selected[j] == i)
                chosen = true;
        if (chosen) {
            r->local_light_staging[n].params.z = (float)(local_layers + 1);
            r->local_light_staging[n].params.w = local->shadow_bias;
            unsigned faces = local->type == AF_LIGHT_POINT ? 6u : 1u;
            for (unsigned face = 0; face < faces; ++face)
                uniform->local_shadow_matrices[local_layers++] = af_local_shadow_matrix(*local, face);
            ++shadow_lights;
        }
    }

    r->local_shadow_lights = shadow_lights;
    r->local_shadow_overflow = requested - shadow_lights;
    uint32_t wanted_layers = local_layers ? local_layers : 1u;
    uint32_t wanted_res = render_settings.local_shadow_resolution ? render_settings.local_shadow_resolution : 512u;
    if (wanted_layers != r->local_shadow_layers || wanted_res != r->local_shadow_resolution) {
        if (!af_mtl_r_wait_idle(r))
            return false;
        af_mtl_shadows *replacement =
            af_mtl_shadows_create(r->device, r->shadow_vert_fn, r->shadow_frag_fn, wanted_res, wanted_layers);
        if (!replacement)
            return false;
        af_mtl_shadows_destroy(r->local_shadows);
        r->local_shadows = replacement;
        r->local_shadow_layers = wanted_layers;
        r->local_shadow_resolution = wanted_res;
        for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
            memset(r->frames[f].local_shadow_signatures, 0, sizeof(r->frames[f].local_shadow_signatures));
    }
    uniform->light_count.y = 1.0f / (float)r->local_shadow_resolution;
    uniform->light_count.z = (float)local_layers;
    memcpy(uniform->shadow_matrices, view->matrices, sizeof(view->matrices));
    return true;
}

static size_t add_streamed_ray_instances(af_mtl_renderer *r, const af_mtl_gpu_mesh *mesh, af_mtl_ray_instance base,
                                         af_cluster_view view, size_t extra, af_mtl_ray_instance *inputs) {
    af_mtl_cluster_index_range ranges[AF_CLUSTER_INDEX_RANGES];
    size_t count = af_mtl_cluster_index_ranges(r, mesh, base.world, view, ranges);
    if (!count || count - 1 > extra) {
        inputs[0] = base;
        return 1;
    }
    for (size_t i = 0; i < count; ++i) {
        const af_mtl_cluster_index_range *range = &ranges[i];
        af_mtl_ray_instance input = base;
        input.transient = true;
        input.first_index = range->first;
        input.index_count = range->count;
        if (range->page) {
            af_mtl_cached_cluster_page *page = range->page;
            af_mtl_use_cluster_page(r, page);
            input.page_generation = page->generation;
            input.vertices = page->gpu.vertices.handle;
            input.indices = page->gpu.indices.handle;
            input.vertex_count = page->gpu.vertex_count;
            ++r->cluster_stats.ray_fine_instances;
        }
        inputs[i] = input;
    }
    return count;
}

static bool prepare_ray_scene(af_mtl_renderer *r, const af_scene *scene, bool shadows, bool reflections, bool gi,
                              bool material_effects, af_cluster_view view, uint64_t *emitter_signature) {
    if (r->rays && r->ray_revision != r->mesh_revision) {
        if (!af_mtl_r_wait_idle(r))
            return false;
        af_mtl_ray_query_destroy(r->rays);
        r->rays = NULL;
    }
    if (!r->rays) {
        r->rays = af_mtl_ray_query_create(r->device, AF_FRAMES_IN_FLIGHT);
        r->ray_revision = r->mesh_revision;
        if (!r->rays)
            return false;
    }
    size_t objects = af_scene_object_count(scene);
    size_t limit = af_mtl_ray_query_instance_limit(r->rays);
    size_t extra = objects < limit ? limit - objects : 0;
    if (extra > 256)
        extra = 256;
    size_t capacity = objects + extra;
    if (capacity > r->ray_input_capacity) {
        af_mtl_ray_instance *inputs = realloc(r->ray_inputs, capacity * sizeof(*inputs));
        if (!inputs)
            return false;
        r->ray_inputs = inputs;
        r->ray_input_capacity = capacity;
    }

    size_t count = 0, shadow_index = 0;
    r->ray_raster_casters = 0;
    for (size_t i = 0; i < objects; ++i) {
        af_mtl_shadow_draw *caster = NULL;
        if (shadow_index < r->shadow_count && r->shadow_draws[shadow_index].object_index == i)
            caster = &r->shadow_draws[shadow_index++];
        if (!reflections && !gi && !material_effects && !caster)
            continue;
        const af_scene_object *object = af_scene_object_at(scene, i);
        const af_material *material = &object->material;
        uint8_t mask = (shadows && caster ? AF_RAY_SHADOW_MASK : 0) |
                       ((reflections || material_effects) ? AF_RAY_REFLECTION_MASK : 0) | (gi ? AF_RAY_GI_MASK : 0);
        if (!mask || material->alpha_mode != AF_ALPHA_OPAQUE || af_scene_get_water(scene, object->id) ||
            (!caster && !af_scene_object_visible(scene, object->id)))
            continue;
        size_t mesh = caster ? caster->mesh : af_mtl_find_mesh_index(r, object->mesh);
        if (mesh == r->mesh_count)
            continue;
        const af_mtl_mesh_asset *asset = &r->meshes[mesh];
        if (asset->dynamic || asset->gpu.skinned || asset->gpu.morph)
            continue;
        af_mat4 world, inverse;
        if (caster)
            world = caster->model;
        else if (!af_mtl_object_world(r, scene, i, &world))
            continue;
        if (!af_mat4_inverse(world, &inverse))
            continue;
        if (caster && shadows)
            caster->ray_caster = true;

        af_mtl_ray_instance input = {.mesh_key = mesh + 1,
                                     .vertices = asset->gpu.vertices.handle,
                                     .indices = asset->gpu.indices.handle,
                                     .vertex_count = asset->gpu.vertex_count,
                                     .index_count = asset->gpu.index_count,
                                     .world = world,
                                     .mask = mask,
                                     .single_sided = asset->gpu.sidedness == AF_MESH_SIDES_FRONT,
                                     .color = material->base_color,
                                     .emission_unlit = {material->emission.x * material->emission_strength,
                                                        material->emission.y * material->emission_strength,
                                                        material->emission.z * material->emission_strength,
                                                        material->unlit ? 1.f : 0.f}};
        size_t added = add_streamed_ray_instances(r, &asset->gpu, input, view, extra, r->ray_inputs + count);
        count += added;
        extra -= added - 1;
        af_vec4 emission = input.emission_unlit;
        if (emitter_signature && (emission.x > 0 || emission.y > 0 || emission.z > 0 || material->unlit)) {
            *emitter_signature = af_r_hash(*emitter_signature, &object->id, sizeof(object->id));
            *emitter_signature = af_r_hash(*emitter_signature, &input.mesh_key, sizeof(input.mesh_key));
            *emitter_signature = af_r_hash(*emitter_signature, &emission, sizeof(emission));
            if (material->unlit)
                *emitter_signature = af_r_hash(*emitter_signature, &input.color, sizeof(input.color));
        }
    }
    if (shadows)
        for (size_t i = 0; i < r->shadow_count; ++i)
            if (!r->shadow_draws[i].ray_caster)
                ++r->ray_raster_casters;

    return af_mtl_ray_query_prepare(r->rays, r->frame_index, r->ray_inputs, count,
                                    reflections || gi || material_effects);
}

static size_t shadow_cluster_ranges(const af_mtl_renderer *r, const af_mtl_shadow_draw *draw, af_cluster_view view,
                                    af_mtl_cluster_index_range *ranges) {
    if (draw->object_index == SIZE_MAX || (draw->material_flags & 3u) != AF_ALPHA_OPAQUE)
        return 0;
    return af_mtl_cluster_index_ranges(r, &r->meshes[draw->mesh].gpu, draw->model, view, ranges);
}

static void draw_shadow_casters(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame,
                                const af_scene *scene, af_mat4 matrix, af_cluster_view view, uint64_t *fine_draws) {
    for (size_t i = 0; i < r->shadow_count; ++i) {
        const af_mtl_shadow_draw *draw = &r->shadow_draws[i];
        if (draw->ray_caster || !af_shadow_intersects(matrix, &r->shadow_bounds[i]))
            continue;
        const af_mtl_mesh_asset *asset = &r->meshes[draw->mesh];
        const af_mtl_gpu_mesh *mesh = asset->dynamic ? &asset->streams[r->frame_index].gpu : &asset->gpu;
        af_mat4 mvp = af_mat4_mul(matrix, draw->model);
        af_mtl_shadows_push(enc, mvp.elements, draw->skin_base, draw->material_flags, draw->alpha, draw->cutoff,
                            draw->texture);
        af_mtl_cluster_index_range ranges[AF_CLUSTER_INDEX_RANGES];
        size_t count = shadow_cluster_ranges(r, draw, view, ranges);
        if (!count) {
            af_mtl_bind_object_mesh(enc, frame, mesh, af_mtl_morph_draw_for(r, scene, draw->object_index));
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:mesh->index_count
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:mesh->indices.handle
                     indexBufferOffset:0];
            ++r->shadow_draw_calls;
            continue;
        }
        for (size_t j = 0; j < count; ++j) {
            af_mtl_cached_cluster_page *page = ranges[j].page;
            if (page) {
                af_mtl_use_cluster_page(r, page);
                ++*fine_draws;
            }
            const af_mtl_gpu_mesh *src = page ? &page->gpu : mesh;
            af_mtl_bind_object_mesh(enc, frame, src, NULL);
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                            indexCount:ranges[j].count
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:src->indices.handle
                     indexBufferOffset:(NSUInteger)ranges[j].first * sizeof(uint32_t)];
        }
        r->shadow_draw_calls += count;
    }
}

static uint64_t shadow_signature(af_mtl_renderer *r, af_mat4 matrix, af_shadow_settings settings, bool enabled,
                                 af_cluster_view view) {
    uint64_t h = af_r_hash(UINT64_C(14695981039346656037), &matrix, sizeof(matrix));
    h = af_r_hash(h, &settings.constant_bias, sizeof(float));
    h = af_r_hash(h, &settings.slope_bias, sizeof(float));
    h = af_r_hash(h, &r->mesh_revision, sizeof(r->mesh_revision));
    h = af_r_hash(h, &r->texture_revision, sizeof(r->texture_revision));
    h = af_r_hash(h, &enabled, sizeof(enabled));
    h = af_r_hash(h, &r->skin_revision, sizeof(r->skin_revision));
    if (enabled)
        for (size_t i = 0; i < r->shadow_count; ++i) {
            h = af_r_hash(h, &r->shadow_draws[i].model, sizeof(af_mat4));
            h = af_r_hash(h, &r->shadow_draws[i].mesh, sizeof(size_t));
            h = af_r_hash(h, &r->shadow_draws[i].alpha, sizeof(float));
            h = af_r_hash(h, &r->shadow_draws[i].cutoff, sizeof(float));
            h = af_r_hash(h, &r->shadow_draws[i].material_flags, sizeof(uint32_t));
            h = af_r_hash(h, &r->shadow_draws[i].ray_caster, sizeof(bool));
            h = af_r_hash(h, &r->shadow_draws[i].texture, sizeof(void *));
            const af_mtl_morph_draw *morph = af_mtl_morph_draw_for(r, r->morph_scene, r->shadow_draws[i].object_index);
            if (morph)
                h = af_r_hash(h, &morph->signature, sizeof(morph->signature));
            const af_mtl_shadow_draw *draw = &r->shadow_draws[i];
            if (r->meshes[draw->mesh].gpu.residency && !draw->ray_caster &&
                af_shadow_intersects(matrix, &r->shadow_bounds[i])) {
                af_mtl_cluster_index_range ranges[AF_CLUSTER_INDEX_RANGES];
                size_t count = shadow_cluster_ranges(r, draw, view, ranges);
                h = af_r_hash(h, &count, sizeof(count));
                for (size_t j = 0; j < count; ++j)
                    if (ranges[j].page)
                        h = af_r_hash(h, &ranges[j].page->generation, sizeof(ranges[j].page->generation));
            }
        }
    return h ? h : 1;
}

static bool prepare_temporal(af_mtl_renderer *r, const af_scene *scene, const af_camera *camera, af_rect area,
                             af_color clear, af_render_settings settings, bool temporal, bool jitter,
                             af_frame_uniform *uniform, uint64_t *local_light_signature) {
    uniform->unjittered_view_projection = uniform->view_projection;
    uniform->previous_view_projection = uniform->view_projection;
    r->temporal_jitter = (af_vec2){0};
    if (!temporal) {
        r->temporal_valid = false;
        return true;
    }
    uint64_t signature = af_rc_temporal_settings_hash(UINT64_C(14695981039346656037), settings);
    af_directional_light light = af_scene_get_light(scene);
    signature = af_r_hash(signature, &light, sizeof(light));
    signature = af_r_hash(signature, &clear, sizeof(clear));
    signature = af_r_hash(signature, &r->texture_revision, sizeof(r->texture_revision));
    signature = af_r_hash(signature, &r->mesh_revision, sizeof(r->mesh_revision));
    uint64_t *light_hash = local_light_signature ? local_light_signature : &signature;
    for (size_t i = 0; i < af_scene_local_light_count(scene); ++i)
        *light_hash = af_rc_temporal_light_hash(*light_hash, *af_scene_local_light_at(scene, i));
    bool cut = af_vec3_length(af_vec3_sub(camera->position, r->temporal_camera.position)) > 5 ||
               af_vec3_dot(af_camera_forward(camera), af_camera_forward(&r->temporal_camera)) < .7071f ||
               camera->vertical_fov != r->temporal_camera.vertical_fov ||
               camera->near_clip != r->temporal_camera.near_clip || camera->far_clip != r->temporal_camera.far_clip;
    if (!r->temporal_valid || r->temporal_scene != scene || memcmp(&area, &r->temporal_viewport, sizeof(area)) ||
        signature != r->temporal_signature || cut) {
        r->temporal_valid = false;
        r->temporal_frame = 0;
        ++r->temporal_resets;
        af_mtl_post_reset_temporal(r->swapchain.post);
    }
    r->temporal_scene = scene;
    r->temporal_camera = *camera;
    r->temporal_viewport = area;
    r->temporal_signature = signature;
    double ratio = fmax((double)r->swapchain.width / r->swapchain.render_width,
                        (double)r->swapchain.height / r->swapchain.render_height);
    unsigned phases = (unsigned)fmax(16, ceil(8 * ratio * ratio));
    unsigned phase = (unsigned)(r->temporal_frame % phases) + 1;
    int width = 1, height = 1;
    SDL_GetWindowSize(r->window, &width, &height);
    float pixels_x = area.width * (float)r->swapchain.render_width / (float)(width ? width : 1);
    float pixels_y = area.height * (float)r->swapchain.render_height / (float)(height ? height : 1);
    float jx = jitter ? af_rc_halton(phase, 2) - .5f : 0.f;
    float jy = jitter ? af_rc_halton(phase, 3) - .5f : 0.f;
    r->temporal_jitter =
        (af_vec2){jx / (float)r->swapchain.render_width, jy / (float)r->swapchain.render_height};
    for (unsigned c = 0; c < 4; ++c) {
        uniform->view_projection.elements[4 * c] += 2 * jx / pixels_x * uniform->view_projection.elements[4 * c + 3];
        uniform->view_projection.elements[4 * c + 1] +=
            2 * jy / pixels_y * uniform->view_projection.elements[4 * c + 3];
    }
    if (!af_mat4_inverse(uniform->view_projection, &uniform->inverse_view_projection))
        return false;
    uniform->previous_view_projection =
        r->temporal_valid ? r->previous_view_projection : uniform->unjittered_view_projection;
    uniform->sky_params.y = 1.0f;

    size_t count = af_scene_object_count(scene);
    size_t capacity = 16;
    while (capacity < count)
        capacity *= 2;
    if (r->motion_next_capacity < capacity) {
        af_motion_snapshot *next = realloc(r->motion_next, capacity * sizeof(*next));
        if (!next)
            return false;
        r->motion_next = next;
        r->motion_next_capacity = capacity;
    }
    r->motion_next_count = count;
    af_motion_record *records = r->motion_records;
    memset(records, 0, (count ? count : 1) * sizeof(*records));
    r->temporal_objects = r->temporal_skins = 0;
    for (size_t i = 0; i < count; ++i) {
        const af_scene_object *o = af_scene_object_at(scene, i);
        af_mat4 model = af_mat4_identity();
        bool visible = af_mtl_object_world(r, scene, i, &model) && af_scene_object_visible(scene, o->id);
        uint64_t hash = af_r_hash(UINT64_C(14695981039346656037), o->mesh, strlen(o->mesh));
        hash = af_rc_temporal_material_hash(hash, o->material);
        const af_mtl_morph_draw *morph = af_mtl_morph_draw_for(r, scene, i);
        if (morph)
            hash = af_r_hash(hash, &morph->signature, sizeof(morph->signature));
        r->motion_next[i] = (af_motion_snapshot){o->id, model, hash, visible};
        af_motion_snapshot key = {.id = o->id};
        const af_motion_snapshot *old =
            r->motion_count ? bsearch(&key, r->motion_previous, r->motion_count, sizeof(key), af_rc_motion_compare)
                            : NULL;
        bool valid = r->temporal_valid && old && old->visible && visible && old->signature == hash;
        records[i].model = valid ? old->model : model;
        size_t current_skin = 0;
        while (current_skin < r->skin_count && r->skins[current_skin].object != o->id)
            ++current_skin;
        size_t previous_skin = 0;
        while (previous_skin < r->previous_skin_count && r->previous_skins[previous_skin].object != o->id)
            ++previous_skin;
        if (current_skin < r->skin_count) {
            bool bones = previous_skin < r->previous_skin_count &&
                         r->previous_skins[previous_skin].count == r->skins[current_skin].count;
            valid &= bones;
            if (bones)
                records[i].info[0] = (uint32_t)(AF_SKIN_MATRICES + 1 + previous_skin * AF_ANIMATION_MAX_JOINTS);
        } else if (previous_skin < r->previous_skin_count) {
            valid = false;
        }
        records[i].info[1] = valid ? 1u : 0u;
        r->temporal_objects += valid;
        r->temporal_skins += valid && current_skin < r->skin_count;
    }
    if (count > 1)
        qsort(r->motion_next, count, sizeof(*r->motion_next), af_rc_motion_compare);
    return true;
}

static bool reserve_draw_records(af_mtl_renderer *r, af_mtl_frame *frame, size_t count) {
    if (!count)
        return true;
    size_t capacity = 16;
    while (capacity < count)
        capacity *= 2;
    if (r->motion_records_capacity < capacity) {
        af_motion_record *records = realloc(r->motion_records, capacity * sizeof(*records));
        if (!records)
            return false;
        r->motion_records = records;
        r->motion_records_capacity = capacity;
    }
    uint64_t bytes = (uint64_t)capacity * sizeof(af_motion_record);
    if (frame->motion_buffer.capacity >= bytes)
        return true;
    af_mtl_buffer replacement = {0};
    if (!af_mtl_create_buffer(r, NULL, bytes, &replacement))
        return false;
    af_mtl_destroy_buffer(&frame->motion_buffer);
    frame->motion_buffer = replacement;
    return true;
}

static void bind_scene_frame_resources(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame,
                                       bool ray_active, bool ray_reflections, bool cluster_indirect) {
    [enc setVertexBuffer:frame->uniform.handle offset:0 atIndex:AF_MTL_BUF_FRAME];
    [enc setVertexBuffer:frame->skin_buffer.handle offset:0 atIndex:AF_MTL_BUF_SKIN];
    [enc setVertexBuffer:frame->motion_buffer.handle offset:0 atIndex:AF_MTL_BUF_MOTION];

    [enc setFragmentBuffer:frame->uniform.handle offset:0 atIndex:AF_MTL_BUF_FRAME];
    [enc setFragmentBuffer:frame->light_buffer.handle offset:0 atIndex:AF_MTL_BUF_LIGHTS];
    [enc setFragmentBuffer:frame->light_tile_buffer.handle offset:0 atIndex:AF_MTL_BUF_LIGHT_TILES];

    af_mtl_texture *spec = NULL, *diff = NULL, *sky = NULL;
    af_mtl_active_environment_textures(r, &spec, &diff, &sky);

    [enc setFragmentTexture:af_mtl_shadows_texture(r->local_shadows, r->frame_index) atIndex:AF_MTL_TEX_LOCAL_SHADOW];
    [enc setFragmentSamplerState:af_mtl_shadows_sampler(r->local_shadows) atIndex:AF_MTL_TEX_LOCAL_SHADOW];

    [enc setFragmentTexture:af_mtl_texture_handle(spec) atIndex:AF_MTL_TEX_ENV];
    [enc setFragmentSamplerState:af_mtl_texture_sampler(spec) atIndex:AF_MTL_TEX_ENV];

    [enc setFragmentTexture:af_mtl_texture_handle(diff) atIndex:AF_MTL_TEX_IRRADIANCE];
    [enc setFragmentSamplerState:af_mtl_texture_sampler(diff) atIndex:AF_MTL_TEX_IRRADIANCE];

    [enc setFragmentTexture:af_mtl_texture_handle(r->brdf) atIndex:AF_MTL_TEX_BRDF];
    [enc setFragmentSamplerState:af_mtl_texture_sampler(r->brdf) atIndex:AF_MTL_TEX_BRDF];

    [enc setFragmentTexture:af_mtl_texture_handle(sky) atIndex:AF_MTL_TEX_SKY];
    [enc setFragmentSamplerState:af_mtl_texture_sampler(sky) atIndex:AF_MTL_TEX_SKY];

    [enc setFragmentTexture:af_mtl_refraction_color(r->swapchain.refraction, r->frame_index) atIndex:AF_MTL_TEX_REFRACT];
    [enc setFragmentSamplerState:af_mtl_refraction_color_sampler(r->swapchain.refraction) atIndex:AF_MTL_TEX_REFRACT];

    [enc setFragmentTexture:af_mtl_refraction_depth(r->swapchain.refraction, r->frame_index)
                    atIndex:AF_MTL_TEX_OPAQUE_DEPTH];
    [enc setFragmentSamplerState:af_mtl_refraction_depth_sampler(r->swapchain.refraction)
                         atIndex:AF_MTL_TEX_OPAQUE_DEPTH];

    [enc setFragmentTexture:af_mtl_shadows_texture(r->shadows, r->frame_index) atIndex:AF_MTL_TEX_SHADOW];
    [enc setFragmentSamplerState:af_mtl_shadows_sampler(r->shadows) atIndex:AF_MTL_TEX_SHADOW];

    if (ray_active && r->rays) {
        id<MTLAccelerationStructure> tlas = af_mtl_ray_query_tlas(r->rays, r->frame_index);
        id<MTLBuffer> surfaces = af_mtl_ray_query_surfaces(r->rays, r->frame_index);
        if (tlas)
            [enc setFragmentAccelerationStructure:tlas atBufferIndex:AF_MTL_BUF_ACCEL];
        if (surfaces)
            [enc setFragmentBuffer:surfaces offset:0 atIndex:AF_MTL_BUF_REFLECTION];
        af_mtl_ray_query_use_resources(r->rays, r->frame_index, enc, ray_reflections);
    }

    (void)cluster_indirect;
}

static void bind_scene_material(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, const af_material *material,
                                size_t object_count) {
    const char *names[] = {material->texture, material->normal_map, material->metallic_roughness_map};
    af_mtl_texture *textures[] = {r->white, r->flat_normal, r->white};
    const uint32_t slots[] = {AF_MTL_TEX_BASE, AF_MTL_TEX_NORMAL, AF_MTL_TEX_MR};
    for (unsigned i = 0; i < 3; ++i) {
        if (names[i][0]) {
            size_t t = 0;
            while (t < r->texture_count && (r->textures[t].normal != (i != 0) || strcmp(r->textures[t].name, names[i])))
                ++t;
            if (t < r->texture_count)
                textures[i] = r->textures[t].texture;
            else
                r->missing_textures += object_count;
        }
        [enc setFragmentTexture:af_mtl_texture_handle(textures[i]) atIndex:slots[i]];
        [enc setFragmentSamplerState:af_mtl_texture_sampler(textures[i]) atIndex:slots[i]];
    }
}

static bool prop_batch_matches(const af_mtl_prop_batch *batch, const af_mtl_gpu_mesh *mesh, const af_material *material,
                               const af_mesh_draw *draw) {
    return batch->mesh == mesh && !memcmp(&batch->draw.material, &draw->material, sizeof(draw->material)) &&
           !strcmp(batch->material->texture, material->texture) &&
           !strcmp(batch->material->normal_map, material->normal_map) &&
           !strcmp(batch->material->metallic_roughness_map, material->metallic_roughness_map) &&
           !strcmp(batch->material->shader, material->shader);
}

static void draw_prop_batch(af_mtl_renderer *r, id<MTLRenderCommandEncoder> enc, af_mtl_frame *frame,
                            af_mtl_prop_batch *batch, size_t *draws) {
    if (!batch->count)
        return;
    uint32_t first_instance = batch->first_object;
    if (batch->count > 1) {
        batch->draw.material.params.y += (float)AF_DRAW_INSTANCED;
        first_instance = batch->first_record;
    }
    [enc setRenderPipelineState:af_mtl_material_pipeline(r, batch->material->shader, false)];
    bind_scene_material(r, enc, batch->material, batch->count);
    [enc setVertexBytes:&batch->draw length:sizeof(batch->draw) atIndex:AF_MTL_BUF_PUSH];
    [enc setFragmentBytes:&batch->draw length:sizeof(batch->draw) atIndex:AF_MTL_BUF_PUSH];
    af_mtl_bind_object_mesh(enc, frame, batch->mesh, NULL);
    [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:batch->mesh->index_count
                     indexType:MTLIndexTypeUInt32
                   indexBuffer:batch->mesh->indices.handle
             indexBufferOffset:0
                 instanceCount:batch->count
                    baseVertex:0
                  baseInstance:first_instance];
    ++*draws;
    *batch = (af_mtl_prop_batch){0};
}

static bool bin_and_upload_local_lights(af_mtl_renderer *r, af_mtl_frame *frame, const af_camera *camera,
                                        af_frame_uniform *uniform) {
    uint32_t cols = (r->swapchain.render_width + AF_LIGHT_TILE_SIZE - 1u) / AF_LIGHT_TILE_SIZE;
    uint32_t rows = (r->swapchain.render_height + AF_LIGHT_TILE_SIZE - 1u) / AF_LIGHT_TILE_SIZE;
    if (cols == 0)
        cols = 1;
    if (rows == 0)
        rows = 1;
    size_t tile_count = (size_t)cols * rows;
    if (tile_count > r->light_tile_capacity) {
        af_light_tile_mask *grown = realloc(r->light_tile_staging, tile_count * sizeof(*grown));
        if (!grown)
            return false;
        r->light_tile_staging = grown;
        r->light_tile_capacity = tile_count;
    }
    r->light_tile_cols = cols;
    r->light_tile_rows = rows;
    uniform->light_tile_params = (af_vec4){(float)cols, (float)rows, (float)AF_LIGHT_TILE_SIZE, 0.0f};
    af_rc_bin_local_lights(r->local_light_staging, r->local_light_staging_count, camera, uniform,
                           r->swapchain.render_width, r->swapchain.render_height, cols, rows, r->light_tile_staging);
    if (r->local_light_staging_count > 0) {
        memcpy([frame->light_buffer.handle contents], r->local_light_staging,
               (size_t)r->local_light_staging_count * sizeof(af_gpu_light));
    }
    uint64_t tile_bytes = (uint64_t)tile_count * sizeof(af_light_tile_mask);
    if (frame->light_tile_buffer.capacity < tile_bytes) {
        uint64_t alloc_bytes = frame->light_tile_buffer.capacity ? frame->light_tile_buffer.capacity : 1024u;
        while (alloc_bytes < tile_bytes)
            alloc_bytes *= 2u;
        af_mtl_buffer replacement = {0};
        if (!af_mtl_create_buffer(r, NULL, alloc_bytes, &replacement))
            return false;
        af_mtl_destroy_buffer(&frame->light_tile_buffer);
        frame->light_tile_buffer = replacement;
    }
    memcpy([frame->light_tile_buffer.handle contents], r->light_tile_staging, (size_t)tile_bytes);
    return true;
}

static id<MTLRenderCommandEncoder> begin_scene_pass(af_mtl_renderer *r, id<MTLCommandBuffer> cmd, af_color clear_color,
                                                    bool load_existing) {
    uint32_t fi = r->frame_index;
    uint32_t cfg = r->swapchain.resource_config;
    bool has_motion = (cfg & AF_RESOURCE_MOTION) != 0 || (cfg & AF_RESOURCE_RAY_GI) != 0;
    bool has_gi = (cfg & AF_RESOURCE_RAY_GI) != 0;

    MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = af_mtl_post_scene_texture(r->swapchain.post, fi);
    rp.colorAttachments[0].loadAction = load_existing ? MTLLoadActionLoad : MTLLoadActionClear;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    rp.colorAttachments[0].clearColor = MTLClearColorMake(clear_color.r, clear_color.g, clear_color.b, clear_color.a);

    if (has_motion) {
        rp.colorAttachments[1].texture = af_mtl_post_motion_texture(r->swapchain.post, fi);
        rp.colorAttachments[1].loadAction = load_existing ? MTLLoadActionLoad : MTLLoadActionClear;
        rp.colorAttachments[1].storeAction = MTLStoreActionStore;
        rp.colorAttachments[1].clearColor = MTLClearColorMake(0.0, 0.0, -1.0, 65504.0);
    }
    if (has_gi) {
        rp.colorAttachments[2].texture = af_mtl_post_gi_texture(r->swapchain.post, fi);
        rp.colorAttachments[2].loadAction = load_existing ? MTLLoadActionLoad : MTLLoadActionClear;
        rp.colorAttachments[2].storeAction = MTLStoreActionStore;
        rp.colorAttachments[2].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);
    }
    rp.depthAttachment.texture = r->swapchain.depths[fi];
    rp.depthAttachment.loadAction = load_existing ? MTLLoadActionLoad : MTLLoadActionClear;
    rp.depthAttachment.storeAction = MTLStoreActionStore;
    rp.depthAttachment.clearDepth = 1.0;

    return [cmd renderCommandEncoderWithDescriptor:rp];
}

bool af_mtl_r_draw(af_mtl_renderer *r, af_color color, const af_scene *scene, const af_camera *camera,
                   af_rect viewport_rect, af_object_id highlighted, const af_overlay_frame *overlay, float frame_time_ms,
                   bool *rendered) {
    *rendered = false;
    r->missing_textures = 0;

    @autoreleasepool {
        af_render_settings env_settings = af_scene_get_render_settings(scene);
        bool upscale = r->upscaler && r->upscaler_enabled && !r->upscaler_failed && r->temporal_supported &&
                       r->debug_view == AF_DEBUG_SHADED && !(r->capture_pending && r->capture_motion);
        if (upscale)
            env_settings.anti_aliasing = AF_AA_OFF;
        if (r->debug_view != AF_DEBUG_SHADED)
            env_settings.anti_aliasing = AF_AA_TAA;
        if (env_settings.anti_aliasing == AF_AA_TAA && !r->temporal_supported)
            env_settings.anti_aliasing = AF_AA_FXAA;
        if (env_settings.motion_blur && !r->temporal_supported)
            env_settings.motion_blur = false;

        uint32_t resource_config =
            (uint32_t)env_settings.anti_aliasing | (env_settings.bloom ? AF_RESOURCE_BLOOM : 0u) |
            ((env_settings.anti_aliasing == AF_AA_TAA || env_settings.motion_blur || env_settings.depth_of_field ||
              (r->capture_pending && r->capture_motion))
                 ? AF_RESOURCE_MOTION
                 : 0u) |
            ((env_settings.refraction && af_scene_water_count(scene) > 0) ? AF_RESOURCE_REFRACTION : 0u);
        if (upscale)
            resource_config |= AF_RESOURCE_UPSCALER | AF_RESOURCE_MOTION;

        if (r->ray_supported && !r->ray_failed) {
            bool any_shadows = af_scene_get_shadows(scene).enabled;
            if (!any_shadows) {
                for (size_t i = 0; i < af_scene_local_light_count(scene); ++i) {
                    const af_local_light *local = af_scene_local_light_at(scene, i);
                    if (local->enabled && local->cast_shadows && local->intensity > 0) {
                        any_shadows = true;
                        break;
                    }
                }
            }
            if (env_settings.ray_traced_shadows && any_shadows)
                resource_config |= AF_RESOURCE_RAY_SHADOWS;
            if (env_settings.ray_traced_water_reflections) {
                for (size_t i = 0; i < af_scene_water_count(scene); ++i) {
                    const af_water *water = af_scene_water_at(scene, i);
                    if (water->enabled && water->mode != AF_WATER_PARTICLES &&
                        af_scene_object_visible(scene, water->owner)) {
                        resource_config |= AF_RESOURCE_RAY_REFLECTIONS;
                        break;
                    }
                }
            }
            if (env_settings.ray_traced_material_effects)
                resource_config |= AF_RESOURCE_RAY_MATERIALS;
            if (env_settings.ray_traced_gi) {
                resource_config |= AF_RESOURCE_RAY_GI | AF_RESOURCE_MOTION;
            }
        }

        if (r->resize_pending || !r->swapchain.post || r->swapchain.resource_config != resource_config) {
            if (!af_mtl_create_swapchain(r, env_settings, resource_config)) {
                if (upscale) {
                    af_log("warning", "MetalFX upscaler resources unavailable. Restoring built-in rendering.");
                    r->upscaler_failed = true;
                    r->resize_pending = r->swapchain_invalid = true;
                    return true;
                }
                if (resource_config & AF_RESOURCE_RAY_QUERY) {
                    af_log("warning", "Optional Metal ray-query pipeline unavailable; retrying raster rendering.");
                    r->ray_failed = true;
                    r->resize_pending = r->swapchain_invalid = true;
                    return true;
                }
                return false;
            }
            if (r->resize_pending || !r->swapchain.post || r->swapchain.resource_config != resource_config)
                return true;
        }

        if (!(resource_config & AF_RESOURCE_RAY_QUERY) && r->rays) {
            if (!af_mtl_r_wait_idle(r))
                return false;
            af_mtl_ray_query_destroy(r->rays);
            r->rays = NULL;
            r->ray_raster_casters = 0;
        }

        if ((env_settings.environment_enabled || env_settings.skybox) && !env_settings.environment_asset[0] &&
            env_settings.environment_profile != r->environment_profile &&
            !af_mtl_create_environment(r, env_settings.environment_profile))
            return false;

        bool env_found = !env_settings.environment_asset[0];
        for (size_t i = 0; i < r->environment_count; ++i)
            if (!strcmp(r->environments[i].name, env_settings.environment_asset))
                env_found = true;
        if (!env_found && (env_settings.environment_enabled || env_settings.skybox))
            ++r->missing_textures;
        if (strcmp(r->active_environment, env_settings.environment_asset)) {
            if (!af_mtl_r_wait_idle(r))
                return false;
            snprintf(r->active_environment, sizeof(r->active_environment), "%s", env_settings.environment_asset);
        }

        af_frame_uniform uniform;
        af_shadow_view shadow_view;
        af_rect resolved_vp = af_rc_resolve_viewport(r->window, viewport_rect);
        if (!af_mtl_prepare_morphs(r, scene) || !prepare_shadows(r, scene, camera, resolved_vp, &uniform, &shadow_view))
            return false;

        af_mtl_frame *frame = &r->frames[r->frame_index];
        Uint64 wait_start = SDL_GetPerformanceCounter();
        dispatch_semaphore_wait(r->frame_semaphore, DISPATCH_TIME_FOREVER);
        r->frame_semaphore_held = true;
        r->cpu_fence_ms =
            (double)(SDL_GetPerformanceCounter() - wait_start) * 1000.0 / (double)SDL_GetPerformanceFrequency();

#define ABORT_FRAME(ret_val)                                                                                           \
    do {                                                                                                               \
        if (r->frame_semaphore_held) {                                                                                 \
            r->frame_semaphore_held = false;                                                                           \
            dispatch_semaphore_signal(r->frame_semaphore);                                                             \
        }                                                                                                              \
        return (ret_val);                                                                                              \
    } while (0)

        if (frame->timing_ready) {
            r->gpu_ms[0] = frame->gpu_total_ms;
            r->gpu_ms[1] = 0.0;
            r->gpu_ms[2] = frame->gpu_total_ms;
            r->gpu_ms[3] = 0.0;
            r->gpu_ms[4] = 0.0;
            r->timing_valid = true;
            frame->timing_ready = false;
        }

        af_cluster_view cluster_camera = af_rc_cluster_view(&uniform, camera);
        r->cluster_stats = (af_cluster_stats){0};
        af_mtl_update_cluster_pages(r);
        if (!af_mtl_stream_prepare(r))
            ABORT_FRAME(false);
        if (r->morph_vertex_count &&
            !af_mtl_stream_buffer(r, &frame->morph_buffer, r->morph_vertices, r->morph_vertex_count * sizeof(af_vertex)))
            ABORT_FRAME(false);

        bool ray_shadows = (resource_config & AF_RESOURCE_RAY_SHADOWS) != 0;
        bool ray_reflections = (resource_config & AF_RESOURCE_RAY_REFLECTIONS) != 0;
        bool ray_gi = (resource_config & AF_RESOURCE_RAY_GI) != 0;
        bool ray_material_effects = (resource_config & AF_RESOURCE_RAY_MATERIALS) != 0;
        uniform.sky_params.z = ray_shadows ? 1.f : 0.f;
        uniform.sky_params.w = ray_reflections ? 1.f : 0.f;
        af_directional_light sun = af_scene_get_light(scene);
        uniform.ray_params = (af_vec4){ray_material_effects && !r->ray_failed ? 1.0f : 0.0f, sun.angular_radius, 4.0f,
                                       (float)env_settings.rtgi_bounces};
        uniform.rtgi_params = (af_vec4){ray_gi && !r->ray_failed ? 1.0f : 0.0f, env_settings.rtgi_intensity,
                                        env_settings.rtgi_ray_distance, (float)env_settings.rtgi_samples};
        uniform.rtgi_flags = (af_vec4){env_settings.rtgi_emissive ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};

        uint64_t emitter_signature = UINT64_C(14695981039346656037);
        if ((ray_shadows || ray_reflections || ray_gi || ray_material_effects) &&
            !prepare_ray_scene(r, scene, ray_shadows, ray_reflections, ray_gi, ray_material_effects, cluster_camera,
                               ray_gi && env_settings.rtgi_emissive ? &emitter_signature : NULL)) {
            af_log("warning", "Metal ray-query preparation failed; using raster and environment fallbacks.");
            r->ray_failed = true;
            r->cluster_stats.ray_fine_instances = 0;
            r->resize_pending = true;
            ABORT_FRAME(true);
        }

        /* Upload skin matrices. */
        af_mat4 *skin_dst = (af_mat4 *)[frame->skin_buffer.handle contents];
        skin_dst[0] = af_mat4_identity();
        for (size_t i = 0; i < r->skin_count; ++i)
            for (unsigned j = 0; j < AF_ANIMATION_MAX_JOINTS; ++j)
                skin_dst[1 + i * AF_ANIMATION_MAX_JOINTS + j] =
                    j < r->skins[i].count ? r->skins[i].matrices[j] : af_mat4_identity();
        skin_dst[AF_SKIN_MATRICES] = af_mat4_identity();
        for (size_t i = 0; i < r->previous_skin_count; ++i)
            for (unsigned j = 0; j < AF_ANIMATION_MAX_JOINTS; ++j)
                skin_dst[AF_SKIN_MATRICES + 1 + i * AF_ANIMATION_MAX_JOINTS + j] =
                    j < r->previous_skins[i].count ? r->previous_skins[i].matrices[j] : af_mat4_identity();

        /* Acquire next drawable. */
        wait_start = SDL_GetPerformanceCounter();
        id<CAMetalDrawable> drawable = [r->layer nextDrawable];
        r->cpu_acquire_ms =
            (double)(SDL_GetPerformanceCounter() - wait_start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
        if (!drawable || !drawable.texture) {
            r->resize_pending = true;
            ABORT_FRAME(true);
        }
        if ((uint32_t)drawable.texture.width != r->swapchain.width ||
            (uint32_t)drawable.texture.height != r->swapchain.height) {
            r->resize_pending = r->swapchain_invalid = true;
            ABORT_FRAME(true);
        }

        bool temporal =
            r->temporal_supported && (upscale || env_settings.anti_aliasing == AF_AA_TAA || env_settings.motion_blur ||
                                      (resource_config & AF_RESOURCE_RAY_GI) != 0 ||
                                      (r->capture_pending && r->capture_motion));
        bool spatial = upscale && af_mtl_upscaler_is_spatial(r->upscaler);
        bool jitter = (upscale && !spatial) || env_settings.anti_aliasing == AF_AA_TAA;
        size_t object_count = af_scene_object_count(scene);
        if (temporal && object_count > AF_SCENE_FILE_MAX_OBJECTS) {
            af_log("error", "Temporal rendering supports at most %u objects.", AF_SCENE_FILE_MAX_OBJECTS);
            ABORT_FRAME(false);
        }
        size_t record_count = temporal ? (object_count ? object_count : 1) : 0;
        bool batch_props =
            !r->prop_instancing_disabled && object_count > 1 && object_count <= AF_SCENE_FILE_MAX_OBJECTS;
        if (batch_props && !reserve_draw_records(r, frame, record_count + object_count))
            batch_props = false;
        if (!batch_props && !reserve_draw_records(r, frame, record_count))
            ABORT_FRAME(false);

        uint64_t local_light_signature = UINT64_C(14695981039346656037);
        if (!prepare_temporal(r, scene, camera, resolved_vp, color, env_settings, temporal, jitter, &uniform,
                              ray_gi ? &local_light_signature : NULL))
            ABORT_FRAME(false);

        bool gi_lighting_changed = ray_gi && emitter_signature != r->gi_emitter_signature;
        bool color_history_changed =
            gi_lighting_changed || (ray_gi && local_light_signature != r->temporal_local_light_signature);
        if (color_history_changed)
            af_mtl_post_invalidate_lighting_history(r->swapchain.post, gi_lighting_changed);

        af_vec2 gi_jitter_delta = {0};
        if (r->temporal_valid) {
            gi_jitter_delta.x = r->previous_temporal_jitter.x - r->temporal_jitter.x;
            gi_jitter_delta.y = r->previous_temporal_jitter.y - r->temporal_jitter.y;
        }

        if (r->ui)
            af_mtl_ui_process_uploads(r->ui, r->frame_index);
        if (overlay->index_count && (!r->ui || !af_mtl_ui_prepare(r->ui, r->frame_index, overlay)))
            ABORT_FRAME(false);
        if (!bin_and_upload_local_lights(r, frame, camera, &uniform))
            ABORT_FRAME(false);

        memcpy([frame->uniform.handle contents], &uniform, sizeof(uniform));

        Uint64 record_start = SDL_GetPerformanceCounter();
        id<MTLCommandBuffer> cmd = [r->queue commandBuffer];
        if (!cmd)
            ABORT_FRAME(false);

        /* 1. Hardware ray tracing AS builds. */
        if (resource_config & AF_RESOURCE_RAY_QUERY)
            af_mtl_ray_query_record(r->rays, r->frame_index, cmd);

        /* 2. Directional & local shadow passes. */
        r->shadow_draw_calls = 0;
        r->shadow_passes = r->shadow_cached_passes = 0;
        for (uint32_t layer = 0; layer < shadow_view.count; ++layer) {
            uint64_t signature = shadow_signature(r, shadow_view.matrices[layer], af_scene_get_shadows(scene),
                                                  uniform.shadow_params.x > .5f, cluster_camera);
            signature = af_r_hash(signature, &resource_config, sizeof(resource_config));
            if (frame->shadow_signatures[layer] == signature) {
                ++r->shadow_cached_passes;
                continue;
            }
            frame->shadow_signatures[layer] = signature;
            ++r->shadow_passes;
            id<MTLRenderCommandEncoder> senc = af_mtl_shadows_begin(
                r->shadows, cmd, r->frame_index, layer, af_scene_get_shadows(scene), frame->skin_buffer.handle);
            if (senc) {
                if (uniform.shadow_params.x > .5f)
                    draw_shadow_casters(r, senc, frame, scene, shadow_view.matrices[layer], cluster_camera,
                                        &r->cluster_stats.shadow_fine_draws);
                [senc endEncoding];
            }
        }
        for (uint32_t layer = 0; layer < r->local_shadow_layers; ++layer) {
            af_shadow_settings bias = af_scene_get_shadows(scene);
            bias.constant_bias = 1.0f;
            bias.slope_bias = 1.5f;
            uint64_t signature = shadow_signature(r, uniform.local_shadow_matrices[layer], bias,
                                                  layer < (uint32_t)uniform.light_count.z, cluster_camera);
            if (frame->local_shadow_signatures[layer] == signature) {
                ++r->shadow_cached_passes;
                continue;
            }
            frame->local_shadow_signatures[layer] = signature;
            ++r->shadow_passes;
            id<MTLRenderCommandEncoder> lenc = af_mtl_shadows_begin(r->local_shadows, cmd, r->frame_index, layer, bias,
                                                                    frame->skin_buffer.handle);
            if (lenc) {
                if (layer < (uint32_t)uniform.light_count.z)
                    draw_shadow_casters(r, lenc, frame, scene, uniform.local_shadow_matrices[layer], cluster_camera,
                                        &r->cluster_stats.local_shadow_fine_draws);
                [lenc endEncoding];
            }
        }

        /* 3. GPU cluster culling compute pass (if applicable). */
        af_mat4 visibility_projection = cluster_camera.projection;
        float cluster_pixel_scale = cluster_camera.pixel_scale;
        size_t draw_calls = 0, missing_meshes = 0;
        r->visible_objects = r->culled_objects = 0;
        bool cluster_indirect = af_mtl_prepare_cluster_commands(r, frame, cmd, scene, visibility_projection,
                                                                cluster_pixel_scale, object_count);

        /* 4. Main HDR scene render pass. */
        bool ray_active = (resource_config & AF_RESOURCE_RAY_QUERY) != 0;
        bool ray_needs_surfaces = ray_reflections || ray_gi || ray_material_effects;
        id<MTLRenderCommandEncoder> enc = begin_scene_pass(r, cmd, color, false);
        bool gi_denoised = false;

        const af_landscape *scene_ls = scene ? af_scene_get_landscape(scene) : NULL;
        bool has_landscape = scene_ls && scene_ls->enabled && scene_ls->chunk_count > 0;

        int logical_w = 0, logical_h = 0;
        SDL_GetWindowSize(r->window, &logical_w, &logical_h);
        if (logical_w <= 0)
            logical_w = 1;
        if (logical_h <= 0)
            logical_h = 1;
        af_rect post_area = resolved_vp;
        post_area.x /= (float)logical_w;
        post_area.y /= (float)logical_h;
        post_area.width /= (float)logical_w;
        post_area.height /= (float)logical_h;

        if (enc && (object_count > 0 || env_settings.skybox || has_landscape)) {
            float sx = (float)r->swapchain.render_width / (float)logical_w;
            float sy = (float)r->swapchain.render_height / (float)logical_h;
            MTLViewport vp = {(double)(resolved_vp.x * sx),      (double)(resolved_vp.y * sy),
                              (double)(resolved_vp.width * sx),  (double)(resolved_vp.height * sy),
                              0.0,                               1.0};
            NSUInteger sc_x = (NSUInteger)fmaxf(0.0f, resolved_vp.x * sx);
            NSUInteger sc_y = (NSUInteger)fmaxf(0.0f, resolved_vp.y * sy);
            NSUInteger sc_w = (NSUInteger)fmaxf(1.0f, resolved_vp.width * sx);
            NSUInteger sc_h = (NSUInteger)fmaxf(1.0f, resolved_vp.height * sy);
            if (sc_x + sc_w > r->swapchain.render_width)
                sc_w = r->swapchain.render_width > sc_x ? r->swapchain.render_width - sc_x : 0;
            if (sc_y + sc_h > r->swapchain.render_height)
                sc_h = r->swapchain.render_height > sc_y ? r->swapchain.render_height - sc_y : 0;
            MTLScissorRect sc = {sc_x, sc_y, sc_w, sc_h};

            [enc setViewport:vp];
            [enc setScissorRect:sc];
            [enc setCullMode:MTLCullModeNone];
            [enc setFrontFacingWinding:MTLWindingCounterClockwise];
            bind_scene_frame_resources(r, enc, frame, ray_active, ray_needs_surfaces, cluster_indirect);

            if (env_settings.skybox) {
                [enc setRenderPipelineState:r->swapchain.sky_pipeline];
                [enc setDepthStencilState:r->depth_none];
                [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            }

            af_blend_draw *blended = object_count ? malloc(object_count * sizeof(*blended)) : NULL;
            size_t blend_count = 0;
            if (object_count && !blended) {
                [enc endEncoding];
                ABORT_FRAME(false);
            }
            for (size_t i = 0; i < object_count; ++i) {
                const af_scene_object *o = af_scene_object_at(scene, i);
                if (o->material.alpha_mode != AF_ALPHA_BLEND && !af_scene_get_water(scene, o->id))
                    continue;
                af_mat4 world;
                if (!af_mtl_object_world(r, scene, i, &world))
                    continue;
                af_vec3 pos = {world.elements[12], world.elements[13], world.elements[14]};
                blended[blend_count++] =
                    (af_blend_draw){i, af_vec3_dot(af_vec3_sub(pos, camera->position), af_camera_forward(camera))};
            }
            if (blend_count > 1)
                qsort(blended, blend_count, sizeof(*blended), af_rc_blend_compare);
            bool refract = env_settings.refraction && env_settings.refraction_strength > 0 &&
                           af_scene_water_count(scene) > 0;

            for (unsigned alpha_pass = 0; alpha_pass < 2; ++alpha_pass) {
                if (alpha_pass) {
                    if (blend_count == 0)
                        break;
                    if (((r->swapchain.resource_config & AF_RESOURCE_RAY_GI) && !gi_denoised) || refract) {
                        [enc endEncoding];
                        if ((r->swapchain.resource_config & AF_RESOURCE_RAY_GI) && !gi_denoised) {
                            af_mtl_post_denoise_gi(r->swapchain.post, cmd, r->frame_index, env_settings, post_area,
                                                   gi_jitter_delta);
                            gi_denoised = true;
                        }
                        if (refract) {
                            af_mtl_refraction_copy(r->swapchain.refraction, cmd, r->frame_index,
                                                   af_mtl_post_scene_texture(r->swapchain.post, r->frame_index),
                                                   r->swapchain.depths[r->frame_index]);
                        }
                        enc = begin_scene_pass(r, cmd, color, true);
                        if (!enc) {
                            free(blended);
                            ABORT_FRAME(false);
                        }
                        [enc setViewport:vp];
                        [enc setScissorRect:sc];
                        [enc setCullMode:MTLCullModeNone];
                        [enc setFrontFacingWinding:MTLWindingCounterClockwise];
                        bind_scene_frame_resources(r, enc, frame, ray_active, ray_needs_surfaces, false);
                    }
                }

                [enc setDepthStencilState:alpha_pass ? r->depth_ro : r->depth_rw];
                [enc setRenderPipelineState:alpha_pass ? r->swapchain.blend_pipeline : r->swapchain.pipeline];

                af_mtl_prop_batch batch = {0};
                for (size_t order = 0; order < (alpha_pass ? blend_count : object_count); ++order) {
                    size_t i = alpha_pass ? blended[order].index : order;
                    const af_scene_object *object = af_scene_object_at(scene, i);
                    const af_water *water = af_scene_get_water(scene, object->id);
                    if (water && !water->enabled)
                        continue;
                    if (!alpha_pass && water)
                        continue;
                    if (!af_scene_object_visible(scene, object->id) || (!water && !strcmp(object->mesh, "__node")))
                        continue;
                    char water_mesh[64];
                    if (water)
                        af_r_water_name(object->id, water_mesh);
                    size_t index = af_mtl_find_mesh_index(r, water ? water_mesh : object->mesh);
                    if (index == r->mesh_count) {
                        if (alpha_pass || object->material.alpha_mode != AF_ALPHA_BLEND)
                            ++missing_meshes;
                        continue;
                    }
                    const af_mtl_mesh_asset *asset = &r->meshes[index];
                    const af_mtl_gpu_mesh *mesh = asset->dynamic ? &asset->streams[r->frame_index].gpu : &asset->gpu;
                    bool blend = object->material.alpha_mode == AF_ALPHA_BLEND;
                    bool split_blend = blend && mesh->sidedness != AF_MESH_SIDES_DEFAULT;
                    if (!alpha_pass && blend && !split_blend)
                        continue;
                    bool count_object = alpha_pass || !split_blend;
                    const af_mtl_morph_draw *morph = af_mtl_morph_draw_for(r, scene, i);
                    af_mat4 world;
                    if (!af_mtl_object_world(r, scene, i, &world))
                        continue;
                    if (!r->culling_disabled) {
                        af_shadow_bounds bounds = af_mtl_object_bounds(r, object->id, world, mesh, morph);
                        if (!af_shadow_intersects(visibility_projection, &bounds)) {
                            if (count_object)
                                ++r->culled_objects;
                            continue;
                        }
                    }
                    if (count_object)
                        ++r->visible_objects;

                    uint32_t draw_flags = af_rc_mesh_face_flags(mesh->sidedness, world);
                    if (object->material.receive_shadows)
                        draw_flags |= AF_DRAW_RECEIVE_SHADOWS;
                    if (split_blend)
                        draw_flags |= alpha_pass ? AF_DRAW_BLEND_PARTIAL : AF_DRAW_BLEND_OPAQUE;
                    af_color base = object->material.base_color;
                    if (object->id == highlighted) {
                        base.r *= 1.25f;
                        base.g *= 1.1f;
                        base.b *= 0.75f;
                    }
                    af_mesh_draw transform = {
                        .model = world,
                        .material = {
                            .tint = base,
                            .params = {(float)((object->material.unlit ? 1u : 0u) |
                                               ((uint32_t)object->material.alpha_mode << 1u) |
                                               ((uint32_t)lroundf(object->material.alpha_cutoff * 65535) << 3u) |
                                               (object->material.normal_map[0] ? (1u << 23u) : 0u)),
                                       (float)draw_flags, object->material.physically_based ? 1.0f : 0.0f,
                                       object->material.ior},
                            .surface = {object->material.roughness, object->material.metallic,
                                        object->material.normal_strength,
                                        (float)af_mtl_skin_base(r, object->id, mesh)},
                            .emission = {object->material.emission.x, object->material.emission.y,
                                         object->material.emission.z, object->material.emission_strength},
                        }};

                    bool clustered = !r->clusters_disabled && !alpha_pass && !water && !morph && mesh->cluster_count &&
                                     object->material.alpha_mode == AF_ALPHA_OPAQUE;
                    bool can_batch = !clustered && batch_props && !alpha_pass && !asset->dynamic && !mesh->skinned &&
                                     !morph && object->material.alpha_mode == AF_ALPHA_OPAQUE &&
                                     object->id != highlighted;
                    if (can_batch) {
                        if (!prop_batch_matches(&batch, mesh, &object->material, &transform)) {
                            draw_prop_batch(r, enc, frame, &batch, &draw_calls);
                            batch = (af_mtl_prop_batch){mesh, &object->material, transform, (uint32_t)i, 0, 1};
                        } else {
                            if (batch.count == 1) {
                                batch.first_record = (uint32_t)record_count;
                                r->motion_records[record_count++] =
                                    (af_motion_record){batch.draw.model, {batch.first_object}};
                            }
                            r->motion_records[record_count++] = (af_motion_record){world, {(uint32_t)i}};
                            ++batch.count;
                        }
                        continue;
                    }
                    draw_prop_batch(r, enc, frame, &batch, &draw_calls);

                    if (water) {
                        transform.material.tint =
                            (af_color){water->scattering.x, water->scattering.y, water->scattering.z, 1};
                        transform.material.params =
                            (af_vec4){(float)((1u << 19u) | (1u << 21u) | (2u << 1u)), 1, 1, water->wind};
                        if (water->mode != AF_WATER_PARTICLES)
                            transform.material.params.x += (float)(1u << 18u);
                        if (refract)
                            transform.material.params.x += (float)(1u << 22u);
                        if (water->mode != AF_WATER_PARTICLES &&
                            wa_contains(ph_water_runtime(r->water_physics), scene, water->owner, camera->position))
                            transform.material.params.x += (float)(1u << 23u);
                        transform.material.surface =
                            (af_vec4){water->roughness, water->ripple_scale,
                                      water->mode == AF_WATER_PARTICLES ? water->particle_spacing : water->depth,
                                      water->mode == AF_WATER_PARTICLES ? 0 : water->ripple_strength};
                        transform.material.emission =
                            (af_vec4){water->absorption.x, water->absorption.y, water->absorption.z, water->ior};
                    }

                    bind_scene_material(r, enc, &object->material, count_object ? 1 : 0);
                    af_mtl_bind_object_mesh(enc, frame, mesh, morph);

                    if (water) {
                        [enc setDepthBias:4.0f slopeScale:1.0f clamp:0.0f];
                        [enc setRenderPipelineState:r->swapchain.water_attenuation];
                        transform.material.params.x += (float)(1u << 20u);
                        [enc setVertexBytes:&transform length:sizeof(transform) atIndex:AF_MTL_BUF_PUSH];
                        [enc setFragmentBytes:&transform length:sizeof(transform) atIndex:AF_MTL_BUF_PUSH];
                        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:mesh->index_count
                                         indexType:MTLIndexTypeUInt32
                                       indexBuffer:mesh->indices.handle
                                 indexBufferOffset:0
                                     instanceCount:1
                                        baseVertex:0
                                      baseInstance:(NSUInteger)i];
                        ++draw_calls;
                        transform.material.params.x -= (float)(1u << 20u);
                        [enc setRenderPipelineState:r->swapchain.water_scattering];
                        [enc setVertexBytes:&transform length:sizeof(transform) atIndex:AF_MTL_BUF_PUSH];
                        [enc setFragmentBytes:&transform length:sizeof(transform) atIndex:AF_MTL_BUF_PUSH];
                        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:mesh->index_count
                                         indexType:MTLIndexTypeUInt32
                                       indexBuffer:mesh->indices.handle
                                 indexBufferOffset:0
                                     instanceCount:1
                                        baseVertex:0
                                      baseInstance:(NSUInteger)i];
                        ++draw_calls;
                        [enc setDepthBias:0.0f slopeScale:0.0f clamp:0.0f];
                        continue;
                    }

                    [enc setRenderPipelineState:af_mtl_material_pipeline(r, object->material.shader, alpha_pass != 0)];
                    [enc setVertexBytes:&transform length:sizeof(transform) atIndex:AF_MTL_BUF_PUSH];
                    [enc setFragmentBytes:&transform length:sizeof(transform) atIndex:AF_MTL_BUF_PUSH];

                    if (clustered && cluster_indirect && r->cluster_spans[i].count) {
                        draw_calls += af_mtl_draw_indirect_clusters(r, enc, frame, mesh, r->cluster_spans[i]);
                    } else if (clustered) {
                        draw_calls += af_mtl_draw_clusters(r, enc, frame, mesh, transform, visibility_projection,
                                                           cluster_pixel_scale, (uint32_t)i);
                    } else {
                        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:mesh->index_count
                                         indexType:MTLIndexTypeUInt32
                                       indexBuffer:mesh->indices.handle
                                 indexBufferOffset:0
                                     instanceCount:1
                                        baseVertex:0
                                      baseInstance:(NSUInteger)i];
                        ++draw_calls;
                    }
                }
                draw_prop_batch(r, enc, frame, &batch, &draw_calls);

                /* Landscape terrain chunks (opaque pass). */
                if (alpha_pass == 0 && scene_ls && scene_ls->enabled) {
                    const af_landscape *ls = scene_ls;
                    [enc setRenderPipelineState:r->swapchain.pipeline];
                    [enc setDepthStencilState:r->depth_rw];
                    for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
                        const af_terrain_chunk *chunk = &ls->chunks[c_idx];
                        size_t index = af_mtl_find_mesh_index(r, chunk->mesh_name);
                        if (index == r->mesh_count)
                            continue;
                        const af_mtl_mesh_asset *asset = &r->meshes[index];
                        const af_mtl_gpu_mesh *mesh = asset->dynamic ? &asset->streams[r->frame_index].gpu : &asset->gpu;
                        if (!mesh->vertices.handle || !mesh->indices.handle)
                            continue;
                        if (!r->culling_disabled) {
                            af_shadow_bounds bounds =
                                af_shadow_transform_bounds(af_mat4_identity(), chunk->bounds_min, chunk->bounds_max);
                            if (!af_shadow_intersects(visibility_projection, &bounds)) {
                                ++r->culled_objects;
                                continue;
                            }
                        }
                        ++r->visible_objects;

                        uint64_t sum_weights[AF_LANDSCAPE_MAX_LAYERS] = {0};
                        for (size_t v_i = 0; v_i < AF_LANDSCAPE_CHUNK_VERTICES * AF_LANDSCAPE_CHUNK_VERTICES; ++v_i) {
                            sum_weights[0] += chunk->weights[v_i * 4 + 0];
                            sum_weights[1] += chunk->weights[v_i * 4 + 1];
                            sum_weights[2] += chunk->weights[v_i * 4 + 2];
                            sum_weights[3] += chunk->weights[v_i * 4 + 3];
                        }
                        size_t dominant = 0;
                        for (size_t l_i = 1; l_i < AF_LANDSCAPE_MAX_LAYERS; ++l_i)
                            if (sum_weights[l_i] > sum_weights[dominant])
                                dominant = l_i;

                        af_mtl_texture *diffuse_tex = r->white;
                        if (ls->layers[dominant].diffuse_texture[0] != '\0') {
                            for (size_t t = 0; t < r->texture_count; ++t)
                                if (!r->textures[t].normal &&
                                    !strcmp(r->textures[t].name, ls->layers[dominant].diffuse_texture)) {
                                    diffuse_tex = r->textures[t].texture;
                                    break;
                                }
                        }
                        af_mtl_texture *normal_tex = r->flat_normal;
                        bool has_normal = false;
                        if (ls->layers[dominant].normal_texture[0] != '\0') {
                            for (size_t t = 0; t < r->texture_count; ++t)
                                if (r->textures[t].normal &&
                                    !strcmp(r->textures[t].name, ls->layers[dominant].normal_texture)) {
                                    normal_tex = r->textures[t].texture;
                                    has_normal = true;
                                    break;
                                }
                        }

                        af_mesh_draw chunk_transform = {
                            .model = af_mat4_identity(),
                            .material = {
                                .tint = {1.0f, 1.0f, 1.0f, 1.0f},
                                .params = {(float)(((uint32_t)AF_ALPHA_OPAQUE << 1u) | (has_normal ? (1u << 23u) : 0u)),
                                           1.0f, 1.0f, 1.45f},
                                .surface = {ls->layers[dominant].roughness, ls->layers[dominant].metalness, 1.0f, 0.0f},
                                .emission = {0.0f, 0.0f, 0.0f, 0.0f}}};
                        [enc setFragmentTexture:af_mtl_texture_handle(diffuse_tex) atIndex:AF_MTL_TEX_BASE];
                        [enc setFragmentSamplerState:af_mtl_texture_sampler(diffuse_tex) atIndex:AF_MTL_TEX_BASE];
                        [enc setFragmentTexture:af_mtl_texture_handle(normal_tex) atIndex:AF_MTL_TEX_NORMAL];
                        [enc setFragmentSamplerState:af_mtl_texture_sampler(normal_tex) atIndex:AF_MTL_TEX_NORMAL];
                        [enc setFragmentTexture:af_mtl_texture_handle(r->white) atIndex:AF_MTL_TEX_MR];
                        [enc setFragmentSamplerState:af_mtl_texture_sampler(r->white) atIndex:AF_MTL_TEX_MR];
                        [enc setVertexBytes:&chunk_transform length:sizeof(chunk_transform) atIndex:AF_MTL_BUF_PUSH];
                        [enc setFragmentBytes:&chunk_transform length:sizeof(chunk_transform) atIndex:AF_MTL_BUF_PUSH];
                        [enc setVertexBuffer:mesh->vertices.handle offset:0 atIndex:AF_MTL_VERTEX_BUFFER_INDEX];
                        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                        indexCount:mesh->index_count
                                         indexType:MTLIndexTypeUInt32
                                       indexBuffer:mesh->indices.handle
                                 indexBufferOffset:0];
                        ++draw_calls;
                    }
                }

                /* Landscape foliage instances. */
                if (scene_ls && scene_ls->enabled) {
                    const af_landscape *ls = scene_ls;
                    for (size_t l = 0; l < ls->foliage_layer_count; ++l) {
                        const af_foliage_layer *fl = &ls->foliage_layers[l];
                        if (!fl->visible || fl->total_instances == 0 || fl->mesh[0] == '\0')
                            continue;
                        bool is_blend = fl->material.alpha_mode == AF_ALPHA_BLEND;
                        if ((alpha_pass == 0 && is_blend) || (alpha_pass == 1 && !is_blend))
                            continue;
                        size_t index = af_mtl_find_mesh_index(r, fl->mesh);
                        if (index == r->mesh_count) {
                            ++missing_meshes;
                            continue;
                        }
                        const af_mtl_mesh_asset *asset = &r->meshes[index];
                        const af_mtl_gpu_mesh *mesh = asset->dynamic ? &asset->streams[r->frame_index].gpu : &asset->gpu;
                        if (!mesh->vertices.handle || !mesh->indices.handle)
                            continue;
                        [enc setRenderPipelineState:af_mtl_material_pipeline(r, fl->material.shader, alpha_pass != 0)];
                        [enc setVertexBuffer:mesh->vertices.handle offset:0 atIndex:AF_MTL_VERTEX_BUFFER_INDEX];
                        bind_scene_material(r, enc, &fl->material, 0);
                        float max_dist_sq = fl->max_distance * fl->max_distance;
                        for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
                            const af_terrain_chunk *chunk = &ls->chunks[c_idx];
                            const af_foliage_chunk *fc = &chunk->foliage[l];
                            if (fc->count == 0)
                                continue;
                            af_vec3 center = {(chunk->bounds_min.x + chunk->bounds_max.x) * 0.5f,
                                              (chunk->bounds_min.y + chunk->bounds_max.y) * 0.5f,
                                              (chunk->bounds_min.z + chunk->bounds_max.z) * 0.5f};
                            af_vec3 to_cam = af_vec3_sub(center, camera->position);
                            if (af_vec3_dot(to_cam, to_cam) > max_dist_sq)
                                continue;
                            if (!r->culling_disabled) {
                                af_vec3 ext_max = chunk->bounds_max;
                                ext_max.y += 10.0f;
                                af_shadow_bounds bounds =
                                    af_shadow_transform_bounds(af_mat4_identity(), chunk->bounds_min, ext_max);
                                if (!af_shadow_intersects(visibility_projection, &bounds)) {
                                    r->culled_objects += fc->count;
                                    continue;
                                }
                            }
                            for (size_t inst_idx = 0; inst_idx < fc->count; ++inst_idx) {
                                af_mat4 model = af_transform_matrix(fc->instances[inst_idx].transform);
                                uint32_t draw_flags = af_rc_mesh_face_flags(mesh->sidedness, model);
                                if (fl->material.receive_shadows)
                                    draw_flags |= AF_DRAW_RECEIVE_SHADOWS;
                                af_mesh_draw inst_transform = {
                                    .model = model,
                                    .material = {
                                        .tint = fl->material.base_color,
                                        .params = {(float)((fl->material.unlit ? 1u : 0u) |
                                                           ((uint32_t)fl->material.alpha_mode << 1u) |
                                                           ((uint32_t)lroundf(fl->material.alpha_cutoff * 65535.0f)
                                                            << 3u) |
                                                           (fl->material.normal_map[0] ? (1u << 23u) : 0u)),
                                                   (float)draw_flags, fl->material.physically_based ? 1.0f : 0.0f,
                                                   fl->material.ior},
                                        .surface = {fl->material.roughness, fl->material.metallic,
                                                    fl->material.normal_strength, 0.0f},
                                        .emission = {fl->material.emission.x, fl->material.emission.y,
                                                     fl->material.emission.z, fl->material.emission_strength}}};
                                [enc setVertexBytes:&inst_transform
                                             length:sizeof(inst_transform)
                                            atIndex:AF_MTL_BUF_PUSH];
                                [enc setFragmentBytes:&inst_transform
                                               length:sizeof(inst_transform)
                                              atIndex:AF_MTL_BUF_PUSH];
                                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                                                indexCount:mesh->index_count
                                                 indexType:MTLIndexTypeUInt32
                                               indexBuffer:mesh->indices.handle
                                         indexBufferOffset:0];
                                ++draw_calls;
                                ++r->visible_objects;
                            }
                        }
                    }
                }
            }
            free(blended);
        }
        if (enc)
            [enc endEncoding];

        /* Upload finalized motion/instancing records before submitting the command buffer. */
        if (record_count > 0) {
            memcpy([frame->motion_buffer.handle contents], r->motion_records,
                   record_count * sizeof(*r->motion_records));
        }

        /* 5. GI denoise (if not already run before transparent pass). */
        if ((r->swapchain.resource_config & AF_RESOURCE_RAY_GI) && !gi_denoised) {
            af_mtl_post_denoise_gi(r->swapchain.post, cmd, r->frame_index, env_settings, post_area, gi_jitter_delta);
        }

        /* 6. Optional MetalFX upscaler. */
        bool upscaled = false;
        if (upscale && r->swapchain.upscaler_created) {
            float jpx = r->temporal_jitter.x * (float)r->swapchain.render_width;
            float jpy = r->temporal_jitter.y * (float)r->swapchain.render_height;
            upscaled = af_mtl_upscaler_encode(r->upscaler, cmd,
                                              af_mtl_post_scene_texture(r->swapchain.post, r->frame_index),
                                              r->swapchain.depths[r->frame_index],
                                              af_mtl_post_motion_texture(r->swapchain.post, r->frame_index),
                                              af_mtl_post_upscaled_texture(r->swapchain.post, r->frame_index), jpx, jpy,
                                              !r->temporal_valid || color_history_changed);
        }

        /* 7. Post-processing & final output pass + Nuklear UI overlay. */
        float time_sec = (float)r->rendered_frames * (1.0f / 60.0f);
        float delta_sec = frame_time_ms * 0.001f;
        id<MTLRenderCommandEncoder> out_enc = af_mtl_post_draw(
            r->swapchain.post, cmd, drawable.texture, r->frame_index, env_settings, post_area, r->temporal_jitter,
            r->debug_view, upscaled, time_sec, delta_sec);
        if (out_enc) {
            if (r->ui && overlay->index_count)
                af_mtl_ui_draw(r->ui, out_enc, r->frame_index, overlay, r->swapchain.width, r->swapchain.height);
            [out_enc endEncoding];
        }

        /* 8. Optional test pixel/region/motion readback. */
        if (r->capture_pending) {
            id<MTLTexture> src_tex = r->capture_motion ? af_mtl_post_motion_texture(r->swapchain.post, r->frame_index)
                                                       : drawable.texture;
            uint32_t tex_w = (uint32_t)src_tex.width;
            uint32_t tex_h = (uint32_t)src_tex.height;
            uint32_t x = (uint32_t)fmaxf(
                0.0f, fminf((float)(tex_w - 1), r->capture_position.x * (float)tex_w / (float)logical_w));
            uint32_t y = (uint32_t)fmaxf(
                0.0f, fminf((float)(tex_h - 1), r->capture_position.y * (float)tex_h / (float)logical_h));
            if (r->capture_width > tex_w - x)
                r->capture_width = tex_w - x;
            if (r->capture_height > tex_h - y)
                r->capture_height = tex_h - y;
            NSUInteger bpp = r->capture_motion ? 8u : 4u;
            NSUInteger bpr = (NSUInteger)r->capture_width * bpp;
            id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
            [blit copyFromTexture:src_tex
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(x, y, 0)
                       sourceSize:MTLSizeMake(r->capture_width, r->capture_height, 1)
                         toBuffer:r->capture_buffer.handle
                destinationOffset:0
           destinationBytesPerRow:bpr
         destinationBytesPerImage:bpr * r->capture_height];
            [blit endEncoding];
            r->capture_pending = false;
            r->capture_ready = true;
        }

        r->cpu_record_ms =
            (double)(SDL_GetPerformanceCounter() - record_start) * 1000.0 / (double)SDL_GetPerformanceFrequency();

        dispatch_semaphore_t sem = r->frame_semaphore;
        af_mtl_frame *completed_frame = frame;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer> cb) {
            if (cb.GPUEndTime > cb.GPUStartTime) {
                completed_frame->gpu_total_ms = (cb.GPUEndTime - cb.GPUStartTime) * 1000.0;
                completed_frame->timing_ready = true;
            }
            dispatch_semaphore_signal(sem);
        }];
        r->frame_semaphore_held = false;

        AF_MTL_RELEASE(frame->last_command);
        frame->last_command = [cmd retain];

        [cmd presentDrawable:drawable];
        [cmd commit];

        /* Advance temporal & frame bookkeeping. */
        if (temporal) {
            af_motion_snapshot *swap = r->motion_previous;
            size_t swap_cap = r->motion_previous_capacity;
            r->motion_previous = r->motion_next;
            r->motion_previous_capacity = r->motion_next_capacity;
            r->motion_count = r->motion_next_count;
            r->motion_next = swap;
            r->motion_next_capacity = swap_cap;
            r->previous_skin_count = r->skin_count;
            if (r->skin_count)
                memcpy(r->previous_skins, r->skins, r->skin_count * sizeof(*r->skins));
            r->previous_view_projection = uniform.unjittered_view_projection;
            r->previous_temporal_jitter = r->temporal_jitter;
            r->temporal_valid = true;
            ++r->temporal_frame;
        }
        if (ray_gi) {
            r->gi_emitter_signature = emitter_signature;
            r->temporal_local_light_signature = local_light_signature;
        }
        r->draw_calls = draw_calls;
        r->missing_meshes = missing_meshes;
        ++r->rendered_frames;
        r->frame_index = (r->frame_index + 1u) % AF_FRAMES_IN_FLIGHT;
        *rendered = true;
        return true;

#undef ABORT_FRAME
    }
}

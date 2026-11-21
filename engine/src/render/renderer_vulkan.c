/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "renderer_internal.h"

static int blend_compare(const void *a, const void *b) {
    const af_blend_draw *x = a, *y = b;
    if (x->depth != y->depth)
        return x->depth > y->depth ? -1 : 1;
    return (x->index > y->index) - (x->index < y->index);
}

static uint32_t mesh_face_flags(af_mesh_sidedness sidedness, af_mat4 model) {
    if (sidedness == AF_MESH_SIDES_DEFAULT)
        return 0;
    const float *m = model.elements;
    af_vec3 a = {m[0], m[1], m[2]}, b = {m[4], m[5], m[6]}, c = {m[8], m[9], m[10]};
    uint32_t face = sidedness == AF_MESH_SIDES_FRONT ? AF_DRAW_CULL_BACK : AF_DRAW_FLIP_BACK_NORMAL;
    return face | (af_vec3_dot(a, af_vec3_cross(b, c)) < 0 ? AF_DRAW_MIRRORED : 0);
}
static uint32_t shadow_material_flags(af_alpha_mode alpha_mode, af_mesh_sidedness sidedness, af_mat4 model) {
    return (uint32_t)alpha_mode | (mesh_face_flags(sidedness, model) << 8);
}

static bool prepare_shadows(af_renderer *renderer, const af_scene *scene, const af_camera *camera, af_rect area,
                            af_frame_uniform *uniform, af_shadow_view *view) {
    af_shadow_settings settings = af_scene_get_shadows(scene);
    uint32_t layers = settings.mode == AF_SHADOW_CASCADED ? 4u : 1u;
    if (settings.resolution != renderer->shadow_resolution || layers != renderer->shadow_layers) {
        if (!af_renderer_wait_idle(renderer))
            return false;
        af_vk_shadows *replacement =
            af_vk_shadows_create(renderer->physical_device, renderer->device, renderer->allocator, settings.resolution,
                                 layers, renderer->frame_layout, af_vk_texture_layout(renderer->white));
        if (!replacement)
            return false;
        af_vk_shadows_destroy(renderer->shadows);
        renderer->shadows = replacement;
        renderer->shadow_resolution = settings.resolution;
        renderer->shadow_layers = layers;
        for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
            memset(renderer->frames[f].shadow_signatures, 0, sizeof(renderer->frames[f].shadow_signatures));
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
        for (size_t l = 0; l < ls->foliage_layer_count; ++l) {
            if (ls->foliage_layers[l].cast_shadows && ls->foliage_layers[l].visible)
                count += ls->foliage_layers[l].total_instances;
        }
    }
    if (count > renderer->shadow_capacity) {
        af_shadow_draw *draws = realloc(renderer->shadow_draws, count * sizeof(*draws));
        if (!draws)
            return false;
        renderer->shadow_draws = draws;
        af_shadow_bounds *bounds = realloc(renderer->shadow_bounds, count * sizeof(*bounds));
        if (!bounds)
            return false;
        renderer->shadow_bounds = bounds;
        renderer->shadow_capacity = count;
    }
    renderer->shadow_count = 0;
    for (size_t i = 0; (settings.enabled || local_cast) && i < af_scene_object_count(scene); ++i) {
        const af_scene_object *object = af_scene_object_at(scene, i);
        if (!object->material.cast_shadows || object->material.alpha_mode == AF_ALPHA_BLEND ||
            !af_scene_object_visible(scene, object->id))
            continue;
        size_t mesh = af_r_find_mesh_index(renderer, object->mesh);
        af_mat4 model;
        if (mesh == renderer->mesh_count || !af_r_object_world(renderer, scene, i, &model))
            continue;
        size_t n = renderer->shadow_count++;
        af_vk_texture *texture = renderer->white;
        for (size_t t = 0; t < renderer->texture_count; ++t)
            if (!renderer->textures[t].normal && !strcmp(renderer->textures[t].name, object->material.texture)) {
                texture = renderer->textures[t].texture;
                break;
            }
        renderer->shadow_draws[n] = (af_shadow_draw){model,
                                                     mesh,
                                                     af_r_skin_base(renderer, object->id, &renderer->meshes[mesh].gpu),
                                                     texture,
                                                     object->material.base_color.a,
                                                     object->material.alpha_cutoff,
                                                     shadow_material_flags(object->material.alpha_mode,
                                                                           renderer->meshes[mesh].gpu.sidedness, model),
                                                     false,
                                                     i};
        renderer->shadow_bounds[n] =
            af_r_object_bounds(renderer, object->id, model, &renderer->meshes[mesh].gpu, af_r_morph_draw_for(renderer, scene, i));
    }
    if ((settings.enabled || local_cast) && ls && ls->enabled) {
        for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
            const af_terrain_chunk *c = &ls->chunks[c_idx];
            size_t mesh = af_r_find_mesh_index(renderer, c->mesh_name);
            if (mesh == renderer->mesh_count)
                continue;
            size_t n = renderer->shadow_count++;
            renderer->shadow_draws[n] = (af_shadow_draw){
                af_mat4_identity(), mesh, 0, renderer->white, 1.0f, 0.5f, (uint32_t)AF_ALPHA_OPAQUE, false, SIZE_MAX};
            renderer->shadow_bounds[n] = af_shadow_transform_bounds(af_mat4_identity(), c->bounds_min, c->bounds_max);
        }
        for (size_t l = 0; l < ls->foliage_layer_count; ++l) {
            const af_foliage_layer *fl = &ls->foliage_layers[l];
            if (!fl->visible || !fl->cast_shadows || fl->total_instances == 0 || fl->mesh[0] == '\0')
                continue;
            size_t mesh = af_r_find_mesh_index(renderer, fl->mesh);
            if (mesh == renderer->mesh_count)
                continue;
            const af_gpu_mesh *m = &renderer->meshes[mesh].gpu;
            af_vk_texture *texture = renderer->white;
            for (size_t t = 0; t < renderer->texture_count; ++t) {
                if (!renderer->textures[t].normal && !strcmp(renderer->textures[t].name, fl->material.texture)) {
                    texture = renderer->textures[t].texture;
                    break;
                }
            }
            for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
                const af_foliage_chunk *fc = &ls->chunks[c_idx].foliage[l];
                for (size_t inst_idx = 0; inst_idx < fc->count; ++inst_idx) {
                    af_mat4 model = af_transform_matrix(fc->instances[inst_idx].transform);
                    size_t n = renderer->shadow_count++;
                    renderer->shadow_draws[n] = (af_shadow_draw){model,
                                                                 mesh,
                                                                 0,
                                                                 texture,
                                                                 fl->material.base_color.a,
                                                                 fl->material.alpha_cutoff,
                                                                 shadow_material_flags(fl->material.alpha_mode,
                                                                                       m->sidedness, model),
                                                                 false,
                                                                 SIZE_MAX};
                    renderer->shadow_bounds[n] = af_shadow_transform_bounds(model, m->minimum, m->maximum);
                }
            }
        }
    }
    af_directional_light light = af_scene_get_light(scene);
    af_vec3 direction = af_vec3_normalize(light.direction), forward = af_camera_forward(camera);
    if (!af_shadow_build(camera, area.width / area.height, direction, settings, renderer->shadow_bounds,
                         renderer->shadow_count, view))
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
    SDL_GetWindowSize(renderer->window, &logical_width, &logical_height);
    uniform->water_screen =
        (af_vec4){(float)renderer->swapchain.render_extent.width, (float)renderer->swapchain.render_extent.height,
                  render_settings.refraction_strength, render_settings.refraction ? 1.f : 0};
    uniform->water_viewport =
        (af_vec4){area.x / (float)logical_width, area.y / (float)logical_height,
                  (area.x + area.width) / (float)logical_width, (area.y + area.height) / (float)logical_height};
    double water_time = af_physics_time(renderer->water_physics);
    af_vec3 air = af_wind_velocity(af_scene_get_wind(scene), water_time);
    uniform->wind_time = (af_vec4){air.x, air.z, (float)water_time, (float)(renderer->rendered_frames & 255u)};
    af_water_sample underwater = af_physics_sample_water(renderer->water_physics, scene, camera->position);
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
    renderer->local_light_staging_count = 0;
    for (size_t i = 0; i < af_scene_local_light_count(scene); ++i) {
        const af_local_light *local = af_scene_local_light_at(scene, i);
        if (!local->enabled)
            continue;
        af_vec3 ray = af_vec3_normalize(local->direction);
        unsigned n = renderer->local_light_staging_count++;
        renderer->local_light_staging[n] =
            (af_gpu_light){.position_range = {local->position.x, local->position.y, local->position.z, local->range},
                           .color_intensity = {local->color.x, local->color.y, local->color.z, local->intensity},
                           .direction_inner = {ray.x, ray.y, ray.z, cosf(local->inner_angle)},
                           .params = {cosf(local->outer_angle), local->type == AF_LIGHT_SPOT ? 1.0f : 0.0f, 0, 0}};
        uniform->light_count.x = (float)renderer->local_light_staging_count;
        bool chosen = false;
        for (unsigned j = 0; j < selected_count; ++j)
            if (selected[j] == i)
                chosen = true;
        if (chosen) {
            renderer->local_light_staging[n].params.z = (float)(local_layers + 1);
            renderer->local_light_staging[n].params.w = local->shadow_bias;
            unsigned faces = local->type == AF_LIGHT_POINT ? 6u : 1u;
            for (unsigned face = 0; face < faces; ++face)
                uniform->local_shadow_matrices[local_layers++] = af_local_shadow_matrix(*local, face);
            ++shadow_lights;
        }
    }
    renderer->local_shadow_lights = shadow_lights;
    renderer->local_shadow_overflow = requested - shadow_lights;
    uint32_t wanted_layers = local_layers ? local_layers : 1;
    if (wanted_layers != renderer->local_shadow_layers ||
        render_settings.local_shadow_resolution != renderer->local_shadow_resolution) {
        if (!af_renderer_wait_idle(renderer))
            return false;
        af_vk_shadows *replacement =
            af_vk_shadows_create(renderer->physical_device, renderer->device, renderer->allocator,
                                 render_settings.local_shadow_resolution, wanted_layers, renderer->frame_layout,
                                 af_vk_texture_layout(renderer->white));
        if (!replacement)
            return false;
        af_vk_shadows_destroy(renderer->local_shadows);
        renderer->local_shadows = replacement;
        renderer->local_shadow_layers = wanted_layers;
        renderer->local_shadow_resolution = render_settings.local_shadow_resolution;
        af_r_update_environment_descriptors(renderer);
        for (unsigned f = 0; f < AF_FRAMES_IN_FLIGHT; ++f)
            memset(renderer->frames[f].local_shadow_signatures, 0, sizeof(renderer->frames[f].local_shadow_signatures));
    }
    uniform->light_count.y = 1.0f / (float)renderer->local_shadow_resolution;
    uniform->light_count.z = (float)local_layers;
    memcpy(uniform->shadow_matrices, view->matrices, sizeof(view->matrices));
    return true;
}

static size_t add_streamed_ray_instances(af_renderer *r, const af_gpu_mesh *mesh, af_ray_instance base,
                                         af_cluster_view view, size_t extra, af_ray_instance *inputs) {
    af_cluster_index_range ranges[AF_CLUSTER_INDEX_RANGES];
    size_t count = af_r_cluster_index_ranges(r, mesh, base.world, view, ranges);
    if (!count || count - 1 > extra) {
        inputs[0] = base;
        return 1;
    }
    for (size_t i = 0; i < count; ++i) {
        const af_cluster_index_range *range = &ranges[i];
        af_ray_instance input = base;
        input.transient = true;
        input.first_index = range->first;
        input.index_count = range->count;
        if (range->page) {
            af_cached_cluster_page *page = range->page;
            af_r_use_cluster_page(r, page);
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

static bool prepare_ray_scene(af_renderer *r, af_frame *frame, const af_scene *scene, bool shadows, bool reflections,
                              bool gi, bool material_effects, af_cluster_view view, uint64_t *emitter_signature) {
    if (r->rays && r->ray_revision != r->mesh_revision) {
        if (!af_renderer_wait_idle(r))
            return false;
        af_vk_ray_query_destroy(r->rays);
        r->rays = NULL;
    }
    if (!r->rays) {
        r->rays = af_vk_ray_query_create(r->physical_device, r->device, r->allocator, AF_FRAMES_IN_FLIGHT,
                                         r->timestamp_bits, r->timestamp_period);
        r->ray_revision = r->mesh_revision;
        if (!r->rays)
            return false;
    }
    size_t objects = af_scene_object_count(scene);
    size_t limit = af_vk_ray_query_instance_limit(r->rays);
    /* Reserve space for ordinary geometry before adding streamed ranges. */
    size_t extra = objects < limit ? limit - objects : 0;
    if (extra > 256)
        extra = 256;
    size_t capacity = objects + extra;
    if (capacity > r->ray_input_capacity) {
        af_ray_instance *inputs = realloc(r->ray_inputs, capacity * sizeof(*inputs));
        if (!inputs)
            return false;
        r->ray_inputs = inputs;
        r->ray_input_capacity = capacity;
    }

    size_t count = 0, shadow_index = 0;
    r->ray_raster_casters = 0;
    for (size_t i = 0; i < objects; ++i) {
        af_shadow_draw *caster = NULL;
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
        size_t mesh = caster ? caster->mesh : af_r_find_mesh_index(r, object->mesh);
        if (mesh == r->mesh_count)
            continue;
        const af_mesh_asset *asset = &r->meshes[mesh];
        if (asset->dynamic || asset->gpu.skinned || asset->gpu.morph)
            continue;
        af_mat4 world, inverse;
        if (caster)
            world = caster->model;
        else if (!af_r_object_world(r, scene, i, &world))
            continue;
        if (!af_mat4_inverse(world, &inverse))
            continue;
        if (caster && shadows)
            caster->ray_caster = true;

        af_ray_instance input = {.mesh_key = mesh + 1,
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
            /* Track which emitters contribute and their output. Motion of a
               visible emitter is continuous and should keep temporal history. */
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
    return af_vk_ray_query_prepare(r->rays, r->frame_index, r->ray_inputs, count,
                                   reflections || gi || material_effects, frame->descriptor);
}
static size_t shadow_cluster_ranges(const af_renderer *r, const af_shadow_draw *draw, af_cluster_view view,
                                    af_cluster_index_range *ranges) {
    if (draw->object_index == SIZE_MAX || (draw->material_flags & 3u) != AF_ALPHA_OPAQUE)
        return 0;
    return af_r_cluster_index_ranges(r, &r->meshes[draw->mesh].gpu, draw->model, view, ranges);
}

static void draw_shadow_casters(af_renderer *r, af_frame *frame, const af_scene *scene, af_vk_shadows *maps,
                                af_mat4 matrix, af_cluster_view view, uint64_t *fine_draws) {
    for (size_t i = 0; i < r->shadow_count; ++i) {
        const af_shadow_draw *draw = &r->shadow_draws[i];
        if (draw->ray_caster || !af_shadow_intersects(matrix, &r->shadow_bounds[i]))
            continue;
        const af_mesh_asset *asset = &r->meshes[draw->mesh];
        const af_gpu_mesh *mesh = asset->dynamic ? &asset->streams[r->frame_index].gpu : &asset->gpu;
        af_mat4 mvp = af_mat4_mul(matrix, draw->model);
        af_vk_shadows_push(maps, frame->command, mvp.elements, draw->skin_base, draw->material_flags, draw->alpha,
                           draw->cutoff, af_vk_texture_descriptor(draw->texture));
        af_cluster_index_range ranges[AF_CLUSTER_INDEX_RANGES];
        size_t count = shadow_cluster_ranges(r, draw, view, ranges);
        if (!count) {
            af_r_bind_object_mesh(frame, mesh, af_r_morph_draw_for(r, scene, draw->object_index));
            vkCmdDrawIndexed(frame->command, mesh->index_count, 1, 0, 0, 0);
            ++r->shadow_draw_calls;
            continue;
        }
        for (size_t j = 0; j < count; ++j) {
            af_cached_cluster_page *page = ranges[j].page;
            if (page) {
                af_r_use_cluster_page(r, page);
                ++*fine_draws;
            }
            af_r_bind_object_mesh(frame, page ? &page->gpu : mesh, NULL);
            vkCmdDrawIndexed(frame->command, ranges[j].count, 1, ranges[j].first, 0, 0);
        }
        r->shadow_draw_calls += count;
    }
}

static uint64_t shadow_signature(af_renderer *r, af_mat4 matrix, af_shadow_settings settings, bool enabled,
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
            const af_morph_draw *morph = af_r_morph_draw_for(r, r->morph_scene, r->shadow_draws[i].object_index);
            if (morph)
                h = af_r_hash(h, &morph->signature, sizeof(morph->signature));
            const af_shadow_draw *draw = &r->shadow_draws[i];
            if (r->meshes[draw->mesh].gpu.residency && !draw->ray_caster &&
                af_shadow_intersects(matrix, &r->shadow_bounds[i])) {
                af_cluster_index_range ranges[AF_CLUSTER_INDEX_RANGES];
                size_t count = shadow_cluster_ranges(r, draw, view, ranges);
                h = af_r_hash(h, &count, sizeof(count));
                for (size_t j = 0; j < count; ++j)
                    if (ranges[j].page)
                        h = af_r_hash(h, &ranges[j].page->generation, sizeof(ranges[j].page->generation));
            }
        }
    return h ? h : 1;
}

static int motion_compare(const void *a, const void *b) {
    af_object_id x = ((const af_motion_snapshot *)a)->id, y = ((const af_motion_snapshot *)b)->id;
    return (x > y) - (x < y);
}
static float halton(unsigned n, unsigned base) {
    float value = 0, scale = 1;
    while (n) {
        scale /= (float)base;
        value += scale * (float)(n % base);
        n /= base;
    }
    return value;
}
static uint64_t temporal_settings_hash(uint64_t h, af_render_settings value) {
    h = af_r_hash(h, &value.ray_traced_shadows, sizeof(value.ray_traced_shadows));
    h = af_r_hash(h, &value.ray_traced_water_reflections, sizeof(value.ray_traced_water_reflections));
    h = af_r_hash(h, &value.ray_traced_gi, sizeof(value.ray_traced_gi));
    h = af_r_hash(h, &value.rtgi_intensity, sizeof(value.rtgi_intensity));
    h = af_r_hash(h, &value.rtgi_ray_distance, sizeof(value.rtgi_ray_distance));
    h = af_r_hash(h, &value.rtgi_samples, sizeof(value.rtgi_samples));
    h = af_r_hash(h, &value.rtgi_bounces, sizeof(value.rtgi_bounces));
    h = af_r_hash(h, &value.rtgi_emissive, sizeof(value.rtgi_emissive));
    h = af_r_hash(h, &value.anti_aliasing, sizeof(value.anti_aliasing));
    h = af_r_hash(h, &value.aa_sharpness, sizeof(value.aa_sharpness));
    h = af_r_hash(h, &value.taa_history_weight, sizeof(value.taa_history_weight));
    h = af_r_hash(h, &value.exposure, sizeof(value.exposure));
    h = af_r_hash(h, &value.tone_mapping, sizeof(value.tone_mapping));
    h = af_r_hash(h, &value.underwater, sizeof(value.underwater));
    h = af_r_hash(h, &value.refraction, sizeof(value.refraction));
    h = af_r_hash(h, &value.refraction_strength, sizeof(value.refraction_strength));
    h = af_r_hash(h, &value.bloom, sizeof(value.bloom));
    h = af_r_hash(h, &value.bloom_strength, sizeof(value.bloom_strength));
    h = af_r_hash(h, &value.bloom_threshold, sizeof(value.bloom_threshold));
    h = af_r_hash(h, &value.bloom_radius, sizeof(value.bloom_radius));
    h = af_r_hash(h, &value.local_shadow_resolution, sizeof(value.local_shadow_resolution));
    h = af_r_hash(h, &value.environment_enabled, sizeof(value.environment_enabled));
    h = af_r_hash(h, &value.environment_profile, sizeof(value.environment_profile));
    h = af_r_hash(h, &value.skybox, sizeof(value.skybox));
    h = af_r_hash(h, &value.skybox_intensity, sizeof(value.skybox_intensity));
    h = af_r_hash(h, &value.environment_intensity, sizeof(value.environment_intensity));
    h = af_r_hash(h, &value.environment_rotation, sizeof(value.environment_rotation));
    h = af_r_hash(h, &value.motion_blur, sizeof(value.motion_blur));
    h = af_r_hash(h, &value.motion_blur_strength, sizeof(value.motion_blur_strength));
    h = af_r_hash(h, value.environment_asset, strlen(value.environment_asset));
    return h;
}
static uint64_t temporal_material_hash(uint64_t h, af_material value) {
    h = af_r_hash(h, &value.alpha_mode, sizeof(value.alpha_mode));
    h = af_r_hash(h, &value.alpha_cutoff, sizeof(value.alpha_cutoff));
    h = af_r_hash(h, &value.base_color, sizeof(value.base_color));
    h = af_r_hash(h, &value.unlit, sizeof(value.unlit));
    h = af_r_hash(h, &value.cast_shadows, sizeof(value.cast_shadows));
    h = af_r_hash(h, &value.receive_shadows, sizeof(value.receive_shadows));
    h = af_r_hash(h, &value.physically_based, sizeof(value.physically_based));
    h = af_r_hash(h, &value.roughness, sizeof(value.roughness));
    h = af_r_hash(h, &value.metallic, sizeof(value.metallic));
    h = af_r_hash(h, &value.emission, sizeof(value.emission));
    h = af_r_hash(h, &value.emission_strength, sizeof(value.emission_strength));
    h = af_r_hash(h, &value.normal_strength, sizeof(value.normal_strength));
    h = af_r_hash(h, value.texture, strlen(value.texture));
    h = af_r_hash(h, value.normal_map, strlen(value.normal_map));
    h = af_r_hash(h, value.metallic_roughness_map, strlen(value.metallic_roughness_map));
    h = af_r_hash(h, value.shader, strlen(value.shader));
    return h;
}
static uint64_t temporal_light_hash(uint64_t h, af_local_light value) {
    h = af_r_hash(h, &value.type, sizeof(value.type));
    h = af_r_hash(h, &value.enabled, sizeof(value.enabled));
    h = af_r_hash(h, &value.position, sizeof(value.position));
    h = af_r_hash(h, &value.color, sizeof(value.color));
    h = af_r_hash(h, &value.intensity, sizeof(value.intensity));
    h = af_r_hash(h, &value.range, sizeof(value.range));
    h = af_r_hash(h, &value.direction, sizeof(value.direction));
    h = af_r_hash(h, &value.cast_shadows, sizeof(value.cast_shadows));
    h = af_r_hash(h, &value.shadow_bias, sizeof(value.shadow_bias));
    h = af_r_hash(h, &value.inner_angle, sizeof(value.inner_angle));
    h = af_r_hash(h, &value.outer_angle, sizeof(value.outer_angle));
    return h;
}
static bool prepare_temporal(af_renderer *r, const af_scene *scene, const af_camera *camera, af_rect area,
                             af_color clear, af_render_settings settings, bool temporal, bool jitter,
                             af_frame_uniform *uniform, uint64_t *local_light_signature) {
    uniform->unjittered_view_projection = uniform->view_projection;
    uniform->previous_view_projection = uniform->view_projection;
    r->temporal_jitter = (af_vec2){0};
    if (!temporal) {
        r->temporal_valid = false;
        return true;
    }
    uint64_t signature = temporal_settings_hash(UINT64_C(14695981039346656037), settings);
    af_directional_light light = af_scene_get_light(scene);
    signature = af_r_hash(signature, &light, sizeof(light));
    signature = af_r_hash(signature, &clear, sizeof(clear));
    signature = af_r_hash(signature, &r->texture_revision, sizeof(r->texture_revision));
    signature = af_r_hash(signature, &r->mesh_revision, sizeof(r->mesh_revision));
    /* The GI shader doesn't sample local lights. Keep their changes out of
       the signature used to reset GI and its motion vectors when GI is active. */
    uint64_t *light_hash = local_light_signature ? local_light_signature : &signature;
    for (size_t i = 0; i < af_scene_local_light_count(scene); ++i)
        *light_hash = temporal_light_hash(*light_hash, *af_scene_local_light_at(scene, i));
    bool cut = af_vec3_length(af_vec3_sub(camera->position, r->temporal_camera.position)) > 5 ||
               af_vec3_dot(af_camera_forward(camera), af_camera_forward(&r->temporal_camera)) < .7071f ||
               camera->vertical_fov != r->temporal_camera.vertical_fov ||
               camera->near_clip != r->temporal_camera.near_clip || camera->far_clip != r->temporal_camera.far_clip;
    if (!r->temporal_valid || r->temporal_scene != scene || memcmp(&area, &r->temporal_viewport, sizeof(area)) ||
        signature != r->temporal_signature || cut) {
        r->temporal_valid = false;
        r->temporal_frame = 0;
        ++r->temporal_resets;
        af_vk_post_reset_temporal(r->swapchain.post);
    }
    r->temporal_scene = scene;
    r->temporal_camera = *camera;
    r->temporal_viewport = area;
    r->temporal_signature = signature;
    double ratio = fmax((double)r->swapchain.extent.width / r->swapchain.render_extent.width,
                        (double)r->swapchain.extent.height / r->swapchain.render_extent.height);
    unsigned phases = (unsigned)fmax(16, ceil(8 * ratio * ratio));
    unsigned phase = (unsigned)(r->temporal_frame % phases) + 1;
    int width = 1, height = 1;
    SDL_GetWindowSize(r->window, &width, &height);
    float pixels_x = area.width * (float)r->swapchain.render_extent.width / (float)(width ? width : 1),
          pixels_y = area.height * (float)r->swapchain.render_extent.height / (float)(height ? height : 1);
    float jx = jitter ? halton(phase, 2) - .5f : 0.f;
    float jy = jitter ? halton(phase, 3) - .5f : 0.f;
    r->temporal_jitter =
        (af_vec2){jx / (float)r->swapchain.render_extent.width, jy / (float)r->swapchain.render_extent.height};
    for (unsigned c = 0; c < 4; ++c) {
        uniform->view_projection.elements[4 * c] += 2 * jx / pixels_x * uniform->view_projection.elements[4 * c + 3];
        uniform->view_projection.elements[4 * c + 1] +=
            2 * jy / pixels_y * uniform->view_projection.elements[4 * c + 3];
    }
    if (!af_mat4_inverse(uniform->view_projection, &uniform->inverse_view_projection))
        return false;
    uniform->previous_view_projection =
        r->temporal_valid ? r->previous_view_projection : uniform->unjittered_view_projection;
    uniform->sky_params.y = 1;
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
        bool visible = af_r_object_world(r, scene, i, &model) && af_scene_object_visible(scene, o->id);
        uint64_t hash = af_r_hash(UINT64_C(14695981039346656037), o->mesh, strlen(o->mesh));
        hash = temporal_material_hash(hash, o->material);
        const af_morph_draw *morph = af_r_morph_draw_for(r, scene, i);
        if (morph)
            hash = af_r_hash(hash, &morph->signature, sizeof(morph->signature));
        r->motion_next[i] = (af_motion_snapshot){o->id, model, hash, visible};
        af_motion_snapshot key = {.id = o->id};
        const af_motion_snapshot *old =
            r->motion_count ? bsearch(&key, r->motion_previous, r->motion_count, sizeof(key), motion_compare) : NULL;
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
        } else if (previous_skin < r->previous_skin_count)
            valid = false;
        records[i].info[1] = valid ? 1u : 0u;
        r->temporal_objects += valid;
        r->temporal_skins += valid && current_skin < r->skin_count;
    }
    if (count > 1)
        qsort(r->motion_next, count, sizeof(*r->motion_next), motion_compare);
    return true;
}

/* The frame's storage buffer holds previous poses by scene index, followed by
   compact current poses for instanced draws. The frame fence owns both ranges. */
static bool reserve_draw_records(af_renderer *r, af_frame *frame, size_t count) {
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
    VkDeviceSize bytes = capacity * sizeof(af_motion_record);
    if (frame->motion_buffer.capacity >= bytes)
        return true;
    af_gpu_buffer replacement = {0};
    if (!af_r_upload_buffer(r, NULL, bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &replacement)) {
        af_r_destroy_buffer(r, &replacement);
        return false;
    }
    replacement.capacity = bytes;
    af_r_destroy_buffer(r, &frame->motion_buffer);
    frame->motion_buffer = replacement;
    VkDescriptorBufferInfo info = {frame->motion_buffer.handle, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstSet = frame->descriptor,
                                  .dstBinding = 9,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                  .pBufferInfo = &info};
    vkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
    return true;
}

static bool upload_draw_records(af_renderer *r, af_frame *frame, size_t count) {
    if (!count)
        return true;
    void *mapped = frame->motion_buffer.alloc.mapped;
    if (!mapped)
        return false;
    VkDeviceSize bytes = (VkDeviceSize)(count * sizeof(*r->motion_records));
    memcpy(mapped, r->motion_records, (size_t)bytes);
    return af_vk_flush(r->allocator, &frame->motion_buffer.alloc, 0, bytes);
}


static void bind_scene_material(af_renderer *r, af_frame *frame, const af_material *material, size_t object_count) {
    const char *names[] = {material->texture, material->normal_map, material->metallic_roughness_map};
    af_vk_texture *textures[] = {r->white, r->flat_normal, r->white};
    const uint32_t sets[] = {1, 3, 4};
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
        VkDescriptorSet descriptor = af_vk_texture_descriptor(textures[i]);
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, r->swapchain.pipeline_layout, sets[i],
                                1, &descriptor, 0, NULL);
    }
}


static bool prop_batch_matches(const af_prop_batch *batch, const af_gpu_mesh *mesh, const af_material *material,
                               const af_mesh_draw *draw) {
    return batch->mesh == mesh && !memcmp(&batch->draw.material, &draw->material, sizeof(draw->material)) &&
           !strcmp(batch->material->texture, material->texture) &&
           !strcmp(batch->material->normal_map, material->normal_map) &&
           !strcmp(batch->material->metallic_roughness_map, material->metallic_roughness_map) &&
           !strcmp(batch->material->shader, material->shader);
}

static void draw_prop_batch(af_renderer *r, af_frame *frame, af_prop_batch *batch, size_t *draws) {
    if (!batch->count)
        return;
    uint32_t first_instance = batch->first_object;
    if (batch->count > 1) {
        batch->draw.material.params.y += (float)AF_DRAW_INSTANCED;
        first_instance = batch->first_record;
    }
    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      af_r_material_pipeline(r, batch->material->shader, false));
    bind_scene_material(r, frame, batch->material, batch->count);
    vkCmdPushConstants(frame->command, r->swapchain.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(batch->draw), &batch->draw);
    af_r_bind_object_mesh(frame, batch->mesh, NULL);
    vkCmdDrawIndexed(frame->command, batch->mesh->index_count, batch->count, 0, 0, first_instance);
    ++*draws;
    *batch = (af_prop_batch){0};
}

static bool bin_and_upload_local_lights(af_renderer *renderer, af_frame *frame, const af_camera *camera,
                                        af_frame_uniform *uniform) {
    uint32_t cols = (renderer->swapchain.render_extent.width + AF_LIGHT_TILE_SIZE - 1u) / AF_LIGHT_TILE_SIZE;
    uint32_t rows = (renderer->swapchain.render_extent.height + AF_LIGHT_TILE_SIZE - 1u) / AF_LIGHT_TILE_SIZE;
    if (cols == 0)
        cols = 1;
    if (rows == 0)
        rows = 1;
    size_t tile_count = (size_t)cols * rows;
    if (tile_count > renderer->light_tile_capacity) {
        af_light_tile_mask *grown = realloc(renderer->light_tile_staging, tile_count * sizeof(*grown));
        if (!grown)
            return false;
        renderer->light_tile_staging = grown;
        renderer->light_tile_capacity = tile_count;
    }
    renderer->light_tile_cols = cols;
    renderer->light_tile_rows = rows;
    uniform->light_tile_params = (af_vec4){(float)cols, (float)rows, (float)AF_LIGHT_TILE_SIZE, 0.0f};
    memset(renderer->light_tile_staging, 0, tile_count * sizeof(af_light_tile_mask));

    uint32_t count = renderer->local_light_staging_count;
    if (count > 0) {
        float rw = (float)renderer->swapchain.render_extent.width;
        float rh = (float)renderer->swapchain.render_extent.height;
        float vp_x = uniform->water_viewport.x * rw;
        float vp_y = uniform->water_viewport.y * rh;
        float vp_w = (uniform->water_viewport.z - uniform->water_viewport.x) * rw;
        float vp_h = (uniform->water_viewport.w - uniform->water_viewport.y) * rh;
        const float *m = uniform->view_projection.elements;
        for (uint32_t i = 0; i < count; ++i) {
            const af_gpu_light *l = &renderer->local_light_staging[i];
            af_vec3 center = {l->position_range.x, l->position_range.y, l->position_range.z};
            float range = l->position_range.w;
            if (!(range > 0.0f) || !(l->color_intensity.w > 0.0f))
                continue;
            af_vec3 ext = {range, range, range};
            af_shadow_bounds bounds =
                af_shadow_transform_bounds(af_mat4_identity(), af_vec3_sub(center, ext), af_vec3_add(center, ext));
            if (!af_shadow_intersects(uniform->view_projection, &bounds))
                continue;
            uint32_t min_tx = 0, max_tx = cols - 1u;
            uint32_t min_ty = 0, max_ty = rows - 1u;
            af_vec3 to_light = af_vec3_sub(center, camera->position);
            float dist_sq = af_vec3_dot(to_light, to_light);
            float safe_r = range + camera->near_clip + 0.05f;
            if (dist_sq > safe_r * safe_r) {
                bool crosses_near = false;
                float ndc_min_x = 1e9f, ndc_max_x = -1e9f, ndc_min_y = 1e9f, ndc_max_y = -1e9f;
                for (unsigned c = 0; c < 8u; ++c) {
                    float x = center.x + ((c & 1u) ? range : -range);
                    float y = center.y + ((c & 2u) ? range : -range);
                    float z = center.z + ((c & 4u) ? range : -range);
                    float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
                    float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
                    float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
                    if (cw <= 0.001f) {
                        crosses_near = true;
                        break;
                    }
                    float nx = cx / cw;
                    float ny = cy / cw;
                    if (nx < ndc_min_x)
                        ndc_min_x = nx;
                    if (nx > ndc_max_x)
                        ndc_max_x = nx;
                    if (ny < ndc_min_y)
                        ndc_min_y = ny;
                    if (ny > ndc_max_y)
                        ndc_max_y = ny;
                }
                if (!crosses_near) {
                    float px0 = vp_x + (ndc_min_x * 0.5f + 0.5f) * vp_w;
                    float px1 = vp_x + (ndc_max_x * 0.5f + 0.5f) * vp_w;
                    float py0 = vp_y + (ndc_min_y * 0.5f + 0.5f) * vp_h;
                    float py1 = vp_y + (ndc_max_y * 0.5f + 0.5f) * vp_h;
                    if (px1 < 0.0f || px0 >= rw || py1 < 0.0f || py0 >= rh)
                        continue;
                    int t0x = (int)floorf(px0 / (float)AF_LIGHT_TILE_SIZE) - 1;
                    int t1x = (int)floorf(px1 / (float)AF_LIGHT_TILE_SIZE) + 1;
                    int t0y = (int)floorf(py0 / (float)AF_LIGHT_TILE_SIZE) - 1;
                    int t1y = (int)floorf(py1 / (float)AF_LIGHT_TILE_SIZE) + 1;
                    if (t0x < 0)
                        t0x = 0;
                    if (t0y < 0)
                        t0y = 0;
                    if (t1x >= (int)cols)
                        t1x = (int)cols - 1;
                    if (t1y >= (int)rows)
                        t1y = (int)rows - 1;
                    if (t0x > t1x || t0y > t1y)
                        continue;
                    min_tx = (uint32_t)t0x;
                    max_tx = (uint32_t)t1x;
                    min_ty = (uint32_t)t0y;
                    max_ty = (uint32_t)t1y;
                }
            }
            uint32_t word = i >> 5u;
            uint32_t bit = 1u << (i & 31u);
            for (uint32_t ty = min_ty; ty <= max_ty; ++ty) {
                af_light_tile_mask *row = &renderer->light_tile_staging[(size_t)ty * cols];
                for (uint32_t tx = min_tx; tx <= max_tx; ++tx)
                    row[tx].words[word] |= bit;
            }
        }
        if (!frame->light_buffer.alloc.mapped)
            return false;
        VkDeviceSize light_bytes = (VkDeviceSize)count * sizeof(af_gpu_light);
        memcpy(frame->light_buffer.alloc.mapped, renderer->local_light_staging, (size_t)light_bytes);
        if (!af_vk_flush(renderer->allocator, &frame->light_buffer.alloc, 0, light_bytes))
            return false;
    }

    VkDeviceSize tile_bytes = (VkDeviceSize)tile_count * sizeof(af_light_tile_mask);
    if (frame->light_tile_buffer.capacity < tile_bytes) {
        VkDeviceSize alloc_bytes = frame->light_tile_buffer.capacity ? frame->light_tile_buffer.capacity : 1024;
        while (alloc_bytes < tile_bytes)
            alloc_bytes *= 2;
        af_gpu_buffer replacement = {0};
        if (!af_r_upload_buffer(renderer, NULL, alloc_bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &replacement)) {
            af_r_destroy_buffer(renderer, &replacement);
            return false;
        }
        replacement.capacity = alloc_bytes;
        af_r_destroy_buffer(renderer, &frame->light_tile_buffer);
        frame->light_tile_buffer = replacement;
        VkDescriptorBufferInfo tile_info = {frame->light_tile_buffer.handle, 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstSet = frame->descriptor,
                                      .dstBinding = 13,
                                      .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      .pBufferInfo = &tile_info};
        vkUpdateDescriptorSets(renderer->device, 1, &write, 0, NULL);
    }
    if (!frame->light_tile_buffer.alloc.mapped)
        return false;
    memcpy(frame->light_tile_buffer.alloc.mapped, renderer->light_tile_staging, (size_t)tile_bytes);
    return af_vk_flush(renderer->allocator, &frame->light_tile_buffer.alloc, 0, tile_bytes);
}


bool af_renderer_draw(af_renderer *renderer, af_color color, const af_scene *scene, const af_camera *camera,
                      af_rect viewport_rect, af_object_id highlighted, const af_overlay_frame *overlay,
                      float frame_time_ms, bool *rendered) {
    *rendered = false;
    renderer->missing_textures = 0;
    af_render_settings environment_settings = af_scene_get_render_settings(scene);
    bool upscale = renderer->upscaler_enabled && !renderer->upscaler_failed && renderer->temporal_supported &&
                   renderer->debug_view == AF_DEBUG_SHADED && !(renderer->capture_pending && renderer->capture_motion);
    if (upscale)
        environment_settings.anti_aliasing = AF_AA_OFF;
    if (renderer->debug_view != AF_DEBUG_SHADED)
        environment_settings.anti_aliasing = AF_AA_TAA;
    if (environment_settings.anti_aliasing == AF_AA_TAA && !renderer->temporal_supported)
        environment_settings.anti_aliasing = AF_AA_FXAA;
    if (environment_settings.motion_blur && !renderer->temporal_supported)
        environment_settings.motion_blur = false;
    uint32_t resource_config =
        (uint32_t)environment_settings.anti_aliasing | (environment_settings.bloom ? AF_RESOURCE_BLOOM : 0u) |
        ((environment_settings.anti_aliasing == AF_AA_TAA || environment_settings.motion_blur ||
          environment_settings.depth_of_field || (renderer->capture_pending && renderer->capture_motion))
             ? AF_RESOURCE_MOTION
             : 0u) |
        ((environment_settings.refraction && af_scene_water_count(scene) > 0) ? AF_RESOURCE_REFRACTION : 0u);
    if (upscale)
        resource_config |= AF_RESOURCE_UPSCALER | AF_RESOURCE_MOTION;
    if (renderer->ray_supported && !renderer->ray_failed) {
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
        if (environment_settings.ray_traced_shadows && any_shadows)
            resource_config |= AF_RESOURCE_RAY_SHADOWS;
        if (environment_settings.ray_traced_water_reflections) {
            for (size_t i = 0; i < af_scene_water_count(scene); ++i) {
                const af_water *water = af_scene_water_at(scene, i);
                if (water->enabled && water->mode != AF_WATER_PARTICLES &&
                    af_scene_object_visible(scene, water->owner)) {
                    resource_config |= AF_RESOURCE_RAY_REFLECTIONS;
                    break;
                }
            }
        }
        if (environment_settings.ray_traced_material_effects)
            resource_config |= AF_RESOURCE_RAY_MATERIALS;
        if (environment_settings.ray_traced_gi) {
            resource_config |= AF_RESOURCE_RAY_GI;
            /* The GI denoiser reprojects with motion vectors. */
            resource_config |= AF_RESOURCE_MOTION;
        }
    }
    if (renderer->resize_pending || !renderer->swapchain.handle ||
        renderer->swapchain.resource_config != resource_config) {
        if (!af_r_create_swapchain(renderer, environment_settings, resource_config)) {
            if (upscale) {
                af_log("warning", "Vulkan upscaler resources unavailable. Restoring built-in rendering.");
                renderer->upscaler_failed = true;
                renderer->resize_pending = renderer->swapchain_invalid = true;
                return true;
            }
            if (resource_config & AF_RESOURCE_RAY_QUERY) {
                af_log("warning", "Optional ray-query pipeline unavailable; retrying raster rendering.");
                renderer->ray_failed = true;
                renderer->resize_pending = renderer->swapchain_invalid = true;
                return true;
            }
            return false;
        }
        if (renderer->resize_pending || !renderer->swapchain.handle ||
            renderer->swapchain.resource_config != resource_config)
            return true;
    }
    if (!(resource_config & AF_RESOURCE_RAY_QUERY) && renderer->rays) {
        if (!af_renderer_wait_idle(renderer))
            return false;
        af_vk_ray_query_destroy(renderer->rays);
        renderer->rays = NULL;
        renderer->ray_raster_casters = 0;
    }
    if ((environment_settings.environment_enabled || environment_settings.skybox) &&
        !environment_settings.environment_asset[0] &&
        environment_settings.environment_profile != renderer->environment_profile &&
        !af_r_create_environment(renderer, environment_settings.environment_profile))
        return false;
    bool environment_found = !environment_settings.environment_asset[0];
    for (size_t i = 0; i < renderer->environment_count; ++i)
        if (!strcmp(renderer->environments[i].name, environment_settings.environment_asset))
            environment_found = true;
    if (!environment_found && (environment_settings.environment_enabled || environment_settings.skybox))
        ++renderer->missing_textures;
    if (strcmp(renderer->active_environment, environment_settings.environment_asset)) {
        if (!af_renderer_wait_idle(renderer))
            return false;
        snprintf(renderer->active_environment, sizeof(renderer->active_environment), "%s",
                 environment_settings.environment_asset);
        af_r_update_environment_descriptors(renderer);
    }
    if (environment_settings.anti_aliasing == AF_AA_TAA && !renderer->temporal_supported)
        environment_settings.anti_aliasing = AF_AA_FXAA;
    if (environment_settings.motion_blur && !renderer->temporal_supported)
        environment_settings.motion_blur = false;
    af_frame_uniform uniform;
    af_shadow_view shadow_view;
    if (!af_r_prepare_morphs(renderer, scene) ||
        !prepare_shadows(renderer, scene, camera, af_r_resolve_viewport(renderer, viewport_rect), &uniform, &shadow_view))
        return false;
    af_frame *frame = &renderer->frames[renderer->frame_index];
    Uint64 wait_start = SDL_GetPerformanceCounter();
    if (!af_r_vk_ok(vkWaitForFences(renderer->device, 1, &frame->fence, VK_TRUE, UINT64_MAX), "vkWaitForFences"))
        return false;
    renderer->cpu_fence_ms =
        (double)(SDL_GetPerformanceCounter() - wait_start) * 1000 / (double)SDL_GetPerformanceFrequency();
    af_cluster_view cluster_camera = af_r_cluster_view(&uniform, camera);
    renderer->cluster_stats = (af_cluster_stats){0};
    af_r_update_cluster_pages(renderer);
    if (!af_r_stream_prepare(renderer))
        return false;
    if (renderer->morph_vertex_count &&
        !af_r_stream_buffer(renderer, &frame->morph_buffer, renderer->morph_vertices,
                       renderer->morph_vertex_count * sizeof(af_vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT))
        return false;
    bool ray_shadows = (resource_config & AF_RESOURCE_RAY_SHADOWS) != 0;
    bool ray_reflections = (resource_config & AF_RESOURCE_RAY_REFLECTIONS) != 0;
    bool ray_gi = (resource_config & AF_RESOURCE_RAY_GI) != 0;
    bool ray_material_effects = (resource_config & AF_RESOURCE_RAY_MATERIALS) != 0;
    uniform.sky_params.z = ray_shadows ? 1.f : 0.f;
    uniform.sky_params.w = ray_reflections ? 1.f : 0.f;
    af_directional_light sun = af_scene_get_light(scene);
    uniform.ray_params = (af_vec4){ray_material_effects && !renderer->ray_failed ? 1.0f : 0.0f,
                                   sun.angular_radius, 4.0f, (float)environment_settings.rtgi_bounces};
    uniform.rtgi_params = (af_vec4){
        ray_gi && !renderer->ray_failed ? 1.0f : 0.0f,
        environment_settings.rtgi_intensity,
        environment_settings.rtgi_ray_distance,
        (float)environment_settings.rtgi_samples,
    };
    uniform.rtgi_flags = (af_vec4){environment_settings.rtgi_emissive ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    uint64_t emitter_signature = UINT64_C(14695981039346656037);
    if ((ray_shadows || ray_reflections || ray_gi || ray_material_effects) &&
        !prepare_ray_scene(renderer, frame, scene, ray_shadows, ray_reflections, ray_gi, ray_material_effects,
                           cluster_camera, ray_gi && environment_settings.rtgi_emissive ? &emitter_signature : NULL)) {
        af_log("warning", "Ray-query initialization/build preparation failed; using raster and environment fallbacks.");
        renderer->ray_failed = true;
        renderer->cluster_stats.ray_fine_instances = 0;
        renderer->resize_pending = true;
        return true;
    }
    if (frame->timestamp_submitted) {
        uint64_t values[5];
        VkResult query = vkGetQueryPoolResults(renderer->device, frame->timestamps, 0, 5, sizeof(values), values,
                                               sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
        if (query == VK_SUCCESS) {
            uint64_t mask =
                renderer->timestamp_bits == 64 ? UINT64_MAX : ((UINT64_C(1) << renderer->timestamp_bits) - 1);
            double scale = (double)renderer->timestamp_period / 1000000;
            renderer->gpu_ms[0] = (double)((values[4] - values[0]) & mask) * scale;
            for (unsigned i = 1; i < 5; ++i)
                renderer->gpu_ms[i] = (double)((values[i] - values[i - 1]) & mask) * scale;
            renderer->timing_valid = true;
        } else if (query != VK_NOT_READY)
            return af_r_vk_ok(query, "timestamp results");
    }
    void *skin_mapped = frame->skin_buffer.alloc.mapped;
    if (!skin_mapped)
        return false;
    af_mat4 *skin_destination = skin_mapped;
    skin_destination[0] = af_mat4_identity();
    for (size_t i = 0; i < renderer->skin_count; ++i)
        for (unsigned j = 0; j < AF_ANIMATION_MAX_JOINTS; ++j)
            skin_destination[1 + i * AF_ANIMATION_MAX_JOINTS + j] =
                j < renderer->skins[i].count ? renderer->skins[i].matrices[j] : af_mat4_identity();
    skin_destination[AF_SKIN_MATRICES] = af_mat4_identity();
    for (size_t i = 0; i < renderer->previous_skin_count; ++i)
        for (unsigned j = 0; j < AF_ANIMATION_MAX_JOINTS; ++j)
            skin_destination[AF_SKIN_MATRICES + 1 + i * AF_ANIMATION_MAX_JOINTS + j] =
                j < renderer->previous_skins[i].count ? renderer->previous_skins[i].matrices[j] : af_mat4_identity();
    if (!af_vk_flush(renderer->allocator, &frame->skin_buffer.alloc, 0, VK_WHOLE_SIZE))
        return false;
    uint32_t image_index = 0;
    wait_start = SDL_GetPerformanceCounter();
    VkResult result = vkAcquireNextImageKHR(renderer->device, renderer->swapchain.handle, UINT64_MAX, frame->acquired,
                                            VK_NULL_HANDLE, &image_index);
    renderer->cpu_acquire_ms =
        (double)(SDL_GetPerformanceCounter() - wait_start) * 1000 / (double)SDL_GetPerformanceFrequency();
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        renderer->resize_pending = renderer->swapchain_invalid = true;
        return true;
    }
    if (result == VK_SUBOPTIMAL_KHR)
        renderer->resize_pending = renderer->swapchain_invalid = true;
    else if (!af_r_vk_ok(result, "vkAcquireNextImageKHR"))
        return false;
    bool temporal =
        renderer->temporal_supported &&
        (upscale || environment_settings.anti_aliasing == AF_AA_TAA || environment_settings.motion_blur ||
         (resource_config & AF_RESOURCE_RAY_GI) != 0 || (renderer->capture_pending && renderer->capture_motion));
    bool spatial = upscale && renderer->upscaler->struct_size >= sizeof(*renderer->upscaler) &&
                   (renderer->upscaler->flags & AF_VULKAN_UPSCALER_SPATIAL);
    bool jitter = (upscale && !spatial) || environment_settings.anti_aliasing == AF_AA_TAA;
    size_t object_count = af_scene_object_count(scene);
    if (temporal && object_count > AF_SCENE_FILE_MAX_OBJECTS) {
        af_log("error", "Temporal rendering supports at most %u objects.", AF_SCENE_FILE_MAX_OBJECTS);
        return false;
    }
    size_t record_count = temporal ? (object_count ? object_count : 1) : 0;
    bool batch_props =
        !renderer->prop_instancing_disabled && object_count > 1 && object_count <= AF_SCENE_FILE_MAX_OBJECTS;
    if (batch_props && !reserve_draw_records(renderer, frame, record_count + object_count))
        batch_props = false;
    if (!batch_props && !reserve_draw_records(renderer, frame, record_count))
        return false;
    uint64_t local_light_signature = UINT64_C(14695981039346656037);
    if (!prepare_temporal(renderer, scene, camera, af_r_resolve_viewport(renderer, viewport_rect), color,
                          environment_settings, temporal, jitter, &uniform, ray_gi ? &local_light_signature : NULL))
        return false;
    bool gi_lighting_changed = ray_gi && emitter_signature != renderer->gi_emitter_signature;
    bool color_history_changed =
        gi_lighting_changed || (ray_gi && local_light_signature != renderer->temporal_local_light_signature);
    if (color_history_changed)
        af_vk_post_invalidate_lighting_history(renderer->swapchain.post, gi_lighting_changed);
    /* Motion vectors are generated from unjittered clip coordinates, while GI
       history is stored in the jittered render-target pixel grid. Convert the
       current pixel to the previous frame's grid during history reprojection. */
    af_vec2 gi_jitter_delta = {0};
    if (renderer->temporal_valid) {
        gi_jitter_delta.x = renderer->previous_temporal_jitter.x - renderer->temporal_jitter.x;
        gi_jitter_delta.y = renderer->previous_temporal_jitter.y - renderer->temporal_jitter.y;
    }
    VkDescriptorImageInfo refraction_images[] = {af_vk_refraction_color(renderer->swapchain.refraction, image_index),
                                                 af_vk_refraction_depth(renderer->swapchain.refraction, image_index)};
    VkWriteDescriptorSet water_writes[2];
    for (unsigned i = 0; i < 2; ++i)
        water_writes[i] = (VkWriteDescriptorSet){.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                 .dstSet = frame->descriptor,
                                                 .dstBinding = 7 + i,
                                                 .descriptorCount = 1,
                                                 .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                 .pImageInfo = &refraction_images[i]};
    vkUpdateDescriptorSets(renderer->device, 2, water_writes, 0, NULL);
    if (overlay->index_count && (!renderer->ui || !af_vk_ui_prepare(renderer->ui, renderer->frame_index, overlay)))
        return false;
    if (!bin_and_upload_local_lights(renderer, frame, camera, &uniform))
        return false;
    void *mapped = frame->uniform.alloc.mapped;
    if (!mapped)
        return false;
    memcpy(mapped, &uniform, sizeof(uniform));
    if (!af_vk_flush(renderer->allocator, &frame->uniform.alloc, 0, sizeof(uniform)))
        return false;
    if (!af_r_vk_ok(vkResetCommandBuffer(frame->command, 0), "vkResetCommandBuffer"))
        return false;
    VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    Uint64 record_start = SDL_GetPerformanceCounter();
    if (!af_r_vk_ok(vkBeginCommandBuffer(frame->command, &begin), "vkBeginCommandBuffer"))
        return false;
    af_vk_refraction_prepare(renderer->swapchain.refraction, frame->command, image_index);
    if (renderer->ui)
        af_vk_ui_record_uploads(renderer->ui, frame->command, renderer->frame_index);
    if (frame->timestamps) {
        vkCmdResetQueryPool(frame->command, frame->timestamps, 0, 5);
        vkCmdWriteTimestamp(frame->command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, frame->timestamps, 0);
    }
    if (resource_config & AF_RESOURCE_RAY_QUERY)
        af_vk_ray_query_record(renderer->rays, renderer->frame_index, frame->command);
    renderer->shadow_draw_calls = 0;
    renderer->shadow_passes = renderer->shadow_cached_passes = 0;
    for (uint32_t layer = 0; layer < shadow_view.count; ++layer) {
        uint64_t signature = shadow_signature(renderer, shadow_view.matrices[layer], af_scene_get_shadows(scene),
                                              uniform.shadow_params.x > .5f, cluster_camera);
        signature = af_r_hash(signature, &resource_config, sizeof(resource_config));
        if (frame->shadow_signatures[layer] == signature) {
            ++renderer->shadow_cached_passes;
            continue;
        }
        frame->shadow_signatures[layer] = signature;
        ++renderer->shadow_passes;
        af_vk_shadows_begin(renderer->shadows, frame->command, renderer->frame_index, layer,
                            af_scene_get_shadows(scene), frame->descriptor);
        if (uniform.shadow_params.x > .5f)
            draw_shadow_casters(renderer, frame, scene, renderer->shadows, shadow_view.matrices[layer], cluster_camera,
                                &renderer->cluster_stats.shadow_fine_draws);
        vkCmdEndRenderPass(frame->command);
    }
    for (uint32_t layer = 0; layer < renderer->local_shadow_layers; ++layer) {
        af_shadow_settings bias = af_scene_get_shadows(scene);
        bias.constant_bias = 1;
        bias.slope_bias = 1.5f;
        uint64_t signature = shadow_signature(renderer, uniform.local_shadow_matrices[layer], bias,
                                              layer < (uint32_t)uniform.light_count.z, cluster_camera);
        if (frame->local_shadow_signatures[layer] == signature) {
            ++renderer->shadow_cached_passes;
            continue;
        }
        frame->local_shadow_signatures[layer] = signature;
        ++renderer->shadow_passes;
        af_vk_shadows_begin(renderer->local_shadows, frame->command, renderer->frame_index, layer, bias,
                            frame->descriptor);
        if (layer < (uint32_t)uniform.light_count.z)
            draw_shadow_casters(renderer, frame, scene, renderer->local_shadows, uniform.local_shadow_matrices[layer],
                                cluster_camera, &renderer->cluster_stats.local_shadow_fine_draws);
        vkCmdEndRenderPass(frame->command);
    }
    if (frame->timestamps)
        vkCmdWriteTimestamp(frame->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame->timestamps, 1);
    VkClearValue clears[] = {{.color = {.float32 = {color.r, color.g, color.b, color.a}}},
                             {.depthStencil = {.depth = 1.0f}},
                             {.color = {.float32 = {0, 0, -1, 65504}}},
                             {.color = {.float32 = {0, 0, 0, 0}}}};
    VkRenderPassBeginInfo pass = {.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
                                  .renderPass = renderer->swapchain.render_pass,
                                  .framebuffer = af_vk_post_scene_framebuffer(renderer->swapchain.post, image_index),
                                  .renderArea = {.extent = renderer->swapchain.render_extent},
                                  .clearValueCount = (renderer->swapchain.resource_config & AF_RESOURCE_RAY_GI)   ? 4u
                                                     : (renderer->swapchain.resource_config & AF_RESOURCE_MOTION) ? 3u
                                                                                                                  : 2u,
                                  .pClearValues = clears};
    af_mat4 visibility_projection = cluster_camera.projection;
    float cluster_pixel_scale = cluster_camera.pixel_scale;
    size_t draw_calls = 0, missing_meshes = 0;
    renderer->visible_objects = renderer->culled_objects = 0;
    bool cluster_indirect = af_r_prepare_cluster_commands(renderer, frame, scene, visibility_projection,
                                                     cluster_pixel_scale, object_count);
    vkCmdBeginRenderPass(frame->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
    bool gi_denoised = false;
    const af_landscape *scene_ls = scene ? af_scene_get_landscape(scene) : NULL;
    bool has_landscape = scene_ls && scene_ls->enabled && scene_ls->chunk_count > 0;
    if (object_count > 0 || environment_settings.skybox || has_landscape) {
        VkExtent2D extent = renderer->swapchain.render_extent;
        int logical_width = 0, logical_height = 0;
        SDL_GetWindowSize(renderer->window, &logical_width, &logical_height);
        af_rect area = af_r_resolve_viewport(renderer, viewport_rect);
        float sx = (float)extent.width / (float)(logical_width > 0 ? logical_width : 1);
        float sy = (float)extent.height / (float)(logical_height > 0 ? logical_height : 1);
        VkViewport viewport = {
            .x = area.x * sx, .y = area.y * sy, .width = area.width * sx, .height = area.height * sy, .maxDepth = 1.0f};
        VkRect2D scissor = {.offset = {(int32_t)viewport.x, (int32_t)viewport.y},
                            .extent = {(uint32_t)viewport.width, (uint32_t)viewport.height}};
        VkDeviceSize offset = 0;
        if (environment_settings.skybox) {
            vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain.sky_pipeline);
            vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    renderer->swapchain.pipeline_layout, 0, 1, &frame->descriptor, 0, NULL);
            vkCmdSetViewport(frame->command, 0, 1, &viewport);
            vkCmdSetScissor(frame->command, 0, 1, &scissor);
            vkCmdDraw(frame->command, 3, 1, 0, 0);
        }
        vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain.pipeline);
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain.pipeline_layout, 0,
                                1, &frame->descriptor, 0, NULL);
        VkDescriptorSet shadow_descriptor = af_vk_shadows_descriptor(renderer->shadows, renderer->frame_index);
        vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain.pipeline_layout, 2,
                                1, &shadow_descriptor, 0, NULL);
        vkCmdSetViewport(frame->command, 0, 1, &viewport);
        vkCmdSetScissor(frame->command, 0, 1, &scissor);
        af_blend_draw *blended = object_count ? malloc(object_count * sizeof(*blended)) : NULL;
        size_t blend_count = 0;
        if (object_count && !blended)
            return false;
        for (size_t i = 0; i < object_count; ++i) {
            const af_scene_object *o = af_scene_object_at(scene, i);
            if (o->material.alpha_mode != AF_ALPHA_BLEND && !af_scene_get_water(scene, o->id))
                continue;
            af_mat4 world;
            if (!af_r_object_world(renderer, scene, i, &world))
                continue;
            af_vec3 position = {world.elements[12], world.elements[13], world.elements[14]};
            blended[blend_count++] =
                (af_blend_draw){i, af_vec3_dot(af_vec3_sub(position, camera->position), af_camera_forward(camera))};
        }
        if (blend_count > 1)
            qsort(blended, blend_count, sizeof(*blended), blend_compare);
        bool refract = environment_settings.refraction && environment_settings.refraction_strength > 0 &&
                       af_scene_water_count(scene) > 0;
        af_rect post_norm_area = area;
        post_norm_area.x /= (float)(logical_width ? logical_width : 1);
        post_norm_area.y /= (float)(logical_height ? logical_height : 1);
        post_norm_area.width /= (float)(logical_width ? logical_width : 1);
        post_norm_area.height /= (float)(logical_height ? logical_height : 1);
        for (unsigned alpha_pass = 0; alpha_pass < 2; ++alpha_pass) {
            if (alpha_pass) {
                if (blend_count == 0)
                    break;
                if (((renderer->swapchain.resource_config & AF_RESOURCE_RAY_GI) && !gi_denoised) || refract) {
                    vkCmdEndRenderPass(frame->command);
                    if ((renderer->swapchain.resource_config & AF_RESOURCE_RAY_GI) && !gi_denoised) {
                        af_vk_post_denoise_gi(renderer->swapchain.post, frame->command, image_index, environment_settings,
                                              post_norm_area, gi_jitter_delta);
                        gi_denoised = true;
                    }
                    if (refract)
                        af_vk_refraction_copy(renderer->swapchain.refraction, frame->command, image_index,
                                              af_vk_post_scene_image(renderer->swapchain.post, image_index),
                                              renderer->swapchain.depths[image_index].image);
                    pass.renderPass = renderer->swapchain.water_pass;
                    vkCmdBeginRenderPass(frame->command, &pass, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdSetViewport(frame->command, 0, 1, &viewport);
                    vkCmdSetScissor(frame->command, 0, 1, &scissor);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 0, 1, &frame->descriptor, 0, NULL);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 2, 1, &shadow_descriptor, 0, NULL);
                }
            }
            vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              alpha_pass ? renderer->swapchain.blend_pipeline : renderer->swapchain.pipeline);
            af_prop_batch batch = {0};
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
                size_t index = af_r_find_mesh_index(renderer, water ? water_mesh : object->mesh);
                if (index == renderer->mesh_count) {
                    if (alpha_pass || object->material.alpha_mode != AF_ALPHA_BLEND)
                        ++missing_meshes;
                    continue;
                }
                const af_mesh_asset *asset = &renderer->meshes[index];
                const af_gpu_mesh *mesh = asset->dynamic ? &asset->streams[renderer->frame_index].gpu : &asset->gpu;
                bool blend = object->material.alpha_mode == AF_ALPHA_BLEND;
                bool split_blend = blend && mesh->sidedness != AF_MESH_SIDES_DEFAULT;
                if (!alpha_pass && blend && !split_blend)
                    continue;
                bool count_object = alpha_pass || !split_blend;
                const af_morph_draw *morph = af_r_morph_draw_for(renderer, scene, i);
                af_mat4 world;
                if (!af_r_object_world(renderer, scene, i, &world))
                    continue;
                if (!renderer->culling_disabled) {
                    af_shadow_bounds bounds = af_r_object_bounds(renderer, object->id, world, mesh, morph);
                    if (!af_shadow_intersects(visibility_projection, &bounds)) {
                        if (count_object)
                            ++renderer->culled_objects;
                        continue;
                    }
                }
                if (count_object)
                    ++renderer->visible_objects;
                uint32_t draw_flags = mesh_face_flags(mesh->sidedness, world);
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
                                    object->material.normal_strength, (float)af_r_skin_base(renderer, object->id, mesh)},
                        .emission = {object->material.emission.x, object->material.emission.y,
                                     object->material.emission.z, object->material.emission_strength},
                    }};
                /* Keep the original depth-test order for coplanar surfaces. Culled
                   objects don't break a run of compatible submitted draws. */
                bool clustered = !renderer->clusters_disabled && !alpha_pass && !water && !morph &&
                                 mesh->cluster_count && object->material.alpha_mode == AF_ALPHA_OPAQUE;
                bool can_batch = !clustered && batch_props && !alpha_pass && !asset->dynamic && !mesh->skinned && !morph &&
                                 object->material.alpha_mode == AF_ALPHA_OPAQUE && object->id != highlighted;
                if (can_batch) {
                    if (!prop_batch_matches(&batch, mesh, &object->material, &transform)) {
                        draw_prop_batch(renderer, frame, &batch, &draw_calls);
                        batch = (af_prop_batch){mesh, &object->material, transform, (uint32_t)i, 0, 1};
                    } else {
                        if (batch.count == 1) {
                            batch.first_record = (uint32_t)record_count;
                            renderer->motion_records[record_count++] =
                                (af_motion_record){batch.draw.model, {batch.first_object}};
                        }
                        renderer->motion_records[record_count++] = (af_motion_record){world, {(uint32_t)i}};
                        ++batch.count;
                    }
                    continue;
                }
                draw_prop_batch(renderer, frame, &batch, &draw_calls);
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
                        wa_contains(ph_water_runtime(renderer->water_physics), scene, water->owner, camera->position))
                        transform.material.params.x += (float)(1u << 23u);
                    transform.material.surface =
                        (af_vec4){water->roughness, water->ripple_scale,
                                  water->mode == AF_WATER_PARTICLES ? water->particle_spacing : water->depth,
                                  water->mode == AF_WATER_PARTICLES ? 0 : water->ripple_strength};
                    transform.material.emission =
                        (af_vec4){water->absorption.x, water->absorption.y, water->absorption.z, water->ior};
                }
                bind_scene_material(renderer, frame, &object->material, count_object ? 1 : 0);
                vkCmdPushConstants(frame->command, renderer->swapchain.pipeline_layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(transform),
                                   &transform);
                af_r_bind_object_mesh(frame, mesh, morph);
                if (water) {
                    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      renderer->swapchain.water_attenuation);
                    transform.material.params.x += (float)(1u << 20u);
                    vkCmdPushConstants(frame->command, renderer->swapchain.pipeline_layout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(transform),
                                       &transform);
                    vkCmdDrawIndexed(frame->command, mesh->index_count, 1, 0, 0, (uint32_t)i);
                    ++draw_calls;
                    transform.material.params.x -= (float)(1u << 20u);
                    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      renderer->swapchain.water_scattering);
                    vkCmdPushConstants(frame->command, renderer->swapchain.pipeline_layout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(transform),
                                       &transform);
                } else
                    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      af_r_material_pipeline(renderer, object->material.shader, alpha_pass != 0));
                if (clustered && cluster_indirect && renderer->cluster_spans[i].count)
                    draw_calls += af_r_draw_indirect_clusters(renderer, frame, mesh, renderer->cluster_spans[i]);
                else if (clustered)
                    draw_calls += af_r_draw_clusters(renderer, frame, mesh, transform, visibility_projection, cluster_pixel_scale, (uint32_t)i);
                else {
                    vkCmdDrawIndexed(frame->command, mesh->index_count, 1, 0, 0, (uint32_t)i);
                    ++draw_calls;
                }
            }

            draw_prop_batch(renderer, frame, &batch, &draw_calls);

            if (alpha_pass == 0 && scene_ls && scene_ls->enabled) {
                const af_landscape *ls = scene_ls;
                vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->swapchain.pipeline);
                for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
                    const af_terrain_chunk *chunk = &ls->chunks[c_idx];
                    size_t index = af_r_find_mesh_index(renderer, chunk->mesh_name);
                    if (index == renderer->mesh_count)
                        continue;
                    const af_mesh_asset *asset = &renderer->meshes[index];
                    const af_gpu_mesh *mesh = asset->dynamic ? &asset->streams[renderer->frame_index].gpu : &asset->gpu;
                    if (!mesh->vertices.handle || !mesh->indices.handle)
                        continue;

                    if (!renderer->culling_disabled) {
                        af_shadow_bounds bounds =
                            af_shadow_transform_bounds(af_mat4_identity(), chunk->bounds_min, chunk->bounds_max);
                        if (!af_shadow_intersects(visibility_projection, &bounds)) {
                            ++renderer->culled_objects;
                            continue;
                        }
                    }
                    ++renderer->visible_objects;

                    /* Dominant layer of this chunk determines texture and surface bindings */
                    uint64_t sum_weights[AF_LANDSCAPE_MAX_LAYERS] = {0};
                    for (size_t v_i = 0; v_i < AF_LANDSCAPE_CHUNK_VERTICES * AF_LANDSCAPE_CHUNK_VERTICES; ++v_i) {
                        sum_weights[0] += chunk->weights[v_i * 4 + 0];
                        sum_weights[1] += chunk->weights[v_i * 4 + 1];
                        sum_weights[2] += chunk->weights[v_i * 4 + 2];
                        sum_weights[3] += chunk->weights[v_i * 4 + 3];
                    }
                    size_t dominant = 0;
                    for (size_t l_i = 1; l_i < AF_LANDSCAPE_MAX_LAYERS; ++l_i) {
                        if (sum_weights[l_i] > sum_weights[dominant])
                            dominant = l_i;
                    }

                    af_vk_texture *diffuse_tex = renderer->white;
                    if (ls->layers[dominant].diffuse_texture[0] != '\0') {
                        for (size_t t = 0; t < renderer->texture_count; ++t) {
                            if (!renderer->textures[t].normal &&
                                !strcmp(renderer->textures[t].name, ls->layers[dominant].diffuse_texture)) {
                                diffuse_tex = renderer->textures[t].texture;
                                break;
                            }
                        }
                    }

                    af_vk_texture *normal_tex = renderer->flat_normal;
                    bool has_normal = false;
                    if (ls->layers[dominant].normal_texture[0] != '\0') {
                        for (size_t t = 0; t < renderer->texture_count; ++t) {
                            if (renderer->textures[t].normal &&
                                !strcmp(renderer->textures[t].name, ls->layers[dominant].normal_texture)) {
                                normal_tex = renderer->textures[t].texture;
                                has_normal = true;
                                break;
                            }
                        }
                    }

                    float roughness = ls->layers[dominant].roughness;
                    float metalness = ls->layers[dominant].metalness;

                    struct {
                        af_mat4 model;
                        af_color tint;
                        af_vec4 params, surface, emission;
                    } chunk_transform = {
                        .model = af_mat4_identity(),
                        .tint = {1.0f, 1.0f, 1.0f, 1.0f},
                        .params = {(float)(((uint32_t)AF_ALPHA_OPAQUE << 1u) | (has_normal ? (1u << 23u) : 0u)),
                                   1.0f, 1.0f, 1.45f},
                        .surface = {roughness, metalness, 1.0f, 0.0f},
                        .emission = {0.0f, 0.0f, 0.0f, 0.0f}};
                    VkDescriptorSet desc_diff = af_vk_texture_descriptor(diffuse_tex);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 1, 1, &desc_diff, 0, NULL);
                    VkDescriptorSet desc_norm = af_vk_texture_descriptor(normal_tex);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 3, 1, &desc_norm, 0, NULL);
                    VkDescriptorSet desc_white = af_vk_texture_descriptor(renderer->white);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 4, 1, &desc_white, 0, NULL);
                    vkCmdPushConstants(frame->command, renderer->swapchain.pipeline_layout,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(chunk_transform), &chunk_transform);
                    vkCmdBindVertexBuffers(frame->command, 0, 1, &mesh->vertices.handle, &offset);
                    vkCmdBindIndexBuffer(frame->command, mesh->indices.handle, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(frame->command, mesh->index_count, 1, 0, 0, 0);
                    ++draw_calls;
                }
            }

            if (scene_ls && scene_ls->enabled) {
                const af_landscape *ls = scene_ls;
                for (size_t l = 0; l < ls->foliage_layer_count; ++l) {
                    const af_foliage_layer *fl = &ls->foliage_layers[l];
                    if (!fl->visible || fl->total_instances == 0 || fl->mesh[0] == '\0')
                        continue;
                    bool is_blend = fl->material.alpha_mode == AF_ALPHA_BLEND;
                    if ((alpha_pass == 0 && is_blend) || (alpha_pass == 1 && !is_blend))
                        continue;

                    size_t index = af_r_find_mesh_index(renderer, fl->mesh);
                    if (index == renderer->mesh_count) {
                        ++missing_meshes;
                        continue;
                    }
                    const af_mesh_asset *asset = &renderer->meshes[index];
                    const af_gpu_mesh *mesh = asset->dynamic ? &asset->streams[renderer->frame_index].gpu : &asset->gpu;
                    if (!mesh->vertices.handle || !mesh->indices.handle)
                        continue;

                    vkCmdBindPipeline(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      af_r_material_pipeline(renderer, fl->material.shader, alpha_pass != 0));
                    vkCmdBindVertexBuffers(frame->command, 0, 1, &mesh->vertices.handle, &offset);
                    vkCmdBindIndexBuffer(frame->command, mesh->indices.handle, 0, VK_INDEX_TYPE_UINT32);

                    af_vk_texture *tex = renderer->white;
                    if (fl->material.texture[0]) {
                        for (size_t t = 0; t < renderer->texture_count; ++t) {
                            if (!renderer->textures[t].normal &&
                                !strcmp(renderer->textures[t].name, fl->material.texture)) {
                                tex = renderer->textures[t].texture;
                                break;
                            }
                        }
                    }
                    VkDescriptorSet desc_tex = af_vk_texture_descriptor(tex);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 1, 1, &desc_tex, 0, NULL);

                    af_vk_texture *norm = renderer->flat_normal;
                    if (fl->material.normal_map[0]) {
                        for (size_t t = 0; t < renderer->texture_count; ++t) {
                            if (renderer->textures[t].normal &&
                                !strcmp(renderer->textures[t].name, fl->material.normal_map)) {
                                norm = renderer->textures[t].texture;
                                break;
                            }
                        }
                    }
                    VkDescriptorSet desc_norm = af_vk_texture_descriptor(norm);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 3, 1, &desc_norm, 0, NULL);

                    af_vk_texture *mr = renderer->white;
                    if (fl->material.metallic_roughness_map[0]) {
                        for (size_t t = 0; t < renderer->texture_count; ++t) {
                            if (!renderer->textures[t].normal &&
                                !strcmp(renderer->textures[t].name, fl->material.metallic_roughness_map)) {
                                mr = renderer->textures[t].texture;
                                break;
                            }
                        }
                    }
                    VkDescriptorSet desc_mr = af_vk_texture_descriptor(mr);
                    vkCmdBindDescriptorSets(frame->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            renderer->swapchain.pipeline_layout, 4, 1, &desc_mr, 0, NULL);

                    float max_dist_sq = fl->max_distance * fl->max_distance;

                    for (size_t c_idx = 0; c_idx < ls->chunk_count; ++c_idx) {
                        const af_terrain_chunk *chunk = &ls->chunks[c_idx];
                        const af_foliage_chunk *fc = &chunk->foliage[l];
                        if (fc->count == 0)
                            continue;

                        af_vec3 chunk_center = {(chunk->bounds_min.x + chunk->bounds_max.x) * 0.5f,
                                                (chunk->bounds_min.y + chunk->bounds_max.y) * 0.5f,
                                                (chunk->bounds_min.z + chunk->bounds_max.z) * 0.5f};
                        af_vec3 to_cam = af_vec3_sub(chunk_center, camera->position);
                        if (af_vec3_dot(to_cam, to_cam) > max_dist_sq)
                            continue;

                        if (!renderer->culling_disabled) {
                            af_vec3 ext_max = chunk->bounds_max;
                            ext_max.y += 10.0f;
                            af_shadow_bounds bounds =
                                af_shadow_transform_bounds(af_mat4_identity(), chunk->bounds_min, ext_max);
                            if (!af_shadow_intersects(visibility_projection, &bounds)) {
                                renderer->culled_objects += fc->count;
                                continue;
                            }
                        }

                        for (size_t inst_idx = 0; inst_idx < fc->count; ++inst_idx) {
                            af_mat4 model = af_transform_matrix(fc->instances[inst_idx].transform);
                            uint32_t draw_flags = mesh_face_flags(mesh->sidedness, model);
                            if (fl->material.receive_shadows)
                                draw_flags |= AF_DRAW_RECEIVE_SHADOWS;
                            struct {
                                af_mat4 model;
                                af_color tint;
                                af_vec4 params, surface, emission;
                            } inst_transform = {
                                .model = model,
                                .tint = fl->material.base_color,
                                .params = {(float)((fl->material.unlit ? 1u : 0u) |
                                                   ((uint32_t)fl->material.alpha_mode << 1u) |
                                                   ((uint32_t)lroundf(fl->material.alpha_cutoff * 65535.0f) << 3u) |
                                                   (fl->material.normal_map[0] ? (1u << 23u) : 0u)),
                                           (float)draw_flags,
                                           fl->material.physically_based ? 1.0f : 0.0f, fl->material.ior},
                                .surface = {fl->material.roughness, fl->material.metallic, fl->material.normal_strength,
                                            0.0f},
                                .emission = {fl->material.emission.x, fl->material.emission.y, fl->material.emission.z,
                                             fl->material.emission_strength}};
                            vkCmdPushConstants(frame->command, renderer->swapchain.pipeline_layout,
                                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                               sizeof(inst_transform), &inst_transform);
                            vkCmdDrawIndexed(frame->command, mesh->index_count, 1, 0, 0, 0);
                            ++draw_calls;
                            ++renderer->visible_objects;
                        }
                    }
                }
            }
        }
        free(blended);
    }
    vkCmdEndRenderPass(frame->command);
    int logical_w = 0, logical_h = 0;
    SDL_GetWindowSize(renderer->window, &logical_w, &logical_h);
    af_rect post_area = af_r_resolve_viewport(renderer, viewport_rect);
    post_area.x /= (float)(logical_w ? logical_w : 1);
    post_area.y /= (float)(logical_h ? logical_h : 1);
    post_area.width /= (float)(logical_w ? logical_w : 1);
    post_area.height /= (float)(logical_h ? logical_h : 1);
    if (frame->timestamps)
        vkCmdWriteTimestamp(frame->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame->timestamps, 2);
    if ((renderer->swapchain.resource_config & AF_RESOURCE_RAY_GI) && !gi_denoised)
        af_vk_post_denoise_gi(renderer->swapchain.post, frame->command, image_index, environment_settings, post_area,
                              gi_jitter_delta);
    bool upscaled =
        upscale && af_r_record_upscaler(renderer, frame->command, image_index, post_area, environment_settings.exposure,
                                   camera, frame_time_ms, !renderer->temporal_valid || color_history_changed);
    float time_sec = (float)renderer->rendered_frames * (1.0f / 60.0f);
    float delta_sec = frame_time_ms * 0.001f;
    af_vk_post_draw(renderer->swapchain.post, frame->command, image_index, environment_settings, post_area,
                    renderer->temporal_jitter, renderer->debug_view, upscaled, time_sec, delta_sec);
    if (frame->timestamps)
        vkCmdWriteTimestamp(frame->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame->timestamps, 3);
    if (renderer->ui && overlay->index_count)
        af_vk_ui_draw(renderer->ui, frame->command, renderer->frame_index, overlay, renderer->swapchain.extent);
    vkCmdEndRenderPass(frame->command);
    if (frame->timestamps)
        vkCmdWriteTimestamp(frame->command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, frame->timestamps, 4);
    if (renderer->capture_pending) {
        int w = 0, h = 0;
        SDL_GetWindowSize(renderer->window, &w, &h);
        uint32_t x = (uint32_t)fmaxf(
            0, fminf((float)renderer->swapchain.extent.width - 1,
                     renderer->capture_position.x * (float)renderer->swapchain.extent.width / (float)(w ? w : 1)));
        uint32_t y = (uint32_t)fmaxf(
            0, fminf((float)renderer->swapchain.extent.height - 1,
                     renderer->capture_position.y * (float)renderer->swapchain.extent.height / (float)(h ? h : 1)));
        VkImageLayout capture_layout =
            renderer->capture_motion ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkImageMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                                        .oldLayout = capture_layout,
                                        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .image = renderer->capture_motion
                                                     ? af_vk_post_motion_image(renderer->swapchain.post, image_index)
                                                     : renderer->swapchain.images[image_index],
                                        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        barrier.srcAccessMask |= renderer->capture_motion ? VK_ACCESS_SHADER_READ_BIT : 0;
        vkCmdPipelineBarrier(frame->command,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier);
        renderer->capture_extent.width = renderer->capture_extent.width < renderer->swapchain.extent.width - x
                                             ? renderer->capture_extent.width
                                             : renderer->swapchain.extent.width - x;
        renderer->capture_extent.height = renderer->capture_extent.height < renderer->swapchain.extent.height - y
                                              ? renderer->capture_extent.height
                                              : renderer->swapchain.extent.height - y;
        VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                  .imageOffset = {(int32_t)x, (int32_t)y, 0},
                                  .imageExtent = {renderer->capture_extent.width, renderer->capture_extent.height, 1}};
        vkCmdCopyImageToBuffer(frame->command, barrier.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               renderer->capture_buffer.handle, 1, &copy);
        VkBufferMemoryBarrier host = {.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                                      .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
                                      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                      .buffer = renderer->capture_buffer.handle,
                                      .size = VK_WHOLE_SIZE};
        vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 1,
                             &host, 0, NULL);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = capture_layout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = renderer->capture_motion ? VK_ACCESS_SHADER_READ_BIT : 0;
        vkCmdPipelineBarrier(frame->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             renderer->capture_motion ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                      : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0, 0, NULL, 0, NULL, 1, &barrier);
        renderer->capture_pending = false;
        renderer->capture_ready = true;
        renderer->capture_format =
            renderer->capture_motion ? VK_FORMAT_R16G16B16A16_SFLOAT : renderer->swapchain.surface_format.format;
    }
    if (!upload_draw_records(renderer, frame, record_count) ||
        !af_r_vk_ok(vkEndCommandBuffer(frame->command), "vkEndCommandBuffer"))
        return false;
    renderer->cpu_record_ms =
        (double)(SDL_GetPerformanceCounter() - record_start) * 1000 / (double)SDL_GetPerformanceFrequency();
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                      VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    VkSemaphore ready = renderer->swapchain.present_ready[image_index];
    VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                           .waitSemaphoreCount = 1,
                           .pWaitSemaphores = &frame->acquired,
                           .pWaitDstStageMask = &wait_stage,
                           .commandBufferCount = 1,
                           .pCommandBuffers = &frame->command,
                           .signalSemaphoreCount = 1,
                           .pSignalSemaphores = &ready};
    /* Reset only after acquisition succeeds, so out-of-date frames cannot
       leave an unsignaled fence that hangs the next frame. */
    if (!af_r_vk_ok(vkResetFences(renderer->device, 1, &frame->fence), "vkResetFences") ||
        !af_r_vk_ok(vkQueueSubmit(renderer->graphics_queue, 1, &submit, frame->fence), "vkQueueSubmit"))
        return false;
    renderer->gi_emitter_signature = emitter_signature;
    renderer->temporal_local_light_signature = local_light_signature;
    if (temporal) {
        af_motion_snapshot *spare = renderer->motion_previous;
        size_t spare_capacity = renderer->motion_previous_capacity;
        renderer->motion_previous = renderer->motion_next;
        renderer->motion_previous_capacity = renderer->motion_next_capacity;
        renderer->motion_count = renderer->motion_next_count;
        renderer->motion_next = spare;
        renderer->motion_next_capacity = spare_capacity;
        renderer->previous_view_projection = uniform.unjittered_view_projection;
        renderer->previous_temporal_jitter = renderer->temporal_jitter;
        renderer->previous_skin_count = renderer->skin_count;
        memcpy(renderer->previous_skins, renderer->skins, renderer->skin_count * sizeof(renderer->skins[0]));
        renderer->temporal_valid = true;
        ++renderer->temporal_frame;
    }
    frame->timestamp_submitted = frame->timestamps != VK_NULL_HANDLE;
    renderer->draw_calls = draw_calls;
    if (missing_meshes && missing_meshes != renderer->missing_meshes)
        af_log("warning", "%zu visible scene objects reference unloaded mesh assets.", missing_meshes);
    renderer->missing_meshes = missing_meshes;
    VkPresentInfoKHR present = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &ready,
                                .swapchainCount = 1,
                                .pSwapchains = &renderer->swapchain.handle,
                                .pImageIndices = &image_index};
    result = vkQueuePresentKHR(renderer->present_queue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        renderer->resize_pending = renderer->swapchain_invalid = true;
    else if (!af_r_vk_ok(result, "vkQueuePresentKHR"))
        return false;
    renderer->frame_index = (renderer->frame_index + 1) % AF_FRAMES_IN_FLIGHT;
    if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
        ++renderer->rendered_frames;
        *rendered = true;
    }
    return true;
}


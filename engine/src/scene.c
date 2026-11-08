/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "scene_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool valid_name(const char *name, size_t capacity) {
    return name && name[0] && strlen(name) < capacity;
}

static size_t object_hash(af_object_id id) {
    id ^= id >> 30;
    id *= UINT64_C(0xbf58476d1ce4e5b9);
    id ^= id >> 27;
    id *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(id ^ (id >> 31));
}
static void index_insert(af_scene *scene, size_t index) {
    size_t at = object_hash(scene->objects[index].id) & (scene->object_slot_capacity - 1);
    while (scene->object_slots[at])
        at = (at + 1) & (scene->object_slot_capacity - 1);
    scene->object_slots[at] = index + 1;
}
static bool index_reserve(af_scene *scene, size_t count) {
    if (count > SIZE_MAX / 2)
        return false;
    if (scene->object_slot_capacity >= count * 2)
        return true;
    size_t capacity = 16;
    while (capacity < count * 2) {
        if (capacity > SIZE_MAX / 2)
            return false;
        capacity *= 2;
    }
    size_t *slots = calloc(capacity, sizeof(*slots));
    if (!slots)
        return false;
    free(scene->object_slots);
    scene->object_slots = slots;
    scene->object_slot_capacity = capacity;
    for (size_t i = 0; i < scene->count; ++i)
        index_insert(scene, i);
    return true;
}
bool af_scene_reindex(af_scene *scene) {
    if (!scene || !index_reserve(scene, scene->count))
        return false;
    if (scene->object_slot_capacity) {
        memset(scene->object_slots, 0, scene->object_slot_capacity * sizeof(*scene->object_slots));
        for (size_t i = 0; i < scene->count; ++i)
            index_insert(scene, i);
    }
    return true;
}
static af_scene_object *find_object(const af_scene *scene, af_object_id id) {
    if (!scene || !id)
        return NULL;
    if (scene->object_slot_capacity) {
        size_t at = object_hash(id) & (scene->object_slot_capacity - 1);
        while (scene->object_slots[at]) {
            size_t index = scene->object_slots[at] - 1;
            if (scene->objects[index].id == id)
                return &scene->objects[index];
            at = (at + 1) & (scene->object_slot_capacity - 1);
        }
        return NULL;
    }
    for (size_t i = 0; i < scene->count; ++i)
        if (scene->objects[i].id == id)
            return &scene->objects[i];
    return NULL;
}

af_scene *af_scene_create(void) {
    af_scene *scene = calloc(1, sizeof(*scene));
    if (scene) {
        scene->wind = af_wind_default();
        scene->next_id = 1;
        scene->next_joint_id = 1;
        scene->next_part_id = 1;
        scene->light = af_directional_light_default();
        scene->shadows = af_shadow_settings_default();
        scene->render_settings = af_render_settings_default();
    }
    return scene;
}

void af_scene_destroy(af_scene *scene) {
    if (!scene)
        return;
    for (size_t i = 0; i < scene->blueprint_count; ++i)
        af_blueprint_destroy(scene->blueprints[i]);
    free(scene->blueprints);
    af_landscape_destroy(scene->landscape);
    for (size_t i = 0; i < scene->collision_mesh_count; ++i)
        cm_release(scene->collision_meshes[i].mesh);
    free(scene->object_slots);
    free(scene->objects);
    free(scene->assets);
    free(scene);
}

af_object_id af_scene_add_object(af_scene *scene, const char *name, const char *mesh) {
    if (!scene || !valid_name(name, AF_OBJECT_NAME_CAPACITY) || !valid_name(mesh, AF_MESH_NAME_CAPACITY) ||
        scene->next_id == AF_OBJECT_NONE)
        return AF_OBJECT_NONE;
    /* Copy before realloc: callers may use a name from an existing object. */
    af_scene_object object = {.id = scene->next_id,
                              .transform = af_transform_identity(),
                              .visible = true,
                              .material = af_material_default(),
                              .collider = af_collider_default(),
                              .gameplay = af_gameplay_component_default(),
                              .animation = af_animation_component_default(),
                              .model = af_model_binding_default(),
                              .rig = af_character_rig_default(),
                              .body = af_rigid_body_default()};
    memcpy(object.name, name, strlen(name) + 1);
    memcpy(object.mesh, mesh, strlen(mesh) + 1);
    if (!index_reserve(scene, scene->count + 1))
        return AF_OBJECT_NONE;
    if (scene->count == scene->capacity) {
        if (scene->capacity > SIZE_MAX / 2 / sizeof(*scene->objects))
            return AF_OBJECT_NONE;
        size_t capacity = scene->capacity ? scene->capacity * 2 : 16;
        af_scene_object *objects = realloc(scene->objects, capacity * sizeof(*objects));
        if (!objects)
            return AF_OBJECT_NONE;
        scene->objects = objects;
        scene->capacity = capacity;
    }
    scene->objects[scene->count++] = object;
    index_insert(scene, scene->count - 1);
    ++scene->next_id; /* Unsigned wrap to zero permanently exhausts the ID space. */
    return object.id;
}

bool af_scene_remove_object(af_scene *scene, af_object_id id) {
    af_scene_object *object = find_object(scene, id);
    if (!object)
        return false;
    bool *removed = calloc(scene->count, sizeof(*removed));
    if (!removed)
        return false;
    for (size_t i = 0; i < scene->count; ++i)
        removed[i] = scene->objects[i].id == id || af_scene_is_descendant(scene, scene->objects[i].id, id);
    size_t write = 0;
    for (size_t i = 0; i < scene->count; ++i)
        if (!removed[i])
            scene->objects[write++] = scene->objects[i];
    scene->count = write;
    free(removed);
    (void)af_scene_reindex(scene);
    write = 0;
    for (size_t i = 0; i < scene->water_count; ++i)
        if (find_object(scene, scene->waters[i].owner))
            scene->waters[write++] = scene->waters[i];
    scene->water_count = write;
    write = 0;
    for (size_t i = 0; i < scene->part_count; ++i)
        if (find_object(scene, scene->parts[i].owner))
            scene->parts[write++] = scene->parts[i];
    scene->part_count = write;
    for (size_t i = scene->joint_count; i > 0; --i) {
        af_joint *j = &scene->joints[i - 1];
        if (!find_object(scene, j->a) || (j->b && !find_object(scene, j->b)))
            af_scene_remove_joint(scene, j->id);
    }
    return true;
}

size_t af_scene_object_count(const af_scene *scene) {
    return scene ? scene->count : 0;
}
const af_scene_object *af_scene_get_object(const af_scene *scene, af_object_id id) {
    return find_object(scene, id);
}
const af_scene_object *af_scene_object_at(const af_scene *scene, size_t index) {
    return scene && index < scene->count ? &scene->objects[index] : NULL;
}

bool af_scene_set_object_name(af_scene *scene, af_object_id id, const char *name) {
    af_scene_object *object = find_object(scene, id);
    if (!object || !valid_name(name, AF_OBJECT_NAME_CAPACITY))
        return false;
    memmove(object->name, name, strlen(name) + 1);
    return true;
}

bool af_scene_set_object_mesh(af_scene *scene, af_object_id id, const char *mesh) {
    af_scene_object *object = find_object(scene, id);
    if (!object || !valid_name(mesh, AF_MESH_NAME_CAPACITY) ||
        (af_scene_get_water(scene, id) && strcmp(mesh, "__node")))
        return false;
    memmove(object->mesh, mesh, strlen(mesh) + 1);
    return true;
}

bool af_scene_set_object_transform(af_scene *scene, af_object_id id, af_transform transform) {
    af_scene_object *object = find_object(scene, id);
    af_vec3 vectors[] = {transform.position, transform.rotation, transform.scale};
    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); ++i)
        if (!isfinite(vectors[i].x) || !isfinite(vectors[i].y) || !isfinite(vectors[i].z))
            return false;
    if (!object)
        return false;
    af_transform previous = object->transform;
    object->transform = transform;
    if (!af_scene_set_body(scene, id, object->body)) {
        object->transform = previous;
        return false;
    }
    const af_water *water = af_scene_get_water(scene, id);
    if (water && !af_scene_set_water(scene, id, *water)) {
        object->transform = previous;
        return false;
    }
    object->transform = transform;
    return true;
}

bool af_scene_set_object_visible(af_scene *scene, af_object_id id, bool visible) {
    af_scene_object *object = find_object(scene, id);
    if (!object)
        return false;
    object->visible = visible;
    return true;
}

af_scene *af_scene_clone(const af_scene *source) {
    if (!source)
        return NULL;
    af_scene *copy = af_scene_create();
    if (!copy)
        return NULL;
    memcpy(copy->joints, source->joints, sizeof(copy->joints));
    copy->joint_count = source->joint_count;
    copy->next_joint_id = source->next_joint_id;
    memcpy(copy->waters, source->waters, sizeof(copy->waters));
    copy->water_count = source->water_count;
    memcpy(copy->parts, source->parts, sizeof(copy->parts));
    copy->part_count = source->part_count;
    copy->next_part_id = source->next_part_id;
    copy->wind = source->wind;
    copy->next_id = source->next_id;
    copy->light = source->light;
    copy->shadows = source->shadows;
    copy->render_settings = source->render_settings;
    copy->local_light_count = source->local_light_count;
    copy->landscape = af_landscape_clone(source->landscape);
    if (source->blueprint_count) {
        copy->blueprints = malloc(source->blueprint_count * sizeof(*copy->blueprints));
        if (copy->blueprints) {
            for (size_t i = 0; i < source->blueprint_count; ++i) {
                copy->blueprints[i] = af_blueprint_clone(source->blueprints[i]);
            }
            copy->blueprint_count = copy->blueprint_capacity = source->blueprint_count;
        }
    }
    memcpy(copy->local_lights, source->local_lights, sizeof(copy->local_lights));
    if (source->count) {
        copy->objects = malloc(source->count * sizeof(*copy->objects));
        if (!copy->objects) {
            af_scene_destroy(copy);
            return NULL;
        }
        memcpy(copy->objects, source->objects, source->count * sizeof(*copy->objects));
        copy->count = copy->capacity = source->count;
    }
    if (source->asset_count) {
        copy->assets = malloc(source->asset_count * sizeof(*copy->assets));
        if (!copy->assets) {
            af_scene_destroy(copy);
            return NULL;
        }
        memcpy(copy->assets, source->assets, source->asset_count * sizeof(*copy->assets));
        copy->asset_count = copy->asset_capacity = source->asset_count;
    }
    memcpy(copy->collision_meshes, source->collision_meshes, sizeof(copy->collision_meshes));
    copy->collision_mesh_count = source->collision_mesh_count;
    for (size_t i = 0; i < copy->collision_mesh_count; ++i)
        cm_retain(copy->collision_meshes[i].mesh);
    if (!af_scene_reindex(copy)) {
        af_scene_destroy(copy);
        return NULL;
    }
    return copy;
}

size_t af_scene_asset_count(const af_scene *scene) {
    return scene ? scene->asset_count : 0;
}
const af_scene_asset *af_scene_asset_at(const af_scene *scene, size_t i) {
    return scene && i < scene->asset_count ? &scene->assets[i] : NULL;
}
const af_scene_asset *af_scene_find_asset(const af_scene *scene, const char *name) {
    if (scene && name)
        for (size_t i = 0; i < scene->asset_count; ++i)
            if (!strcmp(scene->assets[i].name, name))
                return &scene->assets[i];
    return NULL;
}
bool af_scene_set_asset(af_scene *scene, const char *name, af_asset_kind kind, const char *path) {
    if (!scene || !valid_name(name, AF_MESH_NAME_CAPACITY) || !valid_name(path, AF_ASSET_PATH_CAPACITY) ||
        (kind != AF_ASSET_MESH && kind != AF_ASSET_TEXTURE && kind != AF_ASSET_NORMAL_MAP && kind != AF_ASSET_AUDIO &&
         kind != AF_ASSET_METALLIC_ROUGHNESS && kind != AF_ASSET_ENVIRONMENT && kind != AF_ASSET_ANIMATION))
        return false;
    af_scene_asset asset = {.kind = kind};
    memcpy(asset.name, name, strlen(name) + 1);
    memcpy(asset.path, path, strlen(path) + 1);
    for (size_t i = 0; i < scene->asset_count; ++i)
        if (!strcmp(scene->assets[i].name, name)) {
            scene->assets[i] = asset;
            return true;
        }
    if (scene->asset_count >= AF_SCENE_MAX_ASSETS)
        return false;
    if (scene->asset_count == scene->asset_capacity) {
        size_t n = scene->asset_capacity ? scene->asset_capacity * 2 : 8;
        af_scene_asset *assets = realloc(scene->assets, n * sizeof(*assets));
        if (!assets)
            return false;
        scene->assets = assets;
        scene->asset_capacity = n;
    }
    scene->assets[scene->asset_count++] = asset;
    return true;
}

af_material af_material_default(void) {
    return (af_material){.alpha_cutoff = .5f,
                         .base_color = {1, 1, 1, 1},
                         .cast_shadows = true,
                         .receive_shadows = true,
                         .roughness = 0.6f,
                         .normal_strength = 1};
}
af_directional_light af_directional_light_default(void) {
    return (af_directional_light){
        .direction = {-0.6f, -1, -0.4f}, .color = {1, 0.95f, 0.86f}, .intensity = 1.2f, .ambient = 0.22f};
}
bool af_scene_set_object_material(af_scene *scene, af_object_id id, af_material material) {
    af_scene_object *object = find_object(scene, id);
    if (!object || !memchr(material.texture, 0, sizeof(material.texture)) ||
        !memchr(material.normal_map, 0, sizeof(material.normal_map)) ||
        !memchr(material.metallic_roughness_map, 0, sizeof(material.metallic_roughness_map)))
        return false;
    if (material.alpha_mode < AF_ALPHA_OPAQUE || material.alpha_mode > AF_ALPHA_BLEND ||
        !isfinite(material.alpha_cutoff) || material.alpha_cutoff < 0 || material.alpha_cutoff > 1)
        return false;
    float values[] = {material.base_color.r, material.base_color.g, material.base_color.b, material.base_color.a};
    for (unsigned i = 0; i < 4; ++i)
        if (!isfinite(values[i]) || values[i] < 0 || values[i] > 1)
            return false;
    if (!isfinite(material.roughness) || material.roughness < 0 || material.roughness > 1 ||
        !isfinite(material.metallic) || material.metallic < 0 || material.metallic > 1)
        return false;
    float emission[] = {material.emission.x, material.emission.y, material.emission.z};
    for (unsigned i = 0; i < 3; ++i)
        if (!isfinite(emission[i]) || emission[i] < 0 || emission[i] > 1)
            return false;
    if (!isfinite(material.emission_strength) || material.emission_strength < 0 || material.emission_strength > 1000)
        return false;
    if (!isfinite(material.normal_strength) || material.normal_strength < 0 || material.normal_strength > 4)
        return false;
    object->material = material;
    return true;
}
bool af_scene_set_light(af_scene *scene, af_directional_light light) {
    float values[] = {light.direction.x, light.direction.y, light.direction.z, light.color.x,
                      light.color.y,     light.color.z,     light.intensity,   light.ambient};
    if (!scene)
        return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!isfinite(values[i]))
            return false;
    if (af_vec3_length(light.direction) < 1e-6f || light.color.x < 0 || light.color.y < 0 || light.color.z < 0 ||
        light.intensity < 0 || light.intensity > 100 || light.ambient < 0 || light.ambient > 10)
        return false;
    scene->light = light;
    return true;
}
af_directional_light af_scene_get_light(const af_scene *scene) {
    return scene ? scene->light : af_directional_light_default();
}

bool af_scene_is_descendant(const af_scene *scene, af_object_id id, af_object_id ancestor) {
    const af_scene_object *object = find_object(scene, id);
    for (unsigned depth = 0; object && object->parent && depth < AF_SCENE_MAX_DEPTH; ++depth) {
        if (object->parent == ancestor)
            return true;
        object = find_object(scene, object->parent);
    }
    return false;
}
bool af_scene_set_parent(af_scene *scene, af_object_id child, af_object_id parent) {
    af_scene_object *object = find_object(scene, child);
    if (!object || (parent && af_scene_get_water(scene, child)) ||
        (parent && object->body.enabled && object->body.type == AF_BODY_DYNAMIC) || child == parent ||
        (parent && !find_object(scene, parent)) || (parent && af_scene_is_descendant(scene, parent, child)))
        return false;
    af_object_id previous = object->parent;
    object->parent = parent;
    for (size_t i = 0; i < scene->count; ++i) {
        const af_scene_object *node = &scene->objects[i];
        unsigned depth = 0;
        while (node->parent) {
            if (++depth >= AF_SCENE_MAX_DEPTH || (node = find_object(scene, node->parent)) == NULL) {
                object->parent = previous;
                return false;
            }
        }
    }
    for (size_t i = 0; i < scene->count; ++i)
        if (scene->objects[i].model.root &&
            !af_scene_is_descendant(scene, scene->objects[i].id, scene->objects[i].model.root))
            scene->objects[i].model = af_model_binding_default();
    return true;
}
bool af_scene_world_matrix(const af_scene *scene, af_object_id id, af_mat4 *out) {
    const af_scene_object *object = find_object(scene, id);
    if (!object || !out)
        return false;
    af_mat4 world = af_transform_matrix(object->transform);
    unsigned depth = 0;
    while (object->parent) {
        if (++depth >= AF_SCENE_MAX_DEPTH || (object = find_object(scene, object->parent)) == NULL)
            return false;
        world = af_mat4_mul(af_transform_matrix(object->transform), world);
    }
    for (unsigned i = 0; i < 16; ++i)
        if (!isfinite(world.elements[i]))
            return false;
    *out = world;
    return true;
}
bool af_scene_object_visible(const af_scene *scene, af_object_id id) {
    const af_scene_object *object = find_object(scene, id);
    unsigned depth = 0;
    while (object) {
        if (!object->visible)
            return false;
        if (!object->parent)
            return true;
        if (++depth >= AF_SCENE_MAX_DEPTH)
            return false;
        object = find_object(scene, object->parent);
    }
    return false;
}

bool af_scene_set_shadows(af_scene *scene, af_shadow_settings settings) {
    if (!scene || !af_shadow_settings_valid(settings))
        return false;
    scene->shadows = settings;
    return true;
}
af_shadow_settings af_scene_get_shadows(const af_scene *scene) {
    return scene ? scene->shadows : af_shadow_settings_default();
}

af_render_settings af_render_settings_default(void) {
    return (af_render_settings){.taa_history_weight = .9f,
                                .aa_sharpness = .1f,
                                .underwater = true,
                                .refraction = true,
                                .refraction_strength = 1.f,
                                .bloom_strength = .12f,
                                .bloom_threshold = 1.f,
                                .bloom_radius = 1.5f,
                                .environment_intensity = 1.f,
                                .skybox_intensity = 1.f,
                                .local_shadow_resolution = 512,
                                .tone_mapper = AF_TONE_MAP_REINHARD,
                                .auto_exposure_speed = 2.0f,
                                .auto_exposure_min = -3.0f,
                                .auto_exposure_max = 3.0f,
                                .saturation = 1.0f,
                                .contrast = 1.0f,
                                .color_filter = {1.0f, 1.0f, 1.0f},
                                .dof_focus_distance = 10.0f,
                                .dof_focus_range = 5.0f,
                                .dof_blur_radius = 0.008f,
                                .motion_blur_strength = 1.0f,
                                .motion_blur_samples = 16,
                                .vignette_intensity = 0.35f,
                                .vignette_smoothness = 0.5f,
                                .chromatic_aberration_strength = 0.005f,
                                .film_grain_intensity = 0.035f,
                                .ray_traced_gi = false,
                                .rtgi_intensity = 1.0f,
                                .rtgi_ray_distance = 25.0f,
                                .rtgi_samples = 1};
}
af_render_settings af_scene_get_render_settings(const af_scene *scene) {
    return scene ? scene->render_settings : af_render_settings_default();
}
bool af_scene_set_render_settings(af_scene *scene, af_render_settings settings) {
    if (settings.anti_aliasing < AF_AA_OFF || settings.anti_aliasing > AF_AA_TAA || !isfinite(settings.aa_sharpness) ||
        settings.aa_sharpness < 0 || settings.aa_sharpness > 1 || !isfinite(settings.taa_history_weight) ||
        settings.taa_history_weight < 0 || settings.taa_history_weight > .98f)
        return false;
    if (!isfinite(settings.refraction_strength) || settings.refraction_strength < 0 || settings.refraction_strength > 2)
        return false;
    if (!scene || !isfinite(settings.exposure) || settings.exposure < -10 || settings.exposure > 10 ||
        !isfinite(settings.bloom_strength) || settings.bloom_strength < 0 || settings.bloom_strength > 5 ||
        !isfinite(settings.bloom_threshold) || settings.bloom_threshold < 0 || settings.bloom_threshold > 100 ||
        !isfinite(settings.bloom_radius) || settings.bloom_radius < 0 || settings.bloom_radius > 4 ||
        settings.environment_profile > 2 ||
        (settings.local_shadow_resolution && settings.local_shadow_resolution != 256 &&
         settings.local_shadow_resolution != 512 && settings.local_shadow_resolution != 1024) ||
        !memchr(settings.environment_asset, 0, sizeof(settings.environment_asset)) ||
        !isfinite(settings.skybox_intensity) || settings.skybox_intensity < 0 || settings.skybox_intensity > 100 ||
        !isfinite(settings.environment_intensity) || settings.environment_intensity < 0 ||
        settings.environment_intensity > 100 || !isfinite(settings.environment_rotation) ||
        fabsf(settings.environment_rotation) > 1000)
        return false;
    if (settings.tone_mapper < AF_TONE_MAP_OFF || settings.tone_mapper > AF_TONE_MAP_AGX)
        return false;
    if (!isfinite(settings.auto_exposure_speed) || settings.auto_exposure_speed < 0 ||
        settings.auto_exposure_speed > 20.0f)
        return false;
    if (!isfinite(settings.auto_exposure_min) || settings.auto_exposure_min < -10.0f ||
        settings.auto_exposure_min > 10.0f)
        return false;
    if (!isfinite(settings.auto_exposure_max) || settings.auto_exposure_max < -10.0f ||
        settings.auto_exposure_max > 10.0f)
        return false;
    if (!isfinite(settings.saturation) || settings.saturation < 0.0f || settings.saturation > 5.0f)
        return false;
    if (!isfinite(settings.contrast) || settings.contrast < 0.0f || settings.contrast > 5.0f)
        return false;
    if (!isfinite(settings.color_filter.x) || settings.color_filter.x < 0.0f || settings.color_filter.x > 5.0f ||
        !isfinite(settings.color_filter.y) || settings.color_filter.y < 0.0f || settings.color_filter.y > 5.0f ||
        !isfinite(settings.color_filter.z) || settings.color_filter.z < 0.0f || settings.color_filter.z > 5.0f)
        return false;
    if (!isfinite(settings.dof_focus_distance) || settings.dof_focus_distance < 0 ||
        settings.dof_focus_distance > 10000.0f)
        return false;
    if (!isfinite(settings.dof_focus_range) || settings.dof_focus_range < 0 || settings.dof_focus_range > 1000.0f)
        return false;
    if (!isfinite(settings.dof_blur_radius) || settings.dof_blur_radius < 0 || settings.dof_blur_radius > 0.1f)
        return false;
    if (!isfinite(settings.motion_blur_strength) || settings.motion_blur_strength < 0 ||
        settings.motion_blur_strength > 5.0f)
        return false;
    if (!isfinite(settings.vignette_intensity) || settings.vignette_intensity < 0 || settings.vignette_intensity > 1.0f)
        return false;
    if (!isfinite(settings.vignette_smoothness) || settings.vignette_smoothness < 0 ||
        settings.vignette_smoothness > 1.0f)
        return false;
    if (!isfinite(settings.chromatic_aberration_strength) || settings.chromatic_aberration_strength < 0 ||
        settings.chromatic_aberration_strength > 0.1f)
        return false;
    if (!isfinite(settings.film_grain_intensity) || settings.film_grain_intensity < 0 ||
        settings.film_grain_intensity > 1.0f)
        return false;
    if (!isfinite(settings.rtgi_intensity) || settings.rtgi_intensity < 0.0f || settings.rtgi_intensity > 10.0f)
        return false;
    if (!isfinite(settings.rtgi_ray_distance) || settings.rtgi_ray_distance < 0.0f ||
        settings.rtgi_ray_distance > 1000.0f)
        return false;
    if (settings.tone_mapping && settings.tone_mapper == AF_TONE_MAP_OFF)
        settings.tone_mapper = AF_TONE_MAP_REINHARD;
    if (settings.rtgi_ray_distance == 0.0f)
        settings.rtgi_ray_distance = 25.0f;
    if (settings.rtgi_samples > 4)
        settings.rtgi_samples = 4; /* The shader and editor cap GI at 4 samples. */
    if (settings.rtgi_samples == 0)
        settings.rtgi_samples = 1;
    if (!settings.local_shadow_resolution)
        settings.local_shadow_resolution = 512;
    scene->render_settings = settings;
    return true;
}

af_local_light af_local_light_default(af_light_type type) {
    af_local_light light = {.type = type,
                            .enabled = true,
                            .position = {0, 3, 0},
                            .color = {1, 1, 1},
                            .intensity = 10,
                            .range = 10,
                            .shadow_bias = .001f,
                            .direction = {0, -1, 0},
                            .inner_angle = AF_PI / 12,
                            .outer_angle = AF_PI / 6};
    const char *name = type == AF_LIGHT_SPOT ? "Spot light" : "Point light";
    memcpy(light.name, name, strlen(name) + 1);
    return light;
}
static bool valid_local_light(af_local_light light) {
    if (!isfinite(light.shadow_bias) || light.shadow_bias < 0 || light.shadow_bias > .1f)
        return false;
    if (!memchr(light.name, 0, sizeof(light.name)) || !light.name[0] ||
        (light.type != AF_LIGHT_POINT && light.type != AF_LIGHT_SPOT))
        return false;
    float values[] = {light.position.x, light.position.y, light.position.z, light.color.x,
                      light.color.y,    light.color.z,    light.intensity,  light.range};
    for (unsigned i = 0; i < 8; ++i)
        if (!isfinite(values[i]))
            return false;
    if (!isfinite(light.direction.x) || !isfinite(light.direction.y) || !isfinite(light.direction.z) ||
        !isfinite(light.inner_angle) || !isfinite(light.outer_angle) || light.inner_angle < 0 ||
        light.outer_angle < light.inner_angle || light.outer_angle > 89 * AF_PI / 180)
        return false;
    if (light.type == AF_LIGHT_SPOT &&
        (!isfinite(af_vec3_length(light.direction)) || af_vec3_length(light.direction) < 1e-6f))
        return false;
    return light.color.x >= 0 && light.color.x <= 100 && light.color.y >= 0 && light.color.y <= 100 &&
           light.color.z >= 0 && light.color.z <= 100 && light.intensity >= 0 && light.intensity <= 10000 &&
           light.range >= 0.01f && light.range <= 10000;
}
size_t af_scene_local_light_count(const af_scene *scene) {
    return scene ? scene->local_light_count : 0;
}
const af_local_light *af_scene_local_light_at(const af_scene *scene, size_t index) {
    return scene && index < scene->local_light_count ? &scene->local_lights[index] : NULL;
}
bool af_scene_add_local_light(af_scene *scene, af_local_light light) {
    if (!scene || scene->local_light_count >= AF_SCENE_MAX_LOCAL_LIGHTS || !valid_local_light(light))
        return false;
    scene->local_lights[scene->local_light_count++] = light;
    return true;
}
bool af_scene_set_local_light(af_scene *scene, size_t index, af_local_light light) {
    if (!scene || index >= scene->local_light_count || !valid_local_light(light))
        return false;
    scene->local_lights[index] = light;
    return true;
}
bool af_scene_remove_local_light(af_scene *scene, size_t index) {
    if (!scene || index >= scene->local_light_count)
        return false;
    memmove(&scene->local_lights[index], &scene->local_lights[index + 1],
            (--scene->local_light_count - index) * sizeof(af_local_light));
    return true;
}

static float interpolate_angle(float a, float b, float t) {
    return a + remainderf(b - a, 2 * AF_PI) * t;
}
bool af_scene_interpolated_world_matrix(const af_scene *scene, const af_scene *previous, af_object_id id, float alpha,
                                        af_mat4 *out) {
    if (!out || !isfinite(alpha))
        return false;
    alpha = fminf(1, fmaxf(0, alpha));
    const af_scene_object *node = af_scene_get_object(scene, id);
    if (!node)
        return false;
    af_mat4 result = af_mat4_identity();
    for (unsigned depth = 0; node && depth < AF_SCENE_MAX_DEPTH; ++depth) {
        af_transform t = node->transform;
        const af_scene_object *old = af_scene_get_object(previous, node->id);
        if (old && old->parent == node->parent) {
            t.position = af_vec3_add(old->transform.position,
                                     af_vec3_scale(af_vec3_sub(t.position, old->transform.position), alpha));
            t.scale =
                af_vec3_add(old->transform.scale, af_vec3_scale(af_vec3_sub(t.scale, old->transform.scale), alpha));
            t.rotation = (af_vec3){interpolate_angle(old->transform.rotation.x, t.rotation.x, alpha),
                                   interpolate_angle(old->transform.rotation.y, t.rotation.y, alpha),
                                   interpolate_angle(old->transform.rotation.z, t.rotation.z, alpha)};
        }
        af_mat4 local = af_transform_matrix(t);
        if (old && old->parent == node->parent && node->body.enabled && node->body.type == AF_BODY_DYNAMIC)
            local = af_animation_pose_matrix((af_animation_pose){
                .translation = t.position,
                .scale = t.scale,
                .rotation = af_quaternion_slerp(af_quaternion_from_euler(old->transform.rotation),
                                                af_quaternion_from_euler(node->transform.rotation), alpha)});
        result = af_mat4_mul(local, result);
        if (!node->parent) {
            *out = result;
            return true;
        }
        node = af_scene_get_object(scene, node->parent);
    }
    return false;
}

bool af_scene_set_object_collider(af_scene *scene, af_object_id id, af_collider collider) {
    af_scene_object *object = find_object(scene, id);
    if (!object || (collider.enabled && collider.type != AF_COLLIDER_NONE && af_scene_get_water(scene, id)) ||
        !af_collider_valid(collider) ||
        (collider.type == AF_COLLIDER_MESH &&
         (af_vec3_length(collider.center) > 1e-6f || (object->body.enabled && object->body.type != AF_BODY_STATIC))))
        return false;
    af_collider previous = object->collider;
    object->collider = collider;
    if (!af_scene_set_body(scene, id, object->body)) {
        object->collider = previous;
        return false;
    }
    return true;
}

af_animation_component af_animation_component_default(void) {
    return (af_animation_component){.speed = 1, .loop = true};
}
af_model_binding af_model_binding_default(void) {
    return (af_model_binding){.skin = -1};
}
bool af_scene_set_object_animation(af_scene *scene, af_object_id id, af_animation_component value) {
    af_scene_object *o = find_object(scene, id);
    if (!o || !memchr(value.asset, 0, sizeof(value.asset)) || value.clip >= 128 || !isfinite(value.speed) ||
        fabsf(value.speed) > 100)
        return false;
    o->animation = value;
    return true;
}
bool af_scene_set_model_binding(af_scene *scene, af_object_id id, af_model_binding value) {
    af_scene_object *o = find_object(scene, id);
    if (!o || value.node >= 16384 || value.skin < -1 || value.skin >= 128)
        return false;
    if (value.root &&
        (!find_object(scene, value.root) || value.root == id || !af_scene_is_descendant(scene, id, value.root)))
        return false;
    if (value.root && !value.primitive)
        for (size_t i = 0; i < scene->count; ++i) {
            const af_scene_object *other = &scene->objects[i];
            if (other->id != id && other->model.root == value.root && !other->model.primitive &&
                other->model.node == value.node)
                return false;
        }
    o->model = value.root ? value : af_model_binding_default();
    return true;
}

bool af_scene_set_character_rig(af_scene *s, af_object_id id, af_character_rig rig) {
    af_scene_object *o = find_object(s, id);
    if (!o || !af_character_rig_valid(rig))
        return false;
    o->rig = rig;
    return true;
}

const af_landscape *af_scene_get_landscape(const af_scene *scene) {
    return scene ? scene->landscape : NULL;
}
af_landscape *af_scene_get_landscape_mut(af_scene *scene) {
    return scene ? scene->landscape : NULL;
}
bool af_scene_set_landscape(af_scene *scene, af_landscape *landscape) {
    if (!scene)
        return false;
    if (scene->landscape != landscape) {
        af_landscape_destroy(scene->landscape);
        scene->landscape = landscape;
    }
    return true;
}
bool af_scene_remove_landscape(af_scene *scene) {
    if (!scene || !scene->landscape)
        return false;
    af_landscape_destroy(scene->landscape);
    scene->landscape = NULL;
    return true;
}

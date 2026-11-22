/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "agent_internal.h"
#include <afterlight/audio.h>
#include <afterlight/blueprint.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *alpha_mode_str(af_alpha_mode m) {
    if (m == AF_ALPHA_MASK)
        return "mask";
    if (m == AF_ALPHA_BLEND)
        return "blend";
    return "opaque";
}

static af_alpha_mode parse_alpha_mode(const char *s, af_alpha_mode fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "opaque"))
        return AF_ALPHA_OPAQUE;
    if (!strcmp(s, "mask") || !strcmp(s, "cutout"))
        return AF_ALPHA_MASK;
    if (!strcmp(s, "blend"))
        return AF_ALPHA_BLEND;
    return fb;
}

static const char *collider_type_str(af_collider_type t) {
    switch (t) {
    case AF_COLLIDER_BOX: return "box";
    case AF_COLLIDER_SPHERE: return "sphere";
    case AF_COLLIDER_CAPSULE: return "capsule";
    case AF_COLLIDER_MESH: return "mesh";
    case AF_COLLIDER_COMPOUND: return "compound";
    default: return "none";
    }
}

static af_collider_type parse_collider_type(const char *s, af_collider_type fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "none"))
        return AF_COLLIDER_NONE;
    if (!strcmp(s, "box"))
        return AF_COLLIDER_BOX;
    if (!strcmp(s, "sphere"))
        return AF_COLLIDER_SPHERE;
    if (!strcmp(s, "capsule"))
        return AF_COLLIDER_CAPSULE;
    if (!strcmp(s, "mesh"))
        return AF_COLLIDER_MESH;
    if (!strcmp(s, "compound"))
        return AF_COLLIDER_COMPOUND;
    return fb;
}

static const char *body_type_str(af_body_type t) {
    if (t == AF_BODY_KINEMATIC)
        return "kinematic";
    if (t == AF_BODY_DYNAMIC)
        return "dynamic";
    return "static";
}

static af_body_type parse_body_type(const char *s, af_body_type fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "static"))
        return AF_BODY_STATIC;
    if (!strcmp(s, "kinematic"))
        return AF_BODY_KINEMATIC;
    if (!strcmp(s, "dynamic"))
        return AF_BODY_DYNAMIC;
    return fb;
}

static const char *role_str(af_gameplay_role r) {
    switch (r) {
    case AF_ROLE_SPAWN: return "spawn";
    case AF_ROLE_CHECKPOINT: return "checkpoint";
    case AF_ROLE_GOAL: return "goal";
    case AF_ROLE_RESTART: return "restart";
    case AF_ROLE_PLATFORM: return "platform";
    default: return "none";
    }
}

static af_gameplay_role parse_role(const char *s, af_gameplay_role fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "none"))
        return AF_ROLE_NONE;
    if (!strcmp(s, "spawn"))
        return AF_ROLE_SPAWN;
    if (!strcmp(s, "checkpoint"))
        return AF_ROLE_CHECKPOINT;
    if (!strcmp(s, "goal"))
        return AF_ROLE_GOAL;
    if (!strcmp(s, "restart"))
        return AF_ROLE_RESTART;
    if (!strcmp(s, "platform"))
        return AF_ROLE_PLATFORM;
    return fb;
}

static const char *asset_kind_str(af_asset_kind k) {
    switch (k) {
    case AF_ASSET_MESH: return "mesh";
    case AF_ASSET_TEXTURE: return "texture";
    case AF_ASSET_NORMAL_MAP: return "normal_map";
    case AF_ASSET_AUDIO: return "audio";
    case AF_ASSET_METALLIC_ROUGHNESS: return "metallic_roughness";
    case AF_ASSET_ENVIRONMENT: return "environment";
    case AF_ASSET_ANIMATION: return "animation";
    }
    return "mesh";
}

static bool parse_asset_kind(const char *s, af_asset_kind *out) {
    if (!s || !out)
        return false;
    if (!strcmp(s, "mesh")) {
        *out = AF_ASSET_MESH;
        return true;
    }
    if (!strcmp(s, "texture")) {
        *out = AF_ASSET_TEXTURE;
        return true;
    }
    if (!strcmp(s, "normal_map") || !strcmp(s, "normal")) {
        *out = AF_ASSET_NORMAL_MAP;
        return true;
    }
    if (!strcmp(s, "metallic_roughness") || !strcmp(s, "surface")) {
        *out = AF_ASSET_METALLIC_ROUGHNESS;
        return true;
    }
    if (!strcmp(s, "environment") || !strcmp(s, "hdr")) {
        *out = AF_ASSET_ENVIRONMENT;
        return true;
    }
    if (!strcmp(s, "animation")) {
        *out = AF_ASSET_ANIMATION;
        return true;
    }
    if (!strcmp(s, "audio") || !strcmp(s, "sound")) {
        *out = AF_ASSET_AUDIO;
        return true;
    }
    return false;
}

static const char *aa_str(af_anti_aliasing aa) {
    switch (aa) {
    case AF_AA_FXAA: return "fxaa";
    case AF_AA_SMAA: return "smaa";
    case AF_AA_TAA: return "taa";
    default: return "off";
    }
}

static af_anti_aliasing parse_aa(const char *s, af_anti_aliasing fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "off"))
        return AF_AA_OFF;
    if (!strcmp(s, "fxaa"))
        return AF_AA_FXAA;
    if (!strcmp(s, "smaa"))
        return AF_AA_SMAA;
    if (!strcmp(s, "taa"))
        return AF_AA_TAA;
    return fb;
}

static const char *tone_mapper_str(af_tone_mapping tm) {
    switch (tm) {
    case AF_TONE_MAP_REINHARD: return "reinhard";
    case AF_TONE_MAP_ACES: return "aces";
    case AF_TONE_MAP_AGX: return "agx";
    default: return "off";
    }
}

static af_tone_mapping parse_tone_mapper(const char *s, af_tone_mapping fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "off"))
        return AF_TONE_MAP_OFF;
    if (!strcmp(s, "reinhard"))
        return AF_TONE_MAP_REINHARD;
    if (!strcmp(s, "aces"))
        return AF_TONE_MAP_ACES;
    if (!strcmp(s, "agx"))
        return AF_TONE_MAP_AGX;
    return fb;
}

static af_brush_falloff parse_falloff(const char *s) {
    if (!s || !strcmp(s, "smooth"))
        return AF_FALLOFF_SMOOTH;
    if (!strcmp(s, "linear"))
        return AF_FALLOFF_LINEAR;
    if (!strcmp(s, "spherical"))
        return AF_FALLOFF_SPHERICAL;
    if (!strcmp(s, "flat"))
        return AF_FALLOFF_FLAT;
    return AF_FALLOFF_SMOOTH;
}

static const char *water_mode_str(af_water_mode m) {
    if (m == AF_WATER_RIVER)
        return "river";
    if (m == AF_WATER_PARTICLES)
        return "particles";
    return "pool";
}

static af_water_mode parse_water_mode(const char *s, af_water_mode fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "pool"))
        return AF_WATER_POOL;
    if (!strcmp(s, "river"))
        return AF_WATER_RIVER;
    if (!strcmp(s, "particles"))
        return AF_WATER_PARTICLES;
    return fb;
}

static af_water_quality parse_water_quality(const char *s, af_water_quality fb) {
    if (!s)
        return fb;
    if (!strcmp(s, "low"))
        return AF_WATER_LOW;
    if (!strcmp(s, "medium"))
        return AF_WATER_MEDIUM;
    if (!strcmp(s, "high"))
        return AF_WATER_HIGH;
    return fb;
}

/* ---- Serializers ---- */

static cJSON *serialize_material(const af_material *m) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "alpha_mode", alpha_mode_str(m->alpha_mode));
    cJSON_AddNumberToObject(j, "alpha_cutoff", m->alpha_cutoff);
    cJSON_AddItemToObject(j, "base_color", af_agent_json_color(m->base_color));
    cJSON_AddStringToObject(j, "texture", m->texture);
    cJSON_AddStringToObject(j, "normal_map", m->normal_map);
    cJSON_AddNumberToObject(j, "normal_strength", m->normal_strength);
    cJSON_AddStringToObject(j, "metallic_roughness_map", m->metallic_roughness_map);
    cJSON_AddStringToObject(j, "shader", m->shader);
    cJSON_AddBoolToObject(j, "physically_based", m->physically_based);
    cJSON_AddNumberToObject(j, "roughness", m->roughness);
    cJSON_AddNumberToObject(j, "metallic", m->metallic);
    cJSON_AddNumberToObject(j, "ior", m->ior);
    cJSON_AddItemToObject(j, "emission", af_agent_json_vec3(m->emission));
    cJSON_AddNumberToObject(j, "emission_strength", m->emission_strength);
    cJSON_AddBoolToObject(j, "unlit", m->unlit);
    cJSON_AddBoolToObject(j, "cast_shadows", m->cast_shadows);
    cJSON_AddBoolToObject(j, "receive_shadows", m->receive_shadows);
    return j;
}

static cJSON *serialize_object(const af_scene *scene, const af_scene_object *o) {
    cJSON *j = cJSON_CreateObject();
    cJSON_AddItemToObject(j, "id", af_agent_json_id(o->id));
    cJSON_AddStringToObject(j, "name", o->name);
    cJSON_AddStringToObject(j, "mesh", o->mesh);
    cJSON_AddBoolToObject(j, "visible", o->visible);
    if (o->parent != AF_OBJECT_NONE)
        cJSON_AddItemToObject(j, "parent", af_agent_json_id(o->parent));
    cJSON_AddItemToObject(j, "position", af_agent_json_vec3(o->transform.position));
    cJSON_AddItemToObject(j, "rotation", af_agent_json_vec3(o->transform.rotation));
    cJSON_AddItemToObject(j, "scale", af_agent_json_vec3(o->transform.scale));
    cJSON_AddItemToObject(j, "material", serialize_material(&o->material));

    if (o->collider.type != AF_COLLIDER_NONE) {
        cJSON *c = cJSON_CreateObject();
        cJSON_AddStringToObject(c, "type", collider_type_str(o->collider.type));
        cJSON_AddBoolToObject(c, "enabled", o->collider.enabled);
        cJSON_AddBoolToObject(c, "trigger", o->collider.trigger);
        cJSON_AddItemToObject(c, "center", af_agent_json_vec3(o->collider.center));
        cJSON_AddItemToObject(c, "half_extents", af_agent_json_vec3(o->collider.half_extents));
        cJSON_AddNumberToObject(c, "radius", o->collider.radius);
        cJSON_AddNumberToObject(c, "half_height", o->collider.half_height);
        cJSON_AddNumberToObject(c, "layer", (double)o->collider.layer);
        size_t pcount = af_scene_collider_part_count(scene, o->id);
        if (pcount > 0) {
            cJSON *parts = cJSON_CreateArray();
            for (size_t i = 0; i < pcount; ++i) {
                const af_collider_part *p = af_scene_collider_part_at(scene, o->id, i);
                cJSON *pj = cJSON_CreateObject();
                cJSON_AddItemToObject(pj, "id", af_agent_json_id(p->id));
                cJSON_AddStringToObject(pj, "name", p->name);
                cJSON_AddStringToObject(pj, "type", collider_type_str(p->collider.type));
                cJSON_AddItemToObject(pj, "position", af_agent_json_vec3(p->local.position));
                cJSON_AddItemToObject(pj, "rotation", af_agent_json_vec3(p->local.rotation));
                cJSON_AddItemToObject(pj, "half_extents", af_agent_json_vec3(p->collider.half_extents));
                cJSON_AddNumberToObject(pj, "radius", p->collider.radius);
                cJSON_AddNumberToObject(pj, "density", p->density);
                cJSON_AddItemToArray(parts, pj);
            }
            cJSON_AddItemToObject(c, "parts", parts);
        }
        cJSON_AddItemToObject(j, "collider", c);
    }

    if (o->body.enabled) {
        cJSON *b = cJSON_CreateObject();
        cJSON_AddBoolToObject(b, "enabled", o->body.enabled);
        cJSON_AddStringToObject(b, "type", body_type_str(o->body.type));
        cJSON_AddNumberToObject(b, "mass", o->body.mass);
        cJSON_AddNumberToObject(b, "friction", o->body.friction);
        cJSON_AddNumberToObject(b, "restitution", o->body.restitution);
        cJSON_AddNumberToObject(b, "linear_damping", o->body.linear_damping);
        cJSON_AddNumberToObject(b, "angular_damping", o->body.angular_damping);
        cJSON_AddNumberToObject(b, "gravity_scale", o->body.gravity_scale);
        cJSON_AddBoolToObject(b, "allow_sleep", o->body.allow_sleep);
        cJSON_AddBoolToObject(b, "continuous", o->body.continuous);
        cJSON_AddItemToObject(j, "body", b);
    }

    if (o->gameplay.player.enabled || o->gameplay.camera.enabled || o->gameplay.role != AF_ROLE_NONE) {
        cJSON *g = cJSON_CreateObject();
        cJSON_AddStringToObject(g, "role", role_str(o->gameplay.role));
        cJSON_AddNumberToObject(g, "checkpoint_order", (double)o->gameplay.checkpoint_order);
        if (o->gameplay.player.enabled) {
            cJSON *pl = cJSON_CreateObject();
            cJSON_AddBoolToObject(pl, "enabled", true);
            cJSON_AddNumberToObject(pl, "mouse_sensitivity", o->gameplay.player.mouse_sensitivity);
            cJSON_AddItemToObject(g, "player", pl);
        }
        if (o->gameplay.camera.enabled) {
            cJSON *cm = cJSON_CreateObject();
            cJSON_AddBoolToObject(cm, "enabled", true);
            cJSON_AddBoolToObject(cm, "primary", o->gameplay.camera.primary);
            cJSON_AddNumberToObject(cm, "eye_height", o->gameplay.camera.eye_height);
            cJSON_AddNumberToObject(cm, "vertical_fov", o->gameplay.camera.vertical_fov);
            cJSON_AddNumberToObject(cm, "near_clip", o->gameplay.camera.near_clip);
            cJSON_AddNumberToObject(cm, "far_clip", o->gameplay.camera.far_clip);
            cJSON_AddItemToObject(g, "camera", cm);
        }
        cJSON_AddItemToObject(j, "gameplay", g);
    }

    if (o->animation.asset[0]) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "asset", o->animation.asset);
        cJSON_AddNumberToObject(a, "clip", (double)o->animation.clip);
        cJSON_AddNumberToObject(a, "speed", o->animation.speed);
        cJSON_AddBoolToObject(a, "autoplay", o->animation.autoplay);
        cJSON_AddBoolToObject(a, "loop", o->animation.loop);
        cJSON_AddItemToObject(j, "animation", a);
    }

    const af_water *w = af_scene_get_water(scene, o->id);
    if (w) {
        cJSON *wj = cJSON_CreateObject();
        cJSON_AddBoolToObject(wj, "enabled", w->enabled);
        cJSON_AddStringToObject(wj, "mode", water_mode_str(w->mode));
        cJSON_AddItemToObject(wj, "half_size", af_agent_json_vec2(w->half_size));
        cJSON_AddNumberToObject(wj, "depth", w->depth);
        cJSON_AddNumberToObject(wj, "roughness", w->roughness);
        cJSON_AddNumberToObject(wj, "ior", w->ior);
        cJSON_AddItemToObject(wj, "absorption", af_agent_json_vec3(w->absorption));
        cJSON_AddItemToObject(wj, "scattering", af_agent_json_vec3(w->scattering));
        cJSON_AddItemToObject(j, "water", wj);
    }
    return j;
}

/* ---- Component Appliers ---- */

static void apply_material_json(af_material *m, const cJSON *j) {
    if (!j || !cJSON_IsObject(j))
        return;
    m->alpha_mode = parse_alpha_mode(af_agent_str(j, "alpha_mode", NULL), m->alpha_mode);
    if (af_agent_has(j, "alpha_cutoff"))
        m->alpha_cutoff = af_agent_f32(j, "alpha_cutoff", m->alpha_cutoff);
    (void)af_agent_color(j, "base_color", &m->base_color);
    if (af_agent_has(j, "texture"))
        af_agent_copy_str(m->texture, sizeof(m->texture), af_agent_str(j, "texture", ""));
    if (af_agent_has(j, "normal_map"))
        af_agent_copy_str(m->normal_map, sizeof(m->normal_map), af_agent_str(j, "normal_map", ""));
    if (af_agent_has(j, "normal_strength"))
        m->normal_strength = af_agent_f32(j, "normal_strength", m->normal_strength);
    if (af_agent_has(j, "metallic_roughness_map"))
        af_agent_copy_str(m->metallic_roughness_map, sizeof(m->metallic_roughness_map),
                          af_agent_str(j, "metallic_roughness_map", ""));
    if (af_agent_has(j, "shader"))
        af_agent_copy_str(m->shader, sizeof(m->shader), af_agent_str(j, "shader", ""));
    m->physically_based = af_agent_bool(j, "physically_based", m->physically_based);
    if (af_agent_has(j, "roughness"))
        m->roughness = af_agent_f32(j, "roughness", m->roughness);
    if (af_agent_has(j, "metallic"))
        m->metallic = af_agent_f32(j, "metallic", m->metallic);
    if (af_agent_has(j, "ior"))
        m->ior = af_agent_f32(j, "ior", m->ior);
    (void)af_agent_vec3(j, "emission", &m->emission);
    if (af_agent_has(j, "emission_strength"))
        m->emission_strength = af_agent_f32(j, "emission_strength", m->emission_strength);
    m->unlit = af_agent_bool(j, "unlit", m->unlit);
    m->cast_shadows = af_agent_bool(j, "cast_shadows", m->cast_shadows);
    m->receive_shadows = af_agent_bool(j, "receive_shadows", m->receive_shadows);
}

static bool apply_object_components(af_scene *scene, af_object_id id, const cJSON *args, char *err, size_t err_cap) {
    const af_scene_object *cur = af_scene_get_object(scene, id);
    if (!cur) {
        snprintf(err, err_cap, "Object not found.");
        return false;
    }

    af_transform tr = cur->transform;
    bool tr_changed = false;
    const cJSON *tr_obj = cJSON_GetObjectItemCaseSensitive(args, "transform");
    const cJSON *tr_src = (tr_obj && cJSON_IsObject(tr_obj)) ? tr_obj : args;
    if (af_agent_vec3(tr_src, "position", &tr.position))
        tr_changed = true;
    if (af_agent_vec3(tr_src, "rotation", &tr.rotation))
        tr_changed = true;
    if (af_agent_vec3(tr_src, "scale", &tr.scale))
        tr_changed = true;
    if (tr_changed && !af_scene_set_object_transform(scene, id, tr)) {
        snprintf(err, err_cap, "Invalid transform values (must be finite; scale non-zero).");
        return false;
    }

    if (af_agent_has(args, "visible")) {
        if (!af_scene_set_object_visible(scene, id, af_agent_bool(args, "visible", cur->visible))) {
            snprintf(err, err_cap, "Failed to set visibility.");
            return false;
        }
    }

    if (af_agent_has(args, "parent")) {
        af_object_id parent = af_agent_id(args, "parent");
        if (!af_scene_set_parent(scene, id, parent)) {
            snprintf(err, err_cap, "Failed to set parent (cycle, depth limit, or invalid parent ID).");
            return false;
        }
    }

    const cJSON *mat_json = cJSON_GetObjectItemCaseSensitive(args, "material");
    if (mat_json && cJSON_IsObject(mat_json)) {
        af_material mat = af_scene_get_object(scene, id)->material;
        apply_material_json(&mat, mat_json);
        if (!af_scene_set_object_material(scene, id, mat)) {
            snprintf(err, err_cap, "Invalid material values.");
            return false;
        }
    }

    const cJSON *col_json = cJSON_GetObjectItemCaseSensitive(args, "collider");
    if (col_json && cJSON_IsObject(col_json)) {
        af_collider col = af_scene_get_object(scene, id)->collider;
        if (col.type == AF_COLLIDER_NONE && af_agent_has(col_json, "type"))
            col = af_collider_default();
        col.type = parse_collider_type(af_agent_str(col_json, "type", NULL), col.type);
        col.enabled = af_agent_bool(col_json, "enabled", col.type != AF_COLLIDER_NONE);
        col.trigger = af_agent_bool(col_json, "trigger", col.trigger);
        (void)af_agent_vec3(col_json, "center", &col.center);
        (void)af_agent_vec3(col_json, "half_extents", &col.half_extents);
        if (af_agent_has(col_json, "radius"))
            col.radius = af_agent_f32(col_json, "radius", col.radius);
        if (af_agent_has(col_json, "half_height"))
            col.half_height = af_agent_f32(col_json, "half_height", col.half_height);
        if (af_agent_has(col_json, "layer"))
            col.layer = af_agent_u32(col_json, "layer", col.layer);
        if (!af_scene_set_object_collider(scene, id, col)) {
            snprintf(err, err_cap, "Invalid collider parameters.");
            return false;
        }
    }

    const cJSON *body_json = cJSON_GetObjectItemCaseSensitive(args, "body");
    if (body_json && cJSON_IsObject(body_json)) {
        af_rigid_body body = af_scene_get_object(scene, id)->body;
        if (!body.enabled)
            body = af_rigid_body_default();
        body.enabled = af_agent_bool(body_json, "enabled", true);
        body.type = parse_body_type(af_agent_str(body_json, "type", NULL), body.type);
        if (af_agent_has(body_json, "mass"))
            body.mass = af_agent_f32(body_json, "mass", body.mass);
        if (af_agent_has(body_json, "friction"))
            body.friction = af_agent_f32(body_json, "friction", body.friction);
        if (af_agent_has(body_json, "restitution"))
            body.restitution = af_agent_f32(body_json, "restitution", body.restitution);
        if (af_agent_has(body_json, "linear_damping"))
            body.linear_damping = af_agent_f32(body_json, "linear_damping", body.linear_damping);
        if (af_agent_has(body_json, "angular_damping"))
            body.angular_damping = af_agent_f32(body_json, "angular_damping", body.angular_damping);
        if (af_agent_has(body_json, "gravity_scale"))
            body.gravity_scale = af_agent_f32(body_json, "gravity_scale", body.gravity_scale);
        body.allow_sleep = af_agent_bool(body_json, "allow_sleep", body.allow_sleep);
        body.continuous = af_agent_bool(body_json, "continuous", body.continuous);
        (void)af_agent_vec3(body_json, "velocity", &body.velocity);
        (void)af_agent_vec3(body_json, "angular_velocity", &body.angular_velocity);
        if (!af_scene_set_body(scene, id, body)) {
            snprintf(err, err_cap, "Invalid rigid body parameters.");
            return false;
        }
    }

    const cJSON *gp_json = cJSON_GetObjectItemCaseSensitive(args, "gameplay");
    if (gp_json && cJSON_IsObject(gp_json)) {
        af_gameplay_component gp = af_scene_get_object(scene, id)->gameplay;
        gp.role = parse_role(af_agent_str(gp_json, "role", NULL), gp.role);
        if (af_agent_has(gp_json, "checkpoint_order"))
            gp.checkpoint_order = af_agent_u32(gp_json, "checkpoint_order", gp.checkpoint_order);
        const cJSON *pl = cJSON_GetObjectItemCaseSensitive(gp_json, "player");
        if (pl && cJSON_IsObject(pl)) {
            gp.player.enabled = af_agent_bool(pl, "enabled", true);
            if (af_agent_has(pl, "mouse_sensitivity"))
                gp.player.mouse_sensitivity = af_agent_f32(pl, "mouse_sensitivity", gp.player.mouse_sensitivity);
        }
        const cJSON *cm = cJSON_GetObjectItemCaseSensitive(gp_json, "camera");
        if (cm && cJSON_IsObject(cm)) {
            gp.camera.enabled = af_agent_bool(cm, "enabled", true);
            gp.camera.primary = af_agent_bool(cm, "primary", gp.camera.primary);
            if (af_agent_has(cm, "eye_height"))
                gp.camera.eye_height = af_agent_f32(cm, "eye_height", gp.camera.eye_height);
            if (af_agent_has(cm, "fov_degrees"))
                gp.camera.vertical_fov = af_agent_f32(cm, "fov_degrees", 60.0f) * (AF_PI / 180.0f);
            else if (af_agent_has(cm, "vertical_fov"))
                gp.camera.vertical_fov = af_agent_f32(cm, "vertical_fov", gp.camera.vertical_fov);
            if (af_agent_has(cm, "near_clip"))
                gp.camera.near_clip = af_agent_f32(cm, "near_clip", gp.camera.near_clip);
            if (af_agent_has(cm, "far_clip"))
                gp.camera.far_clip = af_agent_f32(cm, "far_clip", gp.camera.far_clip);
        }
        if (!af_scene_set_object_gameplay(scene, id, gp)) {
            snprintf(err, err_cap, "Invalid gameplay component parameters.");
            return false;
        }
    }

    const cJSON *anim_json = cJSON_GetObjectItemCaseSensitive(args, "animation");
    if (anim_json && cJSON_IsObject(anim_json)) {
        af_animation_component anim = af_scene_get_object(scene, id)->animation;
        if (!anim.asset[0])
            anim = af_animation_component_default();
        if (af_agent_has(anim_json, "asset"))
            af_agent_copy_str(anim.asset, sizeof(anim.asset), af_agent_str(anim_json, "asset", ""));
        if (af_agent_has(anim_json, "clip"))
            anim.clip = af_agent_u32(anim_json, "clip", anim.clip);
        if (af_agent_has(anim_json, "speed"))
            anim.speed = af_agent_f32(anim_json, "speed", anim.speed);
        anim.autoplay = af_agent_bool(anim_json, "autoplay", anim.autoplay);
        anim.loop = af_agent_bool(anim_json, "loop", anim.loop);
        if (!af_scene_set_object_animation(scene, id, anim)) {
            snprintf(err, err_cap, "Invalid animation component parameters.");
            return false;
        }
    }

    const cJSON *water_json = cJSON_GetObjectItemCaseSensitive(args, "water");
    if (water_json) {
        if (cJSON_IsNull(water_json) ||
            (cJSON_IsObject(water_json) && af_agent_bool(water_json, "remove", false))) {
            (void)af_scene_remove_water(scene, id);
        } else if (cJSON_IsObject(water_json)) {
            const af_water *existing = af_scene_get_water(scene, id);
            af_water w = existing ? *existing : af_water_default();
            w.owner = id;
            w.enabled = af_agent_bool(water_json, "enabled", true);
            w.mode = parse_water_mode(af_agent_str(water_json, "mode", NULL), w.mode);
            w.quality = parse_water_quality(af_agent_str(water_json, "quality", NULL), w.quality);
            (void)af_agent_vec2(water_json, "half_size", &w.half_size);
            if (af_agent_has(water_json, "depth"))
                w.depth = af_agent_f32(water_json, "depth", w.depth);
            if (af_agent_has(water_json, "roughness"))
                w.roughness = af_agent_f32(water_json, "roughness", w.roughness);
            if (af_agent_has(water_json, "ior"))
                w.ior = af_agent_f32(water_json, "ior", w.ior);
            if (af_agent_has(water_json, "wind"))
                w.wind = af_agent_f32(water_json, "wind", w.wind);
            if (af_agent_has(water_json, "ripple_scale"))
                w.ripple_scale = af_agent_f32(water_json, "ripple_scale", w.ripple_scale);
            if (af_agent_has(water_json, "ripple_strength"))
                w.ripple_strength = af_agent_f32(water_json, "ripple_strength", w.ripple_strength);
            (void)af_agent_vec3(water_json, "absorption", &w.absorption);
            (void)af_agent_vec3(water_json, "scattering", &w.scattering);
            if (!af_scene_set_water(scene, id, w)) {
                snprintf(err, err_cap, "Invalid water component (water owner must be root with unit scale and yaw only).");
                return false;
            }
        }
    }

    return true;
}

static bool save_scene_or_err(const af_scene *scene, const char *path, cJSON **out_err) {
    af_scene_error serr = {0};
    if (!af_scene_save(scene, path, &serr)) {
        *out_err = af_agent_err(serr.message[0] ? serr.message : "Failed to save scene.");
        return false;
    }
    return true;
}

/* ---- Scene Commands ---- */

static cJSON *cmd_scene_create(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("scene_create requires 'scene_path'.");

    af_scene *scene = af_scene_create();
    if (!scene)
        return af_agent_err("Out of memory creating scene.");

    bool starter = af_agent_bool(args, "starter", true);
    if (starter) {
        af_object_id ground = af_scene_add_object(scene, "Ground", "cube");
        if (ground != AF_OBJECT_NONE) {
            af_transform gt = af_transform_identity();
            gt.position = (af_vec3){0.0f, -0.5f, 0.0f};
            gt.scale = (af_vec3){20.0f, 1.0f, 20.0f};
            (void)af_scene_set_object_transform(scene, ground, gt);
            af_material gm = af_material_default();
            gm.physically_based = true;
            gm.base_color = (af_color){0.35f, 0.38f, 0.34f, 1.0f};
            gm.roughness = 0.85f;
            (void)af_scene_set_object_material(scene, ground, gm);
            af_collider gc = af_collider_default();
            gc.type = AF_COLLIDER_BOX;
            gc.enabled = true;
            gc.half_extents = (af_vec3){0.5f, 0.5f, 0.5f};
            (void)af_scene_set_object_collider(scene, ground, gc);
        }
        af_object_id player = af_scene_add_object(scene, "Player", "__node");
        if (player != AF_OBJECT_NONE) {
            af_transform pt = af_transform_identity();
            pt.position = (af_vec3){0.0f, 1.0f, 6.0f};
            (void)af_scene_set_object_transform(scene, player, pt);
            (void)af_scene_set_object_visible(scene, player, false);
            af_collider pc = af_collider_default();
            pc.type = AF_COLLIDER_CAPSULE;
            pc.enabled = true;
            pc.radius = 0.35f;
            pc.half_height = 0.55f;
            (void)af_scene_set_object_collider(scene, player, pc);
            af_gameplay_component gp = af_gameplay_component_default();
            gp.player.enabled = true;
            gp.camera.enabled = true;
            gp.camera.primary = true;
            (void)af_scene_set_object_gameplay(scene, player, gp);
        }
        af_render_settings rs = af_render_settings_default();
        rs.skybox = true;
        rs.environment_enabled = true;
        rs.tone_mapping = true;
        rs.tone_mapper = AF_TONE_MAP_ACES;
        (void)af_scene_set_render_settings(scene, rs);
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "scene_path", path);
    cJSON_AddNumberToObject(data, "object_count", (double)af_scene_object_count(scene));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_scene_inspect(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("scene_inspect requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    cJSON *data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "scene_path", path);
    cJSON_AddNumberToObject(data, "object_count", (double)af_scene_object_count(scene));
    cJSON_AddNumberToObject(data, "asset_count", (double)af_scene_asset_count(scene));
    cJSON_AddNumberToObject(data, "local_light_count", (double)af_scene_local_light_count(scene));
    cJSON_AddNumberToObject(data, "joint_count", (double)af_scene_joint_count(scene));
    cJSON_AddNumberToObject(data, "water_count", (double)af_scene_water_count(scene));
    cJSON_AddNumberToObject(data, "blueprint_count", (double)af_scene_blueprint_count(scene));

    /* Objects */
    cJSON *objs = cJSON_CreateArray();
    for (size_t i = 0; i < af_scene_object_count(scene); ++i)
        cJSON_AddItemToArray(objs, serialize_object(scene, af_scene_object_at(scene, i)));
    cJSON_AddItemToObject(data, "objects", objs);

    /* Assets */
    cJSON *assets = cJSON_CreateArray();
    for (size_t i = 0; i < af_scene_asset_count(scene); ++i) {
        const af_scene_asset *a = af_scene_asset_at(scene, i);
        cJSON *aj = cJSON_CreateObject();
        cJSON_AddStringToObject(aj, "name", a->name);
        cJSON_AddStringToObject(aj, "kind", asset_kind_str(a->kind));
        cJSON_AddStringToObject(aj, "path", a->path);
        cJSON_AddItemToArray(assets, aj);
    }
    cJSON_AddItemToObject(data, "assets", assets);

    /* Sun */
    af_directional_light sun = af_scene_get_light(scene);
    cJSON *sun_j = cJSON_CreateObject();
    cJSON_AddItemToObject(sun_j, "direction", af_agent_json_vec3(sun.direction));
    cJSON_AddItemToObject(sun_j, "color", af_agent_json_vec3(sun.color));
    cJSON_AddNumberToObject(sun_j, "intensity", sun.intensity);
    cJSON_AddNumberToObject(sun_j, "ambient", sun.ambient);
    cJSON_AddNumberToObject(sun_j, "angular_radius", sun.angular_radius);
    cJSON_AddItemToObject(data, "directional_light", sun_j);

    /* Local lights */
    cJSON *lights = cJSON_CreateArray();
    for (size_t i = 0; i < af_scene_local_light_count(scene); ++i) {
        const af_local_light *l = af_scene_local_light_at(scene, i);
        cJSON *lj = cJSON_CreateObject();
        cJSON_AddNumberToObject(lj, "index", (double)i);
        cJSON_AddStringToObject(lj, "name", l->name);
        cJSON_AddStringToObject(lj, "type", l->type == AF_LIGHT_SPOT ? "spot" : "point");
        cJSON_AddBoolToObject(lj, "enabled", l->enabled);
        cJSON_AddItemToObject(lj, "position", af_agent_json_vec3(l->position));
        cJSON_AddItemToObject(lj, "color", af_agent_json_vec3(l->color));
        cJSON_AddNumberToObject(lj, "intensity", l->intensity);
        cJSON_AddNumberToObject(lj, "range", l->range);
        cJSON_AddItemToObject(lj, "direction", af_agent_json_vec3(l->direction));
        cJSON_AddBoolToObject(lj, "cast_shadows", l->cast_shadows);
        cJSON_AddNumberToObject(lj, "shadow_bias", l->shadow_bias);
        cJSON_AddNumberToObject(lj, "inner_angle", l->inner_angle);
        cJSON_AddNumberToObject(lj, "outer_angle", l->outer_angle);
        cJSON_AddItemToArray(lights, lj);
    }
    cJSON_AddItemToObject(data, "local_lights", lights);

    /* Shadows */
    af_shadow_settings sh = af_scene_get_shadows(scene);
    cJSON *sh_j = cJSON_CreateObject();
    cJSON_AddBoolToObject(sh_j, "enabled", sh.enabled);
    cJSON_AddStringToObject(sh_j, "mode", sh.mode == AF_SHADOW_CASCADED ? "cascaded" : "single");
    cJSON_AddNumberToObject(sh_j, "resolution", (double)sh.resolution);
    cJSON_AddNumberToObject(sh_j, "distance", sh.distance);
    cJSON_AddNumberToObject(sh_j, "filter_radius", (double)sh.filter_radius);
    cJSON_AddItemToObject(data, "shadows", sh_j);

    /* Render / Post settings */
    af_render_settings rs = af_scene_get_render_settings(scene);
    cJSON *rs_j = cJSON_CreateObject();
    cJSON_AddStringToObject(rs_j, "anti_aliasing", aa_str(rs.anti_aliasing));
    cJSON_AddNumberToObject(rs_j, "exposure", rs.exposure);
    cJSON_AddBoolToObject(rs_j, "tone_mapping", rs.tone_mapping);
    cJSON_AddStringToObject(rs_j, "tone_mapper", tone_mapper_str(rs.tone_mapper));
    cJSON_AddBoolToObject(rs_j, "bloom", rs.bloom);
    cJSON_AddNumberToObject(rs_j, "bloom_strength", rs.bloom_strength);
    cJSON_AddNumberToObject(rs_j, "bloom_threshold", rs.bloom_threshold);
    cJSON_AddBoolToObject(rs_j, "environment_enabled", rs.environment_enabled);
    cJSON_AddNumberToObject(rs_j, "environment_profile", (double)rs.environment_profile);
    cJSON_AddStringToObject(rs_j, "environment_asset", rs.environment_asset);
    cJSON_AddBoolToObject(rs_j, "skybox", rs.skybox);
    cJSON_AddNumberToObject(rs_j, "skybox_intensity", rs.skybox_intensity);
    cJSON_AddBoolToObject(rs_j, "ray_traced_shadows", rs.ray_traced_shadows);
    cJSON_AddBoolToObject(rs_j, "ray_traced_water_reflections", rs.ray_traced_water_reflections);
    cJSON_AddBoolToObject(rs_j, "ray_traced_material_effects", rs.ray_traced_material_effects);
    cJSON_AddBoolToObject(rs_j, "ray_traced_gi", rs.ray_traced_gi);
    cJSON_AddNumberToObject(rs_j, "rtgi_intensity", rs.rtgi_intensity);
    cJSON_AddNumberToObject(rs_j, "rtgi_samples", (double)rs.rtgi_samples);
    cJSON_AddNumberToObject(rs_j, "rtgi_bounces", (double)rs.rtgi_bounces);
    cJSON_AddBoolToObject(rs_j, "depth_of_field", rs.depth_of_field);
    cJSON_AddBoolToObject(rs_j, "motion_blur", rs.motion_blur);
    cJSON_AddBoolToObject(rs_j, "vignette", rs.vignette);
    cJSON_AddBoolToObject(rs_j, "chromatic_aberration", rs.chromatic_aberration);
    cJSON_AddBoolToObject(rs_j, "film_grain", rs.film_grain);
    cJSON_AddItemToObject(data, "render_settings", rs_j);

    /* Landscape */
    const af_landscape *ls = af_scene_get_landscape(scene);
    if (ls) {
        cJSON *ls_j = cJSON_CreateObject();
        cJSON_AddBoolToObject(ls_j, "enabled", ls->enabled);
        cJSON_AddNumberToObject(ls_j, "chunks_x", (double)ls->chunk_count_x);
        cJSON_AddNumberToObject(ls_j, "chunks_z", (double)ls->chunk_count_z);
        cJSON_AddNumberToObject(ls_j, "vertex_spacing", ls->vertex_spacing);
        cJSON_AddItemToObject(ls_j, "origin", af_agent_json_vec3(ls->origin));
        cJSON_AddNumberToObject(ls_j, "world_size_x",
                                (double)(ls->chunk_count_x * AF_LANDSCAPE_CHUNK_QUADS) * ls->vertex_spacing);
        cJSON_AddNumberToObject(ls_j, "world_size_z",
                                (double)(ls->chunk_count_z * AF_LANDSCAPE_CHUNK_QUADS) * ls->vertex_spacing);
        cJSON_AddNumberToObject(ls_j, "total_foliage_instances", (double)ls->total_foliage_instances);
        cJSON *fls = cJSON_CreateArray();
        for (size_t i = 0; i < ls->foliage_layer_count; ++i) {
            const af_foliage_layer *fl = &ls->foliage_layers[i];
            cJSON *fj = cJSON_CreateObject();
            cJSON_AddNumberToObject(fj, "index", (double)i);
            cJSON_AddStringToObject(fj, "name", fl->name);
            cJSON_AddStringToObject(fj, "mesh", fl->mesh);
            cJSON_AddBoolToObject(fj, "visible", fl->visible);
            cJSON_AddNumberToObject(fj, "instances", (double)fl->total_instances);
            cJSON_AddItemToArray(fls, fj);
        }
        cJSON_AddItemToObject(ls_j, "foliage_layers", fls);
        cJSON_AddItemToObject(data, "landscape", ls_j);
    }

    /* Active game camera */
    af_camera cam;
    if (af_scene_game_camera(scene, &cam)) {
        cJSON *cj = cJSON_CreateObject();
        cJSON_AddItemToObject(cj, "position", af_agent_json_vec3(cam.position));
        cJSON_AddNumberToObject(cj, "yaw", cam.yaw);
        cJSON_AddNumberToObject(cj, "pitch", cam.pitch);
        cJSON_AddNumberToObject(cj, "vertical_fov", cam.vertical_fov);
        cJSON_AddItemToObject(data, "game_camera", cj);
    }

    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_object_create(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    const char *name = af_agent_str(args, "name", "Object");
    const char *mesh = af_agent_str(args, "mesh", "cube");
    if (!path || !path[0])
        return af_agent_err("object_create requires 'scene_path'.");

    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_object_id id = af_scene_add_object(scene, name, mesh);
    if (id == AF_OBJECT_NONE) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to create object (invalid name/mesh or object limit reached).");
    }

    char err_msg[256] = {0};
    if (!apply_object_components(scene, id, args, err_msg, sizeof(err_msg))) {
        af_scene_destroy(scene);
        return af_agent_err(err_msg);
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = serialize_object(scene, af_scene_get_object(scene, id));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_object_update(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("object_update requires 'scene_path'.");

    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_object_id id = af_agent_resolve_object(scene, args);
    if (id == AF_OBJECT_NONE) {
        af_scene_destroy(scene);
        return af_agent_err("Target object not found (provide 'id' or 'object_name').");
    }

    if (af_agent_has(args, "new_name")) {
        if (!af_scene_set_object_name(scene, id, af_agent_str(args, "new_name", ""))) {
            af_scene_destroy(scene);
            return af_agent_err("Invalid new_name.");
        }
    }
    if (af_agent_has(args, "mesh")) {
        if (!af_scene_set_object_mesh(scene, id, af_agent_str(args, "mesh", ""))) {
            af_scene_destroy(scene);
            return af_agent_err("Invalid mesh name.");
        }
    }

    char err_msg[256] = {0};
    if (!apply_object_components(scene, id, args, err_msg, sizeof(err_msg))) {
        af_scene_destroy(scene);
        return af_agent_err(err_msg);
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = serialize_object(scene, af_scene_get_object(scene, id));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_object_delete(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("object_delete requires 'scene_path'.");

    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_object_id id = af_agent_resolve_object(scene, args);
    if (id == AF_OBJECT_NONE) {
        af_scene_destroy(scene);
        return af_agent_err("Object not found.");
    }

    size_t removed = 0;
    if (af_agent_bool(args, "recursive", true)) {
        bool progress = true;
        while (progress) {
            progress = false;
            for (size_t i = af_scene_object_count(scene); i > 0; --i) {
                const af_scene_object *o = af_scene_object_at(scene, i - 1);
                if (o->id != id && af_scene_is_descendant(scene, o->id, id)) {
                    (void)af_scene_remove_object(scene, o->id);
                    ++removed;
                    progress = true;
                    break;
                }
            }
        }
    }
    if (!af_scene_remove_object(scene, id)) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to remove object.");
    }
    ++removed;

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "removed", (double)removed);
    cJSON_AddNumberToObject(data, "object_count", (double)af_scene_object_count(scene));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_object_duplicate(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("object_duplicate requires 'scene_path'.");

    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_object_id src_id = af_agent_resolve_object(scene, args);
    const af_scene_object *src = af_scene_get_object(scene, src_id);
    if (!src) {
        af_scene_destroy(scene);
        return af_agent_err("Source object not found.");
    }
    af_scene_object copy = *src;
    const char *new_name = af_agent_str(args, "new_name", copy.name);
    af_object_id dst_id = af_scene_add_object(scene, new_name, copy.mesh);
    if (dst_id == AF_OBJECT_NONE) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to duplicate object.");
    }
    af_vec3 offset = {0};
    if (af_agent_vec3(args, "offset", &offset))
        copy.transform.position = af_vec3_add(copy.transform.position, offset);
    (void)af_scene_set_object_transform(scene, dst_id, copy.transform);
    (void)af_scene_set_object_visible(scene, dst_id, copy.visible);
    (void)af_scene_set_object_material(scene, dst_id, copy.material);
    (void)af_scene_set_object_collider(scene, dst_id, copy.collider);
    (void)af_scene_set_body(scene, dst_id, copy.body);
    (void)af_scene_set_object_gameplay(scene, dst_id, copy.gameplay);
    (void)af_scene_set_object_animation(scene, dst_id, copy.animation);
    if (copy.parent != AF_OBJECT_NONE)
        (void)af_scene_set_parent(scene, dst_id, copy.parent);

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = serialize_object(scene, af_scene_get_object(scene, dst_id));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_collider_part_add(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("collider_part_add requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_object_id owner = af_agent_resolve_object(scene, args);
    if (owner == AF_OBJECT_NONE) {
        af_scene_destroy(scene);
        return af_agent_err("Owner object not found.");
    }

    af_collider_part part = af_collider_part_default();
    af_agent_copy_str(part.name, sizeof(part.name), af_agent_str(args, "part_name", "Part"));
    (void)af_agent_vec3(args, "position", &part.local.position);
    (void)af_agent_vec3(args, "rotation", &part.local.rotation);
    part.collider.type = parse_collider_type(af_agent_str(args, "type", "box"), AF_COLLIDER_BOX);
    (void)af_agent_vec3(args, "half_extents", &part.collider.half_extents);
    if (af_agent_has(args, "radius"))
        part.collider.radius = af_agent_f32(args, "radius", part.collider.radius);
    if (af_agent_has(args, "density"))
        part.density = af_agent_f32(args, "density", part.density);

    af_collider_part_id pid = af_scene_add_collider_part(scene, owner, part);
    if (!pid) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to add collider part (ensure owner collider is compound or valid part params).");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "part_id", af_agent_json_id(pid));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_collider_part_remove(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    af_collider_part_id pid = af_agent_id(args, "part_id");
    if (!path || !pid)
        return af_agent_err("collider_part_remove requires 'scene_path' and 'part_id'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    if (!af_scene_remove_collider_part(scene, pid)) {
        af_scene_destroy(scene);
        return af_agent_err("Collider part not found.");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static cJSON *cmd_joint_add(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("joint_add requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_joint j = af_joint_default();
    af_agent_copy_str(j.name, sizeof(j.name), af_agent_str(args, "name", "Joint"));
    const char *type = af_agent_str(args, "type", "hinge");
    j.type = !strcmp(type, "distance") ? AF_JOINT_DISTANCE : AF_JOINT_HINGE;
    j.enabled = af_agent_bool(args, "enabled", true);
    j.collide_connected = af_agent_bool(args, "collide_connected", false);
    j.a = af_agent_id(args, "object_a");
    j.b = af_agent_id(args, "object_b");
    (void)af_agent_vec3(args, "anchor_a", &j.anchor_a);
    (void)af_agent_vec3(args, "anchor_b", &j.anchor_b);
    if (af_agent_has(args, "distance"))
        j.distance = af_agent_f32(args, "distance", j.distance);
    (void)af_agent_vec3(args, "axis_a", &j.axis_a);
    (void)af_agent_vec3(args, "axis_b", &j.axis_b);
    j.limits_enabled = af_agent_bool(args, "limits_enabled", j.limits_enabled);
    if (af_agent_has(args, "lower_limit"))
        j.lower_limit = af_agent_f32(args, "lower_limit", j.lower_limit);
    if (af_agent_has(args, "upper_limit"))
        j.upper_limit = af_agent_f32(args, "upper_limit", j.upper_limit);
    j.motor_enabled = af_agent_bool(args, "motor_enabled", j.motor_enabled);
    if (af_agent_has(args, "motor_speed"))
        j.motor_speed = af_agent_f32(args, "motor_speed", j.motor_speed);
    if (af_agent_has(args, "max_motor_torque"))
        j.max_motor_torque = af_agent_f32(args, "max_motor_torque", j.max_motor_torque);

    af_joint_id jid = af_scene_add_joint(scene, j);
    if (!jid) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to add joint (check object_a/object_b IDs and joint limits).");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "joint_id", af_agent_json_id(jid));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_joint_remove(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    af_joint_id jid = af_agent_id(args, "joint_id");
    if (!path || !jid)
        return af_agent_err("joint_remove requires 'scene_path' and 'joint_id'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    if (!af_scene_remove_joint(scene, jid)) {
        af_scene_destroy(scene);
        return af_agent_err("Joint not found.");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static cJSON *cmd_asset_import(const cJSON *args) {
    const char *scene_path = af_agent_str(args, "scene_path", NULL);
    const char *name = af_agent_str(args, "name", NULL);
    const char *kind_str = af_agent_str(args, "kind", NULL);
    const char *asset_path = af_agent_str(args, "asset_path", NULL);
    if (!scene_path || !name || !kind_str || !asset_path)
        return af_agent_err("asset_import requires 'scene_path', 'name', 'kind', and 'asset_path'.");

    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(scene_path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    char resolved[AF_ASSET_PATH_CAPACITY];
    if (!af_asset_resolve_path(scene_path, asset_path, resolved, sizeof(resolved))) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to resolve asset path.");
    }

    af_asset_error aerr = {0};
    cJSON *data = cJSON_CreateObject();

    /* Full glTF / GLB hierarchy + materials + animations import. */
    if (!strcmp(kind_str, "model")) {
        af_object_id root = AF_OBJECT_NONE;
        if (!af_scene_import_model(scene, name, resolved, &root, &aerr)) {
            af_scene_destroy(scene);
            cJSON_Delete(data);
            return af_agent_err(aerr.message[0] ? aerr.message : "Model import failed.");
        }
        if (root != AF_OBJECT_NONE) {
            const af_scene_object *ro = af_scene_get_object(scene, root);
            if (ro) {
                af_transform tr = ro->transform;
                bool tr_changed = false;
                if (af_agent_vec3(args, "position", &tr.position))
                    tr_changed = true;
                if (af_agent_vec3(args, "rotation", &tr.rotation))
                    tr_changed = true;
                if (af_agent_vec3(args, "scale", &tr.scale))
                    tr_changed = true;
                if (tr_changed)
                    (void)af_scene_set_object_transform(scene, root, tr);
                if (ro->animation.asset[0])
                    cJSON_AddStringToObject(data, "animation_asset", ro->animation.asset);
            }
        }
        cJSON *err = NULL;
        if (!save_scene_or_err(scene, scene_path, &err)) {
            af_scene_destroy(scene);
            cJSON_Delete(data);
            return err;
        }
        cJSON_AddItemToObject(data, "root_id", af_agent_json_id(root));
        cJSON_AddStringToObject(data, "name", name);
        cJSON_AddStringToObject(data, "kind", "model");
        af_scene_destroy(scene);
        return af_agent_ok(data);
    }

    af_asset_kind kind;
    if (!parse_asset_kind(kind_str, &kind)) {
        af_scene_destroy(scene);
        cJSON_Delete(data);
        return af_agent_err("Unsupported asset kind (use: mesh, model, texture, normal_map, metallic_roughness, "
                            "environment, animation, audio).");
    }

    /* Validate asset load before registering. */
    bool valid = false;
    if (kind == AF_ASSET_MESH) {
        af_mesh_file mesh = {0};
        valid = af_mesh_file_load(resolved, &mesh, &aerr);
        if (valid) {
            cJSON_AddNumberToObject(data, "vertices", (double)mesh.data.vertex_count);
            cJSON_AddNumberToObject(data, "indices", (double)mesh.data.index_count);
            af_mesh_file_free(&mesh);
        }
    } else if (kind == AF_ASSET_TEXTURE || kind == AF_ASSET_NORMAL_MAP || kind == AF_ASSET_METALLIC_ROUGHNESS) {
        af_image_file img = {0};
        valid = af_image_file_load(resolved, &img, &aerr);
        if (valid) {
            cJSON_AddNumberToObject(data, "width", (double)img.width);
            cJSON_AddNumberToObject(data, "height", (double)img.height);
            af_image_file_free(&img);
        }
    } else if (kind == AF_ASSET_ENVIRONMENT) {
        af_hdr_file hdr = {0};
        valid = af_hdr_file_load(resolved, &hdr, &aerr);
        if (valid) {
            cJSON_AddNumberToObject(data, "width", (double)hdr.width);
            cJSON_AddNumberToObject(data, "height", (double)hdr.height);
            af_hdr_file_free(&hdr);
        }
    } else if (kind == AF_ASSET_ANIMATION) {
        af_animation_file anim = {0};
        valid = af_animation_file_load(resolved, &anim, &aerr);
        if (valid) {
            cJSON_AddNumberToObject(data, "clips", (double)anim.clip_count);
            cJSON_AddNumberToObject(data, "skins", (double)anim.skin_count);
            af_animation_file_free(&anim);
        }
    } else if (kind == AF_ASSET_AUDIO) {
        af_audio *audio = af_audio_create();
        valid = audio && af_audio_load(audio, name, resolved, &aerr);
        af_audio_destroy(audio);
    }

    if (!valid) {
        af_scene_destroy(scene);
        cJSON_Delete(data);
        return af_agent_err(aerr.message[0] ? aerr.message : "Asset validation failed.");
    }

    if (!af_scene_set_asset(scene, name, kind, asset_path)) {
        af_scene_destroy(scene);
        cJSON_Delete(data);
        return af_agent_err("Failed to register asset in scene.");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, scene_path, &err)) {
        af_scene_destroy(scene);
        cJSON_Delete(data);
        return err;
    }

    cJSON_AddStringToObject(data, "name", name);
    cJSON_AddStringToObject(data, "kind", asset_kind_str(kind));
    cJSON_AddStringToObject(data, "path", asset_path);
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_asset_remove(const cJSON *args) {
    const char *scene_path = af_agent_str(args, "scene_path", NULL);
    const char *name = af_agent_str(args, "name", NULL);
    if (!scene_path || !name)
        return af_agent_err("asset_remove requires 'scene_path' and 'name'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(scene_path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    if (!af_scene_remove_asset(scene, name)) {
        af_scene_destroy(scene);
        return af_agent_err("Asset not found in scene.");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, scene_path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

/* ---- Terrain & Foliage ---- */

static void apply_terrain_layer_json(af_terrain_layer *layer, const cJSON *j) {
    if (!layer || !j || !cJSON_IsObject(j))
        return;
    if (af_agent_has(j, "name"))
        af_agent_copy_str(layer->name, sizeof(layer->name), af_agent_str(j, "name", layer->name));
    if (af_agent_has(j, "diffuse_texture"))
        af_agent_copy_str(layer->diffuse_texture, sizeof(layer->diffuse_texture),
                          af_agent_str(j, "diffuse_texture", ""));
    if (af_agent_has(j, "normal_texture"))
        af_agent_copy_str(layer->normal_texture, sizeof(layer->normal_texture), af_agent_str(j, "normal_texture", ""));
    (void)af_agent_color(j, "color", &layer->color);
    if (af_agent_has(j, "roughness"))
        layer->roughness = af_agent_f32(j, "roughness", layer->roughness);
    if (af_agent_has(j, "metalness"))
        layer->metalness = af_agent_f32(j, "metalness", layer->metalness);
    if (af_agent_has(j, "uv_scale"))
        layer->uv_scale = af_agent_f32(j, "uv_scale", layer->uv_scale);
}

static cJSON *cmd_terrain_create(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path || !path[0])
        return af_agent_err("terrain_create requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    uint32_t cx = af_agent_u32(args, "chunks_x", 4u);
    uint32_t cz = af_agent_u32(args, "chunks_z", 4u);
    float spacing = af_agent_f32(args, "vertex_spacing", 1.0f);
    if (cx < 1u || cx > 16u || cz < 1u || cz > 16u || spacing <= 0.05f) {
        af_scene_destroy(scene);
        return af_agent_err("chunks_x/chunks_z must be in 1..16 and vertex_spacing > 0.05.");
    }

    af_landscape *ls = af_landscape_create(cx, cz, spacing);
    if (!ls) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to create landscape.");
    }
    af_vec3 origin;
    if (af_agent_vec3(args, "origin", &origin))
        ls->origin = origin;

    const cJSON *hills = cJSON_GetObjectItemCaseSensitive(args, "procedural_hills");
    if (hills && cJSON_IsObject(hills)) {
        float scale = af_agent_f32(hills, "scale", 0.05f);
        float amplitude = af_agent_f32(hills, "amplitude", 6.0f);
        uint32_t seed = af_agent_u32(hills, "seed", 1337u);
        (void)af_landscape_generate_procedural_hills(ls, scale, amplitude, seed);
    }

    const cJSON *layers = cJSON_GetObjectItemCaseSensitive(args, "layers");
    if (layers && cJSON_IsArray(layers)) {
        int n = cJSON_GetArraySize(layers);
        for (int i = 0; i < n && (size_t)i < AF_LANDSCAPE_MAX_LAYERS; ++i)
            apply_terrain_layer_json(&ls->layers[i], cJSON_GetArrayItem(layers, i));
    }

    if (af_agent_bool(args, "auto_paint_slopes", false)) {
        size_t rock = (size_t)af_agent_u32(args, "rock_layer", 1u);
        float min_slope = af_agent_f32(args, "min_slope_rad", 0.45f);
        size_t sand = (size_t)af_agent_u32(args, "sand_layer", 2u);
        float max_height = af_agent_f32(args, "sand_max_height", 1.0f);
        (void)af_landscape_auto_paint_slopes(ls, rock, min_slope, sand, max_height);
    }

    if (!af_scene_set_landscape(scene, ls)) {
        af_landscape_destroy(ls);
        af_scene_destroy(scene);
        return af_agent_err("Failed to attach landscape to scene.");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "chunks_x", (double)cx);
    cJSON_AddNumberToObject(data, "chunks_z", (double)cz);
    cJSON_AddNumberToObject(data, "world_size_x", (double)(cx * AF_LANDSCAPE_CHUNK_QUADS) * spacing);
    cJSON_AddNumberToObject(data, "world_size_z", (double)(cz * AF_LANDSCAPE_CHUNK_QUADS) * spacing);
    cJSON_AddItemToObject(data, "origin", af_agent_json_vec3(af_scene_get_landscape(scene)->origin));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_terrain_sculpt(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    const char *op = af_agent_str(args, "operation", NULL);
    if (!path || !op)
        return af_agent_err("terrain_sculpt requires 'scene_path' and 'operation'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    af_landscape *ls = af_scene_get_landscape_mut(scene);
    if (!ls) {
        af_scene_destroy(scene);
        return af_agent_err("Scene has no terrain; call terrain_create first.");
    }

    af_vec3 center = {0};
    (void)af_agent_vec3(args, "center", &center);
    float radius = af_agent_f32(args, "radius", 8.0f);
    af_brush_falloff falloff = parse_falloff(af_agent_str(args, "falloff", "smooth"));
    bool ok = false;

    if (!strcmp(op, "raise")) {
        ok = af_landscape_sculpt_raise_lower(ls, center, radius, fabsf(af_agent_f32(args, "delta", 2.0f)), falloff);
    } else if (!strcmp(op, "lower")) {
        ok = af_landscape_sculpt_raise_lower(ls, center, radius, -fabsf(af_agent_f32(args, "delta", 2.0f)), falloff);
    } else if (!strcmp(op, "smooth")) {
        ok = af_landscape_sculpt_smooth(ls, center, radius, af_agent_f32(args, "strength", 0.5f), falloff);
    } else if (!strcmp(op, "flatten")) {
        ok = af_landscape_sculpt_flatten(ls, center, radius, af_agent_f32(args, "target_height", 0.0f),
                                         af_agent_f32(args, "strength", 1.0f), falloff);
    } else if (!strcmp(op, "paint_layer")) {
        size_t layer = (size_t)af_agent_u32(args, "layer", 0u);
        ok = af_landscape_paint_layer(ls, center, radius, layer, af_agent_f32(args, "strength", 1.0f), falloff);
    } else if (!strcmp(op, "auto_paint_slopes")) {
        ok = af_landscape_auto_paint_slopes(ls, (size_t)af_agent_u32(args, "rock_layer", 1u),
                                            af_agent_f32(args, "min_slope_rad", 0.45f),
                                            (size_t)af_agent_u32(args, "sand_layer", 2u),
                                            af_agent_f32(args, "sand_max_height", 1.0f));
    } else if (!strcmp(op, "procedural_hills")) {
        ok = af_landscape_generate_procedural_hills(ls, af_agent_f32(args, "scale", 0.05f),
                                                    af_agent_f32(args, "amplitude", 6.0f),
                                                    af_agent_u32(args, "seed", 1337u));
    } else if (!strcmp(op, "set_layer")) {
        size_t idx = (size_t)af_agent_u32(args, "layer", 0u);
        if (idx < AF_LANDSCAPE_MAX_LAYERS) {
            apply_terrain_layer_json(&ls->layers[idx], args);
            ++ls->modification_version;
            ok = true;
        }
    }

    if (!ok) {
        af_scene_destroy(scene);
        return af_agent_err("Terrain sculpt operation failed or unknown operation.");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "center_height", af_landscape_get_height(ls, center.x, center.z));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_terrain_query(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("terrain_query requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    const af_landscape *ls = af_scene_get_landscape(scene);
    if (!ls) {
        af_scene_destroy(scene);
        return af_agent_err("Scene has no terrain.");
    }

    cJSON *results = cJSON_CreateArray();
    const cJSON *pts = cJSON_GetObjectItemCaseSensitive(args, "points");
    if (pts && cJSON_IsArray(pts)) {
        int n = cJSON_GetArraySize(pts);
        for (int i = 0; i < n; ++i) {
            const cJSON *pt = cJSON_GetArrayItem(pts, i);
            float x = 0, z = 0;
            if (cJSON_IsArray(pt) && cJSON_GetArraySize(pt) >= 2) {
                x = (float)cJSON_GetArrayItem(pt, 0)->valuedouble;
                z = (float)cJSON_GetArrayItem(pt, 1)->valuedouble;
            } else if (cJSON_IsObject(pt)) {
                x = af_agent_f32(pt, "x", 0.0f);
                z = af_agent_f32(pt, "z", 0.0f);
            }
            float h = af_landscape_get_height(ls, x, z);
            af_vec3 norm = {0, 1, 0};
            (void)af_landscape_get_normal(ls, x, z, &norm);
            float w[4] = {1, 0, 0, 0};
            (void)af_landscape_get_layer_weights(ls, x, z, w);
            cJSON *item = cJSON_CreateObject();
            cJSON_AddNumberToObject(item, "x", x);
            cJSON_AddNumberToObject(item, "z", z);
            cJSON_AddNumberToObject(item, "height", h);
            cJSON_AddItemToObject(item, "normal", af_agent_json_vec3(norm));
            cJSON *wj = cJSON_CreateArray();
            for (int k = 0; k < 4; ++k)
                cJSON_AddItemToArray(wj, cJSON_CreateNumber(w[k]));
            cJSON_AddItemToObject(item, "weights", wj);
            cJSON_AddItemToArray(results, item);
        }
    } else {
        float x = af_agent_f32(args, "x", 0.0f);
        float z = af_agent_f32(args, "z", 0.0f);
        float h = af_landscape_get_height(ls, x, z);
        af_vec3 norm = {0, 1, 0};
        (void)af_landscape_get_normal(ls, x, z, &norm);
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "x", x);
        cJSON_AddNumberToObject(item, "z", z);
        cJSON_AddNumberToObject(item, "height", h);
        cJSON_AddItemToObject(item, "normal", af_agent_json_vec3(norm));
        cJSON_AddItemToArray(results, item);
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddItemToObject(data, "samples", results);
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_foliage_add_layer(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    const char *name = af_agent_str(args, "name", "Foliage");
    const char *mesh = af_agent_str(args, "mesh", "cube");
    if (!path)
        return af_agent_err("foliage_add_layer requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    af_landscape *ls = af_scene_get_landscape_mut(scene);
    if (!ls) {
        af_scene_destroy(scene);
        return af_agent_err("Scene has no terrain; call terrain_create first.");
    }

    int idx = af_landscape_add_foliage_layer(ls, name, mesh);
    if (idx < 0) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to add foliage layer (limit 16 layers).");
    }

    af_foliage_layer *fl = &ls->foliage_layers[idx];
    fl->visible = af_agent_bool(args, "visible", true);
    fl->cast_shadows = af_agent_bool(args, "cast_shadows", true);
    if (af_agent_has(args, "max_distance"))
        fl->max_distance = af_agent_f32(args, "max_distance", fl->max_distance);
    const cJSON *mat = cJSON_GetObjectItemCaseSensitive(args, "material");
    if (mat && cJSON_IsObject(mat))
        apply_material_json(&fl->material, mat);

    const cJSON *rules = cJSON_GetObjectItemCaseSensitive(args, "rules");
    if (rules && cJSON_IsObject(rules)) {
        fl->rules.random_yaw = af_agent_bool(rules, "random_yaw", fl->rules.random_yaw);
        fl->rules.random_pitch_roll = af_agent_bool(rules, "random_pitch_roll", fl->rules.random_pitch_roll);
        fl->rules.uniform_scale = af_agent_bool(rules, "uniform_scale", fl->rules.uniform_scale);
        fl->rules.collision_avoidance = af_agent_bool(rules, "collision_avoidance", fl->rules.collision_avoidance);
        if (af_agent_has(rules, "max_tilt"))
            fl->rules.max_tilt = af_agent_f32(rules, "max_tilt", fl->rules.max_tilt);
        (void)af_agent_vec3(rules, "scale_min", &fl->rules.scale_min);
        (void)af_agent_vec3(rules, "scale_max", &fl->rules.scale_max);
        if (af_agent_has(rules, "slope_min"))
            fl->rules.slope_min = af_agent_f32(rules, "slope_min", fl->rules.slope_min);
        if (af_agent_has(rules, "slope_max"))
            fl->rules.slope_max = af_agent_f32(rules, "slope_max", fl->rules.slope_max);
        if (af_agent_has(rules, "height_min"))
            fl->rules.height_min = af_agent_f32(rules, "height_min", fl->rules.height_min);
        if (af_agent_has(rules, "height_max"))
            fl->rules.height_max = af_agent_f32(rules, "height_max", fl->rules.height_max);
        if (af_agent_has(rules, "density"))
            fl->rules.density = af_agent_f32(rules, "density", fl->rules.density);
        if (af_agent_has(rules, "min_spacing"))
            fl->rules.min_spacing = af_agent_f32(rules, "min_spacing", fl->rules.min_spacing);
        if (af_agent_has(rules, "align_to_normal"))
            fl->rules.align_to_normal = af_agent_f32(rules, "align_to_normal", fl->rules.align_to_normal);
        if (af_agent_has(rules, "offset_y"))
            fl->rules.offset_y = af_agent_f32(rules, "offset_y", fl->rules.offset_y);
        if (af_agent_has(rules, "paint_mask_layer"))
            fl->rules.paint_mask_layer = af_agent_i32(rules, "paint_mask_layer", fl->rules.paint_mask_layer);
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "layer_index", (double)idx);
    cJSON_AddStringToObject(data, "name", fl->name);
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_foliage_populate(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("foliage_populate requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    af_landscape *ls = af_scene_get_landscape_mut(scene);
    if (!ls) {
        af_scene_destroy(scene);
        return af_agent_err("Scene has no terrain.");
    }

    const char *mode = af_agent_str(args, "mode", "populate");
    int layer_idx = af_agent_i32(args, "layer_index", 0);
    uint32_t seed = af_agent_u32(args, "seed", 42u);
    bool ok = false;

    if (!strcmp(mode, "populate")) {
        if (layer_idx >= 0 && (size_t)layer_idx < ls->foliage_layer_count)
            ok = af_landscape_populate_procedural_foliage(ls, (size_t)layer_idx, seed);
    } else if (!strcmp(mode, "paint")) {
        af_vec3 center = {0};
        (void)af_agent_vec3(args, "center", &center);
        if (layer_idx >= 0 && (size_t)layer_idx < ls->foliage_layer_count)
            ok = af_landscape_paint_foliage(ls, (size_t)layer_idx, center, af_agent_f32(args, "radius", 6.0f),
                                            af_agent_f32(args, "strength", 1.0f), seed);
    } else if (!strcmp(mode, "erase")) {
        af_vec3 center = {0};
        (void)af_agent_vec3(args, "center", &center);
        ok = af_landscape_erase_foliage(ls, layer_idx, center, af_agent_f32(args, "radius", 6.0f));
    } else if (!strcmp(mode, "clear")) {
        af_landscape_clear_foliage(ls, layer_idx);
        ok = true;
    }

    if (!ok) {
        af_scene_destroy(scene);
        return af_agent_err("Foliage operation failed (check layer_index and mode).");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "total_foliage_instances", (double)ls->total_foliage_instances);
    if (layer_idx >= 0 && (size_t)layer_idx < ls->foliage_layer_count)
        cJSON_AddNumberToObject(data, "layer_instances", (double)ls->foliage_layers[layer_idx].total_instances);
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_foliage_remove_layer(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    uint32_t idx = af_agent_u32(args, "layer_index", UINT32_MAX);
    if (!path || idx == UINT32_MAX)
        return af_agent_err("foliage_remove_layer requires 'scene_path' and 'layer_index'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    af_landscape *ls = af_scene_get_landscape_mut(scene);
    if (!ls || !af_landscape_remove_foliage_layer(ls, idx)) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to remove foliage layer.");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

/* ---- Lighting, Post-Processing, Wind, Camera & Raycast ---- */

static cJSON *cmd_lighting_set_sun(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("lighting_set_sun requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_directional_light sun = af_scene_get_light(scene);
    (void)af_agent_vec3(args, "direction", &sun.direction);
    (void)af_agent_vec3(args, "color", &sun.color);
    if (af_agent_has(args, "intensity"))
        sun.intensity = af_agent_f32(args, "intensity", sun.intensity);
    if (af_agent_has(args, "ambient"))
        sun.ambient = af_agent_f32(args, "ambient", sun.ambient);
    if (af_agent_has(args, "angular_radius"))
        sun.angular_radius = af_agent_f32(args, "angular_radius", sun.angular_radius);
    if (!af_scene_set_light(scene, sun)) {
        af_scene_destroy(scene);
        return af_agent_err("Invalid directional light parameters.");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static void apply_local_light_json(af_local_light *l, const cJSON *args) {
    if (af_agent_has(args, "name"))
        af_agent_copy_str(l->name, sizeof(l->name), af_agent_str(args, "name", l->name));
    const char *type = af_agent_str(args, "type", NULL);
    if (type)
        l->type = !strcmp(type, "spot") ? AF_LIGHT_SPOT : AF_LIGHT_POINT;
    l->enabled = af_agent_bool(args, "enabled", l->enabled);
    (void)af_agent_vec3(args, "position", &l->position);
    (void)af_agent_vec3(args, "color", &l->color);
    if (af_agent_has(args, "intensity"))
        l->intensity = af_agent_f32(args, "intensity", l->intensity);
    if (af_agent_has(args, "range"))
        l->range = af_agent_f32(args, "range", l->range);
    (void)af_agent_vec3(args, "direction", &l->direction);
    l->cast_shadows = af_agent_bool(args, "cast_shadows", l->cast_shadows);
    if (af_agent_has(args, "shadow_bias"))
        l->shadow_bias = af_agent_f32(args, "shadow_bias", l->shadow_bias);
    if (af_agent_has(args, "inner_angle"))
        l->inner_angle = af_agent_f32(args, "inner_angle", l->inner_angle);
    if (af_agent_has(args, "outer_angle"))
        l->outer_angle = af_agent_f32(args, "outer_angle", l->outer_angle);
}

static cJSON *cmd_lighting_add_local(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("lighting_add_local requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    const char *type = af_agent_str(args, "type", "point");
    af_local_light l = af_local_light_default(!strcmp(type, "spot") ? AF_LIGHT_SPOT : AF_LIGHT_POINT);
    apply_local_light_json(&l, args);
    if (!af_scene_add_local_light(scene, l)) {
        af_scene_destroy(scene);
        return af_agent_err("Failed to add local light (check parameters or 128-light limit).");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = cJSON_CreateObject();
    cJSON_AddNumberToObject(data, "index", (double)(af_scene_local_light_count(scene) - 1));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_lighting_update_local(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    uint32_t idx = af_agent_u32(args, "index", UINT32_MAX);
    if (!path || idx == UINT32_MAX)
        return af_agent_err("lighting_update_local requires 'scene_path' and 'index'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    const af_local_light *cur = af_scene_local_light_at(scene, idx);
    if (!cur) {
        af_scene_destroy(scene);
        return af_agent_err("Local light index out of range.");
    }
    af_local_light l = *cur;
    apply_local_light_json(&l, args);
    if (!af_scene_set_local_light(scene, idx, l)) {
        af_scene_destroy(scene);
        return af_agent_err("Invalid local light parameters.");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static cJSON *cmd_lighting_remove_local(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    uint32_t idx = af_agent_u32(args, "index", UINT32_MAX);
    if (!path || idx == UINT32_MAX)
        return af_agent_err("lighting_remove_local requires 'scene_path' and 'index'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");
    if (!af_scene_remove_local_light(scene, idx)) {
        af_scene_destroy(scene);
        return af_agent_err("Local light index out of range.");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static cJSON *cmd_lighting_set_shadows(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("lighting_set_shadows requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_shadow_settings sh = af_scene_get_shadows(scene);
    sh.enabled = af_agent_bool(args, "enabled", sh.enabled);
    const char *mode = af_agent_str(args, "mode", NULL);
    if (mode)
        sh.mode = !strcmp(mode, "single") ? AF_SHADOW_SINGLE : AF_SHADOW_CASCADED;
    if (af_agent_has(args, "resolution"))
        sh.resolution = af_agent_u32(args, "resolution", sh.resolution);
    if (af_agent_has(args, "distance"))
        sh.distance = af_agent_f32(args, "distance", sh.distance);
    if (af_agent_has(args, "constant_bias"))
        sh.constant_bias = af_agent_f32(args, "constant_bias", sh.constant_bias);
    if (af_agent_has(args, "slope_bias"))
        sh.slope_bias = af_agent_f32(args, "slope_bias", sh.slope_bias);
    if (af_agent_has(args, "normal_bias"))
        sh.normal_bias = af_agent_f32(args, "normal_bias", sh.normal_bias);
    if (af_agent_has(args, "filter_radius"))
        sh.filter_radius = af_agent_u32(args, "filter_radius", sh.filter_radius);
    if (af_agent_has(args, "split_lambda"))
        sh.split_lambda = af_agent_f32(args, "split_lambda", sh.split_lambda);
    if (af_agent_has(args, "blend_fraction"))
        sh.blend_fraction = af_agent_f32(args, "blend_fraction", sh.blend_fraction);
    sh.debug_cascades = af_agent_bool(args, "debug_cascades", sh.debug_cascades);

    if (!af_scene_set_shadows(scene, sh)) {
        af_scene_destroy(scene);
        return af_agent_err("Invalid shadow settings (resolution must be 512/1024/2048/4096).");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static cJSON *cmd_render_set_settings(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("render_set_settings requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_render_settings r = af_scene_get_render_settings(scene);
    r.ray_traced_shadows = af_agent_bool(args, "ray_traced_shadows", r.ray_traced_shadows);
    r.ray_traced_water_reflections =
        af_agent_bool(args, "ray_traced_water_reflections", r.ray_traced_water_reflections);
    r.ray_traced_material_effects = af_agent_bool(args, "ray_traced_material_effects", r.ray_traced_material_effects);
    r.ray_traced_gi = af_agent_bool(args, "ray_traced_gi", r.ray_traced_gi);
    if (af_agent_has(args, "rtgi_intensity"))
        r.rtgi_intensity = af_agent_f32(args, "rtgi_intensity", r.rtgi_intensity);
    if (af_agent_has(args, "rtgi_ray_distance"))
        r.rtgi_ray_distance = af_agent_f32(args, "rtgi_ray_distance", r.rtgi_ray_distance);
    if (af_agent_has(args, "rtgi_samples"))
        r.rtgi_samples = af_agent_u32(args, "rtgi_samples", r.rtgi_samples);
    if (af_agent_has(args, "rtgi_bounces"))
        r.rtgi_bounces = af_agent_u32(args, "rtgi_bounces", r.rtgi_bounces);
    r.rtgi_emissive = af_agent_bool(args, "rtgi_emissive", r.rtgi_emissive);

    r.anti_aliasing = parse_aa(af_agent_str(args, "anti_aliasing", NULL), r.anti_aliasing);
    if (af_agent_has(args, "aa_sharpness"))
        r.aa_sharpness = af_agent_f32(args, "aa_sharpness", r.aa_sharpness);
    if (af_agent_has(args, "taa_history_weight"))
        r.taa_history_weight = af_agent_f32(args, "taa_history_weight", r.taa_history_weight);
    if (af_agent_has(args, "exposure"))
        r.exposure = af_agent_f32(args, "exposure", r.exposure);
    r.tone_mapping = af_agent_bool(args, "tone_mapping", r.tone_mapping);
    r.tone_mapper = parse_tone_mapper(af_agent_str(args, "tone_mapper", NULL), r.tone_mapper);
    r.auto_exposure = af_agent_bool(args, "auto_exposure", r.auto_exposure);
    if (af_agent_has(args, "saturation"))
        r.saturation = af_agent_f32(args, "saturation", r.saturation);
    if (af_agent_has(args, "contrast"))
        r.contrast = af_agent_f32(args, "contrast", r.contrast);
    (void)af_agent_vec3(args, "color_filter", &r.color_filter);

    r.depth_of_field = af_agent_bool(args, "depth_of_field", r.depth_of_field);
    if (af_agent_has(args, "dof_focus_distance"))
        r.dof_focus_distance = af_agent_f32(args, "dof_focus_distance", r.dof_focus_distance);
    if (af_agent_has(args, "dof_focus_range"))
        r.dof_focus_range = af_agent_f32(args, "dof_focus_range", r.dof_focus_range);
    if (af_agent_has(args, "dof_blur_radius"))
        r.dof_blur_radius = af_agent_f32(args, "dof_blur_radius", r.dof_blur_radius);

    r.motion_blur = af_agent_bool(args, "motion_blur", r.motion_blur);
    if (af_agent_has(args, "motion_blur_strength"))
        r.motion_blur_strength = af_agent_f32(args, "motion_blur_strength", r.motion_blur_strength);
    if (af_agent_has(args, "motion_blur_samples"))
        r.motion_blur_samples = af_agent_u32(args, "motion_blur_samples", r.motion_blur_samples);

    r.vignette = af_agent_bool(args, "vignette", r.vignette);
    if (af_agent_has(args, "vignette_intensity"))
        r.vignette_intensity = af_agent_f32(args, "vignette_intensity", r.vignette_intensity);
    if (af_agent_has(args, "vignette_smoothness"))
        r.vignette_smoothness = af_agent_f32(args, "vignette_smoothness", r.vignette_smoothness);

    r.chromatic_aberration = af_agent_bool(args, "chromatic_aberration", r.chromatic_aberration);
    if (af_agent_has(args, "chromatic_aberration_strength"))
        r.chromatic_aberration_strength =
            af_agent_f32(args, "chromatic_aberration_strength", r.chromatic_aberration_strength);

    r.film_grain = af_agent_bool(args, "film_grain", r.film_grain);
    if (af_agent_has(args, "film_grain_intensity"))
        r.film_grain_intensity = af_agent_f32(args, "film_grain_intensity", r.film_grain_intensity);

    r.underwater = af_agent_bool(args, "underwater", r.underwater);
    r.refraction = af_agent_bool(args, "refraction", r.refraction);
    if (af_agent_has(args, "refraction_strength"))
        r.refraction_strength = af_agent_f32(args, "refraction_strength", r.refraction_strength);

    r.bloom = af_agent_bool(args, "bloom", r.bloom);
    if (af_agent_has(args, "bloom_strength"))
        r.bloom_strength = af_agent_f32(args, "bloom_strength", r.bloom_strength);
    if (af_agent_has(args, "bloom_threshold"))
        r.bloom_threshold = af_agent_f32(args, "bloom_threshold", r.bloom_threshold);
    if (af_agent_has(args, "bloom_radius"))
        r.bloom_radius = af_agent_f32(args, "bloom_radius", r.bloom_radius);

    if (af_agent_has(args, "local_shadow_resolution"))
        r.local_shadow_resolution = af_agent_u32(args, "local_shadow_resolution", r.local_shadow_resolution);
    r.environment_enabled = af_agent_bool(args, "environment_enabled", r.environment_enabled);
    if (af_agent_has(args, "environment_profile"))
        r.environment_profile = af_agent_u32(args, "environment_profile", r.environment_profile);
    if (af_agent_has(args, "environment_asset"))
        af_agent_copy_str(r.environment_asset, sizeof(r.environment_asset),
                          af_agent_str(args, "environment_asset", ""));
    r.skybox = af_agent_bool(args, "skybox", r.skybox);
    if (af_agent_has(args, "skybox_intensity"))
        r.skybox_intensity = af_agent_f32(args, "skybox_intensity", r.skybox_intensity);
    if (af_agent_has(args, "environment_intensity"))
        r.environment_intensity = af_agent_f32(args, "environment_intensity", r.environment_intensity);
    if (af_agent_has(args, "environment_rotation"))
        r.environment_rotation = af_agent_f32(args, "environment_rotation", r.environment_rotation);

    if (!af_scene_set_render_settings(scene, r)) {
        af_scene_destroy(scene);
        return af_agent_err("Invalid render settings (check ranges, e.g. exposure in [-10,10], rtgi_samples in 1..4).");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static cJSON *cmd_wind_set(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("wind_set requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_wind w = af_scene_get_wind(scene);
    w.enabled = af_agent_bool(args, "enabled", true);
    if (af_agent_vec3(args, "direction", &w.direction)) {
        w.direction.y = 0.0f;
        float len = hypotf(w.direction.x, w.direction.z);
        if (len > 1e-5f) {
            w.direction.x /= len;
            w.direction.z /= len;
        }
    }
    if (af_agent_has(args, "speed"))
        w.speed = af_agent_f32(args, "speed", w.speed);
    if (af_agent_has(args, "gust_strength"))
        w.gust_strength = af_agent_f32(args, "gust_strength", w.gust_strength);
    if (af_agent_has(args, "gust_frequency"))
        w.gust_frequency = af_agent_f32(args, "gust_frequency", w.gust_frequency);
    if (af_agent_has(args, "air_density"))
        w.air_density = af_agent_f32(args, "air_density", w.air_density);
    if (!af_scene_set_wind(scene, w)) {
        af_scene_destroy(scene);
        return af_agent_err(
            "Invalid wind settings (speed in 0..60, gust_strength in 0..1, gust_frequency in 0..2, air_density in 0.01..10).");
    }
    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }
    af_scene_destroy(scene);
    return af_agent_ok(NULL);
}

static cJSON *cmd_camera_configure(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    if (!path)
        return af_agent_err("camera_configure requires 'scene_path'.");
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    /* Find requested camera object, or existing primary camera, or create one. */
    af_object_id id = af_agent_resolve_object(scene, args);
    if (id == AF_OBJECT_NONE) {
        for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
            const af_scene_object *o = af_scene_object_at(scene, i);
            if (o->gameplay.camera.enabled && o->gameplay.camera.primary) {
                id = o->id;
                break;
            }
        }
    }
    if (id == AF_OBJECT_NONE) {
        id = af_scene_add_object(scene, af_agent_str(args, "name", "Main Camera"), "__node");
        if (id == AF_OBJECT_NONE) {
            af_scene_destroy(scene);
            return af_agent_err("Failed to create camera object.");
        }
        (void)af_scene_set_object_visible(scene, id, false);
    }

    const af_scene_object *o = af_scene_get_object(scene, id);
    af_transform tr = o->transform;
    af_gameplay_component gp = o->gameplay;
    gp.camera.enabled = true;
    gp.camera.primary = af_agent_bool(args, "primary", true);
    if (af_agent_has(args, "eye_height"))
        gp.camera.eye_height = af_agent_f32(args, "eye_height", gp.camera.eye_height);
    if (af_agent_has(args, "fov_degrees"))
        gp.camera.vertical_fov = af_agent_f32(args, "fov_degrees", 60.0f) * (AF_PI / 180.0f);
    else if (af_agent_has(args, "vertical_fov"))
        gp.camera.vertical_fov = af_agent_f32(args, "vertical_fov", gp.camera.vertical_fov);
    if (af_agent_has(args, "near_clip"))
        gp.camera.near_clip = af_agent_f32(args, "near_clip", gp.camera.near_clip);
    if (af_agent_has(args, "far_clip"))
        gp.camera.far_clip = af_agent_f32(args, "far_clip", gp.camera.far_clip);

    (void)af_agent_vec3(args, "position", &tr.position);
    af_vec3 target = {0};
    if (af_agent_vec3(args, "look_at", &target)) {
        af_vec3 eye = {tr.position.x, tr.position.y + gp.camera.eye_height, tr.position.z};
        af_vec3 d = af_vec3_sub(target, eye);
        float len = af_vec3_length(d);
        if (len > 1e-5f) {
            d = af_vec3_scale(d, 1.0f / len);
            float limit = AF_PI * (89.0f / 180.0f);
            float rx = asinf(fminf(1.0f, fmaxf(-1.0f, d.y)));
            tr.rotation.x = fminf(limit, fmaxf(-limit, rx));
            tr.rotation.y = atan2f(-d.x, -d.z);
            tr.rotation.z = 0.0f;
        }
    } else {
        (void)af_agent_vec3(args, "rotation", &tr.rotation);
    }

    if (!af_scene_set_object_transform(scene, id, tr) || !af_scene_set_object_gameplay(scene, id, gp)) {
        af_scene_destroy(scene);
        return af_agent_err("Invalid camera transform or camera component settings.");
    }

    cJSON *err = NULL;
    if (!save_scene_or_err(scene, path, &err)) {
        af_scene_destroy(scene);
        return err;
    }

    cJSON *data = serialize_object(scene, af_scene_get_object(scene, id));
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

static cJSON *cmd_scene_raycast(const cJSON *args) {
    const char *path = af_agent_str(args, "scene_path", NULL);
    af_vec3 origin = {0}, dir = {0, -1, 0};
    if (!path || !af_agent_vec3(args, "origin", &origin) || !af_agent_vec3(args, "direction", &dir))
        return af_agent_err("scene_raycast requires 'scene_path', 'origin', and 'direction'.");
    float max_dist = af_agent_f32(args, "distance", 1000.0f);
    af_scene_error serr = {0};
    af_scene *scene = af_scene_load(path, &serr);
    if (!scene)
        return af_agent_err(serr.message[0] ? serr.message : "Failed to load scene.");

    af_query_filter filter = af_query_filter_default();
    filter.include_triggers = af_agent_bool(args, "include_triggers", false);
    af_collision_hit hit = {0};
    bool hit_obj = af_scene_raycast(scene, origin, dir, max_dist, filter, &hit);

    bool hit_terrain = false;
    af_vec3 t_pt = {0}, t_norm = {0, 1, 0};
    float t_dist = max_dist;
    const af_landscape *ls = af_scene_get_landscape(scene);
    if (ls && ls->enabled) {
        af_vec3 ndir = af_vec3_normalize(dir);
        if (af_landscape_raycast(ls, origin, ndir, &t_pt, &t_norm)) {
            t_dist = af_vec3_length(af_vec3_sub(t_pt, origin));
            if (t_dist <= max_dist)
                hit_terrain = true;
        }
    }

    cJSON *data = cJSON_CreateObject();
    bool choose_terrain = hit_terrain && (!hit_obj || t_dist < hit.distance);
    cJSON_AddBoolToObject(data, "hit", hit_obj || hit_terrain);
    if (choose_terrain) {
        cJSON_AddStringToObject(data, "target", "terrain");
        cJSON_AddItemToObject(data, "point", af_agent_json_vec3(t_pt));
        cJSON_AddItemToObject(data, "normal", af_agent_json_vec3(t_norm));
        cJSON_AddNumberToObject(data, "distance", t_dist);
    } else if (hit_obj) {
        cJSON_AddStringToObject(data, "target", "object");
        cJSON_AddItemToObject(data, "object_id", af_agent_json_id(hit.object));
        const af_scene_object *o = af_scene_get_object(scene, hit.object);
        if (o)
            cJSON_AddStringToObject(data, "object_name", o->name);
        cJSON_AddItemToObject(data, "point", af_agent_json_vec3(hit.point));
        cJSON_AddItemToObject(data, "normal", af_agent_json_vec3(hit.normal));
        cJSON_AddNumberToObject(data, "distance", hit.distance);
    }
    af_scene_destroy(scene);
    return af_agent_ok(data);
}

cJSON *af_agent_dispatch_scene(const char *cmd, const cJSON *args) {
    if (!args || !cJSON_IsObject(args))
        return af_agent_err("Command requires an 'args' JSON object.");
    if (!strcmp(cmd, "scene_create"))
        return cmd_scene_create(args);
    if (!strcmp(cmd, "scene_inspect"))
        return cmd_scene_inspect(args);
    if (!strcmp(cmd, "object_create"))
        return cmd_object_create(args);
    if (!strcmp(cmd, "object_update"))
        return cmd_object_update(args);
    if (!strcmp(cmd, "object_delete"))
        return cmd_object_delete(args);
    if (!strcmp(cmd, "object_duplicate"))
        return cmd_object_duplicate(args);
    if (!strcmp(cmd, "collider_part_add"))
        return cmd_collider_part_add(args);
    if (!strcmp(cmd, "collider_part_remove"))
        return cmd_collider_part_remove(args);
    if (!strcmp(cmd, "joint_add"))
        return cmd_joint_add(args);
    if (!strcmp(cmd, "joint_remove"))
        return cmd_joint_remove(args);
    if (!strcmp(cmd, "asset_import"))
        return cmd_asset_import(args);
    if (!strcmp(cmd, "asset_remove"))
        return cmd_asset_remove(args);
    if (!strcmp(cmd, "terrain_create"))
        return cmd_terrain_create(args);
    if (!strcmp(cmd, "terrain_sculpt"))
        return cmd_terrain_sculpt(args);
    if (!strcmp(cmd, "terrain_query"))
        return cmd_terrain_query(args);
    if (!strcmp(cmd, "foliage_add_layer"))
        return cmd_foliage_add_layer(args);
    if (!strcmp(cmd, "foliage_populate"))
        return cmd_foliage_populate(args);
    if (!strcmp(cmd, "foliage_remove_layer"))
        return cmd_foliage_remove_layer(args);
    if (!strcmp(cmd, "lighting_set_sun"))
        return cmd_lighting_set_sun(args);
    if (!strcmp(cmd, "lighting_add_local"))
        return cmd_lighting_add_local(args);
    if (!strcmp(cmd, "lighting_update_local"))
        return cmd_lighting_update_local(args);
    if (!strcmp(cmd, "lighting_remove_local"))
        return cmd_lighting_remove_local(args);
    if (!strcmp(cmd, "lighting_set_shadows"))
        return cmd_lighting_set_shadows(args);
    if (!strcmp(cmd, "render_set_settings"))
        return cmd_render_set_settings(args);
    if (!strcmp(cmd, "wind_set"))
        return cmd_wind_set(args);
    if (!strcmp(cmd, "camera_configure"))
        return cmd_camera_configure(args);
    if (!strcmp(cmd, "scene_raycast"))
        return cmd_scene_raycast(args);
    return af_agent_err("Unknown command.");
}

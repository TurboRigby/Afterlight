/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/scene.h>
#include <math.h>
af_gameplay_component af_gameplay_component_default(void) {
    return (af_gameplay_component){
        .player = {.controller = af_character_config_default(), .mouse_sensitivity = .0025f},
        .camera = {.eye_height = .75f, .vertical_fov = AF_PI / 3, .near_clip = .08f, .far_clip = 160}};
}
bool af_gameplay_component_valid(af_gameplay_component c) {
    return c.role >= AF_ROLE_NONE && c.role < AF_ROLE_COUNT && c.checkpoint_order <= 1024 &&
           af_character_config_valid(c.player.controller) && isfinite(c.player.mouse_sensitivity) &&
           c.player.mouse_sensitivity >= .0001f && c.player.mouse_sensitivity <= .05f &&
           isfinite(c.camera.eye_height) && fabsf(c.camera.eye_height) <= 10 && isfinite(c.camera.vertical_fov) &&
           c.camera.vertical_fov >= AF_PI / 18 && c.camera.vertical_fov <= AF_PI * 5 / 6 &&
           isfinite(c.camera.near_clip) && c.camera.near_clip >= .001f && isfinite(c.camera.far_clip) &&
           c.camera.far_clip > c.camera.near_clip && c.camera.far_clip <= 100000;
}
bool af_scene_set_object_gameplay(af_scene *scene, af_object_id id, af_gameplay_component value) {
    /* Scene views are const to callers; the scene owns this object. */
    af_scene_object *object = (af_scene_object *)af_scene_get_object(scene, id);
    if (!object || !af_gameplay_component_valid(value))
        return false;
    object->gameplay = value;
    return true;
}
bool af_scene_game_camera(const af_scene *scene, af_camera *out) {
    if (!scene || !out)
        return false;
    for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
        const af_scene_object *o = af_scene_object_at(scene, i);
        const af_camera_component *c = &o->gameplay.camera;
        if (!c->enabled || !c->primary)
            continue;
        af_mat4 world;
        if (!af_scene_world_matrix(scene, o->id, &world))
            return false;
        af_vec4 p = af_mat4_transform(world, (af_vec4){0, c->eye_height, 0, 1});
        af_vec3 forward = af_vec3_normalize((af_vec3){-world.elements[8], -world.elements[9], -world.elements[10]});
        if (af_vec3_length(forward) < .5f)
            return false;
        *out = (af_camera){.position = {p.x, p.y, p.z},
                           .yaw = atan2f(forward.z, forward.x),
                           .pitch = asinf(fminf(1, fmaxf(-1, forward.y))),
                           .vertical_fov = c->vertical_fov,
                           .near_clip = c->near_clip,
                           .far_clip = c->far_clip};
        return true;
    }
    return false;
}

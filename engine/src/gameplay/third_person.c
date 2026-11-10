/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/third_person.h>
#include <math.h>
static bool finite3(af_vec3 v) {
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
af_third_person_config af_third_person_config_default(void) {
    return (af_third_person_config){.controller = af_character_config_default(),
                                    .walk_speed = 2,
                                    .run_speed = 5,
                                    .turn_speed = 12,
                                    .camera_distance = 4.5f,
                                    .camera_min_distance = .15f,
                                    .camera_height = .65f,
                                    .camera_radius = .18f,
                                    .camera_recovery = 6,
                                    .mouse_sensitivity = .0025f};
}
bool af_third_person_config_valid(af_third_person_config c) {
    float v[] = {c.walk_speed,    c.run_speed,     c.turn_speed,      c.camera_distance,  c.camera_min_distance,
                 c.camera_height, c.camera_radius, c.camera_recovery, c.mouse_sensitivity};
    for (unsigned i = 0; i < 9; ++i)
        if (!isfinite(v[i]) || v[i] < 0)
            return false;
    return af_character_config_valid(c.controller) && c.walk_speed > 0 && c.run_speed >= c.walk_speed &&
           c.run_speed <= 100 && c.turn_speed <= 100 && c.camera_distance >= .2f && c.camera_distance <= 30 &&
           c.camera_min_distance <= c.camera_distance && c.camera_height <= 10 && c.camera_radius >= .01f &&
           c.camera_radius <= 1 && c.camera_recovery <= 100 && c.mouse_sensitivity <= .1f;
}
af_third_person af_third_person_default(af_vec3 position) {
    af_camera camera = af_camera_default();
    camera.pitch = -.2f;
    return (af_third_person){.body = {.position = position}, .orbit = camera, .camera_distance = 4.5f};
}
void af_third_person_look(af_third_person *p, af_third_person_config c, af_vec2 pixels) {
    if (p && af_third_person_config_valid(c) && isfinite(pixels.x) && isfinite(pixels.y))
        af_camera_rotate(&p->orbit, pixels.x * c.mouse_sensitivity, -pixels.y * c.mouse_sensitivity);
}
static bool step(const struct af_scene *scene, af_third_person *p, af_third_person_config c,
                 af_third_person_input input, float delta, af_query_filter filter, const af_vec3 *root) {
    if (!p || !af_third_person_config_valid(c) || !af_camera_is_valid(&p->orbit) || !isfinite(p->facing) ||
        !isfinite(input.move.x) || !isfinite(input.move.y) || !isfinite(delta) || delta <= 0 || delta > .1f)
        return false;
    af_third_person next = *p;
    af_vec3 forward = {cosf(p->orbit.yaw), 0, sinf(p->orbit.yaw)}, right = {-forward.z, 0, forward.x};
    af_vec3 direction = af_vec3_add(af_vec3_scale(forward, input.move.y), af_vec3_scale(right, input.move.x));
    float length = af_vec3_length(direction);
    if (!isfinite(length))
        return false;
    if (length > 1)
        direction = af_vec3_scale(direction, 1 / length);
    af_character_config controller = c.controller;
    controller.speed = input.run ? c.run_speed : c.walk_speed;
    if (length > .001f) {
        float target = atan2f(-direction.x, -direction.z);
        next.facing = remainderf(
            next.facing + remainderf(target - next.facing, 2 * AF_PI) * (1 - expf(-c.turn_speed * delta)), 2 * AF_PI);
    }
    if (root && p->body.grounded && !input.jump) {
        float sine = sinf(next.facing), cosine = cosf(next.facing);
        af_vec3 world = {root->x * cosine + root->z * sine, 0, -root->x * sine + root->z * cosine};
        if (!af_character_step_root_motion(scene, &next.body, controller, world, input.jump, delta, filter))
            return false;
    } else if (!af_character_step(scene, &next.body, controller,
                                  (af_character_input){.move = direction, .jump = input.jump}, delta, filter))
        return false;
    *p = next;
    return true;
}
bool af_third_person_step(const struct af_scene *scene, af_third_person *p, af_third_person_config c,
                          af_third_person_input input, float dt, af_query_filter filter) {
    return step(scene, p, c, input, dt, filter, NULL);
}
bool af_third_person_step_root_motion(const struct af_scene *scene, af_third_person *p, af_third_person_config c,
                                      af_third_person_input input, af_vec3 local_delta, float dt,
                                      af_query_filter filter) {
    if (!finite3(local_delta))
        return false;
    return step(scene, p, c, input, dt, filter, &local_delta);
}
bool af_third_person_camera(const struct af_scene *scene, af_third_person *p, af_third_person_config c, float delta,
                            af_query_filter filter, af_camera *out) {
    if (!scene || !p || !out || !af_third_person_config_valid(c) || !af_camera_is_valid(&p->orbit) ||
        !finite3(p->body.position) || !isfinite(p->camera_distance) || !isfinite(delta) || delta < 0 || delta > .1f)
        return false;
    af_vec3 target = p->body.position;
    target.y += c.camera_height;
    af_vec3 forward = af_camera_forward(&p->orbit), boom = af_vec3_scale(forward, -c.camera_distance);
    filter.include_triggers = false;
    af_collision_hit hit;
    float allowed = c.camera_distance;
    if (af_scene_sweep_capsule(scene, target, c.camera_radius, 0, boom, .01f, filter, &hit))
        allowed = fmaxf(0, c.camera_distance * hit.fraction - .01f);
    /* Obstructions retract immediately; easing only extends an already-clear
       boom. A very near obstruction may retract below the preferred minimum. */
    float distance = fminf(c.camera_distance, fmaxf(0, p->camera_distance));
    if (allowed < distance)
        distance = allowed;
    else
        distance = fminf(allowed, distance + (c.camera_distance - distance) * (1 - expf(-c.camera_recovery * delta)));
    if (allowed >= c.camera_min_distance)
        distance = fmaxf(c.camera_min_distance, distance);
    af_camera camera = p->orbit;
    camera.position = af_vec3_sub(target, af_vec3_scale(forward, distance));
    if (!af_camera_is_valid(&camera))
        return false;
    p->camera_distance = distance;
    *out = camera;
    return true;
}

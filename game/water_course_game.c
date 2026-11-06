/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/game_module.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
typedef struct course {
    af_character player;
    af_character_config config;
    af_swim_state swim;
    af_swim_config water;
    af_camera camera;
    af_gameplay_status status;
    af_object_id body, source, receiver, fluid;
    af_vec3 spawn, checkpoint, published;
    float tilt, received;
    bool hard, tipping;
} course;
static float distance(af_vec3 a, af_vec3 b) {
    float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return sqrtf(x * x + y * y + z * z);
}
static bool finite3(af_vec3 p) { return isfinite(p.x) && isfinite(p.y) && isfinite(p.z); }
static void camera(course *s, const af_game_host *h) {
    s->camera.position = s->player.position;
    s->camera.position.y += .65f;
    if (h->camera_enabled)
        h->set_camera(h->engine, &s->camera);
}
static void publish(course *s, const af_game_host *h) {
    const af_scene_object *o = h->get_object(h->scene, s->body);
    if (o) {
        af_transform t = o->transform;
        t.position = s->player.position;
        t.rotation.y = s->camera.yaw + AF_PI / 2;
        h->set_transform(h->scene, s->body, t);
        s->published = t.position;
    }
    camera(s, h);
    if (s->status.finished)
        snprintf(s->status.hint, sizeof(s->status.hint),
                 "Course complete! R restarts | N changes course");
    else if (s->hard && s->status.checkpoint == s->status.checkpoint_count && s->received < .05f)
        snprintf(s->status.hint, sizeof(s->status.hint),
                 "Fill receiving cup: %.0f / 50 litres | E near the source cup", s->received * 1000);
    else
        snprintf(s->status.hint, sizeof(s->status.hint),
                 "%s | Space up/jump | Ctrl dive | F grab | V throw%s",
                 s->swim.head_underwater ? "Diving"
                 : s->swim.swimming      ? "Swimming"
                                         : "Walk to the next marker",
                 s->hard ? " | E pour" : "");
    h->report_gameplay(h->engine, s->status);
}
static bool init(void *memory, const af_game_host *h) {
    course *s = memory;
    for (size_t i = 0; i < h->object_count(h->scene); ++i) {
        const af_scene_object *o = h->object_at(h->scene, i);
        if (o->gameplay.player.enabled) {
            if (s->body)
                return false;
            s->body = o->id;
            s->spawn = o->transform.position;
            s->config = o->gameplay.player.controller;
        }
        if (o->gameplay.role == AF_ROLE_CHECKPOINT)
            ++s->status.checkpoint_count;
        if (!strcmp(o->name, "Source cup"))
            s->source = o->id;
        if (!strcmp(o->name, "Receiving cup"))
            s->receiver = o->id;
        if (!strcmp(o->name, "Course pour"))
            s->fluid = o->id;
    }
    if (!s->body)
        return false;
    s->hard = s->receiver != 0;
    s->checkpoint = s->spawn;
    s->player.position = s->spawn;
    s->published = s->spawn;
    s->water = h->swim_config_default();
    s->camera = (af_camera){.yaw = -AF_PI / 2,
                            .pitch = -.12f,
                            .vertical_fov = AF_PI / 3,
                            .near_clip = .05f,
                            .far_clip = 180};
    publish(s, h);
    h->capture_mouse(h->engine, true);
    h->reset_interpolation(h->engine);
    h->log("Water course: reach markers in order. Space swims up/jumps, Ctrl dives, F/V grab/throw. E "
           "tips the lab cup. R resets, N changes course.");
    return true;
}
static void frame(void *memory, const af_game_host *h, double dt) {
    (void)dt;
    course *s = memory;
    if (h->input_enabled) {
        if (h->key_pressed(h, AF_KEY_ESCAPE))
            h->capture_mouse(h->engine, false);
        else if (h->mouse_pressed(h->engine, AF_MOUSE_LEFT) && h->mouse_in_viewport(h->engine))
            h->capture_mouse(h->engine, true);
        if (h->mouse_captured(h->engine)) {
            af_vec2 m = h->mouse_delta(h->engine);
            s->camera.yaw += m.x * .0025f;
            s->camera.pitch = fmaxf(-1.5f, fminf(1.5f, s->camera.pitch - m.y * .0025f));
        }
    }
    camera(s, h);
}
static void update(void *memory, const af_game_host *h, double delta) {
    course *s = memory;
    float dt = (float)delta;
    bool controls = h->input_enabled && h->mouse_captured(h->engine);
    if (controls && h->key_pressed(h, AF_KEY_R)) {
        if (h->request_scene)
            h->request_scene(h, s->hard ? "water-expedition" : "water-course");
        return;
    }
    if (controls && h->key_pressed(h, AF_KEY_N)) {
        if (h->request_scene)
            h->request_scene(h, s->hard ? "water-course" : "water-expedition");
        return;
    }
    const af_scene_object *body = h->get_object(h->scene, s->body);
    if (!body)
        return;
    if (distance(body->transform.position, s->published) > .001f) {
        s->player = (af_character){.position = body->transform.position};
        s->swim = (af_swim_state){0};
        h->reset_interpolation(h->engine);
    }
    if (s->source) {
        const af_scene_object *cup = h->get_object(h->scene, s->source);
        if (cup) {
            if (controls && h->key_pressed(h, AF_KEY_E) &&
                distance(s->player.position, cup->transform.position) < 3)
                s->tipping = !s->tipping;
            float target = s->tipping ? -1.65f : 0;
            s->tilt += fmaxf(-dt * .65f, fminf(dt * .65f, target - s->tilt));
            af_transform t = cup->transform;
            t.rotation.z = s->tilt;
            h->set_transform(h->scene, s->source, t);
        }
    }
    af_swim_input input = {0};
    if (controls && !s->status.finished) {
        float forward = (h->key_down(h, AF_KEY_W) ? 1.f : 0) - (h->key_down(h, AF_KEY_S) ? 1.f : 0),
              right = (h->key_down(h, AF_KEY_D) ? 1.f : 0) - (h->key_down(h, AF_KEY_A) ? 1.f : 0);
        input.move = (af_vec3){cosf(s->camera.yaw) * forward - sinf(s->camera.yaw) * right, 0,
                               sinf(s->camera.yaw) * forward + cosf(s->camera.yaw) * right};
        input.vertical =
            (h->key_down(h, AF_KEY_SPACE) ? 1.f : 0) - (h->key_down(h, AF_KEY_LEFT_CONTROL) ? 1.f : 0);
        input.jump = h->key_pressed(h, AF_KEY_SPACE);
        if (forward != 0.f || right != 0.f || input.vertical != 0.f || input.jump)
            s->status.running = true;
    }
    af_character_config config = s->config;
    if (controls && h->key_down(h, AF_KEY_LEFT_SHIFT))
        config.speed *= 1.5f;
    af_query_filter filter = {.ignore = s->body, .mask = UINT32_MAX};
    if (!h->swim_character(h->engine, &s->player, config, &s->swim, s->water, input, dt, filter)) {
        h->log("Water controller rejected input.");
        return;
    }
    if (s->status.running && !s->status.finished)
        s->status.elapsed += delta;
    if (s->player.position.y < -6) {
        ++s->status.deaths;
        s->player = (af_character){.position = s->checkpoint};
        s->swim = (af_swim_state){0};
        h->reset_interpolation(h->engine);
    }
    af_vec3 forward = {cosf(s->camera.yaw) * cosf(s->camera.pitch), sinf(s->camera.pitch),
                       sinf(s->camera.yaw) * cosf(s->camera.pitch)};
    camera(s, h);
    if (controls && h->key_pressed(h, AF_KEY_F)) {
        if (h->held_body(h->engine))
            h->release_body(h->engine, (af_vec3){0});
        else {
            af_collision_hit hit;
            if (h->raycast(h->scene, s->camera.position, forward, 6, filter, &hit))
                h->grab_body(h->engine, hit.object, hit.point);
        }
    }
    if (h->held_body(h->engine)) {
        af_vec3 target = {s->camera.position.x + forward.x * 2, s->camera.position.y + forward.y * 2,
                          s->camera.position.z + forward.z * 2};
        h->move_grab(h->engine, target);
        if (controls && h->key_pressed(h, AF_KEY_V))
            h->release_body(h->engine, (af_vec3){forward.x * 8, forward.y * 8 + 1, forward.z * 8});
    }
    s->received = 0;
    if (s->hard && s->receiver && s->fluid) {
        const af_scene_object *cup = h->get_object(h->scene, s->receiver);
        af_water_particle particles[4096];
        size_t count = h->water_particles(h->engine, s->fluid, particles, 4096);
        for (size_t i = 0; cup && i < count && i < 4096; ++i) {
            af_vec3 p = particles[i].position, c = cup->transform.position;
            if (fabsf(p.x - c.x) < .64f && fabsf(p.z - c.z) < .64f && p.y > c.y && p.y < c.y + 1.2f)
                s->received += particles[i].volume;
        }
    }
    af_collision_hit hits[64];
    filter.include_triggers = true;
    size_t count = h->overlap_capsule(h->scene, s->player.position, s->config.radius,
                                      s->config.half_height, filter, hits, 64);
    for (size_t i = 0; i < count && i < 64; ++i) {
        const af_scene_object *o = h->get_object(h->scene, hits[i].object);
        if (!o)
            continue;
        if (o->gameplay.role == AF_ROLE_CHECKPOINT &&
            o->gameplay.checkpoint_order == s->status.checkpoint + 1) {
            af_mat4 m;
            if (h->world_matrix(h->scene, o->id, &m)) {
                s->status.checkpoint = o->gameplay.checkpoint_order;
                s->checkpoint = (af_vec3){m.elements[12], m.elements[13], m.elements[14]};
                h->log("Water checkpoint reached.");
            }
        }
        if (o->gameplay.role == AF_ROLE_GOAL && s->status.checkpoint == s->status.checkpoint_count &&
            (!s->hard || s->received >= .05f)) {
            s->status.finished = true;
            s->status.running = false;
            s->status.finish_time = s->status.elapsed;
        }
    }
    publish(s, h);
}
static void shutdown(void *memory, const af_game_host *h) {
    (void)memory;
    if (h->held_body(h->engine))
        h->release_body(h->engine, (af_vec3){0});
    h->capture_mouse(h->engine, false);
}
static size_t save(const void *memory, void *bytes, size_t cap) {
    if (!bytes)
        return sizeof(course);
    if (cap < sizeof(course))
        return 0;
    memcpy(bytes, memory, sizeof(course));
    return sizeof(course);
}
static bool load(void *memory, const af_game_host *h, uint32_t version, const void *bytes, size_t size) {
    if (version != 1 || size != sizeof(course))
        return false;
    course copy;
    memcpy(&copy, bytes, size);
    if (!finite3(copy.player.position) || !finite3(copy.player.velocity) || !isfinite(copy.camera.yaw) ||
        !isfinite(copy.camera.pitch) || !isfinite(copy.status.elapsed) ||
        copy.status.checkpoint > copy.status.checkpoint_count || !h->get_object(h->scene, copy.body))
        return false;
    *(course *)memory = copy;
    publish(memory, h);
    return true;
}
AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t abi) {
    static const af_game_module api = {.abi_version = AF_GAME_ABI_VERSION,
                                       .struct_size = sizeof(api),
                                       .host_size = sizeof(af_game_host),
                                       .object_size = sizeof(af_scene_object),
                                       .name = "Playable water course",
                                       .state_size = sizeof(course),
                                       .init = init,
                                       .update = update,
                                       .frame_update = frame,
                                       .shutdown = shutdown,
                                       .state_version = 1,
                                       .save_state = save,
                                       .load_state = load};
    return abi == AF_GAME_ABI_VERSION ? &api : NULL;
}

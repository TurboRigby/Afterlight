/* SPDX-License-Identifier: MPL-2.0
 * LUMINA: Deep Core
 * A Ray-Tracing Showcase Game for Afterlight Engine
 */

#include <afterlight/game_module.h>
#include <afterlight/water.h>
#include <afterlight/input.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

enum { CELL_COUNT = 3 };

typedef struct lumina_state {
    af_third_person player;
    af_third_person_config third_cfg;
    af_camera camera;
    af_vec3 spawn_pos;
    float yaw, pitch;
    bool first_person;
    bool flashlight_on;

    af_object_id player_id;
    af_object_id core_id;
    af_object_id gate_id;
    af_object_id lift_id;
    af_object_id water_id;

    af_object_id cells[CELL_COUNT];
    af_transform cell_initial[CELL_COUNT];
    af_object_id relays[CELL_COUNT];
    af_transform relay_poses[CELL_COUNT];

    int held_cell; // -1 if none, 0..2
    bool relay_active[CELL_COUNT];

    double run_time;
    bool core_stabilized;
    bool game_won;

    float gate_open_progress;
    float gate_base_y;
    float core_spin_angle;
    float core_pulse_phase;

    af_gameplay_status status;
} lumina_state;

typedef struct lumina_save_blob {
    uint32_t version;
    double run_time;
    int held_cell;
    uint8_t relay_active[CELL_COUNT];
    uint8_t core_stabilized;
    uint8_t game_won;
    uint8_t first_person;
    uint8_t flashlight_on;
    float player_pos[3];
    float yaw, pitch;
} lumina_save_blob;

static float vec3_dist(af_vec3 a, af_vec3 b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

static af_vec3 get_camera_forward(float yaw, float pitch) {
    float horizontal = cosf(pitch);
    return (af_vec3){cosf(yaw) * horizontal, sinf(pitch), sinf(yaw) * horizontal};
}

static bool init(void *memory, const af_game_host *h) {
    lumina_state *s = memory;
    memset(s, 0, sizeof(*s));

    s->first_person = true;
    s->flashlight_on = true;
    s->held_cell = -1;
    s->third_cfg = h->third_person_config_default();
    s->third_cfg.camera_distance = 4.0f;
    s->third_cfg.camera_height = 0.8f;

    // Scan scene objects
    size_t count = h->object_count(h->scene);
    for (size_t i = 0; i < count; ++i) {
        const af_scene_object *o = h->object_at(h->scene, i);
        if (!strcmp(o->name, "Player")) {
            s->player_id = o->id;
            s->spawn_pos = o->transform.position;
        } else if (!strcmp(o->name, "Reactor_Core")) {
            s->core_id = o->id;
        } else if (!strcmp(o->name, "Exit_Barrier")) {
            s->gate_id = o->id;
            s->gate_base_y = o->transform.position.y;
        } else if (!strcmp(o->name, "Extraction_Lift")) {
            s->lift_id = o->id;
        } else if (!strcmp(o->name, "Cooling_Basin_Water")) {
            s->water_id = o->id;
        } else if (!strcmp(o->name, "Cell_Alpha")) {
            s->cells[0] = o->id;
            s->cell_initial[0] = o->transform;
        } else if (!strcmp(o->name, "Cell_Beta")) {
            s->cells[1] = o->id;
            s->cell_initial[1] = o->transform;
        } else if (!strcmp(o->name, "Cell_Gamma")) {
            s->cells[2] = o->id;
            s->cell_initial[2] = o->transform;
        } else if (!strcmp(o->name, "Relay_Coolant")) {
            s->relays[0] = o->id;
            s->relay_poses[0] = o->transform;
        } else if (!strcmp(o->name, "Relay_Optics")) {
            s->relays[1] = o->id;
            s->relay_poses[1] = o->transform;
        } else if (!strcmp(o->name, "Relay_Magnetic")) {
            s->relays[2] = o->id;
            s->relay_poses[2] = o->transform;
        }
    }

    if (!s->player_id)
        s->spawn_pos = (af_vec3){0, 1.05f, 10.0f};
    else if (s->spawn_pos.y < 1.0f)
        s->spawn_pos.y = 1.05f;

    s->player = h->third_person_default(s->spawn_pos);
    s->yaw = -AF_PI * 0.5f; // Face North toward reactor core (-Z)
    s->pitch = 0.0f;
    s->player.orbit.yaw = s->yaw;
    s->player.orbit.pitch = s->pitch;

    // In first person, collapse scale so the camera is never inside geometry
    if (s->player_id) {
        const af_scene_object *pobj = h->get_object(h->scene, s->player_id);
        if (pobj) {
            af_transform t = pobj->transform;
            t.scale = s->first_person ? (af_vec3){0, 0, 0} : (af_vec3){1.0f, 1.0f, 1.0f};
            h->set_transform(h->scene, s->player_id, t);
        }
    }

    h->capture_mouse(h->engine, true);
    h->log("LUMINA: DEEP CORE | Containment blackout detected. Retrieve 3 Quantum Prisms to stabilize the Core.");
    return true;
}

static void update(void *memory, const af_game_host *h, double dt) {
    lumina_state *s = memory;
    s->run_time += dt;

    // Toggle Flashlight (F key)
    if (h->input_enabled && h->key_pressed(h, AF_KEY_F)) {
        s->flashlight_on = !s->flashlight_on;
        h->log(s->flashlight_on ? "Scanner Spotlight: ACTIVE" : "Scanner Spotlight: OFF");
    }

    // Toggle Camera View (V key)
    if (h->input_enabled && h->key_pressed(h, AF_KEY_V)) {
        s->first_person = !s->first_person;
        h->log(s->first_person ? "View: First-Person" : "View: Third-Person");
    }

    // Reset Run (R key)
    if (h->input_enabled && h->key_pressed(h, AF_KEY_R)) {
        s->player = h->third_person_default(s->spawn_pos);
        s->yaw = -AF_PI * 0.5f;
        s->pitch = 0.0f;
        s->player.orbit.yaw = s->yaw;
        s->player.orbit.pitch = s->pitch;
        s->held_cell = -1;
        s->core_stabilized = false;
        s->game_won = false;
        s->run_time = 0.0;
        for (int i = 0; i < CELL_COUNT; ++i) {
            s->relay_active[i] = false;
            if (s->cells[i])
                h->set_transform(h->scene, s->cells[i], s->cell_initial[i]);
            if (s->relays[i]) {
                const af_scene_object *ro = h->get_object(h->scene, s->relays[i]);
                if (ro) {
                    af_material m = ro->material;
                    m.emission_strength = 0.3f;
                    h->set_material(h->scene, s->relays[i], m);
                }
            }
        }
        h->log("LUMINA: Run reset to spawn.");
    }

    // Character Movement Input
    af_third_person_input input = {0};
    if (h->input_enabled && !s->game_won) {
        float fwd = 0.0f, right = 0.0f;
        if (h->key_down(h, AF_KEY_W))
            fwd += 1.0f;
        if (h->key_down(h, AF_KEY_S))
            fwd -= 1.0f;
        if (h->key_down(h, AF_KEY_D))
            right += 1.0f;
        if (h->key_down(h, AF_KEY_A))
            right -= 1.0f;

        // Also check action bindings if set
        fwd += h->action_value(h, "MoveY");
        right += h->action_value(h, "MoveX");

        input.move = (af_vec2){right, fwd};
        input.run = h->key_down(h, AF_KEY_LEFT_SHIFT) || (h->action_value(h, "Run") > 0.5f);
        input.jump = h->key_pressed(h, AF_KEY_SPACE) || h->action_pressed(h, "Jump");
    }

    // Synchronize orbit camera yaw with look yaw so character steps in camera forward direction
    s->player.orbit.yaw = s->yaw;
    s->player.orbit.pitch = s->pitch;

    // Step physics & collision
    af_query_filter filter = {.ignore = s->player_id, .mask = UINT32_MAX};
    h->third_person_step(h->scene, &s->player, s->third_cfg, input, (float)dt, filter);

    // Sync player object position and scale (collapsed in first-person so geometry never clips view)
    if (s->player_id) {
        const af_scene_object *pobj = h->get_object(h->scene, s->player_id);
        if (pobj) {
            af_transform t = pobj->transform;
            t.position = s->player.body.position;
            t.rotation.y = s->yaw + AF_PI * 0.5f;
            t.scale = s->first_person ? (af_vec3){0, 0, 0} : (af_vec3){1.0f, 1.0f, 1.0f};
            h->set_transform(h->scene, s->player_id, t);
        }
    }

    // Water ripple disturbance when wading in the flooded basin
    if (s->water_id && s->player.body.position.y < 0.3f) {
        h->disturb_water(h->engine, s->water_id, s->player.body.position, 1.2f, 1.5f);
    }

    // Interaction handling (E key or Left Mouse Click when captured)
    bool interact = h->input_enabled && (h->key_pressed(h, AF_KEY_E) || (h->mouse_captured(h->engine) && h->mouse_pressed(h->engine, AF_MOUSE_LEFT)));

    const char *prompt_action = NULL;

    // Check interaction with cells or relays
    if (s->held_cell < 0) {
        // Find nearest unplaced cell
        float best_dist = 3.0f;
        int target_cell = -1;
        for (int i = 0; i < CELL_COUNT; ++i) {
            if (s->relay_active[i] || !s->cells[i])
                continue;
            const af_scene_object *co = h->get_object(h->scene, s->cells[i]);
            if (!co)
                continue;
            float d = vec3_dist(s->player.body.position, co->transform.position);
            if (d < best_dist) {
                best_dist = d;
                target_cell = i;
            }
        }

        if (target_cell >= 0) {
            static const char *names[] = {"Alpha Prism (Cyan)", "Beta Prism (Amber)", "Gamma Prism (Crimson)"};
            prompt_action = target_cell == 0
                                ? "[E] Retrieve Coolant Prism"
                                : (target_cell == 1 ? "[E] Retrieve Optical Prism" : "[E] Retrieve Magnetic Prism");
            if (interact) {
                s->held_cell = target_cell;
                char msg[128];
                snprintf(msg, sizeof(msg), "Acquired %s! Bring it to the corresponding Relay Pedestal.",
                         names[target_cell]);
                h->log(msg);
            }
        }
    } else {
        // Holding a cell: check distance to matching relay
        int cell_idx = s->held_cell;
        if (s->relays[cell_idx]) {
            const af_scene_object *ro = h->get_object(h->scene, s->relays[cell_idx]);
            if (ro) {
                float d = vec3_dist(s->player.body.position, ro->transform.position);
                if (d < 3.2f) {
                    prompt_action = "[E] Insert Prism into Relay Dock";
                    if (interact) {
                        s->relay_active[cell_idx] = true;
                        // Snap cell onto pedestal dock
                        af_transform t = ro->transform;
                        t.position.y += 1.15f;
                        t.scale = (af_vec3){0.8f, 0.8f, 0.8f};
                        h->set_transform(h->scene, s->cells[cell_idx], t);

                        // Boost relay emission
                        af_material mat = ro->material;
                        mat.emission_strength = 3.0f;
                        h->set_material(h->scene, s->relays[cell_idx], mat);

                        // Also boost relay beacon local light (lights 5, 6, 7)
                        size_t light_idx = 5 + (size_t)cell_idx;
                        if (light_idx < h->local_light_count(h->scene)) {
                            const af_local_light *cur = h->local_light_at(h->scene, light_idx);
                            if (cur) {
                                af_local_light l = *cur;
                                l.intensity = 5.0f;
                                l.range = 14.0f;
                                h->set_local_light(h->scene, light_idx, l);
                            }
                        }

                        s->held_cell = -1;
                        h->log("Relay resonant dock synchronized!");
                    }
                } else {
                    prompt_action = "[E] Drop Prism";
                    if (interact) {
                        // Drop in front of player
                        af_vec3 fwd = get_camera_forward(s->yaw, 0);
                        const af_scene_object *co = h->get_object(h->scene, s->cells[cell_idx]);
                        if (co) {
                            af_transform t = co->transform;
                            t.position =
                                (af_vec3){s->player.body.position.x + fwd.x * 1.5f, s->player.body.position.y + 0.3f,
                                          s->player.body.position.z + fwd.z * 1.5f};
                            h->set_transform(h->scene, s->cells[cell_idx], t);
                        }
                        s->held_cell = -1;
                        h->log("Prism placed on deck.");
                    }
                }
            }
        }
    }

    // If carrying a cell: position it floating next to player & sync light
    if (s->held_cell >= 0) {
        int cell_idx = s->held_cell;
        af_vec3 fwd = get_camera_forward(s->yaw, 0);
        af_vec3 right_vec = {-fwd.z, 0, fwd.x};
        af_vec3 carry_pos = {s->player.body.position.x + fwd.x * 0.7f + right_vec.x * 0.4f,
                             s->player.body.position.y + 0.6f + sinf((float)s->run_time * 4.0f) * 0.05f,
                             s->player.body.position.z + fwd.z * 0.7f + right_vec.z * 0.4f};
        const af_scene_object *co = h->get_object(h->scene, s->cells[cell_idx]);
        if (co) {
            af_transform t = co->transform;
            t.position = carry_pos;
            t.rotation.y = (float)s->run_time * 1.5f;
            h->set_transform(h->scene, s->cells[cell_idx], t);
        }
        // Move cell's point light (lights 2, 3, 4) to follow player
        size_t cell_light_idx = 2 + (size_t)cell_idx;
        if (cell_light_idx < h->local_light_count(h->scene)) {
            const af_local_light *cur = h->local_light_at(h->scene, cell_light_idx);
            if (cur) {
                af_local_light l = *cur;
                l.position = carry_pos;
                l.intensity = 4.5f;
                l.range = 16.0f;
                h->set_local_light(h->scene, cell_light_idx, l);
            }
        }
    }

    // Check Core Stabilization
    bool all_active = s->relay_active[0] && s->relay_active[1] && s->relay_active[2];
    if (all_active && !s->core_stabilized) {
        s->core_stabilized = true;
        h->log("CRITICAL UPDATE: Quantum Core Stabilized! Emergency Blast Gate unlocking at South Deck.");
    }

    // Reactor Core animation & pulsing
    s->core_spin_angle += (float)dt * (s->core_stabilized ? 2.5f : 0.6f);
    s->core_pulse_phase += (float)dt * 3.0f;
    if (s->core_id) {
        const af_scene_object *co = h->get_object(h->scene, s->core_id);
        if (co) {
            af_transform t = co->transform;
            t.rotation.y = s->core_spin_angle;
            h->set_transform(h->scene, s->core_id, t);
        }
    }

    // Update Core Plasma Light (Local Light 1)
    if (1 < h->local_light_count(h->scene)) {
        const af_local_light *cur = h->local_light_at(h->scene, 1);
        if (cur) {
            af_local_light l = *cur;
            float pulse = sinf(s->core_pulse_phase) * 0.5f + 0.5f;
            l.intensity = s->core_stabilized ? (7.0f + pulse * 3.0f) : (4.0f + pulse * 1.5f);
            if (s->core_stabilized) {
                // Radiant golden-cyan stabilized emission
                l.color = (af_vec3){0.2f + pulse * 0.3f, 0.9f, 1.0f};
            }
            h->set_local_light(h->scene, 1, l);
        }
    }

    // Barrier Gate Opening animation when core is stabilized
    if (s->core_stabilized && s->gate_id) {
        s->gate_open_progress = fminf(1.0f, s->gate_open_progress + (float)dt * 0.35f);
        const af_scene_object *go = h->get_object(h->scene, s->gate_id);
        if (go) {
            af_transform t = go->transform;
            t.position.y = s->gate_base_y + s->gate_open_progress * 5.2f;
            h->set_transform(h->scene, s->gate_id, t);
        }
    }

    // Win Condition: Reach extraction elevator past the open gate
    if (s->core_stabilized && !s->game_won) {
        float lift_dist = vec3_dist(s->player.body.position, (af_vec3){0, 0, 24.0f});
        if (lift_dist < 3.5f) {
            s->game_won = true;
            s->status.finished = true;
            s->status.finish_time = s->run_time;
            h->log("MISSION ACCOMPLISHED! Surface extraction underway. Facility saved!");
        }
    }

    // Construct HUD Status
    int active_count = (int)s->relay_active[0] + (int)s->relay_active[1] + (int)s->relay_active[2];
    if (s->game_won) {
        snprintf(s->status.hint, sizeof(s->status.hint),
                 "MISSION WON! Sub-Core Stabilized in %.1fs | All 3 Relays Synchronized | Press R to restart",
                 s->run_time);
    } else if (s->core_stabilized) {
        snprintf(s->status.hint, sizeof(s->status.hint),
                 "CORE STABILIZED! Gate open -> Proceed to Extraction Elevator at South Deck!");
    } else {
        snprintf(s->status.hint, sizeof(s->status.hint), "Relays: %d/3 [%s%s%s] | %s | F:Light V:View", active_count,
                 s->relay_active[0] ? "C" : ".", s->relay_active[1] ? "A" : ".", s->relay_active[2] ? "M" : ".",
                 prompt_action ? prompt_action : "Locate 3 Quantum Prisms");
    }
    s->status.elapsed = s->run_time;
    s->status.checkpoint = (unsigned)active_count;
    s->status.checkpoint_count = 3;
    h->report_gameplay(h->engine, s->status);
}

static void frame_update(void *memory, const af_game_host *h, double dt) {
    lumina_state *s = memory;

    if (h->input_enabled) {
        // Click to capture mouse, Escape to release
        if (h->mouse_pressed(h->engine, AF_MOUSE_LEFT) && h->mouse_in_viewport(h->engine))
            h->capture_mouse(h->engine, true);
        else if (h->key_pressed(h, AF_KEY_ESCAPE))
            h->capture_mouse(h->engine, false);

        // Mouse look when captured
        if (h->mouse_captured(h->engine)) {
            af_vec2 d = h->mouse_delta(h->engine);
            float sens = 0.0022f;
            s->yaw += d.x * sens;
            s->pitch = fmaxf(-1.45f, fminf(1.45f, s->pitch - d.y * sens));
        }
    }

    // Camera setup
    if (h->camera_enabled) {
        s->player.orbit.yaw = s->yaw;
        s->player.orbit.pitch = s->pitch;

        if (s->first_person) {
            s->camera.position =
                (af_vec3){s->player.body.position.x, s->player.body.position.y + 0.65f, s->player.body.position.z};
            s->camera.yaw = s->yaw;
            s->camera.pitch = s->pitch;
            s->camera.vertical_fov = 1.15f;
            s->camera.near_clip = 0.05f;
            s->camera.far_clip = 180.0f;
            h->set_camera(h->engine, &s->camera);
        } else {
            af_query_filter filter = {.ignore = s->player_id, .mask = UINT32_MAX};
            if (h->third_person_camera(h->scene, &s->player, s->third_cfg, (float)fmin(0.1, fmax(0.001, dt)), filter,
                                       &s->camera))
                h->set_camera(h->engine, &s->camera);
        }
    }

    // Update Player Scanner Spotlight (Local Light 0)
    if (0 < h->local_light_count(h->scene)) {
        const af_local_light *cur = h->local_light_at(h->scene, 0);
        if (cur) {
            af_local_light spot = *cur;
            spot.enabled = s->flashlight_on;
            spot.position = s->camera.position;
            spot.direction = get_camera_forward(s->yaw, s->pitch);
            spot.intensity = s->flashlight_on ? 6.5f : 0.0f;
            spot.cast_shadows = true;
            h->set_local_light(h->scene, 0, spot);
        }
    }
}

static void shutdown(void *memory, const af_game_host *h) {
    (void)memory;
    h->capture_mouse(h->engine, false);
}

static size_t save_state(const void *memory, void *bytes, size_t capacity) {
    const lumina_state *s = memory;
    if (!bytes || capacity < sizeof(lumina_save_blob))
        return sizeof(lumina_save_blob);

    lumina_save_blob *blob = bytes;
    memset(blob, 0, sizeof(*blob));
    blob->version = 1;
    blob->run_time = s->run_time;
    blob->held_cell = s->held_cell;
    for (int i = 0; i < CELL_COUNT; ++i)
        blob->relay_active[i] = (uint8_t)s->relay_active[i];
    blob->core_stabilized = (uint8_t)s->core_stabilized;
    blob->game_won = (uint8_t)s->game_won;
    blob->first_person = (uint8_t)s->first_person;
    blob->flashlight_on = (uint8_t)s->flashlight_on;
    blob->player_pos[0] = s->player.body.position.x;
    blob->player_pos[1] = s->player.body.position.y;
    blob->player_pos[2] = s->player.body.position.z;
    blob->yaw = s->yaw;
    blob->pitch = s->pitch;
    return sizeof(lumina_save_blob);
}

static bool load_state(void *memory, const af_game_host *h, uint32_t version, const void *bytes, size_t size) {
    (void)h;
    if (version != 1 || size < sizeof(lumina_save_blob))
        return false;
    lumina_state *s = memory;
    const lumina_save_blob *blob = bytes;
    s->run_time = blob->run_time;
    s->held_cell = blob->held_cell;
    for (int i = 0; i < CELL_COUNT; ++i)
        s->relay_active[i] = blob->relay_active[i] != 0;
    s->core_stabilized = blob->core_stabilized != 0;
    s->game_won = blob->game_won != 0;
    s->first_person = blob->first_person != 0;
    s->flashlight_on = blob->flashlight_on != 0;
    s->player.body.position = (af_vec3){blob->player_pos[0], blob->player_pos[1], blob->player_pos[2]};
    s->yaw = blob->yaw;
    s->pitch = blob->pitch;
    return true;
}

AF_GAME_EXPORT const af_game_module *af_game_get_api(uint32_t abi) {
    if (abi != AF_GAME_ABI_VERSION)
        return NULL;
    static const af_game_module api = {.abi_version = AF_GAME_ABI_VERSION,
                                       .struct_size = sizeof(api),
                                       .host_size = sizeof(af_game_host),
                                       .object_size = sizeof(af_scene_object),
                                       .name = "LUMINA: Deep Core",
                                       .state_size = sizeof(lumina_state),
                                       .init = init,
                                       .update = update,
                                       .frame_update = frame_update,
                                       .shutdown = shutdown,
                                       .state_version = 1,
                                       .save_state = save_state,
                                       .load_state = load_state};
    return &api;
}

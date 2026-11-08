/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_THIRD_PERSON_H
#define AFTERLIGHT_THIRD_PERSON_H
#include <afterlight/character.h>
#include <afterlight/camera.h>

typedef struct af_third_person_config {
    af_character_config controller;
    float walk_speed, run_speed, turn_speed;
    float camera_distance, camera_min_distance, camera_height, camera_radius, camera_recovery;
    float mouse_sensitivity;
} af_third_person_config;
typedef struct af_third_person {
    af_character body;
    af_camera orbit;
    float facing, camera_distance;
} af_third_person;
typedef struct af_third_person_input {
    af_vec2 move;
    bool run, jump;
} af_third_person_input;
af_third_person_config af_third_person_config_default(void);
bool af_third_person_config_valid(af_third_person_config config);
af_third_person af_third_person_default(af_vec3 capsule_center);
/* Mouse look runs per render frame; movement runs on fixed ticks. Move.x is
   camera-right, Move.y camera-forward. Camera collision excludes triggers. */
void af_third_person_look(af_third_person *player, af_third_person_config config, af_vec2 pixels);
bool af_third_person_step(const struct af_scene *scene, af_third_person *player, af_third_person_config config,
                          af_third_person_input input, float delta, af_query_filter filter);
bool af_third_person_step_root_motion(const struct af_scene *, af_third_person *, af_third_person_config,
                                      af_third_person_input, af_vec3 local_delta, float delta, af_query_filter);
bool af_third_person_camera(const struct af_scene *scene, af_third_person *player, af_third_person_config config,
                            float delta, af_query_filter filter, af_camera *camera);
#endif

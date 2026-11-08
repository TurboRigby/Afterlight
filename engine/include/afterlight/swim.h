/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_SWIM_H
#define AFTERLIGHT_SWIM_H
#include <afterlight/character.h>
#include <afterlight/water.h>
typedef struct af_swim_config {
    float speed, vertical_speed, drag, neutral_fraction, entry_fraction, exit_fraction;
} af_swim_config;
typedef struct af_swim_state {
    bool swimming, head_underwater;
    af_object_id water;
    float immersion, exit_time;
} af_swim_state;
typedef struct af_swim_input {
    af_vec3 move;
    float vertical;
    bool jump;
} af_swim_input;
af_swim_config af_swim_config_default(void);
bool af_swim_config_valid(af_swim_config);
/* Optional water-aware controller: normal character movement on land, free
   swept capsule movement while swimming. XZ move, vertical [-1,1]; jump is an
   edge for surface exits. Physics may be NULL for calm authored-water queries.
   Particle fluids are excluded. Mutates state/character only on success. */
bool af_character_swim_step(const af_physics *, const af_scene *, af_character *, af_character_config, af_swim_state *,
                            af_swim_config, af_swim_input, float, af_query_filter);
#endif

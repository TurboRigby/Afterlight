/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AFTERLIGHT_BLUEPRINT_H
#define AFTERLIGHT_BLUEPRINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <afterlight/math.h>
#include <afterlight/story.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AF_BP_MAX_PINS_PER_NODE 16u
#define AF_BP_MAX_NODES 256
#define AF_BP_MAX_LINKS 1024
#define AF_BP_MAX_VARIABLES 128
#define AF_SCENE_MAX_BLUEPRINTS 128
#define AF_BP_NAME_CAPACITY 32u
#define AF_BP_STRING_CAPACITY 64u

typedef enum af_bp_pin_type {
    AF_BP_PIN_EXEC = 0,
    AF_BP_PIN_FLOAT,
    AF_BP_PIN_BOOL,
    AF_BP_PIN_VEC3,
    AF_BP_PIN_OBJECT,
    AF_BP_PIN_STRING
} af_bp_pin_type;

typedef enum af_bp_pin_dir { AF_BP_PIN_INPUT = 0, AF_BP_PIN_OUTPUT } af_bp_pin_dir;

typedef struct af_bp_value {
    af_bp_pin_type type;
    union {
        float f;
        bool b;
        af_vec3 v;
        af_object_id obj;
        char s[AF_BP_STRING_CAPACITY];
    };
} af_bp_value;

typedef struct af_bp_pin {
    uint32_t id;
    char name[AF_BP_NAME_CAPACITY];
    af_bp_pin_type type;
    af_bp_pin_dir dir;
    af_bp_value default_val;
    af_bp_value runtime_val;
} af_bp_pin;

typedef struct af_bp_link {
    uint32_t id;
    uint32_t from_node_id;
    uint32_t from_pin_id;
    uint32_t to_node_id;
    uint32_t to_pin_id;
} af_bp_link;

typedef enum af_bp_node_type {
    /* Events */
    AF_BP_NODE_EVENT_START = 0,
    AF_BP_NODE_EVENT_TICK,
    AF_BP_NODE_EVENT_KEY,
    AF_BP_NODE_EVENT_COLLISION,
    AF_BP_NODE_EVENT_CUSTOM,

    /* Flow Control */
    AF_BP_NODE_BRANCH,
    AF_BP_NODE_SEQUENCE,
    AF_BP_NODE_GATE,
    AF_BP_NODE_DELAY,
    AF_BP_NODE_FOR_LOOP,

    /* Math & Logic */
    AF_BP_NODE_ADD,
    AF_BP_NODE_SUB,
    AF_BP_NODE_MUL,
    AF_BP_NODE_DIV,
    AF_BP_NODE_LERP,
    AF_BP_NODE_SIN,
    AF_BP_NODE_COS,
    AF_BP_NODE_VEC3_MAKE,
    AF_BP_NODE_VEC3_BREAK,
    AF_BP_NODE_VEC3_ADD,
    AF_BP_NODE_VEC3_SCALE,
    AF_BP_NODE_VEC3_DOT,
    AF_BP_NODE_VEC3_LENGTH,
    AF_BP_NODE_VEC3_NORMALIZE,
    AF_BP_NODE_COMPARE_EQUAL,
    AF_BP_NODE_COMPARE_GREATER,
    AF_BP_NODE_COMPARE_LESS,
    AF_BP_NODE_LOGIC_AND,
    AF_BP_NODE_LOGIC_OR,
    AF_BP_NODE_LOGIC_NOT,
    AF_BP_NODE_RANDOM_FLOAT,

    /* Variables */
    AF_BP_NODE_GET_VAR,
    AF_BP_NODE_SET_VAR,

    /* Engine Actions & Properties */
    AF_BP_NODE_GET_POSITION,
    AF_BP_NODE_SET_POSITION,
    AF_BP_NODE_GET_ROTATION,
    AF_BP_NODE_SET_ROTATION,
    AF_BP_NODE_GET_SCALE,
    AF_BP_NODE_SET_SCALE,
    AF_BP_NODE_TRANSLATE,
    AF_BP_NODE_ROTATE,
    AF_BP_NODE_SET_VISIBLE,
    AF_BP_NODE_SET_COLOR,
    AF_BP_NODE_SET_LIGHT,
    AF_BP_NODE_ADD_FORCE,
    AF_BP_NODE_SET_VELOCITY,
    AF_BP_NODE_PLAY_SOUND,
    AF_BP_NODE_PRINT,

    AF_BP_NODE_EVENT_INTERACT,
    AF_BP_NODE_SHOW_DIALOGUE,
    AF_BP_NODE_GET_STORY_FLAG,
    AF_BP_NODE_SET_STORY_FLAG,
    AF_BP_NODE_CHANGE_SCENE,

    AF_BP_NODE_TYPE_COUNT
} af_bp_node_type;

typedef struct af_bp_node {
    uint32_t id;
    af_bp_node_type type;
    char title[48];
    float x, y;
    float width, height;
    af_bp_pin pins[AF_BP_MAX_PINS_PER_NODE];
    uint32_t pin_count;
    char extra_str[AF_BP_STRING_CAPACITY];
    char dialogue_text[AF_STORY_TEXT_CAPACITY];
    float custom_timer;
    bool gate_open;
} af_bp_node;

typedef struct af_bp_variable {
    char name[AF_BP_NAME_CAPACITY];
    af_bp_pin_type type;
    af_bp_value val;
} af_bp_variable;

typedef struct af_blueprint {
    char name[64];
    af_object_id bound_object;
    float interact_range; /* Zero disables the component. */
    char interact_prompt[AF_STORY_LABEL_CAPACITY];
    bool enabled;

    af_bp_node *nodes;
    size_t node_count;
    size_t node_capacity;

    af_bp_link *links;
    size_t link_count;
    size_t link_capacity;

    af_bp_variable *variables;
    size_t var_count;
    size_t var_capacity;

    uint32_t next_node_id;
    uint32_t next_link_id;

    float last_exec_time;
    uint32_t last_executed_node_id;
    bool started;
} af_blueprint;

/* Graph Construction & Manipulation */
af_blueprint *af_blueprint_create(const char *name);
af_blueprint *af_blueprint_clone(const af_blueprint *bp);
void af_blueprint_destroy(af_blueprint *bp);

uint32_t af_blueprint_add_node(af_blueprint *bp, af_bp_node_type type, float x, float y);
bool af_blueprint_remove_node(af_blueprint *bp, uint32_t node_id);
af_bp_node *af_blueprint_get_node(af_blueprint *bp, uint32_t node_id);
const af_bp_node *af_blueprint_get_node_const(const af_blueprint *bp, uint32_t node_id);

uint32_t af_blueprint_add_link(af_blueprint *bp, uint32_t from_node, uint32_t from_pin, uint32_t to_node,
                               uint32_t to_pin);
bool af_blueprint_remove_link(af_blueprint *bp, uint32_t link_id);
const af_bp_link *af_blueprint_find_link_to_pin(const af_blueprint *bp, uint32_t to_node, uint32_t to_pin);
bool af_blueprint_can_link_pins(const af_blueprint *bp, uint32_t from_node, uint32_t from_pin, uint32_t to_node,
                                uint32_t to_pin);

bool af_blueprint_add_variable(af_blueprint *bp, const char *name, af_bp_pin_type type, af_bp_value val);
bool af_blueprint_set_variable(af_blueprint *bp, const char *name, af_bp_value val);
const af_bp_variable *af_blueprint_get_variable(const af_blueprint *bp, const char *name);
bool af_blueprint_remove_variable(af_blueprint *bp, const char *name);

/* Execution VM */
void af_blueprint_reset_runtime(af_blueprint *bp);
void af_blueprint_step(af_blueprint *bp, af_scene *scene, void *engine_handle, float dt);
void af_blueprint_trigger_key(af_blueprint *bp, af_scene *scene, void *engine_handle, const char *key_name,
                              bool pressed);
void af_blueprint_trigger_collision(af_blueprint *bp, af_scene *scene, void *engine_handle, af_object_id obj_a,
                                    af_object_id obj_b);

/* Serialization */
char *af_blueprint_serialize_json(const af_blueprint *bp);
af_blueprint *af_blueprint_deserialize_json(const char *json_text);
bool af_blueprint_save_file(const af_blueprint *bp, const char *path);
af_blueprint *af_blueprint_load_file(const char *path);

/* Scene Integration */
size_t af_scene_blueprint_count(const af_scene *scene);
af_blueprint *af_scene_get_blueprint(af_scene *scene, size_t index);
const af_blueprint *af_scene_get_blueprint_const(const af_scene *scene, size_t index);
af_blueprint *af_scene_get_blueprint_by_name(af_scene *scene, const char *name);
af_blueprint *af_scene_add_blueprint(af_scene *scene, const char *name);
bool af_scene_remove_blueprint(af_scene *scene, size_t index);
void af_scene_step_blueprints(af_scene *scene, void *engine_handle, float dt);
void af_scene_trigger_blueprint_key(af_scene *scene, void *engine_handle, const char *key, bool pressed);
void af_scene_trigger_blueprint_collision(af_scene *scene, void *engine_handle, af_object_id a, af_object_id b);

/* Information / Meta */
const char *af_bp_node_type_name(af_bp_node_type type);
const char *af_bp_node_type_category(af_bp_node_type type);
const char *af_bp_pin_type_name(af_bp_pin_type type);

#ifdef __cplusplus
}
#endif

#endif /* AFTERLIGHT_BLUEPRINT_H */

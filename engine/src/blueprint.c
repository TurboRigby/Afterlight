/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/blueprint.h>
#include "scene_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void init_node_pins(af_bp_node *node) {
    node->pin_count = 0;
    uint32_t pid = 1;

#define ADD_PIN(pname, ptype, pdir, def_field, def_val)                                                                \
    do {                                                                                                               \
        if (node->pin_count < AF_BP_MAX_PINS_PER_NODE) {                                                               \
            af_bp_pin *p = &node->pins[node->pin_count++];                                                             \
            p->id = pid++;                                                                                             \
            snprintf(p->name, sizeof(p->name), "%s", pname);                                                           \
            p->type = (ptype);                                                                                         \
            p->dir = (pdir);                                                                                           \
            p->default_val.type = (ptype);                                                                             \
            p->default_val.def_field = (def_val);                                                                      \
            p->runtime_val = p->default_val;                                                                           \
        }                                                                                                              \
    } while (0)

#define ADD_EXEC_IN(pname) ADD_PIN(pname, AF_BP_PIN_EXEC, AF_BP_PIN_INPUT, b, false)
#define ADD_EXEC_OUT(pname) ADD_PIN(pname, AF_BP_PIN_EXEC, AF_BP_PIN_OUTPUT, b, false)
#define ADD_FLOAT_IN(pname, def) ADD_PIN(pname, AF_BP_PIN_FLOAT, AF_BP_PIN_INPUT, f, def)
#define ADD_FLOAT_OUT(pname) ADD_PIN(pname, AF_BP_PIN_FLOAT, AF_BP_PIN_OUTPUT, f, 0.0f)
#define ADD_BOOL_IN(pname, def) ADD_PIN(pname, AF_BP_PIN_BOOL, AF_BP_PIN_INPUT, b, def)
#define ADD_BOOL_OUT(pname) ADD_PIN(pname, AF_BP_PIN_BOOL, AF_BP_PIN_OUTPUT, b, false)
#define ADD_VEC3_IN(pname, def_x, def_y, def_z)                                                                        \
    ADD_PIN(pname, AF_BP_PIN_VEC3, AF_BP_PIN_INPUT, v, ((af_vec3){def_x, def_y, def_z}))
#define ADD_VEC3_OUT(pname) ADD_PIN(pname, AF_BP_PIN_VEC3, AF_BP_PIN_OUTPUT, v, ((af_vec3){0, 0, 0}))
#define ADD_OBJ_IN(pname) ADD_PIN(pname, AF_BP_PIN_OBJECT, AF_BP_PIN_INPUT, obj, 0)
#define ADD_OBJ_OUT(pname) ADD_PIN(pname, AF_BP_PIN_OBJECT, AF_BP_PIN_OUTPUT, obj, 0)
#define ADD_STR_IN(pname, def) ADD_PIN(pname, AF_BP_PIN_STRING, AF_BP_PIN_INPUT, s[0], 0)

    switch (node->type) {
    case AF_BP_NODE_EVENT_START:
        snprintf(node->title, sizeof(node->title), "Event BeginPlay");
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_EVENT_TICK:
        snprintf(node->title, sizeof(node->title), "Event Tick");
        ADD_EXEC_OUT("Exec");
        ADD_FLOAT_OUT("Delta Seconds");
        break;
    case AF_BP_NODE_EVENT_KEY:
        snprintf(node->title, sizeof(node->title), "Input Key");
        ADD_EXEC_OUT("Pressed");
        ADD_EXEC_OUT("Released");
        break;
    case AF_BP_NODE_EVENT_COLLISION:
        snprintf(node->title, sizeof(node->title), "On Hit");
        ADD_EXEC_OUT("Exec");
        ADD_OBJ_OUT("Other Object");
        break;
    case AF_BP_NODE_EVENT_CUSTOM:
        snprintf(node->title, sizeof(node->title), "Custom Event");
        ADD_EXEC_OUT("Exec");
        break;

    /* Flow Control */
    case AF_BP_NODE_BRANCH:
        snprintf(node->title, sizeof(node->title), "Branch");
        ADD_EXEC_IN("Exec");
        ADD_BOOL_IN("Condition", true);
        ADD_EXEC_OUT("True");
        ADD_EXEC_OUT("False");
        break;
    case AF_BP_NODE_SEQUENCE:
        snprintf(node->title, sizeof(node->title), "Sequence");
        ADD_EXEC_IN("Exec");
        ADD_EXEC_OUT("Then 0");
        ADD_EXEC_OUT("Then 1");
        ADD_EXEC_OUT("Then 2");
        break;
    case AF_BP_NODE_GATE:
        snprintf(node->title, sizeof(node->title), "Gate");
        ADD_EXEC_IN("Enter");
        ADD_EXEC_IN("Open");
        ADD_EXEC_IN("Close");
        ADD_EXEC_IN("Toggle");
        ADD_BOOL_IN("Start Closed", false);
        ADD_EXEC_OUT("Exit");
        node->gate_open = true;
        break;
    case AF_BP_NODE_DELAY:
        snprintf(node->title, sizeof(node->title), "Delay");
        ADD_EXEC_IN("Exec");
        ADD_FLOAT_IN("Duration", 1.0f);
        ADD_EXEC_OUT("Completed");
        break;
    case AF_BP_NODE_FOR_LOOP:
        snprintf(node->title, sizeof(node->title), "For Loop");
        ADD_EXEC_IN("Exec");
        ADD_FLOAT_IN("First Index", 0.0f);
        ADD_FLOAT_IN("Last Index", 10.0f);
        ADD_EXEC_OUT("Loop Body");
        ADD_FLOAT_OUT("Index");
        ADD_EXEC_OUT("Completed");
        break;

    /* Math */
    case AF_BP_NODE_ADD:
        snprintf(node->title, sizeof(node->title), "Add (+)");
        ADD_FLOAT_IN("A", 0.0f);
        ADD_FLOAT_IN("B", 0.0f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_SUB:
        snprintf(node->title, sizeof(node->title), "Subtract (-)");
        ADD_FLOAT_IN("A", 0.0f);
        ADD_FLOAT_IN("B", 0.0f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_MUL:
        snprintf(node->title, sizeof(node->title), "Multiply (*)");
        ADD_FLOAT_IN("A", 1.0f);
        ADD_FLOAT_IN("B", 1.0f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_DIV:
        snprintf(node->title, sizeof(node->title), "Divide (/)");
        ADD_FLOAT_IN("A", 1.0f);
        ADD_FLOAT_IN("B", 1.0f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_LERP:
        snprintf(node->title, sizeof(node->title), "Lerp");
        ADD_FLOAT_IN("A", 0.0f);
        ADD_FLOAT_IN("B", 1.0f);
        ADD_FLOAT_IN("Alpha", 0.5f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_SIN:
        snprintf(node->title, sizeof(node->title), "Sin");
        ADD_FLOAT_IN("Angle", 0.0f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_COS:
        snprintf(node->title, sizeof(node->title), "Cos");
        ADD_FLOAT_IN("Angle", 0.0f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_VEC3_MAKE:
        snprintf(node->title, sizeof(node->title), "Make Vector");
        ADD_FLOAT_IN("X", 0.0f);
        ADD_FLOAT_IN("Y", 0.0f);
        ADD_FLOAT_IN("Z", 0.0f);
        ADD_VEC3_OUT("Vector");
        break;
    case AF_BP_NODE_VEC3_BREAK:
        snprintf(node->title, sizeof(node->title), "Break Vector");
        ADD_VEC3_IN("Vector", 0.0f, 0.0f, 0.0f);
        ADD_FLOAT_OUT("X");
        ADD_FLOAT_OUT("Y");
        ADD_FLOAT_OUT("Z");
        break;
    case AF_BP_NODE_VEC3_ADD:
        snprintf(node->title, sizeof(node->title), "Vector + Vector");
        ADD_VEC3_IN("A", 0.0f, 0.0f, 0.0f);
        ADD_VEC3_IN("B", 0.0f, 0.0f, 0.0f);
        ADD_VEC3_OUT("Result");
        break;
    case AF_BP_NODE_VEC3_SCALE:
        snprintf(node->title, sizeof(node->title), "Vector * Float");
        ADD_VEC3_IN("Vector", 0.0f, 0.0f, 0.0f);
        ADD_FLOAT_IN("Scale", 1.0f);
        ADD_VEC3_OUT("Result");
        break;
    case AF_BP_NODE_VEC3_DOT:
        snprintf(node->title, sizeof(node->title), "Vector Dot Product");
        ADD_VEC3_IN("A", 0.0f, 0.0f, 0.0f);
        ADD_VEC3_IN("B", 0.0f, 0.0f, 0.0f);
        ADD_FLOAT_OUT("Result");
        break;
    case AF_BP_NODE_VEC3_LENGTH:
        snprintf(node->title, sizeof(node->title), "Vector Length");
        ADD_VEC3_IN("Vector", 0.0f, 0.0f, 0.0f);
        ADD_FLOAT_OUT("Length");
        break;
    case AF_BP_NODE_VEC3_NORMALIZE:
        snprintf(node->title, sizeof(node->title), "Normalize");
        ADD_VEC3_IN("Vector", 0.0f, 0.0f, 0.0f);
        ADD_VEC3_OUT("Result");
        break;
    case AF_BP_NODE_COMPARE_EQUAL:
        snprintf(node->title, sizeof(node->title), "Equal (==)");
        ADD_FLOAT_IN("A", 0.0f);
        ADD_FLOAT_IN("B", 0.0f);
        ADD_BOOL_OUT("Result");
        break;
    case AF_BP_NODE_COMPARE_GREATER:
        snprintf(node->title, sizeof(node->title), "Greater (>)");
        ADD_FLOAT_IN("A", 0.0f);
        ADD_FLOAT_IN("B", 0.0f);
        ADD_BOOL_OUT("Result");
        break;
    case AF_BP_NODE_COMPARE_LESS:
        snprintf(node->title, sizeof(node->title), "Less (<)");
        ADD_FLOAT_IN("A", 0.0f);
        ADD_FLOAT_IN("B", 0.0f);
        ADD_BOOL_OUT("Result");
        break;
    case AF_BP_NODE_LOGIC_AND:
        snprintf(node->title, sizeof(node->title), "AND");
        ADD_BOOL_IN("A", true);
        ADD_BOOL_IN("B", true);
        ADD_BOOL_OUT("Result");
        break;
    case AF_BP_NODE_LOGIC_OR:
        snprintf(node->title, sizeof(node->title), "OR");
        ADD_BOOL_IN("A", false);
        ADD_BOOL_IN("B", false);
        ADD_BOOL_OUT("Result");
        break;
    case AF_BP_NODE_LOGIC_NOT:
        snprintf(node->title, sizeof(node->title), "NOT");
        ADD_BOOL_IN("In", false);
        ADD_BOOL_OUT("Out");
        break;
    case AF_BP_NODE_RANDOM_FLOAT:
        snprintf(node->title, sizeof(node->title), "Random Float in Range");
        ADD_FLOAT_IN("Min", 0.0f);
        ADD_FLOAT_IN("Max", 1.0f);
        ADD_FLOAT_OUT("Result");
        break;

    /* Variables */
    case AF_BP_NODE_GET_VAR:
        snprintf(node->title, sizeof(node->title), "Get Variable");
        ADD_FLOAT_OUT("Value");
        break;
    case AF_BP_NODE_SET_VAR:
        snprintf(node->title, sizeof(node->title), "Set Variable");
        ADD_EXEC_IN("Exec");
        ADD_FLOAT_IN("Value", 0.0f);
        ADD_EXEC_OUT("Exec");
        break;

    /* Actions */
    case AF_BP_NODE_GET_POSITION:
        snprintf(node->title, sizeof(node->title), "Get Actor Location");
        ADD_OBJ_IN("Target");
        ADD_VEC3_OUT("Location");
        break;
    case AF_BP_NODE_SET_POSITION:
        snprintf(node->title, sizeof(node->title), "Set Actor Location");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("New Location", 0.0f, 0.0f, 0.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_GET_ROTATION:
        snprintf(node->title, sizeof(node->title), "Get Actor Rotation");
        ADD_OBJ_IN("Target");
        ADD_VEC3_OUT("Rotation");
        break;
    case AF_BP_NODE_SET_ROTATION:
        snprintf(node->title, sizeof(node->title), "Set Actor Rotation");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("New Rotation", 0.0f, 0.0f, 0.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_GET_SCALE:
        snprintf(node->title, sizeof(node->title), "Get Actor Scale");
        ADD_OBJ_IN("Target");
        ADD_VEC3_OUT("Scale");
        break;
    case AF_BP_NODE_SET_SCALE:
        snprintf(node->title, sizeof(node->title), "Set Actor Scale");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("New Scale", 1.0f, 1.0f, 1.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_TRANSLATE:
        snprintf(node->title, sizeof(node->title), "Add Actor Offset");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("Delta Location", 0.0f, 0.0f, 0.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_ROTATE:
        snprintf(node->title, sizeof(node->title), "Add Actor Rotation");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("Delta Rotation", 0.0f, 0.0f, 0.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_SET_VISIBLE:
        snprintf(node->title, sizeof(node->title), "Set Actor Hidden In Game");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_BOOL_IN("Hidden", false);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_SET_COLOR:
        snprintf(node->title, sizeof(node->title), "Set Base Color");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("Color RGB", 1.0f, 1.0f, 1.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_SET_LIGHT:
        snprintf(node->title, sizeof(node->title), "Set Directional Light");
        ADD_EXEC_IN("Exec");
        ADD_FLOAT_IN("Intensity", 1.0f);
        ADD_VEC3_IN("Color RGB", 1.0f, 1.0f, 1.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_ADD_FORCE:
        snprintf(node->title, sizeof(node->title), "Add Force");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("Force", 0.0f, 10.0f, 0.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_SET_VELOCITY:
        snprintf(node->title, sizeof(node->title), "Set Velocity");
        ADD_EXEC_IN("Exec");
        ADD_OBJ_IN("Target");
        ADD_VEC3_IN("Velocity", 0.0f, 0.0f, 0.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_PLAY_SOUND:
        snprintf(node->title, sizeof(node->title), "Play Sound 2D");
        ADD_EXEC_IN("Exec");
        ADD_STR_IN("Sound Name", "");
        ADD_FLOAT_IN("Volume", 1.0f);
        ADD_EXEC_OUT("Exec");
        break;
    case AF_BP_NODE_PRINT:
        snprintf(node->title, sizeof(node->title), "Print String");
        ADD_EXEC_IN("Exec");
        ADD_STR_IN("Message", "Hello Blueprint");
        ADD_FLOAT_IN("Float Value", 0.0f);
        ADD_EXEC_OUT("Exec");
        break;

    default:
        snprintf(node->title, sizeof(node->title), "Node");
        break;
    }

#undef ADD_PIN
#undef ADD_EXEC_IN
#undef ADD_EXEC_OUT
#undef ADD_FLOAT_IN
#undef ADD_FLOAT_OUT
#undef ADD_BOOL_IN
#undef ADD_BOOL_OUT
#undef ADD_VEC3_IN
#undef ADD_VEC3_OUT
#undef ADD_OBJ_IN
#undef ADD_OBJ_OUT
#undef ADD_STR_IN
}

af_blueprint *af_blueprint_create(const char *name) {
    af_blueprint *bp = (af_blueprint *)calloc(1, sizeof(af_blueprint));
    if (!bp)
        return NULL;
    snprintf(bp->name, sizeof(bp->name), "%s", (name && name[0]) ? name : "NewBlueprint");
    bp->enabled = true;
    bp->next_node_id = 1;
    bp->next_link_id = 1;
    return bp;
}

void af_blueprint_destroy(af_blueprint *bp) {
    if (!bp)
        return;
    free(bp->nodes);
    free(bp->links);
    free(bp->variables);
    free(bp);
}

af_blueprint *af_blueprint_clone(const af_blueprint *bp) {
    if (!bp)
        return NULL;
    af_blueprint *copy = af_blueprint_create(bp->name);
    if (!copy)
        return NULL;
    copy->bound_object = bp->bound_object;
    copy->enabled = bp->enabled;
    copy->next_node_id = bp->next_node_id;
    copy->next_link_id = bp->next_link_id;

    if (bp->node_count > 0) {
        copy->nodes = (af_bp_node *)malloc(bp->node_count * sizeof(af_bp_node));
        if (copy->nodes) {
            memcpy(copy->nodes, bp->nodes, bp->node_count * sizeof(af_bp_node));
            copy->node_count = bp->node_count;
            copy->node_capacity = bp->node_count;
        }
    }

    if (bp->link_count > 0) {
        copy->links = (af_bp_link *)malloc(bp->link_count * sizeof(af_bp_link));
        if (copy->links) {
            memcpy(copy->links, bp->links, bp->link_count * sizeof(af_bp_link));
            copy->link_count = bp->link_count;
            copy->link_capacity = bp->link_count;
        }
    }

    if (bp->var_count > 0) {
        copy->variables = (af_bp_variable *)malloc(bp->var_count * sizeof(af_bp_variable));
        if (copy->variables) {
            memcpy(copy->variables, bp->variables, bp->var_count * sizeof(af_bp_variable));
            copy->var_count = bp->var_count;
            copy->var_capacity = bp->var_count;
        }
    }

    return copy;
}

uint32_t af_blueprint_add_node(af_blueprint *bp, af_bp_node_type type, float x, float y) {
    if (!bp || type >= AF_BP_NODE_TYPE_COUNT)
        return 0;

    if (bp->node_count >= bp->node_capacity) {
        size_t new_cap = bp->node_capacity == 0 ? 16 : bp->node_capacity * 2;
        af_bp_node *new_nodes = (af_bp_node *)realloc(bp->nodes, new_cap * sizeof(af_bp_node));
        if (!new_nodes)
            return 0;
        bp->nodes = new_nodes;
        bp->node_capacity = new_cap;
    }

    af_bp_node *node = &bp->nodes[bp->node_count++];
    memset(node, 0, sizeof(*node));
    node->id = bp->next_node_id++;
    node->type = type;
    node->x = x;
    node->y = y;
    node->width = 180.0f;
    node->height = 120.0f;

    init_node_pins(node);
    return node->id;
}

bool af_blueprint_remove_node(af_blueprint *bp, uint32_t node_id) {
    if (!bp || !node_id)
        return false;

    /* Remove all connected links */
    for (size_t i = 0; i < bp->link_count;) {
        if (bp->links[i].from_node_id == node_id || bp->links[i].to_node_id == node_id) {
            af_blueprint_remove_link(bp, bp->links[i].id);
        } else {
            i++;
        }
    }

    for (size_t i = 0; i < bp->node_count; ++i) {
        if (bp->nodes[i].id == node_id) {
            if (i + 1 < bp->node_count) {
                memmove(&bp->nodes[i], &bp->nodes[i + 1], (bp->node_count - i - 1) * sizeof(af_bp_node));
            }
            bp->node_count--;
            return true;
        }
    }
    return false;
}

af_bp_node *af_blueprint_get_node(af_blueprint *bp, uint32_t node_id) {
    if (!bp || !node_id)
        return NULL;
    for (size_t i = 0; i < bp->node_count; ++i) {
        if (bp->nodes[i].id == node_id)
            return &bp->nodes[i];
    }
    return NULL;
}

const af_bp_node *af_blueprint_get_node_const(const af_blueprint *bp, uint32_t node_id) {
    if (!bp || !node_id)
        return NULL;
    for (size_t i = 0; i < bp->node_count; ++i) {
        if (bp->nodes[i].id == node_id)
            return &bp->nodes[i];
    }
    return NULL;
}

static const af_bp_pin *find_pin(const af_bp_node *node, uint32_t pin_id) {
    if (!node || !pin_id)
        return NULL;
    for (uint32_t i = 0; i < node->pin_count; ++i) {
        if (node->pins[i].id == pin_id)
            return &node->pins[i];
    }
    return NULL;
}

bool af_blueprint_can_link_pins(const af_blueprint *bp, uint32_t from_node, uint32_t from_pin, uint32_t to_node,
                                uint32_t to_pin) {
    if (!bp || from_node == to_node)
        return false;

    const af_bp_node *n_from = af_blueprint_get_node_const(bp, from_node);
    const af_bp_node *n_to = af_blueprint_get_node_const(bp, to_node);
    if (!n_from || !n_to)
        return false;

    const af_bp_pin *p_from = find_pin(n_from, from_pin);
    const af_bp_pin *p_to = find_pin(n_to, to_pin);
    if (!p_from || !p_to)
        return false;

    /* Must be Output -> Input */
    if (p_from->dir != AF_BP_PIN_OUTPUT || p_to->dir != AF_BP_PIN_INPUT)
        return false;

    /* Exec can only connect to Exec */
    if (p_from->type == AF_BP_PIN_EXEC)
        return p_to->type == AF_BP_PIN_EXEC;
    if (p_to->type == AF_BP_PIN_EXEC)
        return false;

    /* Data pin type compatibility */
    if (p_from->type == p_to->type)
        return true;

    /* Allow float -> bool / string conversion */
    if (p_from->type == AF_BP_PIN_FLOAT && (p_to->type == AF_BP_PIN_BOOL || p_to->type == AF_BP_PIN_STRING))
        return true;

    return false;
}

uint32_t af_blueprint_add_link(af_blueprint *bp, uint32_t from_node, uint32_t from_pin, uint32_t to_node,
                               uint32_t to_pin) {
    if (!af_blueprint_can_link_pins(bp, from_node, from_pin, to_node, to_pin))
        return 0;

    /* Remove any existing link into the input pin */
    for (size_t i = 0; i < bp->link_count; ++i) {
        if (bp->links[i].to_node_id == to_node && bp->links[i].to_pin_id == to_pin) {
            af_blueprint_remove_link(bp, bp->links[i].id);
            break;
        }
    }

    /* If output is Exec, clear any existing outgoing link from it */
    const af_bp_node *n_from = af_blueprint_get_node_const(bp, from_node);
    const af_bp_pin *p_from = n_from ? find_pin(n_from, from_pin) : NULL;
    if (p_from && p_from->type == AF_BP_PIN_EXEC) {
        for (size_t i = 0; i < bp->link_count; ++i) {
            if (bp->links[i].from_node_id == from_node && bp->links[i].from_pin_id == from_pin) {
                af_blueprint_remove_link(bp, bp->links[i].id);
                break;
            }
        }
    }

    if (bp->link_count >= bp->link_capacity) {
        size_t new_cap = bp->link_capacity == 0 ? 16 : bp->link_capacity * 2;
        af_bp_link *new_links = (af_bp_link *)realloc(bp->links, new_cap * sizeof(af_bp_link));
        if (!new_links)
            return 0;
        bp->links = new_links;
        bp->link_capacity = new_cap;
    }

    af_bp_link *link = &bp->links[bp->link_count++];
    link->id = bp->next_link_id++;
    link->from_node_id = from_node;
    link->from_pin_id = from_pin;
    link->to_node_id = to_node;
    link->to_pin_id = to_pin;

    return link->id;
}

bool af_blueprint_remove_link(af_blueprint *bp, uint32_t link_id) {
    if (!bp || !link_id)
        return false;
    for (size_t i = 0; i < bp->link_count; ++i) {
        if (bp->links[i].id == link_id) {
            if (i + 1 < bp->link_count) {
                memmove(&bp->links[i], &bp->links[i + 1], (bp->link_count - i - 1) * sizeof(af_bp_link));
            }
            bp->link_count--;
            return true;
        }
    }
    return false;
}

const af_bp_link *af_blueprint_find_link_to_pin(const af_blueprint *bp, uint32_t to_node, uint32_t to_pin) {
    if (!bp || !to_node || !to_pin)
        return NULL;
    for (size_t i = 0; i < bp->link_count; ++i) {
        if (bp->links[i].to_node_id == to_node && bp->links[i].to_pin_id == to_pin)
            return &bp->links[i];
    }
    return NULL;
}

bool af_blueprint_add_variable(af_blueprint *bp, const char *name, af_bp_pin_type type, af_bp_value val) {
    if (!bp || !name || !name[0])
        return false;
    if (af_blueprint_get_variable(bp, name))
        return false;

    if (bp->var_count >= bp->var_capacity) {
        size_t new_cap = bp->var_capacity == 0 ? 8 : bp->var_capacity * 2;
        af_bp_variable *new_vars = (af_bp_variable *)realloc(bp->variables, new_cap * sizeof(af_bp_variable));
        if (!new_vars)
            return false;
        bp->variables = new_vars;
        bp->var_capacity = new_cap;
    }

    af_bp_variable *v = &bp->variables[bp->var_count++];
    snprintf(v->name, sizeof(v->name), "%s", name);
    v->type = type;
    v->val = val;
    v->val.type = type;
    return true;
}

bool af_blueprint_set_variable(af_blueprint *bp, const char *name, af_bp_value val) {
    if (!bp || !name)
        return false;
    for (size_t i = 0; i < bp->var_count; ++i) {
        if (strcmp(bp->variables[i].name, name) == 0) {
            bp->variables[i].val = val;
            return true;
        }
    }
    return false;
}

const af_bp_variable *af_blueprint_get_variable(const af_blueprint *bp, const char *name) {
    if (!bp || !name)
        return NULL;
    for (size_t i = 0; i < bp->var_count; ++i) {
        if (strcmp(bp->variables[i].name, name) == 0)
            return &bp->variables[i];
    }
    return NULL;
}

bool af_blueprint_remove_variable(af_blueprint *bp, const char *name) {
    if (!bp || !name)
        return false;
    for (size_t i = 0; i < bp->var_count; ++i) {
        if (strcmp(bp->variables[i].name, name) == 0) {
            if (i + 1 < bp->var_count) {
                memmove(&bp->variables[i], &bp->variables[i + 1], (bp->var_count - i - 1) * sizeof(af_bp_variable));
            }
            bp->var_count--;
            return true;
        }
    }
    return false;
}

const char *af_bp_node_type_name(af_bp_node_type type) {
    switch (type) {
    case AF_BP_NODE_EVENT_START:
        return "BeginPlay";
    case AF_BP_NODE_EVENT_TICK:
        return "Tick";
    case AF_BP_NODE_EVENT_KEY:
        return "Input Key";
    case AF_BP_NODE_EVENT_COLLISION:
        return "On Hit";
    case AF_BP_NODE_EVENT_CUSTOM:
        return "Custom Event";

    case AF_BP_NODE_BRANCH:
        return "Branch";
    case AF_BP_NODE_SEQUENCE:
        return "Sequence";
    case AF_BP_NODE_GATE:
        return "Gate";
    case AF_BP_NODE_DELAY:
        return "Delay";
    case AF_BP_NODE_FOR_LOOP:
        return "For Loop";

    case AF_BP_NODE_ADD:
        return "Add";
    case AF_BP_NODE_SUB:
        return "Subtract";
    case AF_BP_NODE_MUL:
        return "Multiply";
    case AF_BP_NODE_DIV:
        return "Divide";
    case AF_BP_NODE_LERP:
        return "Lerp";
    case AF_BP_NODE_SIN:
        return "Sin";
    case AF_BP_NODE_COS:
        return "Cos";
    case AF_BP_NODE_VEC3_MAKE:
        return "Make Vector";
    case AF_BP_NODE_VEC3_BREAK:
        return "Break Vector";
    case AF_BP_NODE_VEC3_ADD:
        return "Vector + Vector";
    case AF_BP_NODE_VEC3_SCALE:
        return "Vector * Float";
    case AF_BP_NODE_VEC3_DOT:
        return "Vector Dot";
    case AF_BP_NODE_VEC3_LENGTH:
        return "Vector Length";
    case AF_BP_NODE_VEC3_NORMALIZE:
        return "Vector Normalize";
    case AF_BP_NODE_COMPARE_EQUAL:
        return "Equal (==)";
    case AF_BP_NODE_COMPARE_GREATER:
        return "Greater (>)";
    case AF_BP_NODE_COMPARE_LESS:
        return "Less (<)";
    case AF_BP_NODE_LOGIC_AND:
        return "AND";
    case AF_BP_NODE_LOGIC_OR:
        return "OR";
    case AF_BP_NODE_LOGIC_NOT:
        return "NOT";
    case AF_BP_NODE_RANDOM_FLOAT:
        return "Random Float";

    case AF_BP_NODE_GET_VAR:
        return "Get Variable";
    case AF_BP_NODE_SET_VAR:
        return "Set Variable";

    case AF_BP_NODE_GET_POSITION:
        return "Get Actor Location";
    case AF_BP_NODE_SET_POSITION:
        return "Set Actor Location";
    case AF_BP_NODE_GET_ROTATION:
        return "Get Actor Rotation";
    case AF_BP_NODE_SET_ROTATION:
        return "Set Actor Rotation";
    case AF_BP_NODE_GET_SCALE:
        return "Get Actor Scale";
    case AF_BP_NODE_SET_SCALE:
        return "Set Actor Scale";
    case AF_BP_NODE_TRANSLATE:
        return "Add Actor Offset";
    case AF_BP_NODE_ROTATE:
        return "Add Actor Rotation";
    case AF_BP_NODE_SET_VISIBLE:
        return "Set Actor Hidden";
    case AF_BP_NODE_SET_COLOR:
        return "Set Base Color";
    case AF_BP_NODE_SET_LIGHT:
        return "Set Directional Light";
    case AF_BP_NODE_ADD_FORCE:
        return "Add Force";
    case AF_BP_NODE_SET_VELOCITY:
        return "Set Velocity";
    case AF_BP_NODE_PLAY_SOUND:
        return "Play Sound";
    case AF_BP_NODE_PRINT:
        return "Print String";

    default:
        return "Unknown";
    }
}

const char *af_bp_node_type_category(af_bp_node_type type) {
    if (type <= AF_BP_NODE_EVENT_CUSTOM)
        return "Events";
    if (type <= AF_BP_NODE_FOR_LOOP)
        return "Flow Control";
    if (type <= AF_BP_NODE_RANDOM_FLOAT)
        return "Math & Logic";
    if (type <= AF_BP_NODE_SET_VAR)
        return "Variables";
    return "Actions";
}

const char *af_bp_pin_type_name(af_bp_pin_type type) {
    switch (type) {
    case AF_BP_PIN_EXEC:
        return "Exec";
    case AF_BP_PIN_FLOAT:
        return "Float";
    case AF_BP_PIN_BOOL:
        return "Boolean";
    case AF_BP_PIN_VEC3:
        return "Vector";
    case AF_BP_PIN_OBJECT:
        return "Actor";
    case AF_BP_PIN_STRING:
        return "String";
    default:
        return "Unknown";
    }
}

size_t af_scene_blueprint_count(const af_scene *scene) {
    return scene ? scene->blueprint_count : 0;
}

af_blueprint *af_scene_get_blueprint(af_scene *scene, size_t index) {
    if (!scene || index >= scene->blueprint_count)
        return NULL;
    return scene->blueprints[index];
}

const af_blueprint *af_scene_get_blueprint_const(const af_scene *scene, size_t index) {
    if (!scene || index >= scene->blueprint_count)
        return NULL;
    return scene->blueprints[index];
}

af_blueprint *af_scene_get_blueprint_by_name(af_scene *scene, const char *name) {
    if (!scene || !name)
        return NULL;
    for (size_t i = 0; i < scene->blueprint_count; ++i) {
        if (strcmp(scene->blueprints[i]->name, name) == 0)
            return scene->blueprints[i];
    }
    return NULL;
}

af_blueprint *af_scene_add_blueprint(af_scene *scene, const char *name) {
    if (!scene)
        return NULL;
    if (scene->blueprint_count >= scene->blueprint_capacity) {
        size_t new_cap = scene->blueprint_capacity == 0 ? 4 : scene->blueprint_capacity * 2;
        af_blueprint **new_arr = (af_blueprint **)realloc(scene->blueprints, new_cap * sizeof(af_blueprint *));
        if (!new_arr)
            return NULL;
        scene->blueprints = new_arr;
        scene->blueprint_capacity = new_cap;
    }
    af_blueprint *bp = af_blueprint_create(name);
    if (!bp)
        return NULL;
    scene->blueprints[scene->blueprint_count++] = bp;
    return bp;
}

bool af_scene_remove_blueprint(af_scene *scene, size_t index) {
    if (!scene || index >= scene->blueprint_count)
        return false;
    af_blueprint_destroy(scene->blueprints[index]);
    if (index + 1 < scene->blueprint_count) {
        memmove(&scene->blueprints[index], &scene->blueprints[index + 1],
                (scene->blueprint_count - index - 1) * sizeof(af_blueprint *));
    }
    scene->blueprint_count--;
    return true;
}

void af_scene_step_blueprints(af_scene *scene, void *engine_handle, float dt) {
    if (!scene)
        return;
    for (size_t i = 0; i < scene->blueprint_count; ++i) {
        if (scene->blueprints[i]->enabled) {
            af_blueprint_step(scene->blueprints[i], scene, engine_handle, dt);
        }
    }
}

void af_scene_trigger_blueprint_key(af_scene *scene, void *engine_handle, const char *key, bool pressed) {
    if (!scene || !key)
        return;
    for (size_t i = 0; i < scene->blueprint_count; ++i) {
        if (scene->blueprints[i]->enabled) {
            af_blueprint_trigger_key(scene->blueprints[i], scene, engine_handle, key, pressed);
        }
    }
}

void af_scene_trigger_blueprint_collision(af_scene *scene, void *engine_handle, af_object_id a, af_object_id b) {
    if (!scene)
        return;
    for (size_t i = 0; i < scene->blueprint_count; ++i) {
        if (scene->blueprints[i]->enabled) {
            af_blueprint_trigger_collision(scene->blueprints[i], scene, engine_handle, a, b);
        }
    }
}

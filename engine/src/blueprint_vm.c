/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/blueprint.h>
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AF_BP_VM_MAX_DEPTH 32
#define AF_BP_VM_MAX_STEPS 512

typedef struct af_bp_vm_ctx {
    af_blueprint *bp;
    af_scene *scene;
    void *engine_handle;
    float dt;
    int depth;
    int steps;
} af_bp_vm_ctx;

static af_bp_value eval_data_pin(af_bp_vm_ctx *ctx, af_bp_node *node, uint32_t pin_id);
static void execute_node(af_bp_vm_ctx *ctx, af_bp_node *node, uint32_t entry_pin_id);

static af_bp_pin *find_pin_mut(af_bp_node *node, uint32_t pin_id) {
    if (!node)
        return NULL;
    for (uint32_t i = 0; i < node->pin_count; ++i) {
        if (node->pins[i].id == pin_id)
            return &node->pins[i];
    }
    return NULL;
}

static af_bp_pin *find_pin_by_name(af_bp_node *node, const char *name, af_bp_pin_dir dir) {
    if (!node || !name)
        return NULL;
    for (uint32_t i = 0; i < node->pin_count; ++i) {
        if (node->pins[i].dir == dir && strcmp(node->pins[i].name, name) == 0)
            return &node->pins[i];
    }
    return NULL;
}

static af_bp_value get_input_val(af_bp_vm_ctx *ctx, af_bp_node *node, const char *pin_name) {
    af_bp_pin *pin = find_pin_by_name(node, pin_name, AF_BP_PIN_INPUT);
    if (!pin) {
        af_bp_value empty = {0};
        return empty;
    }
    return eval_data_pin(ctx, node, pin->id);
}

static void follow_exec_pin(af_bp_vm_ctx *ctx, af_bp_node *node, const char *pin_name) {
    af_bp_pin *pin = find_pin_by_name(node, pin_name, AF_BP_PIN_OUTPUT);
    if (!pin || pin->type != AF_BP_PIN_EXEC)
        return;

    for (size_t i = 0; i < ctx->bp->link_count; ++i) {
        const af_bp_link *link = &ctx->bp->links[i];
        if (link->from_node_id == node->id && link->from_pin_id == pin->id) {
            af_bp_node *next_node = af_blueprint_get_node(ctx->bp, link->to_node_id);
            if (next_node) {
                execute_node(ctx, next_node, link->to_pin_id);
            }
            break;
        }
    }
}

static af_bp_value eval_data_pin(af_bp_vm_ctx *ctx, af_bp_node *node, uint32_t pin_id) {
    af_bp_pin *pin = find_pin_mut(node, pin_id);
    if (!pin) {
        af_bp_value empty = {0};
        return empty;
    }

    /* Check if connected to an output link */
    const af_bp_link *link = af_blueprint_find_link_to_pin(ctx->bp, node->id, pin_id);
    if (!link) {
        return pin->default_val;
    }

    if (ctx->depth >= AF_BP_VM_MAX_DEPTH) {
        return pin->default_val;
    }

    af_bp_node *source_node = af_blueprint_get_node(ctx->bp, link->from_node_id);
    if (!source_node)
        return pin->default_val;

    af_bp_pin *source_pin = find_pin_mut(source_node, link->from_pin_id);
    if (!source_pin)
        return pin->default_val;

    ctx->depth++;

    /* Compute source node value based on its type */
    switch (source_node->type) {
    case AF_BP_NODE_ADD: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        source_pin->runtime_val.f = a + b;
        break;
    }
    case AF_BP_NODE_SUB: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        source_pin->runtime_val.f = a - b;
        break;
    }
    case AF_BP_NODE_MUL: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        source_pin->runtime_val.f = a * b;
        break;
    }
    case AF_BP_NODE_DIV: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        source_pin->runtime_val.f = fabsf(b) > 1e-6f ? (a / b) : 0.0f;
        break;
    }
    case AF_BP_NODE_LERP: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        float alpha = get_input_val(ctx, source_node, "Alpha").f;
        source_pin->runtime_val.f = a + (b - a) * alpha;
        break;
    }
    case AF_BP_NODE_SIN: {
        float angle = get_input_val(ctx, source_node, "Angle").f;
        source_pin->runtime_val.f = sinf(angle);
        break;
    }
    case AF_BP_NODE_COS: {
        float angle = get_input_val(ctx, source_node, "Angle").f;
        source_pin->runtime_val.f = cosf(angle);
        break;
    }
    case AF_BP_NODE_VEC3_MAKE: {
        float x = get_input_val(ctx, source_node, "X").f;
        float y = get_input_val(ctx, source_node, "Y").f;
        float z = get_input_val(ctx, source_node, "Z").f;
        source_pin->runtime_val.v = (af_vec3){x, y, z};
        break;
    }
    case AF_BP_NODE_VEC3_BREAK: {
        af_vec3 v = get_input_val(ctx, source_node, "Vector").v;
        if (strcmp(source_pin->name, "X") == 0)
            source_pin->runtime_val.f = v.x;
        else if (strcmp(source_pin->name, "Y") == 0)
            source_pin->runtime_val.f = v.y;
        else if (strcmp(source_pin->name, "Z") == 0)
            source_pin->runtime_val.f = v.z;
        break;
    }
    case AF_BP_NODE_VEC3_ADD: {
        af_vec3 a = get_input_val(ctx, source_node, "A").v;
        af_vec3 b = get_input_val(ctx, source_node, "B").v;
        source_pin->runtime_val.v = af_vec3_add(a, b);
        break;
    }
    case AF_BP_NODE_VEC3_SCALE: {
        af_vec3 v = get_input_val(ctx, source_node, "Vector").v;
        float s = get_input_val(ctx, source_node, "Scale").f;
        source_pin->runtime_val.v = af_vec3_scale(v, s);
        break;
    }
    case AF_BP_NODE_VEC3_DOT: {
        af_vec3 a = get_input_val(ctx, source_node, "A").v;
        af_vec3 b = get_input_val(ctx, source_node, "B").v;
        source_pin->runtime_val.f = af_vec3_dot(a, b);
        break;
    }
    case AF_BP_NODE_VEC3_LENGTH: {
        af_vec3 v = get_input_val(ctx, source_node, "Vector").v;
        source_pin->runtime_val.f = af_vec3_length(v);
        break;
    }
    case AF_BP_NODE_VEC3_NORMALIZE: {
        af_vec3 v = get_input_val(ctx, source_node, "Vector").v;
        source_pin->runtime_val.v = af_vec3_normalize(v);
        break;
    }
    case AF_BP_NODE_COMPARE_EQUAL: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        source_pin->runtime_val.b = fabsf(a - b) < 1e-4f;
        break;
    }
    case AF_BP_NODE_COMPARE_GREATER: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        source_pin->runtime_val.b = a > b;
        break;
    }
    case AF_BP_NODE_COMPARE_LESS: {
        float a = get_input_val(ctx, source_node, "A").f;
        float b = get_input_val(ctx, source_node, "B").f;
        source_pin->runtime_val.b = a < b;
        break;
    }
    case AF_BP_NODE_LOGIC_AND: {
        bool a = get_input_val(ctx, source_node, "A").b;
        bool b = get_input_val(ctx, source_node, "B").b;
        source_pin->runtime_val.b = a && b;
        break;
    }
    case AF_BP_NODE_LOGIC_OR: {
        bool a = get_input_val(ctx, source_node, "A").b;
        bool b = get_input_val(ctx, source_node, "B").b;
        source_pin->runtime_val.b = a || b;
        break;
    }
    case AF_BP_NODE_LOGIC_NOT: {
        bool in_val = get_input_val(ctx, source_node, "In").b;
        source_pin->runtime_val.b = !in_val;
        break;
    }
    case AF_BP_NODE_RANDOM_FLOAT: {
        float min_val = get_input_val(ctx, source_node, "Min").f;
        float max_val = get_input_val(ctx, source_node, "Max").f;
        float r = (float)rand() / (float)RAND_MAX;
        source_pin->runtime_val.f = min_val + (max_val - min_val) * r;
        break;
    }
    case AF_BP_NODE_GET_VAR: {
        const af_bp_variable *v = af_blueprint_get_variable(ctx->bp, source_node->extra_str);
        if (v)
            source_pin->runtime_val = v->val;
        break;
    }
    case AF_BP_NODE_GET_POSITION: {
        af_object_id obj_id = get_input_val(ctx, source_node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj)
            source_pin->runtime_val.v = obj->transform.position;
        break;
    }
    case AF_BP_NODE_GET_ROTATION: {
        af_object_id obj_id = get_input_val(ctx, source_node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj)
            source_pin->runtime_val.v = obj->transform.rotation;
        break;
    }
    case AF_BP_NODE_GET_SCALE: {
        af_object_id obj_id = get_input_val(ctx, source_node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj)
            source_pin->runtime_val.v = obj->transform.scale;
        break;
    }
    default:
        break;
    }

    ctx->depth--;
    return source_pin->runtime_val;
}

static void execute_node(af_bp_vm_ctx *ctx, af_bp_node *node, uint32_t entry_pin_id) {
    if (!ctx || !node || !ctx->bp->enabled)
        return;
    if (ctx->steps++ >= AF_BP_VM_MAX_STEPS)
        return;

    ctx->bp->last_executed_node_id = node->id;

    switch (node->type) {
    /* Flow Control */
    case AF_BP_NODE_BRANCH: {
        bool cond = get_input_val(ctx, node, "Condition").b;
        if (cond) {
            follow_exec_pin(ctx, node, "True");
        } else {
            follow_exec_pin(ctx, node, "False");
        }
        break;
    }
    case AF_BP_NODE_SEQUENCE: {
        follow_exec_pin(ctx, node, "Then 0");
        follow_exec_pin(ctx, node, "Then 1");
        follow_exec_pin(ctx, node, "Then 2");
        break;
    }
    case AF_BP_NODE_GATE: {
        af_bp_pin *entry_pin = find_pin_mut(node, entry_pin_id);
        if (entry_pin) {
            if (strcmp(entry_pin->name, "Open") == 0) {
                node->gate_open = true;
            } else if (strcmp(entry_pin->name, "Close") == 0) {
                node->gate_open = false;
            } else if (strcmp(entry_pin->name, "Toggle") == 0) {
                node->gate_open = !node->gate_open;
            } else if (strcmp(entry_pin->name, "Enter") == 0 && node->gate_open) {
                follow_exec_pin(ctx, node, "Exit");
            }
        }
        break;
    }
    case AF_BP_NODE_DELAY: {
        float dur = get_input_val(ctx, node, "Duration").f;
        node->custom_timer += ctx->dt;
        if (node->custom_timer >= dur) {
            node->custom_timer = 0.0f;
            follow_exec_pin(ctx, node, "Completed");
        }
        break;
    }
    case AF_BP_NODE_FOR_LOOP: {
        float first = get_input_val(ctx, node, "First Index").f;
        float last = get_input_val(ctx, node, "Last Index").f;
        af_bp_pin *idx_pin = find_pin_by_name(node, "Index", AF_BP_PIN_OUTPUT);
        for (float i = first; i <= last && ctx->steps < AF_BP_VM_MAX_STEPS; i += 1.0f) {
            if (idx_pin)
                idx_pin->runtime_val.f = i;
            follow_exec_pin(ctx, node, "Loop Body");
        }
        follow_exec_pin(ctx, node, "Completed");
        break;
    }

    /* Variables */
    case AF_BP_NODE_SET_VAR: {
        af_bp_value val = get_input_val(ctx, node, "Value");
        af_blueprint_set_variable(ctx->bp, node->extra_str, val);
        follow_exec_pin(ctx, node, "Exec");
        break;
    }

    /* Actions */
    case AF_BP_NODE_SET_POSITION: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 pos = get_input_val(ctx, node, "New Location").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj) {
            af_transform t = obj->transform;
            t.position = pos;
            af_scene_set_object_transform(ctx->scene, obj_id, t);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_SET_ROTATION: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 rot = get_input_val(ctx, node, "New Rotation").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj) {
            af_transform t = obj->transform;
            t.rotation = rot;
            af_scene_set_object_transform(ctx->scene, obj_id, t);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_SET_SCALE: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 sc = get_input_val(ctx, node, "New Scale").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj) {
            af_transform t = obj->transform;
            t.scale = sc;
            af_scene_set_object_transform(ctx->scene, obj_id, t);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_TRANSLATE: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 delta = get_input_val(ctx, node, "Delta Location").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj) {
            af_transform t = obj->transform;
            t.position = af_vec3_add(t.position, delta);
            af_scene_set_object_transform(ctx->scene, obj_id, t);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_ROTATE: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 delta = get_input_val(ctx, node, "Delta Rotation").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj) {
            af_transform t = obj->transform;
            t.rotation = af_vec3_add(t.rotation, delta);
            af_scene_set_object_transform(ctx->scene, obj_id, t);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_SET_VISIBLE: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        bool hidden = get_input_val(ctx, node, "Hidden").b;
        af_scene_set_object_visible(ctx->scene, obj_id, !hidden);
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_SET_COLOR: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 col = get_input_val(ctx, node, "Color RGB").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj) {
            af_material mat = obj->material;
            mat.base_color = (af_color){col.x, col.y, col.z, 1.0f};
            af_scene_set_object_material(ctx->scene, obj_id, mat);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_SET_LIGHT: {
        float intensity = get_input_val(ctx, node, "Intensity").f;
        af_vec3 col = get_input_val(ctx, node, "Color RGB").v;
        af_directional_light light = af_scene_get_light(ctx->scene);
        light.intensity = intensity;
        light.color = col;
        af_scene_set_light(ctx->scene, light);
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_ADD_FORCE: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 force = get_input_val(ctx, node, "Force").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj && obj->body.enabled) {
            af_rigid_body body = obj->body;
            body.velocity = af_vec3_add(body.velocity, af_vec3_scale(force, ctx->dt));
            af_scene_set_body(ctx->scene, obj_id, body);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_SET_VELOCITY: {
        af_object_id obj_id = get_input_val(ctx, node, "Target").obj;
        if (!obj_id)
            obj_id = ctx->bp->bound_object;
        af_vec3 vel = get_input_val(ctx, node, "Velocity").v;
        const af_scene_object *obj = af_scene_get_object(ctx->scene, obj_id);
        if (obj && obj->body.enabled) {
            af_rigid_body body = obj->body;
            body.velocity = vel;
            af_scene_set_body(ctx->scene, obj_id, body);
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_PLAY_SOUND: {
        if (ctx->engine_handle) {
            af_bp_pin *pin = find_pin_by_name(node, "Sound Name", AF_BP_PIN_INPUT);
            const char *snd = pin ? pin->default_val.s : "";
            float vol = get_input_val(ctx, node, "Volume").f;
            if (snd && snd[0]) {
                af_engine_play_sound((af_engine *)ctx->engine_handle, snd, vol);
            }
        }
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    case AF_BP_NODE_PRINT: {
        af_bp_pin *msg_pin = find_pin_by_name(node, "Message", AF_BP_PIN_INPUT);
        const char *msg = msg_pin ? msg_pin->default_val.s : "";
        float val = get_input_val(ctx, node, "Float Value").f;
        printf("[Blueprint %s] %s (value: %.3f)\n", ctx->bp->name, msg, (double)val);
        follow_exec_pin(ctx, node, "Exec");
        break;
    }
    default:
        break;
    }
}

void af_blueprint_reset_runtime(af_blueprint *bp) {
    if (!bp)
        return;
    bp->started = false;
    bp->last_executed_node_id = 0;
    for (size_t i = 0; i < bp->node_count; ++i) {
        bp->nodes[i].custom_timer = 0.0f;
        if (bp->nodes[i].type == AF_BP_NODE_GATE) {
            bp->nodes[i].gate_open = true;
        }
    }
}

void af_blueprint_step(af_blueprint *bp, af_scene *scene, void *engine_handle, float dt) {
    if (!bp || !scene || !bp->enabled)
        return;

    af_bp_vm_ctx ctx = {
        .bp = bp,
        .scene = scene,
        .engine_handle = engine_handle,
        .dt = dt,
        .depth = 0,
        .steps = 0,
    };

    /* BeginPlay */
    if (!bp->started) {
        bp->started = true;
        for (size_t i = 0; i < bp->node_count; ++i) {
            if (bp->nodes[i].type == AF_BP_NODE_EVENT_START) {
                ctx.steps = 0;
                follow_exec_pin(&ctx, &bp->nodes[i], "Exec");
            }
        }
    }

    /* Tick */
    for (size_t i = 0; i < bp->node_count; ++i) {
        if (bp->nodes[i].type == AF_BP_NODE_EVENT_TICK) {
            af_bp_pin *dt_pin = find_pin_by_name(&bp->nodes[i], "Delta Seconds", AF_BP_PIN_OUTPUT);
            if (dt_pin)
                dt_pin->runtime_val.f = dt;
            ctx.steps = 0;
            follow_exec_pin(&ctx, &bp->nodes[i], "Exec");
        }
    }
}

void af_blueprint_trigger_key(af_blueprint *bp, af_scene *scene, void *engine_handle, const char *key_name,
                              bool pressed) {
    if (!bp || !scene || !bp->enabled || !key_name)
        return;

    af_bp_vm_ctx ctx = {
        .bp = bp,
        .scene = scene,
        .engine_handle = engine_handle,
        .dt = 0.016f,
        .depth = 0,
        .steps = 0,
    };

    for (size_t i = 0; i < bp->node_count; ++i) {
        if (bp->nodes[i].type == AF_BP_NODE_EVENT_KEY) {
            if (!bp->nodes[i].extra_str[0] || strcmp(bp->nodes[i].extra_str, key_name) == 0) {
                ctx.steps = 0;
                follow_exec_pin(&ctx, &bp->nodes[i], pressed ? "Pressed" : "Released");
            }
        }
    }
}

void af_blueprint_trigger_collision(af_blueprint *bp, af_scene *scene, void *engine_handle, af_object_id obj_a,
                                    af_object_id obj_b) {
    if (!bp || !scene || !bp->enabled)
        return;

    af_bp_vm_ctx ctx = {
        .bp = bp,
        .scene = scene,
        .engine_handle = engine_handle,
        .dt = 0.016f,
        .depth = 0,
        .steps = 0,
    };

    af_object_id other = 0;
    if (bp->bound_object == obj_a)
        other = obj_b;
    else if (bp->bound_object == obj_b)
        other = obj_a;
    else if (!bp->bound_object)
        other = obj_b;
    else
        return;

    for (size_t i = 0; i < bp->node_count; ++i) {
        if (bp->nodes[i].type == AF_BP_NODE_EVENT_COLLISION) {
            af_bp_pin *other_pin = find_pin_by_name(&bp->nodes[i], "Other Object", AF_BP_PIN_OUTPUT);
            if (other_pin)
                other_pin->runtime_val.obj = other;
            ctx.steps = 0;
            follow_exec_pin(&ctx, &bp->nodes[i], "Exec");
        }
    }
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/blueprint.h>
#include <afterlight/scene.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr)                                                                                                    \
    do {                                                                                                               \
        if (!(expr)) {                                                                                                 \
            fprintf(stderr, "%s:%d: Assertion failed: %s\n", __FILE__, __LINE__, #expr);                               \
            exit(1);                                                                                                   \
        }                                                                                                              \
    } while (0)

static void test_creation_and_pins(void) {
    af_blueprint *bp = af_blueprint_create("TestGraph");
    CHECK(bp != NULL);
    CHECK(strcmp(bp->name, "TestGraph") == 0);
    CHECK(bp->enabled == true);

    uint32_t n_start = af_blueprint_add_node(bp, AF_BP_NODE_EVENT_START, 0, 0);
    uint32_t n_tick = af_blueprint_add_node(bp, AF_BP_NODE_EVENT_TICK, 0, 150);
    uint32_t n_add = af_blueprint_add_node(bp, AF_BP_NODE_ADD, 200, 0);
    uint32_t n_branch = af_blueprint_add_node(bp, AF_BP_NODE_BRANCH, 200, 150);

    CHECK(n_start != 0);
    CHECK(n_tick != 0);
    CHECK(n_add != 0);
    CHECK(n_branch != 0);

    const af_bp_node *node_add = af_blueprint_get_node_const(bp, n_add);
    CHECK(node_add != NULL);
    CHECK(node_add->pin_count == 3); /* A, B, Result */
    CHECK(node_add->pins[0].dir == AF_BP_PIN_INPUT);
    CHECK(node_add->pins[1].dir == AF_BP_PIN_INPUT);
    CHECK(node_add->pins[2].dir == AF_BP_PIN_OUTPUT);

    af_blueprint_destroy(bp);
}

static void test_linking(void) {
    af_blueprint *bp = af_blueprint_create("LinkTest");

    uint32_t n_tick = af_blueprint_add_node(bp, AF_BP_NODE_EVENT_TICK, 0, 0);
    uint32_t n_branch = af_blueprint_add_node(bp, AF_BP_NODE_BRANCH, 200, 0);
    uint32_t n_add = af_blueprint_add_node(bp, AF_BP_NODE_ADD, 0, 200);

    /* Exec out of Tick (pin 1) to Exec in of Branch (pin 1) */
    CHECK(af_blueprint_can_link_pins(bp, n_tick, 1, n_branch, 1));
    uint32_t link_exec = af_blueprint_add_link(bp, n_tick, 1, n_branch, 1);
    CHECK(link_exec != 0);

    /* Data out of Add (pin 3: Result) to Condition of Branch (pin 2: bool) */
    CHECK(af_blueprint_can_link_pins(bp, n_add, 3, n_branch, 2));
    uint32_t link_data = af_blueprint_add_link(bp, n_add, 3, n_branch, 2);
    CHECK(link_data != 0);

    /* Reject Exec to Float */
    CHECK(!af_blueprint_can_link_pins(bp, n_tick, 1, n_add, 1));
    /* Reject Input to Output */
    CHECK(!af_blueprint_can_link_pins(bp, n_branch, 1, n_tick, 1));
    /* Reject Same Node */
    CHECK(!af_blueprint_can_link_pins(bp, n_branch, 3, n_branch, 1));

    CHECK(bp->link_count == 2);
    CHECK(af_blueprint_remove_link(bp, link_exec));
    CHECK(bp->link_count == 1);

    af_blueprint_destroy(bp);
}

static void test_variables(void) {
    af_blueprint *bp = af_blueprint_create("VarTest");

    af_bp_value vf = {.type = AF_BP_PIN_FLOAT, .f = 42.0f};
    CHECK(af_blueprint_add_variable(bp, "Speed", AF_BP_PIN_FLOAT, vf));

    af_bp_value vb = {.type = AF_BP_PIN_BOOL, .b = true};
    CHECK(af_blueprint_add_variable(bp, "IsActive", AF_BP_PIN_BOOL, vb));

    const af_bp_variable *var = af_blueprint_get_variable(bp, "Speed");
    CHECK(var != NULL);
    CHECK(fabsf(var->val.f - 42.0f) < 1e-5f);

    vf.f = 99.5f;
    CHECK(af_blueprint_set_variable(bp, "Speed", vf));
    var = af_blueprint_get_variable(bp, "Speed");
    CHECK(fabsf(var->val.f - 99.5f) < 1e-5f);

    CHECK(af_blueprint_remove_variable(bp, "Speed"));
    CHECK(af_blueprint_get_variable(bp, "Speed") == NULL);

    af_blueprint_destroy(bp);
}

static void test_vm_math_and_logic(void) {
    af_blueprint *bp = af_blueprint_create("MathVM");
    af_scene *scene = af_scene_create();

    /* Add node: A=10, B=20 */
    uint32_t n_add = af_blueprint_add_node(bp, AF_BP_NODE_ADD, 0, 0);
    af_bp_node *node_add = af_blueprint_get_node(bp, n_add);
    node_add->pins[0].default_val.f = 10.0f;
    node_add->pins[1].default_val.f = 20.0f;

    /* Multiply node: A connected to Add.Result, B=3 */
    uint32_t n_mul = af_blueprint_add_node(bp, AF_BP_NODE_MUL, 200, 0);
    af_bp_node *node_mul = af_blueprint_get_node(bp, n_mul);
    node_mul->pins[1].default_val.f = 3.0f;
    af_blueprint_add_link(bp, n_add, 3, n_mul, 1);

    /* Set Variable node: sets "Answer" */
    af_bp_value init_val = {.type = AF_BP_PIN_FLOAT, .f = 0.0f};
    af_blueprint_add_variable(bp, "Answer", AF_BP_PIN_FLOAT, init_val);
    uint32_t n_set = af_blueprint_add_node(bp, AF_BP_NODE_SET_VAR, 400, 0);
    af_bp_node *node_set = af_blueprint_get_node(bp, n_set);
    snprintf(node_set->extra_str, sizeof(node_set->extra_str), "Answer");

    /* BeginPlay -> SetVar */
    uint32_t n_start = af_blueprint_add_node(bp, AF_BP_NODE_EVENT_START, -200, 0);
    af_blueprint_add_link(bp, n_start, 1, n_set, 1);
    /* Mul.Result -> SetVar.Value */
    af_blueprint_add_link(bp, n_mul, 3, n_set, 2);

    /* Step graph */
    af_blueprint_step(bp, scene, NULL, 0.016f);

    const af_bp_variable *ans = af_blueprint_get_variable(bp, "Answer");
    CHECK(ans != NULL);
    /* (10 + 20) * 3 = 90 */
    CHECK(fabsf(ans->val.f - 90.0f) < 1e-4f);

    af_scene_destroy(scene);
    af_blueprint_destroy(bp);
}

static void test_vm_scene_action(void) {
    af_blueprint *bp = af_blueprint_create("SceneActionVM");
    af_scene *scene = af_scene_create();
    af_object_id obj_id = af_scene_add_object(scene, "Mover", "cube");
    bp->bound_object = obj_id;

    /* Event Tick -> Add Actor Offset (0, 5, 0) */
    uint32_t n_tick = af_blueprint_add_node(bp, AF_BP_NODE_EVENT_TICK, 0, 0);
    uint32_t n_trans = af_blueprint_add_node(bp, AF_BP_NODE_TRANSLATE, 200, 0);
    af_bp_node *node_trans = af_blueprint_get_node(bp, n_trans);
    node_trans->pins[2].default_val.v = (af_vec3){0.0f, 5.0f, 0.0f};

    af_blueprint_add_link(bp, n_tick, 1, n_trans, 1);

    /* Step 4 frames */
    for (int i = 0; i < 4; ++i) {
        af_blueprint_step(bp, scene, NULL, 0.016f);
    }

    const af_scene_object *obj = af_scene_get_object(scene, obj_id);
    CHECK(obj != NULL);
    /* 4 steps * 5 units = 20 units Y */
    CHECK(fabsf(obj->transform.position.y - 20.0f) < 1e-4f);

    af_scene_destroy(scene);
    af_blueprint_destroy(bp);
}

static void test_serialization(void) {
    af_blueprint *bp = af_blueprint_create("SerializeTest");
    bp->bound_object = 42;

    af_bp_value vf = {.type = AF_BP_PIN_FLOAT, .f = 3.1415f};
    af_blueprint_add_variable(bp, "Pi", AF_BP_PIN_FLOAT, vf);

    uint32_t n1 = af_blueprint_add_node(bp, AF_BP_NODE_EVENT_START, 100, 150);
    uint32_t n2 = af_blueprint_add_node(bp, AF_BP_NODE_PRINT, 350, 150);
    af_blueprint_add_link(bp, n1, 1, n2, 1);
    uint32_t n3 = af_blueprint_add_node(bp, AF_BP_NODE_TIMELINE, 560, 150);
    af_bp_node *node = af_blueprint_get_node(bp, n3);
    CHECK(node && node->timeline);
    af_timeline *timeline = node->timeline;
    timeline->light_count = 2;
    timeline->lights[0] = (af_timeline_light){0, 1};
    timeline->lights[1] = (af_timeline_light){5, 3};

    char *json = af_blueprint_serialize_json(bp);
    CHECK(json != NULL);
    CHECK(strstr(json, "afterlight.blueprint") != NULL);
    CHECK(strstr(json, "SerializeTest") != NULL);
    CHECK(strstr(json, "Pi") != NULL);

    af_blueprint *loaded = af_blueprint_deserialize_json(json);
    CHECK(loaded != NULL);
    CHECK(strcmp(loaded->name, "SerializeTest") == 0);
    CHECK(loaded->bound_object == 42);
    CHECK(loaded->node_count == 3);
    CHECK(loaded->link_count == 1);
    CHECK(loaded->var_count == 1);

    const af_bp_variable *pi_var = af_blueprint_get_variable(loaded, "Pi");
    CHECK(pi_var != NULL);
    CHECK(fabsf(pi_var->val.f - 3.1415f) < 1e-4f);
    af_blueprint *clone = af_blueprint_clone(loaded);
    CHECK(clone);
    af_timeline *copy = af_blueprint_get_node(clone, n3)->timeline;
    CHECK(copy && copy != af_blueprint_get_node(loaded, n3)->timeline);
    af_scene *scene = af_scene_create();
    CHECK(scene && af_timeline_sample(copy, scene, AF_OBJECT_NONE, 2.5f));
    CHECK(af_scene_get_light(scene).intensity == 2);
    copy->lights[1].time = 0;
    CHECK(!af_timeline_valid(copy) && !af_blueprint_serialize_json(clone));
    af_scene_destroy(scene);
    af_blueprint_destroy(clone);

    free(json);
    af_blueprint_destroy(loaded);
    af_blueprint_destroy(bp);
}

static void test_scene_blueprints(void) {
    af_scene *scene = af_scene_create();
    CHECK(scene != NULL);
    CHECK(af_scene_blueprint_count(scene) == 0);

    af_blueprint *bp = af_scene_add_blueprint(scene, "SceneRotator");
    CHECK(bp != NULL);
    CHECK(af_scene_blueprint_count(scene) == 1);
    CHECK(af_scene_get_blueprint(scene, 0) == bp);
    CHECK(af_scene_get_blueprint_by_name(scene, "SceneRotator") == bp);

    af_object_id obj = af_scene_add_object(scene, "Spinner", "cube");
    bp->bound_object = obj;

    /* Tick -> Add Actor Rotation (0, 1.5, 0) */
    uint32_t n_tick = af_blueprint_add_node(bp, AF_BP_NODE_EVENT_TICK, 0, 0);
    uint32_t n_rot = af_blueprint_add_node(bp, AF_BP_NODE_ROTATE, 200, 0);
    af_bp_node *node_rot = af_blueprint_get_node(bp, n_rot);
    node_rot->pins[2].default_val.v = (af_vec3){0.0f, 1.5f, 0.0f};
    af_blueprint_add_link(bp, n_tick, 1, n_rot, 1);

    af_scene_step_blueprints(scene, NULL, 0.016f);

    const af_scene_object *o = af_scene_get_object(scene, obj);
    CHECK(o != NULL);
    CHECK(fabsf(o->transform.rotation.y - 1.5f) < 1e-4f);

    /* Clone scene */
    af_scene *clone = af_scene_clone(scene);
    CHECK(clone != NULL);
    CHECK(af_scene_blueprint_count(clone) == 1);
    CHECK(strcmp(af_scene_get_blueprint(clone, 0)->name, "SceneRotator") == 0);

    /* Remove blueprint from original */
    CHECK(af_scene_remove_blueprint(scene, 0));
    CHECK(af_scene_blueprint_count(scene) == 0);

    af_scene_destroy(clone);
    af_scene_destroy(scene);
}

int main(void) {
    printf("Running Blueprint tests...\n");
    test_creation_and_pins();
    test_linking();
    test_variables();
    test_vm_math_and_logic();
    test_vm_scene_action();
    test_serialization();
    test_scene_blueprints();
    printf("All Blueprint tests passed!\n");
    return 0;
}

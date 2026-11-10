/* SPDX-License-Identifier: MPL-2.0 */
#include "runtime/runtime.h"
#include <afterlight/blueprint.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Transition %d: %s (%s)\n", __LINE__, #x, r.status);                                       \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    ed_runtime r = {0};
    CHECK(argc == 2);
    af_scene *s = af_scene_create(), *next = af_scene_create();
    af_scene_add_object(s, "First", "cube");
    af_scene_add_object(next, "Second", "cube");
    af_blueprint *graph = af_scene_add_blueprint(s, "Broken route");
    uint32_t begin = af_blueprint_add_node(graph, AF_BP_NODE_EVENT_START, 0, 0);
    uint32_t change = af_blueprint_add_node(graph, AF_BP_NODE_CHANGE_SCENE, 0, 0);
    af_bp_node *node = af_blueprint_get_node(graph, change);
    CHECK(node && af_blueprint_add_link(graph, begin, 1, change, 1));
    snprintf(node->pins[1].default_val.s, sizeof(node->pins[1].default_val.s), "missing");
    af_scene_error e;
    CHECK(af_scene_save(next, "second.afterlight.json", &e));
    CHECK(ed_runtime_set_project(&r, "game.afterlight.project", "first.afterlight.json"));
    CHECK(ed_runtime_start_file(&r, s, NULL, argv[1]));
    ed_runtime_frame_update(&r, .01, false);
    graph = af_scene_get_blueprint(r.scene, 0);
    const af_bp_trace_entry *trace = af_blueprint_trace_at(graph, 0);
    CHECK(trace && trace->node == change && af_blueprint_trace_at(graph, 1)->node == begin);
    CHECK(graph->diagnostic_node == change && strstr(graph->diagnostic, "missing") &&
          strstr(r.status, "Broken route node 2 -> missing") && !strcmp(af_scene_object_at(r.scene, 0)->name, "First"));
    CHECK(!ed_runtime_request_scene(&r, "../escape"));
    CHECK(ed_runtime_request_scene(&r, "missing") && !ed_runtime_transition(&r) &&
          !strcmp(af_scene_object_at(r.scene, 0)->name, "First"));
    CHECK(ed_runtime_request_scene(&r, "second"));
    CHECK(!strcmp(af_scene_object_at(r.scene, 0)->name, "First"));
    ed_runtime_update(&r, .01, false);
    CHECK(r.transition_count == 1 && !strcmp(af_scene_object_at(r.scene, 0)->name, "Second"));
    CHECK(r.host.runtime_context == &r && !strcmp(af_scene_object_at(s, 0)->name, "First"));
    ed_runtime_stop(&r);
    af_scene_destroy(s);
    af_scene_destroy(next);
    remove("second.afterlight.json");
    puts("Scene transitions: deferred swap, named local scenes, invalid paths, failed-load retention and source "
         "isolation passed.");
    return 0;
}

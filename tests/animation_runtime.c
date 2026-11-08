/* SPDX-License-Identifier: MPL-2.0 */
#include "animation_runtime.h"
#include "document.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Animation runtime %d: %s\n", __LINE__, #c);                                               \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id node(const af_scene *s, af_object_id root, uint32_t source) {
    for (size_t i = 0; i < af_scene_object_count(s); ++i) {
        const af_scene_object *o = af_scene_object_at(s, i);
        if (o->model.root == root && !o->model.primitive && o->model.node == source)
            return o->id;
    }
    return 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_import_mesh(&d, "actor", argv[1]));
    af_object_id root = d.selection, tip = node(d.scene, root, 2);
    CHECK(tip);
    const af_scene_asset *a = af_scene_find_asset(d.scene, "actor_animation");
    CHECK(a && a->kind == AF_ASSET_ANIMATION);
    char asset_name[64];
    snprintf(asset_name, sizeof(asset_name), "%s", a->name);
    af_animation_runtime *runtime = af_animation_runtime_create(NULL);
    af_asset_error error;
    CHECK(runtime && af_animation_runtime_load(runtime, asset_name, argv[1], &error));
    af_animation_runtime_scene(runtime, d.scene);
    uint64_t revision = d.revision;
    af_transform authored = af_scene_get_object(d.scene, tip)->transform;
    CHECK(af_animation_runtime_update(runtime, NULL, 1, .5, false));
    af_mat4 world;
    CHECK(af_animation_runtime_world(runtime, tip, &world) && fabsf(world.elements[0] - 1) < .0001f);
    CHECK(af_animation_runtime_update(runtime, NULL, 1, .5, true));
    CHECK(af_animation_runtime_world(runtime, tip, &world) && fabsf(world.elements[0] - sqrtf(.75f)) < .0001f);
    CHECK(d.revision == revision && af_scene_get_object(d.scene, tip)->transform.rotation.z == authored.rotation.z);
    af_animation_bone bones[2];
    CHECK(af_animation_runtime_bones(runtime, root, bones, 2) == 2 && bones[1].has_parent &&
          isfinite(bones[1].position.x));
    af_animation_debug debug = af_animation_runtime_debug(runtime, root);
    CHECK(debug.available && !debug.driven && debug.pending_events == 0);
    CHECK(af_animation_runtime_pause(runtime, root, true) && af_animation_runtime_update(runtime, NULL, 1, .5, true) &&
          af_animation_runtime_status(runtime, root).time == .5);
    CHECK(af_animation_runtime_seek(runtime, root, 1) && af_animation_runtime_update(runtime, NULL, 1, 0, true) &&
          af_animation_runtime_world(runtime, tip, &world) && fabsf(world.elements[0] - .5f) < .0001f);
    CHECK(!af_animation_runtime_load(runtime, asset_name, "missing-animation.gltf", &error) &&
          af_animation_runtime_status(runtime, root).time == 1); /* last-good source */
    af_animation_runtime_scene(runtime, NULL);
    CHECK(ed_document_duplicate(&d));
    af_object_id duplicate = d.selection;
    CHECK(duplicate != root && node(d.scene, duplicate, 2) != tip);
    af_animation_runtime_scene(runtime, d.scene);
    CHECK(af_animation_runtime_update(runtime, NULL, 1, .25, true));
    CHECK(af_animation_runtime_seek(runtime, duplicate, .75) &&
          af_animation_runtime_status(runtime, root).time == .25 &&
          af_animation_runtime_status(runtime, duplicate).time == .75);
    af_scene_error scene_error;
    CHECK(af_scene_save(d.scene, "animation.afterlight.json", &scene_error));
    af_scene *loaded = af_scene_load("animation.afterlight.json", &scene_error);
    CHECK(loaded && node(loaded, duplicate, 2) && af_scene_find_asset(loaded, asset_name)->kind == AF_ASSET_ANIMATION);
    af_scene_destroy(loaded);
    af_animation_component configuration = af_scene_get_object(d.scene, root)->animation;
    configuration.speed = 2;
    CHECK(ed_document_animation(&d, root, configuration));
    ed_document_end_edit(&d);
    CHECK(ed_document_undo(&d) && af_scene_get_object(d.scene, root)->animation.speed == 1);
    af_animation_runtime_scene(runtime, NULL);
    af_animation_runtime_destroy(runtime);
    ed_document_destroy(&d);
    puts("Animation runtime: source bindings, authored-pose isolation, pause/seek, last-good source, independent "
         "duplicates, persistence and undo passed.");
    return 0;
}

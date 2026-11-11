/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include "animation/animation_runtime.h"
#include "document/document.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Animation authoring %d: %s\n", __LINE__, #c);                                             \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)

static int roundtrip(const char *path) {
    af_asset_error error;
    af_animation_file source, copy, loaded;
    CHECK(af_animation_file_load(path, &source, &error));
    CHECK(af_animation_file_copy(&source, &copy, &error));
    CHECK(af_animation_file_save("authoring-roundtrip.afanim", &copy, &error));
    CHECK(af_animation_file_load("authoring-roundtrip.afanim", &loaded, &error));
    CHECK(loaded.node_count == source.node_count && loaded.skin_count == source.skin_count &&
          loaded.clip_count == source.clip_count);
    af_animation_pose *a = calloc(source.node_count, sizeof(*a)), *b = calloc(source.node_count, sizeof(*b));
    CHECK(a && b);
    for (size_t i = 0; i < source.clip_count; ++i)
        for (unsigned step = 0; step < 5; ++step) {
            double time = source.clips[i].duration * step / 4;
            CHECK(af_animation_sample(&source, i, time, a, source.node_count));
            CHECK(af_animation_sample(&loaded, i, time, b, loaded.node_count));
            CHECK(!memcmp(a, b, source.node_count * sizeof(*a)));
        }
    free(a);
    free(b);
    af_animation_file_free(&source);
    af_animation_file_free(&copy);
    af_animation_file_free(&loaded);
    return 0;
}
int main(int argc, char **argv) {
    CHECK(argc == 4);
    for (int i = 1; i < argc; ++i)
        CHECK(!roundtrip(argv[i]));
    af_asset_error error;
    af_animation_file f;
    CHECK(af_animation_file_load(argv[1], &f, &error));
    size_t clip = f.clip_count;
    CHECK(af_animation_clip_add(&f, "Native wave", 2));
    CHECK(af_animation_key_set(&f, clip, 2, AF_ANIM_ROTATION, 1, (af_vec4){0, 0, 1, 0}, AF_ANIM_LINEAR));
    af_animation_pose poses[4];
    CHECK(af_animation_sample(&f, clip, .5, poses, 4));
    CHECK(fabsf(poses[2].rotation.z - sqrtf(.5f)) < .0001f);
    CHECK(af_animation_key_set(&f, clip, 2, AF_ANIM_ROTATION, 1, (af_vec4){0, 0, 0, 1}, AF_ANIM_STEP));
    CHECK(f.clips[clip].channels[0].key_count == 2);
    CHECK(af_animation_key_set(&f, clip, 2, AF_ANIM_TRANSLATION, 1, (af_vec4){2, 1, 0, 0}, AF_ANIM_LINEAR));
    CHECK(af_animation_key_set(&f, clip, 2, AF_ANIM_TRANSLATION, .5f, (af_vec4){1, 1, 0, 0}, AF_ANIM_LINEAR));
    CHECK(af_animation_key_remove(&f, clip, 2, AF_ANIM_TRANSLATION, .5f));
    CHECK(!af_animation_key_set(&f, clip, 2, AF_ANIM_ROTATION, NAN, (af_vec4){0}, AF_ANIM_LINEAR));
    CHECK(!af_animation_key_set(&f, clip, 2, AF_ANIM_TRANSLATION, 3, (af_vec4){0}, AF_ANIM_LINEAR));
    CHECK(af_animation_file_save("authoring-native.afanim", &f, &error));
    f.parents[2] = 2;
    CHECK(!af_animation_file_save("authoring-native.afanim", &f, &error));
    af_animation_file_free(&f);
    CHECK(af_animation_file_load("authoring-native.afanim", &f, &error));
    CHECK(f.parents[2] != 2);
    FILE *input = fopen("authoring-native.afanim", "rb");
    CHECK(input && !fseek(input, 0, SEEK_END));
    long size = ftell(input);
    CHECK(size > 16 && !fseek(input, 0, SEEK_SET));
    unsigned char *bytes = malloc((size_t)size);
    CHECK(bytes && fread(bytes, 1, (size_t)size, input) == (size_t)size && !fclose(input));
    for (size_t cut = 8; cut < (size_t)size; cut += 31) {
        FILE *bad = fopen("authoring-truncated.afanim", "wb");
        CHECK(bad && fwrite(bytes, 1, cut, bad) == cut && !fclose(bad));
        af_animation_file rejected;
        CHECK(!af_animation_file_load("authoring-truncated.afanim", &rejected, &error) && !rejected.bind_pose);
    }
    free(bytes);
    af_animation_runtime *runtime = af_animation_runtime_create(NULL);
    ed_document d;
    CHECK(runtime && ed_document_init(&d) && ed_document_import_mesh(&d, "actor", argv[1]));
    CHECK(af_animation_runtime_load(runtime, "actor_animation", "authoring-native.afanim", &error));
    af_animation_runtime_scene(runtime, d.scene);
    CHECK(af_animation_runtime_play(runtime, d.selection, clip, 0));
    CHECK(af_animation_runtime_seek(runtime, d.selection, .5));
    CHECK(af_animation_runtime_update(runtime, NULL, 1, 0, true));
    CHECK(af_animation_runtime_status(runtime, d.selection).clip == clip);
    af_animation_runtime_scene(runtime, NULL);
    af_animation_runtime_destroy(runtime);
    ed_document_destroy(&d);
    af_animation_file_free(&f);
    remove("authoring-native.afanim");
    remove("authoring-roundtrip.afanim");
    remove("authoring-truncated.afanim");
    puts("Native animation: TRS/morph/cubic roundtrips, pose keys, interpolation, deletion, malformed files, atomic "
         "save and runtime playback passed.");
    return 0;
}

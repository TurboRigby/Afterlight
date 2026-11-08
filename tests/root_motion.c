/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/afterlight.h>
#include <stdio.h>
#include <math.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Root motion %d: %s\n", __LINE__, #x);                                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_animation_file f;
    af_asset_error e;
    CHECK(af_animation_file_load(argv[1], &f, &e));
    af_vec3 delta;
    CHECK(af_animation_root_delta(&f, 1, 10, 1.9, .2, true, &delta) && fabsf(delta.z + .4f) < 1e-5f);
    CHECK(af_animation_root_delta(&f, 1, 10, .1, -4.2, true, &delta) && fabsf(delta.z - 8.4f) < 1e-5f);
    CHECK(af_animation_root_delta(&f, 1, 10, 1.9, .2, false, &delta) && fabsf(delta.z + .2f) < 1e-5f);
    af_locomotion_player p = {0};
    CHECK(af_locomotion_player_init(&p, &f));
    p.root_node = 10;
    p.root_motion = true;
    af_animation_pose out[64];
    CHECK(af_locomotion_player_step(&p, &f, (af_locomotion_input){.speed = 2, .grounded = true}, 1.f / 60) &&
          fabsf(p.root_delta.z + 2.f / 60) < 1e-5f);
    CHECK(af_locomotion_player_sample(&p, &f, out, 64) && fabsf(out[10].translation.z) < 1e-6f);
    af_scene *scene = af_scene_create();
    af_object_id wall = af_scene_add_object(scene, "Wall", "cube");
    af_collider box = af_collider_default();
    box.type = AF_COLLIDER_BOX;
    box.half_extents = (af_vec3){2, 2, .1f};
    CHECK(af_scene_set_object_collider(scene, wall, box));
    af_transform t = af_transform_identity();
    t.position = (af_vec3){0, 1, -1};
    CHECK(af_scene_set_object_transform(scene, wall, t));
    af_character c = {.position = {0, 1, 0}};
    af_character_config config = af_character_config_default();
    config.gravity = 0;
    for (unsigned i = 0; i < 120; ++i)
        CHECK(af_character_step_root_motion(scene, &c, config, (af_vec3){.x = .01f, .z = -.04f}, false, 1.f / 60,
                                            af_query_filter_default()));
    CHECK(c.position.z > -.6f && c.position.x > 1);
    af_character old = c;
    CHECK(!af_character_step_root_motion(scene, &c, config, (af_vec3){NAN, 0, 0}, false, 1.f / 60,
                                         af_query_filter_default()) &&
          c.position.x == old.position.x);
    af_scene_destroy(scene);
    af_locomotion_player_destroy(&p);
    af_animation_file_free(&f);
    puts("Root motion: wraps, reverse, clamping, visual extraction and collision sliding passed.");
    return 0;
}

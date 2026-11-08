/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/locomotion.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Blend %d: %s\n", __LINE__, #x);                                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_animation_file f;
    af_asset_error e;
    CHECK(af_animation_file_load(argv[1], &f, &e));
    af_locomotion_player p = {0};
    CHECK(af_locomotion_player_init(&p, &f));
    af_animation_pose out[64], a[64], b[64];
    for (unsigned i = 0; i < 30; ++i)
        CHECK(af_locomotion_player_step(&p, &f, (af_locomotion_input){.speed = 3.5f, .grounded = true}, 1.f / 60));
    CHECK(fabsf(p.weights[1] - .5f) < 1e-6f && fabsf(p.weights[2] - .5f) < 1e-6f);
    CHECK(af_locomotion_player_sample(&p, &f, out, 64));
    CHECK(af_animation_sample(&f, p.clips[1], p.motion.phase * f.clips[p.clips[1]].duration, a, 64));
    CHECK(af_animation_sample(&f, p.clips[2], p.motion.phase * f.clips[p.clips[2]].duration, b, 64));
    CHECK(fabsf(out[1].translation.y - (a[1].translation.y + b[1].translation.y) * .5f) < 1e-5f);
    CHECK(af_locomotion_player_step(&p, &f, (af_locomotion_input){.speed = 3.5f, .vertical_speed = 4}, 1.f / 60));
    CHECK(p.motion.state == AF_LOCOMOTION_JUMP && p.motion.transition < .2f);
    CHECK(af_locomotion_player_sample(&p, &f, out, 64));
    double phase = p.motion.phase;
    CHECK(!af_locomotion_player_step(&p, &f, (af_locomotion_input){.speed = NAN}, 1.f / 60) && p.motion.phase == phase);
    af_locomotion_player_destroy(&p);
    af_character_rig rig = af_character_rig_default();
    rig.root_node = 10;
    rig.clips[1] = 2;
    rig.clips[2] = 1;
    f.clips[1].name[0] = 0;
    CHECK(!af_locomotion_player_init(&p, &f));
    CHECK(af_locomotion_player_init_rig(&p, &f, rig) && p.clips[1] == 2);
    af_locomotion_player_destroy(&p);
    af_animation_file_free(&f);
    puts("Locomotion blending: adjacent weights, synchronized phases, intermediate pose and airborne transitions "
         "passed.");
    return 0;
}

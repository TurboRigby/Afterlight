/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include <math.h>
#include <stdio.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Playback %d: %s\n", __LINE__, #c);                                                        \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 2);
    af_animation_file file;
    af_asset_error error;
    CHECK(af_animation_file_load(argv[1], &file, &error));
    af_animation_pose poses[2];
    af_animation_player p = af_animation_player_default();
    CHECK(af_animation_player_play(&p, &file, 0, 0) && af_animation_player_update(&p, &file, .5));
    CHECK(p.time == .5 && af_animation_player_sample(&p, &file, poses, 2) &&
          fabsf(poses[0].translation.x - 1) < .0001f);
    af_animation_player_pause(&p, true);
    CHECK(af_animation_player_update(&p, &file, 1) && p.time == .5);
    af_animation_player_pause(&p, false);
    CHECK(af_animation_player_seek(&p, &file, 1.5) && af_animation_player_update(&p, &file, 1) && p.time == .5);
    p.loop = false;
    CHECK(af_animation_player_seek(&p, &file, 1.5) && af_animation_player_update(&p, &file, 1) && p.time == 2 &&
          p.finished && !p.playing);
    p.speed = -1;
    CHECK(af_animation_player_play(&p, &file, 0, 0) && p.time == 2 && af_animation_player_update(&p, &file, .5) &&
          p.time == 1.5);
    p.speed = 1;
    p.loop = true;
    CHECK(af_animation_player_play(&p, &file, 0, 0) && af_animation_player_update(&p, &file, .5));
    CHECK(af_animation_player_play(&p, &file, 1, .5f) && af_animation_player_update(&p, &file, .25) &&
          af_animation_player_sample(&p, &file, poses, 2));
    CHECK(fabsf(poses[0].translation.x - .75f) < .0001f);
    float before = poses[0].translation.x;
    CHECK(af_animation_player_play(&p, &file, 2, .5f) && af_animation_player_sample(&p, &file, poses, 2) &&
          fabsf(poses[0].translation.x - before) < .0001f); /* interrupted blend is continuous */
    CHECK(af_animation_player_update(&p, &file, .5) && !p.fading && af_animation_player_sample(&p, &file, poses, 2));
    CHECK(fabsf(poses[0].translation.x - 1.25f) < .0001f);
    af_animation_player_stop(&p);
    CHECK(!p.playing && p.time == 0 && !p.fading);
    CHECK(!af_animation_player_play(&p, &file, 99, 0) && !af_animation_player_seek(&p, &file, NAN) &&
          !af_animation_player_update(&p, &file, -1));
    p.loop = false;
    file.clips[1].duration = .1;
    CHECK(af_animation_player_play(&p, &file, 1, 1) && af_animation_player_update(&p, &file, .2) && p.finished &&
          p.playing && p.fading);
    CHECK(af_animation_player_update(&p, &file, 1) && !p.playing && !p.fading);
    af_animation_player_destroy(&p);
    af_animation_file_free(&file);
    puts("Playback: pause, loop, seek, reverse, endpoint hold, crossfade and interrupted-blend continuity passed.");
    return 0;
}

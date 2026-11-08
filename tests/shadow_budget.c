/* SPDX-License-Identifier: MPL-2.0 */
#include "local_shadow.h"
#include <stdio.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Shadow budget %d: %s\n", __LINE__, #c);                                                   \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 0, 0};
    camera.pitch = 0;
    camera.yaw = -AF_PI / 2;
    af_local_light lights[8];
    for (unsigned i = 0; i < 8; ++i) {
        lights[i] = af_local_light_default(AF_LIGHT_POINT);
        lights[i].position = (af_vec3){0, 0, -(float)(10 + i)};
        lights[i].range = 2;
        lights[i].cast_shadows = true;
    }
    lights[0].position = (af_vec3){1000, 0, 0};
    lights[1].enabled = false;
    lights[2].intensity = 0;
    lights[3].cast_shadows = false;
    unsigned selected[4], eligible;
    CHECK(af_local_shadow_select(lights, 8, &camera, 1, selected, &eligible) == 4 && eligible == 4 && selected[0] == 4);
    lights[0].position = (af_vec3){0, 0, -5};
    CHECK(af_local_shadow_select(lights, 8, &camera, 1, selected, &eligible) == 4 && eligible == 5 &&
          selected[0] == 0 && selected[3] == 6);
    lights[0].position = (af_vec3){3, 0, -5};
    lights[0].range = 5;
    CHECK(af_local_shadow_select(lights, 1, &camera, 1, selected, &eligible) ==
          1); /* center outside, range intersects */
    CHECK(af_local_shadow_select(lights, 0, &camera, 1, selected, &eligible) == 0 && eligible == 0);
    puts("Shadow budget: frustum ranges, disabled/zero lights, stable influence priority and bounded overflow passed.");
    return 0;
}

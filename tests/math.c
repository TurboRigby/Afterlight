/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/math.h>
#include <afterlight/camera.h>
#include <math.h>
#include <stdio.h>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static int near(float a, float b) {
    return fabsf(a - b) < 0.00001f;
}

int main(void) {
    af_vec3 x = {1, 0, 0}, y = {0, 1, 0};
    af_vec3 cross = af_vec3_cross(x, y);
    CHECK(near(cross.x, 0) && near(cross.y, 0) && near(cross.z, 1));
    CHECK(near(af_vec3_dot(x, y), 0));
    af_vec3 normal = af_vec3_normalize((af_vec3){3, 4, 0});
    CHECK(near(normal.x, 0.6f) && near(normal.y, 0.8f));
    CHECK(near(af_vec3_length(af_vec3_normalize((af_vec3){0})), 0));
    CHECK(near(af_vec3_length(af_vec3_normalize((af_vec3){NAN, 0, 0})), 0));
    af_mat4 model = af_mat4_mul(af_mat4_translation((af_vec3){2, 3, 4}), af_mat4_scale((af_vec3){2, 3, 4}));
    af_vec4 point = af_mat4_transform(model, (af_vec4){1, 1, 1, 1});
    CHECK(near(point.x, 4) && near(point.y, 6) && near(point.z, 8) && near(point.w, 1));
    af_vec4 direction = af_mat4_transform(model, (af_vec4){1, 0, 0, 0});
    CHECK(near(direction.x, 2) && near(direction.y, 0) && near(direction.z, 0));
    af_vec4 rotated = af_mat4_transform(af_mat4_rotation_y(AF_PI / 2), (af_vec4){1, 0, 0, 1});
    CHECK(near(rotated.x, 0) && near(rotated.z, -1));
    rotated = af_mat4_transform(af_mat4_rotation_x(AF_PI / 2), (af_vec4){0, 1, 0, 1});
    CHECK(near(rotated.y, 0) && near(rotated.z, 1));
    af_mat4 view = af_mat4_look_at((af_vec3){2, 3, 4}, (af_vec3){2, 3, 3}, y);
    point = af_mat4_transform(view, (af_vec4){2, 3, 4, 1});
    CHECK(near(point.x, 0) && near(point.y, 0) && near(point.z, 0));
    point = af_mat4_transform(view, (af_vec4){2, 3, 3, 1});
    CHECK(near(point.z, -1));
    af_mat4 projection = af_mat4_perspective(AF_PI / 2, 2, 0.1f, 100);
    af_vec4 near_plane = af_mat4_transform(projection, (af_vec4){0, 0, -0.1f, 1});
    af_vec4 far_plane = af_mat4_transform(projection, (af_vec4){0, 0, -100, 1});
    CHECK(near(near_plane.z / near_plane.w, 0) && near(far_plane.z / far_plane.w, 1));
    point = af_mat4_transform(projection, (af_vec4){2, 1, -1, 1});
    CHECK(near(point.x / point.w, 1) && near(point.y / point.w, -1));
    af_mat4 invalid[] = {af_mat4_perspective(0, 1, 0.1f, 10), af_mat4_perspective(AF_PI, 1, 0.1f, 10),
                         af_mat4_perspective(1, 0, 0.1f, 10), af_mat4_perspective(1, 1, 0, 10),
                         af_mat4_perspective(1, 1, 1, 1),     af_mat4_perspective(NAN, 1, 0.1f, 10)};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
        for (int j = 0; j < 16; ++j)
            CHECK(invalid[i].elements[j] == 0);
    af_mat4 composed = af_mat4_mul(model, af_mat4_rotation_y(0.7f)), inverse;
    CHECK(af_mat4_inverse(composed, &inverse));
    af_mat4 identity = af_mat4_mul(composed, inverse);
    for (int i = 0; i < 16; ++i)
        CHECK(near(identity.elements[i], i % 5 == 0 ? 1.0f : 0.0f));
    CHECK(af_mat4_inverse(projection, &inverse));
    point = af_mat4_transform(inverse, af_mat4_transform(projection, (af_vec4){2, 1, -3, 1}));
    CHECK(near(point.x, 2) && near(point.y, 1) && near(point.z, -3) && near(point.w, 1));
    inverse = af_mat4_identity();
    CHECK(!af_mat4_inverse(af_mat4_scale((af_vec3){1, 0, 1}), &inverse));
    CHECK(inverse.elements[0] == 1 && inverse.elements[5] == 1);
    CHECK(!af_mat4_inverse((af_mat4){0}, &inverse));
    CHECK(!af_mat4_inverse((af_mat4){{NAN}}, &inverse));
    CHECK(!af_mat4_inverse(model, NULL));
    af_camera camera = af_camera_default();
    CHECK(af_camera_is_valid(&camera));
    af_vec3 forward = af_camera_forward(&camera);
    CHECK(near(forward.x, 0) && near(forward.y, 0) && near(forward.z, -1));
    af_camera_move(&camera, (af_vec3){1, 2, 1});
    CHECK(near(camera.position.x, 1) && near(camera.position.y, 2) && near(camera.position.z, 2));
    af_camera_rotate(&camera, AF_PI / 2, 0);
    forward = af_camera_forward(&camera);
    CHECK(near(forward.x, 1) && near(forward.z, 0));
    af_camera_move(&camera, (af_vec3){0, 0, 1});
    CHECK(near(camera.position.x, 2));
    point = af_mat4_transform(af_camera_view(&camera),
                              (af_vec4){camera.position.x, camera.position.y, camera.position.z, 1});
    CHECK(near(point.x, 0) && near(point.y, 0) && near(point.z, 0));
    af_camera_rotate(&camera, 0, 100);
    CHECK(af_camera_is_valid(&camera) && camera.pitch < AF_PI / 2 && camera.pitch > 1.5f);
    af_camera_rotate(&camera, 0, -200);
    CHECK(af_camera_is_valid(&camera) && camera.pitch > -AF_PI / 2 && camera.pitch < -1.5f);
    af_camera original = camera;
    af_camera_rotate(&camera, NAN, 0);
    af_camera_move(&camera, (af_vec3){INFINITY, 0, 0});
    CHECK(camera.yaw == original.yaw && camera.position.x == original.position.x);
    camera.near_clip = -1;
    CHECK(!af_camera_is_valid(&camera));
    CHECK(!af_camera_is_valid(NULL));
    puts("Afterlight math: vectors, composition, rotations, view, Vulkan projection passed.");
    return 0;
}

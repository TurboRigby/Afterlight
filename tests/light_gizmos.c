/* SPDX-License-Identifier: MPL-2.0 */
#include "light_gizmo.h"
#include "document.h"
#include <math.h>
#include <stdio.h>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "Light gizmo %d: %s\n", __LINE__, #c);                                                     \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d));
    CHECK(ed_document_add_light(&d, AF_LIGHT_POINT));
    af_local_light light = *af_scene_local_light_at(d.scene, 0);
    light.position = (af_vec3){0, 0, 0};
    light.range = 4;
    CHECK(ed_document_local_light(&d, 0, light));
    ed_document_end_edit(&d);
    ed_line3 lines[ED_LIGHT_MAX_LINES];
    CHECK(ed_light_lines(light, lines, ED_LIGHT_MAX_LINES) == 96);
    for (unsigned i = 0; i < 96; ++i)
        CHECK(fabsf(af_vec3_length(af_vec3_sub(lines[i].a, light.position)) - 4) < .001f);
    af_camera camera = af_camera_default();
    af_rect viewport = {100, 50, 800, 600};
    af_vec2 screen;
    CHECK(ed_project(&camera, viewport, light.position, &screen));
    CHECK(ed_light_pick(d.scene, &camera, viewport, screen) == 0);
    CHECK(ed_light_pick(d.scene, &camera, viewport, (af_vec2){0, 0}) == -1);
    ed_gizmo gizmo = {.snap = true};
    af_transform initial = ed_light_transform(light);
    ed_gizmo_project(&gizmo, &camera, viewport, light.position, af_mat4_identity(), initial.rotation);
    CHECK(gizmo.projected);
    ed_gizmo_begin(&gizmo, 0, 0, initial, gizmo.origin);
    af_transform moved = ed_gizmo_drag(&gizmo, (af_vec2){gizmo.origin.x + 80, gizmo.origin.y});
    af_local_light next = ed_light_apply(light, moved, ED_GIZMO_MOVE, 0);
    CHECK(next.position.x != light.position.x && fabsf(next.position.x * 2 - roundf(next.position.x * 2)) < .001f);
    CHECK(ed_document_local_light(&d, 0, next));
    next.position.x += .5f;
    CHECK(ed_document_local_light(&d, 0, next));
    ed_document_end_edit(&d);
    CHECK(ed_document_undo(&d) && af_scene_local_light_at(d.scene, 0)->position.x == 0);
    CHECK(ed_document_redo(&d) && af_scene_local_light_at(d.scene, 0)->position.x == next.position.x);
    light.type = AF_LIGHT_SPOT;
    light.direction = (af_vec3){0, -1, 0};
    CHECK(ed_light_lines(light, lines, ED_LIGHT_MAX_LINES) == 73);
    initial = ed_light_transform(light);
    next = ed_light_apply(light, initial, ED_GIZMO_ROTATE, 0);
    CHECK(af_vec3_length(af_vec3_sub(light.direction, next.direction)) < .0001f);
    initial.rotation = (af_vec3){0, AF_PI * .5f, 0};
    next = ed_light_apply(light, initial, ED_GIZMO_ROTATE, 1);
    CHECK(next.direction.x < -.99f);
    initial.scale = (af_vec3){3, 3, 3};
    next = ed_light_apply(light, initial, ED_GIZMO_SCALE, 1);
    CHECK(next.range == 3);
    af_render_settings settings = af_scene_get_render_settings(d.scene);
    settings.skybox = true;
    CHECK(ed_document_render_settings(&d, settings));
    ed_document_end_edit(&d);
    CHECK(af_scene_get_render_settings(d.scene).skybox);
    CHECK(ed_document_undo(&d) && !af_scene_get_render_settings(d.scene).skybox);
    CHECK(ed_document_redo(&d) && af_scene_get_render_settings(d.scene).skybox);
    ed_document_destroy(&d);
    puts("Light gizmos: point/spot geometry, picking, snapped move, normalized direction, range and grouped undo/redo "
         "passed.");
    return 0;
}

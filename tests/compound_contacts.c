/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/compound.h>
#include <afterlight/physics.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                        \
    do {                                                                                                \
        if (!(x)) {                                                                                     \
            fprintf(stderr, "Compound contacts %d: %s (%s)\n", __LINE__, #x, af_physics_error(p));      \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)
static af_object_id prop(af_scene *s) {
    af_object_id id = af_scene_add_object(s, "Offset two-part body", "__node");
    af_collider_part part = af_collider_part_default();
    part.collider.half_extents = (af_vec3){.4f, .4f, .4f};
    if (!af_scene_add_collider_part(s, id, part))
        return 0;
    part.local.position.x = 1;
    if (!af_scene_add_collider_part(s, id, part))
        return 0;
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_COMPOUND;
    if (!af_scene_set_object_collider(s, id, c))
        return 0;
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    b.mass = 4;
    b.gravity_scale = 0;
    if (!af_scene_set_body(s, id, b))
        return 0;
    return id;
}
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    af_object_id id = prop(s);
    CHECK(id && af_physics_step(p, s, 1.f / 60));
    af_physics_state state = af_physics_get_state(p, id);
    CHECK(fabsf(state.position.x - .5f) < .0001f &&
          fabsf(af_scene_get_object(s, id)->transform.position.x) < .0001f);
    CHECK(af_physics_apply_impulse(p, s, id, (af_vec3){0, 0, 1},
                                   af_vec3_add(state.position, (af_vec3){0, 1, 0})));
    for (unsigned i = 0; i < 120; ++i) {
        CHECK(af_physics_step(p, s, 1.f / 60));
        af_world_collider root;
        CHECK(af_scene_world_collider(s, id, &root) &&
              af_vec3_length(af_vec3_sub(root.center, af_physics_get_state(p, id).position)) < .001f);
    }
    af_scene_destroy(s);
    af_physics_reset(p);
    s = af_scene_create();
    af_object_id floor = af_scene_add_object(s, "Floor", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = (af_vec3){5, .5f, 5};
    af_transform t = af_transform_identity();
    t.position.y = -.5f;
    CHECK(af_scene_set_object_transform(s, floor, t) && af_scene_set_object_collider(s, floor, c));
    id = prop(s);
    t = af_transform_identity();
    t.position.y = 3;
    CHECK(af_scene_set_object_transform(s, id, t));
    af_rigid_body body = af_scene_get_object(s, id)->body;
    body.gravity_scale = 1;
    CHECK(af_scene_set_body(s, id, body));
    for (unsigned i = 0; i < 360; ++i)
        CHECK(af_physics_step(p, s, 1.f / 60));
    CHECK(af_vec3_length(af_physics_get_state(p, id).velocity) < .2f);
    for (size_t i = 0; i < 2; ++i) {
        af_world_collider w;
        CHECK(af_scene_collider_shape_at(s, id, i, &w));
        float extent = fabsf(w.axes[0].y) * w.half_extents.x + fabsf(w.axes[1].y) * w.half_extents.y +
                       fabsf(w.axes[2].y) * w.half_extents.z;
        CHECK(w.center.y - extent > -.035f);
    }
    af_physics_contact contacts[32];
    size_t count = af_physics_contacts(p, contacts, 32);
    CHECK(count > 0);
    bool part = false;
    for (size_t i = 0; i < count && i < 32; ++i)
        part |= contacts[i].part_a || contacts[i].part_b;
    CHECK(part);
    af_character character = {.position = {.5f, .4f, -.6f}};
    CHECK(af_physics_push_character(p, s, 0, &character, af_character_config_default(),
                                    (af_vec3){0, 0, 2}, 120, 1.f / 60) == 1);
    state = af_physics_get_state(p, id);
    CHECK(af_vec3_length(state.velocity) < .51f);
    af_scene_destroy(s);
    af_physics_destroy(p);
    puts("Compound bodies: COM/pivot separation, full angular inertia, per-part contacts and stable "
         "floor rest passed.");
    return 0;
}

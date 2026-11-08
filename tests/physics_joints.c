/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/joint.h>
#include <afterlight/physics.h>
#include <afterlight/prefab.h>
#include <math.h>
#include <stdio.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Joint %d: %s\n", __LINE__, #x);                                                           \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id ball(af_scene *s, af_vec3 pos) {
    af_object_id id = af_scene_add_object(s, "Ball", "cube");
    af_transform t = af_transform_identity();
    t.position = pos;
    af_scene_set_object_transform(s, id, t);
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_SPHERE;
    c.radius = .2f;
    af_scene_set_object_collider(s, id, c);
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = AF_BODY_DYNAMIC;
    af_scene_set_body(s, id, b);
    return id;
}
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    af_object_id a = ball(s, (af_vec3){1, 2, 0}), b = ball(s, (af_vec3){1, 0, 0});
    af_joint j = af_joint_default();
    j.a = a;
    j.anchor_b = (af_vec3){0, 4, 0};
    j.distance = sqrtf(5);
    af_joint_id first = af_scene_add_joint(s, j);
    CHECK(first);
    j.a = b;
    j.b = a;
    j.anchor_b = (af_vec3){0};
    j.distance = 2;
    CHECK(af_scene_add_joint(s, j));
    j.b = j.a;
    CHECK(!af_scene_add_joint(s, j));
    for (unsigned i = 0; i < 360; ++i) {
        CHECK(af_physics_step(p, s, 1.f / 60));
        af_vec3 x = af_physics_get_state(p, a).position, y = af_physics_get_state(p, b).position;
        CHECK(isfinite(y.y) && fabsf(af_vec3_length(af_vec3_sub(x, (af_vec3){0, 4, 0})) - sqrtf(5)) < .04f);
        CHECK(fabsf(af_vec3_length(af_vec3_sub(x, y)) - 2) < .04f);
    }
    af_scene_error error;
    CHECK(af_scene_save(s, "physics-joints.scene", &error));
    af_scene *loaded = af_scene_load("physics-joints.scene", &error);
    if (!loaded)
        fprintf(stderr, "%s\n", error.message);
    CHECK(loaded && af_scene_joint_count(loaded) == 2 && af_scene_get_joint(loaded, first));
    af_scene *clone = af_scene_clone(s);
    CHECK(clone && af_scene_joint_count(clone) == 2);
    CHECK(af_scene_remove_object(clone, a) && af_scene_joint_count(clone) == 0);
    af_scene_destroy(clone);
    CHECK(!af_prefab_save(s, a, NULL, "bad-joint.prefab", &error));

    af_rigid_body disabled = af_rigid_body_default();
    CHECK(!af_scene_set_body(s, a, disabled));
    af_scene *pref = af_scene_create();
    af_object_id root = ball(pref, (af_vec3){0}), child = af_scene_add_object(pref, "Static child", "cube");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    CHECK(af_scene_set_object_collider(pref, child, c) && af_scene_set_parent(pref, child, root));
    j = af_joint_default();
    j.a = root;
    j.b = child;
    CHECK(af_scene_add_joint(pref, j) && af_prefab_save(pref, root, NULL, "joint.prefab", &error));
    af_object_id instance;
    CHECK(af_prefab_instantiate(pref, "joint.prefab", 0, af_transform_identity(), &instance, &error));
    CHECK(af_scene_joint_count(pref) == 2);
    const af_joint *remapped = af_scene_joint_at(pref, 1);
    CHECK(remapped->a == instance && remapped->b != child &&
          af_scene_get_object(pref, remapped->b)->parent == instance);
    af_scene_destroy(pref);
    remove("joint.prefab");
    CHECK(af_scene_remove_joint(s, first));
    CHECK(af_scene_remove_object(s, a) && af_scene_joint_count(s) == 0);
    af_scene *filtered = af_scene_create();
    af_object_id fa = ball(filtered, (af_vec3){0}), fb = ball(filtered, (af_vec3){0});
    af_joint link = af_joint_default();
    link.a = fa;
    link.b = fb;
    link.distance = 0;
    af_joint_id fid = af_scene_add_joint(filtered, link);
    CHECK(fid);
    af_physics_reset(p);
    CHECK(af_physics_step(p, filtered, 1.f / 60) && af_physics_get_stats(p).contacts == 0);
    link.collide_connected = true;
    CHECK(af_scene_set_joint(filtered, fid, link));
    CHECK(af_physics_step(p, filtered, 1.f / 60) && af_physics_get_stats(p).contacts > 0);
    af_scene_destroy(filtered);
    af_scene_destroy(loaded);
    af_scene_destroy(s);
    af_physics_destroy(p);
    remove("physics-joints.scene");
    puts("Distance joints: pendulum, linked bodies, save/load, clone, removal and external prefab "
         "rejection passed.");
    return 0;
}

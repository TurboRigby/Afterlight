/* SPDX-License-Identifier: MPL-2.0 */

#include <afterlight/physics.h>
#include <stdio.h>
int main(void) {
    af_scene *s = af_scene_create();
    af_physics *p = af_physics_create();
    for (unsigned i = 0; i < 256; ++i) {
        af_object_id id = af_scene_add_object(s, "Sparse CCD sphere", "cube");
        af_collider c = af_collider_default();
        c.type = AF_COLLIDER_SPHERE;
        c.radius = .2f;
        af_transform t = af_transform_identity();
        t.position.z = (float)i * 3;
        af_rigid_body b = af_rigid_body_default();
        b.enabled = true;
        b.type = AF_BODY_DYNAMIC;
        b.gravity_scale = 0;
        b.continuous = true;
        b.velocity.x = .1f;
        if (!af_scene_set_object_transform(s, id, t) || !af_scene_set_object_collider(s, id, c) ||
            !af_scene_set_body(s, id, b))
            return 1;
    }
    if (!af_physics_step(p, s, 1.f / 60))
        return 1;
    af_physics_stats stats = af_physics_get_stats(p);
    bool ok = stats.ccd_pairs == 0 && stats.broadphase_pairs == 0 && stats.dynamic_bodies == 256;
    af_scene_destroy(s);
    af_physics_destroy(p);
    puts("Adaptive broadphase: separated 256-body CCD scene avoids all narrowphase pairs.");
    return ok ? 0 : 1;
}

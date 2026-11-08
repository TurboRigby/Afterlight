/* SPDX-License-Identifier: MPL-2.0 */

#include "render_test.h"
#include <afterlight/collision_mesh.h>
#include <math.h>
#include <stdio.h>
typedef struct state {
    af_scene *scene;
    bool failed;
} state;
static bool source(float y, bool bad) {
    FILE *f = fopen("reload-room.obj", "wb");
    if (!f)
        return false;
    bool ok = fprintf(f, "v -4 %g -4\nv 4 %g -4\nv 4 %g 4\nv -4 %g 4\nf 1 2 3\nf 1 %u 4\n", (double)y, (double)y,
                      (double)y, (double)y, bad ? 1u : 3u) > 0;
    return fclose(f) == 0 && ok;
}
static bool init(af_engine *e, void *memory) {
    state *s = memory;
    if (!source(0, false))
        return false;
    s->scene = af_scene_create();
    af_object_id id = af_scene_add_object(s->scene, "Reload mesh", "room");
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_MESH;
    if (!af_scene_set_object_collider(s->scene, id, c) ||
        !af_scene_set_asset(s->scene, "room", AF_ASSET_MESH, "reload-room.obj"))
        return false;
    af_asset_error error;
    if (!af_engine_load_scene_assets(e, s->scene, &error))
        return false;
    af_engine_set_scene(e, s->scene);
    return true;
}
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Mesh reload %d: %s\n", __LINE__, #x);                                                     \
            s->failed = true;                                                                                          \
            af_engine_request_quit(e);                                                                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)
static void update(af_engine *e, double dt, void *memory) {
    (void)dt;
    state *s = memory;
    af_collision_hit hit;
    af_query_filter filter = {.mask = UINT32_MAX};
    CHECK(af_scene_raycast(s->scene, (af_vec3){0, 3, 0}, (af_vec3){0, -1, 0}, 5, filter, &hit) &&
          fabsf(hit.point.y) < .001f);
    af_asset_error error;
    size_t meshes = af_engine_get_stats(e).loaded_meshes;
    CHECK(source(1, false) && af_engine_reload_asset_source(e, s->scene, "reload-room.obj", &error));
    CHECK(af_scene_raycast(s->scene, (af_vec3){0, 3, 0}, (af_vec3){0, -1, 0}, 5, filter, &hit) &&
          fabsf(hit.point.y - 1) < .001f);
    const af_collision_mesh *good = af_scene_collision_mesh(s->scene, "room");
    FILE *f = fopen("reload-room.obj", "wb");
    CHECK(f && fputs("v 0 0 0\nf 1 1 1\n", f) >= 0 && fclose(f) == 0);
    CHECK(!af_engine_reload_asset_source(e, s->scene, "reload-room.obj", &error) &&
          af_scene_collision_mesh(s->scene, "room") == good);
    CHECK(af_engine_get_stats(e).loaded_meshes == meshes);
    CHECK(source(2, false) && af_engine_reload_asset_source(e, s->scene, "reload-room.obj", &error));
    CHECK(af_scene_raycast(s->scene, (af_vec3){0, 3, 0}, (af_vec3){0, -1, 0}, 5, filter, &hit) &&
          fabsf(hit.point.y - 2) < .001f);
    af_engine_request_quit(e);
}
static void shutdown(af_engine *e, void *memory) {
    state *s = memory;
    af_engine_set_scene(e, NULL);
    af_scene_destroy(s->scene);
    remove("reload-room.obj");
}
int main(void) {
    state s = {0};
    af_engine_config c = af_engine_config_default();
    c.title = "Afterlight mesh reload transaction";
    c.enable_validation = true;
    af_engine *e = af_engine_create(&c);
    if (!e)
        return 1;
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &s};
    int result = af_engine_run(e, &game);
    af_engine_stats stats = af_engine_get_stats(e);
    af_engine_destroy(e);
    return result || s.failed || stats.validation_errors ? 1 : 0;
}

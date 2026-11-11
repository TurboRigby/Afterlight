/* SPDX-License-Identifier: MPL-2.0 */
#include "cJSON.h"
#include "document/document.h"
#include <afterlight/physics.h>
#include <afterlight/water.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "Wind %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
static af_object_id box(af_scene *s, const char *name, af_vec3 position, float mass) {
    af_object_id id = af_scene_add_object(s, name, "cube");
    af_transform t = af_transform_identity();
    t.position = position;
    af_collider c = af_collider_default();
    c.type = AF_COLLIDER_BOX;
    c.half_extents = (af_vec3){.5f, .5f, .5f};
    af_rigid_body b = af_rigid_body_default();
    b.enabled = true;
    b.type = mass > 0 ? AF_BODY_DYNAMIC : AF_BODY_STATIC;
    b.mass = mass > 0 ? mass : 1;
    b.gravity_scale = 0;
    b.linear_damping = b.angular_damping = 0;
    b.allow_sleep = false;
    return af_scene_set_object_transform(s, id, t) && af_scene_set_object_collider(s, id, c) &&
                   af_scene_set_body(s, id, b)
               ? id
               : 0;
}
static bool advance(af_physics *p, af_scene *s, unsigned ticks, float dt) {
    for (unsigned i = 0; i < ticks; ++i)
        if (!af_physics_step(p, s, dt))
            return false;
    return true;
}
static bool rejects(cJSON *json) {
    char *text = cJSON_PrintUnformatted(json);
    FILE *f = fopen("wind-invalid.json", "wb");
    bool wrote = text && f && fputs(text, f) >= 0;
    if (f)
        fclose(f);
    cJSON_free(text);
    af_scene *s = wrote ? af_scene_load("wind-invalid.json", NULL) : NULL;
    bool rejected = wrote && !s;
    af_scene_destroy(s);
    cJSON_Delete(json);
    return rejected;
}
int main(void) {
    ed_document d;
    CHECK(ed_document_init(&d) && ed_document_new(&d));
    af_wind wind = af_wind_default();
    CHECK(!wind.enabled && af_wind_valid(wind));
    wind.enabled = true;
    wind.speed = 10;
    wind.gust_frequency = 0;
    CHECK(ed_document_wind(&d, wind));
    ed_document_end_edit(&d);
    CHECK(ed_document_undo(&d) && !af_scene_get_wind(d.scene).enabled);
    CHECK(ed_document_redo(&d) && af_scene_get_wind(d.scene).enabled);
    af_wind bad = wind;
    bad.direction = (af_vec3){0};
    CHECK(!af_scene_set_wind(d.scene, bad));
    bad = wind;
    bad.speed = NAN;
    CHECK(!af_wind_valid(bad));
    bad = wind;
    bad.direction.y = 1;
    CHECK(!af_wind_valid(bad));
    bad = wind;
    bad.gust_frequency = 3;
    CHECK(!af_wind_valid(bad));
    af_object_id owner = af_scene_add_object(d.scene, "Water", "__node");
    af_water water = af_water_default();
    water.ripple_strength = .43f;
    water.ripple_scale = .7f;
    CHECK(af_scene_set_water(d.scene, owner, water));
    CHECK(af_scene_save(d.scene, "wind.json", NULL));
    af_scene *loaded = af_scene_load("wind.json", NULL);
    CHECK(loaded && af_scene_get_wind(loaded).speed == 10 &&
          fabsf(af_scene_get_water(loaded, owner)->ripple_strength - .43f) < 1e-5f);
    af_scene_destroy(loaded);
    FILE *f = fopen("wind.json", "rb");
    CHECK(f);
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *text = malloc((size_t)size + 1);
    CHECK(text && fread(text, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    text[size] = 0;
    cJSON *root = cJSON_Parse(text);
    free(text);
    CHECK(root);
    cJSON *j = cJSON_Duplicate(root, 1);
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "wind"), "speed");
    CHECK(rejects(j));
    j = cJSON_Duplicate(root, 1);
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "wind"), "speed"), 61);
    CHECK(rejects(j));
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "version"), 21);
    cJSON_DeleteItemFromObjectCaseSensitive(root, "wind");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "landscape");
    cJSON_DeleteItemFromObjectCaseSensitive(root, "blueprints");
    cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "light"), "angular_radius");
    cJSON *legacy_render = cJSON_GetObjectItemCaseSensitive(root, "render");
    cJSON_DeleteItemFromObjectCaseSensitive(legacy_render, "ray_traced_material_effects");
    cJSON_DeleteItemFromObjectCaseSensitive(legacy_render, "ray_traced_shadows");
    cJSON_DeleteItemFromObjectCaseSensitive(legacy_render, "ray_traced_water_reflections");
    cJSON_DeleteItemFromObjectCaseSensitive(legacy_render, "anti_aliasing");
    cJSON_DeleteItemFromObjectCaseSensitive(legacy_render, "aa_sharpness");
    cJSON_DeleteItemFromObjectCaseSensitive(legacy_render, "taa_history_weight");
    cJSON *w = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "water"), 0);
    cJSON_DeleteItemFromObjectCaseSensitive(w, "ripple_scale");
    cJSON_DeleteItemFromObjectCaseSensitive(w, "ripple_strength");
    cJSON *legacy_object;
    cJSON_ArrayForEach(legacy_object, cJSON_GetObjectItemCaseSensitive(root, "objects")) {
        cJSON_DeleteItemFromObjectCaseSensitive(cJSON_GetObjectItemCaseSensitive(legacy_object, "material"), "ior");
    }
    char *legacy = cJSON_Print(root);
    f = fopen("wind-legacy.json", "wb");
    CHECK(f && legacy && fputs(legacy, f) >= 0 && fclose(f) == 0);
    cJSON_free(legacy);
    cJSON_Delete(root);
    loaded = af_scene_load("wind-legacy.json", NULL);
    CHECK(loaded && !af_scene_get_wind(loaded).enabled && af_scene_get_water(loaded, owner)->ripple_scale == 1);
    af_scene_destroy(loaded);
    ed_document_destroy(&d);
    wind.gust_frequency = .5f;
    wind.gust_strength = 1;
    CHECK(af_vec3_length(af_wind_velocity(wind, .3)) != af_vec3_length(af_wind_velocity(wind, 1.2)));
    CHECK(af_vec3_length(af_vec3_sub(af_wind_velocity(wind, .3), af_wind_velocity(wind, .3))) == 0);
    wind.gust_frequency = 0;
    float velocities[3];
    unsigned rates[] = {30, 60, 144};
    for (unsigned r = 0; r < 3; ++r) {
        af_scene *s = af_scene_create();
        af_physics *p = af_physics_create();
        CHECK(s && p && af_scene_set_wind(s, wind));
        af_object_id id = box(s, "Exposed", (af_vec3){0}, 1);
        CHECK(id && advance(p, s, rates[r] * 3, 1.f / (float)rates[r]));
        velocities[r] = af_physics_get_state(p, id).velocity.x;
        CHECK(velocities[r] > 8 && velocities[r] < 10 && fabs(af_physics_time(p) - 3) < .001);
        wind.direction.x = -1;
        CHECK(af_scene_set_wind(s, wind) && advance(p, s, rates[r] * 3, 1.f / (float)rates[r]) &&
              af_physics_get_state(p, id).velocity.x < 0);
        wind.direction.x = 1;
        af_physics_destroy(p);
        af_scene_destroy(s);
    }
    CHECK(fabsf(velocities[0] - velocities[1]) < .03f && fabsf(velocities[1] - velocities[2]) < .03f);
    for (unsigned kind = 0; kind < 4; ++kind) {
        af_scene *s = af_scene_create();
        af_physics *p = af_physics_create();
        CHECK(s && p && af_scene_set_wind(s, wind));
        af_object_id id = box(s, "Prop", (af_vec3){0, kind == 2 ? -1.5f : 0, 0}, kind == 3 ? 100 : 1);
        CHECK(id);
        if (kind == 0)
            CHECK(box(s, "Shelter", (af_vec3){-2, 0, 0}, 0));
        if (kind == 1 || kind == 2) {
            owner = af_scene_add_object(s, "Pool", "__node");
            water = af_water_default();
            water.density = .01f;
            water.drag = 0;
            water.wind = 0;
            water.depth = 5;
            CHECK(af_scene_set_water(s, owner, water));
        }
        CHECK(advance(p, s, 60, 1.f / 60));
        af_physics_state state = af_physics_get_state(p, id);
        if (kind == 0 || kind == 2)
            CHECK(fabsf(state.velocity.x) < .001f);
        if (kind == 1)
            CHECK(state.velocity.x > 0 && state.angular_velocity.z < 0);
        if (kind == 3)
            CHECK(state.velocity.x > 0 && state.velocity.x < 2);
        if (kind == 1)
            CHECK(af_physics_water_stats(p).max_wave > .0001f);
        af_physics_destroy(p);
        af_scene_destroy(s);
    }
    {
        af_scene *s = af_scene_create();
        af_physics *p = af_physics_create();
        CHECK(s && p);
        CHECK(box(s, "Floor", (af_vec3){0, -.5f, 0}, 0));
        af_object_id id = box(s, "Sleeper", (af_vec3){0, .5f, 0}, 1);
        CHECK(id);
        af_rigid_body body = af_scene_get_object(s, id)->body;
        body.gravity_scale = 1;
        body.allow_sleep = true;
        CHECK(af_scene_set_body(s, id, body) && advance(p, s, 180, 1.f / 60) && af_physics_get_state(p, id).sleeping);
        CHECK(af_scene_set_wind(s, wind) && advance(p, s, 30, 1.f / 60));
        CHECK(!af_physics_get_state(p, id).sleeping && af_physics_get_state(p, id).velocity.x > .1f);
        af_physics_destroy(p);
        af_scene_destroy(s);
    }
    remove("wind.json");
    remove("wind-invalid.json");
    remove("wind-legacy.json");
    puts("Wind: limits, persistence/legacy/history, gusts, mass, 30/60/144 Hz, "
         "reversal, shelter, submersion, wake, water waves and partial-exposure "
         "torque passed.");
    return 0;
}

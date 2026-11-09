/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/scene.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                                               \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #condition);                            \
            return 1;                                                                                                  \
        }                                                                                                              \
    } while (0)
#define FIELDS                                                                                                         \
    "\"name\":\"Object\",\"mesh\":\"cube\",\"visible\":true,\"position\":[0,0,0],\"rotation\":[0,0,0],\"scale\":[1,1," \
    "1]"

static bool write_bytes(const char *path, const char *text, size_t length) {
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    bool result = fwrite(text, 1, length, file) == length;
    return fclose(file) == 0 && result;
}
static bool write_text(const char *path, const char *text) {
    return write_bytes(path, text, strlen(text));
}

static int reject_text(const char *text, af_scene_error_code expected) {
    CHECK(write_text("invalid.json", text));
    af_scene_error error;
    af_scene *scene = af_scene_load("invalid.json", &error);
    if (scene) {
        af_scene_destroy(scene);
        fprintf(stderr, "Accepted invalid scene: %s\n", text);
        return 1;
    }
    CHECK(error.code == expected && error.message[0]);
    return 0;
}
static int reject_objects(const char *objects) {
    char text[4096];
    snprintf(text, sizeof(text), "{\"format\":\"afterlight.scene\",\"version\":1,\"next_id\":\"5\",\"objects\":%s}",
             objects);
    return reject_text(text, AF_SCENE_ERROR_FORMAT);
}
static bool same_vector(af_vec3 a, af_vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

int main(int argc, char **argv) {
    af_scene_error error;
    CHECK(argc == 2);
    af_scene *legacy = af_scene_load(argv[1], &error);
    CHECK(legacy && af_scene_get_shadows(legacy).mode == AF_SHADOW_CASCADED);
    CHECK(af_scene_get_render_settings(legacy).rtgi_bounces == 1);
    CHECK(af_scene_get_render_settings(legacy).rtgi_emissive);
    CHECK(af_scene_get_light(legacy).angular_radius == af_directional_light_default().angular_radius);
    CHECK(af_scene_object_at(legacy, 0)->material.cast_shadows &&
          af_scene_object_at(legacy, 0)->material.receive_shadows);
    af_scene_destroy(legacy);
    af_scene *scene = af_scene_create();
    CHECK(scene && af_scene_save(scene, "empty.json", &error) && error.code == AF_SCENE_ERROR_NONE);
    af_scene *copy = af_scene_load("empty.json", &error);
    CHECK(copy && af_scene_object_count(copy) == 0 && af_scene_add_object(copy, "First", "cube") == 1);
    af_scene_destroy(copy);
    const char *special = "quote\" slash\\ line\nУкраїнська literal\\u0000";
    af_object_id first = af_scene_add_object(scene, special, "unloaded/mesh");
    CHECK(first == 1);
    af_transform transform = af_transform_identity();
    transform.position = (af_vec3){FLT_MAX, FLT_TRUE_MIN, -123.4567f};
    transform.rotation = (af_vec3){0.1234567f, -3.14159f, 1e-20f};
    transform.scale = (af_vec3){0, -2, 3.5f};
    CHECK(af_scene_set_object_transform(scene, first, transform));
    CHECK(af_scene_set_object_visible(scene, first, false));
    af_material material = af_material_default();
    material.base_color = (af_color){0.2f, 0.3f, 0.4f, 1};
    material.ior = 1.52f;
    material.unlit = true;
    material.cast_shadows = false;
    material.receive_shadows = false;
    snprintf(material.texture, sizeof(material.texture), "checker");
    CHECK(af_scene_set_object_material(scene, first, material));
    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.mode = AF_SHADOW_CASCADED;
    shadows.resolution = 1024;
    shadows.distance = 120;
    shadows.filter_radius = 2;
    shadows.split_lambda = 0.7f;
    shadows.blend_fraction = 0.15f;
    shadows.debug_cascades = true;
    CHECK(af_scene_set_shadows(scene, shadows));
    af_directional_light light = af_directional_light_default();
    light.intensity = 2;
    light.ambient = 0.35f;
    light.angular_radius = 0.025f;
    CHECK(af_scene_set_light(scene, light));
    af_render_settings render_settings = af_scene_get_render_settings(scene);
    render_settings.ray_traced_material_effects = true;
    render_settings.ray_traced_gi = true;
    render_settings.rtgi_bounces = 3;
    render_settings.rtgi_emissive = false;
    CHECK(af_scene_set_render_settings(scene, render_settings));
    for (unsigned i = 0; i < 256; ++i)
        CHECK(af_scene_add_object(scene, "Shared object", "unloaded/mesh"));
    af_object_id removed = af_scene_add_object(scene, "Deleted highest ID", "cube");
    CHECK(af_scene_remove_object(scene, removed));
    CHECK(af_scene_save(scene, "roundtrip.json", &error));
    /* A second save replaces an existing file through the atomic path. */
    CHECK(af_scene_save(scene, "roundtrip.json", &error));
    copy = af_scene_load("roundtrip.json", &error);
    CHECK(copy && error.code == AF_SCENE_ERROR_NONE);
    CHECK(af_scene_object_count(scene) == af_scene_object_count(copy));
    for (size_t i = 0; i < af_scene_object_count(scene); ++i) {
        const af_scene_object *a = af_scene_object_at(scene, i), *b = af_scene_object_at(copy, i);
        CHECK(a->id == b->id && a->visible == b->visible);
        CHECK(strcmp(a->name, b->name) == 0 && strcmp(a->mesh, b->mesh) == 0);
        CHECK(same_vector(a->transform.position, b->transform.position));
        CHECK(same_vector(a->transform.rotation, b->transform.rotation));
        CHECK(same_vector(a->transform.scale, b->transform.scale));
        CHECK(a->material.unlit == b->material.unlit && !strcmp(a->material.texture, b->material.texture));
        CHECK(a->material.base_color.g == b->material.base_color.g && a->material.ior == b->material.ior &&
              a->material.cast_shadows == b->material.cast_shadows &&
              a->material.receive_shadows == b->material.receive_shadows);
    }
    af_shadow_settings restored = af_scene_get_shadows(copy);
    CHECK(restored.mode == AF_SHADOW_CASCADED && restored.resolution == 1024 && restored.distance == 120 &&
          restored.filter_radius == 2 && restored.split_lambda == 0.7f && restored.blend_fraction == 0.15f &&
          restored.debug_cascades);
    CHECK(af_scene_get_light(copy).intensity == 2 && af_scene_get_light(copy).ambient == 0.35f &&
          af_scene_get_light(copy).angular_radius == 0.025f);
    CHECK(af_scene_get_render_settings(copy).ray_traced_material_effects);
    CHECK(af_scene_get_render_settings(copy).rtgi_bounces == 3);
    CHECK(!af_scene_get_render_settings(copy).rtgi_emissive);
    af_render_settings bounce_limits = af_render_settings_default();
    bounce_limits.rtgi_bounces = 0;
    CHECK(af_scene_set_render_settings(copy, bounce_limits));
    CHECK(af_scene_get_render_settings(copy).rtgi_bounces == 1);
    bounce_limits.rtgi_bounces = 9;
    CHECK(af_scene_set_render_settings(copy, bounce_limits));
    CHECK(af_scene_get_render_settings(copy).rtgi_bounces == 4);
    af_directional_light invalid_sun = af_directional_light_default();
    invalid_sun.angular_radius = NAN;
    CHECK(!af_scene_set_light(copy, invalid_sun));
    invalid_sun = af_directional_light_default();
    invalid_sun.angular_radius = 0.3f;
    CHECK(!af_scene_set_light(copy, invalid_sun));
    CHECK(!af_scene_get_object(copy, removed));
    CHECK(af_scene_add_object(copy, "New ID", "cube") == removed + 1);
    af_scene_destroy(copy);
    /* Failed serialization must leave the previous saved file intact. */
    CHECK(af_scene_set_object_name(scene, first, "\xc0\xaf"));
    CHECK(!af_scene_save(scene, "roundtrip.json", &error) && error.code == AF_SCENE_ERROR_FORMAT);
    copy = af_scene_load("roundtrip.json", &error);
    CHECK(copy && strcmp(af_scene_get_object(copy, first)->name, special) == 0);
    af_scene_destroy(copy);
    CHECK(af_scene_set_object_name(scene, first, special));
    CHECK(!af_scene_save(scene, "missing-parent/scene.json", &error) && error.code == AF_SCENE_ERROR_IO);
    CHECK(!af_scene_save(scene, ".", &error) && error.code == AF_SCENE_ERROR_IO);
    CHECK(!af_scene_save(NULL, "invalid.json", &error) && error.code == AF_SCENE_ERROR_ARGUMENT);
    CHECK(!af_scene_load(NULL, &error) && error.code == AF_SCENE_ERROR_ARGUMENT);
    CHECK(!af_scene_load("no-such-scene.json", &error) && error.code == AF_SCENE_ERROR_IO);
    const char *v3_bad =
        "{\"format\":\"afterlight.scene\",\"version\":3,\"next_id\":\"1\",\"objects\":[],\"assets\":[],\"light\":{"
        "\"direction\":[0,-1,0],\"color\":[1,1,1],\"intensity\":1,\"ambient\":0.2},\"shadows\":{\"enabled\":true,"
        "\"mode\":\"cascaded\",\"resolution\":513,\"distance\":40,\"constant_bias\":1,\"slope_bias\":1,\"normal_bias\":"
        "0.01,\"filter_radius\":1,\"split_lambda\":0.65,\"blend_fraction\":0.1,\"debug_cascades\":false}}";
    CHECK(reject_text(v3_bad, AF_SCENE_ERROR_FORMAT) == 0);
    CHECK(reject_text("{}", AF_SCENE_ERROR_FORMAT) == 0);
    CHECK(reject_text("{", AF_SCENE_ERROR_FORMAT) == 0);
    CHECK(reject_text("[]", AF_SCENE_ERROR_FORMAT) == 0);
    CHECK(reject_text("{\"format\":\"afterlight.scene\",\"version\":99,\"next_id\":\"1\",\"objects\":[]}",
                      AF_SCENE_ERROR_VERSION) == 0);
    CHECK(reject_text("{\"format\":\"afterlight.scene\",\"version\":1,\"version\":1,\"next_id\":\"1\",\"objects\":[]}",
                      AF_SCENE_ERROR_FORMAT) == 0);
    CHECK(reject_text("{\"format\":\"afterlight.scene\",\"version\":1,\"next_id\":\"1\",\"objects\":[],\"unknown\":0}",
                      AF_SCENE_ERROR_FORMAT) == 0);
    CHECK(reject_text("{\"format\":\"afterlight.scene\",\"version\":1,\"next_id\":\"1\",\"objects\":[]} trailing",
                      AF_SCENE_ERROR_FORMAT) == 0);
    CHECK(reject_objects("[{\"id\":\"0\"," FIELDS "}]") == 0);
    CHECK(reject_objects("[{\"id\":\"5\"," FIELDS "}]") == 0);
    CHECK(reject_objects("[{\"id\":1," FIELDS "}]") == 0);
    CHECK(reject_objects("[{\"id\":\"01\"," FIELDS "}]") == 0);
    CHECK(reject_objects("[{\"id\":\"-1\"," FIELDS "}]") == 0);
    CHECK(reject_objects("[{\"id\":\"18446744073709551616\"," FIELDS "}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\"," FIELDS "},{\"id\":\"1\"," FIELDS "}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\"," FIELDS ",\"visible\":false}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\",\"name\":\"Bad\\u0000suffix\",\"mesh\":\"cube\",\"visible\":true,\"position\":"
                         "[0,0,0],\"rotation\":[0,0,0],\"scale\":[1,1,1]}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\",\"name\":\"Bad\nname\",\"mesh\":\"cube\",\"visible\":true,\"position\":[0,0,0]"
                         ",\"rotation\":[0,0,0],\"scale\":[1,1,1]}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\",\"name\":\"Bad\",\"mesh\":\"cube\",\"visible\":true,\"position\":[1e999,0,0],"
                         "\"rotation\":[0,0,0],\"scale\":[1,1,1]}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\",\"name\":\"Bad\",\"mesh\":\"cube\",\"visible\":true,\"position\":[01,0,0],"
                         "\"rotation\":[0,0,0],\"scale\":[1,1,1]}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\",\"name\":\"Bad\",\"mesh\":\"cube\",\"visible\":true,\"position\":[0,0],"
                         "\"rotation\":[0,0,0],\"scale\":[1,1,1]}]") == 0);
    CHECK(reject_objects("[{\"id\":\"1\",\"name\":\"Bad\",\"mesh\":\"cube\",\"visible\":1,\"position\":[0,0,0],"
                         "\"rotation\":[0,0,0],\"scale\":[1,1,1]}]") == 0);
    const char embedded[] = "{}\0trailing";
    CHECK(write_bytes("invalid.json", embedded, sizeof(embedded) - 1));
    CHECK(!af_scene_load("invalid.json", &error) && error.code == AF_SCENE_ERROR_FORMAT);
    FILE *large = fopen("large.json", "wb");
    CHECK(large && fseek(large, (long)AF_SCENE_FILE_MAX_BYTES, SEEK_SET) == 0 && fputc(' ', large) != EOF &&
          fclose(large) == 0);
    CHECK(!af_scene_load("large.json", &error) && error.code == AF_SCENE_ERROR_FORMAT);
    CHECK(remove("large.json") == 0);
    FILE *many = fopen("many.json", "wb");
    CHECK(many && fputs("{\"format\":\"afterlight.scene\",\"version\":1,\"next_id\":\"1\",\"objects\":[", many) >= 0);
    for (unsigned i = 0; i <= AF_SCENE_FILE_MAX_OBJECTS; ++i)
        CHECK(fputs(i ? ",{}" : "{}", many) >= 0);
    CHECK(fputs("]}", many) >= 0 && fclose(many) == 0);
    CHECK(!af_scene_load("many.json", &error) && error.code == AF_SCENE_ERROR_FORMAT);
    CHECK(remove("many.json") == 0);
    char long_name[AF_OBJECT_NAME_CAPACITY + 1];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = 0;
    char long_object[1024];
    snprintf(long_object, sizeof(long_object),
             "[{\"id\":\"1\",\"name\":\"%s\",\"mesh\":\"cube\",\"visible\":true,\"position\":[0,0,0],\"rotation\":[0,0,"
             "0],\"scale\":[1,1,1]}]",
             long_name);
    CHECK(reject_objects(long_object) == 0);
    CHECK(reject_objects("[{\"id\":\"1\",\"name\":\"\",\"mesh\":\"cube\",\"visible\":true,\"position\":[0,0,0],"
                         "\"rotation\":[0,0,0],\"scale\":[1,1,1]}]") == 0);
    const char *high_ids = "{\"format\":\"afterlight.scene\",\"version\":1,\"next_id\":\"18446744073709551614\","
                           "\"objects\":[{\"id\":\"9007199254740993\"," FIELDS "}]}";
    CHECK(write_text("high-ids.json", high_ids));
    copy = af_scene_load("high-ids.json", &error);
    CHECK(copy && af_scene_get_object(copy, UINT64_C(9007199254740993)));
    af_object_id high = af_scene_add_object(copy, "High", "cube");
    CHECK(high == UINT64_MAX - 1 && af_scene_remove_object(copy, high));
    CHECK(af_scene_save(copy, "high-ids.json", &error));
    af_scene_destroy(copy);
    copy = af_scene_load("high-ids.json", &error);
    CHECK(copy && af_scene_add_object(copy, "Last ID", "cube") == UINT64_MAX);
    CHECK(af_scene_remove_object(copy, UINT64_MAX));
    CHECK(af_scene_save(copy, "high-ids.json", &error));
    af_scene_destroy(copy);
    copy = af_scene_load("high-ids.json", &error);
    CHECK(copy && af_scene_add_object(copy, "Exhausted", "cube") == AF_OBJECT_NONE);
    af_scene_destroy(copy);
    af_scene_destroy(scene);
    puts("Scene IO: round trips, exact 64-bit IDs, ID history, UTF-8, malformed files, limits, atomic replacement "
         "passed.");
    return 0;
}

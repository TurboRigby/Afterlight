/* SPDX-License-Identifier: MPL-2.0 */
#include "world.h"
#include "cube.h"
#include <afterlight/model.h>
#include <stdio.h>

const af_vec3 tide_spawn = {0, .86f, 1.55f};
const af_vec3 tide_valve = {-2.6f, 1.08f, -3.46f};
const af_vec3 tide_sight = {0, .86f, -6.38f};
const af_vec3 tide_keys = {2.65f, 1.1f, -6.26f};
const af_vec3 tide_archive = {0, 1.16f, -22.15f};
const af_vec3 tide_manifest = {3.2f, 1.10f, -2.35f};
const af_vec3 tide_slip = {-3.15f, 1.10f, -17.40f};
const af_camera tide_optics = {.position = {0, 1.55f, -6.38f},
                               .yaw = -AF_PI / 2,
                               .pitch = -.57f,
                               .vertical_fov = .89f,
                               .near_clip = .07f,
                               .far_clip = 95};

typedef struct finish {
    af_vec3 rgb;
    float roughness, metal, emission;
} finish;

static const finish concrete = {{.165f, .211f, .22f}, .84f, .02f, 0},
                    floor_plate = {{.13f, .17f, .175f}, .76f, .18f, 0},
                    bulkhead = {{.044f, .09f, .104f}, .46f, .53f, 0}, trim = {{.27f, .35f, .34f}, .35f, .62f, 0},
                    oxide = {{.42f, .155f, .052f}, .39f, .41f, 0}, brass = {{.63f, .34f, .084f}, .31f, .68f, 0},
                    black = {{.018f, .035f, .046f}, .74f, .05f, 0}, signal_red = {{.96f, .10f, .045f}, .45f, 0, 2.3f},
                    signal_green = {{.14f, .91f, .57f}, .38f, 0, 2.2f}, ice = {{.16f, .58f, .92f}, .36f, 0, 2.8f},
                    gold = {{1.f, .43f, .13f}, .33f, 0, 2.4f}, paper = {{.73f, .68f, .47f}, .85f, 0, .18f};

static af_material surface(finish f) {
    af_material m = af_material_default();
    m.physically_based = true;
    m.base_color = (af_color){f.rgb.x, f.rgb.y, f.rgb.z, 1};
    m.roughness = f.roughness;
    m.metallic = f.metal;
    m.emission = f.rgb;
    m.emission_strength = f.emission;
    if (f.emission > 0)
        m.cast_shadows = false;
    return m;
}

static af_object_id box(af_scene *s, const char *name, af_vec3 center, af_vec3 size, finish f, bool solid) {
    af_object_id id = af_scene_add_object(s, name, "tide_block");
    af_transform t = af_transform_identity();
    t.position = center;
    t.scale = size;
    if (!id || !af_scene_set_object_transform(s, id, t) || !af_scene_set_object_material(s, id, surface(f)))
        return AF_OBJECT_NONE;
    if (solid) {
        af_collider c = af_collider_default();
        c.type = AF_COLLIDER_BOX;
        c.enabled = true;
        c.half_extents = (af_vec3){.5f, .5f, .5f};
        if (!af_scene_set_object_collider(s, id, c))
            return AF_OBJECT_NONE;
    }
    return id;
}

static bool prop(af_scene *s, const char *directory, const char *asset, af_vec3 position, float scale, float yaw) {
    char path[AF_ASSET_PATH_CAPACITY];
    int n = snprintf(path, sizeof(path), "%s/%s.glb", directory, asset);
    af_asset_error error = {0};
    af_object_id id;
    if (n < 0 || (size_t)n >= sizeof(path) || !af_scene_import_model(s, asset, path, &id, &error)) {
        fprintf(stderr, "Low Water prop %s failed: %s\n", asset, error.message);
        return false;
    }
    af_transform t = af_transform_identity();
    t.position = position;
    t.rotation.y = yaw;
    t.scale = (af_vec3){scale, scale, scale};
    return af_scene_set_object_transform(s, id, t);
}

static af_local_light fixture(const char *name, af_vec3 position, af_vec3 color, float intensity, float range) {
    af_local_light l = af_local_light_default(AF_LIGHT_POINT);
    snprintf(l.name, sizeof(l.name), "%s", name);
    l.position = position;
    l.color = color;
    l.intensity = intensity;
    l.range = range;
    l.enabled = true;
    l.cast_shadows = false;
    return l;
}

static bool architecture(tide_world *w) {
    af_scene *s = w->scene;
#define BOX(name, x, y, z, sx, sy, sz, look, solid) box(s, name, (af_vec3){x, y, z}, (af_vec3){sx, sy, sz}, look, solid)
    if (!BOX("Intake deck", 0, -.19f, -2.14f, 9.4f, .38f, 9.72f, floor_plate, true) ||
        !BOX("Optical basin bed", 0, -.94f, -9.88f, 4.45f, .3f, 6.0f, black, true) ||
        !BOX("Archive deck", 0, -.19f, -19.32f, 9.4f, .38f, 13.0f, floor_plate, true) ||
        !BOX("Entry wall", 0, 2.32f, 2.79f, 9.7f, 4.66f, .30f, concrete, true) ||
        !BOX("Back wall", 0, 2.32f, -25.83f, 9.7f, 4.66f, .30f, concrete, true) ||
        !BOX("Intake lintel", 0, 3.68f, -5.42f, 9.4f, 1.8f, .60f, bulkhead, false) ||
        !BOX("Optical baffle", 0, 3.37f, -11.90f, 5.55f, 2.42f, .29f, bulkhead, false) ||
        !BOX("Baffle brass edge", 0, 2.18f, -11.72f, 5.48f, .08f, .14f, brass, false) ||
        !BOX("Optical rail", 0, .12f, -6.99f, 4.54f, .24f, .23f, bulkhead, true) ||
        !BOX("Rear rail", 0, .12f, -12.82f, 4.54f, .24f, .23f, bulkhead, true) ||
        !BOX("Sight strip", 0, .015f, -6.59f, 1.38f, .028f, .065f, ice, false) ||
        !BOX("Sight mark", 0, .018f, -6.42f, .075f, .026f, .40f, ice, false) ||
        !BOX("Archive lintel", 0, 3.73f, -15.12f, 9.7f, 1.85f, .65f, bulkhead, false))
        return false;

    for (int side = -1; side <= 1; side += 2) {
        float x = (float)side;
        if (!BOX("Basin catwalk", x * 3.48f, -.19f, -9.9f, 2.51f, .38f, 5.82f, floor_plate, true) ||
            !BOX("Catwalk curb", x * 2.22f, .17f, -9.9f, .18f, .34f, 6.04f, trim, true) ||
            !BOX("Safety rail enamel", x * 2.23f, .351f, -9.9f, .11f, .024f, 5.93f, oxide, false) ||
            !BOX("Retaining wall", x * 4.82f, 2.25f, -11.5f, .29f, 4.65f, 28.8f, concrete, true) ||
            !BOX("Wall plinth", x * 4.60f, .27f, -11.5f, .14f, .54f, 28.3f, bulkhead, false) ||
            !BOX("Roof and light slit", x * 2.62f, 4.57f, -11.5f, 4.39f, .29f, 28.6f, bulkhead, false) ||
            !BOX("Floodgate bulkhead", x * 3.08f, 1.53f, -15.12f, 3.57f, 3.06f, .57f, bulkhead, true) ||
            !BOX("Gate jamb", x * 1.35f, 1.47f, -14.75f, .24f, 2.95f, .15f, brass, false) ||
            !BOX("Ceiling conduit", x * 3.94f, 3.54f, -9.4f, .18f, .20f, 22.8f, oxide, false) ||
            !BOX("Recorder backdrop", x * 2.54f, 1.73f, -24.96f, 2.78f, 3.28f, .24f, bulkhead, false))
            return false;
        for (int segment = 0; segment < 7; ++segment) {
            float z = 1.15f - (float)segment * 3.9f;
            if (!BOX("Rib upright", x * 4.59f, 2.24f, z, .25f, 4.48f, .27f, trim, false) ||
                !BOX("Rib crossbar", x * 2.40f, 4.27f, z, 4.62f, .23f, .28f, trim, false) ||
                !BOX("Wall marker", x * 4.435f, 2.78f, z, .065f, .066f, .76f, segment > 3 ? gold : ice, false))
                return false;
        }
        for (int i = 0; i < 8; ++i) {
            float z = -7.44f - .66f * (float)i;
            if (!BOX("Tread seam", x * 3.49f, .006f, z, 2.22f, .012f, .033f, bulkhead, false))
                return false;
        }
        w->cover[side > 0] = BOX("Optical shutter", x * 1.11f, -.105f, -9.9f, 2.22f, .15f, 5.81f, bulkhead, false);
        if (!w->cover[side > 0])
            return false;
    }
    for (int slat = 0; slat < 13; ++slat) {
        float z = -3.8f - .56f * (float)slat;
        if (!BOX("Skywell louver", 0, 4.41f, z, 1.02f, .12f, .16f, trim, false))
            return false;
    }

    w->gate = BOX("Lift gate", 0, 1.39f, -15.11f, 2.64f, 2.78f, .24f, trim, true);
    w->pump_lamp = BOX("Pump annunciator", -2.60f, 1.82f, -3.51f, .60f, .12f, .18f, signal_red, false);
    w->gate_lamp = BOX("Gate annunciator", 0, 3.10f, -14.75f, 1.46f, .13f, .20f, signal_red, false);
    w->signal = BOX("Transmission filament", 0, 2.83f, -23.13f, .41f, .41f, .39f, gold, false);
    if (!w->gate || !w->pump_lamp || !w->gate_lamp || !w->signal ||
        !BOX("Pump pedestal", -2.6f, .48f, -3.55f, .77f, .96f, .56f, bulkhead, true) ||
        !BOX("Keypad footprint", 2.65f, .42f, -6.36f, .62f, .84f, .55f, bulkhead, true) ||
        !BOX("Dispatch shelf", 3.28f, .81f, -2.43f, 1.22f, .13f, .64f, bulkhead, true) ||
        !BOX("Intake manifest", 3.2f, .896f, -2.35f, .51f, .035f, .32f, paper, false) ||
        !BOX("Service shelf", -3.17f, .81f, -17.52f, 1.22f, .13f, .64f, bulkhead, true) ||
        !BOX("Carbon copy", -3.15f, .899f, -17.4f, .45f, .034f, .34f, paper, false) ||
        !BOX("Recorder footprint", 0, .56f, -22.35f, 2.19f, 1.12f, .85f, bulkhead, true) ||
        !BOX("Transmission column", 0, 2.51f, -23.27f, .37f, 4.08f, .37f, bulkhead, false))
        return false;
#undef BOX
    return true;
}

bool tide_world_build(tide_world *w, af_engine *engine, const char *directory, bool rays, bool cinematic) {
    w->scene = af_scene_create();
    if (!w->scene)
        return false;
    af_scene *s = w->scene;
    af_mesh_data source = cube_mesh_data();
    af_vertex blocks[24];
    for (size_t i = 0; i < 24; ++i) {
        blocks[i] = source.vertices[i];
        blocks[i].position = af_vec3_scale(blocks[i].position, .5f / .65f);
        blocks[i].color = (af_color){1, 1, 1, 1};
    }
    source.vertices = blocks;
    if (!af_engine_upload_mesh(engine, "tide_block", &source) || !architecture(w) ||
        !prop(s, directory, "valve", tide_valve, 1, 0) ||
        !prop(s, directory, "keypad", (af_vec3){2.65f, 0, -6.30f}, 1, 0) ||
        !prop(s, directory, "calibration", (af_vec3){0, 3.04f, -14.03f}, 1, 0) ||
        !prop(s, directory, "archive", (af_vec3){0, 0, -22.46f}, 1, 0) ||
        !prop(s, directory, "crown", (af_vec3){0, 2.82f, -23.15f}, .84f, 0))
        return false;

    w->basin = af_water_default();
    w->basin.owner = af_scene_add_object(s, "Optical water surface", "__node");
    af_transform t = af_transform_identity();
    t.position = (af_vec3){0, -.18f, -9.90f};
    w->basin.depth = .62f;
    w->basin.half_size = (af_vec2){2.19f, 2.85f};
    w->basin.quality = AF_WATER_LOW;
    w->basin.roughness = .045f;
    w->basin.ior = 1.59f;
    w->basin.wind = w->basin.ripple_strength = 0;
    w->basin.scattering = (af_vec3){.015f, .042f, .058f};
    w->basin.absorption = (af_vec3){1.25f, .79f, .62f};
    w->basin.enabled = false;
    if (!w->basin.owner || !af_scene_set_object_transform(s, w->basin.owner, t) ||
        !af_scene_set_water(s, w->basin.owner, w->basin)) {
        fputs("Low Water: invalid basin configuration.\n", stderr);
        return false;
    }

    af_directional_light sun = af_directional_light_default();
    sun.direction = (af_vec3){-.30f, -.85f, -.39f};
    sun.color = (af_vec3){.54f, .72f, 1};
    sun.intensity = 2.05f;
    sun.ambient = .135f;
    af_shadow_settings shadow = af_shadow_settings_default();
    shadow.distance = 34;
    shadow.resolution = 1024;
    af_render_settings r = af_render_settings_default();
    r.ray_traced_shadows = r.ray_traced_gi = r.ray_traced_water_reflections = rays;
    r.ray_traced_material_effects = rays && cinematic;
    r.rtgi_emissive = true;
    r.rtgi_samples = 1;
    r.rtgi_bounces = cinematic ? 2u : 1u;
    r.rtgi_intensity = .96f;
    r.rtgi_ray_distance = 17;
    r.anti_aliasing = AF_AA_TAA;
    r.environment_enabled = true;
    r.environment_intensity = .22f;
    r.skybox = false;
    r.exposure = .32f;
    r.tone_mapping = true;
    r.tone_mapper = AF_TONE_MAP_AGX;
    r.bloom = true;
    r.bloom_threshold = 1.65f;
    r.bloom_strength = .23f;
    r.vignette = true;
    r.vignette_intensity = .15f;
    r.refraction = true;
    r.refraction_strength = .2f;
    r.local_shadow_resolution = 256;
    if (!af_scene_set_light(s, sun) || !af_scene_set_shadows(s, shadow) || !af_scene_set_render_settings(s, r))
        return false;

    af_local_light fixtures[] = {
        fixture("Intake sodium", (af_vec3){-2.8f, 2.92f, -.7f}, (af_vec3){1, .48f, .22f}, 13, 8),
        fixture("Intake work light", (af_vec3){2.7f, 2.96f, -3.5f}, (af_vec3){.38f, .73f, 1}, 11, 7),
        fixture("Water inlet", (af_vec3){-.5f, 2.85f, -7.3f}, (af_vec3){.20f, .64f, 1}, 1.5f, 9),
        fixture("Service catwalk", (af_vec3){3.58f, 2.5f, -10.4f}, (af_vec3){.20f, .7f, .82f}, 7, 7.8f),
        fixture("Archive gate", (af_vec3){.1f, 2.6f, -14.18f}, (af_vec3){.30f, .68f, 1}, 6, 6),
        fixture("Archive sodium", (af_vec3){-.9f, 3.02f, -20.8f}, (af_vec3){1, .36f, .11f}, 7, 10),
        fixture("Rear bay", (af_vec3){3.09f, 2.43f, -23.98f}, (af_vec3){.28f, .55f, 1}, 6, 7),
    };
    w->pump_light = 2;
    w->archive_light = 5;
    fixtures[0].cast_shadows = fixtures[w->archive_light].cast_shadows = true;
    for (size_t i = 0; i < sizeof(fixtures) / sizeof(fixtures[0]); ++i)
        if (!af_scene_add_local_light(s, fixtures[i]))
            return false;
    af_asset_error error = {0};
    if (!af_engine_load_scene_assets(engine, s, &error)) {
        fprintf(stderr, "Low Water asset load: %s\n", error.message);
        return false;
    }
    return true;
}

bool tide_world_pump(tide_world *w, bool on) {
    af_local_light light = *af_scene_local_light_at(w->scene, w->pump_light);
    light.intensity = on ? 16 : 1.5f;
    w->basin.enabled = on;
    return af_scene_set_object_material(w->scene, w->pump_lamp, surface(on ? signal_green : signal_red)) &&
           af_scene_set_local_light(w->scene, w->pump_light, light) &&
           af_scene_set_water(w->scene, w->basin.owner, w->basin);
}

bool tide_world_archive(tide_world *w, bool open) {
    return af_scene_set_object_material(w->scene, w->gate_lamp, surface(open ? signal_green : signal_red));
}

bool tide_world_transmit(tide_world *w, unsigned choice) {
    af_local_light light = *af_scene_local_light_at(w->scene, w->archive_light);
    light.color = choice == 2 ? (af_vec3){.25f, .8f, 1} : (af_vec3){1, .36f, .11f};
    light.intensity = choice ? 17 : 7;
    return af_scene_set_local_light(w->scene, w->archive_light, light) &&
           af_scene_set_object_material(w->scene, w->signal, surface(choice == 2 ? ice : gold));
}

void tide_world_animate(tide_world *w, float cover, float gate) {
    for (unsigned i = 0; i < 2; ++i) {
        af_transform t = af_scene_get_object(w->scene, w->cover[i])->transform;
        t.position.x = (i ? 1.f : -1.f) * (1.11f + 2.33f * cover);
        t.position.y = -.105f - .34f * cover;
        af_scene_set_object_transform(w->scene, w->cover[i], t);
    }
    af_transform t = af_scene_get_object(w->scene, w->gate)->transform;
    t.position.y = 1.39f + 2.82f * gate;
    af_scene_set_object_transform(w->scene, w->gate, t);
}

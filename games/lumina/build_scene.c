/* SPDX-License-Identifier: MPL-2.0
 * LUMINA: Deep Core - Scene Generator
 */

#include <afterlight/scene.h>
#include <afterlight/water.h>
#include <afterlight/material.h>
#include <afterlight/collision.h>
#include <afterlight/gameplay.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void setup_materials_and_colliders(af_scene *scene, af_object_id id, af_vec3 half_extents) {
    af_collider col = {.type = AF_COLLIDER_BOX, .enabled = true, .half_extents = half_extents, .layer = 1};
    af_scene_set_object_collider(scene, id, col);
}

int main(int argc, char **argv) {
    const char *output = argc > 1 ? argv[1] : "games/lumina/scenes/lumina_core.afterlight.json";
    af_scene *scene = af_scene_create();
    if (!scene) {
        fprintf(stderr, "Failed to create scene\n");
        return 1;
    }

    // 1. Register Mesh Assets
    af_scene_set_asset(scene, "reactor_core", AF_ASSET_MESH, "../assets/reactor_core.obj");
    af_scene_set_asset(scene, "conduit_pillar", AF_ASSET_MESH, "../assets/conduit_pillar.obj");
    af_scene_set_asset(scene, "catwalk", AF_ASSET_MESH, "../assets/catwalk.obj");
    af_scene_set_asset(scene, "energy_cell", AF_ASSET_MESH, "../assets/energy_cell.obj");
    af_scene_set_asset(scene, "relay_pedestal", AF_ASSET_MESH, "../assets/relay_pedestal.obj");
    af_scene_set_asset(scene, "barrier_gate", AF_ASSET_MESH, "../assets/barrier_gate.obj");
    af_scene_set_asset(scene, "industrial_lamp", AF_ASSET_MESH, "../assets/industrial_lamp.obj");
    af_scene_set_asset(scene, "player_drone", AF_ASSET_MESH, "../assets/player_drone.obj");

    // 2. Foundation / Floor Basin
    {
        af_object_id floor = af_scene_add_object(scene, "Basin_Floor", "cube");
        af_transform t = {.position = {0, -2.5f, 0}, .scale = {72.0f, 1.0f, 72.0f}};
        af_scene_set_object_transform(scene, floor, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.12f, 0.14f, 0.16f, 1.0f};
        mat.roughness = 0.85f;
        mat.metallic = 0.1f;
        af_scene_set_object_material(scene, floor, mat);
        setup_materials_and_colliders(scene, floor, (af_vec3){36.0f, 0.5f, 36.0f});
    }

    // Ceiling with ventilation aperture
    {
        af_object_id ceiling = af_scene_add_object(scene, "Ceiling_Structure", "cube");
        af_transform t = {.position = {0, 14.0f, 0}, .scale = {72.0f, 1.0f, 72.0f}};
        af_scene_set_object_transform(scene, ceiling, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.15f, 0.16f, 0.18f, 1.0f};
        mat.roughness = 0.7f;
        af_scene_set_object_material(scene, ceiling, mat);
        setup_materials_and_colliders(scene, ceiling, (af_vec3){36.0f, 0.5f, 36.0f});
    }

    // Containment Outer Walls (North, South, East, West)
    {
        struct {
            const char *name;
            af_vec3 pos;
            af_vec3 scale;
        } walls[] = {
            {"Wall_North", {0, 5.5f, -36.0f}, {72.0f, 16.0f, 2.0f}},
            {"Wall_South", {0, 5.5f, 36.0f}, {72.0f, 16.0f, 2.0f}},
            {"Wall_East", {36.0f, 5.5f, 0}, {2.0f, 16.0f, 72.0f}},
            {"Wall_West", {-36.0f, 5.5f, 0}, {2.0f, 16.0f, 72.0f}},
        };
        for (int i = 0; i < 4; ++i) {
            af_object_id w = af_scene_add_object(scene, walls[i].name, "cube");
            af_transform t = {.position = walls[i].pos, .scale = walls[i].scale};
            af_scene_set_object_transform(scene, w, t);
            af_material mat = af_material_default();
            mat.base_color = (af_color){0.18f, 0.20f, 0.22f, 1.0f};
            mat.roughness = 0.65f;
            mat.metallic = 0.2f;
            af_scene_set_object_material(scene, w, mat);
            setup_materials_and_colliders(
                scene, w, (af_vec3){walls[i].scale.x * 0.5f, walls[i].scale.y * 0.5f, walls[i].scale.z * 0.5f});
        }
    }

    // 3. Central Quantum Reactor Core
    {
        af_object_id core = af_scene_add_object(scene, "Reactor_Core", "reactor_core");
        af_transform t = {.position = {0, -1.8f, 0}, .scale = {1.2f, 1.2f, 1.2f}};
        af_scene_set_object_transform(scene, core, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.22f, 0.25f, 0.28f, 1.0f};
        mat.roughness = 0.35f;
        mat.metallic = 0.85f;
        mat.emission = (af_vec3){0.1f, 0.85f, 1.0f};
        mat.emission_strength = 2.5f;
        af_scene_set_object_material(scene, core, mat);
        setup_materials_and_colliders(scene, core, (af_vec3){2.2f, 5.0f, 2.2f});
    }

    // 4. Eight Conduit Pillars around the Chamber (Octagon r = 14m)
    for (int i = 0; i < 8; ++i) {
        float angle = (float)i * (2.0f * AF_PI / 8.0f);
        float px = cosf(angle) * 14.0f;
        float pz = sinf(angle) * 14.0f;
        char name[32];
        snprintf(name, sizeof(name), "Conduit_Pillar_%d", i + 1);
        af_object_id p = af_scene_add_object(scene, name, "conduit_pillar");
        af_transform t = {.position = {px, -2.0f, pz}, .rotation = {0, angle, 0}, .scale = {1.0f, 1.2f, 1.0f}};
        af_scene_set_object_transform(scene, p, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.25f, 0.28f, 0.30f, 1.0f};
        mat.roughness = 0.45f;
        mat.metallic = 0.5f;
        mat.emission = (af_vec3){0.2f, 0.6f, 0.9f};
        mat.emission_strength = 0.8f;
        af_scene_set_object_material(scene, p, mat);
        setup_materials_and_colliders(scene, p, (af_vec3){1.1f, 4.5f, 1.1f});
    }

    // 5. Catwalk Walkways Spanning Across the Flooded Chamber
    // North Branch (Wing 1)
    for (int i = 0; i < 4; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Catwalk_North_%d", i);
        af_object_id cw = af_scene_add_object(scene, name, "catwalk");
        af_transform t = {.position = {0, 0, -6.0f - (float)i * 6.0f}, .scale = {1.0f, 1.0f, 1.0f}};
        af_scene_set_object_transform(scene, cw, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.20f, 0.22f, 0.24f, 1.0f};
        mat.roughness = 0.5f;
        mat.metallic = 0.8f;
        af_scene_set_object_material(scene, cw, mat);
        setup_materials_and_colliders(scene, cw, (af_vec3){1.5f, 0.15f, 3.0f});
    }
    // East Branch (Wing 2)
    for (int i = 0; i < 4; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Catwalk_East_%d", i);
        af_object_id cw = af_scene_add_object(scene, name, "catwalk");
        af_transform t = {
            .position = {6.0f + (float)i * 6.0f, 0, 0}, .rotation = {0, AF_PI * 0.5f, 0}, .scale = {1.0f, 1.0f, 1.0f}};
        af_scene_set_object_transform(scene, cw, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.20f, 0.22f, 0.24f, 1.0f};
        mat.roughness = 0.5f;
        mat.metallic = 0.8f;
        af_scene_set_object_material(scene, cw, mat);
        setup_materials_and_colliders(scene, cw, (af_vec3){3.0f, 0.15f, 1.5f});
    }
    // West Branch (Wing 3)
    for (int i = 0; i < 4; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Catwalk_West_%d", i);
        af_object_id cw = af_scene_add_object(scene, name, "catwalk");
        af_transform t = {
            .position = {-6.0f - (float)i * 6.0f, 0, 0}, .rotation = {0, AF_PI * 0.5f, 0}, .scale = {1.0f, 1.0f, 1.0f}};
        af_scene_set_object_transform(scene, cw, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.20f, 0.22f, 0.24f, 1.0f};
        mat.roughness = 0.5f;
        mat.metallic = 0.8f;
        af_scene_set_object_material(scene, cw, mat);
        setup_materials_and_colliders(scene, cw, (af_vec3){3.0f, 0.15f, 1.5f});
    }
    // South Branch (Exit Route)
    for (int i = 0; i < 3; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "Catwalk_South_%d", i);
        af_object_id cw = af_scene_add_object(scene, name, "catwalk");
        af_transform t = {.position = {0, 0, 6.0f + (float)i * 6.0f}, .scale = {1.0f, 1.0f, 1.0f}};
        af_scene_set_object_transform(scene, cw, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.20f, 0.22f, 0.24f, 1.0f};
        mat.roughness = 0.5f;
        mat.metallic = 0.8f;
        af_scene_set_object_material(scene, cw, mat);
        setup_materials_and_colliders(scene, cw, (af_vec3){1.5f, 0.15f, 3.0f});
    }

    // 6. Three Relay Pedestals
    // Relay 01 (Coolant / North)
    {
        af_object_id r1 = af_scene_add_object(scene, "Relay_Coolant", "relay_pedestal");
        af_transform t = {.position = {0, 0, -8.0f}, .rotation = {0, 0, 0}, .scale = {1, 1, 1}};
        af_scene_set_object_transform(scene, r1, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.25f, 0.28f, 0.32f, 1.0f};
        mat.roughness = 0.4f;
        mat.metallic = 0.7f;
        mat.emission = (af_vec3){0.1f, 0.8f, 1.0f};
        mat.emission_strength = 0.5f;
        af_scene_set_object_material(scene, r1, mat);
        setup_materials_and_colliders(scene, r1, (af_vec3){0.6f, 0.6f, 0.6f});
    }
    // Relay 02 (Optics / East)
    {
        af_object_id r2 = af_scene_add_object(scene, "Relay_Optics", "relay_pedestal");
        af_transform t = {.position = {8.0f, 0, 0}, .rotation = {0, -AF_PI * 0.5f, 0}, .scale = {1, 1, 1}};
        af_scene_set_object_transform(scene, r2, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.25f, 0.28f, 0.32f, 1.0f};
        mat.roughness = 0.4f;
        mat.metallic = 0.7f;
        mat.emission = (af_vec3){1.0f, 0.6f, 0.1f};
        mat.emission_strength = 0.5f;
        af_scene_set_object_material(scene, r2, mat);
        setup_materials_and_colliders(scene, r2, (af_vec3){0.6f, 0.6f, 0.6f});
    }
    // Relay 03 (Magnetic / West)
    {
        af_object_id r3 = af_scene_add_object(scene, "Relay_Magnetic", "relay_pedestal");
        af_transform t = {.position = {-8.0f, 0, 0}, .rotation = {0, AF_PI * 0.5f, 0}, .scale = {1, 1, 1}};
        af_scene_set_object_transform(scene, r3, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.25f, 0.28f, 0.32f, 1.0f};
        mat.roughness = 0.4f;
        mat.metallic = 0.7f;
        mat.emission = (af_vec3){1.0f, 0.15f, 0.1f};
        mat.emission_strength = 0.5f;
        af_scene_set_object_material(scene, r3, mat);
        setup_materials_and_colliders(scene, r3, (af_vec3){0.6f, 0.6f, 0.6f});
    }

    // 7. Three Energy Cells (Prisms)
    // Alpha Cell (Cyan - at end of North Wing)
    {
        af_object_id c1 = af_scene_add_object(scene, "Cell_Alpha", "energy_cell");
        af_transform t = {.position = {0, 0.1f, -26.0f}, .scale = {1, 1, 1}};
        af_scene_set_object_transform(scene, c1, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.1f, 0.9f, 1.0f, 1.0f};
        mat.roughness = 0.1f;
        mat.metallic = 0.9f;
        mat.emission = (af_vec3){0.1f, 0.9f, 1.0f};
        mat.emission_strength = 4.0f;
        af_scene_set_object_material(scene, c1, mat);
        setup_materials_and_colliders(scene, c1, (af_vec3){0.3f, 0.5f, 0.3f});
    }
    // Beta Cell (Amber - at end of East Wing)
    {
        af_object_id c2 = af_scene_add_object(scene, "Cell_Beta", "energy_cell");
        af_transform t = {.position = {26.0f, 0.1f, 0}, .scale = {1, 1, 1}};
        af_scene_set_object_transform(scene, c2, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){1.0f, 0.65f, 0.1f, 1.0f};
        mat.roughness = 0.1f;
        mat.metallic = 0.9f;
        mat.emission = (af_vec3){1.0f, 0.65f, 0.1f};
        mat.emission_strength = 4.0f;
        af_scene_set_object_material(scene, c2, mat);
        setup_materials_and_colliders(scene, c2, (af_vec3){0.3f, 0.5f, 0.3f});
    }
    // Gamma Cell (Crimson - at end of West Wing)
    {
        af_object_id c3 = af_scene_add_object(scene, "Cell_Gamma", "energy_cell");
        af_transform t = {.position = {-26.0f, 0.1f, 0}, .scale = {1, 1, 1}};
        af_scene_set_object_transform(scene, c3, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){1.0f, 0.15f, 0.1f, 1.0f};
        mat.roughness = 0.1f;
        mat.metallic = 0.9f;
        mat.emission = (af_vec3){1.0f, 0.15f, 0.1f};
        mat.emission_strength = 4.0f;
        af_scene_set_object_material(scene, c3, mat);
        setup_materials_and_colliders(scene, c3, (af_vec3){0.3f, 0.5f, 0.3f});
    }

    // 8. Exit Barrier Gate and Extraction Elevator at South Deck
    {
        af_object_id gate = af_scene_add_object(scene, "Exit_Barrier", "barrier_gate");
        af_transform t = {.position = {0, 0, 18.0f}, .scale = {1, 1, 1}};
        af_scene_set_object_transform(scene, gate, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.28f, 0.24f, 0.20f, 1.0f};
        mat.roughness = 0.5f;
        mat.metallic = 0.7f;
        mat.emission = (af_vec3){1.0f, 0.5f, 0.05f};
        mat.emission_strength = 1.0f;
        af_scene_set_object_material(scene, gate, mat);
        setup_materials_and_colliders(scene, gate, (af_vec3){2.0f, 2.5f, 0.5f});
    }
    {
        af_object_id lift = af_scene_add_object(scene, "Extraction_Lift", "cube");
        af_transform t = {.position = {0, -0.1f, 24.0f}, .scale = {5.0f, 0.4f, 5.0f}};
        af_scene_set_object_transform(scene, lift, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.35f, 0.4f, 0.45f, 1.0f};
        mat.roughness = 0.3f;
        mat.metallic = 0.9f;
        af_scene_set_object_material(scene, lift, mat);
        setup_materials_and_colliders(scene, lift, (af_vec3){2.5f, 0.2f, 2.5f});

        // Set gameplay goal role
        af_gameplay_component gp = af_gameplay_component_default();
        gp.role = AF_ROLE_GOAL;
        af_scene_set_object_gameplay(scene, lift, gp);
    }

    // 9. Flooded Cooling Basin Water (Hardware Ray Water Reflections!)
    {
        af_object_id water_obj = af_scene_add_object(scene, "Cooling_Basin_Water", "__node");
        af_transform t = {.position = {0, -0.45f, 0}, .rotation = {0, 0, 0}, .scale = {1.0f, 1.0f, 1.0f}};
        af_scene_set_object_transform(scene, water_obj, t);

        af_collider col = {.enabled = false, .type = AF_COLLIDER_NONE};
        af_scene_set_object_collider(scene, water_obj, col);

        af_water w = af_water_default();
        w.owner = water_obj;
        w.enabled = true;
        w.mode = AF_WATER_POOL;
        w.quality = AF_WATER_HIGH;
        w.half_size = (af_vec2){34.0f, 34.0f};
        w.depth = 2.0f;
        w.density = 1000.0f;
        w.drag = 1.5f;
        w.wave_speed = 1.4f;
        w.damping = 0.985f;
        w.roughness = 0.05f; // Glossy surface for stunning ray-traced reflections
        w.ior = 1.333f;
        w.absorption = (af_vec3){0.02f, 0.14f, 0.18f};
        w.scattering = (af_vec3){0.01f, 0.08f, 0.12f};
        w.ripple_scale = 0.6f;
        w.ripple_strength = 0.18f;
        if (!af_scene_set_water(scene, water_obj, w)) {
            fprintf(stderr, "Warning: af_scene_set_water returned false!\n");
        }
    }

    // 10. Player Character Object
    {
        af_object_id player = af_scene_add_object(scene, "Player", "player_drone");
        af_transform t = {.position = {0, 1.05f, 10.0f},
                          .rotation = {0, 0, 0}, // Face north toward core
                          .scale = {1.0f, 1.0f, 1.0f}};
        af_scene_set_object_transform(scene, player, t);
        af_material mat = af_material_default();
        mat.base_color = (af_color){0.25f, 0.28f, 0.32f, 1.0f};
        mat.metallic = 0.8f;
        mat.roughness = 0.35f;
        af_scene_set_object_material(scene, player, mat);

        af_collider col = {
            .type = AF_COLLIDER_CAPSULE, .enabled = true, .radius = 0.35f, .half_height = 0.55f, .layer = 1};
        af_scene_set_object_collider(scene, player, col);

        af_gameplay_component gp = af_gameplay_component_default();
        gp.role = AF_ROLE_SPAWN;
        gp.player.enabled = true;
        gp.player.controller.speed = 5.2f;
        gp.player.controller.jump_speed = 6.8f;
        gp.player.controller.gravity = 18.0f;
        gp.player.mouse_sensitivity = 0.0024f;
        gp.camera.enabled = true;
        gp.camera.primary = true;
        gp.camera.eye_height = 0.75f;
        gp.camera.vertical_fov = 1.15f; // ~66 deg FOV
        gp.camera.near_clip = 0.05f;
        gp.camera.far_clip = 180.0f;
        af_scene_set_object_gameplay(scene, player, gp);
    }

    // 11. Lighting Configuration
    // Directional Sun Light (Angled skylight beam)
    {
        af_directional_light sun = {
            .direction = {0.35f, -0.88f, 0.32f},
            .color = {0.95f, 0.90f, 0.82f},
            .intensity = 1.9f,
            .ambient = 0.06f // Low ambient so RTGI bounces create natural fill
        };
        af_scene_set_light(scene, sun);
    }

    // Shadows Configuration
    {
        af_shadow_settings ss = {.enabled = true,
                                 .mode = AF_SHADOW_CASCADED,
                                 .resolution = 2048,
                                 .distance = 65.0f,
                                 .constant_bias = 1.25f,
                                 .slope_bias = 1.75f,
                                 .normal_bias = 0.015f,
                                 .filter_radius = 1.0f,
                                 .split_lambda = 0.65f,
                                 .blend_fraction = 0.1f};
        af_scene_set_shadows(scene, ss);
    }

    // Local Lights (Up to 16 hardware ray traced dynamic lights)
    // Light 0: Player Spotlight
    {
        af_local_light spot = af_local_light_default(AF_LIGHT_SPOT);
        snprintf(spot.name, sizeof(spot.name), "Player_Spotlight");
        spot.enabled = true;
        spot.position = (af_vec3){0, 1.5f, 10.0f};
        spot.direction = (af_vec3){0, -0.1f, -1.0f};
        spot.color = (af_vec3){1.0f, 0.95f, 0.90f};
        spot.intensity = 6.0f;
        spot.range = 38.0f;
        spot.inner_angle = 24.0f * (AF_PI / 180.0f);
        spot.outer_angle = 42.0f * (AF_PI / 180.0f);
        spot.cast_shadows = true;
        spot.shadow_bias = 0.015f;
        af_scene_add_local_light(scene, spot);
    }

    // Light 1: Central Reactor Core Light (Pulsing Cyan Plasma)
    {
        af_local_light core_light = af_local_light_default(AF_LIGHT_POINT);
        snprintf(core_light.name, sizeof(core_light.name), "Reactor_Plasma_Light");
        core_light.enabled = true;
        core_light.position = (af_vec3){0, 3.2f, 0};
        core_light.color = (af_vec3){0.1f, 0.85f, 1.0f};
        core_light.intensity = 5.5f;
        core_light.range = 32.0f;
        core_light.cast_shadows = true;
        core_light.shadow_bias = 0.02f;
        af_scene_add_local_light(scene, core_light);
    }

    // Light 2: Alpha Cell Glow (Cyan Point Light)
    {
        af_local_light l = af_local_light_default(AF_LIGHT_POINT);
        snprintf(l.name, sizeof(l.name), "Light_Cell_Alpha");
        l.enabled = true;
        l.position = (af_vec3){0, 0.8f, -26.0f};
        l.color = (af_vec3){0.1f, 0.9f, 1.0f};
        l.intensity = 3.5f;
        l.range = 14.0f;
        l.cast_shadows = true;
        af_scene_add_local_light(scene, l);
    }

    // Light 3: Beta Cell Glow (Amber Point Light)
    {
        af_local_light l = af_local_light_default(AF_LIGHT_POINT);
        snprintf(l.name, sizeof(l.name), "Light_Cell_Beta");
        l.enabled = true;
        l.position = (af_vec3){26.0f, 0.8f, 0};
        l.color = (af_vec3){1.0f, 0.65f, 0.1f};
        l.intensity = 3.5f;
        l.range = 14.0f;
        l.cast_shadows = true;
        af_scene_add_local_light(scene, l);
    }

    // Light 4: Gamma Cell Glow (Red Point Light)
    {
        af_local_light l = af_local_light_default(AF_LIGHT_POINT);
        snprintf(l.name, sizeof(l.name), "Light_Cell_Gamma");
        l.enabled = true;
        l.position = (af_vec3){-26.0f, 0.8f, 0};
        l.color = (af_vec3){1.0f, 0.15f, 0.1f};
        l.intensity = 3.5f;
        l.range = 14.0f;
        l.cast_shadows = true;
        af_scene_add_local_light(scene, l);
    }

    // Light 5-7: Relay Docking Beacon Lights
    {
        af_local_light l1 = af_local_light_default(AF_LIGHT_POINT);
        snprintf(l1.name, sizeof(l1.name), "Light_Relay_Coolant");
        l1.enabled = true;
        l1.position = (af_vec3){0, 1.4f, -8.0f};
        l1.color = (af_vec3){0.1f, 0.8f, 1.0f};
        l1.intensity = 1.8f;
        l1.range = 8.0f;
        af_scene_add_local_light(scene, l1);

        af_local_light l2 = af_local_light_default(AF_LIGHT_POINT);
        snprintf(l2.name, sizeof(l2.name), "Light_Relay_Optics");
        l2.enabled = true;
        l2.position = (af_vec3){8.0f, 1.4f, 0};
        l2.color = (af_vec3){1.0f, 0.6f, 0.1f};
        l2.intensity = 1.8f;
        l2.range = 8.0f;
        af_scene_add_local_light(scene, l2);

        af_local_light l3 = af_local_light_default(AF_LIGHT_POINT);
        snprintf(l3.name, sizeof(l3.name), "Light_Relay_Magnetic");
        l3.enabled = true;
        l3.position = (af_vec3){-8.0f, 1.4f, 0};
        l3.color = (af_vec3){1.0f, 0.15f, 0.1f};
        l3.intensity = 1.8f;
        l3.range = 8.0f;
        af_scene_add_local_light(scene, l3);
    }

    // Light 8: Exit Barrier Strobe Light
    {
        af_local_light l = af_local_light_default(AF_LIGHT_POINT);
        snprintf(l.name, sizeof(l.name), "Light_Exit_Strobe");
        l.enabled = true;
        l.position = (af_vec3){0, 3.2f, 18.0f};
        l.color = (af_vec3){1.0f, 0.5f, 0.05f};
        l.intensity = 2.8f;
        l.range = 16.0f;
        l.cast_shadows = true;
        af_scene_add_local_light(scene, l);
    }

    // 12. Complete Render Settings for Maximum Ray Tracing Fidelity
    {
        af_render_settings rs = af_render_settings_default();
        rs.ray_traced_shadows = true;
        rs.ray_traced_water_reflections = true;
        rs.ray_traced_gi = true;
        rs.rtgi_intensity = 1.35f;
        rs.rtgi_ray_distance = 45.0f;
        rs.rtgi_samples = 1;
        rs.anti_aliasing = AF_AA_TAA;
        rs.aa_sharpness = 0.15f;
        rs.taa_history_weight = 0.88f;
        rs.exposure = 0.38f;
        rs.tone_mapping = true;
        rs.bloom = true;
        rs.bloom_strength = 0.22f;
        rs.bloom_threshold = 0.95f;
        rs.bloom_radius = 1.1f;
        rs.vignette = true;
        rs.vignette_intensity = 0.25f;
        rs.vignette_smoothness = 0.45f;
        rs.underwater = true;
        rs.refraction = true;
        rs.refraction_strength = 0.45f;
        rs.local_shadow_resolution = 1024;
        af_scene_set_render_settings(scene, rs);
    }

    // 13. Save Scene to JSON File
    af_scene_error err = {0};
    if (!af_scene_save(scene, output, &err)) {
        fprintf(stderr, "Failed to save scene: %s\n", err.message);
        af_scene_destroy(scene);
        return 1;
    }

    printf("Successfully built and saved scene to %s (%zu objects)\n", output, af_scene_object_count(scene));
    af_scene_destroy(scene);
    return 0;
}

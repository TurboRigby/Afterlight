/* SPDX-License-Identifier: MPL-2.0 */
#include "render/render_test.h"
#include <afterlight/afterlight.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct fixture {
    af_scene *scene;
    af_object_id floor;
    af_object_id emitter;
    af_object_id wall;
    af_object_id blocker;
    af_object_id bounce_wall, bounce_emitter, bounce_blocker;
    unsigned char blocked[4];
    unsigned phase, frames;
    uint64_t temporal_frames;
    bool failed;
} fixture;

static bool init(af_engine *engine, void *data) {
    fixture *f = data;
    f->scene = af_scene_create();
    if (!f->scene)
        return false;

    /* Ground receiver plane at Y = 0 facing +Y */
    static const af_vertex floor_vertices[] = {
        {.position = {-5, 0, -5}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}, .uv = {0, 0}},
        {.position = {5, 0, -5}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}, .uv = {1, 0}},
        {.position = {5, 0, 5}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}, .uv = {1, 1}},
        {.position = {-5, 0, 5}, .color = {1, 1, 1, 1}, .normal = {0, 1, 0}, .uv = {0, 1}},
    };
    static const uint32_t indices[] = {0, 1, 2, 0, 2, 3};
    af_mesh_data floor_mesh = {floor_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "floor", &floor_mesh))
        return false;
    f->floor = af_scene_add_object(f->scene, "Floor", "floor");
    af_material floor_mat = af_material_default();
    floor_mat.physically_based = true;
    floor_mat.base_color = (af_color){1, 1, 1, 1};
    floor_mat.roughness = 0.8f;
    floor_mat.metallic = 0.0f;
    if (!af_scene_set_object_material(f->scene, f->floor, floor_mat))
        return false;

    /* Emissive overhead emitter at Y = 2 facing -Y */
    static const af_vertex emitter_vertices[] = {
        {.position = {-2, 2, -2}, .color = {1, 1, 1, 1}, .normal = {0, -1, 0}, .uv = {0, 0}},
        {.position = {2, 2, -2}, .color = {1, 1, 1, 1}, .normal = {0, -1, 0}, .uv = {1, 0}},
        {.position = {2, 2, 2}, .color = {1, 1, 1, 1}, .normal = {0, -1, 0}, .uv = {1, 1}},
        {.position = {-2, 2, 2}, .color = {1, 1, 1, 1}, .normal = {0, -1, 0}, .uv = {0, 1}},
    };
    af_mesh_data emitter_mesh = {emitter_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "emitter", &emitter_mesh))
        return false;
    f->emitter = af_scene_add_object(f->scene, "Emitter", "emitter");
    af_material emitter_mat = af_material_default();
    emitter_mat.base_color = (af_color){1, 0, 0, 1};
    emitter_mat.emission = (af_vec3){1, 0, 0};
    emitter_mat.emission_strength = 10.0f;
    emitter_mat.unlit = false;
    if (!af_scene_set_object_material(f->scene, f->emitter, emitter_mat))
        return false;

    /* Sunlit wall on the +X/-Z side. GI rays from the floor bounce off it. */
    static const af_vertex wall_vertices[] = {
        {.position = {-1, 0, -3}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 0}},
        {.position = {3, 0, -3}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 0}},
        {.position = {3, 5, -3}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 1}},
        {.position = {-1, 5, -3}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 1}},
    };
    af_mesh_data wall_mesh = {wall_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "wall", &wall_mesh))
        return false;
    f->wall = af_scene_add_object(f->scene, "Sun wall", "wall");
    af_material wall_mat = af_material_default();
    wall_mat.physically_based = true;
    wall_mat.base_color = (af_color){1, 1, 1, 1};
    wall_mat.roughness = 0.9f;
    if (!af_scene_set_object_material(f->scene, f->wall, wall_mat) ||
        !af_scene_set_object_visible(f->scene, f->wall, false))
        return false;

    /* Thin vertical pane at X = -1.5 between the sunlit wall and the sun. */
    static const af_vertex blocker_vertices[] = {
        {.position = {-1.5f, 0, -5}, .color = {1, 1, 1, 1}, .normal = {1, 0, 0}, .uv = {0, 0}},
        {.position = {-1.5f, 0, 1}, .color = {1, 1, 1, 1}, .normal = {1, 0, 0}, .uv = {1, 0}},
        {.position = {-1.5f, 5, 1}, .color = {1, 1, 1, 1}, .normal = {1, 0, 0}, .uv = {1, 1}},
        {.position = {-1.5f, 5, -5}, .color = {1, 1, 1, 1}, .normal = {1, 0, 0}, .uv = {0, 1}},
    };
    af_mesh_data blocker_mesh = {blocker_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "sun_blocker", &blocker_mesh))
        return false;
    f->blocker = af_scene_add_object(f->scene, "Sun blocker", "sun_blocker");
    if (!af_scene_set_object_material(f->scene, f->blocker, wall_mat) ||
        !af_scene_set_object_visible(f->scene, f->blocker, false))
        return false;

    /* Two-segment fixture: the floor sees the diffuse wall through a gap;
       the wall can see the emissive panel, while the direct floor-to-panel
       path is blocked by the thin pane at z = 0. */
    static const af_vertex bounce_wall_vertices[] = {
        {.position = {-10, 0, -1.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 0}},
        {.position = {10, 0, -1.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 0}},
        {.position = {10, 2, -1.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 1}},
        {.position = {-10, 2, -1.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 1}},
    };
    af_mesh_data bounce_wall_mesh = {bounce_wall_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "gi_bounce_wall", &bounce_wall_mesh))
        return false;
    af_vertex tilted_wall_vertices[4];
    memcpy(tilted_wall_vertices, bounce_wall_vertices, sizeof(tilted_wall_vertices));
    for (size_t i = 0; i < 4; ++i)
        tilted_wall_vertices[i].normal = (af_vec3){0, -0.9396926f, 0.3420201f};
    af_mesh_data tilted_wall_mesh = {tilted_wall_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "gi_tilted_wall", &tilted_wall_mesh))
        return false;
    f->bounce_wall = af_scene_add_object(f->scene, "GI bounce wall", "gi_bounce_wall");
    if (!af_scene_set_object_material(f->scene, f->bounce_wall, wall_mat) ||
        !af_scene_set_object_visible(f->scene, f->bounce_wall, false))
        return false;

    static const af_vertex bounce_emitter_vertices[] = {
        {.position = {-10, 1.5f, -0.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, -1}, .uv = {0, 0}},
        {.position = {-10, 2.5f, -0.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, -1}, .uv = {0, 1}},
        {.position = {10, 2.5f, -0.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, -1}, .uv = {1, 1}},
        {.position = {10, 1.5f, -0.5f}, .color = {1, 1, 1, 1}, .normal = {0, 0, -1}, .uv = {1, 0}},
    };
    af_mesh_data bounce_emitter_mesh = {bounce_emitter_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "gi_bounce_emitter", &bounce_emitter_mesh))
        return false;
    f->bounce_emitter = af_scene_add_object(f->scene, "GI bounce emitter", "gi_bounce_emitter");
    af_material bounce_emitter_mat = af_material_default();
    bounce_emitter_mat.base_color = (af_color){1, 0, 0, 1};
    bounce_emitter_mat.emission = (af_vec3){1, 0, 0};
    bounce_emitter_mat.emission_strength = 10.0f;
    bounce_emitter_mat.unlit = false;
    if (!af_scene_set_object_material(f->scene, f->bounce_emitter, bounce_emitter_mat) ||
        !af_scene_set_object_visible(f->scene, f->bounce_emitter, false))
        return false;

    static const af_vertex bounce_blocker_vertices[] = {
        {.position = {-10, 1.0f, 0.0f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 0}},
        {.position = {10, 1.0f, 0.0f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 0}},
        {.position = {10, 1.9f, 0.0f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {1, 1}},
        {.position = {-10, 1.9f, 0.0f}, .color = {1, 1, 1, 1}, .normal = {0, 0, 1}, .uv = {0, 1}},
    };
    af_mesh_data bounce_blocker_mesh = {bounce_blocker_vertices, 4, indices, 6};
    if (!af_engine_upload_mesh(engine, "gi_bounce_blocker", &bounce_blocker_mesh))
        return false;
    f->bounce_blocker = af_scene_add_object(f->scene, "GI direct blocker", "gi_bounce_blocker");
    af_material bounce_blocker_mat = wall_mat;
    bounce_blocker_mat.base_color = (af_color){0, 0, 0, 1};
    if (!af_scene_set_object_material(f->scene, f->bounce_blocker, bounce_blocker_mat) ||
        !af_scene_set_object_visible(f->scene, f->bounce_blocker, false))
        return false;

    /* Zero direct lighting and ambient */
    af_directional_light light = af_directional_light_default();
    light.intensity = 0;
    light.ambient = 0;
    af_scene_set_light(f->scene, light);

    af_shadow_settings shadows = af_shadow_settings_default();
    shadows.enabled = false;
    af_scene_set_shadows(f->scene, shadows);

    af_render_settings settings = af_scene_get_render_settings(f->scene);
    settings.environment_enabled = false;
    settings.environment_intensity = 0.0f;
    settings.ray_traced_gi = false;
    if (!af_scene_set_render_settings(f->scene, settings))
        return false;

    af_camera camera = af_camera_default();
    camera.position = (af_vec3){0, 1.2f, 2.8f};
    camera.pitch = -0.45f;
    camera.yaw = -AF_PI / 2;
    af_engine_set_camera(engine, &camera);
    af_engine_set_clear_color(engine, (af_color){0, 0, 0, 1});
    af_engine_set_scene(engine, f->scene);
    af_engine_set_simulation_paused(engine, true);
    return true;
}

#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x)) {                                                                                                    \
            fprintf(stderr, "RTGI phase %u: %s (pixel %u/%u/%u)\n", f->phase, #x, p[0], p[1], p[2]);                   \
            f->failed = true;                                                                                          \
            af_engine_request_quit(engine);                                                                            \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

static void update(af_engine *engine, double delta, void *data) {
    (void)delta;
    fixture *f = data;
    unsigned char p[4] = {0};
    if (f->phase == 13) {
        af_local_light local = *af_scene_local_light_at(f->scene, 0);
        local.intensity += 1.0f;
        CHECK(af_scene_set_local_light(f->scene, 0, local));
    }
    unsigned capture_frame = f->phase == 11 ? 1 : 5;
    if (++f->frames < capture_frame)
        return;
    if (f->frames == capture_frame) {
        int w, h;
        af_engine_get_window_size(engine, &w, &h);
        bool wall_sample = f->phase == 4 || f->phase == 5;
        af_vec2 point = wall_sample ? (af_vec2){(float)w / 2, (float)h * 0.40f}
                                    : (af_vec2){(float)w / 2, (float)h * 0.72f};
        CHECK(af_engine_test_capture_pixel(engine, point));
        return;
    }
    CHECK(af_engine_test_read_pixel(engine, p));
    af_engine_stats stats = af_engine_get_stats(engine);
    bool hardware = stats.ray_query_supported;
    if (getenv("AFTERLIGHT_REQUIRE_RAY_QUERY"))
        CHECK(hardware);
    printf("RTGI phase %u: %u/%u/%u, hardware %d, active %d, instances %zu\n", f->phase, p[0], p[1], p[2], hardware,
           stats.ray_traced_gi_active, stats.ray_instances);

    af_render_settings settings = af_scene_get_render_settings(f->scene);
    af_material material = af_scene_get_object(f->scene, f->emitter)->material;
    af_directional_light sun = af_directional_light_default();

    switch (f->phase) {
    case 0:
        /* Phase 0: RTGI is off, floor should be dark */
        CHECK(!stats.ray_traced_gi_active);
        CHECK(p[0] < 5 && p[1] < 5 && p[2] < 5);
        /* Enable RTGI */
        settings.ray_traced_gi = true;
        settings.rtgi_intensity = 3.0f;
        settings.rtgi_samples = 4;
        settings.rtgi_ray_distance = 15.0f;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 1:
        /* Phase 1: RTGI enabled with red emitter overhead */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware) {
            CHECK(stats.ray_instances >= 1 && stats.ray_tracing_bytes > 0);
            /* Red diffuse indirect bounce onto floor */
            CHECK(p[0] > 10 && p[0] > p[1] + 8 && p[0] > p[2] + 8);
        } else {
            CHECK(p[0] < 5 && p[1] < 5 && p[2] < 5);
        }
        /* Change overhead emitter to green */
        material.base_color = (af_color){0, 1, 0, 1};
        material.emission = (af_vec3){0, 1, 0};
        material.emission_strength = 10.0f;
        CHECK(af_scene_set_object_material(f->scene, f->emitter, material));
        break;

    case 2:
        /* Phase 2: Green emitter overhead */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware) {
            /* Green diffuse indirect bounce onto floor */
            CHECK(p[1] > 10 && p[0] < 5 && p[2] < 5);
        }
        /* Turn off RTGI */
        settings.ray_traced_gi = false;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 3:
        /* Phase 3: RTGI off again, floor returns to dark */
        CHECK(!stats.ray_traced_gi_active);
        CHECK(p[0] < 5 && p[1] < 5 && p[2] < 5);
        /* Sunlit wall plus a blocker. Ray-traced shadows stay disabled, so the
           GI sun shadow ray must still see GI-masked geometry (not the shadow mask). */
        sun.direction = (af_vec3){0.8f, 0.0f, -0.6f};
        sun.color = (af_vec3){1, 1, 1};
        sun.intensity = 3.0f;
        sun.ambient = 0.0f;
        af_scene_set_light(f->scene, sun);
        CHECK(af_scene_set_object_visible(f->scene, f->emitter, false));
        CHECK(af_scene_set_object_visible(f->scene, f->wall, true));
        CHECK(af_scene_set_object_visible(f->scene, f->blocker, true));
        settings.ray_traced_gi = true;
        settings.rtgi_intensity = 2.0f;
        settings.rtgi_samples = 4;
        settings.rtgi_ray_distance = 15.0f;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 4:
        /* Phase 4: the blocker shadows the wall, so the GI sun bounce is absent. */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware) {
            f->blocked[0] = p[0];
            f->blocked[1] = p[1];
            f->blocked[2] = p[2];
            CHECK(p[0] < 8 && p[1] < 8 && p[2] < 8);
        }
        CHECK(af_scene_set_object_visible(f->scene, f->blocker, false));
        break;

    case 5:
        /* Phase 5: without the blocker the sunlit wall lights the floor. */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware) {
            CHECK(p[0] > 12);
            CHECK(p[0] > (int)f->blocked[0] + 8);
        }
        CHECK(af_scene_set_object_visible(f->scene, f->emitter, false));
        CHECK(af_scene_set_object_visible(f->scene, f->wall, false));
        CHECK(af_scene_set_object_visible(f->scene, f->blocker, false));
        CHECK(af_scene_set_object_visible(f->scene, f->bounce_wall, true));
        CHECK(af_scene_set_object_visible(f->scene, f->bounce_emitter, true));
        CHECK(af_scene_set_object_visible(f->scene, f->bounce_blocker, true));
        sun.intensity = 0.0f;
        sun.ambient = 0.0f;
        CHECK(af_scene_set_light(f->scene, sun));
        af_camera bounce_camera = af_camera_default();
        bounce_camera.position = (af_vec3){0, 1.2f, 2.8f};
        bounce_camera.yaw = -AF_PI / 2;
        bounce_camera.pitch = -0.45f;
        CHECK(af_engine_set_camera(engine, &bounce_camera));
        settings.ray_traced_gi = true;
        settings.rtgi_intensity = 3.0f;
        settings.rtgi_samples = 4;
        settings.rtgi_ray_distance = 15.0f;
        settings.rtgi_bounces = 2;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 6:
        /* The floor receives red light only when the second ray reaches the emitter. */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware)
            CHECK(p[0] > 10 && p[0] > p[1] + 8 && p[0] > p[2] + 8);
        settings.rtgi_bounces = 1;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 7:
        /* One bounce reaches only the dark wall; the emitter is occluded from the floor. */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware)
            CHECK(p[0] < 5 && p[1] < 5 && p[2] < 5);
        settings.rtgi_bounces = 2;
        settings.rtgi_emissive = false;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 8:
        /* The second-bounce emitter contributes nothing while emissive GI is disabled. */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware)
            CHECK(p[0] < 5 && p[1] < 5 && p[2] < 5);
        settings.rtgi_emissive = true;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 9:
        /* Re-enabling emissive GI restores the red indirect contribution. */
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware)
            CHECK(p[0] > 10 && p[0] > p[1] + 8 && p[0] > p[2] + 8);
        settings.rtgi_bounces = 4;
        settings.anti_aliasing = AF_AA_TAA;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;

    case 10:
        CHECK(stats.ray_traced_gi_active == hardware);
        if (hardware)
            CHECK(p[0] > 10);
        CHECK(af_scene_set_object_visible(f->scene, f->bounce_emitter, false));
        break;

    case 11: {
        /* Hiding the emitter clears the light in the first frame, including with TAA. */
        CHECK(stats.ray_traced_gi_active == hardware);
        CHECK(p[0] < 5 && p[1] < 5 && p[2] < 5);
        /* Put the emitter entirely behind the same wall used for the bounce. */
        CHECK(af_scene_set_object_mesh(f->scene, f->bounce_wall, "gi_tilted_wall"));
        af_transform behind_wall = af_transform_identity();
        behind_wall.scale = (af_vec3){1, 1.79f, -1};
        behind_wall.position = (af_vec3){0, 0.01f - 1.5f * behind_wall.scale.y, -3.0f};
        CHECK(af_scene_set_object_transform(f->scene, f->bounce_emitter, behind_wall));
        CHECK(af_scene_set_object_visible(f->scene, f->bounce_emitter, true));
        settings.rtgi_bounces = 2;
        settings.anti_aliasing = AF_AA_OFF;
        CHECK(af_scene_set_render_settings(f->scene, settings));
        break;
    }

    case 12: {
        /* The wall blocks both rays even with normals tilted 70 degrees from the face. */
        CHECK(stats.ray_traced_gi_active == hardware);
        CHECK(p[0] < 5 && p[1] < 5 && p[2] < 5);
        f->temporal_frames = af_engine_test_temporal(engine).frames;
        af_local_light local = af_local_light_default(AF_LIGHT_POINT);
        local.position = (af_vec3){30, 30, 30};
        CHECK(af_scene_add_local_light(f->scene, local));
        break;
    }

    case 13:
        /* A local light change keeps the GI reprojection history running. */
        if (hardware)
            CHECK(af_engine_test_temporal(engine).frames > f->temporal_frames);
        af_engine_request_quit(engine);
        break;
    }

    ++f->phase;
    f->frames = 0;
}

static void shutdown(af_engine *engine, void *data) {
    fixture *f = data;
    af_engine_set_scene(engine, NULL);
    af_scene_destroy(f->scene);
}

int main(void) {
    af_engine_config config = af_engine_config_default();
    config.title = "Afterlight RTGI regression";
    config.width = 480;
    config.height = 360;
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    if (!engine)
        return 1;
    fixture f = {0};
    af_game game = {.init = init, .update = update, .shutdown = shutdown, .user_data = &f};
    int result = af_engine_run(engine, &game);
    af_engine_stats stats = af_engine_get_stats(engine);
    af_engine_destroy(engine);
    return result || f.failed || f.phase != 14 || stats.validation_errors;
}

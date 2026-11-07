/* SPDX-License-Identifier: MPL-2.0 */
#include "render_test.h"
#include <afterlight/extension_vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct test_state {
    af_scene *scene;
    uint64_t changed_at, resets_before_resize;
    uint32_t width_before_resize;
    unsigned phase;
    bool capture, failed;
} test_state;

static bool init(af_engine *engine, void *data)
{
    test_state *test = data;
    af_engine_set_scene(engine, test->scene);
    af_engine_set_clear_color(engine, (af_color){.8f, .01f, .01f, 1});
    return af_engine_set_upscaler_enabled(engine, false);
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Vulkan upscaler phase %u: %s failed.\n", test->phase, #condition); \
        test->failed = true; \
        af_engine_request_quit(engine); \
        return; \
    } \
} while (0)

static void update(af_engine *engine, double delta, void *data)
{
    (void)delta;
    test_state *test = data;
    af_engine_stats stats = af_engine_get_stats(engine);
    if (stats.rendered_frames < test->changed_at + 8) return;
    if (!test->capture) {
        int width, height;
        af_engine_get_window_size(engine, &width, &height);
        CHECK(af_engine_test_capture_pixel(engine, (af_vec2){(float)width / 2, (float)height / 2}));
        test->capture = true;
        return;
    }
    unsigned char pixel[4];
    CHECK(af_engine_test_read_pixel(engine, pixel));
    bool upscale = test->phase == 1 || test->phase == 2 || test->phase == 5;
    CHECK(stats.effective_aa == (upscale ? AF_AA_OFF : AF_AA_FXAA));
    CHECK(upscale ? pixel[1] > 200 && pixel[0] < 40 : pixel[0] > 200 && pixel[1] < 40);

    af_temporal_test_stats temporal = af_engine_test_temporal(engine);
    switch (test->phase) {
    case 0:
        CHECK(af_engine_set_upscaler_enabled(engine, true));
        break;
    case 1:
        test->width_before_resize = temporal.width;
        test->resets_before_resize = temporal.resets;
        CHECK(af_engine_set_window_size(engine, 800, 450));
        break;
    case 2:
        CHECK(temporal.width > test->width_before_resize && temporal.resets > test->resets_before_resize);
        CHECK(af_engine_set_upscaler_enabled(engine, false));
        break;
    case 3: {
        af_rect inset = {40, 40, 400, 300};
        af_engine_set_viewport(engine, &inset);
        CHECK(af_engine_set_upscaler_enabled(engine, true));
        break;
    }
    case 4:
        af_engine_set_viewport(engine, NULL);
        CHECK(af_engine_set_upscaler_enabled(engine, false));
        CHECK(af_engine_set_upscaler_enabled(engine, true));
        break;
    case 5:
        puts("Vulkan upscaler output, resize reset, native AA, decline and retry passed.");
        af_engine_request_quit(engine);
        break;
    }
    ++test->phase;
    test->changed_at = stats.rendered_frames;
    test->capture = false;
}

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    af_engine_config config = af_engine_config_default();
    config.width = 640;
    config.height = 360;
    config.enable_validation = true;
    af_engine *engine = af_engine_create(&config);
    if (!engine) return 1;
    if (!af_engine_test_temporal(engine).supported) {
        af_engine_destroy(engine);
        return 77;
    }
    const char *validation = getenv("AFTERLIGHT_REQUIRE_VALIDATION");
    test_state test = {.scene = af_scene_create()};
    af_render_settings settings = af_render_settings_default();
    settings.tone_mapping = false;
    settings.anti_aliasing = AF_AA_FXAA;
    settings.bloom = true;
    settings.bloom_threshold = 2;
    int result = 1;
    if ((!validation || strcmp(validation, "1") || af_engine_get_stats(engine).validation_enabled) &&
        test.scene && af_scene_set_render_settings(test.scene, settings) &&
        af_engine_load_extension(engine, argv[1])) {
        af_game game = {.init = init, .update = update, .user_data = &test};
        result = af_engine_run(engine, &game) || test.failed || test.phase != 6;
    }
    af_engine_destroy(engine);
    af_scene_destroy(test.scene);
    return result;
}

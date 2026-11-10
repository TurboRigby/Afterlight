/* SPDX-License-Identifier: MPL-2.0 */
#include "world.h"
#include "gui.h"
#include "render_test.h"
#include <afterlight/extension.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum screen { ARRIVAL, WALK, OPTICS, PLATE, KEYPAD, NOTE, PLAYBACK, DECISION, ENDING, PAUSED } screen;
typedef enum station { NONE, VALVE, SIGHT, KEYS, ARCHIVE, MANIFEST, CARBON } station;

typedef struct page {
    const char *tag, *title, *body;
} page;
static const page arrival = {
    "CINDER ESTUARY / 02:13", "LOW WATER",
    "The city says Ward Nine was empty when it opened the floodgates. Five years later, a distress call from that "
    "night just came through your radio. It came from here. At dawn the station erases its last recording."};
static const page manifest = {
    "INTAKE / ANNOTATED MANIFEST", "Someone counted",
    "The evacuation sheet records zero residents. Under it, in pencil, are six marks and the words ON THE ROOF. "
    "Someone tried to erase the pencil. In the paper, you can still feel each stroke."};
static const page carbon = {
    "SERVICE BAY / CARBON COPY", "A place to go",
    "The last line on the transfer slip is a forwarding address. Iona used it to get two families out of the ward "
    "after the flood. A note beneath it says KEEP THEIR NAMES OFF THE PUBLIC BAND."};
static const page testimony[] = {
    {"MASTER ROLL / 02:13:08", "The order",
     "CONTROL: Lower the east gate.\nIONA: I have six on the roof and people on every landing. Give me twelve "
     "minutes.\n"
     "CONTROL: The public record shows nobody there. Lower the gate."},
    {"MASTER ROLL / 02:15:31", "Twelve minutes",
     "IONA: I pulled the breaker. We got them onto the night ferry. Some of them are still in hiding. "
     "The master roll will prove what Control did. It will also tell Control who got out."},
    {"MASTER ROLL / ADDENDUM", "The last witness",
     "IONA: I routed this call to the next keeper. That must be you. I put the order on its own channel so you can "
     "send it without our names. I leave the switch with you."}};
static const page outcomes[] = {
    {"TRANSMISSION / OPEN BAND", "They heard the whole roll",
     "By sunrise the city knows who gave the order. On the night ferry, a woman hears her own name read back over "
     "the radio. She takes her coat and moves seats. You have given her proof and taken away her hiding place."},
    {"TRANSMISSION / SEALED BAND", "They heard the order",
     "The city hears Control ask for the gate to be lowered. The ward stays unnamed. At sunrise a woman on the night "
     "ferry writes to the inquiry herself. For the first time in five years, she decides who gets to hear her name."}};
static const char access_code[] = "629";
static const af_query_filter geometry = {.mask = UINT32_MAX};

typedef struct game {
    tide_world world;
    af_character walker;
    af_character_config controller;
    af_camera camera;
    ed_gui gui;
    screen screen;
    station note;
    const char *radio;
    double radio_left, ambience_left;
    float shutter, gate;
    unsigned line, cursor, choice, ending;
    char digits[4];
    bool pump, lock_open, tried_code, failed, raster, cinematic, verify, verify_ray;
    unsigned verify_step, verify_wait;
    uint64_t frame_limit;
    const char *capture_path;
} game;

static float clamp(float value, float low, float high) {
    return fminf(high, fmaxf(low, value));
}

static bool key(af_engine *e, af_key k) {
    return af_engine_key_pressed(e, k);
}

static void camera(game *g, af_engine *e) {
    if (g->screen == OPTICS || g->screen == PLATE) {
        af_engine_set_camera(e, &tide_optics);
        return;
    }
    g->camera.position = g->walker.position;
    g->camera.position.y += .69f;
    af_engine_set_camera(e, &g->camera);
}

static void radio(game *g, const char *message) {
    g->radio = message;
    g->radio_left = 14;
}

static void reset(game *g, af_engine *e) {
    g->walker = (af_character){.position = tide_spawn};
    g->camera = af_camera_default();
    g->camera.near_clip = .075f;
    g->camera.far_clip = 95;
    g->camera.vertical_fov = .98f;
    g->camera.pitch = -.055f;
    g->screen = ARRIVAL;
    g->pump = g->lock_open = g->tried_code = false;
    g->shutter = g->gate = 0;
    g->radio_left = g->ambience_left = 0;
    g->ending = 0;
    if (!tide_world_pump(&g->world, false) || !tide_world_archive(&g->world, false) ||
        !tide_world_transmit(&g->world, 0))
        g->failed = true;
    tide_world_animate(&g->world, 0, 0);
    af_engine_set_mouse_capture(e, false);
    af_engine_stop_sounds(e);
    camera(g, e);
    af_engine_reset_interpolation(e);
}

static void start(game *g, af_engine *e) {
    g->screen = WALK;
    if (!g->verify)
        af_engine_set_mouse_capture(e, true);
    radio(g, "IONA / If you got my call, turn the red valve. The water will show you how to get to the master roll.");
}

static af_vec3 anchor(station id) {
    switch (id) {
    case VALVE:
        return tide_valve;
    case SIGHT:
        return tide_sight;
    case KEYS:
        return tide_keys;
    case ARCHIVE:
        return tide_archive;
    case MANIFEST:
        return tide_manifest;
    case CARBON:
        return tide_slip;
    default:
        return (af_vec3){0};
    }
}

static station target(const game *g) {
    if (g->pump && fabsf(g->walker.position.x) < .79f && fabsf(g->walker.position.z - tide_sight.z) < .54f)
        return SIGHT;
    const station sites[] = {VALVE, KEYS, ARCHIVE, MANIFEST, CARBON};
    af_vec3 forward = af_camera_forward(&g->camera);
    float nearest = 2.15f;
    station result = NONE;
    for (size_t i = 0; i < sizeof(sites) / sizeof(sites[0]); ++i) {
        station id = sites[i];
        if ((id == VALVE && g->pump) || (id == KEYS && g->lock_open) ||
            ((id == ARCHIVE || id == CARBON) && !g->lock_open))
            continue;
        af_vec3 delta = af_vec3_sub(anchor(id), g->camera.position);
        float length = af_vec3_length(delta);
        if (length > .001f && length < nearest && af_vec3_dot(delta, forward) > .61f * length) {
            nearest = length;
            result = id;
        }
    }
    return result;
}

static void interact(game *g, af_engine *e, station id) {
    switch (id) {
    case VALVE:
        g->pump = true;
        g->failed = !tide_world_pump(&g->world, true);
        af_engine_play_sound(e, "relay", .62f);
        radio(g, "IONA / Follow the blue mark. Keep your eyes below the baffle. Read the three figures left to right.");
        break;
    case SIGHT:
        g->screen = OPTICS;
        af_engine_reset_interpolation(e);
        break;
    case KEYS:
        if (g->pump) {
            memcpy(g->digits, "000", sizeof(g->digits));
            g->cursor = 0;
            g->tried_code = false;
            g->screen = KEYPAD;
        } else
            radio(g, "LOCK 03 / Optical circuit offline. Restore flow at the intake valve.");
        break;
    case ARCHIVE:
        g->line = 0;
        g->choice = 1;
        g->screen = PLAYBACK;
        af_engine_play_sound(e, "relay", .46f);
        break;
    case MANIFEST:
    case CARBON:
        g->note = id;
        g->screen = NOTE;
        break;
    case NONE:
        break;
    }
}

static void keypad(game *g, af_engine *e, af_key input) {
    switch (input) {
    case AF_KEY_LEFT:
        g->cursor = (g->cursor + 2) % 3;
        break;
    case AF_KEY_RIGHT:
        g->cursor = (g->cursor + 1) % 3;
        break;
    case AF_KEY_UP:
    case AF_KEY_DOWN: {
        int digit = g->digits[g->cursor] - '0';
        g->digits[g->cursor] = (char)('0' + (digit + (input == AF_KEY_UP ? 1 : 9)) % 10);
        g->tried_code = false;
        break;
    }
    case AF_KEY_ENTER:
        if (strcmp(g->digits, access_code)) {
            g->tried_code = true;
            break;
        }
        g->lock_open = true;
        g->failed = !tide_world_archive(&g->world, true);
        g->screen = WALK;
        af_engine_play_sound(e, "relay", .72f);
        radio(g, "IONA / Follow the catwalk around the basin. The master roll is on the far side of the gate.");
        break;
    default:
        break;
    }
}

static void advance(game *g, af_engine *e) {
    switch (g->screen) {
    case ARRIVAL:
        start(g, e);
        break;
    case OPTICS:
    case NOTE:
        g->screen = WALK;
        af_engine_reset_interpolation(e);
        break;
    case PLATE:
        g->screen = OPTICS;
        break;
    case PLAYBACK:
        if (++g->line == sizeof(testimony) / sizeof(testimony[0]))
            g->screen = DECISION;
        break;
    case DECISION:
        g->ending = g->choice + 1;
        g->failed = !tide_world_transmit(&g->world, g->ending);
        g->screen = ENDING;
        af_engine_set_mouse_capture(e, false);
        af_engine_play_sound(e, "carrier", .68f);
        break;
    case PAUSED:
        g->screen = WALK;
        af_engine_set_mouse_capture(e, true);
        break;
    default:
        break;
    }
}

static void controls(game *g, af_engine *e) {
    bool use = key(e, AF_KEY_E), enter = key(e, AF_KEY_ENTER), esc = key(e, AF_KEY_ESCAPE);
    if (g->screen == WALK) {
        if (esc) {
            g->screen = PAUSED;
            af_engine_set_mouse_capture(e, false);
            return;
        }
        if (!af_engine_mouse_captured(e) && af_engine_mouse_pressed(e, AF_MOUSE_LEFT))
            af_engine_set_mouse_capture(e, true);
        if (af_engine_mouse_captured(e))
            af_camera_rotate(&g->camera, af_engine_mouse_delta(e).x * .0024f, -af_engine_mouse_delta(e).y * .0024f);
        if (use)
            interact(g, e, target(g));
        return;
    }
    if (g->screen == KEYPAD) {
        if (esc) {
            g->screen = WALK;
            return;
        }
        const af_key keys[] = {AF_KEY_LEFT, AF_KEY_RIGHT, AF_KEY_UP, AF_KEY_DOWN, AF_KEY_ENTER};
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
            if (key(e, keys[i]))
                keypad(g, e, keys[i]);
        return;
    }
    if (g->screen == ENDING || g->screen == PAUSED) {
        if (key(e, AF_KEY_R))
            reset(g, e);
        else if (key(e, AF_KEY_Q) || (esc && g->screen == ENDING))
            af_engine_request_quit(e);
        else if (g->screen == PAUSED && (esc || enter || use))
            advance(g, e);
        return;
    }
    if (g->screen == OPTICS && key(e, AF_KEY_F)) {
        g->screen = PLATE;
        return;
    }
    if (g->screen == DECISION && (key(e, AF_KEY_LEFT) || key(e, AF_KEY_RIGHT)))
        g->choice = 1 - g->choice;
    if (esc && (g->screen == PLAYBACK || g->screen == DECISION))
        g->screen = WALK;
    else if (enter || use || (esc && g->screen != ARRIVAL && g->screen != DECISION))
        advance(g, e);
}

static void fixed(af_engine *e, double dt, void *data) {
    game *g = data;
    if (g->screen == PAUSED)
        return;
    float delta = (float)dt;
    if (g->screen == WALK && !g->verify) {
        bool captured = af_engine_mouse_captured(e);
        float f = captured ? (float)af_engine_key_down(e, AF_KEY_W) - (float)af_engine_key_down(e, AF_KEY_S) : 0;
        float r = captured ? (float)af_engine_key_down(e, AF_KEY_D) - (float)af_engine_key_down(e, AF_KEY_A) : 0;
        float y = g->camera.yaw;
        af_character_input in = {.move = {cosf(y) * f - sinf(y) * r, 0, sinf(y) * f + cosf(y) * r}};
        af_character_config config = g->controller;
        if (af_engine_key_down(e, AF_KEY_LEFT_SHIFT))
            config.speed *= 1.38f;
        g->failed = !af_character_step(g->world.scene, &g->walker, config, in, delta, geometry);
        if (g->walker.position.y < -2.5f) {
            g->walker = (af_character){.position = tide_spawn};
            af_engine_reset_interpolation(e);
        }
    }
    float shutter = clamp(g->shutter + (g->pump ? delta * .64f : 0), 0, 1);
    float gate = clamp(g->gate + (g->lock_open ? delta * .74f : 0), 0, 1);
    if (shutter != g->shutter || gate != g->gate) {
        g->shutter = shutter;
        g->gate = gate;
        tide_world_animate(&g->world, shutter, gate);
    }
}

typedef struct ink {
    struct nk_command_buffer *canvas;
    const struct nk_user_font *fonts;
    struct nk_color text, quiet, aqua, amber, panel, rule;
} ink;
static const float sizes[] = {13, 14, 15, 16, 17, 19, 27, 32, 41};

static const struct nk_user_font *sized(const ink *i, float size) {
    for (size_t n = 0; n < sizeof(sizes) / sizeof(sizes[0]); ++n)
        if (sizes[n] == size)
            return &i->fonts[n];
    return &i->fonts[4];
}

static void rect(ink *i, float x, float y, float w, float h, struct nk_color color) {
    nk_fill_rect(i->canvas, nk_rect(x, y, w, h), 0, color);
}

static void text(ink *i, float x, float y, float w, float size, const char *label, struct nk_color color) {
    nk_draw_text(i->canvas, nk_rect(x, y, w, size + 3), label, (int)strlen(label), sized(i, size), nk_rgba(0, 0, 0, 0),
                 color);
}

static void wrap(ink *i, float x, float y, float width, float size, const char *body, struct nk_color color) {
    const struct nk_user_font *font = sized(i, size);
    while (*body) {
        const char *line = body, *end = body;
        while (*end && *end != '\n') {
            const char *word = end + 1;
            while (*word && *word != ' ' && *word != '\n')
                ++word;
            if (end > line && font->width(font->userdata, size, line, (int)(word - line)) > width)
                break;
            end = word;
        }
        nk_draw_text(i->canvas, nk_rect(x, y, width, size + 3), line, (int)(end - line), font, nk_rgba(0, 0, 0, 0),
                     color);
        y += size + 7;
        body = end;
        while (*body == ' ' || *body == '\n')
            ++body;
    }
}

static void dossier(ink *i, float x, float y, float w, float h, page p, const char *footer) {
    rect(i, x, y, w, h, i->panel);
    rect(i, x, y, 3, h, i->aqua);
    text(i, x + 25, y + 19, w - 50, 14, p.tag, i->aqua);
    text(i, x + 25, y + 44, w - 50, 32, p.title, i->text);
    rect(i, x + 25, y + 88, w - 50, 1, i->rule);
    wrap(i, x + 25, y + 103, w - 50, w >= 540 ? 19 : 16, p.body, i->text);
    text(i, x + 25, y + h - 34, w - 50, 14, footer, i->quiet);
}

static const char *prompt(station id) {
    switch (id) {
    case VALVE:
        return "E   Open the intake valve";
    case SIGHT:
        return "E   Use the water sight";
    case KEYS:
        return "E   Inspect optical lock";
    case ARCHIVE:
        return "E   Play the master roll";
    case MANIFEST:
        return "E   Read the evacuation manifest";
    case CARBON:
        return "E   Read the carbon copy";
    default:
        return "";
    }
}

static void hud(game *g, ink *i, float w, float h, const af_engine_stats *stats) {
    const char *chapter = g->lock_open ? "03 / THE MASTER ROLL" : g->pump ? "02 / THE WATER SIGHT" : "01 / THE INTAKE";
    const char *task = g->lock_open ? "Find the recorder beyond the lift gate"
                       : g->pump    ? "Read the water sight, then use the keypad"
                                    : "Restore flow at the red valve";
    rect(i, 23, 24, fminf(374, w - 46), 65, i->panel);
    rect(i, 23, 24, 3, 65, i->aqua);
    text(i, 38, 34, 340, 14, chapter, i->aqua);
    text(i, 38, 57, 340, 17, task, i->text);
    if (w >= 730)
        text(i, w - 217, 32, 193, 13,
             stats->ray_traced_water_reflections_active ? "RTGI / SHADOWS / REFLECTIONS"
             : stats->ray_traced_gi_active              ? "RTGI / RAY SHADOWS"
                                                        : "STATION / SERVICE LIGHTING",
             i->quiet);
    float cx = w * .5f, cy = h * .5f;
    rect(i, cx - 1, cy - 1, 3, 3, i->text);
    station focus = target(g);
    if (focus != NONE) {
        rect(i, cx - 145, cy + 22, 290, 33, i->panel);
        text(i, cx - 131, cy + 29, 263, 17, prompt(focus), i->aqua);
    }
    if (g->radio_left > 0) {
        float width = fminf(w - 46, 712);
        float x = (w - width) * .5f;
        rect(i, x, h - 117, width, 66, i->panel);
        wrap(i, x + 14, h - 106, width - 28, 17, g->radio, i->text);
    }
    text(i, 25, h - 36, w - 50, 14, "WASD  Move     Mouse  Look     E  Use     Esc  Pause", i->quiet);
}

static void show_keypad(game *g, ink *i, float w, float h) {
    float x = (w - 420) * .5f, y = (h - 277) * .5f;
    rect(i, x, y, 420, 277, i->panel);
    rect(i, x, y, 3, 277, i->aqua);
    text(i, x + 24, y + 22, 365, 14, "WARD NINE ARCHIVE / INTERLOCK 03", i->aqua);
    text(i, x + 24, y + 51, 365, 27, "Optical calibration", i->text);
    for (unsigned slot = 0; slot < 3; ++slot) {
        float dx = x + 95 + (float)slot * 81;
        struct nk_color color = slot == g->cursor ? i->aqua : i->quiet;
        rect(i, dx, y + 106, 66, 72, i->rule);
        rect(i, dx, y + 176, 66, 2, color);
        const char digit[] = {g->digits[slot], 0};
        text(i, dx + 20, y + 114, 42, 41, digit, color);
    }
    text(i, x + 24, y + 192, 368, 16,
         g->tried_code ? "Code rejected. Read the water from left to right."
                       : "Enter the three digits reflected in the basin.",
         g->tried_code ? i->amber : i->text);
    text(i, x + 24, y + 236, 368, 14, "Arrows  Set digits     Enter  Submit     Esc  Leave", i->quiet);
}

static void show_decision(game *g, ink *i, float w, float h) {
    float width = fminf(w - 40, 680), x = (w - width) * .5f, y = (h - 338) * .5f;
    rect(i, x, y, width, 338, i->panel);
    rect(i, x, y, 3, 338, i->amber);
    text(i, x + 25, y + 21, width - 50, 14, "CARRIER LINK / READY", i->amber);
    text(i, x + 25, y + 50, width - 50, 32, "What will you transmit?", i->text);
    const char *titles[] = {"OPEN BAND / Send the entire roll", "SEALED BAND / Send the order alone"};
    const char *subtitles[] = {"Publish the evidence and the survivors' names.",
                               "Keep the names sealed and send the order to the inquiry."};
    for (unsigned choice = 0; choice < 2; ++choice) {
        float row = y + 107 + 87 * (float)choice;
        struct nk_color color = g->choice == choice ? i->aqua : i->quiet;
        rect(i, x + 25, row, width - 50, 73, i->rule);
        rect(i, x + 25, row, 3, 73, color);
        text(i, x + 39, row + 8, width - 74, 19, titles[choice], color);
        wrap(i, x + 39, row + 37, width - 74, 16, subtitles[choice], i->text);
    }
    text(i, x + 25, y + 300, width - 50, 14, "Left / Right  Choose     Enter  Transmit     Esc  Step back", i->quiet);
}

static bool draw(game *g, af_engine *e, const af_engine_stats *stats) {
    int width, height;
    af_engine_get_window_size(e, &width, &height);
    if (width <= 0 || height <= 0)
        return true;
    float w = (float)width, h = (float)height;
    struct nk_context *ctx = &g->gui.context;
    struct nk_user_font fonts[sizeof(sizes) / sizeof(sizes[0])];
    for (size_t n = 0; n < sizeof(sizes) / sizeof(sizes[0]); ++n) {
        fonts[n] = *ctx->style.font;
        fonts[n].height = sizes[n];
    }
    ed_gui_input(&g->gui, e);
    nk_style_push_style_item(ctx, &ctx->style.window.fixed_background, nk_style_item_hide());
    if (nk_begin(ctx, "Low Water interface", nk_rect(0, 0, w, h), NK_WINDOW_NO_INPUT | NK_WINDOW_NO_SCROLLBAR)) {
        ink i = {nk_window_get_canvas(ctx), fonts,
                 nk_rgb(234, 240, 238),     nk_rgb(169, 191, 191),
                 nk_rgb(106, 233, 212),     nk_rgb(255, 185, 112),
                 nk_rgba(10, 22, 29, 237),  nk_rgba(63, 86, 92, 162)};
        float card = fminf(w - 44, 660), x = (w - card) * .5f;
        switch (g->screen) {
        case ARRIVAL:
            rect(&i, 0, 0, w, h, nk_rgba(3, 10, 14, 105));
            dossier(&i, x, (h - 289) * .5f, card, 289, arrival, "Enter / E  Descend to the intake");
            break;
        case WALK:
            hud(g, &i, w, h, stats);
            break;
        case OPTICS:
            rect(&i, 23, 24, 289, 37, i.panel);
            text(&i, 37, 33, 262, 15, "WATER SIGHT / READ LEFT TO RIGHT", i.aqua);
            rect(&i, 23, h - 65, fminf(w - 46, 561), 39, i.panel);
            text(&i, 37, h - 53, w - 74, 15, "E  Step back     F  Read the service plate", i.quiet);
            break;
        case PLATE:
            dossier(&i, x, (h - 256) * .5f, card, 256,
                    (page){"OPTICAL SERVICE / EMERGENCY BYPASS", "Stamped calibration",
                           "If the sight floods or the baffle sticks, read the punch marks left to right. "
                           "First group, six. Second group, two. Third group, nine."},
                    "Enter / E  Return to the sight");
            break;
        case KEYPAD:
            show_keypad(g, &i, w, h);
            break;
        case NOTE:
            dossier(&i, x, h - 284, card, 249, g->note == MANIFEST ? manifest : carbon, "Enter / E  Put it back");
            break;
        case PLAYBACK:
            dossier(&i, x, h - 300, card, 265, testimony[g->line],
                    g->line + 1 == sizeof(testimony) / sizeof(testimony[0]) ? "Enter / E  Arm the transmitter"
                                                                            : "Enter / E  Continue playback");
            break;
        case DECISION:
            show_decision(g, &i, w, h);
            break;
        case ENDING:
            rect(&i, 0, 0, w, h, nk_rgba(3, 10, 14, 76));
            dossier(&i, x, (h - 299) * .5f, card, 299, outcomes[g->ending - 1],
                    "END OF CHAPTER     R  Replay     Esc  Exit");
            break;
        case PAUSED:
            dossier(&i, x, (h - 251) * .5f, card, 251,
                    (page){"LOW WATER", "Transmission paused",
                           "WASD to move. Use the mouse to look and E to interact. Shift moves faster."},
                    "Enter / Esc  Resume     R  Restart chapter     Q  Quit");
            break;
        }
    }
    nk_end(ctx);
    nk_style_pop_style_item(ctx);
    return ed_gui_render(&g->gui, e, w, h);
}

static bool init(af_engine *e, void *data) {
    game *g = data;
    const char *base = af_engine_get_base_path(e);
    char assets[AF_ASSET_PATH_CAPACITY], font[AF_ASSET_PATH_CAPACITY];
    if (!base || snprintf(assets, sizeof(assets), "%sassets", base) >= (int)sizeof(assets) ||
        snprintf(font, sizeof(font), "%sFiraSans-Regular.ttf", base) >= (int)sizeof(font) ||
        !ed_gui_init(&g->gui, e, font)) {
        fprintf(stderr, "Low Water: could not initialize interface resources from %s.\n", base ? base : "(unknown)");
        return false;
    }
    if (!tide_world_build(&g->world, e, assets, !g->raster, g->cinematic)) {
        fputs("Low Water: could not construct the station.\n", stderr);
        return false;
    }
    const char *sounds[] = {"room", "relay", "carrier"};
    for (size_t i = 0; i < sizeof(sounds) / sizeof(sounds[0]); ++i) {
        char path[AF_ASSET_PATH_CAPACITY];
        af_asset_error error = {0};
        if (snprintf(path, sizeof(path), "%s/%s.wav", assets, sounds[i]) >= (int)sizeof(path) ||
            !af_engine_load_sound(e, sounds[i], path, &error)) {
            fprintf(stderr, "Low Water audio: %s\n", error.message);
            return false;
        }
    }
    af_engine_set_scene(e, g->world.scene);
    af_engine_set_clear_color(e, (af_color){.018f, .037f, .065f, 1});
    g->controller = af_character_config_default();
    g->controller.radius = .27f;
    g->controller.speed = 2.8f;
    reset(g, e);
    return !g->failed;
}

static bool walk_path(game *g, const af_vec3 *points, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        for (unsigned tick = 0;; ++tick) {
            af_vec3 diff = af_vec3_sub(points[i], g->walker.position);
            diff.y = 0;
            if (af_vec3_length(diff) < .07f)
                break;
            af_character_input input = {.move = af_vec3_normalize(diff)};
            if (tick == 600 ||
                !af_character_step(g->world.scene, &g->walker, g->controller, input, 1.f / 60, geometry)) {
                fprintf(stderr, "Blocked verification route at %.2f %.2f %.2f toward %.2f %.2f\n", g->walker.position.x,
                        g->walker.position.y, g->walker.position.z, points[i].x, points[i].z);
                return false;
            }
        }
    }
    return true;
}

static bool aim(game *g, af_engine *e, station id) {
    camera(g, e);
    af_vec3 diff = af_vec3_sub(anchor(id), g->camera.position);
    g->camera.yaw = atan2f(diff.z, diff.x);
    g->camera.pitch = atan2f(diff.y, hypotf(diff.x, diff.z));
    return target(g) == id;
}

static bool capture_optics(game *g, af_engine *e) {
    static unsigned char pixels[1024 * 1024 * 4];
    uint32_t w, h;
    if (!af_engine_test_read_pixels(e, pixels, sizeof(pixels), &w, &h))
        return false;
    if (g->capture_path) {
        FILE *file = fopen(g->capture_path, "wb");
        if (!file)
            return false;
        fprintf(file, "P6\n%u %u\n255\n", w, h);
        for (size_t i = 0; i < (size_t)w * h; ++i)
            fwrite(pixels + 4 * i, 1, 3, file);
        if (fclose(file) != 0)
            return false;
    }
    return true;
}

#define VERIFY(condition)                                                                                              \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            fprintf(stderr, "Low Water verification step %u: %s\n", g->verify_step, #condition);                       \
            g->failed = true;                                                                                          \
            return;                                                                                                    \
        }                                                                                                              \
    } while (0)

static void verify(game *g, af_engine *e, const af_engine_stats *stats) {
    if (stats->rendered_frames < 4)
        return;
    if (g->verify_step == 0) {
        VERIFY(stats->validation_errors == 0 && stats->missing_meshes == 0 && stats->missing_textures == 0);
        if (g->verify_ray)
            VERIFY(stats->ray_traced_gi_active && stats->ray_traced_shadows_active &&
                   (!g->cinematic || stats->ray_traced_material_effects_active));
        start(g, e);
        const af_vec3 route[] = {{-2.53f, 0, -1.83f}};
        VERIFY(walk_path(g, route, 1) && aim(g, e, VALVE));
        interact(g, e, VALVE);
        VERIFY(g->pump);
        const af_vec3 approach[] = {{-1.22f, 0, -4.44f}, tide_sight};
        VERIFY(walk_path(g, approach, 2) && target(g) == SIGHT);
        interact(g, e, SIGHT);
        VERIFY(g->screen == OPTICS);
    } else if (g->verify_step == 1) {
        if (g->shutter < 1 || ++g->verify_wait < 18)
            return;
        if (g->verify_ray)
            VERIFY(stats->ray_traced_water_reflections_active);
        af_temporal_test_stats t = af_engine_test_temporal(e);
        int logical_w, logical_h;
        af_engine_get_window_size(e, &logical_w, &logical_h);
        uint32_t w = t.width > 1024 ? 1024 : t.width, h = t.height > 1024 ? 1024 : t.height;
        af_vec2 center = {
            .x = t.width ? (float)(t.width - w) * (float)logical_w / (2.f * (float)t.width) : 0,
            .y = t.height ? (float)(t.height - h) * (float)logical_h / (2.f * (float)t.height) : 0,
        };
        VERIFY(af_engine_test_capture_region(e, center, w, h));
    } else if (g->verify_step == 2) {
        VERIFY(capture_optics(g, e));
        g->screen = PLATE;
        advance(g, e);
        VERIFY(g->screen == OPTICS);
        advance(g, e);
        const af_vec3 keys[] = {{2.65f, 0, -4.75f}};
        VERIFY(walk_path(g, keys, 1) && aim(g, e, KEYS));
        interact(g, e, KEYS);
        VERIFY(g->screen == KEYPAD);
        keypad(g, e, AF_KEY_ENTER);
        VERIFY(g->tried_code && !g->lock_open);
        for (unsigned i = 0; i < 3; ++i) {
            for (int digit = 0; digit < access_code[i] - '0'; ++digit)
                keypad(g, e, AF_KEY_UP);
            keypad(g, e, AF_KEY_RIGHT);
        }
        keypad(g, e, AF_KEY_ENTER);
        VERIFY(g->lock_open && g->screen == WALK);
    } else if (g->verify_step == 3) {
        if (g->gate < 1)
            return;
        const af_vec3 route[] = {{3.58f, 0, -5.28f}, {3.58f, 0, -13.63f}, {0, 0, -13.63f}, {0, 0, -20.65f}};
        VERIFY(walk_path(g, route, 4) && aim(g, e, ARCHIVE));
        interact(g, e, ARCHIVE);
        for (size_t i = 0; i < sizeof(testimony) / sizeof(testimony[0]); ++i)
            advance(g, e);
        VERIFY(g->screen == DECISION);
        advance(g, e);
        VERIFY(g->screen == ENDING && g->ending == 2);
        interact(g, e, ARCHIVE);
        for (size_t i = 0; i < sizeof(testimony) / sizeof(testimony[0]); ++i)
            advance(g, e);
        g->choice = 0;
        advance(g, e);
        VERIFY(g->screen == ENDING && g->ending == 1);
    } else {
        VERIFY(stats->validation_errors == 0 && !stats->ray_tracing_failed);
        puts("Low Water: walked the level, opened the pump, read the sight, rejected the wrong code, opened the "
             "archive, verified both endings.");
        af_engine_request_quit(e);
        return;
    }
    ++g->verify_step;
}
#undef VERIFY

static void update(af_engine *e, double dt, void *data) {
    game *g = data;
    af_engine_stats stats = af_engine_get_stats(e);
    if (g->verify)
        verify(g, e, &stats);
    else
        controls(g, e);
    if (g->screen != PAUSED) {
        g->radio_left = fmax(0, g->radio_left - dt);
        g->ambience_left -= dt;
        if (g->ambience_left <= 0) {
            af_engine_play_sound(e, "room", .5f);
            g->ambience_left = 11.5;
        }
    }
    camera(g, e);
    if (!draw(g, e, &stats))
        g->failed = true;
    if (g->failed || (g->frame_limit && stats.rendered_frames >= g->frame_limit))
        af_engine_request_quit(e);
}

static void shutdown(af_engine *e, void *data) {
    game *g = data;
    af_engine_set_mouse_capture(e, false);
    ed_gui_destroy(&g->gui);
    af_engine_set_scene(e, NULL);
    af_scene_destroy(g->world.scene);
}

static bool window_size(const char *value, af_engine_config *config) {
    char *end;
    long w = strtol(value, &end, 10);
    if (*end != 'x' || w < 512 || w > 1920)
        return false;
    long h = strtol(end + 1, &end, 10);
    if (*end || h < 350 || h > 1200)
        return false;
    config->width = (int)w;
    config->height = (int)h;
    return true;
}

int main(int argc, char **argv) {
    game g = {0};
    af_engine_config config = af_engine_config_default();
    config.title = "Low Water";
    config.width = 900;
    config.height = 560;
    const char *extensions[AF_EXTENSION_LIMIT];
    size_t count = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--raster"))
            g.raster = true;
        else if (!strcmp(argv[i], "--cinematic"))
            g.cinematic = true;
        else if (!strcmp(argv[i], "--window") && i + 1 < argc) {
            if (!window_size(argv[++i], &config)) {
                fputs("Low Water: expected a window size like 720x480.\n", stderr);
                return 1;
            }
        } else if (!strcmp(argv[i], "--validation"))
            config.enable_validation = true;
        else if (!strcmp(argv[i], "--verify") || !strcmp(argv[i], "--verify-ray")) {
            g.verify = true;
            g.verify_ray = !strcmp(argv[i], "--verify-ray");
            config.enable_validation = true;
            config.width = 512;
            config.height = 350;
            g.frame_limit = 2048;
        } else if (!strcmp(argv[i], "--capture") && i + 1 < argc)
            g.capture_path = argv[++i];
        else if (!strcmp(argv[i], "--extension") && i + 1 < argc && count < AF_EXTENSION_LIMIT)
            extensions[count++] = argv[++i];
        else {
            fprintf(stderr,
                    "Usage: %s [--raster] [--cinematic] [--window WIDTHxHEIGHT] [--extension FILE] [--validation]\n"
                    "       [--verify | --verify-ray [--capture PPM]]\n",
                    argv[0]);
            return 1;
        }
    }
    af_engine *e = af_engine_create_with_extensions(&config, extensions, count);
    if (!e)
        return 1;
    af_game callbacks = {.init = init, .update = update, .fixed_update = fixed, .shutdown = shutdown, .user_data = &g};
    int result = af_engine_run(e, &callbacks);
    af_engine_stats stats = af_engine_get_stats(e);
    printf("Low Water: %" PRIu64
           " frames, ray GI %d, ray shadows %d, ray reflections %d, %.2f ms GPU, %u validation errors.\n",
           stats.rendered_frames, stats.ray_traced_gi_active, stats.ray_traced_shadows_active,
           stats.ray_traced_water_reflections_active, stats.gpu_frame_ms, stats.validation_errors);
    af_engine_destroy(e);
    return result || g.failed || stats.validation_errors || (g.verify && g.verify_step != 4);
}

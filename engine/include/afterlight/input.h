/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_INPUT_H
#define AFTERLIGHT_INPUT_H
#include <afterlight/math.h>
#include <afterlight/asset.h>
typedef enum af_key {
    AF_KEY_ESCAPE,
    AF_KEY_SPACE,
    AF_KEY_R,
    AF_KEY_W, AF_KEY_A, AF_KEY_S, AF_KEY_D,
    AF_KEY_Q, AF_KEY_E, AF_KEY_LEFT_SHIFT,
    AF_KEY_LEFT_CONTROL, AF_KEY_LEFT_SUPER, AF_KEY_ENTER, AF_KEY_TAB,
    AF_KEY_BACKSPACE, AF_KEY_DELETE, AF_KEY_LEFT, AF_KEY_RIGHT, AF_KEY_UP, AF_KEY_DOWN,
    AF_KEY_HOME, AF_KEY_END, AF_KEY_Z, AF_KEY_Y, AF_KEY_C, AF_KEY_V, AF_KEY_X,
    AF_KEY_N, AF_KEY_O, AF_KEY_F, AF_KEY_F5, AF_KEY_F6,
    AF_KEY_COUNT
} af_key;


#define AF_INPUT_MAX_ACTIONS 32u
typedef struct af_action_binding {char name[32];int negative_key,positive_key,pad_button,pad_axis,mouse_axis;float deadzone,scale;} af_action_binding;
typedef struct af_input_map {af_action_binding bindings[AF_INPUT_MAX_ACTIONS];size_t count;} af_input_map;
typedef struct af_input_sample {bool keys[AF_KEY_COUNT],pressed[AF_KEY_COUNT],buttons[32];float axes[8];af_vec2 mouse;} af_input_sample;
af_input_map af_input_map_default(void);
bool af_input_bind(af_input_map*,af_action_binding);
float af_input_value(const af_input_map*,const af_input_sample*,const char *action);
bool af_input_map_save(const char*,const af_input_map*,af_asset_error*);
bool af_input_map_load(const char*,af_input_map*,af_asset_error*);
#endif

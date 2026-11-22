/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef AF_MCP_AGENT_INTERNAL_H
#define AF_MCP_AGENT_INTERNAL_H

#include <afterlight/afterlight.h>
#include <afterlight/compound.h>
#include <afterlight/joint.h>
#include <afterlight/landscape.h>
#include <afterlight/model.h>
#include <afterlight/project.h>
#include <afterlight/water.h>
#include <cJSON.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* JSON helper utilities (defined in agent_main.c). */
cJSON *af_agent_ok(cJSON *data);
cJSON *af_agent_err(const char *message);
const char *af_agent_str(const cJSON *obj, const char *key, const char *fallback);
bool af_agent_bool(const cJSON *obj, const char *key, bool fallback);
double af_agent_num(const cJSON *obj, const char *key, double fallback);
float af_agent_f32(const cJSON *obj, const char *key, float fallback);
uint32_t af_agent_u32(const cJSON *obj, const char *key, uint32_t fallback);
int af_agent_i32(const cJSON *obj, const char *key, int fallback);
bool af_agent_has(const cJSON *obj, const char *key);
bool af_agent_vec2(const cJSON *obj, const char *key, af_vec2 *out);
bool af_agent_vec3(const cJSON *obj, const char *key, af_vec3 *out);
bool af_agent_color(const cJSON *obj, const char *key, af_color *out);
af_object_id af_agent_id(const cJSON *obj, const char *key);
af_object_id af_agent_resolve_object(const af_scene *scene, const cJSON *args);
void af_agent_copy_str(char *dst, size_t cap, const char *src);

cJSON *af_agent_json_vec2(af_vec2 v);
cJSON *af_agent_json_vec3(af_vec3 v);
cJSON *af_agent_json_color(af_color c);
cJSON *af_agent_json_id(af_object_id id);

/* Command handlers in agent_scene.c and agent_capture.c. */
cJSON *af_agent_dispatch_scene(const char *cmd, const cJSON *args);
cJSON *af_agent_cmd_capture(const cJSON *args);

#endif

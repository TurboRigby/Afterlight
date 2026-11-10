/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_STORY_INTERNAL_H
#define AF_STORY_INTERNAL_H

#include <afterlight/blueprint.h>
#include <afterlight/story.h>

bool af_story_open_dialogue(af_scene *, const af_blueprint *, uint32_t node, const af_story_dialogue *);
bool af_story_request_destination(af_scene *, const af_blueprint *, uint32_t node, const char *);
bool af_blueprint_dispatch_story(af_blueprint *, af_scene *, void *engine, uint32_t node, const char *output);

#endif

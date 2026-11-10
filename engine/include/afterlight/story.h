/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AFTERLIGHT_STORY_H
#define AFTERLIGHT_STORY_H

#include <afterlight/scene.h>

#define AF_STORY_MAX_FLAGS 64u
#define AF_STORY_FLAG_CAPACITY 32u
#define AF_STORY_TEXT_CAPACITY 512u
#define AF_STORY_LABEL_CAPACITY 64u

typedef struct af_story_flags {
    size_t count;
    char names[AF_STORY_MAX_FLAGS][AF_STORY_FLAG_CAPACITY];
} af_story_flags;

typedef struct af_story_dialogue {
    char speaker[AF_STORY_LABEL_CAPACITY];
    char text[AF_STORY_TEXT_CAPACITY];
    char choices[2][AF_STORY_LABEL_CAPACITY]; /* Empty for a Continue panel. */
} af_story_dialogue;

typedef struct af_interaction_focus {
    af_object_id object;
    const char *prompt; /* Borrowed from the scene's bound Blueprint. */
} af_interaction_focus;

typedef struct af_story_scene_change {
    char destination[AF_STORY_LABEL_CAPACITY];
    size_t graph; /* SIZE_MAX if the source is outside the scene. */
    uint32_t node;
} af_story_scene_change;

bool af_scene_story_flag(const af_scene *, const char *name);
bool af_scene_set_story_flag(af_scene *, const char *name, bool value);
const af_story_flags *af_scene_story_flags(const af_scene *);
bool af_scene_restore_story_flags(af_scene *, const af_story_flags *);
af_interaction_focus af_scene_interaction_focus(const af_scene *, const af_camera *, af_object_id viewer);
bool af_scene_interact(af_scene *, void *engine, af_object_id object);
const af_story_dialogue *af_scene_story_dialogue(const af_scene *);
bool af_scene_answer_dialogue(af_scene *, void *engine, unsigned choice);
bool af_scene_take_story_destination(af_scene *, char out[AF_STORY_LABEL_CAPACITY]);
bool af_scene_take_story_change(af_scene *, af_story_scene_change *);

#endif

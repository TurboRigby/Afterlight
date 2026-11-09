/* SPDX-License-Identifier: MPL-2.0 */
#ifndef AF_MORPH_H
#define AF_MORPH_H
#include <afterlight/animation.h>

typedef struct af_morph_delta {
    af_vec3 position, normal;
} af_morph_delta;

typedef struct af_mesh_morph {
    uint32_t target_count, source_count;
    bool has_normals;
    uint64_t used_targets;
    float defaults[AF_ANIMATION_MAX_MORPHS];
    uint32_t *source_vertices; /* Expanded triangle vertex to source vertex. */
    af_morph_delta *targets;   /* Target-major, in source vertex order. */
} af_mesh_morph;

void af_mesh_morph_free(af_mesh_morph *morph);
void af_mesh_morph_blend(const af_mesh_morph *morph, const float *weights, bool relative, af_morph_delta *out);
void af_animation_blend_pose(af_animation_pose *to, const af_animation_pose *from, float t);
#endif

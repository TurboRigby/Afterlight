/* SPDX-License-Identifier: MPL-2.0 */
#include "environment.h"
void ed_environment_preset(const af_scene *scene, bool night, af_directional_light *sun, af_render_settings *render) {
    *sun = af_scene_get_light(scene);
    *render = af_scene_get_render_settings(scene);
    sun->direction = (af_vec3){-.45f, -1, -.35f};
    sun->color = night ? (af_vec3){.25f, .4f, .8f} : (af_vec3){1, .96f, .9f};
    sun->intensity = night ? .035f : 1.8f;
    sun->ambient = night ? .003f : .08f;
    render->environment_enabled = true;
    render->skybox = true;
    render->tone_mapping = true;
    render->environment_intensity = night ? .02f : .45f;
    render->skybox_intensity = night ? .015f : .8f;
    render->exposure = night ? -1.5f : 0;
}

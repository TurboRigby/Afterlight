/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <afterlight/game_module.h>
#include <stdio.h>
static float action_value(const af_game_host *h,const char *name){return h->input_enabled?af_engine_action_value(h->engine,name):0;}
static bool action_pressed(const af_game_host *h,const char *name){return h->input_enabled&&af_engine_action_pressed(h->engine,name);}
static bool key_down(const af_game_host *host,af_key key){return host->input_enabled&&af_engine_key_down(host->engine,key);}
static bool key_pressed(const af_game_host *host,af_key key){return host->input_enabled&&af_engine_key_pressed(host->engine,key);}
static void log_message(const char *text){if(text)fprintf(stdout,"[game] %s\n",text);}
af_game_host af_game_host_create(af_scene *scene,af_engine *engine){
    return (af_game_host){.abi_version=AF_GAME_ABI_VERSION,.struct_size=sizeof(af_game_host),.scene=scene,.engine=engine,.camera_enabled=true,
        .instantiate_prefab=af_prefab_instantiate,.load_scene_assets=af_engine_load_scene_assets,.action_value=action_value,.action_pressed=action_pressed,.key_down=key_down,.object_count=af_scene_object_count,.object_at=af_scene_object_at,.get_object=af_scene_get_object,.add_object=af_scene_add_object,
        .remove_object=af_scene_remove_object,.set_transform=af_scene_set_object_transform,.set_material=af_scene_set_object_material,.set_parent=af_scene_set_parent,.log=log_message,
        .local_light_count=af_scene_local_light_count,.local_light_at=af_scene_local_light_at,.add_local_light=af_scene_add_local_light,.set_local_light=af_scene_set_local_light,.remove_local_light=af_scene_remove_local_light,.set_light=af_scene_set_light,.set_render_settings=af_scene_set_render_settings,
        .key_pressed=key_pressed,.mouse_delta=af_engine_mouse_delta,.mouse_pressed=af_engine_mouse_pressed,.mouse_in_viewport=af_engine_mouse_in_viewport,.mouse_captured=af_engine_mouse_captured,.capture_mouse=af_engine_set_mouse_capture,.set_camera=af_engine_set_camera,.reset_interpolation=af_engine_reset_interpolation,
        .set_collider=af_scene_set_object_collider,.raycast=af_scene_raycast,.overlap_capsule=af_scene_overlap_capsule,.character_config_default=af_character_config_default,.move_character=af_character_step,.world_matrix=af_scene_world_matrix,.game_camera=af_scene_game_camera,.report_gameplay=af_engine_set_gameplay_status,.play_sound_at=af_engine_play_sound_at,.play_sound=af_engine_play_sound,.animation_play=af_engine_animation_play,.animation_pause=af_engine_animation_pause,.animation_stop=af_engine_animation_stop,.animation_seek=af_engine_animation_seek,.animation_options=af_engine_animation_options,.animation_status=af_engine_animation_status,.animated_world_matrix=af_engine_animated_world_matrix,.animation_clip=af_engine_animation_clip,.animation_root=af_engine_animation_root,.locomotion_root_motion=af_engine_locomotion_root_motion,.locomotion_root_delta=af_engine_locomotion_root_delta,.move_character_root_motion=af_character_step_root_motion,.animation_events=af_engine_animation_events,.locomotion_step=af_engine_locomotion_step,.third_person_config_default=af_third_person_config_default,.third_person_default=af_third_person_default,.third_person_look=af_third_person_look,.third_person_step=af_third_person_step,.third_person_step_root_motion=af_third_person_step_root_motion,.third_person_camera=af_third_person_camera};
}

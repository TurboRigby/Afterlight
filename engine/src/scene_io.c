/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "scene_internal.h"
#include "water_internal.h"
#include "cJSON.h"
#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void set_error(af_scene_error *error, af_scene_error_code code, const char *format, ...)
{
    if (!error) return;
    error->code = code;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(error->message, sizeof(error->message), format, arguments);
    va_end(arguments);
}

static bool valid_utf8(const unsigned char *data, size_t length)
{
    for (size_t i = 0; i < length;) {
        unsigned char c = data[i++];
        if (c < 0x80) continue;
        size_t extra;
        if (c >= 0xc2 && c <= 0xdf) extra = 1;
        else if (c >= 0xe0 && c <= 0xef) extra = 2;
        else if (c >= 0xf0 && c <= 0xf4) extra = 3;
        else return false;
        if (length - i < extra) return false;
        unsigned char second = data[i];
        if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
            (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90)) return false;
        for (size_t j = 0; j < extra; ++j) if ((data[i++] & 0xc0) != 0x80) return false;
    }
    return true;
}

static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

/* cJSON intentionally accepts some non-JSON strings/numbers. Check lexical
   strictness and embedded NULs before creating its tree. Syntax/escape and
   surrogate validation are then handled by cJSON. */
bool af_json_valid_text(const char *text, size_t length)
{
    if (memchr(text, 0, length) || !valid_utf8((const unsigned char *)text, length)) return false;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] == '"') {
            bool closed = false;
            while (++i < length) {
                if ((unsigned char)text[i] < 0x20) return false;
                if (text[i] == '"') { closed = true; break; }
                if (text[i] == '\\') {
                    if (++i >= length) return false;
                    if (text[i] == 'u' && length - i >= 5 && memcmp(text + i + 1, "0000", 4) == 0) return false;
                }
            }
            if (!closed) return false;
        } else if (text[i] == '-' || digit(text[i])) {
            if (text[i] == '-' && (++i >= length || !digit(text[i]))) return false;
            if (text[i] == '0') ++i;
            else { if (!digit(text[i])) return false; while (i < length && digit(text[i])) ++i; }
            if (i < length && text[i] == '.') {
                if (++i >= length || !digit(text[i])) return false;
                while (i < length && digit(text[i])) ++i;
            }
            if (i < length && (text[i] == 'e' || text[i] == 'E')) {
                if (++i < length && (text[i] == '+' || text[i] == '-')) ++i;
                if (i >= length || !digit(text[i])) return false;
                while (i < length && digit(text[i])) ++i;
            }
            if (i < length && !whitespace(text[i]) && text[i] != ',' && text[i] != ']' && text[i] != '}') return false;
            --i;
        } else if ((unsigned char)text[i] < 0x20 && !whitespace(text[i])) return false;
    }
    return true;
}

static bool exact_fields(const cJSON *object, const char *const *names, size_t count)
{
    if (!cJSON_IsObject(object)) return false;
    uint32_t seen = 0;
    const cJSON *field;
    cJSON_ArrayForEach(field, object) {
        size_t i;
        for (i = 0; i < count; ++i) if (strcmp(field->string, names[i]) == 0) break;
        if (i == count || (seen & (1u << i))) return false;
        seen |= 1u << i;
    }
    return seen == (1u << count) - 1;
}

static bool read_id(const cJSON *item, uint64_t *value)
{
    if (!cJSON_IsString(item) || !item->valuestring[0]) return false;
    const char *s = item->valuestring;
    if (s[0] == '0' && s[1]) return false;
    *value = 0;
    for (; *s; ++s) {
        if (!digit(*s) || *value > (UINT64_MAX - (uint64_t)(*s - '0')) / 10) return false;
        *value = *value * 10 + (uint64_t)(*s - '0');
    }
    return true;
}

static bool read_name(const cJSON *item, char *out, size_t capacity)
{
    if (!cJSON_IsString(item) || !item->valuestring[0] || strlen(item->valuestring) >= capacity) return false;
    memcpy(out, item->valuestring, strlen(item->valuestring) + 1);
    return true;
}

static bool read_vector(const cJSON *item, af_vec3 *out)
{
    if (!cJSON_IsArray(item) || cJSON_GetArraySize(item) != 3) return false;
    float values[3];
    for (int i = 0; i < 3; ++i) {
        const cJSON *number = cJSON_GetArrayItem(item, i);
        if (!cJSON_IsNumber(number) || !isfinite(number->valuedouble) || fabs(number->valuedouble) > FLT_MAX) return false;
        values[i] = (float)number->valuedouble;
    }
    *out = (af_vec3){values[0], values[1], values[2]};
    return true;
}

static int compare_ids(const void *left, const void *right)
{
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

static bool read_float_field(const cJSON *object,const char *field,float *out)
{
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(object,field);
    if(!cJSON_IsNumber(value)||!isfinite(value->valuedouble)||fabs(value->valuedouble)>FLT_MAX)return false;
    *out=(float)value->valuedouble;return true;
}

static const char *const role_names[]={"none","spawn","checkpoint","goal","restart","platform"};
static bool read_gameplay(const cJSON *item,af_gameplay_component *out){
    static const char *const fields[]={"role","checkpoint_order","player","camera"};
    static const char *const player_fields[]={"enabled","spawn","radius","half_height","speed","jump_speed","gravity","slope_limit","skin","ground_snap","mouse_sensitivity"};
    static const char *const camera_fields[]={"enabled","primary","eye_height","vertical_fov","near_clip","far_clip"};
    const cJSON *role=cJSON_GetObjectItemCaseSensitive(item,"role"),*order=cJSON_GetObjectItemCaseSensitive(item,"checkpoint_order"),*player=cJSON_GetObjectItemCaseSensitive(item,"player"),*camera=cJSON_GetObjectItemCaseSensitive(item,"camera");
    const cJSON *pe=cJSON_GetObjectItemCaseSensitive(player,"enabled"),*ce=cJSON_GetObjectItemCaseSensitive(camera,"enabled"),*primary=cJSON_GetObjectItemCaseSensitive(camera,"primary");
    af_gameplay_component value=af_gameplay_component_default();
    if(!exact_fields(item,fields,4)||!exact_fields(player,player_fields,11)||!exact_fields(camera,camera_fields,6)||!cJSON_IsString(role)||!cJSON_IsNumber(order)||!isfinite(order->valuedouble)||order->valuedouble<0||order->valuedouble>1024||floor(order->valuedouble)!=order->valuedouble||!cJSON_IsBool(pe)||!cJSON_IsBool(ce)||!cJSON_IsBool(primary)||!read_id(cJSON_GetObjectItemCaseSensitive(player,"spawn"),&value.player.spawn))return false;
    bool found=false;for(unsigned i=0;i<AF_ROLE_COUNT;++i)if(!strcmp(role->valuestring,role_names[i])){value.role=(af_gameplay_role)i;found=true;break;}if(!found)return false;
    value.player.enabled=cJSON_IsTrue(pe)!=0;value.camera.enabled=cJSON_IsTrue(ce)!=0;value.camera.primary=cJSON_IsTrue(primary)!=0;value.checkpoint_order=(unsigned)order->valuedouble;
    float *controller[]={&value.player.controller.radius,&value.player.controller.half_height,&value.player.controller.speed,&value.player.controller.jump_speed,&value.player.controller.gravity,&value.player.controller.slope_limit,&value.player.controller.skin,&value.player.controller.ground_snap,&value.player.mouse_sensitivity};
    for(unsigned i=0;i<9;++i)if(!read_float_field(player,player_fields[i+2],controller[i]))return false;
    float *view[]={&value.camera.eye_height,&value.camera.vertical_fov,&value.camera.near_clip,&value.camera.far_clip};
    for(unsigned i=0;i<4;++i)if(!read_float_field(camera,camera_fields[i+2],view[i]))return false;
    if(!af_gameplay_component_valid(value))return false;*out=value;return true;
}
static bool read_rig(const cJSON *json,af_character_rig *r){
    static const char *const fields[]={"enabled","root_motion","root_node","clips","walk_speed","run_speed","camera_distance","camera_height","turn_speed"};
    const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(json,"enabled"),*root=cJSON_GetObjectItemCaseSensitive(json,"root_motion"),*node=cJSON_GetObjectItemCaseSensitive(json,"root_node"),*clips=cJSON_GetObjectItemCaseSensitive(json,"clips");
    if(!exact_fields(json,fields,9)||!cJSON_IsBool(enabled)||!cJSON_IsBool(root)||!cJSON_IsNumber(node)||!isfinite(node->valuedouble)||node->valuedouble<0||node->valuedouble>=AF_ANIMATION_MAX_NODES||floor(node->valuedouble)!=node->valuedouble||!cJSON_IsArray(clips)||cJSON_GetArraySize(clips)!=5)return false;
    *r=af_character_rig_default();r->enabled=cJSON_IsTrue(enabled)!=0;r->root_motion=cJSON_IsTrue(root)!=0;r->root_node=(uint32_t)node->valuedouble;
    for(unsigned i=0;i<5;++i){const cJSON *c=cJSON_GetArrayItem(clips,(int)i);if(!cJSON_IsNumber(c)||!isfinite(c->valuedouble)||c->valuedouble<0||c->valuedouble>=128||floor(c->valuedouble)!=c->valuedouble)return false;r->clips[i]=(uint32_t)c->valuedouble;}
    float *values[]={&r->walk_speed,&r->run_speed,&r->camera_distance,&r->camera_height,&r->turn_speed};for(unsigned i=0;i<5;++i)if(!read_float_field(json,fields[i+4],values[i]))return false;return af_character_rig_valid(*r);
}
static bool add_rig(cJSON *object,af_character_rig r){
    if(!af_character_rig_valid(r))return false;cJSON *j=cJSON_AddObjectToObject(object,"rig");double clips[5];for(unsigned i=0;i<5;++i)clips[i]=r.clips[i];cJSON *array=cJSON_CreateDoubleArray(clips,5);
    if(!j||!array){cJSON_Delete(array);return false;}if(!cJSON_AddItemToObject(j,"clips",array)){cJSON_Delete(array);return false;}
    return cJSON_AddBoolToObject(j,"enabled",r.enabled)&&cJSON_AddBoolToObject(j,"root_motion",r.root_motion)&&cJSON_AddNumberToObject(j,"root_node",r.root_node)&&cJSON_AddNumberToObject(j,"walk_speed",r.walk_speed)&&cJSON_AddNumberToObject(j,"run_speed",r.run_speed)&&cJSON_AddNumberToObject(j,"camera_distance",r.camera_distance)&&cJSON_AddNumberToObject(j,"camera_height",r.camera_height)&&cJSON_AddNumberToObject(j,"turn_speed",r.turn_speed);
}
static bool add_vector(cJSON*,const char*,af_vec3);
static bool read_body(const cJSON *json,af_rigid_body *out){
    static const char *const fields[]={"enabled","type","mass","friction","restitution","linear_damping","angular_damping","gravity_scale","allow_sleep","continuous","velocity","angular_velocity"};
    const cJSON *enabled=cJSON_GetObjectItemCaseSensitive(json,"enabled"),*type=cJSON_GetObjectItemCaseSensitive(json,"type"),*sleep=cJSON_GetObjectItemCaseSensitive(json,"allow_sleep"),*ccd=cJSON_GetObjectItemCaseSensitive(json,"continuous");
    if(!exact_fields(json,fields,12)||!cJSON_IsBool(enabled)||!cJSON_IsBool(sleep)||!cJSON_IsBool(ccd)||!cJSON_IsString(type))return false;af_rigid_body b=af_rigid_body_default();b.enabled=cJSON_IsTrue(enabled)!=0;b.allow_sleep=cJSON_IsTrue(sleep)!=0;b.continuous=cJSON_IsTrue(ccd)!=0;
    if(!strcmp(type->valuestring,"static"))b.type=AF_BODY_STATIC;else if(!strcmp(type->valuestring,"kinematic"))b.type=AF_BODY_KINEMATIC;else if(!strcmp(type->valuestring,"dynamic"))b.type=AF_BODY_DYNAMIC;else return false;
    float *values[]={&b.mass,&b.friction,&b.restitution,&b.linear_damping,&b.angular_damping,&b.gravity_scale};for(unsigned i=0;i<6;++i)if(!read_float_field(json,fields[i+2],values[i]))return false;
    if(!read_vector(cJSON_GetObjectItemCaseSensitive(json,"velocity"),&b.velocity)||!read_vector(cJSON_GetObjectItemCaseSensitive(json,"angular_velocity"),&b.angular_velocity)||!af_rigid_body_valid(b))return false;*out=b;return true;
}
static bool add_body(cJSON *json,af_rigid_body b){if(!af_rigid_body_valid(b))return false;cJSON *j=cJSON_AddObjectToObject(json,"body");return j&&cJSON_AddBoolToObject(j,"enabled",b.enabled)&&cJSON_AddStringToObject(j,"type",b.type==AF_BODY_DYNAMIC?"dynamic":b.type==AF_BODY_KINEMATIC?"kinematic":"static")&&cJSON_AddNumberToObject(j,"mass",b.mass)&&cJSON_AddNumberToObject(j,"friction",b.friction)&&cJSON_AddNumberToObject(j,"restitution",b.restitution)&&cJSON_AddNumberToObject(j,"linear_damping",b.linear_damping)&&cJSON_AddNumberToObject(j,"angular_damping",b.angular_damping)&&cJSON_AddNumberToObject(j,"gravity_scale",b.gravity_scale)&&cJSON_AddBoolToObject(j,"allow_sleep",b.allow_sleep)&&cJSON_AddBoolToObject(j,"continuous",b.continuous)&&add_vector(j,"velocity",b.velocity)&&add_vector(j,"angular_velocity",b.angular_velocity);}
static af_scene *decode_scene(const cJSON *root,const char *scene_path, af_scene_error *error)
{
    static const char *const root_fields[] = {"format", "version", "next_id", "objects", "assets", "light", "shadows", "render", "local_lights", "joints", "next_joint", "collider_parts", "next_part", "water", "wind"};
    static const char *const object_fields[] = {"id", "name", "mesh", "visible", "position", "rotation", "scale", "material", "parent", "collider", "gameplay", "animation", "model", "rig", "body"};
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "format");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *objects = cJSON_GetObjectItemCaseSensitive(root, "objects");
    uint64_t next_id;
    if (!cJSON_IsString(format) ||
        strcmp(format->valuestring, "afterlight.scene") != 0 || !cJSON_IsNumber(version)) {
        set_error(error, AF_SCENE_ERROR_FORMAT, "Invalid scene header, missing, unknown, or duplicate fields.");
        return NULL;
    }
    if (version->valuedouble != 1 && version->valuedouble != 2 && version->valuedouble != 3 && version->valuedouble != 4 && version->valuedouble != 5 && version->valuedouble != 6 && version->valuedouble != 7 && version->valuedouble != 8 && version->valuedouble != 9 && version->valuedouble != 10 && version->valuedouble != 11 && version->valuedouble != 12 && version->valuedouble != 13 && version->valuedouble != 14 && version->valuedouble != 15 && version->valuedouble != 16 && version->valuedouble != 17 && version->valuedouble != 18 && version->valuedouble != 19 && version->valuedouble != 20 && version->valuedouble != 21 && version->valuedouble != 22 && version->valuedouble != AF_SCENE_FILE_VERSION) {
        set_error(error, AF_SCENE_ERROR_VERSION, "Unsupported scene version; expected %d.", AF_SCENE_FILE_VERSION);
        return NULL;
    }
    if (!exact_fields(root,root_fields,version->valuedouble==1?4:(version->valuedouble==2?6:(version->valuedouble==3?7:version->valuedouble>=22?15:version->valuedouble>=17?14:version->valuedouble>=16?13:version->valuedouble>=14?11:9))) || !read_id(cJSON_GetObjectItemCaseSensitive(root, "next_id"), &next_id) || !cJSON_IsArray(objects) ||
        cJSON_GetArraySize(objects) > (int)AF_SCENE_FILE_MAX_OBJECTS) {
        set_error(error, AF_SCENE_ERROR_FORMAT, "Invalid next_id or object array (maximum %u objects).", AF_SCENE_FILE_MAX_OBJECTS);
        return NULL;
    }
    af_scene *scene = af_scene_create();
    if (!scene) { set_error(error, AF_SCENE_ERROR_MEMORY, "Allocating scene."); return NULL; }
    scene->next_id = next_id;
    size_t count = (size_t)cJSON_GetArraySize(objects);
    uint64_t *ids = count ? malloc(count * sizeof(*ids)) : NULL;
    if (count) scene->objects = calloc(count, sizeof(*scene->objects));
    if (count && (!ids || !scene->objects)) {
        set_error(error, AF_SCENE_ERROR_MEMORY, "Allocating scene objects.");
        goto fail;
    }
    scene->capacity = count;
    const cJSON *item;
    cJSON_ArrayForEach(item, objects) {
        af_scene_object *object = &scene->objects[scene->count];
        object->material=af_material_default();object->collider=af_collider_default();object->gameplay=af_gameplay_component_default();object->animation=af_animation_component_default();object->model=af_model_binding_default();object->rig=af_character_rig_default();object->body=af_rigid_body_default();
        const cJSON *visible = cJSON_GetObjectItemCaseSensitive(item, "visible");
        if (!exact_fields(item, object_fields,version->valuedouble==1?7:(version->valuedouble>=13?15:version->valuedouble>=11?14:version->valuedouble>=10?13:version->valuedouble>=6?11:(version->valuedouble>=5?10:9))) ||
            !read_id(cJSON_GetObjectItemCaseSensitive(item, "id"), &object->id) || object->id == AF_OBJECT_NONE ||
            (next_id != AF_OBJECT_NONE && object->id >= next_id) || !cJSON_IsBool(visible) ||
            !read_name(cJSON_GetObjectItemCaseSensitive(item, "name"), object->name, sizeof(object->name)) ||
            !read_name(cJSON_GetObjectItemCaseSensitive(item, "mesh"), object->mesh, sizeof(object->mesh)) ||
            !read_vector(cJSON_GetObjectItemCaseSensitive(item, "position"), &object->transform.position) ||
            !read_vector(cJSON_GetObjectItemCaseSensitive(item, "rotation"), &object->transform.rotation) ||
            !read_vector(cJSON_GetObjectItemCaseSensitive(item, "scale"), &object->transform.scale)) {
            set_error(error, AF_SCENE_ERROR_FORMAT, "Invalid object at index %zu (ID, name, visibility, or transform).", scene->count);
            goto fail;
        }
        if(version->valuedouble>=13&&!read_body(cJSON_GetObjectItemCaseSensitive(item,"body"),&object->body)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid rigid body.");goto fail;}
        if(version->valuedouble>=11&&!read_rig(cJSON_GetObjectItemCaseSensitive(item,"rig"),&object->rig)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid character rig.");goto fail;}
        if(version->valuedouble>=10){
            static const char *const animation_fields[]={"asset","clip","speed","autoplay","loop"},*const model_fields[]={"root","node","skin","primitive"};
            const cJSON *a=cJSON_GetObjectItemCaseSensitive(item,"animation"),*m=cJSON_GetObjectItemCaseSensitive(item,"model");
            const cJSON *asset=cJSON_GetObjectItemCaseSensitive(a,"asset"),*clip=cJSON_GetObjectItemCaseSensitive(a,"clip"),*autoplay=cJSON_GetObjectItemCaseSensitive(a,"autoplay"),*loop=cJSON_GetObjectItemCaseSensitive(a,"loop"),*node=cJSON_GetObjectItemCaseSensitive(m,"node"),*skin=cJSON_GetObjectItemCaseSensitive(m,"skin"),*primitive=cJSON_GetObjectItemCaseSensitive(m,"primitive");
            if(!exact_fields(a,animation_fields,5)||!exact_fields(m,model_fields,4)||!cJSON_IsString(asset)||strlen(asset->valuestring)>=sizeof(object->animation.asset)||!cJSON_IsNumber(clip)||!isfinite(clip->valuedouble)||clip->valuedouble<0||clip->valuedouble>=128||floor(clip->valuedouble)!=clip->valuedouble||!read_float_field(a,"speed",&object->animation.speed)||fabsf(object->animation.speed)>100||!cJSON_IsBool(autoplay)||!cJSON_IsBool(loop)||!read_id(cJSON_GetObjectItemCaseSensitive(m,"root"),&object->model.root)||!cJSON_IsNumber(node)||!isfinite(node->valuedouble)||node->valuedouble<0||node->valuedouble>=16384||floor(node->valuedouble)!=node->valuedouble||!cJSON_IsNumber(skin)||!isfinite(skin->valuedouble)||skin->valuedouble< -1||skin->valuedouble>=128||floor(skin->valuedouble)!=skin->valuedouble||!cJSON_IsBool(primitive)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid animation component or model binding.");goto fail;}
            snprintf(object->animation.asset,sizeof(object->animation.asset),"%s",asset->valuestring);object->animation.clip=(uint32_t)clip->valuedouble;object->animation.autoplay=cJSON_IsTrue(autoplay)!=0;object->animation.loop=cJSON_IsTrue(loop)!=0;
            object->model.node=(uint32_t)node->valuedouble;object->model.skin=(int32_t)skin->valuedouble;object->model.primitive=cJSON_IsTrue(primitive)!=0;
        }
        if(version->valuedouble>=6&&!read_gameplay(cJSON_GetObjectItemCaseSensitive(item,"gameplay"),&object->gameplay)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid player/camera/gameplay component.");goto fail;}
        if(version->valuedouble>=5){
            static const char *const fields[]={"type","enabled","trigger","center","half_extents","radius","half_height","layer"};
            const cJSON *c=cJSON_GetObjectItemCaseSensitive(item,"collider"),*type=cJSON_GetObjectItemCaseSensitive(c,"type"),*enabled=cJSON_GetObjectItemCaseSensitive(c,"enabled"),*trigger=cJSON_GetObjectItemCaseSensitive(c,"trigger"),*layer=cJSON_GetObjectItemCaseSensitive(c,"layer");
            af_collider value=af_collider_default();
            if(!exact_fields(c,fields,8)||!cJSON_IsString(type)||!cJSON_IsBool(enabled)||!cJSON_IsBool(trigger)||!read_vector(cJSON_GetObjectItemCaseSensitive(c,"center"),&value.center)||
                !read_vector(cJSON_GetObjectItemCaseSensitive(c,"half_extents"),&value.half_extents)||!read_float_field(c,"radius",&value.radius)||!read_float_field(c,"half_height",&value.half_height)||
                !cJSON_IsNumber(layer)||!isfinite(layer->valuedouble)||layer->valuedouble<1||layer->valuedouble>UINT32_MAX||floor(layer->valuedouble)!=layer->valuedouble){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid collider fields.");goto fail;}
            static const char *const types[]={"none","box","sphere","capsule","mesh","compound"};bool known=false;
            for(unsigned shape=0;shape<(version->valuedouble>=16?6u:version->valuedouble>=14?5u:4u);++shape)if(!strcmp(type->valuestring,types[shape])){value.type=(af_collider_type)shape;known=true;break;}
            value.enabled=cJSON_IsTrue(enabled)!=0;value.trigger=cJSON_IsTrue(trigger)!=0;value.layer=(uint32_t)layer->valuedouble;
            if(!known||!af_collider_valid(value)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid collider shape or dimensions.");goto fail;}object->collider=value;
        }
        object->visible = cJSON_IsTrue(visible) != 0;
        ids[scene->count++] = object->id;
        if(version->valuedouble>=2){
            if(!read_id(cJSON_GetObjectItemCaseSensitive(item,"parent"),&object->parent)){
                set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid parent ID.");goto fail;
            }
            static const char *const fields[]={"base_color","texture","unlit","cast_shadows","receive_shadows","physically_based","roughness","metallic","emission","emission_strength","normal_map","normal_strength","metallic_roughness_map","alpha_mode","alpha_cutoff"};
            const cJSON *m=cJSON_GetObjectItemCaseSensitive(item,"material"),*color=cJSON_GetObjectItemCaseSensitive(m,"base_color"),*texture=cJSON_GetObjectItemCaseSensitive(m,"texture"),*unlit=cJSON_GetObjectItemCaseSensitive(m,"unlit");
            af_material material=af_material_default();
            if(!exact_fields(m,fields,version->valuedouble>=12?15:version->valuedouble>=7?13:version->valuedouble>=4?12:(version->valuedouble>=3?5:3))||!cJSON_IsArray(color)||cJSON_GetArraySize(color)!=4||!cJSON_IsString(texture)||strlen(texture->valuestring)>=sizeof(material.texture)||!cJSON_IsBool(unlit)){
                set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid object material.");goto fail;
            }
            float values[4];for(int c=0;c<4;++c){const cJSON *v=cJSON_GetArrayItem(color,c);if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<0||v->valuedouble>1){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid material color.");goto fail;}values[c]=(float)v->valuedouble;}
            material.base_color=(af_color){values[0],values[1],values[2],values[3]};snprintf(material.texture,sizeof(material.texture),"%s",texture->valuestring);material.unlit=cJSON_IsTrue(unlit)!=0;
            if(version->valuedouble>=3){const cJSON *cast=cJSON_GetObjectItemCaseSensitive(m,"cast_shadows"),*receive=cJSON_GetObjectItemCaseSensitive(m,"receive_shadows");
                if(!cJSON_IsBool(cast)||!cJSON_IsBool(receive)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid shadow material flags.");goto fail;}
                material.cast_shadows=cJSON_IsTrue(cast)!=0;material.receive_shadows=cJSON_IsTrue(receive)!=0;
            }
            if(version->valuedouble>=4){const cJSON *pbr=cJSON_GetObjectItemCaseSensitive(m,"physically_based"),*normal=cJSON_GetObjectItemCaseSensitive(m,"normal_map");
                if(!cJSON_IsBool(pbr)||!cJSON_IsString(normal)||strlen(normal->valuestring)>=sizeof(material.normal_map)||
                    !read_float_field(m,"roughness",&material.roughness)||!read_float_field(m,"metallic",&material.metallic)||!read_float_field(m,"emission_strength",&material.emission_strength)||
                    !read_float_field(m,"normal_strength",&material.normal_strength)||!read_vector(cJSON_GetObjectItemCaseSensitive(m,"emission"),&material.emission)){
                    set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid surface material fields.");goto fail;}
                if(version->valuedouble>=7){const cJSON *mr=cJSON_GetObjectItemCaseSensitive(m,"metallic_roughness_map");if(!cJSON_IsString(mr)||strlen(mr->valuestring)>=sizeof(material.metallic_roughness_map)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid metallic/roughness texture.");goto fail;}snprintf(material.metallic_roughness_map,sizeof(material.metallic_roughness_map),"%s",mr->valuestring);}
                material.physically_based=cJSON_IsTrue(pbr)!=0;snprintf(material.normal_map,sizeof(material.normal_map),"%s",normal->valuestring);
            }
            if(version->valuedouble>=12){const cJSON *mode=cJSON_GetObjectItemCaseSensitive(m,"alpha_mode");if(!cJSON_IsString(mode)||!read_float_field(m,"alpha_cutoff",&material.alpha_cutoff)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid alpha material.");goto fail;}if(!strcmp(mode->valuestring,"opaque"))material.alpha_mode=AF_ALPHA_OPAQUE;else if(!strcmp(mode->valuestring,"mask"))material.alpha_mode=AF_ALPHA_MASK;else if(!strcmp(mode->valuestring,"blend"))material.alpha_mode=AF_ALPHA_BLEND;else{set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid alpha mode.");goto fail;}}
            if(!af_scene_set_object_material(scene,object->id,material)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid material.");goto fail;}
        }
    }
    if (count > 1) qsort(ids, count, sizeof(*ids), compare_ids);
    for (size_t i = 1; i < count; ++i) {
        if (ids[i] == ids[i-1]) {
            set_error(error, AF_SCENE_ERROR_FORMAT, "Duplicate object ID %" PRIu64 ".", ids[i]);
            goto fail;
        }
    }
    for(size_t i=0;i<scene->count;++i){const af_scene_object *node=&scene->objects[i];unsigned depth=0;
        while(node->parent){if(++depth>=AF_SCENE_MAX_DEPTH||(node=af_scene_get_object(scene,node->parent))==NULL){set_error(error,AF_SCENE_ERROR_FORMAT,"Hierarchy contains a missing parent, cycle or excessive depth.");goto fail;}}
    }
    for(size_t i=0;i<scene->count;++i)if(!af_scene_set_object_animation(scene,scene->objects[i].id,scene->objects[i].animation)||!af_scene_set_model_binding(scene,scene->objects[i].id,scene->objects[i].model)){set_error(error,AF_SCENE_ERROR_FORMAT,"Animation binding root is missing or outside its hierarchy.");goto fail;}
    if(version->valuedouble>=2){
        static const char *const light_fields[]={"direction","color","intensity","ambient"};
        const cJSON *l=cJSON_GetObjectItemCaseSensitive(root,"light"),*intensity=cJSON_GetObjectItemCaseSensitive(l,"intensity"),*ambient=cJSON_GetObjectItemCaseSensitive(l,"ambient");
        af_directional_light light;
        if(!exact_fields(l,light_fields,4)||!read_vector(cJSON_GetObjectItemCaseSensitive(l,"direction"),&light.direction)||!read_vector(cJSON_GetObjectItemCaseSensitive(l,"color"),&light.color)||!cJSON_IsNumber(intensity)||!cJSON_IsNumber(ambient)){
            set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid directional light.");goto fail;
        }
        if(!isfinite(intensity->valuedouble)||fabs(intensity->valuedouble)>FLT_MAX||!isfinite(ambient->valuedouble)||fabs(ambient->valuedouble)>FLT_MAX){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid light range.");goto fail;}
        light.intensity=(float)intensity->valuedouble;light.ambient=(float)ambient->valuedouble;
        if(!af_scene_set_light(scene,light)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid directional light values.");goto fail;}
        const cJSON *assets=cJSON_GetObjectItemCaseSensitive(root,"assets");
        if(!cJSON_IsArray(assets)||cJSON_GetArraySize(assets)>AF_SCENE_MAX_ASSETS){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid asset manifest.");goto fail;}
        static const char *const fields[]={"name","kind","path"};
        const cJSON *asset;cJSON_ArrayForEach(asset,assets){
            char name[AF_MESH_NAME_CAPACITY],path[AF_ASSET_PATH_CAPACITY],resolved[AF_ASSET_PATH_CAPACITY];
            const cJSON *kind=cJSON_GetObjectItemCaseSensitive(asset,"kind");
            if(!exact_fields(asset,fields,3)||!read_name(cJSON_GetObjectItemCaseSensitive(asset,"name"),name,sizeof(name))||
               !read_name(cJSON_GetObjectItemCaseSensitive(asset,"path"),path,sizeof(path))||!cJSON_IsString(kind)||
               (strcmp(kind->valuestring,"mesh")&&strcmp(kind->valuestring,"texture")&&(version->valuedouble<4||strcmp(kind->valuestring,"normal_map"))&&(version->valuedouble<6||strcmp(kind->valuestring,"audio"))&&(version->valuedouble<7||strcmp(kind->valuestring,"metallic_roughness"))&&(version->valuedouble<9||strcmp(kind->valuestring,"environment"))&&(version->valuedouble<10||strcmp(kind->valuestring,"animation")))||af_scene_find_asset(scene,name)||
               !af_asset_resolve_path(scene_path,path,resolved,sizeof(resolved))||
               !af_scene_set_asset(scene,name,!strcmp(kind->valuestring,"animation")?AF_ASSET_ANIMATION:!strcmp(kind->valuestring,"environment")?AF_ASSET_ENVIRONMENT:!strcmp(kind->valuestring,"metallic_roughness")?AF_ASSET_METALLIC_ROUGHNESS:!strcmp(kind->valuestring,"audio")?AF_ASSET_AUDIO:!strcmp(kind->valuestring,"mesh")?AF_ASSET_MESH:!strcmp(kind->valuestring,"normal_map")?AF_ASSET_NORMAL_MAP:AF_ASSET_TEXTURE,resolved)){
                set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid or duplicate asset reference.");goto fail;
            }
        }
    }
    if(version->valuedouble>=3){
        static const char *const fields[]={"enabled","mode","resolution","distance","constant_bias","slope_bias","normal_bias","filter_radius","split_lambda","blend_fraction","debug_cascades"};
        const cJSON *sh=cJSON_GetObjectItemCaseSensitive(root,"shadows"),*enabled=cJSON_GetObjectItemCaseSensitive(sh,"enabled"),*mode=cJSON_GetObjectItemCaseSensitive(sh,"mode"),*debug=cJSON_GetObjectItemCaseSensitive(sh,"debug_cascades");
        if(!exact_fields(sh,fields,11)||!cJSON_IsBool(enabled)||!cJSON_IsBool(debug)||!cJSON_IsString(mode)){
            set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid shadow settings.");goto fail;}
        af_shadow_settings settings=af_shadow_settings_default();settings.enabled=cJSON_IsTrue(enabled)!=0;settings.debug_cascades=cJSON_IsTrue(debug)!=0;
        if(!strcmp(mode->valuestring,"single"))settings.mode=AF_SHADOW_SINGLE;
        else if(!strcmp(mode->valuestring,"cascaded"))settings.mode=AF_SHADOW_CASCADED;
        else{set_error(error,AF_SCENE_ERROR_FORMAT,"Unknown shadow mode.");goto fail;}
        float values[8];
        for(int i=0;i<8;++i){const cJSON *v=cJSON_GetObjectItemCaseSensitive(sh,fields[i+2]);
            if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||fabs(v->valuedouble)>FLT_MAX){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid shadow numeric field.");goto fail;}values[i]=(float)v->valuedouble;}
        if(values[0]<0||values[0]>4096||floorf(values[0])!=values[0]||values[5]<0||values[5]>2||floorf(values[5])!=values[5]){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid shadow resolution/filter.");goto fail;}
        settings.resolution=(uint32_t)values[0];settings.distance=values[1];settings.constant_bias=values[2];settings.slope_bias=values[3];settings.normal_bias=values[4];settings.filter_radius=(uint32_t)values[5];settings.split_lambda=values[6];settings.blend_fraction=values[7];
        if(!af_scene_set_shadows(scene,settings)){set_error(error,AF_SCENE_ERROR_FORMAT,"Shadow settings outside supported ranges.");goto fail;}
    }
    if(version->valuedouble>=4){
        static const char *const render_fields[]={"exposure","tone_mapping","bloom","bloom_strength","bloom_threshold","bloom_radius","environment_enabled","environment_profile","environment_intensity","environment_rotation","local_shadow_resolution","environment_asset","skybox","skybox_intensity","underwater","refraction","refraction_strength","anti_aliasing","aa_sharpness","taa_history_weight"};const cJSON *render=cJSON_GetObjectItemCaseSensitive(root,"render"),*tone=cJSON_GetObjectItemCaseSensitive(render,"tone_mapping");
        af_render_settings settings=af_render_settings_default();
        if(!exact_fields(render,render_fields,version->valuedouble>=23?20:version->valuedouble>=21?17:version->valuedouble>=20?15:version->valuedouble>=9?14:version->valuedouble>=8?11:2)||!read_float_field(render,"exposure",&settings.exposure)||!cJSON_IsBool(tone)){
            set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid exposure/tone mapping settings.");goto fail;}
        if(version->valuedouble>=8){float profile,resolution;const cJSON *bloom=cJSON_GetObjectItemCaseSensitive(render,"bloom"),*environment=cJSON_GetObjectItemCaseSensitive(render,"environment_enabled");
            if(!cJSON_IsBool(bloom)||!cJSON_IsBool(environment)||!read_float_field(render,"bloom_strength",&settings.bloom_strength)||!read_float_field(render,"bloom_threshold",&settings.bloom_threshold)||!read_float_field(render,"bloom_radius",&settings.bloom_radius)||
               !read_float_field(render,"environment_profile",&profile)||profile<0||profile>2||floorf(profile)!=profile||!read_float_field(render,"environment_intensity",&settings.environment_intensity)||!read_float_field(render,"environment_rotation",&settings.environment_rotation)||
               !read_float_field(render,"local_shadow_resolution",&resolution)||(resolution!=256&&resolution!=512&&resolution!=1024)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid HDR/environment/local shadow settings.");goto fail;}
            settings.environment_enabled=cJSON_IsTrue(environment)!=0;settings.bloom=cJSON_IsTrue(bloom)!=0;settings.environment_profile=(uint32_t)profile;settings.local_shadow_resolution=(uint32_t)resolution;
        }
        if(version->valuedouble>=9){const cJSON *asset=cJSON_GetObjectItemCaseSensitive(render,"environment_asset"),*sky=cJSON_GetObjectItemCaseSensitive(render,"skybox");
            if(!cJSON_IsString(asset)||strlen(asset->valuestring)>=sizeof(settings.environment_asset)||!cJSON_IsBool(sky)||!read_float_field(render,"skybox_intensity",&settings.skybox_intensity)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid skybox/environment asset settings.");goto fail;}
            snprintf(settings.environment_asset,sizeof(settings.environment_asset),"%s",asset->valuestring);settings.skybox=cJSON_IsTrue(sky)!=0;
        }
        if(version->valuedouble>=20){const cJSON *u=cJSON_GetObjectItemCaseSensitive(render,"underwater");if(!cJSON_IsBool(u)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid underwater toggle.");goto fail;}settings.underwater=cJSON_IsTrue(u)!=0;}
        if(version->valuedouble>=21){const cJSON *r=cJSON_GetObjectItemCaseSensitive(render,"refraction");if(!cJSON_IsBool(r)||!read_float_field(render,"refraction_strength",&settings.refraction_strength)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid refraction settings.");goto fail;}settings.refraction=cJSON_IsTrue(r)!=0;}
        if(version->valuedouble>=23){
            const cJSON *aa=cJSON_GetObjectItemCaseSensitive(render,"anti_aliasing");
            if(!cJSON_IsString(aa)||(strcmp(aa->valuestring,"off")&&strcmp(aa->valuestring,"fxaa"))||!read_float_field(render,"aa_sharpness",&settings.aa_sharpness)||!read_float_field(render,"taa_history_weight",&settings.taa_history_weight)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid anti-aliasing settings.");goto fail;}
            settings.anti_aliasing=!strcmp(aa->valuestring,"fxaa")?AF_AA_FXAA:AF_AA_OFF;
        }
        settings.tone_mapping=cJSON_IsTrue(tone)!=0;if(!af_scene_set_render_settings(scene,settings)){set_error(error,AF_SCENE_ERROR_FORMAT,"Exposure outside supported range.");goto fail;}
        const cJSON *lights=cJSON_GetObjectItemCaseSensitive(root,"local_lights");
        if(!cJSON_IsArray(lights)||cJSON_GetArraySize(lights)>AF_SCENE_MAX_LOCAL_LIGHTS){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid local light array.");goto fail;}
        static const char *const light_fields[]={"name","type","enabled","position","color","intensity","range","direction","inner_angle","outer_angle","cast_shadows","shadow_bias"};
        const cJSON *local;cJSON_ArrayForEach(local,lights){af_local_light value=af_local_light_default(AF_LIGHT_POINT);
            const cJSON *type=cJSON_GetObjectItemCaseSensitive(local,"type"),*enabled=cJSON_GetObjectItemCaseSensitive(local,"enabled");
            if(!exact_fields(local,light_fields,version->valuedouble>=8?12:10)||!cJSON_IsString(type)||!cJSON_IsBool(enabled)||!read_name(cJSON_GetObjectItemCaseSensitive(local,"name"),value.name,sizeof(value.name))||
                !read_vector(cJSON_GetObjectItemCaseSensitive(local,"position"),&value.position)||!read_vector(cJSON_GetObjectItemCaseSensitive(local,"color"),&value.color)||
                !read_vector(cJSON_GetObjectItemCaseSensitive(local,"direction"),&value.direction)||!read_float_field(local,"intensity",&value.intensity)||!read_float_field(local,"range",&value.range)||
                !read_float_field(local,"inner_angle",&value.inner_angle)||!read_float_field(local,"outer_angle",&value.outer_angle)){
                set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid local light fields.");goto fail;}
            if(!strcmp(type->valuestring,"point"))value.type=AF_LIGHT_POINT;else if(!strcmp(type->valuestring,"spot"))value.type=AF_LIGHT_SPOT;
            else{set_error(error,AF_SCENE_ERROR_FORMAT,"Unknown local light type.");goto fail;}
            if(version->valuedouble>=8){const cJSON *cast=cJSON_GetObjectItemCaseSensitive(local,"cast_shadows");if(!cJSON_IsBool(cast)||!read_float_field(local,"shadow_bias",&value.shadow_bias)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid local shadow fields.");goto fail;}value.cast_shadows=cJSON_IsTrue(cast)!=0;}
            value.enabled=cJSON_IsTrue(enabled)!=0;if(!af_scene_add_local_light(scene,value)){set_error(error,AF_SCENE_ERROR_FORMAT,"Local light values outside supported ranges.");goto fail;}
        }
    }
    if(version->valuedouble<6){
        af_object_id spawn=0;
        for(size_t i=0;i<scene->count;++i){af_scene_object *o=&scene->objects[i];
            if(!strcmp(o->name,"Spawn")){o->gameplay.role=AF_ROLE_SPAWN;spawn=o->id;}
            else if(!strcmp(o->name,"Moving platform"))o->gameplay.role=AF_ROLE_PLATFORM;
            else if(!strcmp(o->name,"Goal trigger"))o->gameplay.role=AF_ROLE_GOAL;
            else if(!strcmp(o->name,"Restart trigger"))o->gameplay.role=AF_ROLE_RESTART;
        }
        for(size_t i=0;i<scene->count;++i){af_scene_object *o=&scene->objects[i];if(!strcmp(o->name,"Player")&&o->collider.type==AF_COLLIDER_CAPSULE){o->gameplay.player.enabled=true;o->gameplay.player.spawn=spawn;o->gameplay.camera.enabled=o->gameplay.camera.primary=true;o->gameplay.player.controller.radius=o->collider.radius;o->gameplay.player.controller.half_height=o->collider.half_height;}}
    }

    if(version->valuedouble>=16){const cJSON *parts=cJSON_GetObjectItemCaseSensitive(root,"collider_parts"),*entry;uint64_t next;if(!cJSON_IsArray(parts)||cJSON_GetArraySize(parts)>(int)AF_SCENE_MAX_COLLIDER_PARTS||!read_id(cJSON_GetObjectItemCaseSensitive(root,"next_part"),&next))goto part_fail;
        cJSON_ArrayForEach(entry,parts){static const char *const fields[]={"id","owner","name","position","rotation","scale","type","enabled","half_extents","radius","density"};af_collider_part p=af_collider_part_default();const cJSON *type=cJSON_GetObjectItemCaseSensitive(entry,"type"),*enabled=cJSON_GetObjectItemCaseSensitive(entry,"enabled");if(!exact_fields(entry,fields,11)||!read_id(cJSON_GetObjectItemCaseSensitive(entry,"id"),&p.id)||!p.id||(next&&p.id>=next)||af_scene_get_collider_part(scene,p.id)||!read_id(cJSON_GetObjectItemCaseSensitive(entry,"owner"),&p.owner)||!read_name(cJSON_GetObjectItemCaseSensitive(entry,"name"),p.name,sizeof(p.name))||!read_vector(cJSON_GetObjectItemCaseSensitive(entry,"position"),&p.local.position)||!read_vector(cJSON_GetObjectItemCaseSensitive(entry,"rotation"),&p.local.rotation)||!read_vector(cJSON_GetObjectItemCaseSensitive(entry,"scale"),&p.local.scale)||!cJSON_IsString(type)||(strcmp(type->valuestring,"box")&&strcmp(type->valuestring,"sphere"))||!cJSON_IsBool(enabled)||!read_vector(cJSON_GetObjectItemCaseSensitive(entry,"half_extents"),&p.collider.half_extents)||!read_float_field(entry,"radius",&p.collider.radius)||!read_float_field(entry,"density",&p.density))goto part_fail;p.collider.type=!strcmp(type->valuestring,"box")?AF_COLLIDER_BOX:AF_COLLIDER_SPHERE;p.collider.enabled=cJSON_IsTrue(enabled)!=0;if(!af_scene_add_collider_part(scene,p.owner,p))goto part_fail;scene->parts[scene->part_count-1].id=p.id;}scene->next_part_id=next;
    }
    for(size_t i=0;i<scene->count;++i)if(!af_scene_set_body(scene,scene->objects[i].id,scene->objects[i].body)){set_error(error,AF_SCENE_ERROR_FORMAT,"Dynamic bodies need a root sphere/box collider with a centered origin and positive scale.");goto fail;}

    if(version->valuedouble>=14){
        const cJSON *array=cJSON_GetObjectItemCaseSensitive(root,"joints"),*joint_item;uint64_t next;
        if(!cJSON_IsArray(array)||cJSON_GetArraySize(array)>(int)AF_SCENE_MAX_JOINTS||!read_id(cJSON_GetObjectItemCaseSensitive(root,"next_joint"),&next))goto joint_fail;
        cJSON_ArrayForEach(joint_item,array){
            static const char *const fields[]={"id","name","type","enabled","collide_connected","a","b","anchor_a","anchor_b","distance","axis_a","axis_b","reference_a","reference_b","limits_enabled","lower_limit","upper_limit","motor_enabled","motor_speed","max_motor_torque"};af_joint j=af_joint_default();
            const cJSON *type=cJSON_GetObjectItemCaseSensitive(joint_item,"type"),*enabled=cJSON_GetObjectItemCaseSensitive(joint_item,"enabled"),*collide=cJSON_GetObjectItemCaseSensitive(joint_item,"collide_connected");
            if(!exact_fields(joint_item,fields,version->valuedouble>=15?20:17)||!read_id(cJSON_GetObjectItemCaseSensitive(joint_item,"id"),&j.id)||!j.id||(next&&j.id>=next)||af_scene_get_joint(scene,j.id)||!read_name(cJSON_GetObjectItemCaseSensitive(joint_item,"name"),j.name,sizeof(j.name))||!cJSON_IsString(type)||(strcmp(type->valuestring,"distance")&&strcmp(type->valuestring,"hinge"))||!cJSON_IsBool(enabled)||!cJSON_IsBool(collide)||!read_id(cJSON_GetObjectItemCaseSensitive(joint_item,"a"),&j.a)||!read_id(cJSON_GetObjectItemCaseSensitive(joint_item,"b"),&j.b)||!read_vector(cJSON_GetObjectItemCaseSensitive(joint_item,"anchor_a"),&j.anchor_a)||!read_vector(cJSON_GetObjectItemCaseSensitive(joint_item,"anchor_b"),&j.anchor_b)||!read_float_field(joint_item,"distance",&j.distance))goto joint_fail;

            j.type=!strcmp(type->valuestring,"hinge")?AF_JOINT_HINGE:AF_JOINT_DISTANCE;const cJSON *limits=cJSON_GetObjectItemCaseSensitive(joint_item,"limits_enabled");
            if(!cJSON_IsBool(limits)||!read_vector(cJSON_GetObjectItemCaseSensitive(joint_item,"axis_a"),&j.axis_a)||!read_vector(cJSON_GetObjectItemCaseSensitive(joint_item,"axis_b"),&j.axis_b)||!read_vector(cJSON_GetObjectItemCaseSensitive(joint_item,"reference_a"),&j.reference_a)||!read_vector(cJSON_GetObjectItemCaseSensitive(joint_item,"reference_b"),&j.reference_b)||!read_float_field(joint_item,"lower_limit",&j.lower_limit)||!read_float_field(joint_item,"upper_limit",&j.upper_limit))goto joint_fail;
            j.limits_enabled=cJSON_IsTrue(limits)!=0;
            if(version->valuedouble>=15){const cJSON *motor=cJSON_GetObjectItemCaseSensitive(joint_item,"motor_enabled");if(!cJSON_IsBool(motor)||!read_float_field(joint_item,"motor_speed",&j.motor_speed)||!read_float_field(joint_item,"max_motor_torque",&j.max_motor_torque))goto joint_fail;j.motor_enabled=cJSON_IsTrue(motor)!=0;}
            j.enabled=cJSON_IsTrue(enabled)!=0;j.collide_connected=cJSON_IsTrue(collide)!=0;
            af_joint_id assigned=af_scene_add_joint(scene,j);if(!assigned)goto joint_fail;scene->joints[scene->joint_count-1].id=j.id;
        }scene->next_joint_id=next;
    }
    if(version->valuedouble>=22){
        static const char *const fields[]={"enabled","direction","speed","gust_strength","gust_frequency","air_density"};
        const cJSON *j=cJSON_GetObjectItemCaseSensitive(root,"wind"),*enabled=cJSON_GetObjectItemCaseSensitive(j,"enabled");af_wind w=af_wind_default();
        if(!exact_fields(j,fields,6)||!cJSON_IsBool(enabled)||!read_vector(cJSON_GetObjectItemCaseSensitive(j,"direction"),&w.direction)||
           !read_float_field(j,"speed",&w.speed)||!read_float_field(j,"gust_strength",&w.gust_strength)||!read_float_field(j,"gust_frequency",&w.gust_frequency)||!read_float_field(j,"air_density",&w.air_density)){
            set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid wind fields.");goto fail;}
        w.enabled=cJSON_IsTrue(enabled)!=0;if(!af_scene_set_wind(scene,w)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid wind direction or limits.");goto fail;}
    }
    if(version->valuedouble>=17&&!wa_decode(scene,root)){set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid water fields, owner or limits.");goto fail;}
    free(ids);
    return scene;
part_fail:set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid collider part fields, owner, budget or ID counter.");goto fail;
joint_fail:set_error(error,AF_SCENE_ERROR_FORMAT,"Invalid joint fields, endpoints or ID counter.");
fail:
    free(ids);
    af_scene_destroy(scene);
    return NULL;
}

af_scene *af_scene_load(const char *path, af_scene_error *error)
{
    if (error) *error = (af_scene_error){0};
    if (!path || !path[0]) { set_error(error, AF_SCENE_ERROR_ARGUMENT, "A scene path is required."); return NULL; }
    FILE *file = fopen(path, "rb");
    if (!file) { set_error(error, AF_SCENE_ERROR_IO, "Opening scene '%s': %s", path, strerror(errno)); return NULL; }
    char *text = NULL;
    af_scene *scene = NULL;
    cJSON *root = NULL;
    if (fseek(file, 0, SEEK_END) != 0) { set_error(error, AF_SCENE_ERROR_IO, "Seeking scene file."); goto done; }
    long file_size = ftell(file);
    if (file_size < 0) { set_error(error, AF_SCENE_ERROR_IO, "Reading scene size."); goto done; }
    if ((unsigned long)file_size > AF_SCENE_FILE_MAX_BYTES) {
        set_error(error, AF_SCENE_ERROR_FORMAT, "Scene exceeds the %u-byte file limit.", AF_SCENE_FILE_MAX_BYTES); goto done;
    }
    if (fseek(file, 0, SEEK_SET) != 0) { set_error(error, AF_SCENE_ERROR_IO, "Seeking scene file."); goto done; }
    size_t length = (size_t)file_size;
    text = malloc(length + 1);
    if (!text) { set_error(error, AF_SCENE_ERROR_MEMORY, "Allocating scene file buffer."); goto done; }
    if (fread(text, 1, length, file) != length || fgetc(file) != EOF || ferror(file)) {
        set_error(error, AF_SCENE_ERROR_IO, "Reading complete scene file."); goto done;
    }
    text[length] = 0;
    if (!af_json_valid_text(text, length)) { set_error(error, AF_SCENE_ERROR_FORMAT, "Invalid UTF-8 JSON text or embedded NUL."); goto done; }
    const char *parse_end = NULL;
    root = cJSON_ParseWithLengthOpts(text, length + 1, &parse_end, 1);
    if (!root) {
        set_error(error, AF_SCENE_ERROR_FORMAT, "Invalid JSON at byte %zu.", parse_end ? (size_t)(parse_end - text) : 0); goto done;
    }
    scene = decode_scene(root,path, error);
done:
    cJSON_Delete(root);
    free(text);
    if (fclose(file) != 0) {
        af_scene_destroy(scene); scene = NULL;
        set_error(error, AF_SCENE_ERROR_IO, "Closing scene file.");
    }
    return scene;
}

static bool add_id(cJSON *object, const char *name, uint64_t value)
{
    char text[32];
    snprintf(text, sizeof(text), "%" PRIu64, value);
    return cJSON_AddStringToObject(object, name, text) != NULL;
}

static bool add_vector(cJSON *object, const char *name, af_vec3 vector)
{
    if (!isfinite(vector.x) || !isfinite(vector.y) || !isfinite(vector.z)) return false;
    double values[] = {vector.x, vector.y, vector.z};
    cJSON *array = cJSON_CreateDoubleArray(values, 3);
    if (!array) return false;
    if (!cJSON_AddItemToObject(object, name, array)) { cJSON_Delete(array); return false; }
    return true;
}

static bool add_gameplay(cJSON *object,af_gameplay_component value){
    if(!af_gameplay_component_valid(value))return false;
    cJSON *g=cJSON_AddObjectToObject(object,"gameplay"),*p=g?cJSON_AddObjectToObject(g,"player"):NULL,*c=g?cJSON_AddObjectToObject(g,"camera"):NULL;
    return g&&p&&c&&cJSON_AddStringToObject(g,"role",role_names[value.role])&&cJSON_AddNumberToObject(g,"checkpoint_order",value.checkpoint_order)&&
        cJSON_AddBoolToObject(p,"enabled",value.player.enabled)&&add_id(p,"spawn",value.player.spawn)&&cJSON_AddNumberToObject(p,"radius",value.player.controller.radius)&&cJSON_AddNumberToObject(p,"half_height",value.player.controller.half_height)&&cJSON_AddNumberToObject(p,"speed",value.player.controller.speed)&&cJSON_AddNumberToObject(p,"jump_speed",value.player.controller.jump_speed)&&cJSON_AddNumberToObject(p,"gravity",value.player.controller.gravity)&&cJSON_AddNumberToObject(p,"slope_limit",value.player.controller.slope_limit)&&cJSON_AddNumberToObject(p,"skin",value.player.controller.skin)&&cJSON_AddNumberToObject(p,"ground_snap",value.player.controller.ground_snap)&&cJSON_AddNumberToObject(p,"mouse_sensitivity",value.player.mouse_sensitivity)&&
        cJSON_AddBoolToObject(c,"enabled",value.camera.enabled)&&cJSON_AddBoolToObject(c,"primary",value.camera.primary)&&cJSON_AddNumberToObject(c,"eye_height",value.camera.eye_height)&&cJSON_AddNumberToObject(c,"vertical_fov",value.camera.vertical_fov)&&cJSON_AddNumberToObject(c,"near_clip",value.camera.near_clip)&&cJSON_AddNumberToObject(c,"far_clip",value.camera.far_clip);
}
static char *encode_scene(const af_scene *scene,const char *scene_path)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON *objects = cJSON_AddArrayToObject(root, "objects");
    if (!objects || !cJSON_AddStringToObject(root, "format", "afterlight.scene") ||
        !cJSON_AddNumberToObject(root, "version", AF_SCENE_FILE_VERSION) || !add_id(root, "next_id", scene->next_id)) goto fail;


    cJSON *wind=cJSON_AddObjectToObject(root,"wind");af_wind w=af_scene_get_wind(scene);
    if(!wind||!af_wind_valid(w)||!cJSON_AddBoolToObject(wind,"enabled",w.enabled)||!add_vector(wind,"direction",w.direction)||!cJSON_AddNumberToObject(wind,"speed",w.speed)||!cJSON_AddNumberToObject(wind,"gust_strength",w.gust_strength)||!cJSON_AddNumberToObject(wind,"gust_frequency",w.gust_frequency)||!cJSON_AddNumberToObject(wind,"air_density",w.air_density))goto fail;
    if(!wa_encode(scene,root))goto fail;
    cJSON *parts=cJSON_AddArrayToObject(root,"collider_parts");if(!parts||!add_id(root,"next_part",scene->next_part_id))goto fail;
    for(size_t i=0;i<scene->part_count;++i){af_collider_part p=scene->parts[i];cJSON *entry=cJSON_CreateObject();if(!entry)goto fail;cJSON_AddItemToArray(parts,entry);if(!af_collider_part_valid(p)||!add_id(entry,"id",p.id)||!add_id(entry,"owner",p.owner)||!cJSON_AddStringToObject(entry,"name",p.name)||!add_vector(entry,"position",p.local.position)||!add_vector(entry,"rotation",p.local.rotation)||!add_vector(entry,"scale",p.local.scale)||!cJSON_AddStringToObject(entry,"type",p.collider.type==AF_COLLIDER_BOX?"box":"sphere")||!cJSON_AddBoolToObject(entry,"enabled",p.collider.enabled)||!add_vector(entry,"half_extents",p.collider.half_extents)||!cJSON_AddNumberToObject(entry,"radius",p.collider.radius)||!cJSON_AddNumberToObject(entry,"density",p.density))goto fail;}
    cJSON *joints=cJSON_AddArrayToObject(root,"joints");if(!joints||!add_id(root,"next_joint",scene->next_joint_id))goto fail;
    for(size_t i=0;i<scene->joint_count;++i){af_joint j=scene->joints[i];cJSON *item=cJSON_CreateObject();if(!item)goto fail;cJSON_AddItemToArray(joints,item);
        if(!af_joint_valid(j)||!add_id(item,"id",j.id)||!cJSON_AddStringToObject(item,"name",j.name)||!cJSON_AddStringToObject(item,"type",j.type==AF_JOINT_HINGE?"hinge":"distance")||!cJSON_AddBoolToObject(item,"enabled",j.enabled)||!cJSON_AddBoolToObject(item,"collide_connected",j.collide_connected)||!add_id(item,"a",j.a)||!add_id(item,"b",j.b)||!add_vector(item,"anchor_a",j.anchor_a)||!add_vector(item,"anchor_b",j.anchor_b)||!cJSON_AddNumberToObject(item,"distance",j.distance)||!add_vector(item,"axis_a",j.axis_a)||!add_vector(item,"axis_b",j.axis_b)||!add_vector(item,"reference_a",j.reference_a)||!add_vector(item,"reference_b",j.reference_b)||!cJSON_AddBoolToObject(item,"limits_enabled",j.limits_enabled)||!cJSON_AddNumberToObject(item,"lower_limit",j.lower_limit)||!cJSON_AddNumberToObject(item,"upper_limit",j.upper_limit)||!cJSON_AddBoolToObject(item,"motor_enabled",j.motor_enabled)||!cJSON_AddNumberToObject(item,"motor_speed",j.motor_speed)||!cJSON_AddNumberToObject(item,"max_motor_torque",j.max_motor_torque))goto fail;
    }
    cJSON *render=cJSON_AddObjectToObject(root,"render");
    if(!render||!cJSON_AddNumberToObject(render,"exposure",scene->render_settings.exposure)||!cJSON_AddBoolToObject(render,"tone_mapping",scene->render_settings.tone_mapping))goto fail;
    const af_render_settings *rs=&scene->render_settings;
    if(!cJSON_AddStringToObject(render,"anti_aliasing",rs->anti_aliasing==AF_AA_FXAA?"fxaa":"off")||!cJSON_AddNumberToObject(render,"aa_sharpness",rs->aa_sharpness)||!cJSON_AddNumberToObject(render,"taa_history_weight",rs->taa_history_weight))goto fail;
    if(!cJSON_AddBoolToObject(render,"underwater",rs->underwater)||!cJSON_AddBoolToObject(render,"refraction",rs->refraction)||!cJSON_AddNumberToObject(render,"refraction_strength",rs->refraction_strength))goto fail;
    if(!cJSON_AddBoolToObject(render,"bloom",rs->bloom)||!cJSON_AddNumberToObject(render,"bloom_strength",rs->bloom_strength)||!cJSON_AddNumberToObject(render,"bloom_threshold",rs->bloom_threshold)||!cJSON_AddNumberToObject(render,"bloom_radius",rs->bloom_radius)||
       !cJSON_AddBoolToObject(render,"environment_enabled",rs->environment_enabled)||!cJSON_AddNumberToObject(render,"environment_profile",rs->environment_profile)||!cJSON_AddNumberToObject(render,"environment_intensity",rs->environment_intensity)||!cJSON_AddNumberToObject(render,"environment_rotation",rs->environment_rotation)||!cJSON_AddNumberToObject(render,"local_shadow_resolution",rs->local_shadow_resolution))goto fail;
    if(!cJSON_AddStringToObject(render,"environment_asset",rs->environment_asset)||!cJSON_AddBoolToObject(render,"skybox",rs->skybox)||!cJSON_AddNumberToObject(render,"skybox_intensity",rs->skybox_intensity))goto fail;
    cJSON *locals=cJSON_AddArrayToObject(root,"local_lights");if(!locals)goto fail;
    for(size_t i=0;i<scene->local_light_count;++i){const af_local_light *value=&scene->local_lights[i];cJSON *local=cJSON_CreateObject();if(!local)goto fail;
        if(!cJSON_AddItemToArray(locals,local)){cJSON_Delete(local);goto fail;}
        if(!cJSON_AddStringToObject(local,"name",value->name)||!cJSON_AddStringToObject(local,"type",value->type==AF_LIGHT_POINT?"point":"spot")||!cJSON_AddBoolToObject(local,"enabled",value->enabled)||
            !add_vector(local,"position",value->position)||!add_vector(local,"color",value->color)||!add_vector(local,"direction",value->direction)||
            !cJSON_AddNumberToObject(local,"intensity",value->intensity)||!cJSON_AddNumberToObject(local,"range",value->range)||
            !cJSON_AddNumberToObject(local,"inner_angle",value->inner_angle)||!cJSON_AddNumberToObject(local,"outer_angle",value->outer_angle)||!cJSON_AddBoolToObject(local,"cast_shadows",value->cast_shadows)||!cJSON_AddNumberToObject(local,"shadow_bias",value->shadow_bias))goto fail;
    }
    cJSON *sh=cJSON_AddObjectToObject(root,"shadows");af_shadow_settings settings=scene->shadows;
    if(!sh||!cJSON_AddBoolToObject(sh,"enabled",settings.enabled)||!cJSON_AddStringToObject(sh,"mode",settings.mode==AF_SHADOW_SINGLE?"single":"cascaded")||
        !cJSON_AddNumberToObject(sh,"resolution",settings.resolution)||!cJSON_AddNumberToObject(sh,"distance",settings.distance)||!cJSON_AddNumberToObject(sh,"constant_bias",settings.constant_bias)||
        !cJSON_AddNumberToObject(sh,"slope_bias",settings.slope_bias)||!cJSON_AddNumberToObject(sh,"normal_bias",settings.normal_bias)||!cJSON_AddNumberToObject(sh,"filter_radius",settings.filter_radius)||
        !cJSON_AddNumberToObject(sh,"split_lambda",settings.split_lambda)||!cJSON_AddNumberToObject(sh,"blend_fraction",settings.blend_fraction)||!cJSON_AddBoolToObject(sh,"debug_cascades",settings.debug_cascades))goto fail;
    cJSON *light=cJSON_AddObjectToObject(root,"light");
    if(!light||!add_vector(light,"direction",scene->light.direction)||!add_vector(light,"color",scene->light.color)||!cJSON_AddNumberToObject(light,"intensity",scene->light.intensity)||!cJSON_AddNumberToObject(light,"ambient",scene->light.ambient))goto fail;
    cJSON *assets=cJSON_AddArrayToObject(root,"assets");if(!assets)goto fail;
    char folder[AF_ASSET_PATH_CAPACITY];if(!af_asset_resolve_path(scene_path,"placeholder",folder,sizeof(folder)))goto fail;
    char *slash=strrchr(folder,'/');if(slash)slash[1]=0;
    for(size_t i=0;i<scene->asset_count;++i){const af_scene_asset *a=&scene->assets[i];
        const char *reference=a->path;if(!strncmp(reference,folder,strlen(folder)))reference+=strlen(folder);
        cJSON *asset=cJSON_CreateObject();if(!asset)goto fail;
        if(!cJSON_AddItemToArray(assets,asset)){cJSON_Delete(asset);goto fail;}
        if(!cJSON_AddStringToObject(asset,"name",a->name)||!cJSON_AddStringToObject(asset,"kind",a->kind==AF_ASSET_ANIMATION?"animation":a->kind==AF_ASSET_ENVIRONMENT?"environment":a->kind==AF_ASSET_METALLIC_ROUGHNESS?"metallic_roughness":a->kind==AF_ASSET_AUDIO?"audio":a->kind==AF_ASSET_MESH?"mesh":a->kind==AF_ASSET_NORMAL_MAP?"normal_map":"texture")||!cJSON_AddStringToObject(asset,"path",reference))goto fail;
    }
    for (size_t i = 0; i < scene->count; ++i) {
        const af_scene_object *source = &scene->objects[i];
        cJSON *object = cJSON_CreateObject();
        if (!object) goto fail;
        if (!cJSON_AddItemToArray(objects, object)) { cJSON_Delete(object); goto fail; }
        if(!add_body(object,source->body)||!add_rig(object,source->rig)||!add_gameplay(object,source->gameplay))goto fail;
        const af_animation_component *a=&source->animation;const af_model_binding *binding=&source->model;
        cJSON *animation=cJSON_AddObjectToObject(object,"animation"),*model=cJSON_AddObjectToObject(object,"model");
        if(!animation||!model||!memchr(a->asset,0,sizeof(a->asset))||!valid_utf8((const unsigned char*)a->asset,strlen(a->asset))||!cJSON_AddStringToObject(animation,"asset",a->asset)||!cJSON_AddNumberToObject(animation,"clip",a->clip)||!cJSON_AddNumberToObject(animation,"speed",a->speed)||!cJSON_AddBoolToObject(animation,"autoplay",a->autoplay)||!cJSON_AddBoolToObject(animation,"loop",a->loop)||!add_id(model,"root",binding->root)||!cJSON_AddNumberToObject(model,"node",binding->node)||!cJSON_AddNumberToObject(model,"skin",binding->skin)||!cJSON_AddBoolToObject(model,"primitive",binding->primitive))goto fail;
        cJSON *material=cJSON_AddObjectToObject(object,"material");
        if(!material||!cJSON_AddStringToObject(material,"alpha_mode",source->material.alpha_mode==AF_ALPHA_MASK?"mask":source->material.alpha_mode==AF_ALPHA_BLEND?"blend":"opaque")||!cJSON_AddNumberToObject(material,"alpha_cutoff",source->material.alpha_cutoff))goto fail;
        double color[]={source->material.base_color.r,source->material.base_color.g,source->material.base_color.b,source->material.base_color.a};
        cJSON *rgba=cJSON_CreateDoubleArray(color,4);
        if(!material||!rgba){cJSON_Delete(rgba);goto fail;}
        if(!cJSON_AddItemToObject(material,"base_color",rgba)){cJSON_Delete(rgba);goto fail;}
        if(!cJSON_AddStringToObject(material,"texture",source->material.texture)||!cJSON_AddBoolToObject(material,"unlit",source->material.unlit)||!cJSON_AddBoolToObject(material,"cast_shadows",source->material.cast_shadows)||!cJSON_AddBoolToObject(material,"receive_shadows",source->material.receive_shadows))goto fail;
        if(!cJSON_AddBoolToObject(material,"physically_based",source->material.physically_based)||!cJSON_AddNumberToObject(material,"roughness",source->material.roughness)||!cJSON_AddNumberToObject(material,"metallic",source->material.metallic)||
            !add_vector(material,"emission",source->material.emission)||!cJSON_AddNumberToObject(material,"emission_strength",source->material.emission_strength)||
            !cJSON_AddStringToObject(material,"normal_map",source->material.normal_map)||!cJSON_AddNumberToObject(material,"normal_strength",source->material.normal_strength)||!cJSON_AddStringToObject(material,"metallic_roughness_map",source->material.metallic_roughness_map))goto fail;
        const af_collider *value=&source->collider;static const char *const types[]={"none","box","sphere","capsule","mesh","compound"};
        cJSON *collider=cJSON_AddObjectToObject(object,"collider");
        if(!collider||!cJSON_AddStringToObject(collider,"type",types[value->type])||!cJSON_AddBoolToObject(collider,"enabled",value->enabled)||!cJSON_AddBoolToObject(collider,"trigger",value->trigger)||
            !add_vector(collider,"center",value->center)||!add_vector(collider,"half_extents",value->half_extents)||!cJSON_AddNumberToObject(collider,"radius",value->radius)||
            !cJSON_AddNumberToObject(collider,"half_height",value->half_height)||!cJSON_AddNumberToObject(collider,"layer",value->layer))goto fail;
        if (!add_id(object,"parent",source->parent)||!add_id(object, "id", source->id) || !cJSON_AddStringToObject(object, "name", source->name) ||
            !cJSON_AddStringToObject(object, "mesh", source->mesh) ||
            !cJSON_AddBoolToObject(object, "visible", source->visible) ||
            !add_vector(object, "position", source->transform.position) ||
            !add_vector(object, "rotation", source->transform.rotation) ||
            !add_vector(object, "scale", source->transform.scale)) goto fail;
    }
    char *text = cJSON_Print(root);
    cJSON_Delete(root);
    return text;
fail:
    cJSON_Delete(root);
    return NULL;
}

bool af_scene_save(const af_scene *scene, const char *path, af_scene_error *error)
{
    if (error) *error = (af_scene_error){0};
    if (!scene || !path || !path[0]) { set_error(error, AF_SCENE_ERROR_ARGUMENT, "A scene and output path are required."); return false; }
    if (scene->count > AF_SCENE_FILE_MAX_OBJECTS) {
        set_error(error, AF_SCENE_ERROR_FORMAT, "Scene exceeds the %u-object file limit.", AF_SCENE_FILE_MAX_OBJECTS); return false;
    }
    if(!af_scene_joints_valid(scene)){set_error(error,AF_SCENE_ERROR_FORMAT,"Joint endpoints must remain valid physics bodies.");return false;}
    for(size_t i=0;i<scene->joint_count;++i)if(!valid_utf8((const unsigned char*)scene->joints[i].name,strlen(scene->joints[i].name))){set_error(error,AF_SCENE_ERROR_FORMAT,"Joint names must be UTF-8.");return false;}
    for(size_t i=0;i<scene->part_count;++i)if(!valid_utf8((const unsigned char*)scene->parts[i].name,strlen(scene->parts[i].name))){set_error(error,AF_SCENE_ERROR_FORMAT,"Collider part names must be UTF-8.");return false;}
    /* Scene names are byte strings in memory; JSON on disk must be UTF-8. */
    for (size_t i = 0; i < scene->count; ++i) {
        const af_scene_object *object = &scene->objects[i];
        if (!valid_utf8((const unsigned char *)object->name, strlen(object->name)) ||
            !valid_utf8((const unsigned char *)object->mesh, strlen(object->mesh))||
            !valid_utf8((const unsigned char*)object->material.texture,strlen(object->material.texture))||!valid_utf8((const unsigned char*)object->material.normal_map,strlen(object->material.normal_map))||!valid_utf8((const unsigned char*)object->material.metallic_roughness_map,strlen(object->material.metallic_roughness_map))) {
            set_error(error, AF_SCENE_ERROR_FORMAT, "Object names and mesh references must be UTF-8."); return false;
        }
    }
    for(size_t i=0;i<scene->asset_count;++i){const af_scene_asset *a=&scene->assets[i];
        if(!valid_utf8((const unsigned char*)a->path,strlen(a->path))||!valid_utf8((const unsigned char*)a->name,strlen(a->name))){
            set_error(error,AF_SCENE_ERROR_FORMAT,"Asset names/paths must be UTF-8.");return false;
        }
    }
    for(size_t i=0;i<scene->local_light_count;++i)if(!valid_utf8((const unsigned char*)scene->local_lights[i].name,strlen(scene->local_lights[i].name))){set_error(error,AF_SCENE_ERROR_FORMAT,"Light names must be UTF-8.");return false;}
    if(!valid_utf8((const unsigned char*)scene->render_settings.environment_asset,strlen(scene->render_settings.environment_asset))){set_error(error,AF_SCENE_ERROR_FORMAT,"Environment asset must be UTF-8.");return false;}
    char *text = encode_scene(scene,path);
    if (!text) { set_error(error, AF_SCENE_ERROR_MEMORY, "Encoding scene JSON."); return false; }
    size_t length = strlen(text);
    if (length >= AF_SCENE_FILE_MAX_BYTES || strlen(path) > SIZE_MAX - 80) {
        cJSON_free(text); set_error(error, AF_SCENE_ERROR_FORMAT, "Scene or output path is too large."); return false;
    }
    char *temporary = malloc(strlen(path) + 80);
    if (!temporary) { cJSON_free(text); set_error(error, AF_SCENE_ERROR_MEMORY, "Allocating temporary path."); return false; }
    static _Atomic uint64_t sequence;
    FILE *file = NULL;
    for (unsigned attempt = 0; attempt < 64; ++attempt) {
        uint64_t nonce = atomic_fetch_add(&sequence, 1);
        snprintf(temporary, strlen(path) + 80, "%s.afterlight-%" PRIx64 "-%" PRIx64 ".tmp", path, (uint64_t)time(NULL), nonce);
        file = fopen(temporary, "wbx");
        if (file || errno != EEXIST) break;
    }
    bool success = false;
    if (!file) { set_error(error, AF_SCENE_ERROR_IO, "Creating temporary scene file: %s", strerror(errno)); goto done; }
    bool written = fwrite(text, 1, length, file) == length && fputc('\n', file) != EOF && fflush(file) == 0;
    if (fclose(file) != 0) written = false;
    if (!written) { set_error(error, AF_SCENE_ERROR_IO, "Writing complete scene file."); goto remove_temporary; }
#ifdef _WIN32
    success = MoveFileExA(temporary, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!success) set_error(error, AF_SCENE_ERROR_IO, "Replacing scene file (Windows error %lu).", (unsigned long)GetLastError());
#else
    success = rename(temporary, path) == 0;
    if (!success) set_error(error, AF_SCENE_ERROR_IO, "Replacing scene file: %s", strerror(errno));
#endif
remove_temporary:
    if (!success) (void)remove(temporary);
done:
    free(temporary);
    cJSON_free(text);
    return success;
}

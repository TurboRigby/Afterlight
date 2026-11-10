/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "landscape_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHUNK_V AF_LANDSCAPE_CHUNK_VERTICES
#define CHUNK_Q AF_LANDSCAPE_CHUNK_QUADS

static bool read_float(const cJSON *j, float *out) {
    if (!cJSON_IsNumber(j) || !isfinite(j->valuedouble))
        return false;
    *out = (float)j->valuedouble;
    return true;
}

static bool read_vec3(const cJSON *j, af_vec3 *out) {
    if (!cJSON_IsArray(j) || cJSON_GetArraySize(j) != 3)
        return false;
    float x, y, z;
    if (!read_float(cJSON_GetArrayItem(j, 0), &x) || !read_float(cJSON_GetArrayItem(j, 1), &y) ||
        !read_float(cJSON_GetArrayItem(j, 2), &z))
        return false;
    *out = (af_vec3){x, y, z};
    return true;
}

static cJSON *write_vec3(af_vec3 v) {
    cJSON *arr = cJSON_CreateArray();
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.x));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.y));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(v.z));
    return arr;
}

bool ls_decode(af_scene *scene, const cJSON *root) {
    if (!scene || !root)
        return false;
    const cJSON *lj = cJSON_GetObjectItemCaseSensitive(root, "landscape");
    if (!lj || cJSON_IsNull(lj))
        return true; /* Optional or null landscape */
    if (!cJSON_IsObject(lj))
        return false;

    const cJSON *cx_item = cJSON_GetObjectItemCaseSensitive(lj, "chunks_x");
    const cJSON *cz_item = cJSON_GetObjectItemCaseSensitive(lj, "chunks_z");
    const cJSON *sp_item = cJSON_GetObjectItemCaseSensitive(lj, "spacing");

    uint32_t cx = (cx_item && cJSON_IsNumber(cx_item)) ? (uint32_t)cx_item->valueint : 4;
    uint32_t cz = (cz_item && cJSON_IsNumber(cz_item)) ? (uint32_t)cz_item->valueint : 4;
    float spacing = (sp_item && cJSON_IsNumber(sp_item)) ? (float)sp_item->valuedouble : 1.0f;

    af_landscape *ls = af_landscape_create(cx, cz, spacing);
    if (!ls)
        return false;

    const cJSON *en_item = cJSON_GetObjectItemCaseSensitive(lj, "enabled");
    if (en_item && cJSON_IsBool(en_item))
        ls->enabled = cJSON_IsTrue(en_item);

    const cJSON *orig_item = cJSON_GetObjectItemCaseSensitive(lj, "origin");
    if (orig_item)
        read_vec3(orig_item, &ls->origin);

    /* Layers */
    const cJSON *layers_arr = cJSON_GetObjectItemCaseSensitive(lj, "layers");
    if (layers_arr && cJSON_IsArray(layers_arr)) {
        size_t count = (size_t)cJSON_GetArraySize(layers_arr);
        if (count > AF_LANDSCAPE_MAX_LAYERS)
            count = AF_LANDSCAPE_MAX_LAYERS;
        for (size_t i = 0; i < count; ++i) {
            const cJSON *item = cJSON_GetArrayItem(layers_arr, (int)i);
            if (!cJSON_IsObject(item))
                continue;
            af_terrain_layer *l = &ls->layers[i];

            const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
            if (name && cJSON_IsString(name))
                snprintf(l->name, sizeof(l->name), "%s", name->valuestring);

            const cJSON *diff = cJSON_GetObjectItemCaseSensitive(item, "diffuse");
            if (diff && cJSON_IsString(diff))
                snprintf(l->diffuse_texture, sizeof(l->diffuse_texture), "%s", diff->valuestring);

            const cJSON *norm = cJSON_GetObjectItemCaseSensitive(item, "normal");
            if (norm && cJSON_IsString(norm))
                snprintf(l->normal_texture, sizeof(l->normal_texture), "%s", norm->valuestring);

            const cJSON *rough = cJSON_GetObjectItemCaseSensitive(item, "roughness");
            if (rough && cJSON_IsNumber(rough))
                l->roughness = (float)rough->valuedouble;

            const cJSON *metal = cJSON_GetObjectItemCaseSensitive(item, "metalness");
            if (metal && cJSON_IsNumber(metal))
                l->metalness = (float)metal->valuedouble;

            const cJSON *scale = cJSON_GetObjectItemCaseSensitive(item, "uv_scale");
            if (scale && cJSON_IsNumber(scale))
                l->uv_scale = (float)scale->valuedouble;
        }
    }

    /* Foliage Layers */
    const cJSON *fl_arr = cJSON_GetObjectItemCaseSensitive(lj, "foliage_layers");
    if (fl_arr && cJSON_IsArray(fl_arr)) {
        size_t count = (size_t)cJSON_GetArraySize(fl_arr);
        for (size_t i = 0; i < count && i < AF_LANDSCAPE_MAX_FOLIAGE_LAYERS; ++i) {
            const cJSON *item = cJSON_GetArrayItem(fl_arr, (int)i);
            if (!cJSON_IsObject(item))
                continue;

            const cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
            const cJSON *mesh = cJSON_GetObjectItemCaseSensitive(item, "mesh");

            int idx =
                af_landscape_add_foliage_layer(ls, name ? name->valuestring : "Layer", mesh ? mesh->valuestring : "");
            if (idx >= 0) {
                af_foliage_layer *fl = &ls->foliage_layers[idx];
                const cJSON *vis = cJSON_GetObjectItemCaseSensitive(item, "visible");
                if (vis && cJSON_IsBool(vis))
                    fl->visible = cJSON_IsTrue(vis);

                const cJSON *shadows = cJSON_GetObjectItemCaseSensitive(item, "cast_shadows");
                if (shadows && cJSON_IsBool(shadows))
                    fl->cast_shadows = cJSON_IsTrue(shadows);

                const cJSON *dist = cJSON_GetObjectItemCaseSensitive(item, "max_distance");
                if (dist && cJSON_IsNumber(dist))
                    fl->max_distance = (float)dist->valuedouble;

                /* Rules */
                const cJSON *rj = cJSON_GetObjectItemCaseSensitive(item, "rules");
                if (rj && cJSON_IsObject(rj)) {
                    const cJSON *ry = cJSON_GetObjectItemCaseSensitive(rj, "random_yaw");
                    if (ry && cJSON_IsBool(ry))
                        fl->rules.random_yaw = cJSON_IsTrue(ry);
                    const cJSON *ymin = cJSON_GetObjectItemCaseSensitive(rj, "yaw_min");
                    if (ymin && cJSON_IsNumber(ymin))
                        fl->rules.yaw_min = (float)ymin->valuedouble;
                    const cJSON *ymax = cJSON_GetObjectItemCaseSensitive(rj, "yaw_max");
                    if (ymax && cJSON_IsNumber(ymax))
                        fl->rules.yaw_max = (float)ymax->valuedouble;

                    const cJSON *smin = cJSON_GetObjectItemCaseSensitive(rj, "scale_min");
                    if (smin)
                        read_vec3(smin, &fl->rules.scale_min);
                    const cJSON *smax = cJSON_GetObjectItemCaseSensitive(rj, "scale_max");
                    if (smax)
                        read_vec3(smax, &fl->rules.scale_max);

                    const cJSON *dens = cJSON_GetObjectItemCaseSensitive(rj, "density");
                    if (dens && cJSON_IsNumber(dens))
                        fl->rules.density = (float)dens->valuedouble;
                    const cJSON *spac = cJSON_GetObjectItemCaseSensitive(rj, "min_spacing");
                    if (spac && cJSON_IsNumber(spac))
                        fl->rules.min_spacing = (float)spac->valuedouble;
                    const cJSON *norm_al = cJSON_GetObjectItemCaseSensitive(rj, "align_to_normal");
                    if (norm_al && cJSON_IsNumber(norm_al))
                        fl->rules.align_to_normal = (float)norm_al->valuedouble;
                    const cJSON *off_y = cJSON_GetObjectItemCaseSensitive(rj, "offset_y");
                    if (off_y && cJSON_IsNumber(off_y))
                        fl->rules.offset_y = (float)off_y->valuedouble;
                }
            }
        }
    }

    /* Chunks */
    const cJSON *chunks_arr = cJSON_GetObjectItemCaseSensitive(lj, "chunks");
    if (chunks_arr && cJSON_IsArray(chunks_arr)) {
        size_t c_count = (size_t)cJSON_GetArraySize(chunks_arr);
        for (size_t i = 0; i < c_count && i < ls->chunk_count; ++i) {
            const cJSON *cj = cJSON_GetArrayItem(chunks_arr, (int)i);
            if (!cJSON_IsObject(cj))
                continue;

            const cJSON *cx_val = cJSON_GetObjectItemCaseSensitive(cj, "cx");
            const cJSON *cz_val = cJSON_GetObjectItemCaseSensitive(cj, "cz");
            int32_t chunk_x = cx_val ? (int32_t)cx_val->valueint : (int32_t)(i % ls->chunk_count_x);
            int32_t chunk_z = cz_val ? (int32_t)cz_val->valueint : (int32_t)(i / ls->chunk_count_x);

            size_t c_idx = (size_t)chunk_z * ls->chunk_count_x + (size_t)chunk_x;
            if (c_idx >= ls->chunk_count)
                continue;
            af_terrain_chunk *c = &ls->chunks[c_idx];

            /* Heights */
            const cJSON *h_arr = cJSON_GetObjectItemCaseSensitive(cj, "heights");
            if (h_arr && cJSON_IsArray(h_arr)) {
                size_t h_count = (size_t)cJSON_GetArraySize(h_arr);
                for (size_t v = 0; v < h_count && v < CHUNK_V * CHUNK_V; ++v) {
                    const cJSON *hv = cJSON_GetArrayItem(h_arr, (int)v);
                    if (hv && cJSON_IsNumber(hv))
                        c->heights[v] = (float)hv->valuedouble;
                }
            }

            /* Weights */
            const cJSON *w_arr = cJSON_GetObjectItemCaseSensitive(cj, "weights");
            if (w_arr && cJSON_IsArray(w_arr)) {
                size_t w_count = (size_t)cJSON_GetArraySize(w_arr);
                for (size_t b = 0; b < w_count && b < CHUNK_V * CHUNK_V * 4; ++b) {
                    const cJSON *wv = cJSON_GetArrayItem(w_arr, (int)b);
                    if (wv && cJSON_IsNumber(wv))
                        c->weights[b] = (uint8_t)wv->valueint;
                }
            }

            /* Foliage instances */
            const cJSON *f_arr = cJSON_GetObjectItemCaseSensitive(cj, "foliage");
            if (f_arr && cJSON_IsArray(f_arr)) {
                size_t f_layer_count = (size_t)cJSON_GetArraySize(f_arr);
                for (size_t fl = 0; fl < f_layer_count && fl < AF_LANDSCAPE_MAX_FOLIAGE_LAYERS; ++fl) {
                    const cJSON *fl_entry = cJSON_GetArrayItem(f_arr, (int)fl);
                    if (!cJSON_IsObject(fl_entry))
                        continue;

                    const cJSON *l_idx_item = cJSON_GetObjectItemCaseSensitive(fl_entry, "layer");
                    size_t l_idx = l_idx_item ? (size_t)l_idx_item->valueint : fl;
                    if (l_idx >= ls->foliage_layer_count)
                        continue;

                    const cJSON *inst_arr = cJSON_GetObjectItemCaseSensitive(fl_entry, "instances");
                    if (!inst_arr || !cJSON_IsArray(inst_arr))
                        continue;

                    size_t num_inst = (size_t)cJSON_GetArraySize(inst_arr);
                    af_foliage_chunk *fc = &c->foliage[l_idx];
                    fc->instances = calloc(num_inst, sizeof(af_foliage_instance));
                    fc->capacity = num_inst;
                    fc->count = 0;

                    for (size_t inst_i = 0; inst_i < num_inst; ++inst_i) {
                        const cJSON *inst_v = cJSON_GetArrayItem(inst_arr, (int)inst_i);
                        if (!cJSON_IsArray(inst_v) || cJSON_GetArraySize(inst_v) < 9)
                            continue;

                        af_foliage_instance inst;
                        inst.transform.position.x = (float)cJSON_GetArrayItem(inst_v, 0)->valuedouble;
                        inst.transform.position.y = (float)cJSON_GetArrayItem(inst_v, 1)->valuedouble;
                        inst.transform.position.z = (float)cJSON_GetArrayItem(inst_v, 2)->valuedouble;

                        inst.transform.rotation.x = (float)cJSON_GetArrayItem(inst_v, 3)->valuedouble;
                        inst.transform.rotation.y = (float)cJSON_GetArrayItem(inst_v, 4)->valuedouble;
                        inst.transform.rotation.z = (float)cJSON_GetArrayItem(inst_v, 5)->valuedouble;

                        inst.transform.scale.x = (float)cJSON_GetArrayItem(inst_v, 6)->valuedouble;
                        inst.transform.scale.y = (float)cJSON_GetArrayItem(inst_v, 7)->valuedouble;
                        inst.transform.scale.z = (float)cJSON_GetArrayItem(inst_v, 8)->valuedouble;

                        fc->instances[fc->count++] = inst;
                        ls->foliage_layers[l_idx].total_instances++;
                        ls->total_foliage_instances++;
                    }
                }
            }

            /* Update bounds */
            float min_y = c->heights[0], max_y = c->heights[0];
            for (size_t v = 1; v < CHUNK_V * CHUNK_V; ++v) {
                if (c->heights[v] < min_y)
                    min_y = c->heights[v];
                if (c->heights[v] > max_y)
                    max_y = c->heights[v];
            }
            c->bounds_min.y = min_y;
            c->bounds_max.y = max_y;
            c->dirty_mesh = true;
        }
    }

    af_landscape_recompute_normals(ls, 0, 0, (int32_t)ls->chunk_count_x - 1, (int32_t)ls->chunk_count_z - 1);
    af_scene_set_landscape(scene, ls);
    return true;
}

bool ls_encode(const af_scene *scene, cJSON *root) {
    if (!scene || !root)
        return false;
    const af_landscape *ls = af_scene_get_landscape(scene);
    if (!ls)
        return cJSON_AddNullToObject(root, "landscape") != NULL;

    cJSON *lj = cJSON_CreateObject();
    if (!lj)
        return false;
    cJSON_AddItemToObject(root, "landscape", lj);

    cJSON_AddBoolToObject(lj, "enabled", ls->enabled);
    cJSON_AddNumberToObject(lj, "chunks_x", ls->chunk_count_x);
    cJSON_AddNumberToObject(lj, "chunks_z", ls->chunk_count_z);
    cJSON_AddNumberToObject(lj, "spacing", ls->vertex_spacing);
    cJSON_AddItemToObject(lj, "origin", write_vec3(ls->origin));

    /* Layers */
    cJSON *layers_arr = cJSON_CreateArray();
    cJSON_AddItemToObject(lj, "layers", layers_arr);
    for (size_t i = 0; i < ls->layer_count; ++i) {
        const af_terrain_layer *l = &ls->layers[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", l->name);
        cJSON_AddStringToObject(item, "diffuse", l->diffuse_texture);
        cJSON_AddStringToObject(item, "normal", l->normal_texture);
        cJSON_AddNumberToObject(item, "roughness", l->roughness);
        cJSON_AddNumberToObject(item, "metalness", l->metalness);
        cJSON_AddNumberToObject(item, "uv_scale", l->uv_scale);
        cJSON_AddItemToArray(layers_arr, item);
    }

    /* Foliage Layers */
    cJSON *fl_arr = cJSON_CreateArray();
    cJSON_AddItemToObject(lj, "foliage_layers", fl_arr);
    for (size_t i = 0; i < ls->foliage_layer_count; ++i) {
        const af_foliage_layer *fl = &ls->foliage_layers[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", fl->name);
        cJSON_AddStringToObject(item, "mesh", fl->mesh);
        cJSON_AddBoolToObject(item, "visible", fl->visible);
        cJSON_AddBoolToObject(item, "cast_shadows", fl->cast_shadows);
        cJSON_AddNumberToObject(item, "max_distance", fl->max_distance);

        cJSON *rules = cJSON_CreateObject();
        cJSON_AddBoolToObject(rules, "random_yaw", fl->rules.random_yaw);
        cJSON_AddNumberToObject(rules, "yaw_min", fl->rules.yaw_min);
        cJSON_AddNumberToObject(rules, "yaw_max", fl->rules.yaw_max);
        cJSON_AddItemToObject(rules, "scale_min", write_vec3(fl->rules.scale_min));
        cJSON_AddItemToObject(rules, "scale_max", write_vec3(fl->rules.scale_max));
        cJSON_AddNumberToObject(rules, "density", fl->rules.density);
        cJSON_AddNumberToObject(rules, "min_spacing", fl->rules.min_spacing);
        cJSON_AddNumberToObject(rules, "align_to_normal", fl->rules.align_to_normal);
        cJSON_AddNumberToObject(rules, "offset_y", fl->rules.offset_y);
        cJSON_AddItemToObject(item, "rules", rules);

        cJSON_AddItemToArray(fl_arr, item);
    }

    /* Chunks */
    cJSON *chunks_arr = cJSON_CreateArray();
    cJSON_AddItemToObject(lj, "chunks", chunks_arr);
    for (size_t i = 0; i < ls->chunk_count; ++i) {
        const af_terrain_chunk *c = &ls->chunks[i];
        cJSON *cj = cJSON_CreateObject();
        cJSON_AddNumberToObject(cj, "cx", c->chunk_x);
        cJSON_AddNumberToObject(cj, "cz", c->chunk_z);

        /* Heights */
        cJSON *h_arr = cJSON_CreateArray();
        for (size_t v = 0; v < CHUNK_V * CHUNK_V; ++v) {
            cJSON_AddItemToArray(h_arr, cJSON_CreateNumber(roundf(c->heights[v] * 1000.0f) / 1000.0f));
        }
        cJSON_AddItemToObject(cj, "heights", h_arr);

        /* Weights */
        cJSON *w_arr = cJSON_CreateArray();
        for (size_t b = 0; b < CHUNK_V * CHUNK_V * 4; ++b) {
            cJSON_AddItemToArray(w_arr, cJSON_CreateNumber(c->weights[b]));
        }
        cJSON_AddItemToObject(cj, "weights", w_arr);

        /* Foliage */
        cJSON *f_arr = cJSON_CreateArray();
        for (size_t fl = 0; fl < ls->foliage_layer_count; ++fl) {
            const af_foliage_chunk *fc = &c->foliage[fl];
            if (fc->count == 0)
                continue;

            cJSON *fl_entry = cJSON_CreateObject();
            cJSON_AddNumberToObject(fl_entry, "layer", (double)fl);
            cJSON *inst_arr = cJSON_CreateArray();
            for (size_t inst_i = 0; inst_i < fc->count; ++inst_i) {
                const af_foliage_instance *inst = &fc->instances[inst_i];
                cJSON *inst_v = cJSON_CreateArray();
                cJSON_AddItemToArray(inst_v, cJSON_CreateNumber(roundf(inst->transform.position.x * 100.0f) / 100.0f));
                cJSON_AddItemToArray(inst_v, cJSON_CreateNumber(roundf(inst->transform.position.y * 100.0f) / 100.0f));
                cJSON_AddItemToArray(inst_v, cJSON_CreateNumber(roundf(inst->transform.position.z * 100.0f) / 100.0f));

                cJSON_AddItemToArray(inst_v,
                                     cJSON_CreateNumber(roundf(inst->transform.rotation.x * 1000.0f) / 1000.0f));
                cJSON_AddItemToArray(inst_v,
                                     cJSON_CreateNumber(roundf(inst->transform.rotation.y * 1000.0f) / 1000.0f));
                cJSON_AddItemToArray(inst_v,
                                     cJSON_CreateNumber(roundf(inst->transform.rotation.z * 1000.0f) / 1000.0f));

                cJSON_AddItemToArray(inst_v, cJSON_CreateNumber(roundf(inst->transform.scale.x * 100.0f) / 100.0f));
                cJSON_AddItemToArray(inst_v, cJSON_CreateNumber(roundf(inst->transform.scale.y * 100.0f) / 100.0f));
                cJSON_AddItemToArray(inst_v, cJSON_CreateNumber(roundf(inst->transform.scale.z * 100.0f) / 100.0f));
                cJSON_AddItemToArray(inst_arr, inst_v);
            }
            cJSON_AddItemToObject(fl_entry, "instances", inst_arr);
            cJSON_AddItemToArray(f_arr, fl_entry);
        }
        cJSON_AddItemToObject(cj, "foliage", f_arr);
        cJSON_AddItemToArray(chunks_arr, cj);
    }
    return true;
}

/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "asset_browser.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
static bool contains(const char *text, const char *query) {
    if (!query[0])
        return true;
    for (; *text; ++text) {
        const unsigned char *a = (const unsigned char *)text, *b = (const unsigned char *)query;
        while (*a && *b && tolower(*a) == tolower(*b)) {
            ++a;
            ++b;
        }
        if (!*b)
            return true;
    }
    return false;
}
bool ed_asset_matches(const af_scene_asset *asset, const char *query, int kind) {
    return asset && query && (kind < 0 || kind == (int)asset->kind) &&
           (contains(asset->name, query) || contains(asset->path, query));
}
bool ed_document_assign_asset(ed_document *d, const char *name) {
    if (!d || !name)
        return false;
    const af_scene_asset *asset = af_scene_find_asset(d->scene, name);
    if (!asset)
        return !strcmp(name, "cube") && ed_document_mesh(d, "cube");
    if (asset->kind == AF_ASSET_AUDIO || asset->kind == AF_ASSET_ANIMATION)
        return false;
    if (asset->kind == AF_ASSET_ENVIRONMENT) {
        af_render_settings settings = af_scene_get_render_settings(d->scene);
        snprintf(settings.environment_asset, sizeof(settings.environment_asset), "%s", name);
        settings.environment_enabled = true;
        return ed_document_render_settings(d, settings);
    }
    if (asset->kind == AF_ASSET_MESH)
        return ed_document_mesh(d, name);
    const af_scene_object *object = af_scene_get_object(d->scene, d->selection);
    if (!object)
        return false;
    af_material material = object->material;
    snprintf(asset->kind == AF_ASSET_METALLIC_ROUGHNESS ? material.metallic_roughness_map
             : asset->kind == AF_ASSET_NORMAL_MAP       ? material.normal_map
                                                        : material.texture,
             AF_MESH_NAME_CAPACITY, "%s", name);
    return ed_document_material(d, material);
}

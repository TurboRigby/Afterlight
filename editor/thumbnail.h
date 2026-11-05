/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ED_THUMBNAIL_H
#define ED_THUMBNAIL_H
#include <afterlight/afterlight.h>
#define ED_THUMBNAIL_SIZE 64
#define ED_THUMBNAIL_CACHE_SIZE 64
typedef struct ed_thumbnail_entry {
    char name[AF_MESH_NAME_CAPACITY];
    uint64_t signature,last_used;
    bool occupied,ready,material;
} ed_thumbnail_entry;
typedef struct ed_thumbnails {
    ed_thumbnail_entry entries[ED_THUMBNAIL_CACHE_SIZE];
    uint64_t frame;
    unsigned budget;
} ed_thumbnails;
/* CPU-only previews. Texture aspect/alpha, actual mesh silhouette/depth and
   material sphere shading are retained without touching the active scene. */
bool ed_thumbnail_generate(const af_scene *scene,const af_scene_asset *asset,const af_material *material,unsigned char rgba[ED_THUMBNAIL_SIZE*ED_THUMBNAIL_SIZE*4]);
void ed_thumbnails_begin(ed_thumbnails *cache);
void ed_thumbnails_invalidate(ed_thumbnails *cache);
uint32_t ed_thumbnails_get(ed_thumbnails *cache,af_engine *engine,const af_scene *scene,const af_scene_asset *asset,const af_material *material);
#endif

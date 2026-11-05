/* SPDX-License-Identifier: MPL-2.0
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "document.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(c) do{if(!(c)){fprintf(stderr,"CHECK %s:%d: %s\n",__FILE__,__LINE__,#c);return 1;}}while(0)
int main(void)
{
    ed_document document;
    CHECK(ed_document_init(&document)&&!ed_document_dirty(&document));
    CHECK(ed_document_add_cube(&document));
    af_object_id first=document.selection;
    CHECK(first&&ed_document_dirty(&document)&&af_scene_object_count(document.scene)==1);
    ed_document_end_edit(&document);
    af_transform transform=af_transform_identity();transform.position.x=3;
    CHECK(ed_document_transform(&document,transform));transform.position.x=4;
    CHECK(ed_document_transform(&document,transform));
    CHECK(ed_document_undo(&document));
    CHECK(af_scene_get_object(document.scene,first)->transform.position.x==0);
    CHECK(ed_document_redo(&document));
    CHECK(af_scene_get_object(document.scene,first)->transform.position.x==4);
    ed_document_end_edit(&document);
    transform.position.x=NAN;
    uint64_t revision=document.revision;
    CHECK(!ed_document_transform(&document,transform)&&document.revision==revision);
    CHECK(ed_document_name(&document,"Edited cube"));ed_document_end_edit(&document);
    CHECK(ed_document_visible(&document,false));
    CHECK(ed_document_duplicate(&document));
    af_object_id second=document.selection;
    CHECK(second!=first&&af_scene_object_count(document.scene)==2);
    CHECK(!af_scene_get_object(document.scene,second)->visible);
    CHECK(ed_document_delete(&document)&&!af_scene_get_object(document.scene,second));
    CHECK(ed_document_undo(&document)&&af_scene_get_object(document.scene,second));
    CHECK(ed_document_save(&document,"editor-document.afterlight.json")&&!ed_document_dirty(&document));
    CHECK(ed_document_name(&document,"Changed"));ed_document_end_edit(&document);
    CHECK(ed_document_dirty(&document)&&ed_document_undo(&document)&&!ed_document_dirty(&document));
    CHECK(ed_document_redo(&document)&&ed_document_dirty(&document));
    revision=document.revision;
    CHECK(!ed_document_load(&document,"no-such-scene.json",false));
    CHECK(document.revision==revision&&ed_document_dirty(&document));
    CHECK(!ed_document_save(&document,"missing-parent/document.json")&&ed_document_dirty(&document));
    CHECK(ed_document_load(&document,"editor-document.afterlight.json",false)&&!ed_document_dirty(&document));
    CHECK(!strcmp(af_scene_get_object(document.scene,second)->name,"Edited cube"));
    CHECK(!document.undo_count&&!document.redo_count);
    af_shadow_settings shadows=af_scene_get_shadows(document.scene);shadows.enabled=false;shadows.mode=AF_SHADOW_SINGLE;
    CHECK(ed_document_shadows(&document,shadows));ed_document_end_edit(&document);
    CHECK(!af_scene_get_shadows(document.scene).enabled&&ed_document_undo(&document)&&af_scene_get_shadows(document.scene).enabled);
    CHECK(ed_document_redo(&document)&&!af_scene_get_shadows(document.scene).enabled);
    af_material material=af_scene_get_object(document.scene,document.selection)->material;material.cast_shadows=false;
    CHECK(ed_document_material(&document,material));ed_document_end_edit(&document);CHECK(ed_document_undo(&document)&&af_scene_get_object(document.scene,document.selection)->material.cast_shadows);
    CHECK(ed_document_new(&document)&&af_scene_object_count(document.scene)==0&&!ed_document_dirty(&document));
    for(unsigned i=0;i<80;++i){CHECK(ed_document_add_cube(&document));ed_document_end_edit(&document);}
    CHECK(document.undo_count==ED_HISTORY_LIMIT);
    ed_document_destroy(&document);
    puts("Editor document: edits, grouping, undo/redo, selection, dirty state, save/load, failed IO, history bounds passed.");
    return 0;
}

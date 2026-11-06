/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/animation.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Events %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv){CHECK(argc==2);af_animation_file file;af_asset_error error;CHECK(af_animation_file_load(argv[1],&file,&error));const af_animation_clip *c=&file.clips[1];CHECK(c->event_count==4);af_animation_events e;
    CHECK(af_animation_collect_events(c,0,.25,false,&e)&&e.count==1&&!strcmp(e.items[0].name,"foot_left"));
    CHECK(af_animation_collect_events(c,.25,.01,false,&e)&&e.count==0);
    CHECK(af_animation_collect_events(c,1.7,.6,true,&e)&&e.count==2&&!strcmp(e.items[0].name,"foot_right")&&!strcmp(e.items[1].name,"foot_left"));
    CHECK(af_animation_collect_events(c,.3,-.6,true,&e)&&e.count==2&&!strcmp(e.items[0].name,"foot_left")&&!strcmp(e.items[1].name,"foot_right"));
    CHECK(af_animation_collect_events(c,0,100,true,&e)&&e.count==16&&e.dropped==184);
    CHECK(af_animation_collect_events(c,0,100,false,&e)&&e.count==4&&e.dropped==0);
    af_animation_file_free(&file);puts("Events: loaded markers, exact boundaries, loop/reverse ordering and bounded overflow passed.");return 0;}

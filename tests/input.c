/* SPDX-License-Identifier: MPL-2.0 */
#include <afterlight/input.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"Input %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){af_input_map m=af_input_map_default(),loaded={0};af_input_sample s={0};s.keys[AF_KEY_W]=true;CHECK(af_input_value(&m,&s,"MoveY")==1);s.axes[0]=.1f;CHECK(af_input_value(&m,&s,"MoveX")==0);s.axes[0]=.59f;CHECK(fabsf(af_input_value(&m,&s,"MoveX")-.5f)<.001f);s.buttons[0]=true;CHECK(af_input_value(&m,&s,"Jump")==1);af_action_binding b=m.bindings[4];b.positive_key=AF_KEY_F;CHECK(af_input_bind(&m,b));s.buttons[0]=false;s.keys[AF_KEY_SPACE]=true;CHECK(af_input_value(&m,&s,"Jump")==0);s.keys[AF_KEY_F]=true;CHECK(af_input_value(&m,&s,"Jump")==1);af_asset_error e;CHECK(af_input_map_save("input-test.json",&m,&e)&&af_input_map_load("input-test.json",&loaded,&e));CHECK(loaded.count==m.count&&af_input_value(&loaded,&s,"Jump")==1);b.deadzone=NAN;CHECK(!af_input_bind(&m,b));FILE *f=fopen("input-test.json","wb");CHECK(f);fputs("[{\"name\":\"bad\"}]",f);fclose(f);CHECK(!af_input_map_load("input-test.json",&loaded,&e)&&loaded.count==m.count);remove("input-test.json");puts("Input actions: keys, pad buttons/axes, deadzone, rebind, persistence and last-good rejection passed.");return 0;}

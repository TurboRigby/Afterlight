# SPDX-License-Identifier: MPL-2.0
"""Generate original integration scenes and a reusable material/physics prefab."""
import copy
import json
from pathlib import Path
import struct
root=Path(__file__).resolve().parents[1]
for source,destination in [('character','toolbox'),('character-course','toolbox-course')]:
    scene=json.loads((root/'sandbox/scenes'/(source+'.afterlight.json')).read_text())
    scene['version']=12
    for obj in scene['objects']:
        obj['material'].setdefault('alpha_mode','opaque');obj['material'].setdefault('alpha_cutoff',.5)
    (root/'sandbox/scenes'/(destination+'.afterlight.json')).write_text(json.dumps(scene,indent=2)+'\n')
scene=json.loads((root/'sandbox/scenes/toolbox.afterlight.json').read_text())
base=copy.deepcopy(next(o for o in scene['objects'] if o['gameplay']['player']['enabled']))
base.update(id='1',parent='0',name='Reusable crate',mesh='__node',position=[0,0,0],rotation=[0,0,0],scale=[1,1,1])
base['gameplay']['player']['enabled']=False;base['gameplay']['camera']['enabled']=False;base['gameplay']['camera']['primary']=False;base['collider']['enabled']=False;base['collider']['type']='none'
objects=[base]
for index,name,position,scale,mode,color in [(2,'Crate',[0,0,0],[1,1,1],'opaque',[.4,.5,.65,1]),(3,'Cutout screen',[0,1,0],[1,.5,.08],'mask',[.3,1,.6,1]),(4,'Glass panel',[0,0,.75],[1,1,.08],'blend',[.25,.7,1,.35])]:
    obj=copy.deepcopy(base);obj.update(id=str(index),parent='1',name=name,mesh='cube',position=position,scale=scale)
    obj['material'].update(alpha_mode=mode,base_color=color,texture='cutout' if mode=='mask' else '')
    obj['collider'].update(enabled=index==2,type='box' if index==2 else 'none',half_extents=[.65,.65,.65])
    objects.append(obj)
scene['objects']=objects;scene['next_id']='5';scene['assets']=[dict(name='cutout',kind='texture',path='../assets/cutout.tga')];scene['local_lights']=[]
(root/'sandbox/prefabs').mkdir(exist_ok=True)
(root/'sandbox/prefabs/crate.afterlight.prefab').write_text(json.dumps(scene,indent=2)+'\n')
header=bytearray(18);header[2]=2;struct.pack_into('<HH',header,12,64,64);header[16]=32;header[17]=0x28
pixels=bytes(c for y in range(64) for x in range(64) for c in (255,255,255,255 if ((x//8+y//8)%2==0) else 0))
(root/'sandbox/assets/cutout.tga').write_bytes(header+pixels)

# Persist the engine's named defaults without duplicating key enum ordinals.
import re
header=(root/'engine/include/afterlight/input.h').read_text().split('} af_key;')[0]
keys=list(dict.fromkeys(re.findall(r'\bAF_KEY_[A-Z_0-9]+\b',header)))
ordinals={name:i for i,name in enumerate(keys)}
def number(value):return ordinals[value] if value.startswith('AF_KEY_') else int(value)
bindings=[]
for name,arguments in re.findall(r'BIND\("([^"]+)",([^;]+)\);',(root/'engine/src/input.c').read_text()):
    neg,pos,button,axis,mouse,scale=[number(value.strip()) for value in arguments.split(',')]
    bindings.append(dict(name=name,negative_key=neg,positive_key=pos,pad_button=button,pad_axis=axis,mouse_axis=mouse,deadzone=.18,scale=scale))
(root/'sandbox/input.json').write_text(json.dumps(bindings,indent=2)+'\n')

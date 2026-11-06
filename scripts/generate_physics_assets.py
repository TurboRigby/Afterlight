# SPDX-License-Identifier: MPL-2.0
"""Generate original primitive physics rooms, dynamic prefab and input defaults."""
import copy,json,math,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def body(enabled=False,kind='static',mass=1,bounce=.1,velocity=None,spin=None,continuous=False):
    return dict(enabled=enabled,type=kind,mass=mass,friction=.6,restitution=bounce,linear_damping=.05,angular_damping=.1,gravity_scale=1,allow_sleep=True,continuous=continuous,velocity=velocity or [0,0,0],angular_velocity=spin or [0,0,0])
def create(name,hard):
    source='toolbox-course' if hard else 'toolbox'
    scene=json.loads((root/'sandbox/scenes'/(source+'.afterlight.json')).read_text());scene['version']=13
    for o in scene['objects']:
        o['body']=body()
        if o['gameplay']['role']=='platform':o['body']=body(True,'kinematic')
    template=copy.deepcopy(next(o for o in scene['objects'] if o['gameplay']['player']['enabled']))
    template['gameplay']['player']['enabled']=False;template['gameplay']['camera'].update(enabled=False,primary=False);template['gameplay']['role']='none';template['gameplay']['checkpoint_order']=0
    scene['assets'].append(dict(name='physics_sphere',kind='mesh',path='../assets/sphere.obj'))
    def shape(label,pos,half,dynamic=True,sphere=False,rotation=None,velocity=None,spin=None,continuous=False,bounce=.1):
        o=copy.deepcopy(template);o.update(id=scene['next_id'],parent='0',name=label,mesh='physics_sphere' if sphere else 'cube',position=pos,rotation=rotation or [0,0,0],scale=[half[0]/.65]*3 if sphere else [h/.65 for h in half]);scene['next_id']=str(int(scene['next_id'])+1)
        o['collider'].update(type='sphere' if sphere else 'box',enabled=True,trigger=False,center=[0,0,0],half_extents=[.65,.65,.65],radius=.65,half_height=0)
        o['material'].update(physically_based=True,roughness=.55,base_color=[.3,.65,.8,1] if sphere else [.8,.48,.2,1],alpha_mode='opaque',alpha_cutoff=.5)
        o['body']=body(dynamic,'dynamic' if dynamic else 'static',bounce=bounce,velocity=velocity,spin=spin,continuous=continuous);scene['objects'].append(o);return o
    shape('Push crate',[0,.55,3.3],[.5,.5,.5],continuous=True)
    shape('Drop crate',[-2,3,0],[.5,.5,.5])
    shape('Drop ball',[2,4,0],[.35,.35,.35],sphere=True,continuous=True,bounce=.35)
    if hard:
        for i in range(4):shape('Stack '+str(i),[-4,.5+i*1.02,3],[.5,.5,.5])
        shape('Rolling ramp',[4,.6,-5],[2,.15,2],False,rotation=[0,0,-.18])
        for i in range(3):shape('Rolling sphere '+str(i),[3+i*.5,2+i*.8,-5],[.3,.3,.3],sphere=True,bounce=.2)
        for i in range(8):shape('Domino '+str(i),[-2+i*.55,1.02,-12],[.12,1,.4],spin=[0,0,-.6] if i==0 else None)
        shape('Thin wall',[-3,1.5,-16],[.01,1.5,2],False)
        shape('Fast sphere',[-6,1,-16],[.12,.12,.12],sphere=True,velocity=[100,0,0],continuous=True,bounce=.8)
        for x in [-7.7,7.7]:shape('Boundary wall',[x,2,-8],[.1,2,15],False)
        shape('Far boundary',[0,2,-21],[7.5,2,.1],False)
        shape('Near boundary',[0,2,9.5],[7.5,2,.1],False)
    (root/'sandbox/scenes'/(name+'.afterlight.json')).write_text(json.dumps(scene,indent=2)+'\n')
    return scene
simple=create('physics-drop',False);create('physics-playground',True)
crate=copy.deepcopy(next(o for o in simple['objects'] if o['name']=='Push crate'));crate.update(id='1',parent='0',position=[0,0,0],name='Dynamic crate');prefab=copy.deepcopy(simple);prefab.update(objects=[crate],next_id='2',assets=[],local_lights=[])
(root/'sandbox/prefabs/physics-crate.afterlight.prefab').write_text(json.dumps(prefab,indent=2)+'\n')
header=(root/'engine/include/afterlight/input.h').read_text().split('} af_key;')[0];keys=list(dict.fromkeys(re.findall(r'\bAF_KEY_[A-Z_0-9]+\b',header)));ordinals={k:i for i,k in enumerate(keys)}
def number(v):return ordinals[v] if v.startswith('AF_KEY_') else int(v)
records=[]
for name,args in re.findall(r'BIND\("([^"]+)",([^;]+)\);',(root/'engine/src/input.c').read_text()):
    neg,pos,button,axis,mouse,scale=[number(v.strip()) for v in args.split(',')];records.append(dict(name=name,negative_key=neg,positive_key=pos,pad_button=button,pad_axis=axis,mouse_axis=mouse,deadzone=.18,scale=scale))
(root/'sandbox/input.json').write_text(json.dumps(records,indent=2)+'\n')

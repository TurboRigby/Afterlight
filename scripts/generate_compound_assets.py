# SPDX-License-Identifier: MPL-2.0
"""Generate original L-prop geometry, compound prop rooms and a portable prefab."""
from pathlib import Path
import copy,json
root=Path(__file__).resolve().parents[1]
shapes=[([-.65,0,0],[.18,1,.18]),([.365,-.82,0],[.835,.18,.18])]
vertices=[];faces=[]
for center,half in shapes:
    first=len(vertices)+1
    vertices.extend([[center[k]+half[k]*(1 if i&(1<<k) else -1) for k in range(3)] for i in range(8)])
    for a,b,c,d in [(0,4,6,2),(1,3,7,5),(0,1,5,4),(2,6,7,3),(0,2,3,1),(4,5,7,6)]:faces.extend([(first+a,first+b,first+c),(first+a,first+c,first+d)])
(root/'sandbox/assets/compound-l.obj').write_text('# SPDX-License-Identifier: MPL-2.0\n# Original two-cuboid L prop.\n'+''.join('v %g %g %g\n'%tuple(v) for v in vertices)+''.join('f %d %d %d\n'%f for f in faces))
def create(name,source,hard):
    s=json.loads((root/'sandbox/scenes'/(source+'.afterlight.json')).read_text());s.update(version=16,collider_parts=[],next_part='1')
    s['assets'].append(dict(name='compound_l',kind='mesh',path='../assets/compound-l.obj'))
    template=copy.deepcopy(next(o for o in s['objects'] if o['name']=='Push crate'))
    def obj(label,pos,mesh='__node',mass=5,parent='0',half=None,sphere=False):
        o=copy.deepcopy(template);o.update(id=s['next_id'],name=label,parent=parent,mesh=mesh,position=pos,rotation=[0,0,0],scale=[1,1,1]);s['next_id']=str(int(s['next_id'])+1)
        o['collider'].update(type='none',center=[0,0,0],half_extents=[.5]*3,radius=.5,half_height=.5,trigger=False)
        o['body'].update(enabled=mass>0,type='dynamic' if mass>0 else 'static',mass=max(.001,mass),gravity_scale=1,continuous=True,velocity=[0,0,0],angular_velocity=[0,0,0]);o['material']['base_color']=[.78,.5,.22,1]
        if half:o['scale']=[v/.65 for v in half];o['collider'].update(type='sphere' if sphere else 'box',radius=.65,half_extents=[.65]*3)
        s['objects'].append(o);return o
    def compound(label,pos,parts,mesh='__node',mass=5):
        owner=obj(label,pos,mesh,mass);owner['collider']['type']='compound'
        for center,half in parts:
            part=dict(id=s['next_part'],owner=owner['id'],name='Box part',position=center,rotation=[0,0,0],scale=[1,1,1],type='box',enabled=True,half_extents=half,radius=.5,density=500);s['next_part']=str(int(s['next_part'])+1);s['collider_parts'].append(part)
            if mesh=='__node':
                visual=obj(label+' visual',center,'cube',0,owner['id']);visual['scale']=[v/.65 for v in half]
        return owner
    first=compound('Imported L prop',[-2,3,1],shapes,'compound_l')
    if hard:
        for i,pos in enumerate([[-1,4,-6],[2,4,-8],[5,3,1]]):
            o=compound('L prop '+str(i),pos,shapes,'compound_l');o['rotation'][2]=.2*(i-1)
        steps=[([-1,.2,0],[.5,.2,.7]),([0,.5,0],[.5,.5,.7]),([1,.8,0],[.5,.8,.7])]
        compound('Compound steps',[3,0,-12.5],steps,mass=8)
        cup=[([0,0,0],[.75,.08,.75]),([-.72,.6,0],[.08,.6,.75]),([.72,.6,0],[.08,.6,.75]),([0,.6,-.72],[.64,.6,.08]),([0,.6,.72],[.64,.6,.08])]
        compound('Open compound cup',[-6,.08,2],cup,mass=4)
        for i in range(2):obj('Cup payload '+str(i),[-6+i*.2,2+i*.5,2],'physics_sphere',1,half=[.15]*3,sphere=True)
    (root/'sandbox/scenes'/(name+'.afterlight.json')).write_text(json.dumps(s,indent=2)+'\n');return s,first
simple,first=create('prop-room','motor-room',False)
create('prop-workshop','machinery-playground',True)
prefab=copy.deepcopy(simple);body=copy.deepcopy(first);old=body['id'];body.update(id='1',name='Dynamic L prop',position=[0,0,0]);parts=[copy.deepcopy(p) for p in simple['collider_parts'] if p['owner']==old]
for i,p in enumerate(parts):p.update(id=str(i+1),owner='1')
prefab.update(objects=[body],next_id='2',collider_parts=parts,next_part=str(len(parts)+1),joints=[],next_joint='1',local_lights=[],assets=[dict(name='compound_l',kind='mesh',path='../assets/compound-l.obj')])
(root/'sandbox/prefabs/compound-l.afterlight.prefab').write_text(json.dumps(prefab,indent=2)+'\n')

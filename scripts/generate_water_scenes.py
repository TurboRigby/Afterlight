# SPDX-License-Identifier: MPL-2.0
"""Generate the water laboratory's simple pool and larger buoyancy test lake."""
from pathlib import Path
import copy,json
root=Path(__file__).resolve().parents[1]
base=json.loads((root/'sandbox/scenes/physics-drop.afterlight.json').read_text())
template=copy.deepcopy(base['objects'][0])
def create(name,hard):
    s=copy.deepcopy(base);s.update(version=17,objects=[],assets=[],joints=[],next_joint='1',collider_parts=[],next_part='1',water=[],next_id='1')
    def obj(name,p,half=None,density=0,color=(.35,.4,.45,1),parent='0'):
        o=copy.deepcopy(template);o.update(id=s['next_id'],name=name,mesh='cube' if half else '__node',parent=parent,position=p,rotation=[0,0,0],scale=[v/.65 for v in half] if half else [1,1,1]);s['next_id']=str(int(s['next_id'])+1)
        o['collider'].update(type='box' if half else 'none',enabled=bool(half),half_extents=[.65]*3,center=[0,0,0]);o['body'].update(enabled=density>0,type='dynamic' if density else 'static',mass=max(.001,density*8*half[0]*half[1]*half[2]) if half else 1,velocity=[0,0,0],angular_velocity=[0,0,0],continuous=True,restitution=.05);o['material']['base_color']=list(color);o['gameplay']['player']['enabled']=False;o['gameplay']['camera']['enabled']=False;s['objects'].append(o);return o
    hx,hz=(7,6) if hard else (3,3)
    obj('Ground',[0,-2.5,0],[12,.15,12])
    obj('Pool floor',[0,-2.1,0],[hx,.1,hz],color=(.3,.48,.5,1))
    for x in (-hx-.15,hx+.15):obj('Pool side',[x,-1,0],[.15,1.1,hz+.3])
    for z in (-hz-.15,hz+.15):obj('Pool end',[0,-1,z],[hx,1.1,.15])
    owner=obj('Lake' if hard else 'Pool',[0,0,0]);w=dict(owner=owner['id'],enabled=True,mode='pool',quality=2 if hard else 1,half_size=[hx,hz],depth=2,density=1000,drag=1.5,wave_speed=3,damping=.8,wind=.04 if hard else .01,absorption=[.7,.12,.055],scattering=[.03,.18,.22],roughness=.16,ior=1.333);s['water'].append(w)
    count=12 if hard else 3
    for i in range(count):
        x=(i%4-1.5)*2 if hard else (i-1)*1.5;z=(i//4-1)*2 if hard else 0
        density=[500,2500,750][i%3];o=obj(('Wood' if density==500 else 'Stone' if density==2500 else 'Light crate')+str(i),[x,1.2+(i%3)*.3,z],[.3,.3,.3],density,color=(.64,.32,.12,1) if density<1000 else (.3,.32,.36,1))
        if hard:o['rotation']=[.1*i,0,.07*i]
    if hard:
        # Uneven compound raft: buoyancy uses the actual offset parts.
        owner=obj('Compound raft',[-3,1,3]);owner['collider'].update(enabled=True,type='compound');owner['body'].update(enabled=True,type='dynamic',mass=192)
        for x in (-.6,.6):
            s['collider_parts'].append(dict(id=s['next_part'],owner=owner['id'],name='Wood pontoon',position=[x,0,0],rotation=[0,0,0],scale=[1,1,1],type='box',enabled=True,half_extents=[.2,.2,.6],radius=.5,density=500));s['next_part']=str(int(s['next_part'])+1)
            visual=obj('Pontoon',[x,0,0],[.2,.2,.6],parent=owner['id'],color=(.6,.28,.1,1));visual['collider']['enabled']=False
    (root/'sandbox/scenes'/(name+'.afterlight.json')).write_text(json.dumps(s,indent=2)+'\n')
create('water-pool',False)
create('water-lake',True)

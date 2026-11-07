# SPDX-License-Identifier: MPL-2.0
"""Generate original first-person water courses, simple before combined expedition."""
from pathlib import Path
import copy,json,math
root=Path(__file__).resolve().parents[1]
base=json.loads((root/'sandbox/scenes/water-pool.afterlight.json').read_text())
template=copy.deepcopy(base['objects'][0])
player_template=copy.deepcopy(next(o for o in json.loads((root/'sandbox/scenes/physics-drop.afterlight.json').read_text())['objects'] if o['gameplay']['player']['enabled']))
def create(name,hard):
    s=copy.deepcopy(base);s.update(version=22,objects=[],water=[],assets=[],collider_parts=[],next_part='1',joints=[],next_joint='1',next_id='1')
    s['render'].update(underwater=True,refraction=True,refraction_strength=1,environment_enabled=True,environment_intensity=.5,skybox=True,skybox_intensity=.8,tone_mapping=True)
    if hard:
        s['version']=24
        s['render'].update(anti_aliasing='off',aa_sharpness=.1,taa_history_weight=.9,
                           ray_traced_shadows=False,ray_traced_water_reflections=True)
    s['wind']=dict(enabled=True,direction=[.8,0,-.6],speed=4,gust_strength=.35,gust_frequency=.25,air_density=1.225)
    def obj(label,p,half=None,color=(.3,.4,.45,1),parent='0',density=0):
        o=copy.deepcopy(template);o.update(id=s['next_id'],name=label,parent=parent,mesh='cube' if half else '__node',position=p,rotation=[0,0,0],scale=[v/.65 for v in half] if half else [1,1,1]);s['next_id']=str(int(s['next_id'])+1);o['collider'].update(type='box' if half else 'none',enabled=bool(half),trigger=False,half_extents=[.65]*3,center=[0,0,0]);o['body'].update(enabled=density>0,type='dynamic' if density else 'static',mass=density*8*half[0]*half[1]*half[2] if density else 1,velocity=[0,0,0],angular_velocity=[0,0,0],continuous=True,restitution=.02);o['material']['base_color']=list(color);s['objects'].append(o);return o
    def marker(label,p,order=0,parent='0',goal=False):
        o=obj(label,p,[.65,.85,.65],(.15,.8,.35,1) if goal else (.9,.6,.08,1),parent);o['collider']['trigger']=True;o['gameplay'].update(role='goal' if goal else 'checkpoint',checkpoint_order=order);o['material'].update(alpha_mode='blend',base_color=[.15,.8,.35,.2] if goal else [.9,.6,.08,.18],cast_shadows=False);return o
    def water(label,p,mode='pool',half=(4,5),depth=2,path=None):
        o=obj(label,p);w=copy.deepcopy(base['water'][0]);w.update(owner=o['id'],mode=mode,half_size=list(half),depth=depth,quality=1,flow_speed=1.2,path=path or [[0,0,-4],[0,0,4]],particle_spacing=.16,initial_volume=.4096,emission_rate=0,viscosity=3,emission_velocity=[0,0,0],ripple_scale=1,ripple_strength=.25);s['water'].append(w);return o,w
    def raft(label,p,order):
        o=obj(label,p,[1.25,.16,1.0],(.6,.3,.12,1),density=500);marker(label+' marker',[0,1.0,0],order,o['id']);return o
    obj('Pool bed',[0,-2.12,0],[4.3,.12,5.3],(.15,.3,.36,1))
    for x in (-4.2,4.2):obj('Stone side',[x,-.9,0],[.2,1.15,5.3])
    obj('Starting dock',[0,0,7.5],[3,.25,2.5],(.4,.45,.47,1))
    water('Course pool',[0,0,0]);raft('Practice raft',[0,.1,0],1)
    if hard:
        path=[[0,0,-5],[0,-.1,-10],[3,-.25,-15]];water('Course river',[0,0,0],'river',(1.6,6),2,path)
        for a,b in zip(path,path[1:]):
            dx,dy,dz=[b[k]-a[k] for k in range(3)];length=math.hypot(dx,dz);yaw=math.atan2(dx,dz);pitch=-math.atan2(dy,length);p=[(a[k]+b[k])/2 for k in range(3)];p[1]-=2.1
            floor=obj('River bed',p,[1.8,.1,math.hypot(length,dy)/2+.15],(.18,.3,.35,1));floor['rotation']=[pitch,yaw,0]
            for sign in (-1,1):
                bank=obj('River bank',[p[0]+sign*1.8*dz/length,p[1]+1.1,p[2]-sign*1.8*dx/length],[.2,1.1,length/2+.2]);bank['rotation']=[pitch,yaw,0]
        raft('River raft',[0,0,-9],2)
        obj('Laboratory dock',[3,.1,-17.5],[4,.35,3.5],(.4,.45,.47,1));marker('Lab checkpoint',[2.2,1.35,-15.5],3)
        # Source sits above the receiving cup; the short chute guides its spill.
        obj('Source pedestal',[3,1.35,-17],[.8,1.1,.8])
        def cup(label,p,kinematic):
            owner=obj(label,p);owner['collider'].update(enabled=True,type='compound');owner['body'].update(enabled=kinematic,type='kinematic' if kinematic else 'static',mass=30)
            for center,half in [([0,0,0],[.8,.1,.8]),([-.7,.6,0],[.1,.6,.8]),([.7,.6,0],[.1,.6,.8]),([0,.6,-.7],[.6,.6,.1]),([0,.6,.7],[.6,.6,.1])]:
                s['collider_parts'].append(dict(id=s['next_part'],owner=owner['id'],name='Cup part',position=center,rotation=[0,0,0],scale=[1,1,1],type='box',enabled=True,half_extents=half,radius=.5,density=1000));s['next_part']=str(int(s['next_part'])+1);visual=obj(label+' visual',center,half,(.45,.5,.55,1),owner['id']);visual['collider']['enabled']=False
            return owner
        cup('Source cup',[3,2.5,-17],True);cup('Receiving cup',[4.8,.55,-17],False)
        chute=obj('Pour chute',[4.1,2.0,-17],[.9,.06,.55],(.5,.55,.6,1));chute['rotation'][2]=-.25
        for z in (-17.65,-16.35):rail=obj('Chute rail',[4.1,2.15,z],[.9,.15,.08]);rail['rotation'][2]=-.25
        water('Course pour',[3,3.3,-17],'particles',(.4,.4),1.6)
        marker('Finish',[6,1.35,-19],goal=True)
        for i in range(3):obj('Floating cargo '+str(i),[-2+i*1.4,1.2,2],[.28,.28,.28],(.62,.3,.12,1),density=500)
    else:
        obj('Far dock',[0,0,-7.5],[3,.25,2.5],(.4,.45,.47,1));marker('Far bank checkpoint',[0,1.15,-6.5],2);marker('Finish',[2,1.15,-8],goal=True)
    # Submerged stepping stones give a clear landing route at either end.
    for z in (4.9,-4.9):obj('Submerged step',[0,-.65,z],[1.3,.18,.5],(.4,.45,.47,1))
    player=copy.deepcopy(player_template);player.update(id=s['next_id'],name='Swimmer',mesh='__node',parent='0',position=[0,1.16,7.5],rotation=[0,0,0],scale=[1,1,1]);s['next_id']=str(int(s['next_id'])+1);player['body']['enabled']=False;player['gameplay']['player']['spawn']='0';s['objects'].append(player)
    (root/'sandbox/scenes'/(name+'.afterlight.json')).write_text(json.dumps(s,indent=2)+'\n')
create('water-course',False)
create('water-expedition',True)

# SPDX-License-Identifier: MPL-2.0
"""Build the motor tutorial and harder powered machinery scene from 0.14 fixtures."""
from pathlib import Path
import copy,json,re
root=Path(__file__).resolve().parents[1]
complex_scene=json.loads((root/'sandbox/scenes/mechanical-playground.afterlight.json').read_text())
for name,source in [('motor-room','mesh-room'),('machinery-playground','mechanical-playground')]:
    s=json.loads((root/'sandbox/scenes'/(source+'.afterlight.json')).read_text());s['version']=15
    if name=='motor-room':
        door=copy.deepcopy(next(o for o in complex_scene['objects'] if o['name']=='Hinged door'));old_id=door['id'];door['id']=s['next_id'];s['next_id']=str(int(s['next_id'])+1);s['objects'].append(door)
        joint=copy.deepcopy(next(j for j in complex_scene['joints'] if j['a']==old_id));joint['id']=s['next_joint'];s['next_joint']=str(int(s['next_joint'])+1);joint['a']=door['id'];s['joints'].append(joint)
    else:
        template=copy.deepcopy(next(o for o in s['objects'] if o['name']=='Hinged door'));template.update(id=s['next_id'],name='Powered rotor',position=[-.5,1.2,-2],scale=[1/.65,.12/.65,.3/.65]);s['next_id']=str(int(s['next_id'])+1);template['body'].update(velocity=[0,0,0],angular_velocity=[0,0,0]);s['objects'].append(template)
        j=copy.deepcopy(s['joints'][-1]);j.update(id=s['next_joint'],name='Powered rotor',a=template['id'],b='0',anchor_a=[0,0,0],anchor_b=[-.5,1.2,-2],type='hinge',axis_a=[0,1,0],axis_b=[0,1,0],reference_a=[1,0,0],reference_b=[1,0,0],limits_enabled=False,lower_limit=-3.141592653589793,upper_limit=3.141592653589793);s['next_joint']=str(int(s['next_joint'])+1);s['joints'].append(j)
    names={o['id']:o['name'] for o in s['objects']}
    for j in s['joints']:
        j.update(motor_enabled=False,motor_speed=0,max_motor_torque=10)
        body=names[j['a']]
        if body=='Hinged door':j.update(name='Powered door',motor_enabled=True,motor_speed=.65,max_motor_torque=6)
        elif body=='Swinging beam':j.update(name='Powered swing',motor_enabled=True,motor_speed=.6,max_motor_torque=10)
        elif body=='Seesaw':j.update(name='Powered seesaw',motor_enabled=True,motor_speed=.5,max_motor_torque=8)
        elif body=='Powered rotor':j.update(motor_enabled=True,motor_speed=1,max_motor_torque=5)
    (root/'sandbox/scenes'/(name+'.afterlight.json')).write_text(json.dumps(s,indent=2)+'\n')
# Keep named input defaults reproducible, including Z brake/resume and X reverse.
header=(root/'engine/include/afterlight/input.h').read_text().split('} af_key;')[0]
keys=list(dict.fromkeys(re.findall(r'\bAF_KEY_[A-Z_0-9]+\b',header)));ordinals={k:i for i,k in enumerate(keys)}
def number(v):return ordinals[v] if v.startswith('AF_KEY_') else int(v)
records=[]
for name,args in re.findall(r'BIND\("([^"]+)",([^;]+)\);',(root/'engine/src/input.c').read_text()):
    neg,pos,button,axis,mouse,scale=[number(v.strip()) for v in args.split(',')];records.append(dict(name=name,negative_key=neg,positive_key=pos,pad_button=button,pad_axis=axis,mouse_axis=mouse,deadzone=.18,scale=scale))
(root/'sandbox/input.json').write_text(json.dumps(records,indent=2)+'\n')

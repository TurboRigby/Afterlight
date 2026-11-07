# SPDX-License-Identifier: MPL-2.0
"""Generate the isolated rigid-geometry sun-shadow comparison scene."""
import copy,json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
scene=json.loads((root/'sandbox/scenes/water-expedition.afterlight.json').read_text())
template=copy.deepcopy(scene['objects'][0]);scene.update(version=24,objects=[],water=[],joints=[],next_joint='1',collider_parts=[],next_part='1',local_lights=[],assets=[dict(name='ray_block',kind='mesh',path='../assets/ray-block.obj')]);scene['wind']['enabled']=False
scene['render'].update(ray_traced_shadows=True,anti_aliasing='taa',aa_sharpness=.1,taa_history_weight=.9,environment_enabled=True,environment_intensity=.18,skybox=True,skybox_intensity=.6,exposure=.4,bloom=False)
scene['light'].update(direction=[.55,-.8,.35],color=[1,.94,.84],intensity=2.4,ambient=.035)
scene['shadows'].update(resolution=512,filter_radius=1,distance=45)
def box(name,position,scale,color,metal=0):
    o=copy.deepcopy(template);o.update(id=str(len(scene['objects'])+1),name=name,mesh='ray_block',parent='0',position=position,rotation=[0,0,0],scale=scale,visible=True)
    o['body']['enabled']=False;o['gameplay']['player']['enabled']=False;o['gameplay']['camera']['enabled']=False;o['gameplay']['role']='none';o['animation']['asset']='';o['material'].update(base_color=[*color,1],texture='',normal_map='',metallic_roughness_map='',physically_based=True,roughness=.55,metallic=metal,cast_shadows=True,receive_shadows=True,alpha_mode='opaque',unlit=False);scene['objects'].append(o);return o
box('Ground',[0,-.15,0],[14,.3,12],[.55,.59,.64])
box('Raised platform',[-3,.18,-1],[3,.35,3],[.32,.38,.44])
box('Tall pillar',[-3,1.7,-1],[.8,3,.8],[.9,.52,.18],.4)
box('Moving shadow arm',[.3,2.2,0],[4,.12,.2],[.9,.73,.25],.6)
for i in range(9):box('Thin caster %02d'%i,[1+i*.33,1.65,-2],[.055,3.3,.08],[.16,.24,.31],.65)
for i in range(4):box('Contact step %02d'%i,[-1.8+i*.65,.15+i*.13,2.3],[.58,.3+i*.26,1.2],[.16,.56,.65])
box('Bridge support',[-2,1.1,-3.7],[.25,2.2,.3],[.8,.27,.2])
box('Bridge support',[2,1.1,-3.7],[.25,2.2,.3],[.8,.27,.2])
box('Bridge lintel',[0,2.3,-3.7],[4.5,.25,.4],[.8,.27,.2])
camera=box('Lab camera',[9,7,10],[1,1,1],[1,1,1]);camera.update(mesh='__node',rotation=[-.48,.733,0]);camera['gameplay']['camera'].update(enabled=True,primary=True,eye_height=0,far_clip=160)
scene['next_id']=str(len(scene['objects'])+1)
(root/'sandbox/scenes/ray-shadow-lab.afterlight.json').write_text(json.dumps(scene,indent=2)+'\n')

# SPDX-License-Identifier: MPL-2.0
"""Add the authored guide rig and audio manifest to the expedition only."""
import copy
import json
from pathlib import Path

def polish(root: Path):
    path=root/'sandbox/scenes/water-expedition.afterlight.json'
    data=json.loads(path.read_text())
    if any(o['name']=='Course guide' for o in data['objects']):
        return
    character=json.loads((root/'sandbox/scenes/character.afterlight.json').read_text())
    nodes=character['objects'][4:]
    start=int(data['next_id'])
    ids={o['id']:str(start+i) for i,o in enumerate(nodes)}
    names={'Character_animation':'Guide_animation','Character_m0_p0':'Guide_mesh'}
    for asset in character['assets']:
        asset=copy.deepcopy(asset);asset['name']=names.get(asset['name'],asset['name']);data['assets'].append(asset)
    for i,original in enumerate(nodes):
        o=copy.deepcopy(original);o['body']=copy.deepcopy(data['objects'][0]['body']);o['material'].setdefault('alpha_mode','opaque');o['material'].setdefault('alpha_cutoff',.5);o['id']=ids[o['id']];o['parent']=ids.get(o['parent'],'0');o['model']['root']=ids.get(o['model']['root'],'0')
        o['mesh']=names.get(o['mesh'],o['mesh']);o['animation']['asset']=names.get(o['animation']['asset'],o['animation']['asset']);o['rig']['enabled']=False
        if i==0:
            o['name']='Course guide';o['position']=[-2.3,.25,5.7];o['rotation']=[0,0,0];o['animation']['autoplay']=True;o['animation']['speed']=.65
        data['objects'].append(o)
    data['next_id']=str(start+len(nodes))
    path.write_text(json.dumps(data,indent=2)+'\n')

if __name__=='__main__':
    polish(Path(__file__).resolve().parents[1])

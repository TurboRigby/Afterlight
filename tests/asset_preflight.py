# SPDX-License-Identifier: MPL-2.0
"""Validate reference kinds before opening Vulkan or creating an export."""
import argparse,json,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--player',required=True);p.add_argument('--root',type=Path,required=True);p.add_argument('--module',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='afterlight-preflight-') as folder:
    folder=Path(folder);scene=folder/'demo.afterlight.json';project=folder/'game.afterlight.project'
    d=json.loads((a.root/'sandbox/scenes/demo.afterlight.json').read_text());d['assets']=[]
    for obj in d['objects']:
        obj['mesh']='cube'
        for field in ('texture','normal_map','metallic_roughness_map'):obj['material'][field]=''
        if 'animation' in obj:obj['animation']['asset']=''
    scene.write_text(json.dumps(d));project.write_text(json.dumps(dict(format='afterlight.project',version=1,name='Preflight',startup_scene=scene.name,game_module=a.module)))
    def run():return subprocess.run([a.player,'--project',str(project),'--check'],capture_output=True,text=True,timeout=20)
    r=run();assert r.returncode==0,r.stderr
    d['objects'][0]['mesh']='missing_mesh';d['objects'][1]['material']['texture']='missing_color';scene.write_text(json.dumps(d));r=run()
    assert r.returncode!=0 and 'missing_mesh' in r.stderr and 'missing_color' in r.stderr and "Object '" in r.stderr,r
    d['objects'][0]['mesh']='cube';d['objects'][1]['material']['texture']='wrong_map';d['assets']=[dict(name='wrong_map',kind='mesh',path=str(a.root/'tests/assets/quad.obj'))];scene.write_text(json.dumps(d));r=run()
    assert r.returncode!=0 and "wrong-kind asset 'wrong_map'" in r.stderr,r
print('Asset preflight: object names, missing references and semantic asset kinds diagnosed without GPU initialization.')

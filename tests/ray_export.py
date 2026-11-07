# SPDX-License-Identifier: MPL-2.0
"""Relocate a ray-shadow lab export, then verify its optional hardware path."""
import argparse,json,os,shutil,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--player',type=Path,required=True);p.add_argument('--module',type=Path,required=True);p.add_argument('--sanitized',action='store_true');a=p.parse_args()
def run(args,success=True):
    env=os.environ.copy();env.update(SDL_AUDIO_DRIVER='dummy',VK_LAYER_VALIDATE_SYNC='1')
    r=subprocess.run([str(x) for x in args],capture_output=True,text=True,env=env,timeout=120)
    assert (r.returncode==0)==success,(args,r.returncode,r.stdout,r.stderr)
    return r.stdout+r.stderr
with tempfile.TemporaryDirectory(prefix='afterlight-ray-export-') as tmp:
    root=Path(tmp);source=root/'source project'
    for folder in ('scenes','assets','game'):(source/folder).mkdir(parents=True)
    shutil.copy2(a.root/'sandbox/scenes/ray-shadow-lab.afterlight.json',source/'scenes')
    shutil.copy2(a.root/'sandbox/assets/ray-block.obj',source/'assets')
    shutil.copy2(a.module,source/'game'/a.module.name)
    project=source/'lab.afterlight.project';project.write_text(json.dumps(dict(format='afterlight.project',version=1,name='Ray lab',startup_scene='scenes/ray-shadow-lab.afterlight.json',game_module='game/'+a.module.name)))
    output=root/'export';args=[os.sys.executable,a.root/'scripts/afterlight.py','export',project,'--output',output,'--player',a.player]
    if a.sanitized:assert 'AddressSanitizer' in run(args,False) and not output.exists()
    else:
        run(args);shutil.rmtree(source);moved=root/'relocated ray lab';output.rename(moved)
        resources=moved/'Game.app/Contents/Resources' if os.sys.platform=='darwin' else moved
        scene=json.loads((resources/'scenes/ray-shadow-lab.afterlight.json').read_text());assert scene['version']==24 and scene['render']['ray_traced_shadows']
        exe=moved/'Game.app/Contents/MacOS/afterlight_player' if os.sys.platform=='darwin' else moved/('afterlight_player.exe' if os.sys.platform=='win32' else 'run.sh')
        run([exe,'--check']);log=run([exe,'--frames','90','--validation']);assert '0 validation errors' in log and 'Ray shadows:' in log
        if os.environ.get('AFTERLIGHT_REQUIRE_RAY_QUERY')=='1':assert 'Ray shadows: hardware ray queries;' in log,log
print('Ray export: scene preference, relocated assets/module, preflight and native player passed.')

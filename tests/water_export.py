# SPDX-License-Identifier: MPL-2.0
"""Export all water labs, remove their source, relocate, then exercise C Play."""
import argparse,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True);p.add_argument('--player',type=Path,required=True);p.add_argument('--module',type=Path,required=True);p.add_argument('--play-test',type=Path,required=True);p.add_argument('--sanitized',action='store_true');p.add_argument('--courses',action='store_true');a=p.parse_args()
names=('water-course','water-expedition') if a.courses else ('water-pool','water-lake','water-stream','water-river-bend','water-pour','water-pour-lab')
def run(args,success=True):
    env=os.environ.copy();env.update(SDL_AUDIO_DRIVER='dummy',VK_LAYER_VALIDATE_SYNC='1')
    result=subprocess.run([str(x) for x in args],capture_output=True,text=True,env=env,timeout=120)
    assert (result.returncode==0)==success,(args,result.returncode,result.stdout,result.stderr)
    return result.stdout+result.stderr
with tempfile.TemporaryDirectory(prefix='afterlight-water-export-') as temp:
    folder=Path(temp);source=folder/'water project';(source/'scenes').mkdir(parents=True);(source/'game').mkdir();shutil.copytree(a.root/'sandbox/assets',source/'assets')
    for name in names:shutil.copy2(a.root/'sandbox/scenes'/(name+'.afterlight.json'),source/'scenes')
    shutil.copy2(a.module,source/'game'/a.module.name)
    project=source/'water.afterlight.project';project.write_text(json.dumps(dict(format='afterlight.project',version=1,name='Water labs',startup_scene='scenes/'+names[0]+'.afterlight.json',game_module='game/'+a.module.name)))
    output=folder/'water export';command=[sys.executable,a.root/'scripts/afterlight.py','export',project,'--output',output,'--player',a.player]
    if a.sanitized:
        assert 'AddressSanitizer' in run(command,False) and not output.exists()
    else:
        run(command);shutil.rmtree(source);moved=folder/'relocated water';output.rename(moved)
        resources=moved/'Game.app/Contents/Resources' if sys.platform=='darwin' else moved
        for name in names:
            scene=json.loads((resources/'scenes'/(name+'.afterlight.json')).read_text());assert scene['water'] and scene['version'] in (17,18,19,20,21,22,23)
        packed=json.loads((resources/'game.afterlight.project').read_text())
        run([a.play_test,resources/packed['startup_scene'] if a.courses else resources/'scenes',resources/packed['game_module'],resources/'game.afterlight.project'])
        exe=moved/'Game.app/Contents/MacOS/afterlight_player' if sys.platform=='darwin' else moved/('afterlight_player.exe' if sys.platform=='win32' else 'run.sh')
        for name in names:
            packed['startup_scene']='scenes/'+name+'.afterlight.json';(resources/'game.afterlight.project').write_text(json.dumps(packed))
            log=run([exe,'--frames','15','--validation']);assert '0 validation errors' in log
print('Water export: sanitizer runtime rejected.' if a.sanitized else 'Water export: courses/labs retain water definitions, rebuild transient surfaces and run C Play after source removal and relocation.')

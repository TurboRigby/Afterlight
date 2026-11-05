# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""End-to-end starter creation, compilation, model packaging and relocated play."""
import argparse,json,os,pathlib,shutil,subprocess,sys,tempfile,zipfile
p=argparse.ArgumentParser();p.add_argument('--sanitized',action='store_true');p.add_argument('--root',type=pathlib.Path,required=True);p.add_argument('--player',type=pathlib.Path,required=True);p.add_argument('--model-test',type=pathlib.Path,required=True);p.add_argument('--fixtures',type=pathlib.Path,required=True);a=p.parse_args()
def run(args,cwd,success=True,environment=None):
    r=subprocess.run([str(x) for x in args],cwd=cwd,env=environment or os.environ.copy(),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=90)
    if (r.returncode==0)!=success:raise AssertionError(f'Unexpected exit {r.returncode}: {args}\n{r.stdout[-10000:]}')
    return r.stdout
with tempfile.TemporaryDirectory(prefix='afterlight-project-tools-') as temporary:
    root=pathlib.Path(temporary);tool=a.root/'scripts/afterlight.py';project=root/'Starter with spaces'
    run([sys.executable,tool,'new',project,'--name','Starter "quoted" project'],root)
    assert (project/'game/game.c').is_file() and (project/'include/afterlight/game_module.h').is_file()
    assert (project/'LICENSE').read_bytes()==(a.root/'LICENSE').read_bytes()
    # Covered template edits must accompany the binary; independent files keep
    # their own terms and are not included by the module's source packager.
    with (project/'game/game.c').open('a') as changed:changed.write('\n/* Modified starter for source availability regression. */\n')
    (project/'game/independent.c').write_text('/* SPDX-License-Identifier: MIT */\nint independent(void) { return 7; }\n')
    run([sys.executable,tool,'new',project,'--name','Refuse overwrite'],root,False)
    run(['cmake','-S',project,'-B',project/'build','-G','Ninja'],root);run(['cmake','--build',project/'build'],root)
    module_archive=next((project/'build').glob('game*.source.zip'))
    with zipfile.ZipFile(module_archive) as source:
        assert source.read('game/game.c')==(project/'game/game.c').read_bytes()
        assert 'include/afterlight/game_module.h' in source.namelist()
        assert 'game/independent.c' not in source.namelist()
    env=os.environ.copy();env['SDL_AUDIO_DRIVER']='dummy';env['VK_LAYER_VALIDATE_SYNC']='1'
    player=a.player/'Contents/MacOS/afterlight_player' if a.player.is_dir() else a.player
    run([player,'--project',project/'game.afterlight.project','--frames','30','--validation'],root,environment=env)
    # Generate a real CPU rich-model import, then merge it into the starter scene.
    run([a.model_test,a.fixtures],root)
    model=json.loads((root/'model.afterlight.json').read_text());scene_path=project/'scenes/start.afterlight.json';scene=json.loads(scene_path.read_text());offset=len(scene['objects'])
    for obj in model['objects']:
        obj['id']=str(int(obj['id'])+offset)
        if obj['parent']!='0':obj['parent']=str(int(obj['parent'])+offset)
    scene['objects']+=model['objects'];scene['assets']=model['assets'];scene['next_id']=str(len(scene['objects'])+1);scene_path.write_text(json.dumps(scene))
    output=root/'export'
    if a.sanitized:
        log=run([sys.executable,tool,'export',project/'game.afterlight.project','--output',output,'--player',a.player],root,False)
        assert 'AddressSanitizer' in log and not output.exists()
        print('Sanitized project tools: starter build/run and model import passed; distribution of a sanitizer runtime was correctly rejected.')
        sys.exit(0)
    run([sys.executable,tool,'export',project/'game.afterlight.project','--output',output,'--player',a.player],root)
    manifest=json.loads((output/'export-manifest.json').read_text());assert manifest['scenes']==1 and manifest['assets']>=5
    packed_resources=output/'Game.app/Contents/Resources' if sys.platform=='darwin' else output
    original_resources=a.player/'Contents/Resources' if a.player.is_dir() else a.player.parent
    assert (packed_resources/'source/afterlight-source.zip').read_bytes()==(original_resources/'source/afterlight-source.zip').read_bytes()
    assert (packed_resources/'source/game-source.zip').read_bytes()==module_archive.read_bytes()
    assert (packed_resources/'LICENSE').read_bytes()==(a.root/'LICENSE').read_bytes()
    assert 'MPL-2.0' in (output/'README.txt').read_text()
    if sys.platform=='darwin':
        # An old/stripped player must not silently produce a source-less export.
        stripped=root/'Stripped player.app';shutil.copytree(a.player,stripped)
        (stripped/'Contents/Resources/source/afterlight-source.zip').unlink()
        rejected=root/'missing-source-export'
        log=run([sys.executable,tool,'export',project/'game.afterlight.project','--output',rejected,'--player',stripped],root,False)
        assert 'missing its build-time MPL source archive' in log and not rejected.exists()
    # Both overwrite and failed-source cases preserve existing output.
    (output/'sentinel').write_text('keep');run([sys.executable,tool,'export',project/'game.afterlight.project','--output',output,'--player',a.player],root,False);assert (output/'sentinel').read_text()=='keep'
    missing_scene=json.loads(scene_path.read_text());missing_scene['assets'][0]['path']='missing.gltf#mesh=0,primitive=0';scene_path.write_text(json.dumps(missing_scene))
    failed=root/'failed-export';run([sys.executable,tool,'export',project/'game.afterlight.project','--output',failed,'--player',a.player],root,False);assert not failed.exists()
    assert not list(root.glob('.afterlight-export-*'))
    moved=root/'Relocated output';output.rename(moved);shutil.rmtree(project)
    if sys.platform=='darwin':
        exe=moved/'Game.app/Contents/MacOS/afterlight_player';resources=moved/'Game.app/Contents/Resources'
        assert not any('/opt/homebrew/' in line for line in run(['otool','-L',exe],root).splitlines())
        packed=json.loads((resources/'game.afterlight.project').read_text());assert packed['name']=='Starter "quoted" project'
        scene=json.loads((resources/'scenes/start.afterlight.json').read_text());assert all(not pathlib.Path(x['path'].partition('#')[0]).is_absolute() for x in scene['assets'])
        env['DYLD_PRINT_LIBRARIES']='1';env.pop('VK_DRIVER_FILES',None);env.pop('VK_ICD_FILENAMES',None)
        log=run([exe,'--frames','45','--validation'],root,environment=env)
        assert '0 validation errors' in log and str(moved) in log and 'libMoltenVK' in log
    elif sys.platform=='win32':run([moved/'afterlight_player.exe','--frames','30'],root,environment=env)
    else:run([moved/'run.sh','--frames','30','--validation'],root,environment=env)
print('Project tools: starter C build/run, rich model dependency rewrite, export/overwrite/failure isolation, source removal, relocated generic player and bundled runtime passed.')

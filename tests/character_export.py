# SPDX-License-Identifier: MPL-2.0
"""Export both animation demos, remove sources, relocate and run their C game."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

p = argparse.ArgumentParser()
p.add_argument('--root', type=Path, required=True)
p.add_argument('--player', type=Path, required=True)
p.add_argument('--module', type=Path, required=True)
p.add_argument('--sanitized', action='store_true')
a = p.parse_args()
def run(args, cwd, success=True):
    env = os.environ.copy();env['SDL_AUDIO_DRIVER'] = 'dummy';env['VK_LAYER_VALIDATE_SYNC'] = '1'
    result = subprocess.run([str(x) for x in args], cwd=cwd, env=env, capture_output=True, text=True, timeout=90)
    assert (result.returncode == 0) == success, f'{args}: {result.stdout}\n{result.stderr}'
    return result.stdout + result.stderr

with tempfile.TemporaryDirectory(prefix='afterlight-animation-export-') as temporary:
    root = Path(temporary);source = root / 'source project'
    for folder in ('assets', 'scenes', 'game'): (source / folder).mkdir(parents=True, exist_ok=True)
    for name in ('locomotion.gltf','jump.wav','land.wav','checkpoint.wav','finish.wav','beacon.glb'): shutil.copy2(a.root / 'sandbox/assets' / name, source / 'assets' / name)
    shutil.copy2(a.module, source / 'game' / a.module.name)
    outputs = []
    for name in ('character', 'character-course'):
        shutil.copy2(a.root / 'sandbox/scenes' / (name + '.afterlight.json'), source / 'scenes' / (name + '.afterlight.json'))
        scene = json.loads((source / 'scenes' / (name + '.afterlight.json')).read_text())
        assert all(not Path(asset['path'].partition('#')[0]).is_absolute() for asset in scene['assets'])
        assert all((source / 'scenes' / asset['path'].partition('#')[0]).resolve().is_relative_to(source.resolve()) for asset in scene['assets'])
        project = {'format': 'afterlight.project', 'version': 1, 'name': name, 'startup_scene': 'scenes/' + name + '.afterlight.json', 'game_module': 'game/' + a.module.name}
        project_path = source / (name + '.afterlight.project');project_path.write_text(json.dumps(project))
        output = root / (name + ' export')
        command = [sys.executable, a.root / 'scripts/afterlight.py', 'export', project_path, '--output', output, '--player', a.player]
        if a.sanitized:
            assert 'AddressSanitizer' in run(command, root, False) and not output.exists()
            continue
        run(command, root);outputs.append(output)
        resources = output / 'Game.app/Contents/Resources' if sys.platform == 'darwin' else output
        packed_project = json.loads((resources / 'game.afterlight.project').read_text())
        packed_scene = json.loads((resources / packed_project['startup_scene']).read_text())
        assert packed_scene['version'] in (10,11,12)
        assert any(asset['kind'] == 'animation' for asset in packed_scene['assets'])
        assert sum(bool(obj['animation']['asset']) for obj in packed_scene['objects']) == 1
        assert all(not Path(asset['path'].split('#')[0]).is_absolute() for asset in packed_scene['assets'])
        with zipfile.ZipFile(resources / 'source/afterlight-source.zip') as archive:
            assert archive.read('game/character_game.c') == (a.root / 'game/character_game.c').read_bytes()
    shutil.rmtree(source)
    for output in outputs:
        moved = root / (output.name + ' relocated');output.rename(moved)
        if sys.platform == 'darwin': exe = moved / 'Game.app/Contents/MacOS/afterlight_player'
        elif sys.platform == 'win32': exe = moved / 'afterlight_player.exe'
        else: exe = moved / 'run.sh'
        log = run([exe, '--frames', '50', '--validation'], root)
        assert '0 validation errors' in log and 'Character demo:' in log
print('Character export: both demos preserve clip/binding assets and covered C source; relocated play after source removal passed.' if not a.sanitized else 'Character export: sanitizer runtimes correctly rejected for both demos.')

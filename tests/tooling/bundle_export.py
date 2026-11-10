# SPDX-License-Identifier: MPL-2.0
"""Verify single-binary bundle compilation and standalone execution."""
import argparse, json, os, shutil, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--root', type=Path, required=True)
p.add_argument('--player', type=Path, required=True)
p.add_argument('--module', type=Path, required=True)
p.add_argument('--extension', type=Path)
p.add_argument('--sanitized', action='store_true')
a = p.parse_args()

def run(args, success=True):
    env = os.environ.copy()
    env.update(SDL_AUDIO_DRIVER='dummy', VK_LAYER_VALIDATE_SYNC='1')
    r = subprocess.run([str(x) for x in args], capture_output=True, text=True, env=env, timeout=120)
    assert (r.returncode == 0) == success, (args, r.returncode, r.stdout, r.stderr)
    return r.stdout + r.stderr

with tempfile.TemporaryDirectory(prefix='afterlight-bundle-export-') as tmp:
    root = Path(tmp)
    source = root / 'source_project'
    for folder in ('scenes', 'assets', 'game'):
        (source / folder).mkdir(parents=True)
    shutil.copy2(a.root / 'sandbox/scenes/obstacle.afterlight.json', source / 'scenes')
    for asset in ('jump.wav', 'land.wav', 'checkpoint.wav', 'finish.wav', 'beacon.glb'):
        src_asset = a.root / 'sandbox/assets' / asset
        if src_asset.is_file():
            shutil.copy2(src_asset, source / 'assets')
    shutil.copy2(a.module, source / 'game' / a.module.name)
    project = source / 'game.afterlight.project'
    project.write_text(json.dumps(dict(
        format='afterlight.project',
        version=1,
        name='Standalone Test',
        startup_scene='scenes/obstacle.afterlight.json',
        game_module='game/' + a.module.name
    )))

    output = root / 'standalone_game'
    bundle_cmd = [
        os.sys.executable,
        a.root / 'scripts/afterlight.py',
        'bundle',
        project,
        '--output', output,
        '--player', a.player
    ]
    if a.extension and a.extension.is_file():
        bundle_cmd.extend(['--extensions', str(a.extension)])

    if a.sanitized:
        assert 'AddressSanitizer' in run(bundle_cmd, False) and not output.exists()
    else:
        run(bundle_cmd)
        assert output.is_file(), f'Bundle output {output} must be a single executable file'
        # Verify output is a regular file and executable
        assert os.access(output, os.X_OK), 'Bundle output must be executable'

        # Move standalone binary to an isolated directory with no source/project files
        isolated = root / 'isolated'
        isolated.mkdir()
        relocated = isolated / 'standalone_game'
        output.rename(relocated)
        shutil.rmtree(source)

        # Execute check on relocated standalone binary
        run([relocated, '--check'])

        # Execute frames on relocated standalone binary
        log = run([relocated, '--frames', '15', '--validation'])
        assert '0 validation errors' in log, f'Validation errors found in log: {log}'

print('Bundle export: standalone single binary created, relocated, preflight and runtime passed.')

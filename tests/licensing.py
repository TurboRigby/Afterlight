# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Check official license bytes, source coverage and distributed notices."""
import argparse
import hashlib
import pathlib
import zipfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--root', type=pathlib.Path, required=True)
parser.add_argument('--archive', type=pathlib.Path, required=True)
parser.add_argument('--player', type=pathlib.Path, required=True)
args = parser.parse_args()
root = args.root
assert hashlib.sha256((root / 'LICENSE').read_bytes()).hexdigest() == '3f3d9e0024b1921b067d6f7f88deb4a60cbe7a78e76c64e3f1d7fc3b779b9d04'
resources = args.player / 'Contents/Resources' if args.player.is_dir() else args.player.parent
for name in ('LICENSE', 'NOTICE', 'THIRD_PARTY_NOTICES.md'):
    assert (resources / name).read_bytes() == (root / name).read_bytes(), name
assert (resources / 'source/afterlight-source.zip').read_bytes() == args.archive.read_bytes()
with zipfile.ZipFile(args.archive) as archive:
    names = set(archive.namelist())
    assert {'LICENSE', 'NOTICE', 'CMakeLists.txt', 'scripts/package_source.py',
            'game/playground_game.c', 'editor/assets/OFL.txt', 'third_party/cgltf/cgltf.h'} <= names
    for name in names:
        assert not any(part in {'.git', 'build', '__pycache__', '.cache'} for part in pathlib.PurePosixPath(name).parts), name
        assert 'CMakeUserPresets' not in name and not name.endswith(('.source.zip', '.pyc')), name
        assert archive.read(name) == (root / name).read_bytes(), f'Stale source: {name}'
    for folder in ('engine', 'editor', 'game', 'player', 'playground', 'sandbox', 'tests', 'scripts', 'cmake'):
        for path in (root / folder).rglob('*'):
            if path.suffix in {'.c', '.h', '.vert', '.frag', '.py', '.cmake'}:
                assert b'SPDX-License-Identifier: MPL-2.0' in path.read_bytes()[:2048], path
                assert path.relative_to(root).as_posix() in names, path
for folder, upstream in (('cgltf', 'cgltf'), ('cjson', 'cjson'), ('nuklear', 'nuklear'), ('stb', 'stb')):
    assert (resources / 'licenses' / folder / 'LICENSE').read_bytes() == (root / 'third_party' / upstream / 'LICENSE').read_bytes()
assert (resources / 'licenses/firasans/OFL.txt').read_bytes() == (root / 'editor/assets/OFL.txt').read_bytes()
print('Licensing: official MPL, original file headers, corresponding source and retained third-party notices passed.')

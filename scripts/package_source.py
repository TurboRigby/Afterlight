# SPDX-License-Identifier: MPL-2.0
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Build a deterministic source archive using only Python's standard library."""
from __future__ import annotations

import argparse
import io
from pathlib import Path
import zipfile

SOURCE_DIRS = ('engine', 'editor', 'game', 'player', 'playground', 'sandbox',
               'scripts', 'tests', 'docs', 'cmake', 'third_party', '.github')
ROOT_FILES = ('CMakeLists.txt', 'CMakePresets.json', 'README.md', 'LICENSE',
              'NOTICE', 'THIRD_PARTY_NOTICES.md', 'CONTRIBUTING.md', 'DCO', '.gitignore', '.gitattributes')
SUFFIXES = {'.c', '.h', '.vert', '.frag', '.glsl', '.hlsl', '.py', '.cmake', '.md', '.json',
            '.sh', '.csv', '.hdr', '.yml', '.yaml', '.obj', '.tga', '.glb', '.gltf', '.png', '.bin', '.wav', '.ttf', '.txt', '.prefab'}


def source_files(root: Path, project: bool = False):
    """Select source inputs explicitly; never archive a checkout's build/history."""
    names = ('CMakeLists.txt', 'LICENSE', 'NOTICE', 'README.md') if project else ROOT_FILES
    paths = {root / name for name in names if (root / name).is_file()}
    folders = ('game', 'include', 'tools') if project else SOURCE_DIRS
    for folder in folders:
        for path in (root / folder).rglob('*'):
            relative = path.relative_to(root)
            parts=relative.parts[1:] if folder=='.github' else relative.parts
            if any(part in {'__pycache__', '.git', 'build', '.cache'} or part.startswith('.') for part in parts):
                continue
            if path.is_symlink():
                raise ValueError(f'Source archive does not follow symlinks: {relative}')
            if not path.is_file():
                continue
            if project:
                # Independent, unmarked game files retain their own licensing.
                if b'SPDX-License-Identifier: MPL-2.0' not in path.read_bytes()[:2048]:
                    continue
            elif path.suffix not in SUFFIXES and path.name not in {'LICENSE', 'NOTICE', 'COPYING'}:
                continue
            paths.add(path)
    return sorted(paths, key=lambda path: path.relative_to(root).as_posix())


def create_archive(root: Path, output: Path, project: bool = False):
    root = root.resolve()
    if not (root / 'LICENSE').is_file():
        raise ValueError('A source archive must include LICENSE.')
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in source_files(root, project):
            entry = zipfile.ZipInfo(path.relative_to(root).as_posix(), (1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, path.read_bytes())
    data = buffer.getvalue()
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_bytes() != data:
        output.write_bytes(data)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--project', action='store_true')
    args = parser.parse_args()
    create_archive(args.root, args.output, args.project)

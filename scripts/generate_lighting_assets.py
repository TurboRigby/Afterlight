# SPDX-License-Identifier: MPL-2.0
"""Regenerate the original smooth sphere used by Afterlight's Lighting Lab."""
from pathlib import Path
import math

rows, columns = 32, 64
lines = ['# Afterlight original procedural sphere; MPL-2.0; see NOTICE and LICENSE.']
for y in range(rows + 1):
    theta = y * math.pi / rows
    for x in range(columns + 1):
        phi = x * 2 * math.pi / columns
        nx, ny, nz = math.sin(theta) * math.cos(phi), math.cos(theta), math.sin(theta) * math.sin(phi)
        lines.extend([f'v {nx * .8:.9f} {ny * .8:.9f} {nz * .8:.9f}',
                      f'vt {x / columns:.9f} {1 - y / rows:.9f}',
                      f'vn {nx:.9f} {ny:.9f} {nz:.9f}'])
for y in range(rows):
    for x in range(columns):
        a = y * (columns + 1) + x + 1
        b, c, d = a + columns + 1, a + columns + 2, a + 1
        triangles = ([] if y == rows - 1 else [(a, b, c)]) + ([] if y == 0 else [(a, c, d)])
        for triangle in triangles:
            lines.append('f ' + ' '.join(f'{i}/{i}/{i}' for i in triangle))
Path(__file__).resolve().parents[1].joinpath('sandbox/assets/sphere.obj').write_text('\n'.join(lines) + '\n')

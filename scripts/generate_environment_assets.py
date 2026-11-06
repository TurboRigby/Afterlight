# SPDX-License-Identifier: MPL-2.0
"""Generate Afterlight's original HDR panorama and malformed decoder fixtures."""
from pathlib import Path
import math

root = Path(__file__).resolve().parents[1]
def rgbe(rgb):
    maximum = max(rgb)
    if maximum < 1e-32:
        return [0, 0, 0, 0]
    fraction, exponent = math.frexp(maximum)
    scale = fraction * 256 / maximum
    return [min(255, int(x * scale)) for x in rgb] + [exponent + 128]

width, height = 64, 32
data = bytearray(f'#?RADIANCE\n# Original Afterlight panorama; MPL-2.0\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n'.encode())
for y in range(height):
    row = []
    for x in range(width):
        u, v = (x + .5) / width, (y + .5) / height
        sky = max(0, math.cos(v * math.pi))
        key = 18 * math.exp(-(((u - .2) / .045) ** 2 + ((v - .3) / .08) ** 2))
        rim = 8 * math.exp(-(((u - .7) / .06) ** 2 + ((v - .4) / .12) ** 2))
        row.append(rgbe([.12 + sky * .6 + key + rim * .1,
                         .18 + sky * .9 + key * .6 + rim * .4,
                         .4 + sky * 1.4 + key * .15 + rim]))
    data.extend([2, 2, width >> 8, width & 255])
    for channel in range(4):
        data.append(width)
        data.extend(pixel[channel] for pixel in row)
root.joinpath('sandbox/assets/studio.hdr').write_bytes(data)
root.joinpath('tests/assets/panorama.hdr').write_bytes(data)

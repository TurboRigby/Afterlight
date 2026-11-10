# SPDX-License-Identifier: MPL-2.0
"""Synthesize the station ambience and two original feedback cues."""
from array import array
from math import exp, pi, sin
from pathlib import Path
import random
import sys
import wave

ASSETS = Path(__file__).with_name("assets")
RATE = 22050


def write(name, duration, sample):
    values = array("h")
    for i in range(round(RATE * duration)):
        t = i / RATE
        fade = min(1, t * 25, (duration - t) * 8)
        values.append(round(32767 * fade * sample(t)))
    if sys.byteorder != "little":
        values.byteswap()
    with wave.open(str(ASSETS / (name + ".wav")), "wb") as wav:
        wav.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        wav.writeframes(values.tobytes())


def tone(f, t):
    return sin(2 * pi * f * t)


noise = random.Random(9)
filtered = 0.0


def room(t):
    global filtered
    filtered = .992 * filtered + .008 * noise.uniform(-1, 1)
    swell = .63 + .37 * tone(.19, t)
    return swell * (.12 * tone(42, t) + .06 * tone(59.95, t) + .38 * filtered)


write("room", 11.5, room)
write("relay", .8, lambda t: exp(-7 * t) * (.42 * tone(180, t) + .12 * tone(519, t)))
write("carrier", 2.6, lambda t: .22 * exp(-1.2 * t) * (tone(220, t) + .53 * tone(329.6, t) + .32 * tone(440, t)))

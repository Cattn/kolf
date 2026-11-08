# SPDX-License-Identifier: GPL-2.0-or-later
# Generates Kolf's synthesised sound effects (tap.wav, sand.wav) into sounds/.
# Run: python scripts/make-sounds.py
import math
import os
import random
import struct
import wave

RATE = 44100
OUTPUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sounds')


def write(name, samples):
    peak = max(1e-9, max(abs(s) for s in samples))
    data = b''.join(struct.pack('<h', int(max(-1.0, min(1.0, s / peak * 0.85)) * 32767)) for s in samples)
    with wave.open(os.path.join(OUTPUT, name), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(data)


def tap():
    rng = random.Random(7)
    length = int(RATE * 0.06)
    samples = []
    low = 0.0
    for i in range(length):
        t = i / RATE
        envelope = math.exp(-t * 90)
        noise = rng.uniform(-1, 1)
        low += 0.35 * (noise - low)
        body = math.sin(2 * math.pi * 1450 * t) * 0.6 + math.sin(2 * math.pi * 2900 * t) * 0.2
        samples.append(envelope * (body + 0.5 * low))
    return samples


def sand():
    rng = random.Random(11)
    length = int(RATE * 0.22)
    samples = []
    low = 0.0
    band = 0.0
    for i in range(length):
        t = i / RATE
        attack = min(1.0, t / 0.015)
        envelope = attack * math.exp(-t * 14)
        noise = rng.uniform(-1, 1)
        low += 0.25 * (noise - low)
        band += 0.6 * (low - band)
        samples.append(envelope * (low - band * 0.7))
    return samples


write('tap.wav', tap())
write('sand.wav', sand())
print('wrote tap.wav and sand.wav to', os.path.abspath(OUTPUT))

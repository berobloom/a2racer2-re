#!/usr/bin/env python3
"""Concrete runs of the accelerated sky's per-frame tint 0x418f00 (session 16).

0x418f00 reads the player's chunk [0x4d9e44], the start chunk [0x4a7c58],
the track's chunk count [[0x4e6670] + 8] and game timer 0 (0x4214d0, ms),
and writes the sky quads' horizon colour (v2..v5 +0x10) and the specular
alphas (v0/v1 and v2..v7 +0x14).  0x418830's fog-colour write is skipped
([0x4ea81c] = 0, as with fog off).  Prints a table for a grid of inputs.

Usage (repo root, with .work/venv): emu_skytint.py [spel.dat]"""
import struct
import sys

sys.path.insert(0, 'reverse-engineering/tools')
from symx87 import Tracer

PATH = sys.argv[1] if len(sys.argv) > 1 else '.work/cd/data/spel.dat'
SKY = 0x4a7c68


def run(n_chunks, start, chunk, ms):
    t = Tracer(PATH)
    track = t.alloc(16)
    t.w32(track + 8, n_chunks)
    t.w32(0x4e6670, track)
    t.w32(0x4a7c58, start)
    t.w32(0x4d9e44, chunk)
    t.w32(0x4ea81c, 0)
    t.stub(0x4214d0, lambda tr: ms)
    t.call(0x418f00, [])
    u32 = lambda a: t.r32(a)
    return (u32(SKY + 0x14) >> 24, u32(SKY + 2 * 32 + 0x10) & 0xffffff,
            u32(SKY + 2 * 32 + 0x14) >> 24, u32(SKY + 6 * 32 + 0x14) >> 24)


if __name__ == '__main__':
    print('progress  time s | top spec | horizon colour | horizon spec | bottom spec')
    for frac in (0.0, 0.125, 0.25, 0.5, 0.75, 1.0):
        for secs in (0, 60, 120, 240):
            top, col, hspec, bspec = run(1000, 0, int(frac * 500), secs * 1000)
            print('%8.3f %7d | %8d | %06x         | %12d | %d' % (frac, secs, top, col, hspec, bspec))

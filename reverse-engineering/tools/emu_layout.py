#!/usr/bin/env python3
"""Run the hires HUD layout filler 0x41b860 under Unicorn at a given screen
size and print the {anim, x, y} entries of the hires half of the element
table 0x488510 (element e at 0x488510 + (e + 84) * 12).

Usage: emu_layout.py <spel.dat> [first last] [width height]
"""
import sys
from symx87 import Tracer

t = Tracer(sys.argv[1])
lo, hi = (int(sys.argv[2]), int(sys.argv[3])) if len(sys.argv) > 3 else (34, 83)
w, h = (int(sys.argv[4]), int(sys.argv[5])) if len(sys.argv) > 5 else (640, 480)
t.w32(0x49e72c, w)
t.w32(0x49e730, h)
t.call(0x41b860, [])
for e in range(lo, hi + 1):
    a = 0x488510 + (e + 84) * 12
    an, x, y = (t.r32(a + 4 * k) for k in range(3))
    s = lambda v: v - (1 << 32) if v & 0x80000000 else v
    print(f'{e:3d}  anim {s(an):3d}  ({s(x)}, {s(y)})')

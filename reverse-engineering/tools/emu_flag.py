#!/usr/bin/env python3
"""Run the finish flag's mesh builder (0x41ad40) and wave (0x41ac00) under
Unicorn and print the vertex grid (session 15).  The mesh is 16 triangle
strips of 34 D3DLVERTEX (x, y, z, reserved, colour, specular, u, v) from
0x4a7dd0, 0x440 bytes apart.

Usage: emu_flag.py <spel.dat> [frame]
"""
import math, struct, sys
from symx87 import Tracer

t = Tracer(sys.argv[1])
SIN, COS = t.alloc(4096 * 4), t.alloc(4096 * 4)
for i in range(4096):
    t.mu.mem_write(SIN + 4 * i, struct.pack('<f', math.sin(i * 2 * math.pi / 4096)))
    t.mu.mem_write(COS + 4 * i, struct.pack('<f', math.cos(i * 2 * math.pi / 4096)))
t.w32(0x4ea418, SIN)
t.w32(0x4ea4b4, COS)
t.w32(0x49b018, 4095)
t.call(0x41ad40, [])
t.w32(0x4aa850, 60)                       # frames per wave period (2 s at 30 fps)
t.w32(0x4aa854, int(sys.argv[2]) if len(sys.argv) > 2 else 0)
t.call(0x41ac00, [])
for strip in (0, 8, 15):
    base = 0x4a7dd0 + strip * 0x440
    row = []
    for v in range(0, 34, 1):
        a = base + v * 32
        x, y, z = (t.rf(a + 4 * k) for k in range(3))
        col = t.r32(a + 16)
        u, vv = t.rf(a + 24), t.rf(a + 28)
        row.append((round(x, 1), round(y, 1), round(z, 1), hex(col), round(u, 4), round(vv, 4)))
    print('strip', strip)
    for r in row[:6] + row[-4:]:
        print('  ', r)

#!/usr/bin/env python3
"""Symbolic trace of spel.dat's object-object collision response 0x448eb0
(session 13).  Two objects with symbolic mass terms (+0x60, +0x64), local
velocity (+0x6c..+0x74) and yaw rate (+0x7c), headings 0 (so local = world),
and a symbolic contact point / normal in the collision record 0x4e6760.
Prints the formula of every float the function stores.

Usage: emu_collide.py <spel.dat>"""
import math
import struct
import sys

from symx87 import Tracer

t = Tracer(sys.argv[1])
OBJ, XF = t.alloc(160 * 2), t.alloc(0x88 * 2)
SIN, COS = t.alloc(4096 * 4), t.alloc(4096 * 4)
for i in range(4096):
    t.mu.mem_write(SIN + 4 * i, struct.pack('<f', math.sin(i * 2 * math.pi / 4096)))
    t.mu.mem_write(COS + 4 * i, struct.pack('<f', math.cos(i * 2 * math.pi / 4096)))
def objname(off):
    k, f = divmod(off, 160)
    return f'o{k + 1}_{f:x}'
def xfname(off):
    k, f = divmod(off, 0x88)
    return f'x{k + 1}_{f:x}'
t.name_region(OBJ, OBJ + 320, objname)
t.name_region(XF, XF + 0x88 * 2, xfname)
t.name_region(SIN, SIN + 4 * 4096, lambda off: f'sin{off // 4}')
t.name_region(COS, COS + 4 * 4096, lambda off: f'cos{off // 4}')
t.name_region(0x47fbf0, 0x47fc30, lambda off: None)  # literal constants
t.w32(0x4e48e0, OBJ)
t.w32(0x4ea9e0, XF)
t.w32(0x4ea418, SIN)
t.w32(0x4ea4b4, COS)
t.w32(0x49b010, 4096)
t.mu.mem_write(0x49b014, struct.pack('<f', 4096.0))
t.w32(0x49b018, 4095)
names = {0x60: 'M', 0x64: 'I', 0x6c: 'vx', 0x70: 'vy', 0x74: 'vz', 0x7c: 'w'}
vals = {0: {0x60: 1.0 / 1000, 0x64: 1.0 / 5000, 0x6c: 1.5, 0x70: 0.0, 0x74: 30.0, 0x7c: 0.1},
        1: {0x60: 1.0 / 1300, 0x64: 1.0 / 6000, 0x6c: -0.5, 0x70: 0.0, 0x74: 20.0, 0x7c: -0.05}}
for k in (0, 1):
    o = OBJ + 160 * k
    t.w32(o + 8, k)  # its transform
    for f, nm in names.items():
        t.set_float(o + f, vals[k][f], f'{nm}{k + 1}')
    x = XF + 0x88 * k
    t.set_float(x + 0x50, 100.0 * k, None)
    t.set_float(x + 0x54, 0.0, None)
    t.set_float(x + 0x58, 40.0 * k, None)
    t.set_float(x + 0x78, 0.0, None)
R = 0x4e6760
t.w32(R + 8, 0)
t.w32(R + 0xc, 1)
for i, (nm, v) in enumerate([('px', 50.0), ('py', 0.0), ('pz', 20.0)]):
    t.set_float(R + 0x10 + 4 * i, v, nm)
for i, (nm, v) in enumerate([('nx', 0.6), ('ny', 0.0), ('nz', 0.8)]):
    t.set_float(R + 0x1c + 4 * i, v, nm)
t.call(0x448eb0, [R])
labels = [('impulse 0x4e6788', 0x4e6788)]
for k in (0, 1):
    for f, nm in names.items():
        labels.append((f'obj{k + 1} {nm} (+{f:#x})', OBJ + 160 * k + f))
t.report(labels, simplify=True)
for k, v in sorted(t.mem.items()):
    if not (OBJ <= k < OBJ + 320) and k != 0x4e6788 and not (t.STK <= k < t.STK + 0x100000):
        print(f'other store {k:#x}')

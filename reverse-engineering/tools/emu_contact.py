#!/usr/bin/env python3
"""Run spel.dat's collision contact code concretely under Unicorn (session 14).

The 76-byte collision entry (0x449a30 fills them at 0x4e6790): four corners,
x/y/z each, at +0x00..+0x2c (the mesh's bottom face, bbox corners 0, 1, 5,
4), the position at +0x30, the heading (4096 a turn) at +0x44, the object at
+0x40 and +0x48 a flag.  This sets two entries up and calls

  0x44b860(out, side, entry, angle)  -- the normal of a side
  0x44a520(A, B, move)               -- B's corners swept by `move` into A's
                                        box, in A's frame; returns t in 0..1
                                        and leaves the hit at 0x4e6678..

Usage: emu_contact.py <spel.dat>
"""
import math
import struct
import sys

from symx87 import Tracer
from unicorn.x86_const import UC_X86_REG_FP0, UC_X86_REG_FPSW

t = Tracer(sys.argv[1])
SIN, COS = t.alloc(4096 * 4), t.alloc(4096 * 4)
for i in range(4096):
    t.mu.mem_write(SIN + 4 * i, struct.pack('<f', math.sin(i * 2 * math.pi / 4096)))
    t.mu.mem_write(COS + 4 * i, struct.pack('<f', math.cos(i * 2 * math.pi / 4096)))
t.w32(0x4ea418, SIN)
t.w32(0x4ea4b4, COS)
t.w32(0x49b010, 4096)
t.mu.mem_write(0x49b014, struct.pack('<f', 4096.0))
t.w32(0x49b018, 4095)


def entry(hx, hz, x, z, heading, obj):
    e = t.alloc(0x4c)
    corners = [(-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz)]  # bbox 0, 1, 5, 4
    for k, (cx, cz) in enumerate(corners):
        for j, v in enumerate((cx, 0.0, cz)):
            t.set_float(e + 12 * k + 4 * j, v)
    for j, v in enumerate((x, 0.0, z)):
        t.set_float(e + 0x30 + 4 * j, v)
    t.w32(e + 0x40, obj)
    t.w32(e + 0x44, heading)
    t.w32(e + 0x48, 0)
    return e


def st0():
    """st(0) after a call, as a float (Unicorn gives (mantissa, exponent))."""
    sw = t.mu.reg_read(UC_X86_REG_FPSW)
    top = (sw >> 11) & 7
    m, ex = t.mu.reg_read(UC_X86_REG_FP0 + top)
    sign = -1.0 if ex & 0x8000 else 1.0
    ex &= 0x7fff
    if ex == 0 and m == 0:
        return 0.0
    return sign * (m / (1 << 63)) * 2.0 ** (ex - 16383)


def normal(e, side, angle):
    out = t.alloc(16)
    t.call(0x44b860, [out, side, e, angle])
    return tuple(round(t.rf(out + 4 * i), 4) for i in range(3))


def sweep(a, b, move):
    mv = t.alloc(16)
    for j, v in enumerate(move):
        t.set_float(mv + 4 * j, v)
    t.w32(0x4e6678, 0)
    t.call(0x44a520, [a, b, mv])
    tt = st0()
    hit = t.r32(0x4e6678)
    pt = tuple(round(t.rf(0x4e667c + 4 * i), 3) for i in range(3))
    return round(tt, 4), hit, pt, t.r32(0x4e6698)


if __name__ == '__main__':
    box = entry(10.0, 20.0, 0.0, 0.0, 0, 1)
    for ang in (0, 1024):
        print('angle', ang, [normal(box, k, ang) for k in range(4)])
    A = entry(10.0, 20.0, 0.0, 0.0, 0, 1)
    for (bx, bz, hd, mv) in [(30.0, 0.0, 0, (-15.0, 0.0, 0.0)),   # B to the right, moving left
                             (0.0, 50.0, 0, (0.0, 0.0, -15.0)),   # B ahead, moving back
                             (30.0, 0.0, 512, (-15.0, 0.0, 0.0)), # B turned 45 degrees
                             (30.0, 0.0, 0, (-5.0, 0.0, 0.0))]:   # stops short
        B = entry(8.0, 18.0, bx, bz, hd, 2)
        print('B at', (bx, bz), 'heading', hd, 'move', mv, '->', sweep(A, B, mv))

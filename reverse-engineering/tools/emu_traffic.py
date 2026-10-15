#!/usr/bin/env python3
"""Run handle_basic_ai's helpers (traffic driving, session 15) symbolically.

  0x406fd0(obj, wp, ai)        the yaw rate toward waypoint `wp`
  0x406e10(r, wp, &pos, len)   "has the car reached `wp`?"

The records are laid out as the code reads them: object 160 bytes at
[0x4e48e0] (+8 the 3D node), AI record 108 bytes at [0x4a5b40] (+0x24 the
behaviour), behaviour 44 bytes at [0x4a5b88], waypoint 80 bytes at
[0x4e6628] (+8 x, y, z), 3D node 153 bytes at [0x4ea9e0] (+0x50 position,
+0x10 + 0x20 the forward axis, read by 0x45e230).

Usage: emu_traffic.py <spel.dat>
"""
import sys

import sympy as sp
from symx87 import Tracer
from unicorn.x86_const import UC_X86_REG_EAX

t = Tracer(sys.argv[1])
OBJ, AI, BEH, WP, NODE = t.alloc(160), t.alloc(108), t.alloc(44 * 4), t.alloc(80 * 2), t.alloc(153)
t.w32(0x4e48e0, OBJ)
t.w32(0x4a5b40, AI)
t.w32(0x4a5b88, BEH)
t.w32(0x4e6628, WP)
t.w32(0x4ea9e0, NODE)
t.w32(OBJ + 8, 0)          # node index 0
t.w32(AI + 0x24, 1)        # behaviour 1


def setup(px, pz, fx, fz, wx, wz, k=2.0):
    t.reset()
    t.set_float(NODE + 0x50, px, 'px')
    t.set_float(NODE + 0x54, 0.0, 'py')
    t.set_float(NODE + 0x58, pz, 'pz')
    t.set_float(NODE + 0x30, fx, 'fx')
    t.set_float(NODE + 0x34, 0.0, 'fy')
    t.set_float(NODE + 0x38, fz, 'fz')
    t.set_float(WP + 80 + 8, wx, 'wx')
    t.set_float(WP + 80 + 12, 0.0, 'wy')
    t.set_float(WP + 80 + 16, wz, 'wz')
    t.set_float(BEH + 44 + 0xc, k, 'K')


if __name__ == '__main__':
    for case in [(0, 0, 0, 1, 10, 100), (0, 0, 0, 1, -10, 100), (0, 0, 1, 0, 0, 100),
                 (5, 7, 0.6, 0.8, -30, 40)]:
        setup(*case)
        t.call(0x406fd0, [0, 1, 0])
        out = t.alloc(16)
        # st(0) holds the result: store it through the shadow
        e = t.st[0] if t.st else None
        print('case', case, '->', sp.simplify(e) if e is not None else None)


def arrived(r, dist_to_wp, seglen):
    """0x406e10 with the car `dist_to_wp` units from the waypoint."""
    setup(0.0, 0.0, 0.0, 1.0, 0.0, dist_to_wp)
    pos = NODE + 0x50
    t.call(0x406e10, [('f', r, None), 1, pos, ('f', seglen, None)])
    return t.mu.reg_read(UC_X86_REG_EAX)


if __name__ == '__main__':
    for r in (1.0, 1.2):
        for seg in (128.0, 100.0, 124.0):
            row = [(dd, arrived(r, dd, seg)) for dd in (0.5, 1.0, 1.4, 1.6, 2.0, 5.0, 20.0, 100.0)]
            print('r', r, 'seglen', seg, row)

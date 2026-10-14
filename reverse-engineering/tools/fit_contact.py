#!/usr/bin/env python3
"""Compare a model of the swept corner test 0x44a520 with the original,
run under Unicorn (emu_contact.py), on random boxes (session 14).

Usage: fit_contact.py <spel.dat> [cases]
"""
import math
import random
import sys

import emu_contact as E

TURN = 4096


def rot(x, z, h):
    """The engine's heading convention (0x44aef0, 0x44b860): x' = x c - z s,
    z' = x s + z c, from the 4096-entry tables."""
    a = (int(h) & (TURN - 1)) * 2 * math.pi / TURN
    c, s = math.cos(a), math.sin(a)
    return x * c - z * s, x * s + z * c


def corners(hx, hz):
    return [(-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz)]


def nearest_side(px, pz, hx, hz):
    d = [pz + hz, hx - px, hz - pz, px + hx]  # sides 0..3: -z, +x, +z, -x
    return min(range(4), key=lambda k: d[k])


def inside(px, pz, hx, hz):
    return -hx <= px <= hx and -hz <= pz <= hz


def entry(p0, p1, hx, hz):
    """Slab entry of p0->p1 into the box: (t, side) or None."""
    (x0, z0), (x1, z1) = p0, p1
    dx, dz = x1 - x0, z1 - z0
    best = None
    for side, (axis, bound, sgn) in enumerate([(1, -hz, -1), (0, hx, 1), (1, hz, 1), (0, -hx, -1)]):
        d = dz if axis else dx
        s = z0 if axis else x0
        if d == 0 or (s - bound) * sgn < 0:   # parallel, or already behind this side
            continue
        t = (bound - s) / d
        if not 0.0 <= t <= 1.0:
            continue
        ox, oz = x0 + dx * t, z0 + dz * t
        if (axis and -hx <= ox <= hx) or (not axis and -hz <= oz <= hz):
            if best is None or t < best[0]:
                best = (t, side)
    return best


def model(A, B, move):
    """A, B = (hx, hz, x, z, heading); move in A's frame.
    Returns (t, hit, corner index of B, side of A)."""
    ahx, ahz, ax, az, ah = A
    bhx, bhz, bx, bz, bh = B
    best = (1.0, 0, 0, 0)
    for k, (cx, cz) in enumerate(corners(bhx, bhz)):
        wx, wz = rot(cx, cz, bh)
        lx, lz = rot(wx + bx - ax, wz + bz - az, -ah)
        p0, p1 = (lx, lz), (lx + move[0], lz + move[2])
        if inside(*p0, ahx, ahz):
            if inside(*p1, ahx, ahz):
                t, side = 0.0, nearest_side(*p0, ahx, ahz)
            else:
                continue
        else:
            e = entry(p0, p1, ahx, ahz)
            if e is None:
                continue
            t, side = e
        if t < best[0]:
            best = (t, 1, k, side)
    return best


def run_c(cli, cases):
    """The port's a2_contact_sweep (contact_cli) on the same cases."""
    import subprocess
    lines = []
    for A, B, move in cases:
        lines.append(' '.join('%.6f' % v for v in (
            -A[0], -A[1], A[0], A[1], A[2], A[3], A[4], -B[0], -B[1], B[0], B[1], B[2], B[3], B[4],
            move[0], move[2])))
    out = subprocess.run([cli], input='\n'.join(lines) + '\n', capture_output=True, text=True).stdout
    res = []
    for l in out.split('\n'):
        if l.strip():
            h, t, c, s = l.split()
            res.append((float(t), int(h), int(c), int(s)))
    return res


def main():
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    cli = sys.argv[3] if len(sys.argv) > 3 else None
    if cli:
        rnd = random.Random(7)
        cases = []
        for i in range(n):
            A = (rnd.uniform(5, 25), rnd.uniform(10, 45), 0.0, 0.0, rnd.randrange(TURN))
            B = (rnd.uniform(5, 25), rnd.uniform(10, 45), rnd.uniform(-70, 70), rnd.uniform(-90, 90),
                 rnd.randrange(TURN))
            move = (rnd.uniform(-40, 40), 0.0, rnd.uniform(-40, 40))
            cases.append((A, B, move))
        port = run_c(cli, cases)
        bad = 0
        for i, ((A, B, move), p) in enumerate(zip(cases, port)):
            ea = E.entry(A[0], A[1], A[2], A[3], A[4], 1)
            eb = E.entry(B[0], B[1], B[2], B[3], B[4], 2)
            t, hit, pt, side = E.sweep(ea, eb, move)
            pc = corners(B[0], B[1])[p[2]]
            same = hit == p[1] and abs(t - p[0]) < 2e-3 and (not hit or (
                side == p[3] and abs(pt[0] - pc[0]) < 1e-2 and abs(pt[2] - pc[1]) < 1e-2))
            if not same:
                bad += 1
                if bad <= 8:
                    print('case', i, 'original', (round(t, 4), hit, pt, side), 'port', p)
        print('port vs original: %d of %d differ' % (bad, n))
        return
    rnd = random.Random(7)
    bad = 0
    for i in range(n):
        A = (rnd.uniform(5, 25), rnd.uniform(10, 45), 0.0, 0.0, rnd.randrange(TURN))
        B = (rnd.uniform(5, 25), rnd.uniform(10, 45), rnd.uniform(-70, 70), rnd.uniform(-90, 90),
             rnd.randrange(TURN))
        move = (rnd.uniform(-40, 40), 0.0, rnd.uniform(-40, 40))
        ea = E.entry(A[0], A[1], A[2], A[3], A[4], 1)
        eb = E.entry(B[0], B[1], B[2], B[3], B[4], 2)
        t, hit, pt, side = E.sweep(ea, eb, move)
        m = model(A, B, move)
        mc = corners(B[0], B[1])[m[2]]
        same = hit == m[1] and abs(t - m[0]) < 2e-3 and (not hit or (
            side == m[3] and abs(pt[0] - mc[0]) < 1e-2 and abs(pt[2] - mc[1]) < 1e-2))
        if not same:
            bad += 1
            if bad <= 12:
                print('case', i, 'A', [round(v, 2) for v in A], 'B', [round(v, 2) for v in B],
                      'move', [round(v, 2) for v in move])
                print('   original t %.4f hit %d corner %s side %d' % (t, hit, pt, side))
                print('   model    t %.4f hit %d corner %s side %d' % (m[0], m[1], mc, m[3]))
    print('%d of %d differ' % (bad, n))


if __name__ == '__main__':
    main()

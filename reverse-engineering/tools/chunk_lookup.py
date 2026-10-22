#!/usr/bin/env python3
"""Compare two position -> chunk lookups on a track's waypoints (session 17).

  nearest  the port's old a2_scene_nearest_chunk: the chunk whose position is
           nearest in xz, over the whole track, then a2_scene_road_height's
           step back when the point is behind that chunk's start
  walk     the engine's 0x445a10: from a hint chunk (the car's previous
           one), walk along the chunk order to the chunk whose start line the
           point is past and the next one's it is not

For every waypoint (owned by chunk c in .pmp, hint c) it prints where the
plane height of the chosen chunk (0x442f00) differs from the waypoint's own
y by more than THRESH units.

Usage: chunk_lookup.py <datadir> <etappeN> [thresh=10]
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../port/a2tools'))
import track  # noqa: E402

d, name = sys.argv[1], sys.argv[2]
thresh = float(sys.argv[3]) if len(sys.argv) > 3 else 10.0
_, pmp = track.load_pmp(open(os.path.join(d, name + '.pmp'), 'rb').read())
_, wps = track.load_dmp(open(os.path.join(d, name + '.dmp'), 'rb').read())
n = len(pmp)
P = [c['pos'] for c in pmp]


def frame(i):
    """0x442cb0: forward toward the next chunk (the last toward chunk 0),
    right = (0,1,0) x forward, up = forward x right; plane (up, d)."""
    a, b = P[i], P[(i + 1) % n]
    f = [b[k] - a[k] for k in range(3)]
    fl = math.sqrt(sum(v * v for v in f)) or 1.0
    f = [v / fl for v in f]
    r = [f[2], 0.0, -f[0]]
    rl = math.hypot(r[0], r[2]) or 1.0
    r = [r[0] / rl, 0.0, r[2] / rl]
    u = [f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]]
    ul = math.sqrt(sum(v * v for v in u)) or 1.0
    u = [v / ul for v in u]
    return f, r, u, sum(u[k] * a[k] for k in range(3))


F = [frame(i) for i in range(n)]


def height(i, x, z):
    _, _, u, dd = F[i]
    return (dd - u[0] * x - u[2] * z) / u[1] if abs(u[1]) > 1e-6 else P[i][1]


def side(i, x, z):
    """0x445a10's test: (x - cx) r.z - (z - cz) r.x; <= 0 is past the start"""
    _, r, _, _ = F[i]
    return (x - P[i][0]) * r[2] - (z - P[i][2]) * r[0]


def walk(hint, x, z):
    i = hint
    if side(i, x, z) <= 0:
        while i < n:
            if side(i, x, z) > 0:
                return i - 1 if i - 1 != -1 else n - 1
            i += 1
    else:
        while i >= 0:
            if side(i, x, z) < 0:
                return i
            i -= 1
    for i in range(n):  # 0x445c43: from the start
        if side(i, x, z) > 0:
            return i - 1 if i - 1 != -1 else n - 1
    return n - 1  # the engine returns n here, one past the end


def nearest(x, z):
    best = min(range(n), key=lambda i: (P[i][0] - x) ** 2 + (P[i][2] - z) ** 2)
    nx = (best + 1) % n
    fx, fz = P[nx][0] - P[best][0], P[nx][2] - P[best][2]
    if (x - P[best][0]) * fx + (z - P[best][2]) * fz < 0:
        best = best - 1 if best > 0 else n - 1
    return best


bad = {'nearest': [], 'walk': []}
total = 0
for c, ch in enumerate(pmp):
    for wi in list(ch['wp_left']) + list(ch['wp_right']):
        if not 0 <= wi < len(wps):
            continue
        w = wps[wi]
        total += 1
        for kind, k in (('nearest', nearest(w['x'], w['z'])), ('walk', walk(c, w['x'], w['z']))):
            dy = height(k, w['x'], w['z']) - w['y']
            if abs(dy) > thresh:
                bad[kind].append((c, wi, k, dy))
print('%s: %d chunks, %d waypoints' % (name, n, total))
for kind in ('nearest', 'walk'):
    b = bad[kind]
    chunks = sorted(set(x[0] for x in b))
    print('  %-7s %4d waypoints off by > %g units, in %d chunks' % (kind, len(b), thresh, len(chunks)))
    worst = sorted(b, key=lambda x: -abs(x[3]))[:4]
    for c, wi, k, dy in worst:
        print('          wp %d of chunk %d -> chunk %d, %+.1f' % (wi, c, k, dy))

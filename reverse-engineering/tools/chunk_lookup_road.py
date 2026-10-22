#!/usr/bin/env python3
"""The two position -> chunk lookups of chunk_lookup.py, off the waypoints
(session 18, open question 60).  For every chunk i it samples 5 points along
i -> i+1 and 17 lateral offsets (-80..80 units along the chunk's right
vector), takes the walk (0x445a10, hint i) and the port's nearest-chunk
lookup, and counts the points where their planes (0x442f00) differ by more
than 2 units.

Usage: chunk_lookup_road.py <datadir> <etappeN>
"""
import contextlib
import io
import os
import runpy
import sys

import numpy as np

name = sys.argv[2]
sys.argv = ['chunk_lookup.py', sys.argv[1], name, '1e9']
with contextlib.redirect_stdout(io.StringIO()):
    g = runpy.run_path(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'chunk_lookup.py'))
P, F, n, height, walk = g['P'], g['F'], g['n'], g['height'], g['walk']
PX = np.array([p[0] for p in P])
PZ = np.array([p[2] for p in P])


def nearest(x, z):
    """chunk_lookup.py's nearest, vectorised"""
    best = int(np.argmin((PX - x) ** 2 + (PZ - z) ** 2))
    nx = (best + 1) % n
    fx, fz = P[nx][0] - P[best][0], P[nx][2] - P[best][2]
    if (x - P[best][0]) * fx + (z - P[best][2]) * fz < 0:
        best = best - 1 if best > 0 else n - 1
    return best


bad = []
tot = 0
for i in range(n - 1):
    j = i + 1
    _, r, _, _ = F[i]
    for s in (0.05, 0.25, 0.5, 0.75, 0.95):
        for lat in range(-80, 81, 10):
            x = P[i][0] + (P[j][0] - P[i][0]) * s + r[0] * lat
            z = P[i][2] + (P[j][2] - P[i][2]) * s + r[2] * lat
            w, k = walk(i, x, z), nearest(x, z)
            tot += 1
            dy = height(k, x, z) - height(w, x, z)
            if abs(dy) > 2:
                bad.append((abs(dy), i, s, lat, w, k))
bad.sort(reverse=True)
print('%s: %d chunks, %d points, %d differ by > 2 units, in %d chunks'
      % (name, n, tot, len(bad), len(set(b[1] for b in bad))))
for b in bad[:5]:
    print('  dy %.1f chunk %d s %.2f lat %d walk %d nearest %d' % b)

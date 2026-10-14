#!/usr/bin/env python3
"""Find where a track's road planes overlap at different heights (session 14).

For every waypoint of every chunk, collect the road-plane (.rmp list 0)
triangles of chunks c-1..c+1 whose xz footprint contains it -- the set the
port's a2_scene_road_height searches -- and report waypoints where those
triangles disagree in height by more than THRESH units, along with the
waypoint's own y (.dmp f3, which follows bridges).

Usage: road_overlap.py <datadir> <etappeN> [thresh=3]
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../port/a2tools'))
import track  # noqa: E402

d, name = sys.argv[1], sys.argv[2]
thresh = float(sys.argv[3]) if len(sys.argv) > 3 else 3.0
_, n, chunks = track.load_rmp(open(os.path.join(d, name + '.rmp'), 'rb').read())
dmp = track.load_dmp(open(os.path.join(d, name + '.dmp'), 'rb').read())


def tris(c):
    if c < 0 or c >= n:
        return
    for p in chunks[c][0]:
        v = p['vertices']
        for t in p['triangles']:
            yield c, v[t[0]], v[t[1]], v[t[2]]


def hits(c, x, z):
    out = []
    for cc in (c - 1, c, c + 1):
        for ch, a, b, e in tris(cc):
            v0x, v0z = b[0] - a[0], b[2] - a[2]
            v1x, v1z = e[0] - a[0], e[2] - a[2]
            den = v0x * v1z - v1x * v0z
            if abs(den) < 1e-6:
                continue
            px, pz = x - a[0], z - a[2]
            u = (px * v1z - v1x * pz) / den
            v = (v0x * pz - px * v0z) / den
            if u < -0.001 or v < -0.001 or u + v > 1.001:
                continue
            out.append((ch, a[1] + u * (b[1] - a[1]) + v * (e[1] - a[1])))
    return out


_, wps = dmp
_, pmp = track.load_pmp(open(os.path.join(d, name + '.pmp'), 'rb').read())
bad = {}
for c, ch in enumerate(pmp):
    for wi in list(ch['wp_left']) + list(ch['wp_right']):
        if not 0 <= wi < len(wps):
            continue
        w = wps[wi]
        h = hits(c, w['x'], w['z'])
        if not h:
            continue
        ys = [y for _, y in h]
        if max(ys) - min(ys) > thresh:
            bad.setdefault(c, []).append((w['y'], sorted(set((cc, round(y, 1)) for cc, y in h))))
for c in sorted(bad):
    wy, hs = bad[c][0]
    print('chunk %d: %d waypoints; e.g. wp y %.1f, road hits %s' % (c, len(bad[c]), wy, hs))
print('%d chunks with overlapping road heights' % len(bad))

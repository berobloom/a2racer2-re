#!/usr/bin/env python3
"""Tools for menu.dat's art and layout (session 12).  Needs .work/venv
(numpy, pillow, opencv-python-headless).

  menutools.py sheet <datadir> <out.png> [lo hi]
      MENURC sprites lo..hi-1 as a labelled contact sheet, in the menu palette
  menutools.py fonts <datadir> <out.png>
      the seven nested .IMG fonts
  menutools.py find <datadir> <capture.png> [background.pcx-name] [lo hi]
      template-match MENURC sprites in a 640x480 capture of the original menu
      (Wine, Xvfb 640x480x24).  A hit needs >= 60% of its opaque pixels within
      8 of the capture; hits that the background already shows are dropped.
  menutools.py text <datadir> <capture.png> [--inks 0x1f,0x3a] "string"...
      locate strings drawn in one of the fonts (fixed cell advance), also
      trying each font with its ink index replaced by the given ones;
      --shadow adds a black copy at (+1, +1) under the text
  menutools.py baked <datadir> <background-name> id...
      exact position of sprites that are baked into a background PCX
  menutools.py diff <a.png> <b.png> [out.png]
      pixel diff of two 640x480 images: count and bounding boxes

The palette is the one shared by the seven full-screen menu PCXs (hoofd.PCX).
"""
import struct
import sys

import cv2
import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, __file__.rsplit('/', 1)[0])


def read_archive(datadir, img, ind):
    """Named (Mrcs) or binary (MENURC) index -> {name or index: bytes}."""
    I = open(f'{datadir}/{ind}', 'rb').read()
    M = open(f'{datadir}/{img}', 'rb').read()
    count = struct.unpack_from('<H', I, 0)[0]
    if 2 + 4 * count == len(I):  # binary index
        offs = list(struct.unpack_from(f'<{count}I', I, 2)) + [len(M)]
        return {i: M[offs[i]:offs[i + 1]] for i in range(count)}
    return None


_MRCS = {}


def mrcs(datadir):
    """Mrcs.img entries by name, listed with the port's a2_dump."""
    if datadir in _MRCS:
        return _MRCS[datadir]
    import subprocess
    import os
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out = subprocess.run([f'{root}/port/build/a2_dump', 'archive', f'{datadir}/Mrcs.img',
                          f'{datadir}/Mrcs.ind', '--list'], capture_output=True, text=True).stdout
    data = open(f'{datadir}/Mrcs.img', 'rb').read()
    files = {}
    for line in out.splitlines()[1:]:
        parts = line.replace('[', ' ').replace(']', ' ').split()
        if len(parts) < 4:
            continue
        name = parts[1]
        off = int(line.split('off=')[1].split()[0])
        size = int(line.split('size=')[1].split()[0])
        files[name] = data[off:off + size]
    _MRCS[datadir] = files
    return files


def palette(datadir):
    import io
    pcx = mrcs(datadir)['hoofd.PCX']
    return np.array(Image.open(io.BytesIO(pcx)).getpalette()[:768], np.uint8).reshape(256, 3)


def background(datadir, name):
    import io
    return np.asarray(Image.open(io.BytesIO(mrcs(datadir)[name])))


def sprites(datadir):
    raw = read_archive(datadir, 'MENURC.IMG', 'MENURC.IND')
    out = {}
    for i, b in raw.items():
        w, h, ax, ay = struct.unpack_from('<4H', b, 0)
        out[i] = np.frombuffer(b[8:8 + w * h], np.uint8).reshape(h, w)
    return out


FONTS = ['CARVED.IMG', 'SMALLBT.IMG', 'ascii12.img', 'cyber.img', 'BUTFNT.IMG', 'STDFONT.IMG',
         'midfont.img']


def fonts(datadir):
    files = mrcs(datadir)
    out = {}
    for f in FONTS:
        d = files[f]
        w, h, ink, key = struct.unpack_from('<4H', d, 256)
        g = np.frombuffer(d[264:], np.uint8).reshape(256, h, w)
        out[f] = g
    return out


FONT_INK = {'CARVED.IMG': 0xfd, 'SMALLBT.IMG': 0x3f, 'ascii12.img': 0xc8, 'cyber.img': 0x70,
            'BUTFNT.IMG': 0xc8, 'STDFONT.IMG': 0x3f, 'midfont.img': 0x3f}


def render_text(glyphs, text, ink=None, font_ink=None):
    """ink: replace the font's own ink index (header u16 at 260) with this one,
    as the menu's set-colour call (0x40f4f0) does."""
    n, h, w = glyphs.shape
    out = np.full((h, max(1, len(text)) * w), 255, np.uint8)
    for k, ch in enumerate(text.encode('latin1')):
        g = glyphs[ch]
        if ink is not None and font_ink is not None:
            g = np.where(g == font_ink, ink, g)
        seg = out[:, k * w:(k + 1) * w]
        out[:, k * w:(k + 1) * w] = np.where(g != 255, g, seg)
    return out


def cmd_sheet(datadir, outpath, lo=0, hi=10**9):
    pal = palette(datadir)
    spr = sprites(datadir)
    tiles = [(i, s) for i, s in spr.items() if lo <= i < hi]
    W, x, y, rowh, pos = 1400, 0, 0, 0, []
    for i, s in tiles:
        h, w = s.shape
        w = max(w, 40)
        if x + w > W:
            x, y, rowh = 0, y + rowh + 14, 0
        pos.append((i, s, x, y))
        x += w + 6
        rowh = max(rowh, h)
    sheet = Image.new('RGB', (W, y + rowh + 16), (40, 60, 40))
    dr = ImageDraw.Draw(sheet)
    for i, s, x, y in pos:
        rgb = Image.fromarray(pal[s])
        mask = Image.fromarray(((s != 255) * 255).astype(np.uint8))
        sheet.paste(rgb, (x, y + 12), mask)
        dr.text((x, y), str(i), fill=(255, 255, 0))
    sheet.save(outpath)


def cmd_fonts(datadir, outpath):
    pal = palette(datadir)
    rows = []
    for f, g in fonts(datadir).items():
        n, h, w = g.shape
        sheet = np.full((3 * (h + 2), 32 * (w + 2), 3), (30, 70, 30), np.uint8)
        for ch in range(32, 128):
            k = ch - 32
            y0, x0 = (k // 32) * (h + 2), (k % 32) * (w + 2)
            m = g[ch] != 255
            sheet[y0:y0 + h, x0:x0 + w][m] = pal[g[ch]][m]
        rows.append(sheet)
    W = max(r.shape[1] for r in rows)
    out = np.vstack([np.pad(r, ((0, 4), (0, W - r.shape[1]), (0, 0))) for r in rows])
    Image.fromarray(out).resize((out.shape[1] * 2, out.shape[0] * 2), Image.NEAREST).save(outpath)


def cmd_find(datadir, cap_path, bgname=None, lo=0, hi=10**9):
    pal = palette(datadir)
    cap = np.asarray(Image.open(cap_path).convert('RGB')).astype(np.float32)
    bg = pal[background(datadir, bgname)].astype(np.float32) if bgname else None
    hits = []
    for i, s in sprites(datadir).items():
        if not lo <= i < hi:
            continue
        h, w = s.shape
        mask = s != 255
        if w > 640 or h > 480 or mask.sum() < 48:
            continue
        tpl = pal[s].astype(np.float32)
        m3 = np.repeat(mask[:, :, None].astype(np.float32), 3, 2)
        res = cv2.matchTemplate(cap, tpl, cv2.TM_SQDIFF, mask=m3) / (mask.sum() * 3)
        while True:
            mn, _, (x, y), _ = cv2.minMaxLoc(res)
            if mn > 4000:
                break
            good = (np.abs(cap[y:y + h, x:x + w] - tpl).max(2) <= 8)[mask].mean()
            if good < 0.6:
                res[max(0, y - 2):y + 3, max(0, x - 2):x + 3] = 1e9
                continue
            if bg is not None:
                bgood = (np.abs(bg[y:y + h, x:x + w] - tpl).max(2) <= 8)[mask].mean()
                if bgood >= good - 0.02:
                    res[max(0, y - 3):y + 4, max(0, x - 3):x + 4] = 1e9
                    continue
            hits.append((i, x, y, w, h, good))
            res[max(0, y - h // 2):y + h // 2 + 1, max(0, x - w // 2):x + w // 2 + 1] = 1e9
    for i, x, y, w, h, g in sorted(hits, key=lambda t: (t[2], t[1])):
        print(f'sprite {i:3d} at ({x:3d},{y:3d}) {w}x{h}  match {g:.2f}')


def shadowed(px, dx=1, dy=1):
    """Black (index 0) copy of the text's opaque pixels at (+dx, +dy), the
    text drawn over it."""
    h, w = px.shape
    out = np.full((h + dy, w + dx), 255, np.uint8)
    m = px != 255
    out[dy:, dx:][m] = 0
    out[:h, :w][m] = px[m]
    return out


def cmd_text(datadir, cap_path, strings, inks=(None,), shadow=False):
    pal = palette(datadir)
    cap = np.asarray(Image.open(cap_path).convert('RGB')).astype(np.float32)
    F = fonts(datadir)
    for text in strings:
        best = []
        for f, g in F.items():
          for ink in inks:
            px = render_text(g, text, ink, FONT_INK[f])
            if shadow:
                px = shadowed(px)
            mask = px != 255
            if mask.sum() < 10:
                continue
            tpl = pal[px].astype(np.float32)
            m3 = np.repeat(mask[:, :, None].astype(np.float32), 3, 2)
            res = cv2.matchTemplate(cap, tpl, cv2.TM_SQDIFF, mask=m3) / (mask.sum() * 3)
            mn, _, loc, _ = cv2.minMaxLoc(res)
            best.append((mn, f, loc, ink))
        best.sort(key=lambda t: t[0])
        mn, f, loc, ink = best[0]
        print(f'{text!r}: {f} ink {ink} at {loc}  err {mn:.2f}' +
              ('' if mn < 5 else '  (no clean match)'))


def cmd_baked(datadir, bgname, ids):
    bg = background(datadir, bgname).astype(np.float32)
    spr = sprites(datadir)
    for i in ids:
        s = spr[i]
        m = (s != 255).astype(np.float32)
        r = cv2.matchTemplate(bg, s.astype(np.float32), cv2.TM_SQDIFF, mask=m)
        mn, _, loc, _ = cv2.minMaxLoc(r)
        print(f'{bgname} sprite {i} {s.shape[1]}x{s.shape[0]} at {loc}',
              'exact' if mn == 0 else f'err {mn:.0f}')


def cmd_diff(a, b, out=None):
    A = np.asarray(Image.open(a).convert('RGB')).astype(int)
    B = np.asarray(Image.open(b).convert('RGB')).astype(int)
    d = np.abs(A - B).max(2) > 1  # Wine shows palette black as (1, 0, 0)
    print('differing pixels:', int(d.sum()))
    if d.any():
        n, lab, stats, _ = cv2.connectedComponentsWithStats(d.astype(np.uint8), connectivity=8)
        boxes = sorted(stats[1:].tolist(), key=lambda s: -s[4])
        for x, y, w, h, area in boxes[:25]:
            print(f'  box ({x},{y}) {w}x{h}  {area} px')
    if out:
        vis = (A * 0.35).astype(np.uint8)
        vis[d] = (255, 0, 0)
        Image.fromarray(vis).save(out)


if __name__ == '__main__':
    a = sys.argv[1:]
    if not a:
        print(__doc__)
    elif a[0] == 'sheet':
        cmd_sheet(a[1], a[2], *(int(x) for x in a[3:5]))
    elif a[0] == 'fonts':
        cmd_fonts(a[1], a[2])
    elif a[0] == 'find':
        rng = [int(x) for x in a[4:6]] if len(a) > 5 else []
        cmd_find(a[1], a[2], a[3] if len(a) > 3 and a[3] != '-' else None, *rng)
    elif a[0] == 'text':
        rest, inks, shadow = a[3:], [None], False
        while rest and rest[0].startswith('--'):
            if rest[0] == '--inks':
                inks += [int(x, 0) for x in rest[1].split(',')]
                rest = rest[2:]
            elif rest[0] == '--shadow':
                shadow, rest = True, rest[1:]
        cmd_text(a[1], a[2], rest, inks, shadow)
    elif a[0] == 'baked':
        cmd_baked(a[1], a[2], [int(x) for x in a[3:]])
    elif a[0] == 'diff':
        cmd_diff(*a[1:4])

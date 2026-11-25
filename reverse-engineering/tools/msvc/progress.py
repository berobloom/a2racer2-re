#!/usr/bin/env python3
"""progress.py: writes PROGRESS.md, how much of spel.dat is rebuilt from C++.

usage: reverse-engineering/tools/msvc/progress.py [--out FILE]

Builds src/spel with src/check.sh and takes its verdicts: a function counts as done only when
our compiled bytes equal the game's (relocation bytes masked, every relocation's target
checked). Near misses get their differing instruction rows from fndiff.py. The functions not
written yet are the rest of reverse-engineering/analysis/spel_funcs.tsv (funcs.py), named from
the FUNCTION annotations in src/spel when a prototype has one. Each function's source file is
the src/spel file that defines it (unplaced.cpp excepted), else the one
reverse-engineering/analysis/spel_modules.tsv gives (modules.py). The C library row comes from
libmatch.py against VC 5.0's LIBC.LIB.

It also writes docs/SPELPROGRESS.HTML (every function, searchable and filterable, with its
instruction diff against the game; the layout of wacky-re's and redcat-rrr-re's reports,
template progress.html next to this file) and docs/SPELPROGRESS.SVG (the README's badge).
"""
import base64
import io
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import coff                  # noqa: E402
import fndiff as D           # noqa: E402
import funcs as F            # noqa: E402
import libmatch              # noqa: E402
import modules as MOD        # noqa: E402
import objmatch as M         # noqa: E402
from pe import Image, ROOT   # noqa: E402

PROG = 'spel'
SRC = os.path.join(ROOT, 'src', PROG)
OBJDIR = os.path.join(ROOT, '.work', 'src', PROG)
LIBC = os.path.join(ROOT, '.work', 'toolchains', 'vc50', 'lib', 'LIBC.LIB')
UNPLACED = 'not placed yet'


def run(args, **kw):
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True, **kw).stdout


def module_label(mods, a):
    m, how = mods.get(a, ('?', ''))
    return m if how else UNPLACED


def align_all(img, near, sizes):
    """{address: {'d': rows that differ, 'i': rows, 'r': similarity, 'h': hunks}} for the near misses."""
    import glob
    objs = [coff.Obj(open(p, 'rb').read()) for p in sorted(glob.glob(os.path.join(OBJDIR, '*.obj')))]
    out = {}
    for addr, name in near.items():
        for ob in objs:
            hit = D.find(ob, name)
            if not hit:
                continue
            glen = len(coff.body(img.read(addr, sizes[addr]), 0, sizes[addr])) if addr in sizes else None
            B, A, ops = D.diff(img, ob, *hit, addr, glen)
            d = D.rows(ops)
            eq = sum(i2 - i1 for t, i1, i2, j1, j2 in ops if t == 'equal')
            out[addr] = {'d': d, 'i': eq + d, 'r': round(2.0 * eq / max(1, len(A) + len(B)), 4),
                         'h': D.hunks(B, A, ops)}
            break
    return out


def logo_png():
    """The red A2 road sign from the box cover (docs/media/cover.jpg), as PNG."""
    from PIL import Image as PImage
    buf = io.BytesIO()
    PImage.open(os.path.join(ROOT, 'docs', 'media', 'cover.jpg')).convert('RGB') \
        .crop((212, 50, 472, 238)).resize((177, 128)).save(buf, 'PNG')
    return base64.b64encode(buf.getvalue()).decode()


def write_html(path, allf, order, al, head):
    fs = []
    for a, n, s, sub in allf:
        f = {'a': a, 'n': n, 's': s, 'sub': sub}
        if s == 'near':
            f.update(al.get(a, {'d': 0, 'i': 0, 'r': 0.0, 'h': []}))
        fs.append(f)
    data = json.dumps({'commit': head, 'subsystems': order, 'functions': fs}, separators=(',', ':'))
    t = open(os.path.join(HERE, 'progress.html')).read()
    t = t.replace('/*ICON*/', logo_png()).replace('/*DATA*/', data.replace('</', '<\\/'))
    open(path, 'w').write(t)


def write_svg(path, done, total):
    """The README badge: the A2 sign, spel.dat, and a bar filled to the byte-identical share."""
    pct = 100.0 * done / total
    w = 290 * pct / 100
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        'width="320" height="240" viewBox="0 0 320 240">\n'
        '<rect width="320" height="240" rx="8" fill="#160809"/>\n'
        '<image x="71" y="10" width="177" height="128" xlink:href="data:image/png;base64,%s"/>\n'
        '<text x="160" y="176" text-anchor="middle" font-family="monospace" font-weight="bold" font-size="22" '
        'fill="#eef0f4">spel.dat</text>\n'
        '<clipPath id="fill"><rect x="15" y="196" width="%.2f" height="24"/></clipPath>\n'
        '<rect x="15" y="196" width="290" height="24" fill="#000" stroke="#eef0f4" stroke-width="1.5"/>\n'
        '<text x="160" y="213" text-anchor="middle" font-family="monospace" font-weight="bold" font-size="14" '
        'fill="#eef0f4">%.1f%%</text>\n'
        '<g clip-path="url(#fill)"><rect x="15" y="196" width="290" height="24" fill="#3d6ee0"/>\n'
        '<text x="160" y="213" text-anchor="middle" font-family="monospace" font-weight="bold" font-size="14" '
        'fill="#ffffff">%.1f%%</text></g>\n'
        '</svg>\n') % (logo_png(), w, pct, pct)
    open(path, 'w').write(svg)


def main():
    out = os.path.join(ROOT, 'PROGRESS.md')
    if '--out' in sys.argv:
        out = sys.argv[sys.argv.index('--out') + 1]

    chk = run(['src/check.sh'], timeout=3600)
    done_map = {}                                   # address -> (name, size, matched)
    for name, size, kind, addr in re.findall(
            r'^(\S+)\s+(\d+) bytes  (MATCH at|differs from) (0x[0-9a-f]+)', chk, re.M):
        done_map[int(addr, 16)] = (name, int(size), kind == 'MATCH at')
    if not done_map:
        sys.exit('src/check.sh printed no comparisons:\n' + chk[-2000:])

    img = Image(PROG)
    fl = F.load(PROG)
    sizes = {a: n for a, n, _ in fl}
    mods = MOD.load(PROG)
    where, defs = M.annotations(SRC, PROG)
    written_in = {a: fn for a, (fn, _) in defs.items() if fn.lower().endswith('.cpp')
                  and fn.lower() != 'unplaced.cpp'}
    named = {v: k for k, v in where.items()}
    code_start, lib_start = fl[0][0], fl[-1][0] + fl[-1][1]
    tva, text = img.text
    code_end = tva + len(text.rstrip(b'\0'))

    allf = []
    for a, n, _ in fl:
        sub = written_in.get(a) or module_label(mods, a)
        if a in done_map:
            nm, _, ok = done_map[a]
            allf.append((a, nm, 'match' if ok else 'near', sub))
        else:
            allf.append((a, named.get(a, 'fn_%06x' % a), 'none', sub))
    extra = [a for a in done_map if a not in sizes]
    if extra:
        print('progress.py: annotated functions that are not starts in spel_funcs.tsv: '
              + ' '.join('0x%x' % a for a in extra))

    al = align_all(img, {a: n for a, (n, _, ok) in done_map.items() if not ok and a in sizes}, sizes)
    lib = libmatch.match(PROG, LIBC)
    lib_bytes = sum(f[1] for f in lib)
    lib_end = lib[-1][0] + lib[-1][1] if lib else lib_start

    total = len(allf)
    done = sum(1 for f in allf if f[2] == 'match')
    near = sum(1 for f in allf if f[2] == 'near')
    unwritten = total - done - near
    c_bytes = sum(done_map[a][1] for a, _, s, _ in allf if s == 'match')
    head = run(['git', 'rev-parse', '--short', 'HEAD']).strip()

    def pct(a, b):
        return '%.1f%%' % (100.0 * a / b) if b else '-'

    def num(x):
        return '{:,}'.format(x)

    order = []
    for _, _, _, sub in allf:
        if sub not in order and sub != UNPLACED:
            order.append(sub)
    if any(f[3] == UNPLACED for f in allf):
        order.append(UNPLACED)

    L = []
    w = L.append
    w('# Progress')
    w('')
    w('How much of `spel.dat` (the race program) is rebuilt from C++. Generated by')
    w('`reverse-engineering/tools/msvc/progress.py` (do not edit by hand), measured at commit `%s`.' % head)
    w('')
    w('**%d of the %d functions found in the game\'s own code compile to the original\'s exact bytes (%s).**'
      % (done, total, pct(done, total)))
    w('')
    w('Of spel.dat\'s `.text` (%s bytes), %s bytes (%s) are rebuilt byte for byte or identified as '
      'byte-identical C library code.' % (num(code_end - tva), num(c_bytes + lib_bytes),
                                          pct(c_bytes + lib_bytes, code_end - tva)))
    w('')
    w('A function counts only when our build of `src/spel/` (Visual C++ 5.0, `cl /O2 /ML`) gives')
    w('the same bytes as the game at the same address, with the target of every relocation checked')
    w('too: calls and globals against their annotations, string literals and float constants')
    w('against the game\'s bytes. A near miss compiles but its code still differs: the number is how')
    w('many instructions differ (`reverse-engineering/tools/msvc/fndiff.py`). The function list is')
    w('`reverse-engineering/analysis/spel_funcs.tsv` (`funcs.py`: call targets, code pointers in the')
    w('data, and the 0x90 padding between functions).')
    w('')
    w('To search and filter the functions and see each near miss\'s diff, open')
    w('[`docs/SPELPROGRESS.HTML`](docs/SPELPROGRESS.HTML) in a browser.')
    w('')
    w('## The programs')
    w('')
    w('| Program | Status |')
    w('|---|---|')
    w('| `spel.dat`, the race | %d of %d functions byte-identical (%s) |' % (done, total, pct(done, total)))
    w('| `menu.dat`, the menus | not started; shares `C:\\work\\winlib\\` with spel.dat |')
    w('')
    w('## spel.dat part by part')
    w('')
    w('| Part | Code | Status |')
    w('|---|---|---|')
    w('| The game\'s own code (`C:\\work\\A2racII\\Game\\`, `C:\\work\\winlib\\`, the 3D library, the input '
      'library), `src/spel/` | 0x%06X-0x%06X, %s bytes | %d of %d functions byte-identical (%s), %d near misses, '
      '%d not written yet |' % (code_start, lib_start - 1, num(lib_start - code_start), done, total,
                                pct(done, total), near, unwritten))
    w('| Visual C++ 5.0 C library (`LIBC.LIB`) | 0x%06X-0x%06X | %d functions, %s bytes, identified byte for byte; '
      'linked from the library, nothing to rewrite |' % (lib_start, lib_end - 1, len(lib), num(lib_bytes)))
    w('| Not identified yet, after the game\'s code | inside 0x%06X-0x%06X '
      '| %s bytes |' % (lib_start, code_end - 1, num(code_end - lib_start - lib_bytes)))
    w('')
    w('## The game\'s own code by source file')
    w('')
    w('The objects are linked in alphabetical order of their file names. A function is placed in a')
    w('file when it uses that file\'s `__FILE__` string, or lies between two functions that do;')
    w('`reverse-engineering/analysis/spel_modules.tsv` (`modules.py`). A function written in')
    w('`src/spel/<file>.cpp` is counted in that file (`unplaced.cpp` excepted). The others are')
    w('"%s".' % UNPLACED)
    w('')
    w('| Source file | Functions | Byte-identical | Near misses | Not written | Done |')
    w('|---|---:|---:|---:|---:|---:|')
    for g in order:
        fs = [s for _, _, s, sub in allf if sub == g]
        m_, n_, x_ = fs.count('match'), fs.count('near'), fs.count('none')
        w('| %s | %d | %d | %d | %d | %s |' % (g, len(fs), m_, n_, x_, pct(m_, len(fs))))
    w('| **All** | **%d** | **%d** | **%d** | **%d** | **%s** |' % (total, done, near, unwritten, pct(done, total)))
    w('')
    w('## Every function')
    w('')
    w('In address order. Names without a meaning yet keep their address (`fn_401000`).')
    w('')
    w('| Address | Function | Source file | Status |')
    w('|---|---|---|---|')
    for a, n, s, sub in allf:
        st = {'match': 'byte-identical', 'none': 'not in src/spel yet'}.get(s)
        if s == 'near':
            st = 'near miss, %d instructions differ' % al[a]['d'] if a in al else 'near miss'
        w('| 0x%06X | `%s` | %s | %s |' % (a, n, sub, st))
    w('')
    open(out, 'w').write('\n'.join(L))
    os.makedirs(os.path.join(ROOT, 'docs'), exist_ok=True)
    write_html(os.path.join(ROOT, 'docs', 'SPELPROGRESS.HTML'), allf, order, al, head)
    write_svg(os.path.join(ROOT, 'docs', 'SPELPROGRESS.SVG'), done, total)
    print('%s: %d of %d byte-identical (%s), %d near misses, %d not written'
          % (os.path.relpath(out, ROOT), done, total, pct(done, total), near, unwritten))


if __name__ == '__main__':
    main()

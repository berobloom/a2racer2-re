#!/usr/bin/env python3
"""modules.py: which original source file (.cpp) each function of a game's code came from.

usage: modules.py PROG [--tsv FILE]

The game's asserts and error messages carry __FILE__ ("C:\\work\\A2racII\\Game\\ai_fsm.cpp",
"D3DLIB.CPP"). A function whose code holds the address of such a string as a 32-bit operand
is marked with that file (CONFIRMED for that function, as far as the string is its own and
not an inline function's: .h/.inl/.hpp paths are left out for that reason).
The linker lays each object's code out in one piece, so a function between two marks of the
same file is from that file too (STRONGLY SUPPORTED). Functions between marks of different
files, or before the first and after the last mark, stay unassigned ('?').
Writes 'address<TAB>module<TAB>how' rows: how is 'string' or 'between'.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import Image, ROOT     # noqa: E402
import funcs as F              # noqa: E402

SRC = re.compile(rb'([A-Za-z]:\\[^\0]*?|[A-Za-z0-9_]+)\.(cpp|CPP|Cpp|c|C)\0')


def source_strings(img):
    """{string address: module base name, lower case} for the .cpp paths in .rdata/.data."""
    out = {}
    for name in ('.rdata', '.data'):
        _, va, _, data = img.section(name)
        for m in SRC.finditer(data):
            s = m.group(0)[:-1].decode('latin1')
            start = m.start()
            # the match must begin a string: the byte before it is 0 or the section start
            if start and data[start - 1] != 0:
                continue
            base = re.split(r'[\\/]', s)[-1]
            out[va + start] = base.lower()
    return out


def assign(prog):
    img = Image(prog)
    tva, text = img.text
    fs = F.load(prog)
    strs = source_strings(img)
    mark = {}
    for a, n, _ in fs:
        code = text[a - tva:a - tva + n]
        hits = set()
        for i in range(len(code) - 3):
            v = struct.unpack_from('<I', code, i)[0]
            if v in strs:
                hits.add(strs[v])
        if len(hits) == 1:
            mark[a] = hits.pop()
    out = {}
    addrs = [a for a, _, _ in fs]
    known = [a for a in addrs if a in mark]
    for a in addrs:
        if a in mark:
            out[a] = (mark[a], 'string')
            continue
        lo = max((k for k in known if k < a), default=None)
        hi = min((k for k in known if k > a), default=None)
        if lo is not None and hi is not None and mark[lo] == mark[hi]:
            out[a] = (mark[lo], 'between')
        else:
            out[a] = ('?', '')
    return out


def load(prog):
    out = {}
    path = os.path.join(ROOT, 'reverse-engineering', 'analysis', prog + '_modules.tsv')
    for line in open(path):
        if line.startswith('0x'):
            a, m, how = (line.rstrip('\n').split('\t') + [''])[:3]
            out[int(a, 16)] = (m, how)
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    prog = sys.argv[1]
    mods = assign(prog)
    if '--tsv' in sys.argv:
        with open(sys.argv[sys.argv.index('--tsv') + 1], 'w') as f:
            f.write('# address\tmodule\thow (reverse-engineering/tools/msvc/modules.py %s)\n' % prog)
            for a in sorted(mods):
                f.write('0x%06x\t%s\t%s\n' % (a, mods[a][0], mods[a][1]))
    n = len(mods)
    s = sum(1 for m, h in mods.values() if h == 'string')
    b = sum(1 for m, h in mods.values() if h == 'between')
    print('%s: %d functions, %d marked by a source string, %d between marks, %d unassigned, %d modules'
          % (prog, n, s, b, n - s - b, len({m for m, h in mods.values() if h})))


if __name__ == '__main__':
    sys.exit(main())

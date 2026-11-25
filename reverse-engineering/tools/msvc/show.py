#!/usr/bin/env python3
"""show.py: disassemble functions of a game's code, with strings, floats and known names resolved.

usage: show.py PROG 0xADDR [0xADDR...]      the function(s) starting there (spel_funcs.tsv)
       show.py PROG --module NAME.cpp       every function the module map gives that file

Operands that point at a string print it; at a float in .rdata, its value; at an annotated
function or global (src/PROG), its name.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capstone              # noqa: E402
import coff                  # noqa: E402
import funcs as F            # noqa: E402
import modules as MOD        # noqa: E402
import objmatch as M         # noqa: E402
from pe import Image, ROOT   # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def note(img, names, v):
    if v in names:
        return names[v]
    for sec in ('.rdata', '.data'):
        _, va, vs, data = img.section(sec)
        if va <= v < va + len(data):
            o = v - va
            s = data[o:o + 80].split(b'\0')[0]
            if len(s) >= 3 and all(32 <= c < 127 or c in (9, 10, 13) for c in s):
                return repr(s.decode('latin1'))
            if sec == '.rdata' and o + 4 <= len(data):
                f = struct.unpack_from('<f', data, o)[0]
                d = struct.unpack_from('<d', data, o)[0] if o + 8 <= len(data) else None
                if 1e-6 < abs(f) < 1e7 or f == 0:
                    return 'float %g' % f + (' / double %g' % d if d and 1e-6 < abs(d) < 1e9 else '')
    return None


def show(img, names, mods, a, n):
    code = coff.body(img.read(a, n), 0, n)
    m = mods.get(a, ('?', ''))
    print('== 0x%06x %s  %d bytes  [%s %s]' % (a, names.get(a, ''), len(code), m[0], m[1]))
    for i in md.disasm(code, a):
        extra = []
        for h in re.findall(r'0x[0-9a-f]{6,}', i.op_str):
            t = note(img, names, int(h, 16))
            if t:
                extra.append(t)
        print('  %06x  %-22s %-6s %-36s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str,
                                              ('; ' + ' '.join(extra)) if extra else ''))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    prog = sys.argv[1]
    img = Image(prog)
    where, _ = M.annotations(os.path.join(ROOT, 'src', prog), prog)
    names = {v: k for k, v in where.items()}
    fl = {a: n for a, n, _ in F.load(prog)}
    mods = MOD.load(prog)
    if sys.argv[2] == '--module':
        addrs = sorted(a for a, (m, h) in mods.items() if m == sys.argv[3] and h)
    else:
        addrs = [int(x, 16) for x in sys.argv[2:]]
    for a in addrs:
        show(img, names, mods, a, fl.get(a, 64))


if __name__ == '__main__':
    sys.exit(main())

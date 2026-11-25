#!/usr/bin/env python3
"""funcs.py: the function starts of a game's own code (everything in .text before the C library).

usage: funcs.py PROG [--tsv FILE]

A start is any of:
  call    the target of a `call rel32` anywhere in .text
  ptr     a 32-bit value in .rdata/.data that points at a 16-byte-aligned address in the code
          (vtables, function-pointer tables, callbacks)
  pad     a 16-byte-aligned address right after 0x90 padding that follows a ret or a jmp:
          VC 5.0 /O2 pads only between functions
  entry   the program's entry point, and the first byte of .text
Each function runs to the next start. Writes 'address<TAB>size<TAB>evidence' rows.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pe import Image, ROOT     # noqa: E402
import libmatch                # noqa: E402

LIBS = os.path.join(ROOT, '.work', 'toolchains', 'vc50', 'lib')


def lib_start(prog):
    found = libmatch.match(prog, os.path.join(LIBS, 'LIBC.LIB'))
    return found[0][0] if found else None


def starts(prog, end=None):
    img = Image(prog)
    tva, text = img.text
    end = end or lib_start(prog)
    ev = {}

    def add(a, why):
        if tva <= a < end:
            ev.setdefault(a, set()).add(why)

    add(tva, 'entry')
    add(img.entry, 'entry')
    for m in re.finditer(b'\xe8', text[:end - tva]):
        i = m.start()
        if i + 5 <= len(text):
            add(tva + i + 5 + struct.unpack_from('<i', text, i + 1)[0], 'call')
    for name in ('.rdata', '.data'):
        try:
            _, va, _, data = img.section(name)
        except KeyError:
            continue
        for i in range(0, len(data) - 3, 4):
            v = struct.unpack_from('<I', data, i)[0]
            if v % 16 == 0:
                add(v, 'ptr')
    for i in range(16, end - tva, 16):
        if text[i - 1] != 0x90:
            continue
        j = i - 1
        while j > i - 16 and text[j] == 0x90:
            j -= 1
        # the byte before the padding ends a ret (c3), ret n (c2 xx xx) or jmp (eb xx / e9 rel32)
        if text[j] == 0xc3 or (j >= 2 and text[j - 2] == 0xc2) or (j >= 1 and text[j - 1] == 0xeb) \
                or (j >= 4 and text[j - 4] == 0xe9) or (j >= 5 and text[j - 5:j - 3] == b'\xff\x25'):
            add(tva + i, 'pad')
    out = sorted(ev)
    return [(a, (out[k + 1] if k + 1 < len(out) else end) - a, ','.join(sorted(ev[a])))
            for k, a in enumerate(out)]


def load(prog):
    """[(address, size, evidence)] from reverse-engineering/analysis/PROG_funcs.tsv."""
    out = []
    for line in open(os.path.join(ROOT, 'reverse-engineering', 'analysis', prog + '_funcs.tsv')):
        if line.startswith('0x'):
            a, n, e = line.rstrip('\n').split('\t')[:3]
            out.append((int(a, 16), int(n), e))
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    prog = sys.argv[1]
    fs = starts(prog)
    if '--tsv' in sys.argv:
        with open(sys.argv[sys.argv.index('--tsv') + 1], 'w') as f:
            f.write('# address\tsize\tevidence (reverse-engineering/tools/msvc/funcs.py %s)\n' % prog)
            for a, n, e in fs:
                f.write('0x%06x\t%d\t%s\n' % (a, n, e))
    print('%s: %d functions, 0x%06x-0x%06x' % (prog, len(fs), fs[0][0], fs[-1][0] + fs[-1][1]))


if __name__ == '__main__':
    sys.exit(main())

#!/usr/bin/env python3
"""libmatch.py: find the modules of a COFF library (VC 5.0's LIBC.LIB, ...) in a game's .text.

usage: libmatch.py PROG LIB [-v] [--tsv FILE]

A library module is linked whole, so each code section of a member is searched for as one
block: the bytes must be equal everywhere except the 4 bytes of each relocation. A match
must also be unique in .text, and blocks under 8 bytes are left out (too short to place).
Prints each match and, at the end, the count and bytes. --tsv writes
'address<TAB>size<TAB>member<TAB>function names' rows.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coff                 # noqa: E402
from pe import Image        # noqa: E402

MIN = 8


def blocks(libpath):
    """Every code section of every member: (member, section name, bytes, reloc mask, function names)."""
    for member, data in coff.lib_members(open(libpath, 'rb').read()):
        try:
            ob = coff.Obj(data)
        except ValueError:
            continue
        funcs = ob.functions()
        for k, sec in enumerate(ob.sections):
            if not sec or not sec.code or len(sec.data) < MIN:
                continue
            mask = bytearray(len(sec.data))
            for off, _si, typ in sec.relocs:
                for j in range(4):
                    if off + j < len(mask):
                        mask[off + j] = 1
            names = [f[0] for f in funcs if f[1] == k]
            yield member, sec.name, bytes(sec.data), bytes(mask), names


def anchor(buf, mask):
    """The longest run of unmasked bytes (where to search from) as (offset, length)."""
    best, start = (0, 0), None
    for i in range(len(buf) + 1):
        if i < len(buf) and not mask[i]:
            start = i if start is None else start
        elif start is not None:
            if i - start > best[1]:
                best = (start, i - start)
            start = None
    return best


def find(text, buf, mask):
    off, n = anchor(buf, mask)
    if n < 6:
        return []
    key = buf[off:off + min(n, 32)]
    hits = []
    for m in re.finditer(re.escape(key), text):
        s = m.start() - off
        if s < 0 or s + len(buf) > len(text):
            continue
        cand = text[s:s + len(buf)]
        if all(mask[i] or cand[i] == buf[i] for i in range(len(buf))):
            hits.append(s)
    return hits


def match(prog, libpath):
    img = Image(prog)
    tva, text = img.text
    found = []
    for member, sname, buf, mask, names in blocks(libpath):
        hits = find(text, buf, mask)
        if len(hits) == 1:
            found.append((tva + hits[0], len(buf), member, names))
    found.sort()
    # the libraries are linked after the game's own objects, as one block: a lone hit far before
    # the next one is a short game function that happens to equal a library one (spel.dat
    # 0x448d70 = stdexcpt.obj's 32-byte exception::exception)
    while len(found) > 1 and found[1][0] - found[0][0] > 0x4000:
        found.pop(0)
    # two members can claim overlapping bytes (one a copy of the other): keep the first
    out, last = [], 0
    for f in found:
        if f[0] >= last:
            out.append(f)
            last = f[0] + f[1]
    return out


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    prog, lib = sys.argv[1], sys.argv[2]
    found = match(prog, lib)
    for a, n, member, names in found:
        if '-v' in sys.argv:
            print('0x%06x %6d  %-40s %s' % (a, n, os.path.basename(member.replace('\\', '/')), ' '.join(names)))
    if '--tsv' in sys.argv:
        with open(sys.argv[sys.argv.index('--tsv') + 1], 'w') as f:
            for a, n, member, names in found:
                f.write('0x%06x\t%d\t%s\t%s\n' % (a, n, os.path.basename(member.replace('\\', '/')), ' '.join(names)))
    print('%s: found %d (%d bytes)%s' % (os.path.basename(lib), len(found), sum(f[1] for f in found),
          ', 0x%06x-0x%06x' % (found[0][0], found[-1][0] + found[-1][1]) if found else ''))


if __name__ == '__main__':
    sys.exit(main())

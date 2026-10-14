#!/usr/bin/env python3
"""Disassemble spel.dat / menu.dat from <start> for <len> bytes, annotating
absolute operands with the string or float stored there (session 13).

Usage: adis.py <exe> <start_hex> <len_hex>"""
import re
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs

d = open(sys.argv[1], 'rb').read()
start, length = int(sys.argv[2], 16), int(sys.argv[3], 16)
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for k in range(n):
    o = pe + 24 + opt + 40 * k
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
    secs.append((0x400000 + va, vs, ro, rs))


def off(v):
    for va, sz, ro, rs in secs:
        if va <= v < va + max(sz, rs) and v - va < rs:
            return v - va + ro
    return None


md = Cs(CS_ARCH_X86, CS_MODE_32)
o = off(start)
for ins in md.disasm(d[o:o + length], start):
    note = ''
    m = re.search(r'0x4[789a][0-9a-f]{4}\b', ins.op_str)
    if m:
        v = int(m.group(), 16)
        oo = off(v)
        if oo is not None and v >= 0x47f000:
            s = d[oo:oo + 60].split(b'\0')[0]
            if len(s) > 3 and all(32 <= c < 127 or c in (9, 10) for c in s):
                note = repr(s.decode('latin1'))
            else:
                note = 'f=%g' % struct.unpack_from('<f', d, oo)[0]
    if ins.mnemonic == 'nop':
        continue
    print('%08x  %-6s %s %s' % (ins.address, ins.mnemonic, ins.op_str, note))

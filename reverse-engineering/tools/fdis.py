#!/usr/bin/env python3
"""Linear disassembly of spel.dat from an address up to the first `ret` (or a
byte count), with every absolute memory operand annotated by the float/double
(and dword) stored there.  Useful for x87-heavy code.

Usage: fdis.py <spel.dat> <start_hex> [max_bytes_hex]"""
import re
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs

path, start = sys.argv[1], int(sys.argv[2], 16)
limit = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x2000
d = open(path, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
opt = struct.unpack_from('<H', d, pe + 20)[0]
secs = []
for i in range(n):
    o = pe + 24 + opt + 40 * i
    vs, va, rs, ro = struct.unpack_from('<IIII', d, o + 8)
    secs.append((0x400000 + va, max(vs, rs), ro, rs))


def off(v):
    for va, sz, ro, rs in secs:
        if va <= v < va + sz:
            return v - va + ro if v - va < rs else None
    return None


md = Cs(CS_ARCH_X86, CS_MODE_32)
o = off(start)
code = d[o:o + limit]
for ins in md.disasm(code, start):
    note = ''
    for m in re.finditer(r'\[(0x[0-9a-f]{6})\]', ins.op_str):
        a = int(m.group(1), 16)
        p = off(a)
        if p is not None and p + 8 <= len(d):
            f = struct.unpack_from('<f', d, p)[0]
            dv = struct.unpack_from('<d', d, p)[0]
            w = struct.unpack_from('<I', d, p)[0]
            if 'qword' in ins.op_str:
                note = f'  ; {a:#x} = {dv:g} (double)'
            else:
                note = f'  ; {a:#x} = {f:g} (dword {w:#x})'
        elif p is None:
            note = f'  ; {a:#x} (bss)'
    print(f'{ins.address:08x}  {ins.mnemonic:6s} {ins.op_str}{note}')
    if ins.mnemonic == 'ret' and len(sys.argv) <= 3:
        break

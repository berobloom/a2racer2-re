#!/usr/bin/env python3
"""Every instruction in spel.dat's .text that names an absolute address.

Usage: xref.py <spel.dat> 0x<addr> [0x<addr> ...]
"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
import peparse as p
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

P = p.parse(sys.argv[1]); d = P['data']; ib = P['imagebase']
t = [s for s in P['sections'] if s[0] == '.text'][0]
md = Cs(CS_ARCH_X86, CS_MODE_32)
code = d[t[3]:t[3] + t[4]]
for a in sys.argv[2:]:
    v = int(a, 16)
    print(f'== {v:#x}')
    pat = struct.pack('<I', v); i = code.find(pat)
    while i >= 0:
        for back in range(1, 8):
            st = i - back
            ins = list(md.disasm(code[st:st + 16], ib + t[1] + st, 1))
            if ins and f'{v:#x}' in ins[0].op_str and ins[0].size >= back + 4:
                print(f'  {ins[0].address:#x}  {ins[0].mnemonic} {ins[0].op_str}')
                break
        i = code.find(pat, i + 1)

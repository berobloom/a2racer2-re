#!/usr/bin/env python3
"""Disassemble an address range with string annotations. Usage: dis.py <addr> [nbytes]"""
import sys, re, struct
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

BASE = "/tmp/opencode/a2/spelana/"
IMG = 0x400000
pe = pefile.PE(BASE + "spel.dat")
md = Cs(CS_ARCH_X86, CS_MODE_32)
text = pe.sections[0]
code = text.get_data()
text_va = text.VirtualAddress + IMG

strings = {}
for line in open(BASE + "izz.txt", encoding="utf-8", errors="replace"):
    m = re.match(r'^(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\d+)\s+(\d+)\s+(\S+)\s+(.*)$', line)
    if m:
        va = int(m.group(1), 16)
        if va < IMG:
            va += IMG
        strings[va] = m.group(6)

funcs = []
with open(BASE + "funcs2.txt") as f:
    for line in f:
        m = re.match(r'^(0x[0-9a-f]+)\s+(\d+)', line)
        if m:
            funcs.append((int(m.group(1), 16), int(m.group(2))))
def func_of(va):
    for s, sz in funcs:
        if s <= va < s + sz:
            return s
    return None

addr = int(sys.argv[1], 16)
n = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x400
off = addr - text_va
for ins in md.disasm(code[off:off+n], addr):
    s = f"{ins.address:08x}  {ins.mnemonic:7s} {ins.op_str}"
    if ins.mnemonic in ('call', 'jmp') and ins.op_str.startswith('0x4'):
        v = int(ins.op_str, 16)
        tag = ''
        if v in strings:
            tag = f'  ; "{strings[v][:64]}"'
        else:
            fva = func_of(v)
            if fva:
                tag = f"  ; -> {hex(fva)}"
        s += tag
    elif ins.mnemonic == 'push' and ins.op_str.startswith('0x4'):
        v = int(ins.op_str, 16)
        if v in strings:
            s += f'   ; "{strings[v][:64]}"'
    print(s)

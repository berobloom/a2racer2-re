#!/usr/bin/env python3
"""Find call sites in a PE (spel.dat / menu.dat) and show their pushed arguments.

  pecalls.py imports <exe> <ImportName>...      call sites of imported functions
  pecalls.py args <exe> <target> [...]          calls to target VAs, with the last
                                                N pushes (NARGS env, default 3);
                                                immediates that point at strings
                                                are shown as the string

Session 12: used to list every Game.ini read/write in spel.dat and menu.dat
(see "What spel.dat writes back to Game.ini" in research/discoveries.md).
"""
import os
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs


class PE:
    def __init__(self, path):
        self.d = open(path, 'rb').read()
        d = self.d
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        ns = struct.unpack_from('<H', d, pe + 6)[0]
        osz = struct.unpack_from('<H', d, pe + 20)[0]
        self.base = struct.unpack_from('<I', d, pe + 24 + 28)[0]
        self.imp_rva = struct.unpack_from('<I', d, pe + 24 + 104)[0]
        self.secs = []
        for i in range(ns):
            o = pe + 24 + osz + 40 * i
            vs, va, rs, ro = struct.unpack_from('<4I', d, o + 8)
            name = d[o:o + 8].rstrip(b'\0').decode()
            self.secs.append((name, self.base + va, ro, max(vs, rs) if name != '.text' else rs))
        self.text = [s for s in self.secs if s[0] == '.text'][0]

    def va2off(self, va):
        for n, v, o, sz in self.secs:
            if v <= va < v + sz:
                return va - v + o
        return None

    def string(self, va):
        o = self.va2off(va)
        if o is None or o >= len(self.d):
            return None
        s = self.d[o:o + 80].split(b'\0')[0]
        if s and all(32 <= c < 127 or c in (9, 10) for c in s):
            return s.decode()
        return None

    def imports(self):
        d = self.d
        imp = self.va2off(self.base + self.imp_rva)
        out = {}
        i = 0
        while True:
            oft, ts, fc, name, ft = struct.unpack_from('<5I', d, imp + 20 * i)
            i += 1
            if not name:
                break
            k = 0
            while True:
                t = struct.unpack_from('<I', d, self.va2off(self.base + (oft or ft)) + 4 * k)[0]
                if not t:
                    break
                if not t & 0x80000000:
                    nm = d[self.va2off(self.base + t) + 2:].split(b'\0')[0].decode()
                    out[nm] = self.base + ft + 4 * k
                k += 1
        return out


def cmd_imports(pe, names):
    imp = pe.imports()
    n, va, off, size = pe.text
    for name in names:
        slot = imp.get(name)
        if slot is None:
            print(name, 'not imported')
            continue
        sites = []
        for pat, tag in ((b'\xff\x15', ''), (b'\x8b\x35', '(mov esi)'), (b'\x8b\x3d', '(mov edi)'),
                         (b'\x8b\x1d', '(mov ebx)'), (b'\x8b\x2d', '(mov ebp)')):
            p = pat + struct.pack('<I', slot)
            j = pe.d.find(p, off, off + size)
            while j >= 0:
                sites.append(hex(j - off + va) + tag)
                j = pe.d.find(p, j + 1, off + size)
        print(name, hex(slot), sites)


def cmd_args(pe, targets):
    nargs = int(os.environ.get('NARGS', '3'))
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    n, va, off, size = pe.text
    d = pe.d
    for o in range(off, off + size - 5):
        if d[o] != 0xe8:
            continue
        site = o - off + va
        t = (site + 5 + struct.unpack_from('<i', d, o + 1)[0]) & 0xffffffff
        if t not in targets:
            continue
        ins = None
        for back in range(80, 10, -1):
            cand = list(md.disasm(d[o - back:o + 5], site - back))
            if cand and cand[-1].address == site and cand[-1].mnemonic == 'call':
                ins = cand
                break
        if not ins:
            print(hex(site), hex(t), '?')
            continue
        pushes = [i for i in ins[:-1] if i.mnemonic == 'push']
        args = []
        for p in reversed(pushes[-nargs:]):
            try:
                v = int(p.op_str, 16)
                args.append(pe.string(v) or p.op_str)
            except ValueError:
                args.append(p.op_str)
        print(hex(site), hex(t), args)


if __name__ == '__main__':
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    pe = PE(sys.argv[2])
    if sys.argv[1] == 'imports':
        cmd_imports(pe, sys.argv[3:])
    else:
        cmd_args(pe, [int(x, 16) for x in sys.argv[3:]])

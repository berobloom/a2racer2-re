#!/usr/bin/env python3
"""pe.py: the original programs' load images, by virtual address.

    Image('spel')  or  Image('menu')     .work/game/spel.dat, .work/game/menu.dat
    img.read(va, n)                      bytes at a virtual address (zeros past raw data)
    img.text                             (va, bytes) of .text
"""
import os
import struct

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
GAME = os.path.join(ROOT, '.work', 'game')
PROGRAMS = {'spel': 'spel.dat', 'menu': 'menu.dat'}


class Image:
    def __init__(self, prog):
        self.prog = prog
        self.path = os.path.join(GAME, PROGRAMS.get(prog, prog))
        b = open(self.path, 'rb').read()
        self.raw = b
        pe = struct.unpack_from('<I', b, 0x3c)[0]
        nsec = struct.unpack_from('<H', b, pe + 6)[0]
        optsz = struct.unpack_from('<H', b, pe + 20)[0]
        self.base = struct.unpack_from('<I', b, pe + 24 + 28)[0]
        self.entry = self.base + struct.unpack_from('<I', b, pe + 24 + 16)[0]
        self.sections = []
        for i in range(nsec):
            o = pe + 24 + optsz + 40 * i
            name = b[o:o + 8].rstrip(b'\0').decode('latin1')
            vsize, va, rsize, rptr = struct.unpack_from('<IIII', b, o + 8)
            self.sections.append((name, self.base + va, vsize, b[rptr:rptr + min(rsize, vsize)]))

    def section(self, name):
        for s in self.sections:
            if s[0] == name:
                return s
        raise KeyError(name)

    @property
    def text(self):
        _, va, _, data = self.section('.text')
        return va, data

    def read(self, va, n):
        for _, sva, vsize, data in self.sections:
            if sva <= va < sva + vsize:
                o = va - sva
                chunk = data[o:o + n]
                return chunk + bytes(max(0, min(n, sva + vsize - va) - len(chunk)))
        return None

    def u32(self, va):
        b = self.read(va, 4)
        return struct.unpack('<I', b)[0] if b and len(b) == 4 else None

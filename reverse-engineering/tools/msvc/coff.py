#!/usr/bin/env python3
"""coff.py: read MSVC COFF object files (.obj) and the members of COFF libraries (.lib).

    Obj(bytes)            sections, symbols, relocations of one object
    lib_members(bytes)    [(member name, object bytes)] of an archive ('!<arch>')
    Obj.functions()       [(name, section index, offset, end)] of each code symbol

A function's end is the next function symbol in its section, or the section's end. Trailing
0x90 padding (the compiler aligns functions to 16 in /O2 output) is part of that span;
body() leaves it out.
"""
import struct
import sys

REL_DIR32, REL_DIR32NB, REL_REL32 = 0x06, 0x07, 0x14
IMAGE_SCN_CNT_CODE = 0x20
IMAGE_SCN_LNK_COMDAT = 0x1000


class Section:
    def __init__(self, name, data, relocs, chars):
        self.name, self.data, self.relocs, self.chars = name, data, relocs, chars

    @property
    def code(self):
        return bool(self.chars & IMAGE_SCN_CNT_CODE)


class Symbol:
    def __init__(self, index, name, value, secnum, typ, klass, aux):
        self.index, self.name, self.value, self.secnum = index, name, value, secnum
        self.type, self.klass, self.aux = typ, klass, aux

    @property
    def is_function(self):
        return (self.type >> 4) == 2


class Obj:
    def __init__(self, b):
        (self.machine, nsec, _ts, symptr, nsym, optsz, _ch) = struct.unpack_from('<HHIIIHH', b, 0)
        if self.machine != 0x14c:
            raise ValueError('not an i386 COFF object (machine 0x%x)' % self.machine)
        strtab = symptr + 18 * nsym
        self._strtab = b[strtab:]

        def sname(raw):
            if raw[:4] == b'\0\0\0\0':
                off = struct.unpack_from('<I', raw, 4)[0]
                return self._strtab[off:self._strtab.index(b'\0', off)].decode('latin1')
            return raw.rstrip(b'\0').decode('latin1')

        self.sections = [None]                       # COFF section numbers are 1-based
        for i in range(nsec):
            o = 20 + optsz + 40 * i
            raw = b[o:o + 8]
            name = raw.rstrip(b'\0').decode('latin1')
            if name.startswith('/'):
                off = int(name[1:])
                name = self._strtab[off:self._strtab.index(b'\0', off)].decode('latin1')
            (_vs, _va, size, dptr, rptr, _lp, nrel, _nl, chars) = struct.unpack_from('<IIIIIIHHI', b, o + 8)
            data = b[dptr:dptr + size] if dptr else bytes(size)
            relocs = [struct.unpack_from('<IIH', b, rptr + 10 * k) for k in range(nrel)]
            self.sections.append(Section(name, data, relocs, chars))
        self.symbols = {}
        i = 0
        while i < nsym:
            o = symptr + 18 * i
            raw = b[o:o + 8]
            value, secnum, typ, klass, naux = struct.unpack_from('<IhHBB', b, o + 8)
            aux = b[o + 18:o + 18 + 18 * naux]
            self.symbols[i] = Symbol(i, sname(raw), value, secnum, typ, klass, aux)
            i += 1 + naux

    def functions(self):
        """[(name, secnum, start, end)] for every function symbol defined in a code section."""
        by_sec = {}
        for s in self.symbols.values():
            if s.is_function and s.secnum > 0 and self.sections[s.secnum].code:
                by_sec.setdefault(s.secnum, []).append(s)
        out = []
        for sec, syms in by_sec.items():
            syms.sort(key=lambda s: s.value)
            for k, s in enumerate(syms):
                end = syms[k + 1].value if k + 1 < len(syms) else len(self.sections[sec].data)
                out.append((s.name, sec, s.value, end))
        return sorted(out, key=lambda f: (f[1], f[2]))

    def relocs_in(self, secnum, start, end):
        """[(offset in section, type, Symbol)] of the relocations inside [start, end)."""
        return [(off, typ, self.symbols[si]) for off, si, typ in self.sections[secnum].relocs
                if start <= off < end]


def body(data, start, end):
    """The function's bytes without the 0x90 alignment padding after it."""
    e = end
    while e > start and data[e - 1] == 0x90 and (e - start) > 1:
        e -= 1
    # keep a lone nop that is the function's own last instruction only if nothing was padded:
    # padding fills up to a 16-byte boundary, so a function never ends in padding of 16 or more
    return data[start:e]


def lib_members(b):
    """[(name, bytes)] of the object members of a COFF archive (the linker members are skipped)."""
    if b[:8] != b'!<arch>\n':
        raise ValueError('not a COFF library')
    out, o, longnames = [], 8, b''
    while o + 60 <= len(b):
        hdr = b[o:o + 60]
        name = hdr[:16].decode('latin1').rstrip()
        size = int(hdr[48:58].decode('latin1').strip())
        data = b[o + 60:o + 60 + size]
        if name == '/':
            pass                                     # the two linker members
        elif name == '//':
            longnames = data
        else:
            if name.startswith('/') and name[1:].isdigit():
                k = int(name[1:])
                end = longnames.find(b'\0', k)
                name = longnames[k:end if end >= 0 else None].decode('latin1').rstrip('\n/')
            else:
                name = name.rstrip('/')
            out.append((name, data))
        o += 60 + size + (size & 1)
    return out


if __name__ == '__main__':
    ob = Obj(open(sys.argv[1], 'rb').read())
    for name, sec, start, end in ob.functions():
        print('%-40s sec %d  0x%04x-0x%04x  %d relocs' % (name, sec, start, end,
                                                       len(ob.relocs_in(sec, start, end))))

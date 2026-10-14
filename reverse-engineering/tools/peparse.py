#!/usr/bin/env python3
"""Minimal PE32 analyzer for A2 Racer II recon (stdlib only).

Usage: peparse.py <file> [--imports|--exports|--sections|--resources|--strings|--all]
"""
import struct, sys, os

def u16(d, o): return struct.unpack_from('<H', d, o)[0]
def u32(d, o): return struct.unpack_from('<I', d, o)[0]

def rva_to_off(sections, rva):
    for name, va, vs, raw, rs in sections:
        if raw and va <= rva < va + vs:
            return raw + (rva - va)
    return None

def parse(fn):
    d = open(fn, 'rb').read()
    if d[:2] != b'MZ':
        sys.exit(f'{fn}: not a DOS/PE file')
    e_lfanew = u32(d, 0x3C)
    if d[e_lfanew:e_lfanew+4] != b'PE\0\0':
        sys.exit(f'{fn}: no PE signature')
    machine = u16(d, e_lfanew+4)
    nsect = u16(d, e_lfanew+6)
    timestamp = u32(d, e_lfanew+8)
    opt = e_lfanew + 24
    sizeof_opt = u16(d, e_lfanew+20)
    magic = u16(d, opt)
    is_pe32plus = (magic == 0x20b)
    entry = u32(d, opt+16)
    imagebase = u32(d, opt+28)
    sectalign = u32(d, opt+32)
    filealign = u32(d, opt+36)
    osver = (u32(d, opt+40), u32(d, opt+44))
    subsys = u16(d, opt+68)
    dllchar = u16(d, opt+70)
    numrva = u32(d, opt+92)
    datadir = opt + 96
    dds = []
    for i in range(16):
        rva, size = u32(d, datadir+i*8), u32(d, datadir+i*8+4)
        dds.append((rva, size))
    sections = []
    shoff = opt + sizeof_opt
    for i in range(nsect):
        o = shoff + i*40
        name = d[o:o+8].rstrip(b'\0').decode('latin1')
        vs, va, raw, rs = u32(d,o+8), u32(d,o+12), u32(d,o+20), u32(d,o+16)
        sections.append((name, va, vs, raw, rs))
    return dict(data=d, machine=machine, nsect=nsect, timestamp=timestamp,
                magic=magic, entry=entry, imagebase=imagebase,
                sectalign=sectalign, filealign=filealign, osver=osver,
                subsys=subsys, dllchar=dllchar, dds=dds, sections=sections,
                is_pe32plus=is_pe32plus)

def show_head(p):
    mach = {0x14c:'i386', 0x14d:'i860', 0x162:'MIPS', 0x166:'MIPS16', 0x168:'MIPS FPU',
            0x1c0:'ARM', 0x1f0:'PPC', 0x200:'IA64', 0x8664:'AMD64'}.get(p['machine'], hex(p['machine']))
    subsys = {2:'GUI', 3:'Console', 1:'Native'}.get(p['subsys'], str(p['subsys']))
    print(f"Machine: {mach} ({'PE32+' if p['is_pe32plus'] else 'PE32'})")
    print(f"Sections: {p['nsect']}  Subsystem: {subsys}")
    print(f"Timestamp: 0x{p['timestamp']:08X}  EntryPoint RVA: 0x{p['entry']:08X}  ImageBase: 0x{p['imagebase']:08X}")
    print(f"OS version: {p['osver'][0]}.{p['osver'][1]}  SectionAlign: 0x{p['sectalign']:x}  FileAlign: 0x{p['filealign']:x}")
    names = ['Export','Import','Resource','Exception','Security','Reloc','Debug','Arch','GlobalPtr','TLS','LoadCfg','BoundImport','IAT','DelayImport','CLR','Reserved']
    for i,(rva,size) in enumerate(p['dds']):
        if rva:
            print(f"  DataDir[{i:2d}] {names[i]:11s} rva=0x{rva:08X} size=0x{size:X}")
    print("Sections:")
    for name, va, vs, raw, rs in p['sections']:
        print(f"  {name:<8} VA=0x{va:08X} VSize=0x{vs:06X} ROff=0x{raw:06X} RSize=0x{rs:06X}")

def show_imports(p):
    d = p['data']; secs = p['sections']
    rva, size = p['dds'][1]
    if not rva: print("(no imports)"); return
    off = rva_to_off(secs, rva)
    if off is None:
        print(f"  (import dir rva 0x{rva:X} not mapped to a section)"); return
    i = 0
    while True:
        e = off + i*20
        oft, ts, fwd, name_rva, first_thunk = u32(d,e), u32(d,e+4), u32(d,e+8), u32(d,e+12), u32(d,e+16)
        if not name_rva and not first_thunk: break
        no = rva_to_off(secs, name_rva)
        dll = d[no:d.index(b'\0', no)].decode('latin1')
        thunk = oft or first_thunk
        to = rva_to_off(secs, thunk)
        funcs = []
        j = 0
        while True:
            f = u32(d, to+j*4)
            if f == 0: break
            if f & 0x80000000:
                funcs.append(f"ord#{f & 0xFFFF}")
            else:
                fo = rva_to_off(secs, f)
                if fo is None: break
                fn = d[fo+2:d.index(b'\0', fo+2)].decode('latin1')
                funcs.append(fn)
            j += 1
        print(f"  {dll}: {', '.join(funcs)}")
        i += 1

def show_exports(p):
    d = p['data']; secs = p['sections']
    rva, size = p['dds'][0]
    if not rva: print("(no exports)"); return
    off = rva_to_off(secs, rva)
    n_funcs = u32(d, off+20)
    name_rva = u32(d, off+12)
    no = rva_to_off(secs, name_rva)
    if no is None:
        # some DLLs have empty name
        print(f"  <noname> exports={n_funcs}")
        return
    libname = d[no:d.index(b'\0', no)].decode('latin1')
    print(f"  Library: {libname}  exports={n_funcs}")

def show_resources(p):
    d = p['data']; secs = p['sections']
    rva, size = p['dds'][2]
    if not rva: print("(no resources)"); return
    types = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',7:'FONTDIR',
             8:'FONT',9:'ACCEL',10:'RCDATA',11:'MESSAGETABLE',12:'GROUP_CURSOR',14:'GROUP_ICON',16:'VERSION',24:'MANIFEST'}
    def walk(off, base, depth=0):
        nr = u16(d, off+12); nn = u16(d, off+14)
        for i in range(nr+nn):
            e = off + 16 + i*8
            name = u32(d, e); kid = u32(d, e+4)
            if name & 0x80000000: nm = d[rva_to_off(secs, name & 0x7FFFFFFF)+2:][:32].decode('latin1','replace')
            else: nm = types.get(name, str(name)) if depth == 0 else str(name)
            if kid & 0x80000000:
                ko = rva_to_off(secs, kid & 0x7FFFFFFF)
                if ko is not None:
                    walk(ko, base, depth+1)
            else:
                o = rva_to_off(secs, kid)
                if o is None: continue
                ro = u32(d, o); sz = u32(d, o+4)
                lang = u16(d, o+14)
                print(f"  {'  '*depth}{nm} lang=0x{lang:04x} size={sz}")
                if depth == 0 and (nm == 'VERSION' or nm == 16):
                    pass
    walk(rva_to_off(secs, rva), rva)

def show_compiler(p):
    d = p['data']; secs = p['sections']
    hints = []
    for m in (b'Borland', b'Microsoft', b'MSVC', b'Watcom', b'GCC', b'MinGW', b'Delphi', b'Visual C'):
        if m in d: hints.append(m.decode())
    if hints:
        print(f"Compiler strings in binary: {set(hints)}")
    else:
        print("No compiler strings found in binary")

def show_strings(p, n=120, minlen=6):
    d = p['data']
    out, cur = [], bytearray()
    for b in d:
        if 32 <= b < 127 or b == 9:
            cur.append(b)
        else:
            if len(cur) >= minlen:
                out.append(cur.decode('latin1'))
            cur = bytearray()
    print(f"ASCII strings ({len(out)} total, showing first {n}):")
    for s in out[:n]:
        print("  ", s)

def main():
    fn = sys.argv[1]
    opts = sys.argv[2:]
    if not opts: opts = ['--all']
    p = parse(fn)
    show_head(p)
    if '--imports' in opts or '--all' in opts:
        print("Imports:"); show_imports(p)
    if '--exports' in opts or '--all' in opts:
        print("Exports:"); show_exports(p)
    if '--resources' in opts or '--all' in opts:
        print("Resources:"); show_resources(p)
    if '--strings' in opts:
        show_strings(p)
    if '--compiler' in opts or '--all' in opts:
        show_compiler(p)

if __name__ == '__main__':
    main()

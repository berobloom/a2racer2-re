#!/usr/bin/env python3
"""spel.dat call-graph harness (capstone + pefile, no deps beyond venv).

Usage:
  analyze.py callers <addr>
  analyze.py callees <addr>
  analyze.py chain <addr>          # forward BFS from addr (init/loop tracing)
  analyze.py whoimports <dllname>
  analyze.py map                   # function map: addr,size,name,strings,srcfiles
  analyze.py tree <addr> [depth]   # call tree with string annotations
"""
import re, struct, sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

BASE = "/tmp/opencode/a2/spelana/"
PE_PATH = BASE + "spel.dat"
IMG = 0x400000

pe = pefile.PE(PE_PATH)
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False

# --- sections ---
sections = [(s.Name.rstrip(b'\0').decode('latin1'), s.VirtualAddress + IMG,
             s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData)
            for s in pe.sections]

def va_to_off(va):
    for name, vaddr, vsize, raw, rsize in sections:
        if vaddr <= va < vaddr + max(vsize, rsize):
            return raw + (va - vaddr)
    return None

# --- imports: IAT addr -> name ---
iat = {}
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode()
    for imp in entry.imports:
        addr = imp.address  # IAT VA
        if imp.name:
            iat[addr] = f"{dll}!{imp.name.decode()}"
        else:
            iat[addr] = f"{dll}!ord#{imp.ordinal}"

# --- functions from refined funcs2.txt ---
funcs = []  # (start, size, name)
with open(BASE + "funcs2.txt") as f:
    for line in f:
        m = re.match(r'^(0x[0-9a-f]+)\s+(\d+)', line)
        if m:
            s = int(m.group(1), 16)
            funcs.append((s, int(m.group(2)), f"sub_{s:08x}"))
func_by_addr = {s: (sz, nm) for s, sz, nm in funcs}

def func_of(va):
    for s, sz, nm in funcs:
        if s <= va < s + sz:
            return s
    return None

# --- strings from izz + raw xref scan (reuse correlate approach) ---
strings = []
with open(BASE + "izz.txt", encoding="utf-8", errors="replace") as f:
    for line in f:
        m = re.match(r'^(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\d+)\s+(\d+)\s+(\S+)\s+(.*)$', line)
        if m:
            va = int(m.group(1), 16)
            if va < IMG:
                va += IMG
            strings.append((va, m.group(6)))

string_by_va = {}
for va, t in strings:
    string_by_va.setdefault(va, t)

# raw xref scan: string va -> list of xref VAs (inside any function)
sec_blobs = {}
for nm, vaddr, vsize, raw, rsize in sections:
    sec_blobs[nm] = (vaddr, pe.get_memory_mapped_image()[vaddr-IMG:vaddr-IMG+max(vsize, rsize)])

def xrefs_to(va):
    pat = struct.pack('<I', va)
    out = []
    for nm, (base_va, blob) in sec_blobs.items():
        start = 0
        while True:
            pos = blob.find(pat, start)
            if pos < 0:
                break
            out.append(base_va + pos)
            start = pos + 1
    return out

# --- string annotations per function (cached from build_annots.py) ---
import json as _json
func_annots = {}
try:
    _cache = _json.load(open(BASE + "func_annotations.json"))
    for k, v in _cache.items():
        func_annots[int(k, 16)] = (v["strings"], v["src"])
except FileNotFoundError:
    pass

def annotate(fva):
    return func_annots.get(fva, ([], []))

# --- disassemble a function's bytes, collect call targets ---
call_cache = {}
def calls_of(fva):
    if fva in call_cache:
        return call_cache[fva]
    sz, nm = func_by_addr.get(fva, (0, '?'))
    off = va_to_off(fva)
    if off is None:
        call_cache[fva] = []
        return []
    data = open(PE_PATH, 'rb').read()[off:off+sz]
    out = []
    for ins in md.disasm(data, fva):
        if ins.mnemonic == 'call':
            op = ins.op_str
            if op.startswith('0x'):
                t = int(op, 16)
                if t in iat:
                    out.append(('imp', iat[t]))
                else:
                    out.append(('call', t))
            elif '[' in op and op.startswith('dword'):
                pass
            else:
                # register-indirect; record text
                out.append(('ind', op))
    call_cache[fva] = out
    return out

def callers_of(fva):
    out = []
    for s, sz, nm in funcs:
        if s == fva:
            continue
        for kind, t in calls_of(s):
            if kind == 'call' and t == fva:
                out.append(s)
    return out

def callees_named(fva):
    return calls_of(fva)

def chain_forward(start, maxdepth=4):
    """BFS call tree (direct calls only)."""
    seen = {start}
    frontier = [start]
    layers = []
    for d in range(maxdepth):
        nxt = set()
        for f in frontier:
            for kind, t in calls_of(f):
                if kind == 'call' and t in func_by_addr and t not in seen:
                    nxt.add(t)
        seen |= nxt
        layers.append(sorted(nxt))
        frontier = list(nxt)
    return layers

def show_func(fva, label=''):
    sz, nm = func_by_addr.get(fva, (0, '?'))
    strs, srcs = annotate(fva)
    print(f"{label}0x{fva:08x} {nm}  (size {sz})")
    for s in srcs:
        print(f"    SRC: {s}")
    for s in strs[:14]:
        print(f"    str: {s[:100]}")

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'callers':
        va = int(sys.argv[2], 16)
        show_func(va, 'TARGET ')
        for c in callers_of(va):
            show_func(c, '  <- ')
    elif cmd == 'callees':
        va = int(sys.argv[2], 16)
        show_func(va, 'FUNC ')
        for kind, t in calls_of(va):
            if kind == 'call' and t in func_by_addr:
                show_func(t, '    -> ')
            elif kind == 'imp':
                print(f'    -> IMP {t}')
    elif cmd == 'chain':
        va = int(sys.argv[2], 16)
        show_func(va, 'ROOT ')
        for d, layer in enumerate(chain_forward(va)):
            print(f'--- depth {d+1}: {len(layer)} funcs')
            for f in layer:
                show_func(f, '    ')
    elif cmd == 'whoimports':
        dll = sys.argv[2]
        hits = []
        for s, sz, nm in funcs:
            for kind, t in calls_of(s):
                if kind == 'imp' and t.startswith(dll):
                    hits.append((s, t))
                    break
        for s, t in hits:
            show_func(s, '')
    elif cmd == 'map':
        for s, sz, nm in sorted(funcs):
            strs, srcs = annotate(s)
            if srcs or any(strs):
                line = f"0x{s:08x} {nm} size={sz}"
                if srcs:
                    line += " SRC=" + ",".join(os.path.basename(x) for x in srcs)
                print(line)
                for st in strs[:6]:
                    print(f"    {st[:90]}")
    elif cmd == 'func':
        va = int(sys.argv[2], 16)
        show_func(va)

#!/usr/bin/env python3
"""Build func_annotations.json: function -> {strings, srcfiles} for spel.dat."""
import re, struct, json
import pefile

BASE = "/tmp/opencode/a2/spelana/"
IMG = 0x400000
pe = pefile.PE(BASE + "spel.dat")
sections = [(s.Name.rstrip(b'\0').decode('latin1'), s.VirtualAddress + IMG,
             s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData)
            for s in pe.sections]

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

strings = []
with open(BASE + "izz.txt", encoding="utf-8", errors="replace") as f:
    for line in f:
        m = re.match(r'^(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\d+)\s+(\d+)\s+(\S+)\s+(.*)$', line)
        if m:
            va = int(m.group(1), 16)
            if va < IMG:
                va += IMG
            strings.append((va, m.group(6)))
print(f"{len(strings)} strings, {len(funcs)} funcs")

# build ONE big xref map: scan sections once for all string pointers
# strategy: for each section, scan for ANY 4-byte value that maps to a string va.
# Build set of string vas, then single pass over each section: take each u32, look up.
strva_set = {va for va, t in strings if 0x400000 <= va <= 0x600000}
blobs = {}
for nm, vaddr, vsize, raw, rsize in sections:
    blobs[nm] = (vaddr, pe.get_memory_mapped_image()[vaddr-IMG:vaddr-IMG+max(vsize, rsize)])

func_hits = {}  # func_va -> set of string vas
for nm, (base_va, blob) in blobs.items():
    print("scanning", nm, len(blob))
    for off in range(0, len(blob) - 3, 1):
        v = struct.unpack_from('<I', blob, off)[0]
        if v in strva_set:
            fva = func_of(base_va + off)
            if fva:
                func_hits.setdefault(fva, set()).add(v)

out = {}
strtext = {va: t for va, t in strings}
for fva, svs in func_hits.items():
    strs = sorted(strtext[v] for v in svs)
    srcs = set()
    for t in strs:
        for src in re.findall(r'[A-Za-z]:[\\/][A-Za-z0-9_\\/.\-]+\.(?:cpp|hpp|h|c)', t):
            srcs.add(src)
    out[f"{fva:08x}"] = {"strings": strs, "src": sorted(srcs)}

with open(BASE + "func_annotations.json", "w") as f:
    json.dump(out, f, indent=1)
print("annotated functions:", len(out))

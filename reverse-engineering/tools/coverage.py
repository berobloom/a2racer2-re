#!/usr/bin/env python3
"""Which spel.dat functions has the project never mentioned?

Collects every direct call target (E8 rel32) and every function pointer
stored in .data/.rdata that points into .text, sizes each function as the
gap to the next entry, and lists the entries whose address appears nowhere
in research/, reverse-engineering/*.md or port/src (as 0x4xxxxx, any case).

Usage: coverage.py <spel.dat> [minsize]
"""
import glob, os, re, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
import peparse as p

P = p.parse(sys.argv[1]); d = P['data']; ib = P['imagebase']
minsize = int(sys.argv[2]) if len(sys.argv) > 2 else 0
text = [s for s in P['sections'] if s[0] == '.text'][0]
_, tva, tvs, traw, trs = text
lo, hi = ib + tva, ib + tva + tvs
code = d[traw:traw + min(tvs, trs)]

entries, callers = set(), {}
for i in range(len(code) - 5):
    if code[i] == 0xE8:
        t = ib + tva + i + 5 + struct.unpack_from('<i', code, i + 1)[0]
        if lo <= t < hi:
            entries.add(t)
            callers.setdefault(t, set()).add(ib + tva + i)
ptrs = set()
for name, va, vs, raw, rs in P['sections']:
    if name == '.text' or not raw:
        continue
    for o in range(raw, raw + rs - 3, 4):
        v = struct.unpack_from('<I', d, o)[0]
        if lo <= v < hi:
            off = v - ib - tva
            # a plausible entry: follows padding / ret or starts a frame
            if off == 0 or code[off - 1] in (0x90, 0xC3, 0xCC) or code[off:off + 3] == b'\x55\x8b\xec':
                ptrs.add(v)
entries |= ptrs
srt = sorted(entries)

root = os.path.join(os.path.dirname(__file__), '..', '..')
corpus = ''
for pat in ('research/*.md', 'reverse-engineering/*.md', 'port/src/*.[ch]', 'port/README.md',
            'reverse-engineering/tools/*.py'):
    for f in glob.glob(os.path.join(root, pat)):
        corpus += open(f, errors='replace').read().lower()
known = set(int(m, 16) for m in re.findall(r'0x(4[0-9a-f]{5})\b', corpus))

rows = []
for k, a in enumerate(srt):
    end = srt[k + 1] if k + 1 < len(srt) else hi
    size = end - a
    if a not in known and size >= minsize:
        rows.append((size, a, len(callers.get(a, ())), a in ptrs))
print(f'{len(srt)} entries, {sum(1 for a in srt if a in known)} mentioned, '
      f'{len(rows)} unmentioned >= {minsize} bytes')
for size, a, nc, isp in sorted(rows, reverse=True):
    print(f'0x{a:06x}  {size:6d} B  {nc:3d} callers{"  (pointer)" if isp else ""}')

# --- tag each function with the source files its asserts name -------------
cpp = {}
for name, va, vs, raw, rs in P['sections']:
    if name == '.text' or not raw:
        continue
    for m in re.finditer(rb'[A-Za-z0-9_\\:.]+\.(?:cpp|c|h)\x00', d[raw:raw + rs]):
        s = m.group()[:-1].decode('latin1')
        cpp[ib + va + m.start()] = s.split('\\')[-1]

def tags(a, size):
    off = a - ib - tva
    body = code[off:off + size]
    t = set()
    for i in range(len(body) - 3):
        v = struct.unpack_from('<I', body, i)[0]
        if v in cpp:
            t.add(cpp[v])
    return t

if os.environ.get('TAGS'):
    by = {}
    for size, a, nc, isp in rows:
        for t in tags(a, size) or {'?'}:
            by.setdefault(t, []).append((size, a))
    for t, lst in sorted(by.items(), key=lambda kv: -sum(s for s, _ in kv[1])):
        print(f'{t:24s} {len(lst):4d} fns {sum(s for s, _ in lst):7d} B  '
              + ' '.join(f'0x{a:06x}' for s, a in sorted(lst, reverse=True)[:8]))

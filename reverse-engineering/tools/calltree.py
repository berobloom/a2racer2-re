#!/usr/bin/env python3
"""Annotated call tree for key architecture functions.
Usage: calltree.py <root_addr> [depth]
Prints each callee with size, source files, and distinguishing strings."""
import sys
sys.path.insert(0, '/tmp/opencode/a2/spelana')
from analyze import func_by_addr, calls_of, annotate, funcs

def name_of(va):
    strs, srcs = annotate(va)
    label = ''
    # heuristic name from strings like "==== Entry init_game ====", "init_xxx ( )\n"
    for s in strs:
        import re
        m = re.search(r'Entry\s+(\w+)', s)
        if m: return m.group(1)
        m = re.search(r'^([a-z_][a-z0-9_]{3,})\s*\(\s*\)', s.strip())
        if m: return m.group(1)
    if srcs:
        import os
        return "?(" + ",".join(os.path.basename(x) for x in srcs[:1]) + ")"
    return "?"

def tree(va, depth=3, indent=0, seen=None):
    if seen is None: seen = set()
    if va in seen or depth <= 0:
        return
    seen.add(va)
    sz, nm = func_by_addr.get(va, (0, '?'))
    strs, srcs = annotate(va)
    label = name_of(va)
    import os
    src = ",".join(os.path.basename(x) for x in srcs[:1])
    print(f"{'  '*indent}0x{va:08x} [{label}] (size {sz})" + (f" src={src}" if src else ""))
    for s in strs[:5]:
        t = s.strip().replace('\n','\\n')
        if len(t) < 90 and t:
            print(f"{'  '*indent}    str: {t}")
    callees = [t for k, t in calls_of(va) if k == 'call' and t in func_by_addr]
    # prioritize callees that have labels
    callees.sort(key=lambda c: (name_of(c) == '?', c))
    for c in callees:
        tree(c, depth-1, indent+1, seen)

if __name__ == '__main__':
    tree(int(sys.argv[1], 16), int(sys.argv[2]) if len(sys.argv) > 2 else 2)

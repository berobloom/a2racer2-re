#!/usr/bin/env python3
"""variants.py: try several spellings of one piece of a source file, and count the instruction
rows each leaves different in one function.

usage: variants.py PROG FILE.cpp NAME VARIANTS.txt

VARIANTS.txt holds blocks separated by lines of '====': the first block is text that must
occur once in src/PROG/FILE.cpp, each following block a replacement for it. Each variant is
compiled with check.sh's options in .work/variants and compared with fndiff.py.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from pe import ROOT   # noqa: E402

FLAGS = re.search(r'CFLAGS="([^"]*)"', open(os.path.join(ROOT, 'src', 'check.sh')).read()).group(1).split()


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    prog, fname, name, vfile = sys.argv[1:5]
    src = os.path.join(ROOT, 'src', prog)
    text = open(os.path.join(src, fname)).read()
    blocks = [b.strip('\n') for b in re.split(r'^====+\s*$', open(vfile).read(), flags=re.M)]
    old, news = blocks[0], blocks[1:]
    if text.count(old) != 1:
        print('the first block occurs %d times in %s' % (text.count(old), fname))
        return 1
    w = os.path.join(ROOT, '.work', 'variants')
    os.makedirs(w, exist_ok=True)
    for f in os.listdir(src):
        if f.endswith('.h'):
            open(os.path.join(w, f), 'w').write(open(os.path.join(src, f)).read())
    for k, new in enumerate([old] + news):
        base = 'v%d' % k
        open(os.path.join(w, base + '.cpp'), 'w').write(text.replace(old, new))
        obj = os.path.join(w, base + '.obj')
        if os.path.exists(obj):
            os.remove(obj)
        subprocess.run([os.path.join(ROOT, 'reverse-engineering', 'tools', 'msvc5.sh'), 'cl'] + FLAGS
                       + ['/Fo' + base + '.obj', base + '.cpp'], cwd=w, capture_output=True)
        if not os.path.exists(obj):
            res = 'does not compile'
        else:
            out = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'fndiff.py'), prog, obj, name],
                                 cwd=ROOT, capture_output=True, text=True).stdout.strip().split('\n')[-1]
            m = re.search(r'(\d+) instructions, (\d+) differ', out)
            res = '%s of %s differ' % (m.group(2), m.group(1)) if m else out
        first = new.strip().split('\n')[0][:70]
        print('%-6s %-22s %s' % ('as is' if k == 0 else 'v%d' % k, res, first))


if __name__ == '__main__':
    sys.exit(main())

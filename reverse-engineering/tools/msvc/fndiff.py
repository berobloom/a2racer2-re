#!/usr/bin/env python3
"""fndiff.py: one function of our .obj against the game's, aligned instruction by instruction.

usage: fndiff.py PROG OBJ NAME [0xADDR]

NAME is the source name ('CalculateStearing', 'Class::method'); the address comes from the
annotations in the .obj's source directory (src/spel for spel) unless given. Relocation bytes
are masked on our side (shown as '@'), and 32-bit constants of 4+ hex digits read as '@' on
both sides, so only instruction shapes count. Columns: the GAME on the left, OURS on the right.
"""
import difflib
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import capstone              # noqa: E402
import coff                  # noqa: E402
import objmatch as M         # noqa: E402
from pe import Image, ROOT   # noqa: E402

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)


def norm(s):
    # a masked relocation (0x90 filler) reads as `[reg - 0x6f6f6f70]` where the game has
    # `[reg + addr]`: the same instruction
    return re.sub(r'0x[0-9a-f]{4,}', '@', s).replace('- @', '+ @')


def dis(buf, vma):
    return [(i.address, i.size, norm(i.mnemonic + ' ' + i.op_str)) for i in md.disasm(buf, vma)]


def rows(ops):
    return sum(max(i2 - i1, j2 - j1) for t, i1, i2, j1, j2 in ops if t != 'equal')


def align(a, b):
    """difflib opcodes for two instruction-text lists, the better of autojunk on and off."""
    best = None
    for junk in (True, False):
        ops = difflib.SequenceMatcher(None, a, b, autojunk=junk).get_opcodes()
        if best is None or rows(ops) < rows(best):
            best = ops
    return best


def ours_masked(ob, sec, start, end):
    data = ob.sections[sec].data
    body = bytearray(coff.body(data, start, end))
    for off, _typ, _sym in ob.relocs_in(sec, start, start + len(body)):
        for j in range(4):
            if 0 <= off - start + j < len(body):
                body[off - start + j] = 0x90
    return bytes(body)


def game_len(prog, addr):
    """The game function's length without its padding, from PROG_funcs.tsv (None if not listed)."""
    import funcs
    for a, n, _ in funcs.load(prog):
        if a == addr:
            b = Image(prog).read(a, n)
            return len(coff.body(b, 0, n))
    return None


def diff(img, ob, sec, start, end, addr, glen=None):
    """(game rows, our rows, opcodes): GAME is the first sequence, OURS the second."""
    ours = ours_masked(ob, sec, start, end)
    A = dis(ours, addr)
    B = dis(img.read(addr, glen or len(ours)), addr)
    return B, A, align([t[2] for t in B], [t[2] for t in A])


def hunks(B, A, ops, context=3):
    """Diff hunks as lists of [tag, game address, our address, text] (tag ' ', '-' game, '+' ours)."""
    sm = difflib.SequenceMatcher(None, [], [])
    sm.get_opcodes = lambda: ops
    out = []
    for grp in sm.get_grouped_opcodes(context):
        h = []
        for t, i1, i2, j1, j2 in grp:
            if t == 'equal':
                h += [[' ', B[i][0], A[j][0], B[i][2]] for i, j in zip(range(i1, i2), range(j1, j2))]
            else:
                h += [['-', B[i][0], 0, B[i][2]] for i in range(i1, i2)]
                h += [['+', 0, A[j][0], A[j][2]] for j in range(j1, j2)]
        out.append(h)
    return out


def find(ob, name):
    for mname, sec, start, end in ob.functions():
        if M.plain(mname) == name or mname == name:
            return sec, start, end
    return None


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    prog, obj, name = sys.argv[1:4]
    img = Image(prog)
    ob = coff.Obj(open(obj, 'rb').read())
    hit = find(ob, name)
    if not hit:
        print('%s: not in %s' % (name, obj))
        return 1
    if len(sys.argv) > 4:
        addr = int(sys.argv[4], 16)
    else:
        where, _ = M.annotations(os.path.join(ROOT, 'src', prog), prog)
        addr = where.get(name)
        if addr is None:
            print('%s: no FUNCTION annotation in src/%s' % (name, prog))
            return 1
    B, A, ops = diff(img, ob, *hit, addr, game_len(prog, addr))
    n = 0
    for t, i1, i2, j1, j2 in ops:
        if t == 'equal':
            continue
        n += max(i2 - i1, j2 - j1)
        for k in range(max(i2 - i1, j2 - j1)):
            b = B[i1 + k] if i1 + k < i2 else None
            a = A[j1 + k] if j1 + k < j2 else None
            print('%-8s %-34s | %-8s %s' % (hex(b[0]) if b else '', b[2] if b else '',
                                            hex(a[0]) if a else '', a[2] if a else ''))
    print('%s: %d instructions, %d differ (game left, ours right; addresses as @)' % (name, len(A), n))


if __name__ == '__main__':
    sys.exit(main())

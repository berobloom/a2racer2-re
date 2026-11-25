#!/usr/bin/env python3
"""objmatch.py: compare every annotated function of our .obj files with the game, byte for byte.

usage: objmatch.py PROG SRCDIR OBJ... [-v]

Addresses come from the annotations in SRCDIR's .cpp and .h files:
    // FUNCTION: SPEL 0x401230      the next declaration or definition is at that address
    // GLOBAL: SPEL 0x4a55a0        the next variable is at that address
Each function the .obj defines and the annotations place is compared with the game:

  bytes    equal everywhere except the 4 bytes of each relocation; the game must also have
           only 0x90 padding after it up to the next 16-byte boundary
  refs     each relocation's target in the game, worked out from the game's bytes, must be
           the address the annotations give the symbol; a string literal or a float
           constant must have the same bytes at that address in the game; a symbol with no
           annotation must point at one address everywhere it is used (it is 'inferred')

A function is MATCH only if both hold. Prints one line per function:
    NAME   SIZE bytes  MATCH at 0xADDR
    NAME   SIZE bytes  differs from 0xADDR  (N bytes, M refs)
-v adds the differing offsets, the reference problems, and the inferred addresses.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coff                  # noqa: E402
from pe import Image         # noqa: E402

ANN = re.compile(r'//\s*(FUNCTION|GLOBAL):\s*(\w+)\s+(0x[0-9a-fA-F]+)\b')
FNAME = re.compile(r'([A-Za-z_~][\w:~]*)\s*\(')
VNAME = re.compile(r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?:=|;)')


CLASS = re.compile(r'^\s*(?:struct|class)\s+(\w+)[^;]*$')


def enclosing(lines, i):
    """The struct/class whose body line i is in ('' at file level): the nearest struct/class
    line above it whose body has not been closed by a '};' line in between."""
    depth = 0
    for j in range(i - 1, -1, -1):
        t = lines[j].strip()
        if t.startswith('};'):
            depth += 1
        m = CLASS.match(lines[j])
        if m and (t.endswith('{') or (j + 1 < len(lines) and lines[j + 1].strip() == '{')):
            if depth == 0:
                return m.group(1)
            depth -= 1
    return ''


def annotations(srcdir, prog):
    """{qualified name: address} from every .cpp/.h in srcdir, and {address: (file, name)} of
    the definitions (functions with a body)."""
    where, defs = {}, {}
    for fn in sorted(os.listdir(srcdir)):
        if not fn.lower().endswith(('.cpp', '.h', '.c')):
            continue
        lines = open(os.path.join(srcdir, fn), encoding='latin1').read().split('\n')
        for i, line in enumerate(lines):
            m = ANN.search(line)
            if not m or m.group(2).upper() != prog.upper():
                continue
            kind, addr = m.group(1), int(m.group(3), 16)
            j = i + 1
            while j < len(lines) and (not lines[j].strip() or lines[j].strip().startswith('//')):
                j += 1
            decl = ''
            while j < len(lines):
                decl += ' ' + lines[j].strip()
                if decl.rstrip().endswith((';', '{', ')')) or '{' in decl:
                    break
                j += 1
            scope = enclosing(lines, i)
            if kind == 'FUNCTION':
                n = FNAME.search(decl)
                if not n:
                    continue
                name = n.group(1)
                if scope and '::' not in name:
                    name = scope + '::' + name
                where[name] = addr
                rest = decl[decl.index(')'):] if ')' in decl else ''
                k = j + 1
                if '{' in rest or (not rest.rstrip().endswith(';') and k < len(lines) and lines[k].strip().startswith('{')):
                    defs[addr] = (fn, name)
            else:
                n = VNAME.search(decl)
                if n:
                    where[n.group(1)] = addr
    return where, defs


def libnames(prog):
    """{source name: address} of the C library functions found in the game
    (reverse-engineering/analysis/PROG_libc.tsv, libmatch.py): calls to them are checked too."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'analysis', prog + '_libc.tsv')
    out = {}
    if os.path.exists(path):
        for line in open(path):
            f = line.rstrip('\n').split('\t')
            if len(f) >= 4 and f[0].startswith('0x'):
                names = f[3].split()
                if len(names) == 1:
                    out[plain(names[0])] = int(f[0], 16)
    return out


def plain(mangled):
    """The qualified source name of an MSVC symbol: '?f@C@@QAEXXZ' -> 'C::f', '??0C@@..' -> 'C::C',
    '??1C@@..' -> 'C::~C', '_f' -> 'f'. Operators and templates come back unchanged."""
    if mangled.startswith('??0') or mangled.startswith('??1'):
        cls = mangled[3:].split('@')[0]
        return '%s::%s%s' % (cls, '~' if mangled[2] == '1' else '', cls)
    if mangled.startswith('??'):
        return mangled
    if mangled.startswith('?'):
        parts = mangled[1:].split('@@')[0].split('@')
        return '::'.join(reversed(parts))
    if mangled.startswith('_'):
        return mangled[1:].split('@')[0]
    return mangled


def const_bytes(ob, sym, addend, n=None):
    """The bytes a relocation to a literal points at in our object: a string up to its NUL, a float
    constant (__real@...) whole. None if the symbol is not a literal of this object."""
    if sym.secnum <= 0:
        return None
    sec = ob.sections[sym.secnum]
    if sec.code:
        return None
    if sym.name.startswith(('$SG', '??_C@')):            # string literals ('??_C@': /O2 implies /GF)
        o = sym.value + addend
        end = sec.data.find(b'\0', o)
        return bytes(sec.data[o:(end + 1) if end >= 0 else None])
    if sym.name.startswith(('__real@', '$T')) or sym.klass == 3:
        # a constant (VC 5.0 names float constants $T<n>) or a static: all of it, up to the next
        # symbol of its section. Checking only up to a NUL would pass 0.25f for 0.5f.
        nxt = [t.value for t in ob.symbols.values() if t.secnum == sym.secnum and t.value > sym.value]
        end = min(nxt) if nxt else len(sec.data)
        return bytes(sec.data[sym.value:end])
    return None


def compare(img, ob, sec, start, end, addr, where, inferred):
    data = ob.sections[sec].data
    ours = coff.body(data, start, end)
    n = len(ours)
    game = img.read(addr, n)
    mask = bytearray(n)
    rel = ob.relocs_in(sec, start, start + n)
    for off, typ, sym in rel:
        for j in range(4):
            if 0 <= off - start + j < n:
                mask[off - start + j] = 1
    bad = [i for i in range(n) if not mask[i] and (game is None or game[i] != ours[i])]
    # after the body: padding to the next 16-byte boundary
    pad_end = (addr + n + 15) & ~15
    tail = img.read(addr + n, pad_end - addr - n) or b''
    if any(b != 0x90 for b in tail):
        bad.append(n)
    probs = []
    for off, typ, sym in rel:
        o = off - start
        addend = struct.unpack_from('<i', ours, o)[0]
        g = struct.unpack_from('<I', game, o)[0] if game else 0
        if typ == coff.REL_REL32:
            target = (addr + o + 4 + struct.unpack('<i', struct.pack('<I', g))[0]) & 0xffffffff
        elif typ == coff.REL_DIR32:
            target = (g - addend) & 0xffffffff
        else:
            probs.append('%s: relocation type 0x%x not handled' % (sym.name, typ))
            continue
        name = plain(sym.name)
        lit = const_bytes(ob, sym, addend if typ == coff.REL_DIR32 else 0)
        if name in where:
            if where[name] != target:
                probs.append('%s: the game has 0x%x, the annotation 0x%x' % (name, target, where[name]))
        elif lit is not None:
            at = target + (addend if typ == coff.REL_DIR32 else 0)
            if img.read(at, len(lit)) != lit:
                probs.append('%s (%r): not at 0x%x in the game' % (sym.name, lit[:24], at))
        else:
            prev = inferred.setdefault(name, target)
            if prev != target:
                probs.append('%s: 0x%x here, 0x%x elsewhere' % (name, target, prev))
    return n, bad, probs


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('-')]
    if len(args) < 3:
        print(__doc__)
        return 2
    prog, srcdir, objs = args[0], args[1], args[2:]
    verbose = '-v' in sys.argv
    img = Image(prog)
    where, defs = annotations(srcdir, prog)
    for k, v in libnames(prog).items():
        where.setdefault(k, v)
    inferred = {}
    seen = set()
    for path in objs:
        ob = coff.Obj(open(path, 'rb').read())
        placed = [where[plain(f[0])] for f in ob.functions()
                  if where.get(plain(f[0])) in defs]
        for x, y in zip(placed, placed[1:]):
            if y < x:
                print('order: %s defines 0x%06x after 0x%06x; the game has them the other way round'
                      % (os.path.basename(path), y, x))
        for mname, sec, start, end in ob.functions():
            name = plain(mname)
            addr = where.get(name)
            if addr is None or addr not in defs:
                if verbose:
                    print('%-36s (no FUNCTION annotation: not compared)' % name)
                continue
            seen.add(addr)
            n, bad, probs = compare(img, ob, sec, start, end, addr, where, inferred)
            if not bad and not probs:
                print('%-36s %5d bytes  MATCH at 0x%06x' % (name, n, addr))
            else:
                print('%-36s %5d bytes  differs from 0x%06x  (%d bytes, %d refs)' % (name, n, addr, len(bad), len(probs)))
            if verbose:
                if bad:
                    print('      bytes differ at +' + ' +'.join('%x' % b for b in bad[:24]))
                for p in probs:
                    print('      ' + p)
    for addr, (fn, name) in sorted(defs.items()):
        if addr not in seen:
            print('%-36s     - bytes  not in any object (%s)' % (name, fn))
    if verbose and inferred:
        print('inferred (no annotation):')
        for k, v in sorted(inferred.items(), key=lambda kv: kv[1]):
            print('      0x%06x %s' % (v, k))


if __name__ == '__main__':
    sys.exit(main())

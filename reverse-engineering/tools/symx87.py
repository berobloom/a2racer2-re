#!/usr/bin/env python3
"""Run a spel.dat function under Unicorn and shadow its x87 arithmetic with
sympy, so every float it stores comes out as a formula of its inputs.

Control flow is decided by the concrete run; the formulas are therefore valid
for the branch taken, and comparisons are logged so the branch conditions are
visible.  Every symbolic store is checked against the concrete value Unicorn
computed: a mismatch means the shadow got an instruction wrong.

Library use (see the harnesses in this directory):

    t = Tracer('spel.dat')
    t.name_region(0x484b18 + 2, 0x484b18 + 2 + 56*4, lambda off: f'f{off//4}')
    t.set_float(addr, value, 'name')      # a symbolic input
    t.call(0x40d5d0, [args...])           # cdecl, ints or ('f', value, 'name')
    t.report(addresses...)                # formulas stored there

Needs: pip install unicorn capstone sympy
"""
import math
import struct

import sympy as sp
from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_MODE_32, Uc
from unicorn.x86_const import *

REGS = {'eax': UC_X86_REG_EAX, 'ebx': UC_X86_REG_EBX, 'ecx': UC_X86_REG_ECX,
        'edx': UC_X86_REG_EDX, 'esi': UC_X86_REG_ESI, 'edi': UC_X86_REG_EDI,
        'ebp': UC_X86_REG_EBP, 'esp': UC_X86_REG_ESP}


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class Tracer:
    HEAP, STK, RET = 0x10000000, 0x20000000, 0x30000000

    def __init__(self, path):
        d = open(path, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        n = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        ib = struct.unpack_from('<I', d, pe + 52)[0]
        size = struct.unpack_from('<I', d, pe + 24 + 56)[0]
        mu = Uc(UC_ARCH_X86, UC_MODE_32)
        mu.mem_map(ib, (size + 0xfff) & ~0xfff)
        mu.mem_write(ib, d[:0x1000])
        for i in range(n):
            o = pe + 24 + opt + 40 * i
            vs, va, rs, raw = struct.unpack_from('<IIII', d, o + 8)
            mu.mem_write(ib + va, d[raw:raw + min(rs, vs if vs else rs)])
        mu.mem_map(self.HEAP, 0x1000000)
        mu.mem_map(self.STK, 0x100000)
        mu.mem_map(self.RET, 0x1000)
        self.mu = mu
        self.heap = self.HEAP
        self.cs = Cs(CS_ARCH_X86, CS_MODE_32)
        self.cs.detail = True
        self.regions = []
        self.stubs = {}
        self.reset()
        mu.hook_add(UC_HOOK_CODE, self._hook)
        # stubs every physics function needs
        self.stub(0x464ca0, lambda t: 0)           # debug print
        self.stub(0x464e50, self._assert)          # assert

    # ------------------------------------------------------------ set-up
    def reset(self):
        self.mem = {}      # addr -> sympy expr (4-byte float slot)
        self.mem8 = {}     # addr -> sympy expr (8-byte double slot)
        self.st = []       # x87 stack shadow, index 0 = st(0)
        self.reg = {}      # gpr name -> expr (when it carries a float bit pattern)
        self.log = []
        self.checks = []

    def alloc(self, nbytes):
        a = self.heap
        self.heap += (nbytes + 15) & ~15
        return a

    def name_region(self, lo, hi, namer):
        self.regions.append((lo, hi, namer))

    def stub(self, addr, fn):
        self.stubs[addr] = fn

    def w32(self, a, v):
        self.mu.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def r32(self, a):
        return struct.unpack('<I', self.mu.mem_read(a, 4))[0]

    def rf(self, a):
        return struct.unpack('<f', self.mu.mem_read(a, 4))[0]

    def set_float(self, a, v, name=None):
        self.mu.mem_write(a, struct.pack('<f', v))
        if name:
            self.mem[a] = sp.Symbol(name)
            self.vals[name] = f32(v)

    vals = {}

    # ------------------------------------------------------------ symbols
    def _name(self, a):
        for lo, hi, namer in self.regions:
            if lo <= a < hi:
                return namer(a - lo)
        return f'm{a:x}'

    def _load32(self, a):
        if a in self.mem:
            return self.mem[a]
        v = self.rf(a)
        nm = self._name(a)
        if nm is None:  # treat as a literal constant
            return sp.Float(v, 8) if not float(v).is_integer() else sp.Integer(int(v))
        s = sp.Symbol(nm)
        self.vals.setdefault(nm, v)
        self.mem[a] = s
        return s

    def _load64(self, a):
        if a in self.mem8:
            return self.mem8[a]
        v = struct.unpack('<d', self.mu.mem_read(a, 8))[0]
        if a < self.HEAP and self._name(a).startswith('m'):
            return sp.nsimplify(v) if abs(v - round(v, 6)) < 1e-12 else sp.Float(v, 15)
        s = sp.Symbol(self._name(a) + 'd')
        self.vals.setdefault(str(s), v)
        return s

    # ------------------------------------------------------------ stubs
    def _assert(self, t):
        self.log.append('ASSERT')
        return 0

    def _ftol(self, t):
        e = self.st.pop(0) if self.st else None
        self.reg['eax'] = None
        if e is not None:
            self.log.append(f'ftol({e})')
        return None  # eax already holds the concrete result? no -> compute
    # (ftol is implemented below in _hook because it must read the FPU)

    # ------------------------------------------------------------ hook
    def _ea(self, op):
        m = op.mem
        a = m.disp
        if m.base:
            a += self.mu.reg_read(self._ucreg(m.base))
        if m.index:
            a += self.mu.reg_read(self._ucreg(m.index)) * m.scale
        return a & 0xffffffff

    def _ucreg(self, r):
        name = self.cs.reg_name(r)
        return REGS[name]

    def _hook(self, uc, addr, size, _):
        if addr == self.RET:
            uc.emu_stop()
            return
        if addr in self.stubs:
            esp = uc.reg_read(UC_X86_REG_ESP)
            ret = struct.unpack('<I', uc.mem_read(esp, 4))[0]
            if addr == 0x4700e0:
                # __ftol: convert st0 (concrete via fistp emulation) to eax
                fpst = uc.reg_read(UC_X86_REG_FP0)
                e = self.st.pop(0) if self.st else None
                val = self._fp_top()
                uc.reg_write(UC_X86_REG_EAX, int(val) & 0xffffffff)
                self._fpu_pop()
                self.log.append(f'ftol -> {int(val)}   [{e}]')
                self.reg['eax'] = None
            else:
                r = self.stubs[addr](self)
                if r is not None:
                    uc.reg_write(UC_X86_REG_EAX, r & 0xffffffff)
            uc.reg_write(UC_X86_REG_ESP, esp + 4)
            uc.reg_write(UC_X86_REG_EIP, ret)
            return
        code = bytes(uc.mem_read(addr, size))
        ins = next(self.cs.disasm(code, addr))
        try:
            self._shadow(ins)
        except Exception as ex:  # keep running; note it
            self.log.append(f'{addr:#x} shadow error {ins.mnemonic} {ins.op_str}: {ex}')

    def _fp_top(self):
        # read st(0) via a scratch fst
        scratch = self.STK + 0x10
        sw = None
        # emulate: we cannot run code here, so read FP registers directly
        top = (self.mu.reg_read(UC_X86_REG_FPSW) >> 11) & 7
        raw = self.mu.reg_read(UC_X86_REG_FP0 + 0)
        mant, exp = raw
        return self._f80(mant, exp)

    def _fpu_pop(self):
        sw = self.mu.reg_read(UC_X86_REG_FPSW)
        top = (sw >> 11) & 7
        tag = self.mu.reg_read(UC_X86_REG_FPTAG)
        tag |= 3 << (2 * top)
        self.mu.reg_write(UC_X86_REG_FPTAG, tag)
        sw = (sw & ~(7 << 11)) | (((top + 1) & 7) << 11)
        self.mu.reg_write(UC_X86_REG_FPSW, sw)

    @staticmethod
    def _f80(mant, exp):
        sign = -1 if exp & 0x8000 else 1
        e = exp & 0x7fff
        if e == 0 and mant == 0:
            return 0.0
        return sign * mant * 2.0 ** (e - 16383 - 63)

    # ---- the x87 / mov shadow
    def _push(self, e):
        self.st.insert(0, e)

    def _pop(self):
        return self.st.pop(0) if self.st else sp.Symbol('UNDERFLOW')

    def _sti(self, opstr):
        # 'st(3)' -> 3, 'st(0)' -> 0
        return int(opstr.strip()[3]) if '(' in opstr else 0

    def _memval(self, ins, op):
        a = self._ea(op)
        return self._load64(a) if op.size == 8 else self._load32(a), a

    def _shadow(self, ins):
        m, ops = ins.mnemonic, ins.operands
        st = self.st
        if m in ('fld', 'fild'):
            if ops[0].type == X86_OP_MEM:
                if m == 'fild':
                    a = self._ea(ops[0])
                    if a in self.mem:
                        e = self.mem[a]
                    else:
                        v = struct.unpack('<i', self.mu.mem_read(a, 4))[0] if ops[0].size == 4 \
                            else struct.unpack('<h', self.mu.mem_read(a, 2))[0]
                        nm = self._name(a)
                        e = sp.Integer(v) if nm.startswith('m') or a >= self.STK else sp.Symbol('i_' + nm)
                    self._push(e)
                else:
                    e, _ = self._memval(ins, ops[0])
                    self._push(e)
            else:
                self._push(st[self._sti(ins.op_str)])
        elif m in ('fld1', 'fldz', 'fldpi', 'fldln2', 'fldl2e'):
            self._push({'fld1': sp.Integer(1), 'fldz': sp.Integer(0), 'fldpi': sp.pi,
                        'fldln2': sp.log(2), 'fldl2e': 1 / sp.log(2)}[m])
        elif m in ('fst', 'fstp'):
            if ops[0].type == X86_OP_MEM:
                a = self._ea(ops[0])
                e = st[0]
                if ops[0].size == 8:
                    self.mem8[a] = e
                else:
                    self.mem[a] = e
                    self.checks.append((ins.address, a, e))
            else:
                i = self._sti(ins.op_str)
                st[i] = st[0]
            if m == 'fstp':
                self._pop()
        elif m in ('fistp', 'fist'):
            self.log.append(f'{ins.address:#x} fist {st[0]}')
            if m == 'fistp':
                self._pop()
        elif m in ('fchs', 'fabs', 'fsqrt', 'fsin', 'fcos'):
            f = {'fchs': lambda x: -x, 'fabs': sp.Abs, 'fsqrt': sp.sqrt, 'fsin': sp.sin,
                 'fcos': sp.cos}[m]
            st[0] = f(st[0])
        elif m == 'fxch':
            i = self._sti(ins.op_str) if ins.op_str else 1
            st[0], st[i] = st[i], st[0]
        elif m == 'fpatan':
            x = self._pop()
            y = st[0]
            st[0] = sp.atan2(y, x)
        elif m == 'fyl2x':
            x = self._pop()
            st[0] = st[0] * sp.log(x, 2)
        elif m in ('fadd', 'fsub', 'fsubr', 'fmul', 'fdiv', 'fdivr', 'faddp', 'fsubp', 'fsubrp',
                   'fmulp', 'fdivp', 'fdivrp', 'fiadd', 'fimul', 'fisub', 'fidiv'):
            base = m.rstrip('p') if m.endswith('p') and m not in ('fsubp', 'fsubrp') else m
            pop = m.endswith('p') and m != 'fsub' and m != 'fsubr'
            op = {'add': lambda a, b: a + b, 'sub': lambda a, b: a - b, 'subr': lambda a, b: b - a,
                  'mul': lambda a, b: a * b, 'div': lambda a, b: a / b, 'divr': lambda a, b: b / a}
            core = m[1:]
            if core.startswith('i'):
                core = core[1:]
            if core.endswith('p'):
                core = core[:-1]
            fn = op[core]
            if len(ops) == 1 and ops[0].type == X86_OP_MEM:
                if m.startswith('fi'):
                    a = self._ea(ops[0])
                    v = struct.unpack('<i', self.mu.mem_read(a, 4))[0]
                    e = self.mem.get(a, sp.Integer(v))
                else:
                    e, _ = self._memval(ins, ops[0])
                st[0] = fn(st[0], e)
            else:
                # two-register forms: 'st(i), st(0)' or 'st(0), st(i)'; one-operand 'st(i)'
                parts = [p.strip() for p in ins.op_str.split(',')] if ins.op_str else ['st(1)', 'st(0)']
                if len(parts) == 1:
                    parts = [parts[0], 'st(0)']
                d, s = self._sti(parts[0]), self._sti(parts[1])
                st[d] = fn(st[d], st[s])
                if m.endswith('p'):
                    self._pop()
        elif m in ('fcom', 'fcomp', 'fcompp', 'fucom', 'fucomp', 'fucompp', 'ftst'):
            if m == 'ftst':
                other = sp.Integer(0)
            elif ops and ops[0].type == X86_OP_MEM:
                other, _ = self._memval(ins, ops[0])
            else:
                other = st[self._sti(ins.op_str) if ins.op_str else 1]
            self.log.append(f'{ins.address:#x} CMP  {st[0]}  vs  {other}')
            n = {'fcom': 0, 'fucom': 0, 'ftst': 0, 'fcomp': 1, 'fucomp': 1, 'fcompp': 2,
                 'fucompp': 2}[m]
            for _ in range(n):
                self._pop()
        elif m in ('fnstsw', 'fstsw', 'fnstcw', 'fldcw', 'fwait', 'wait', 'fclex', 'fnclex'):
            pass
        elif m.startswith('f'):
            self.log.append(f'{ins.address:#x} UNHANDLED {m} {ins.op_str}')
        # ---- integer moves that carry float bit patterns
        elif m == 'mov':
            dst, src = ops
            if dst.type == X86_OP_REG and src.type == X86_OP_MEM and dst.size == 4:
                a = self._ea(src)
                self.reg[self.cs.reg_name(dst.reg)] = self.mem.get(a, ('ADDR', a))
            elif dst.type == X86_OP_MEM and src.type == X86_OP_REG and dst.size == 4:
                a = self._ea(dst)
                r = self.reg.get(self.cs.reg_name(src.reg))
                if isinstance(r, tuple):
                    if r[1] in self.mem:
                        self.mem[a] = self.mem[r[1]]
                    else:
                        self.mem[a] = self._load32(r[1])
                elif r is not None:
                    self.mem[a] = r
                else:
                    self.mem.pop(a, None)
            elif dst.type == X86_OP_MEM and src.type == X86_OP_IMM:
                self.mem.pop(self._ea(dst), None)
            elif dst.type == X86_OP_REG and src.type == X86_OP_REG:
                self.reg[self.cs.reg_name(dst.reg)] = self.reg.get(self.cs.reg_name(src.reg))
            elif dst.type == X86_OP_REG:
                self.reg[self.cs.reg_name(dst.reg)] = None
        elif m == 'push':
            esp = self.mu.reg_read(UC_X86_REG_ESP) - 4
            src = ops[0]
            if src.type == X86_OP_REG:
                r = self.reg.get(self.cs.reg_name(src.reg))
                if isinstance(r, tuple):
                    self.mem[esp] = self._load32(r[1])
                elif r is not None:
                    self.mem[esp] = r
                else:
                    self.mem.pop(esp, None)
            elif src.type == X86_OP_MEM:
                a = self._ea(src)
                self.mem[esp] = self._load32(a)
            else:
                self.mem.pop(esp, None)
        elif m in ('rep movsd', 'movsd') or (m == 'rep' and 'movsd' in ins.op_str):
            n = self.mu.reg_read(UC_X86_REG_ECX) if 'rep' in m or m == 'rep' else 1
            s = self.mu.reg_read(UC_X86_REG_ESI)
            d = self.mu.reg_read(UC_X86_REG_EDI)
            for k in range(n):
                if s + 4 * k in self.mem:
                    self.mem[d + 4 * k] = self.mem[s + 4 * k]
                else:
                    self.mem.pop(d + 4 * k, None)
        else:
            for op in ops:
                if op.type == X86_OP_REG and ins.mnemonic not in ('cmp', 'test', 'push'):
                    self.reg[self.cs.reg_name(op.reg)] = None
                    break

    # ------------------------------------------------------------ running
    def call(self, fn, args, this=None):
        esp = self.STK + 0x80000
        words = []
        for a in args:
            if isinstance(a, tuple):  # ('f', value, name)
                words.append(struct.unpack('<I', struct.pack('<f', a[1]))[0])
            else:
                words.append(a & 0xffffffff)
        esp -= 4 * len(words)
        for i, w in enumerate(words):
            self.w32(esp + 4 * i, w)
            a = args[i]
            if isinstance(a, tuple) and a[2]:
                self.mem[esp + 4 * i] = sp.Symbol(a[2])
                self.vals[a[2]] = f32(a[1])
        esp -= 4
        self.w32(esp, self.RET)
        self.mu.reg_write(UC_X86_REG_ESP, esp)
        if this is not None:
            self.mu.reg_write(UC_X86_REG_ECX, this)
        self.mu.emu_start(fn, self.RET)

    def formula(self, a):
        return self.mem.get(a)

    def report(self, labelled, simplify=True):
        """labelled: list of (label, address).  Prints formula, the concrete
        value Unicorn stored, and the formula evaluated with the inputs."""
        subs = {sp.Symbol(k): v for k, v in self.vals.items()}
        for label, a in labelled:
            e = self.mem.get(a)
            conc = self.rf(a)
            if e is None:
                print(f'{label:24s} = (not symbolic) {conc:.6g}')
                continue
            es = sp.simplify(e) if simplify else e
            try:
                ev = float(es.evalf(subs=subs))
            except Exception:
                ev = float('nan')
            ok = 'OK ' if (math.isfinite(ev) and abs(ev - conc) <= 1e-3 * max(1.0, abs(conc))) else 'BAD'
            print(f'{label:24s} [{ok}] concrete={conc:.6g} eval={ev:.6g}\n    = {es}')

#!/usr/bin/env python3
"""Differential test: the port's driving model (port/src/drive.c, built with
-DA2_DRIVE_TEST into a shared library) against the original functions in
spel.dat, run under Unicorn on the same random states.

Usage: diff_drive.py <spel.dat> <CARS.DAT> <libdrive.so> [cases]
Needs: pip install unicorn
"""
import ctypes as C
import math
import random
import struct
import sys

from unicorn import UC_ARCH_X86, UC_MODE_32, Uc
from unicorn.x86_const import UC_X86_REG_ECX, UC_X86_REG_ESP

SPEL, CARSDAT, LIB = (sys.argv[1:4] + [None]*3)[:3] if len(sys.argv) > 3 else (None, None, None)
CASES = int(sys.argv[4]) if len(sys.argv) > 4 else 300


# ------------------------------------------------------------ the port side
class Car(C.Structure):
    _fields_ = [('f', C.c_float * 37), ('empty', C.c_int)]


class Drive(C.Structure):
    _fields_ = [(n, C.c_float) for n in
                ('v_lat', 'v_y', 'v_fwd', 'pitch_w', 'yaw_rate', 'roll_w', 'x', 'y', 'z',
                 'pitch', 'heading', 'roll')] + [
        ('gear', C.c_int), ('rpm', C.c_float), ('braking', C.c_int), ('skid', C.c_int),
        ('stopped', C.c_int), ('spin', C.c_float), ('drift', C.c_float), ('slide', C.c_float),
        ('hb_heading', C.c_float), ('hb_flag', C.c_int), ('handbrake', C.c_int),
        ('hb_forced', C.c_int), ('throttle', C.c_float), ('brake', C.c_int),
        ('steer', C.c_float), ('rng', C.c_uint)]


HEIGHT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_float, C.c_float, C.POINTER(C.c_float))


class Env(C.Structure):
    _fields_ = [('car', C.POINTER(Car)), ('motor', C.c_int), ('gearbox', C.c_int),
                ('tyres', C.c_int), ('abs', C.c_int), ('turbo_active', C.c_int),
                ('forced_handbrake', C.c_int), ('height', HEIGHT_FN), ('height_ud', C.c_void_p)]


lib = C.CDLL(LIB)
F = C.c_float
lib.a2_drive_gear_accel.restype = F
lib.a2_drive_gear_accel.argtypes = [C.POINTER(Drive), C.POINTER(Env), C.c_int, C.POINTER(F), F]
lib.a2_drive_longitudinal.argtypes = [C.POINTER(Drive), C.POINTER(Env), F, F, F]
lib.a2_drive_steering.argtypes = [C.POINTER(Drive), C.POINTER(Env), F, F, F]
lib.a2_drive_test_suspension.argtypes = [C.POINTER(Drive), C.POINTER(Env), F]
lib.a2_drive_test_ride.argtypes = [C.POINTER(Drive), C.POINTER(Env), F]
lib.a2_drive_test_integrate.argtypes = [C.POINTER(Drive), F]
lib.a2_drive_keys.argtypes = [C.POINTER(Drive), C.c_uint, F]
lib.a2_drive_step.argtypes = [C.POINTER(Drive), C.POINTER(Env), F]
RAND_C = C.c_int.in_dll(lib, 'a2_drive_test_rand')

cars_raw = open(CARSDAT, 'rb').read()


def car_struct(i):
    a = struct.unpack_from('<31f', cars_raw, 124 * i)
    b = struct.unpack_from('<6f', cars_raw, 1240 + 24 * i)
    c = Car()
    for k, v in enumerate(a + b):
        c.f[k] = v
    return c


# ------------------------------------------------------------ the original
class Orig:
    HEAP, STK, STUB = 0x10000000, 0x20000000, 0x30000000

    def __init__(self):
        d = open(SPEL, 'rb').read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        n = struct.unpack_from('<H', d, pe + 6)[0]
        opt = struct.unpack_from('<H', d, pe + 20)[0]
        ib = struct.unpack_from('<I', d, pe + 52)[0]
        size = struct.unpack_from('<I', d, pe + 24 + 56)[0]
        self.image = (ib, d, pe, n, opt, size)
        mu = Uc(UC_ARCH_X86, UC_MODE_32)
        mu.mem_map(ib, (size + 0xfff) & ~0xfff)
        mu.mem_write(ib, d[:0x1000])
        for i in range(n):
            o = pe + 24 + opt + 40 * i
            vs, va, rs, raw = struct.unpack_from('<IIII', d, o + 8)
            mu.mem_write(ib + va, d[raw:raw + min(rs, vs if vs else rs)])
        mu.mem_map(self.HEAP, 0x400000)
        mu.mem_map(self.STK, 0x100000)
        mu.mem_map(self.STUB, 0x1000)
        self.mu = mu
        # world
        self.veh, self.obj = self.HEAP, self.HEAP + 0x1000
        self.w(0x4e48e0, self.veh)
        self.w(0x4ea9e0, self.obj)
        self.w(self.veh + 0x08, 0)
        self.w(self.veh + 0x18, 0)
        sin_t, cos_t = self.HEAP + 0x10000, self.HEAP + 0x20000
        for i in range(4096):
            mu.mem_write(sin_t + 4 * i, struct.pack('<f', math.sin(2 * math.pi * i / 4096)))
            mu.mem_write(cos_t + 4 * i, struct.pack('<f', math.cos(2 * math.pi * i / 4096)))
        self.w(0x4ea418, sin_t)
        self.w(0x4ea4b4, cos_t)
        self.w(0x49b010, 4096)
        self.f(0x49b014, 4096.0)
        self.w(0x49b018, 0xfff)
        self.w(0x4e5d2c, self.HEAP + 0x30000)   # chunk index table (zeros)
        self.w(0x4e65e0, self.HEAP + 0x40000)   # chunk records
        mu.mem_write(0x4a5ee0, cars_raw[:1240])  # CARS.DAT block A
        mu.mem_write(0x4a5df0, cars_raw[1240:1480])  # block B
        # stubs: debug print, assert, rand, chunk lookup, road height
        self.hval = self.STUB + 0x800
        self.patch(0x464ca0, b'\xc3')
        self.patch(0x464e50, b'\xc3')
        self.patch_jmp(0x445a10, b'\x31\xc0\xc2\x10\x00', 0x00)
        self.patch_jmp(0x442f00, b'\xd9\x05' + struct.pack('<I', self.hval) + b'\xc2\x08\x00', 0x40)
        # 0x471790 is the CRT floor (control word 0x173f: round down); an x87
        # equivalent, since the CRT version needs runtime state.
        floor = bytes([0x83, 0xec, 0x04,              # sub esp, 4
                       0xd9, 0x3c, 0x24,              # fnstcw [esp]
                       0x66, 0x8b, 0x04, 0x24,        # mov ax, [esp]
                       0x66, 0x25, 0xff, 0xf3,        # and ax, 0xf3ff
                       0x66, 0x0d, 0x00, 0x04,        # or ax, 0x0400
                       0x66, 0x89, 0x44, 0x24, 0x02,  # mov [esp+2], ax
                       0xd9, 0x6c, 0x24, 0x02,        # fldcw [esp+2]
                       0xdd, 0x44, 0x24, 0x08,        # fld qword [esp+8]
                       0xd9, 0xfc,                    # frndint
                       0xd9, 0x2c, 0x24,              # fldcw [esp]
                       0x83, 0xc4, 0x04,              # add esp, 4
                       0xc3])
        self.patch_jmp(0x471790, floor, 0x80)

    def w(self, a, v):
        self.mu.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def r(self, a):
        return struct.unpack('<I', self.mu.mem_read(a, 4))[0]

    def f(self, a, v):
        self.mu.mem_write(a, struct.pack('<f', v))

    def rf(self, a):
        return struct.unpack('<f', self.mu.mem_read(a, 4))[0]

    def patch(self, a, code):
        self.mu.mem_write(a, code)

    def patch_jmp(self, a, code, slot):
        stub = self.STUB + slot
        self.mu.mem_write(stub, code)
        self.mu.mem_write(a, b'\xe9' + struct.pack('<i', stub - (a + 5)))

    # Unicorn caches translated code, so a stub patched with a new constant
    # keeps returning the old one: the stubs read their value from memory.
    RANDVAL, MASKVAL = 0x30000c00, 0x30000c04

    def set_rand(self, v):
        if not getattr(self, '_rand_patched', False):
            self.patch(0x470120, b'\xa1' + struct.pack('<I', self.RANDVAL) + b'\xc3')
            self._rand_patched = True
        self.w(self.RANDVAL, v)

    def set_mask(self, v):
        if not getattr(self, '_mask_patched', False):
            self.patch(0x4246c0, b'\xa1' + struct.pack('<I', self.MASKVAL) + b'\xc3')
            self.patch(0x466910, b'\x31\xc0\xc3')
            self.patch(0x425cf0, b'\xc3')
            self._mask_patched = True
        self.w(self.MASKVAL, v)

    def call(self, fn, args, this=None):
        esp = self.STK + 0x80000
        words = [struct.unpack('<I', struct.pack('<f', a))[0] if isinstance(a, float) else a & 0xffffffff
                 for a in args]
        esp -= 4 * len(words)
        for i, x in enumerate(words):
            self.w(esp + 4 * i, x)
        esp -= 4
        self.w(esp, self.STUB + 0xf00)
        self.mu.mem_write(self.STUB + 0xf00, b'\xf4')  # hlt marks the return
        self.mu.reg_write(UC_X86_REG_ESP, esp)
        if this is not None:
            self.mu.reg_write(UC_X86_REG_ECX, this)
        self.mu.emu_start(fn, self.STUB + 0xf00)


# state fields: (port name, address getter, kind)
def fields(o):
    v, ob = o.veh, o.obj
    return [('v_lat', v + 0x6c, 'f'), ('v_y', v + 0x70, 'f'), ('v_fwd', v + 0x74, 'f'),
            ('pitch_w', v + 0x78, 'f'), ('yaw_rate', v + 0x7c, 'f'), ('roll_w', v + 0x80, 'f'),
            ('x', ob + 0x50, 'f'), ('y', ob + 0x54, 'f'), ('z', ob + 0x58, 'f'),
            ('pitch', ob + 0x74, 'f'), ('heading', ob + 0x78, 'f'), ('roll', ob + 0x7c, 'f'),
            ('gear', 0x4a63e0, 'i'), ('rpm', 0x4a63d8, 'f'), ('braking', 0x4a63cc, 'i'),
            ('skid', 0x4a63dc, 'i'), ('stopped', 0x4a63e4, 'b'), ('spin', 0x4a6484, 'f'),
            ('drift', 0x4a6480, 'f'), ('slide', 0x4a6488, 'f'), ('hb_heading', 0x4a6490, 'f'),
            ('hb_flag', 0x4a6494, 'b'), ('handbrake', 0x4d9df8, 'i'), ('hb_forced', 0x4a6498, 'i'),
            ('throttle', 0x4d9cdc, 'f'), ('brake', 0x4d9e08, 'i'), ('steer', 0x4d9cd8, 'f')]


def put(o, d):
    for name, a, k in fields(o):
        val = getattr(d, name)
        if k == 'f':
            o.f(a, val)
        elif k == 'b':
            o.mu.mem_write(a, bytes([val & 0xff]))
        else:
            o.w(a, val)


def get(o):
    out = {}
    for name, a, k in fields(o):
        if k == 'f':
            out[name] = o.rf(a)
        elif k == 'b':
            out[name] = o.mu.mem_read(a, 1)[0]
        else:
            out[name] = struct.unpack('<i', struct.pack('<I', o.r(a)))[0]
    return out


def close(a, b):
    if isinstance(a, int) and isinstance(b, int):
        return a == b
    return abs(a - b) <= 1e-3 * max(1.0, abs(a), abs(b))


def rnd_state(rng, car):
    d = Drive()
    d.v_lat = rng.choice([0.0, rng.uniform(-8, 8)])
    d.v_y = rng.uniform(-3, 3)
    d.v_fwd = rng.choice([0.0, rng.uniform(-14, 70), rng.uniform(-1, 1)])
    d.pitch_w, d.roll_w = rng.uniform(-50, 50), rng.uniform(-50, 50)
    d.yaw_rate = rng.uniform(-2, 2)
    d.x, d.y, d.z = rng.uniform(-9e3, 9e3), rng.uniform(-5, 20), rng.uniform(-9e3, 9e3)
    d.pitch, d.roll = rng.uniform(-5, 5), rng.uniform(-5, 5)
    d.heading = rng.uniform(0, 4095)
    d.gear = rng.randint(0, 4)
    d.rpm = 800.0
    d.braking = rng.randint(0, 1)
    d.skid = rng.randint(0, 10)
    d.stopped = rng.randint(0, 1)
    d.spin = rng.choice([0.0, 0.0, rng.uniform(0, 6)])
    d.handbrake = rng.choice([0, 0, 1])
    d.drift = rng.choice([0.0, rng.uniform(0, 1)]) if d.handbrake else 0.0
    d.slide = 0.0
    d.hb_heading = rng.choice([0.0, rng.uniform(0, 6)])
    d.hb_flag = rng.randint(0, 1)
    d.hb_forced = 0
    d.throttle = rng.choice([0.0, 100.0, rng.uniform(0, 100)])
    d.brake = rng.choice([0, 0, rng.randint(1, 100)])
    d.steer = rng.choice([0.0, rng.uniform(-100, 100)])
    d.rng = 1
    return d


def main():
    o = Orig()
    rng = random.Random(1)
    road = [0.0]

    @HEIGHT_FN
    def height(ud, x, z, out):
        out[0] = road[0]
        return 1

    dt = 1 / 30
    o.f(0x4e4970 + 0xc, dt)
    fails = {}
    names = ['longitudinal', 'steering', 'suspension', 'ride', 'integrate', 'keys', 'step']
    o.set_mask(0)
    for case in range(CASES):
        car_i = rng.randint(0, 6)
        car = car_struct(car_i)
        env = Env(C.pointer(car), rng.randint(0, 1), rng.randint(0, 1), rng.randint(0, 1),
                  rng.randint(0, 1), rng.choice([0, 0, 1]), 0, height, None)
        o.w(0x4d9e2c, car_i)
        o.w(0x4d9ec0, env.motor)
        o.w(0x4d9ed0, env.gearbox)
        o.w(0x4d9ec4, env.tyres)
        o.w(0x4d9ecc, env.abs)
        o.w(0x4d9dfc, env.turbo_active)
        o.w(0x4d9e28, 0)
        rv = rng.choice([0, 3, 7])
        o.set_rand(rv)
        RAND_C.value = rv
        road[0] = rng.uniform(-5, 20)
        o.f(o.hval, road[0])
        for name in names:
            d = rnd_state(rng, car)
            if name == 'longitudinal' and d.gear > (5 if env.gearbox else 4):
                d.gear = 0
            put(o, d)
            before = {f[0]: getattr(d, f[0]) for f in fields(o)}
            thr, brk, st = d.throttle, float(d.brake), d.steer
            keys = rng.randint(0, 15)
            if name == 'longitudinal':
                o.call(0x40f0d0, [0, thr, brk])
                lib.a2_drive_longitudinal(C.byref(d), C.byref(env), thr, brk, dt)
            elif name == 'steering':
                o.call(0x40f3c0, [0, st, thr])
                lib.a2_drive_steering(C.byref(d), C.byref(env), st, thr, dt)
            elif name == 'suspension':
                o.call(0x40ebd0, [0])
                o.call(0x40ece0, [0])
                lib.a2_drive_test_suspension(C.byref(d), C.byref(env), dt)
            elif name == 'ride':
                o.call(0x40ee80, [0])
                lib.a2_drive_test_ride(C.byref(d), C.byref(env), dt)
            elif name == 'integrate':
                o.call(0x4404a0, [dt, 1.0, 0, 0, 0], this=o.veh)
                lib.a2_drive_test_integrate(C.byref(d), dt)
            elif name == 'step':
                mode = rng.choice([0, 0, 0, 8, 6, 3])
                o.w(0x4d9a7c, mode)
                env.forced_handbrake = 1 if mode in (8, 6, 3) else 0
                o.call(0x40fc30, [0, st, thr, brk])
                o.call(0x4404a0, [dt, 1.0, 0, 0, 0], this=o.veh)
                lib.a2_drive_step(C.byref(d), C.byref(env), dt)
                env.forced_handbrake = 0
            elif name == 'keys':
                o.set_mask(keys)
                o.w(0x4d9af0, 0)
                o.call(0x4269c0, [0])
                lib.a2_drive_keys(C.byref(d), keys, dt)
            got = get(o)
            for fname, _, _ in fields(o):
                if name == 'keys' and fname not in ('throttle', 'brake', 'steer'):
                    continue
                a, b = got[fname], getattr(d, fname)
                if not close(a, b):
                    fails.setdefault((name, fname), []).append((case, a, b))
                    if name == 'ride' and len(fails[(name, fname)]) == 1:
                        print(f'  ride case {case}: road {road[0]:.4f} in', {k: round(before[k], 4) for k in ('y', 'v_y', 'v_fwd', 'v_lat', 'heading', 'skid')}, 'car', car_i)
                    if name == 'keys' and len(fails[(name, fname)]) == 1:
                        print(f'  keys case {case}: mask {keys:#x} in thr {thr:.3g} brk {brk:.3g} steer {st:.3g}')
    total = CASES
    for name in names:
        bad = {k: v for k, v in fails.items() if k[0] == name}
        if not bad:
            print(f'{name:13s} OK over {total} random states')
        else:
            for (n, fname), lst in sorted(bad.items()):
                c, a, b = lst[0]
                print(f'{name:13s} {fname:11s} {len(lst):4d}/{total} differ, e.g. case {c}: original {a:.6g} port {b:.6g}')


if __name__ == "__main__":
    main()

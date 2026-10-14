#!/usr/bin/env python3
"""Differential test: the port's new-AI vehicle (port/src/ai_car.c, with
drive.c, built with -DA2_DRIVE_TEST into a shared library) against the
original functions in spel.dat, run under Unicorn on the same random states.

    cc -shared -fPIC -O1 -DA2_DRIVE_TEST -Iport/src port/src/ai_car.c \
       port/src/drive.c -lm -o .work/libaicar.so
    diff_ai_car.py <spel.dat> <CARS.DAT> .work/libaicar.so [cases]

Checked: 0x4014e0 (rubber band), 0x40ff90 (longitudinal, with 0x40eae0),
0x410280 (steering), 0x4012c0 (speed control), 0x401230 (steer to a point),
0x401690 / 0x40fe90 (bearings).  Needs: pip install unicorn
"""
import ctypes as C
import math
import random
import struct
import sys

from unicorn import UC_ARCH_X86, UC_MODE_32, Uc
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP

SPEL, CARSDAT, LIB = sys.argv[1:4]
CASES = int(sys.argv[4]) if len(sys.argv) > 4 else 500


class Car(C.Structure):
    _fields_ = [('f', C.c_float * 37), ('empty', C.c_int)]


DRIVE_F = [(n, C.c_float) for n in ('v_lat', 'v_y', 'v_fwd', 'pitch_w', 'yaw_rate', 'roll_w',
                                    'x', 'y', 'z', 'pitch', 'heading', 'roll')] + [
    ('gear', C.c_int), ('rpm', C.c_float), ('braking', C.c_int), ('skid', C.c_int),
    ('dstopped', C.c_int), ('spin', C.c_float), ('ddrift', C.c_float), ('slide', C.c_float),
    ('hb_heading', C.c_float), ('hb_flag', C.c_int), ('handbrake', C.c_int),
    ('hb_forced', C.c_int), ('dthrottle', C.c_float), ('dbrake', C.c_int),
    ('dsteer', C.c_float), ('rng', C.c_uint)]


class AiCar(C.Structure):
    _fields_ = DRIVE_F + [('steer', C.c_float), ('throttle', C.c_float), ('brake', C.c_float),
                          ('engine_off', C.c_int), ('drift', C.c_float),
                          ('drift_sum', C.c_float), ('drift_head', C.c_float),
                          ('stopped', C.c_int)]


HEIGHT_FN = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_float, C.c_float, C.POINTER(C.c_float))


class Env(C.Structure):
    _fields_ = [('car', C.POINTER(Car)), ('gearbox', C.c_int), ('tyres', C.c_int),
                ('player_skid', C.c_int), ('player_stopped', C.c_int), ('demo', C.c_int),
                ('height', HEIGHT_FN), ('height_ud', C.c_void_p)]


lib = C.CDLL(LIB)
F = C.c_float
P = C.POINTER(AiCar)
lib.a2_ai_rubber_band.restype = F
lib.a2_ai_rubber_band.argtypes = [C.c_int, C.c_int, C.c_int, C.c_int]
lib.a2_ai_longitudinal.argtypes = [P, C.POINTER(Env), F, F, F]
lib.a2_ai_steering.argtypes = [P, C.POINTER(Env), C.c_int, F]
lib.a2_ai_speed_control.argtypes = [P, C.POINTER(F * 3), C.c_int, F]
lib.a2_ai_steer_to.argtypes = [P, C.POINTER(F * 3)]
lib.a2_ai_bearing.argtypes = [P, F, F]
lib.a2_ai_rel_angle.argtypes = [P, F, F]

cars_raw = open(CARSDAT, 'rb').read()


def car_struct(i):
    a = struct.unpack_from('<31f', cars_raw, 124 * i)
    b = struct.unpack_from('<6f', cars_raw, 1240 + 24 * i)
    c = Car()
    for k, v in enumerate(a + b):
        c.f[k] = v
    return c


REC = 0x4a55a0


class Orig:
    HEAP, STK, STUB = 0x10000000, 0x20000000, 0x30000000
    RANDVAL = 0x30000c00

    def __init__(self):
        d = open(SPEL, 'rb').read()
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
        mu.mem_map(self.HEAP, 0x400000)
        mu.mem_map(self.STK, 0x100000)
        mu.mem_map(self.STUB, 0x1000)
        self.mu = mu
        self.veh, self.obj = self.HEAP, self.HEAP + 0x1000
        self.w(0x4e48e0, self.veh)
        self.w(0x4ea9e0, self.obj)
        self.w(self.veh + 0x08, 0)     # object index of vehicle 0
        self.w(self.veh + 0x0c, 0)     # its AI index
        self.prog = self.HEAP + 0x50000
        self.w(0x4a5b40, self.prog)    # t_ai_state array (progress at +0x60)
        for i in range(3):
            self.w(0x485b2c + 0x58 * i, 0)  # racer i's object: vehicle 0
        mu.mem_write(0x4a5ee0, cars_raw[:1240])
        mu.mem_write(0x4a5df0, cars_raw[1240:1480])
        self.patch(0x464ca0, b'\xc3')
        self.patch(0x464e50, b'\xc3')
        self.patch(0x470120, b'\xa1' + struct.pack('<I', self.RANDVAL) + b'\xc3')

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

    def call(self, fn, args):
        esp = self.STK + 0x80000
        words = [struct.unpack('<I', struct.pack('<f', a))[0] if isinstance(a, float) else a & 0xffffffff
                 for a in args]
        esp -= 4 * len(words)
        for i, x in enumerate(words):
            self.w(esp + 4 * i, x)
        esp -= 4
        self.w(esp, self.STUB + 0xf00)
        self.mu.mem_write(self.STUB + 0xf00, b'\xf4')
        self.mu.reg_write(UC_X86_REG_ESP, esp)
        self.mu.emu_start(fn, self.STUB + 0xf00)
        return self.mu.reg_read(UC_X86_REG_EAX)

    def st0(self):
        # read st(0) by running "fstp dword [STUB+0xe00]; hlt"
        self.mu.mem_write(self.STUB + 0xd00, b'\xd9\x1d' + struct.pack('<I', self.STUB + 0xe00) + b'\xf4')
        self.mu.emu_start(self.STUB + 0xd00, self.STUB + 0xd06)
        return self.rf(self.STUB + 0xe00)


def fields(o, i):
    v, ob, r = o.veh, o.obj, REC + 0x60 * i
    return [('v_lat', v + 0x6c, 'f'), ('v_fwd', v + 0x74, 'f'), ('pitch_w', v + 0x78, 'f'),
            ('yaw_rate', v + 0x7c, 'f'), ('x', ob + 0x50, 'f'), ('z', ob + 0x58, 'f'),
            ('heading', ob + 0x78, 'f'), ('roll', ob + 0x7c, 'f'),
            ('steer', r + 0x34, 'f'), ('throttle', r + 0x38, 'f'), ('brake', r + 0x3c, 'f'),
            ('engine_off', r + 0x40, 'i'), ('gear', r + 0x44, 'i'), ('drift', r + 0x48, 'f'),
            ('drift_sum', r + 0x4c, 'f'), ('drift_head', r + 0x50, 'f'), ('stopped', r + 0x58, 'i')]


def put(o, i, c):
    o.w(REC + 0x60 * i, 0)  # the record's object
    for name, a, k in fields(o, i):
        val = getattr(c, name)
        if k == 'f':
            o.f(a, val)
        else:
            o.w(a, val)


def get(o, i):
    out = {}
    for name, a, k in fields(o, i):
        out[name] = o.rf(a) if k == 'f' else struct.unpack('<i', struct.pack('<I', o.r(a)))[0]
    return out


def close(a, b):
    if isinstance(a, int) and isinstance(b, int):
        return a == b
    return abs(a - b) <= 1e-3 * max(1.0, abs(a), abs(b))


def rnd_car(rng, top):
    c = AiCar()
    c.v_lat = rng.choice([0.0, rng.uniform(-8, 8), rng.uniform(-1.5, 1.5)])
    c.v_fwd = rng.choice([0.0, rng.uniform(-14, 70), rng.uniform(-1.5, 1.5), rng.uniform(0, 12)])
    c.pitch_w = rng.uniform(-50, 50)
    c.yaw_rate = rng.uniform(-2, 2)
    c.x, c.z = rng.uniform(-9e3, 9e3), rng.uniform(-9e3, 9e3)
    c.heading = rng.uniform(0, 4095)
    c.roll = rng.uniform(-5, 5)
    c.steer = rng.choice([0.0, rng.uniform(-100, 100)])
    c.throttle = rng.choice([0.0, 100.0, rng.uniform(0, 100)])
    c.brake = rng.choice([0.0, 0.0, rng.uniform(0, 100)])
    c.engine_off = rng.choice([0, 0, 0, 1])
    c.gear = rng.randint(0, top)
    c.drift = rng.choice([0.0, 0.0, rng.uniform(0, 1)])
    c.drift_sum = rng.uniform(-1, 1)
    c.drift_head = rng.choice([0.0, rng.uniform(0, 6)])
    c.stopped = rng.randint(0, 1)
    return c


def main():
    o = Orig()
    rng = random.Random(7)
    dt = 1 / 30
    o.f(0x4e4970 + 0xc, dt)
    fails = {}
    names = ['rubber', 'longitudinal', 'steering', 'speed_control', 'steer_to', 'bearing', 'rel_angle']
    for case in range(CASES):
        car_i = rng.randint(0, 6)
        car = car_struct(car_i)
        env = Env(C.pointer(car), rng.randint(0, 1), rng.randint(0, 1), rng.randint(0, 1),
                  rng.randint(0, 1), rng.choice([0, 0, 1]), HEIGHT_FN(), None)
        o.w(0x4d9e2c, car_i)
        o.w(0x4d9ed0, env.gearbox)
        o.w(0x4d9ec4, env.tyres)
        o.w(0x4a63dc, env.player_skid)
        o.mu.mem_write(0x4a63e4, bytes([env.player_stopped]))
        o.w(0x4d9b20, env.demo)
        diff, etappe = rng.randint(0, 1), rng.randint(0, 6)
        o.w(0x4d9ab0, diff)
        o.w(0x4d9a74, etappe)
        lead = rng.choice([rng.randint(-60, 60), rng.randint(-30, 30), 0, 25, 26, -25, -26])
        o.w(o.prog + 0x60, lead)
        o.w(0x4d9e70, 0)
        for name in names:
            i = rng.choice([0, 1, 2, 3, 5])
            state = rng.choice([0, 3, 4])
            o.w(REC + 0x60 * i + 4, state)
            c = rnd_car(rng, 5 if env.gearbox else 4)
            put(o, i, c)
            p = (F * 3)(c.x + rng.uniform(-900, 900), 0.0, c.z + rng.uniform(-900, 900))
            o.f(o.HEAP + 0x60000, p[0])
            o.f(o.HEAP + 0x60004, p[1])
            o.f(o.HEAP + 0x60008, p[2])
            pp = o.HEAP + 0x60000
            if name == 'rubber':
                o.call(0x4014e0, [i])
                a = o.st0()
                b = lib.a2_ai_rubber_band(i, lead, diff, etappe)
                if not close(a, b):
                    fails.setdefault((name, 'factor'), []).append((case, a, b))
                continue
            if name in ('bearing', 'rel_angle'):
                a = struct.unpack('<i', struct.pack('<I', o.call(0x401690 if name == 'bearing' else 0x40fe90, [0, pp])))[0]
                fn = lib.a2_ai_bearing if name == 'bearing' else lib.a2_ai_rel_angle
                b = fn(C.byref(c), p[0], p[2])
                if abs(a - b) > 1 and not (name == 'bearing' and abs(abs(a - b) - 4096) <= 1):
                    fails.setdefault((name, 'value'), []).append((case, a, b))
                continue
            if name == 'longitudinal':
                o.call(0x40ff90, [i])
                band = lib.a2_ai_rubber_band(i, lead, diff, etappe)
                bonus = 0.1 if i >= 3 and state == 3 else 0.0
                lib.a2_ai_longitudinal(C.byref(c), C.byref(env), band, bonus, dt)
            elif name == 'steering':
                o.call(0x410280, [i])
                lib.a2_ai_steering(C.byref(c), C.byref(env), 1 if i >= 3 else 0, dt)
            elif name == 'speed_control':
                o.call(0x4012c0, [i, pp])
                lib.a2_ai_speed_control(C.byref(c), C.byref(p), 1 if i >= 3 else 0, dt)
            elif name == 'steer_to':
                o.call(0x401230, [i, pp])
                lib.a2_ai_steer_to(C.byref(c), C.byref(p))
            got = get(o, i)
            for fname, _, _ in fields(o, i):
                a, b = got[fname], getattr(c, fname)
                if not close(a, b):
                    fails.setdefault((name, fname), []).append((case, a, b))
    for name in names:
        bad = {k: v for k, v in fails.items() if k[0] == name}
        if not bad:
            print(f'{name:14s} OK over {CASES} random states')
        else:
            for (n, fname), lst in sorted(bad.items()):
                c, a, b = lst[0]
                print(f'{name:14s} {fname:11s} {len(lst):4d}/{CASES} differ, e.g. case {c}: original {a:.6g} port {b:.6g}')


if __name__ == "__main__":
    main()

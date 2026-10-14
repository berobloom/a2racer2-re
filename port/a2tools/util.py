"""Shared helpers for a2tools (pure stdlib)."""

import struct

u16 = struct.Struct("<H")
u32 = struct.Struct("<I")
f32 = struct.Struct("<f")


def rd_u16(b, o):
    return u16.unpack_from(b, o)[0]


def rd_u32(b, o):
    return u32.unpack_from(b, o)[0]


def rd_f32(b, o):
    return f32.unpack_from(b, o)[0]


def floats(b, o, n):
    return struct.unpack_from(f"<{n}f", b, o)


def u32s(b, o, n):
    return struct.unpack_from(f"<{n}I", b, o)


def is_finite_float_bits(x):
    e = (x >> 23) & 0xFF
    return not (e == 0xFF and (x & 0x7FFFFF) != 0)  # exclude NaN


def float_plausible(x):
    """Heuristic: world-coordinate-ish float (normal number, not denormal junk)."""
    e = (x >> 23) & 0xFF
    if e == 0:
        return x & 0x7FFFFF == 0  # exact zero ok, denormals not
    return e != 0xFF

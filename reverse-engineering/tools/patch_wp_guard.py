#!/usr/bin/env python3
"""Write a copy of spel.dat that does not read waypoint -1 in 0x40b650.

  patch_wp_guard.py <original spel.dat> <output>

0x40b650 (called only from the traffic recycler 0x40b480, at 0x40b52e) asks
whether a waypoint is free.  It reads the obstacle list (+0x24) of the
waypoint's next (+0x14) and previous (+0x20) links first WITHOUT testing them
for -1 (0x40b6bc, 0x40b6d1), and only then again with the test (0x40b6df,
0x40b6f8).  A lane-start waypoint has previous = -1 (etappe2 has 76 of them),
so the first read lands 0x2c bytes before the waypoint array.  Under Wine the
array (0x4e6628) starts 0x24 bytes into a fresh mapping, the page before is
unmapped, and the game dies with "Unhandled exception!" (open question 55).

The patch turns each unguarded read into "if the link is -1, return 0 (not
free)", the result a non-zero word read from before the array would give on
the original heap (SPECULATIVE: what Windows 9x had there is not known).  The
guarded re-checks then do the real test.  Only for the Wine test copy; the
original binaries stay untouched.
"""
import struct
import sys

PATCHES = {
    # 0x40b6bc, 18 bytes: lea/shl/mov/test/jne near -> cmp eax,-1; je 0x40b75d
    0x40B6BC: (bytes.fromhex("8d3480c1e6048b7c162485ff0f858f000000"),
               bytes.fromhex("83f8ff0f8498000000") + b"\x90" * 9),
    # 0x40b6d1, 14 bytes: lea/shl/mov/test/jne short -> cmp ecx,-1; je 0x40b75d
    0x40B6D1: (bytes.fromhex("8d3489c1e6048b7c162485ff757e"),
               bytes.fromhex("83f9ff0f8483000000") + b"\x90" * 5),
}


def va_to_off(d, va):
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    ns = struct.unpack_from("<H", d, pe + 6)[0]
    osz = struct.unpack_from("<H", d, pe + 20)[0]
    base = struct.unpack_from("<I", d, pe + 52)[0]
    for i in range(ns):
        o = pe + 24 + osz + 40 * i
        vs, sva, rs, ra = struct.unpack_from("<IIII", d, o + 8)
        if base + sva <= va < base + sva + rs:
            return va - base - sva + ra
    raise ValueError(hex(va))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    d = bytearray(open(sys.argv[1], "rb").read())
    for va, (old, new) in PATCHES.items():
        assert len(old) == len(new)
        off = va_to_off(d, va)
        if d[off:off + len(old)] != old:
            sys.exit("unexpected bytes at %#x" % va)
        d[off:off + len(new)] = new
    with open(sys.argv[2], "xb") as f:
        f.write(d)


if __name__ == "__main__":
    main()

"""Minimal PNG writers (stdlib only): indexed and truecolour."""

import struct
import zlib


def _chunk(tag, payload):
    c = struct.pack(">I", len(payload)) + tag + payload
    return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)


def _png(path, w, h, bitdepth, colortype, raw, extra_chunks=()):
    out = b"\x89PNG\r\n\x1a\n"
    out += _chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, bitdepth, colortype, 0, 0, 0))
    for tag, payload in extra_chunks:
        out += _chunk(tag, payload)
    out += _chunk(b"IDAT", zlib.compress(raw, 6)) + _chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)


def write_png_indexed(path, w, h, rows, palette):
    """rows: list of `bytes` of palette indices.  palette: list of (r,g,b)."""
    raw = b"".join(b"\x00" + row for row in rows)
    plte = b"".join(bytes(c) for c in palette)
    _png(path, w, h, 8, 3, raw, [(b"PLTE", plte)])


def write_png_rgb(path, w, h, rows, alpha=False):
    """rows: list of `bytes`, each w*3 (RGB) or w*4 (RGBA)."""
    raw = b"".join(b"\x00" + row for row in rows)
    _png(path, w, h, 8, 6 if alpha else 2, raw)

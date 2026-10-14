"""PCX reader + minimal PNG writer (palette images)."""

import struct
import zlib


def load_pcx(data):
    if data[0] != 0x0A:
        raise ValueError("not a PCX file")
    version = data[1]
    encoding = data[2]
    bpp = data[3]
    xmin, ymin, xmax, ymax = struct.unpack_from("<4H", data, 4)
    w, h = xmax - xmin + 1, ymax - ymin + 1
    planes = data[65]
    bpl = struct.unpack_from("<H", data, 66)[0]
    pal_ofs = len(data) - 769
    palette = None
    if data[pal_ofs] == 0x0C:
        pal = data[pal_ofs + 1:pal_ofs + 769]
        palette = [tuple(pal[i:i + 3]) for i in range(0, 768, 3)]
    raw = data[128:]
    pixels = []
    if encoding == 1:  # RLE
        pos = 0
        buf = bytearray()
        while len(buf) < bpl * h * planes:
            if pos >= len(raw):
                break
            c = raw[pos]
            pos += 1
            if (c & 0xC0) == 0xC0:
                cnt = c & 0x3F
                val = raw[pos]
                pos += 1
                buf.extend(bytes([val]) * cnt)
            else:
                buf.append(c)
        if bpp == 8:
            pixels = [bytes(buf[p * bpl:(p + 1) * bpl][:w]) for p in range(h)]
    else:
        raise ValueError(f"unsupported PCX encoding {encoding}")
    return {
        "version": version, "bpp": bpp, "planes": planes,
        "width": w, "height": h, "palette": palette, "pixels": pixels,
    }


def write_png(path, w, h, pixels, palette):
    """pixels: list of rows (bytes, palette indices)."""
    def chunk(tag, payload):
        c = struct.pack(">I", len(payload)) + tag + payload
        return c + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + row for row in pixels)
    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0)
    plte = b"".join(bytes(c) for c in palette)
    out = sig + chunk(b"IHDR", ihdr) + chunk(b"PLTE", plte)
    out += chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(out)

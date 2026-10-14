"""Skelet2D sprite entries (the RC.IMG / MENURC.IMG archives).

These two archives use the *binary* IND index (u16 count + count x u32 offsets;
the IMG repeats the count in its first u16, which is why entry 0 starts at
offset 2).  Each entry is a single 8-bit paletted sprite:

    u16 width;
    u16 height;
    u16 anchor_x;      // usually width/2
    u16 anchor_y;      // usually height/2
    u8  pixels[width * height];

CONFIRMED: `8 + width*height` equals the entry size for **all** 453 RC.IMG and
482 MENURC.IMG entries.  The anchor is the exact centre for 322/453 and 185/482
respectively, so it is a hotspot rather than a derived value.

Index 0xFF is the transparent key (long 0xFF runs pad the corners of rotated
car sprites).  The palette is external -- the game loads a per-track palette
(Raser.ini [PALET] ETAPPE0..6 -> e0x_pal.pcx) or a menu palette, so a sprite
must be paired with one to be displayed.
"""

import struct

HEADER_LEN = 8
TRANSPARENT = 0xFF


def parse(data, offset=0, size=None):
    """Decode one sprite entry starting at `offset`."""
    w, h, ax, ay = struct.unpack_from("<4H", data, offset)
    need = HEADER_LEN + w * h
    if size is not None and size != need:
        raise ValueError(f"sprite: entry size {size} != {need} for {w}x{h}")
    px = data[offset + HEADER_LEN:offset + need]
    if len(px) != w * h:
        raise ValueError(f"sprite: truncated ({len(px)} of {w * h})")
    rows = [px[y * w:(y + 1) * w] for y in range(h)]
    return {"width": w, "height": h, "anchor": (ax, ay), "rows": rows}


def parse_archive(img, offsets):
    """Decode every entry given the offsets from the binary IND."""
    out = []
    for i, o in enumerate(offsets):
        end = offsets[i + 1] if i + 1 < len(offsets) else len(img)
        out.append(parse(img, o, end - o))
    return out


def to_rgba_rows(sprite, palette, transparent=TRANSPARENT):
    """Flatten to RGBA rows using a 256-entry [(r,g,b), ...] palette."""
    rows = []
    for row in sprite["rows"]:
        out = bytearray()
        for idx in row:
            if idx == transparent:
                out += b"\0\0\0\0"
            else:
                r, g, b = palette[idx]
                out += bytes((r, g, b, 255))
        rows.append(bytes(out))
    return rows

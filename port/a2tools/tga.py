"""Truevision TGA reader for the textures inside RCS.IMG / Mrcs.img.

Every TGA shipped with the game is an **uncompressed true-colour** image
(header type 2, no colour map): 105 entries are 24 bpp and one is 32 bpp, sized
32x32 up to 640x480.  Most carry the standard 26-byte TGA 2.0 footer
("TRUEVISION-XFILE."), which is why the payload is usually 26 bytes short of the
file length.

The game reads these through `winlib` ("Reading TGA file" / "creating surface
%d,%d" in debug.txt) and uploads them as Direct3D textures in the 5:6:5 format
it selects at startup (`TXF_16BPP_RGB selected = 3`).

Type 10 (RLE true-colour) is also decoded here for safety, though no shipped
asset uses it.
"""

import struct

FOOTER_MAGIC = b"TRUEVISION-XFILE."


class TGAError(ValueError):
    pass


def parse(data, name=None):
    """Return {"width","height","bpp","rows"} with rows top-down, RGB or RGBA."""
    if len(data) < 18:
        raise TGAError(f"{name or '<tga>'}: too short")
    idlen = data[0]
    cmaptype = data[1]
    imgtype = data[2]
    cmaplen, cmapbits = struct.unpack_from("<HB", data, 5)
    w, h, bpp, desc = struct.unpack_from("<HHBB", data, 12)

    if cmaptype != 0 or imgtype not in (2, 10):
        raise TGAError(f"{name or '<tga>'}: unsupported type {imgtype} cmap {cmaptype}")
    if bpp not in (24, 32):
        raise TGAError(f"{name or '<tga>'}: unsupported depth {bpp}")

    o = 18 + idlen + (cmaplen * cmapbits // 8)
    nchan = bpp // 8
    npx = w * h

    if imgtype == 2:
        need = npx * nchan
        if len(data) - o < need:
            raise TGAError(f"{name or '<tga>'}: truncated ({len(data) - o} < {need})")
        pixels = data[o:o + need]
    else:
        pixels = _unrle(data, o, npx, nchan, name)

    # TGA stores BGR(A); bit 5 of desc set means the first row is the top one
    top_down = bool(desc & 0x20)
    stride = w * nchan
    rows = []
    for y in range(h):
        src = y if top_down else (h - 1 - y)
        row = bytearray(pixels[src * stride:(src + 1) * stride])
        row[0::nchan], row[2::nchan] = row[2::nchan], row[0::nchan]  # BGR -> RGB
        rows.append(bytes(row))
    return {"width": w, "height": h, "bpp": bpp, "rows": rows}


def _unrle(data, o, npx, nchan, name):
    out = bytearray()
    while len(out) < npx * nchan:
        if o >= len(data):
            raise TGAError(f"{name or '<tga>'}: truncated RLE")
        pkt = data[o]
        o += 1
        count = (pkt & 0x7F) + 1
        if pkt & 0x80:
            out += data[o:o + nchan] * count
            o += nchan
        else:
            out += data[o:o + nchan * count]
            o += nchan * count
    return bytes(out[:npx * nchan])


def looks_like_tga(data):
    return (len(data) >= 18 and data[1] == 0 and data[2] in (2, 10)
            and struct.unpack_from("<HH", data, 12)[0] > 0)

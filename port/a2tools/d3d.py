"""`.d3d` mesh reader (Davilex dplib mesh format).

Layout is CONFIRMED against the loader in spel.dat (`MESH.CPP`, fcn 0x455B50)
and verified over all 447 `.d3d` entries in RCS.IMG:

    u32 vertexCount;
    u32 triangleCount;
    struct { float x, y, z, nx, ny, nz, tu, tv; u8 pad; } vertices[vertexCount];
    struct { u16 a, b, c;                       u8 pad[3]; } triangles[triangleCount];

The loader reads 0x21 (33) bytes per vertex and copies only the first 32 with
`rep movsd ecx=8`; it reads 9 bytes per triangle and stores only the three
leading u16 into a stride-6 WORD index buffer.  **The trailing bytes are read
and discarded.**  They are uninitialised exporter padding: constant within a
model, arbitrary across models (the same habit that left garbage in the IND name
buffers).  Do not attribute meaning to them.

The 8 floats are exactly a D3DVERTEX.  At load time the mesh's type selects:

  type 1 -> D3DVERTEX  : all 8 floats copied through (position, normal, uv)
  type 2 -> D3DLVERTEX : position and uv copied; normals dropped; color forced
                         to 0x00FFFFFF and specular to 0
                         (D3DLVERTEX: x,y,z,reserved,color,specular,tu,tv)

Vertex winding is as stored; indices are triangle-list order and are always
within [0, vertexCount).  See reverse-engineering/file-formats.md.
"""

import struct

HEADER_LEN = 8
VERTEX_LEN = 33
TRIANGLE_LEN = 9

_VERTEX = struct.Struct("<8f")
_TRI = struct.Struct("<3H")


class D3DError(ValueError):
    pass


def expected_size(vertex_count, triangle_count):
    return HEADER_LEN + vertex_count * VERTEX_LEN + triangle_count * TRIANGLE_LEN


def parse(data, name=None, lenient=True):
    """Parse a .d3d blob.

    Returns {"vertices": [(x,y,z,nx,ny,nz,tu,tv), ...],
             "triangles": [(a,b,c), ...],
             "vertex_pad": int, "triangle_pad": (int,int,int),
             "offset": int}

    `offset` is normally 0.  One archive entry (RCS.IMG `E04_koor.D3D`) has an
    index offset 24 bytes too low, so its blob starts with stray leading bytes;
    with lenient=True the header is re-located by finding the shift whose counts
    account for the remaining bytes exactly.
    """
    base = 0
    if len(data) < HEADER_LEN:
        raise D3DError(f"{name or '<d3d>'}: too short ({len(data)} bytes)")
    v, t = struct.unpack_from("<II", data, 0)
    if expected_size(v, t) != len(data):
        base = _relocate(data) if lenient else None
        if base is None:
            raise D3DError(
                f"{name or '<d3d>'}: size {len(data)} != expected "
                f"{expected_size(v, t)} for counts ({v}, {t})")
        v, t = struct.unpack_from("<II", data, base)

    vo = base + HEADER_LEN
    vertices = [_VERTEX.unpack_from(data, vo + i * VERTEX_LEN) for i in range(v)]
    to = vo + v * VERTEX_LEN
    triangles = [_TRI.unpack_from(data, to + i * TRIANGLE_LEN) for i in range(t)]

    for a, b, c in triangles:
        if max(a, b, c) >= v:
            raise D3DError(f"{name or '<d3d>'}: index {max(a, b, c)} >= vertex count {v}")

    return {
        "vertices": vertices,
        "triangles": triangles,
        "vertex_pad": data[vo + 32] if v else None,
        "triangle_pad": tuple(data[to + 6:to + 9]) if t else None,
        "offset": base,
    }


def _relocate(data):
    """Find a leading shift whose header accounts for the rest of the blob."""
    for base in range(0, min(64, len(data) - HEADER_LEN) + 1, 4):
        v, t = struct.unpack_from("<II", data, base)
        if v and t and base + expected_size(v, t) == len(data):
            return base
    return None


def bounds(mesh):
    xs = [v[0] for v in mesh["vertices"]]
    ys = [v[1] for v in mesh["vertices"]]
    zs = [v[2] for v in mesh["vertices"]]
    if not xs:
        return None
    return (min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs))


def to_obj(mesh, name="mesh", mtllib=None, material=None):
    """Wavefront OBJ text.  OBJ indices are 1-based; v/vt/vn share numbering."""
    out = []
    if mtllib:
        out.append(f"mtllib {mtllib}")
    out.append(f"o {name}")
    for x, y, z, _nx, _ny, _nz, _u, _v in mesh["vertices"]:
        out.append(f"v {x:.6g} {y:.6g} {z:.6g}")
    for _x, _y, _z, _nx, _ny, _nz, u, v in mesh["vertices"]:
        # OBJ's V axis points up, D3D texture space points down
        out.append(f"vt {u:.6g} {1.0 - v:.6g}")
    for _x, _y, _z, nx, ny, nz, _u, _v in mesh["vertices"]:
        out.append(f"vn {nx:.6g} {ny:.6g} {nz:.6g}")
    if material:
        out.append(f"usemtl {material}")
    for a, b, c in mesh["triangles"]:
        a, b, c = a + 1, b + 1, c + 1
        out.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
    return "\n".join(out) + "\n"

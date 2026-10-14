"""Track file parsers (etappe*.rmp/.pmp/.cmp/.dmp/.omp/.pth).

All five map formats are CONFIRMED: their structure is taken from the loaders in
spel.dat (`rd_chunk.cpp`, `rd_main.cpp`) and every one of the 7 tracks parses
with **byte-exact** consumption.  Element counts were cross-checked against the
game's own runtime allocations (Raser.ini DEBUGON=1):

    ETAPPE5:  g_rails[3364] == .cmp count
              g_driver_map[4911] == .dmp count
              m_chunks/g_chunks[810] == .pmp count == .rmp chunk count
              503 "Loading static object" lines == .omp count

Spec and evidence: reverse-engineering/file-formats.md.

A track is a chain of *chunks* (778-840 of them).  Per chunk:

  .pmp  world position, plus the indices of the rails (.cmp) and left/right
        driver-map waypoints (.dmp) belonging to that chunk
  .rmp  the actual road geometry: four lists of 104-byte "planes" (quads)
  .omp  static scenery objects, each tagged with its chunk

The rail indices across all chunks partition .cmp exactly, and the left+right
waypoint indices partition .dmp exactly.
"""

import struct

from util import rd_u32, floats, u32s

# ---------------------------------------------------------------- .rmp

PLANE_LEN = 104          # 4 vertices x 20 B + 2 triangles x 12 B
RMP_LISTS = 4            # road-planes, land-planes, + two more (see below)
# per-list maxima asserted by rd_chunk.cpp
RMP_LIST_MAX = (16, 24, 12, None)
RMP_LIST_NAMES = ("road", "land", "list2", "list3")


def load_rmp(data):
    """Road mesh.

        u32 plane_count_hint;       // see note below
        u32 chunk_count;
        chunk[chunk_count] {
            u32 n_road;  plane road[n_road];    // rd_chunk.cpp max 16
            u32 n_land;  plane land[n_land];    // max 24
            u32 n2;      plane l2[n2];          // max 12
            u32 n3;      plane l3[n3];
        }
        plane {                                  // 104 bytes
            struct { float x, y, z, u, v; } v[4];
            u32 tri[2][3];                       // indices 0..3, local to plane
        }

    Returns (hint, chunk_count, chunks) where each chunk is a list of 4 lists of
    planes, and a plane is {"vertices": [(x,y,z,u,v) x4], "triangles": [(a,b,c) x2]}.

    Note: `plane_count_hint` equals the true total plane count for etappe2 and
    etappe4 only; for the other tracks it is a few hundred low.  It is not used
    for parsing -- treat it as a stale authoring figure.
    """
    hint, chunk_count = struct.unpack_from("<2I", data, 0)
    o = 8
    chunks = []
    for _ in range(chunk_count):
        lists = []
        for _l in range(RMP_LISTS):
            n = rd_u32(data, o)
            o += 4
            planes = []
            for _p in range(n):
                vs = [floats(data, o + k * 20, 5) for k in range(4)]
                ts = [u32s(data, o + 80 + k * 12, 3) for k in range(2)]
                planes.append({"vertices": vs, "triangles": ts})
                o += PLANE_LEN
            lists.append(planes)
        chunks.append(lists)
    if o != len(data):
        raise ValueError(f"rmp: consumed {o} of {len(data)} bytes")
    return hint, chunk_count, chunks


def rmp_to_obj(chunks, which=None, name="road"):
    """Wavefront OBJ of the road geometry.

    `which` selects list indices to emit (default: all four).
    Plane vertices are emitted as-is; each plane contributes its own 4 vertices,
    so vertices are duplicated between neighbouring planes (as in the file).
    """
    sel = range(RMP_LISTS) if which is None else which
    out = [f"o {name}"]
    vt = []
    faces = []
    base = 0
    for lists in chunks:
        for li in sel:
            for pl in lists[li]:
                for x, y, z, u, v in pl["vertices"]:
                    out.append(f"v {x:.6g} {y:.6g} {z:.6g}")
                    vt.append(f"vt {u:.6g} {1.0 - v:.6g}")
                for a, b, c in pl["triangles"]:
                    faces.append((base + a + 1, base + b + 1, base + c + 1))
                base += 4
    out.extend(vt)
    for a, b, c in faces:
        out.append(f"f {a}/{a} {b}/{b} {c}/{c}")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------- .pmp

PMP_WP_MAX = 12          # rd_chunk.cpp "chunk waypoint left overflow"


def load_pmp(data):
    """Chunk table.

    trigger0/trigger1 are the per-chunk trigger slots (stored at chunk+0xEC
    and +0xF0 at runtime).  Known values:

        0           NO_TRIGGER
        2           TRIGGER_BENZINE_STATION  (sits on the fuel pump mesh)
        4,5,6,8     speed trigger
        9           TRIGGER_ZERO_CHUNK_MARKER (circuits, always chunk 0)
        13          start line (LIKELY; exactly one per track)
        14,15       TRIGGER_FINISH_POSITION / TRIGGER_FINISH_VALID
        18-21,40-43 lane trigger
        45          checkpoint (1:1 with the A_CHECK* gantries, stages only)

    46 = FUEL_APPROACH and 47 = FINAL_APPROACH are handled by
    check_racer_trigger but appear in no track file: they are raised at runtime.
    Values 1, 22-25 and 31-34 are still unidentified.
    See research/discoveries.md.

        u32 chunk_count;
        chunk[chunk_count] {
            float x, y, z;        // chunk world position
            u32   trigger0, trigger1;  // per-chunk AI trigger slots
            u32 n_rails;    u32 rail[n_rails];      // indices into .cmp
            u32 n_wp_left;  u32 wp_left[n_wp_left];   // indices into .dmp, <=12
            u32 n_wp_right; u32 wp_right[n_wp_right]; // indices into .dmp, <=12
        }

    Returns (count, chunks) with each chunk a dict.
    """
    count = rd_u32(data, 0)
    o = 4
    chunks = []
    for _ in range(count):
        x, y, z = floats(data, o, 3)
        trig0, trig1 = u32s(data, o + 12, 2)
        o += 20
        lists = []
        for _l in range(3):
            n = rd_u32(data, o)
            o += 4
            lists.append(list(u32s(data, o, n)))
            o += 4 * n
        chunks.append({"pos": (x, y, z), "trigger0": trig0, "trigger1": trig1,
                       "rails": lists[0], "wp_left": lists[1], "wp_right": lists[2]})
    if o != len(data):
        raise ValueError(f"pmp: consumed {o} of {len(data)} bytes")
    return count, chunks


# ---------------------------------------------------------------- .cmp / .dmp / .omp

def load_cmp(data):
    """Rails (`c_rail g_rails`): u32 count, then count x 24 B = 6 floats.

    Referenced by the per-chunk rail index lists in .pmp, which partition this
    array exactly.  The 6 floats are two 3D points (rail segment start/end) or a
    point plus extent -- not yet distinguished.
    """
    n = rd_u32(data, 0)
    recs = [floats(data, 4 + i * 24, 6) for i in range(n)]
    if 4 + n * 24 != len(data):
        raise ValueError(f"cmp: count {n} does not fit {len(data)} bytes")
    return n, recs


def load_dmp(data):
    """Driver map / waypoints (`c_waypoint g_driver_map`): u32 count, then
    count x 36 B = 9 dwords:

        u32   type;                 // 0/2/4, sometimes with bit 7 (128) set
        u32   field1;               // ~always 0 (4 non-zero in all 7 tracks)
        float x, y, z;              // world position
        i32   links[4];             // waypoint indices, -1 = none

    CONFIRMED (session 7): the dword that earlier notes called "field3" is the
    waypoint's **y**, so the three coordinates are in natural order.  It equals
    the owning chunk's y for **28452 of 28461 waypoints** across the 7 tracks,
    and the nine that differ are waypoints genuinely off the chunk centre --
    among them the fuel-station slip road on etappe1 -- which is exactly what a
    per-waypoint height is for.  It tracks bridges: on etappe0 chunks 411-419 it
    runs 1, 2, 4, 6, 7, 6, 4, 2, 1.

    This is the AI waypoint graph (ai_wmap), not a "draw map" as earlier
    documents guessed; the .pmp left/right waypoint lists index into it and
    partition it exactly.
    """
    n = rd_u32(data, 0)
    if 4 + n * 36 != len(data):
        raise ValueError(f"dmp: count {n} does not fit {len(data)} bytes")
    recs = []
    for i in range(n):
        o = 4 + i * 36
        raw = u32s(data, o, 9)
        recs.append({
            "type": raw[0],
            "field1": raw[1],
            "x": struct.unpack("<f", struct.pack("<I", raw[2]))[0],
            "y": struct.unpack("<f", struct.pack("<I", raw[3]))[0],
            "z": struct.unpack("<f", struct.pack("<I", raw[4]))[0],
            "links": [l if l != 0xFFFFFFFF else -1 for l in raw[5:9]],
        })
    return n, recs


OMP_LEN = 68


def load_omp(data):
    """Static objects: u32 count, then count x 68 B:

        i32   chunk;          // owning chunk index
        i32   type;           // 0 is by far the most common
        float x, y, z;        // world position
        float rot_x, rot_y, rot_z;   // degrees; only rot_y is ever used
        char  anim_name[36];  // e.g. "A_LNTRN0_ANIM", resolved to an anim index

    Field meanings confirmed against the game's own log lines
    ("chunk 46 / type 0 / position (...) / animname A_LNTRN0_ANIM").

    Note: etappe2.omp and etappe6.omp declare one record more than the file
    actually contains; the count is clamped.
    """
    n = rd_u32(data, 0)
    avail = (len(data) - 4) // OMP_LEN
    short = max(0, n - avail)
    n = min(n, avail)
    recs = []
    for i in range(n):
        o = 4 + i * OMP_LEN
        chunk, typ = struct.unpack_from("<2i", data, o)
        x, y, z, rx, ry, rz = floats(data, o + 8, 6)
        name = data[o + 32:o + OMP_LEN].split(b"\0")[0].decode("latin1")
        recs.append({"chunk": chunk, "type": typ, "pos": (x, y, z),
                     "rot": (rx, ry, rz), "name": name})
    return n, recs, short


# ---------------------------------------------------------------- .pth

def load_pth(data):
    """Camera paths (`pth_main.cpp`).  CONFIRMED: all 7 tracks parse byte-exactly.

        i32 version;              // must be -1; the loader rejects anything else
        u32 path_count;
        path[path_count] {
            u32 first_chunk;      // the two bracket the stretch of road this
            u32 last_chunk;       //   path covers (0 = unset, seen twice)
            u32 f2, f3, f4;       // unidentified; f4 is a round 5000..50000
            u32 point_count;
            point[point_count] {
                u32   flag;       // always 0 in the shipped tracks
                float x, y, z;
            }
        }

    The loader is fcn.00433710 (`etappe%d.pth`, opened "r+b"), which reads the
    version, then the count into 0x4E47C0, then six dwords per record into
    0x4E0710+ before the points.  Used by the helicopter camera (`pth_heli.cpp`).

    Returns (version, [ {first_chunk, last_chunk, f2, f3, f4, points} ]).
    """
    version, count = struct.unpack_from("<iI", data, 0)
    o = 8
    paths = []
    for _ in range(count):
        h = u32s(data, o, 6)
        o += 24
        pts = []
        for _k in range(h[5]):
            flag = rd_u32(data, o)
            x, y, z = floats(data, o + 4, 3)
            pts.append({"flag": flag, "pos": (x, y, z)})
            o += 16
        paths.append({"first_chunk": h[0], "last_chunk": h[1],
                      "f2": h[2], "f3": h[3], "f4": h[4], "points": pts})
    if o != len(data):
        raise ValueError(f"pth: consumed {o} of {len(data)} bytes")
    return version, paths

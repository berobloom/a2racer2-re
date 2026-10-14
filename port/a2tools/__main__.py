"""Track/asset dump CLI.

Usage:
  python3 a2tools tracks <file.rmp|.pmp|.cmp|.dmp|.omp|.pth> [--json] [-o road.obj]
  python3 a2tools img <file.IMG> <file.IND>          list archive entries
  python3 a2tools extract <file.IMG> <file.IND> <outdir>
  python3 a2tools pcx <file.pcx> [<out.png>]
  python3 a2tools tga <file.tga> [<out.png>]
  python3 a2tools sprites <RC.IMG> <RC.IND> <palette.pcx> <outdir>
  python3 a2tools d3d <file.d3d> [-o out.obj]        mesh info / OBJ export
  python3 a2tools cars <CARS.DAT>
"""

import collections
import json
import os
import signal
import struct
import sys

import archives
import cars as carsmod
import d3d as d3dmod
import image
import pcx as pcxmod
import sprite as spritemod
import tga as tgamod
import track
from util import rd_u32, floats

EXT = "A2TOOLS_DATA"


def find_data_dir():
    d = os.environ.get(EXT)
    if d:
        return d
    for cand in ("/tmp/opencode/a2/cd/data", "data", os.path.join(os.path.dirname(__file__), "..", "data")):
        if os.path.isdir(cand):
            return cand
    return "."


def p(path):
    if os.path.isabs(path):
        return path
    return os.path.join(find_data_dir(), path)


def f32s(v):
    out = []
    for x in v:
        if isinstance(x, int):
            x = struct.unpack("<f", struct.pack("<I", x))[0]
        out.append(round(x, 2))
    return out


def dump_text(out):
    """Human-readable dump of a parsed-file dict."""
    for k, v in out.items():
        if isinstance(v, list):
            print(f"{k}: {len(v)} entries")
            for i, item in enumerate(v[:40]):
                print(f"  [{i:4d}] {item}")
            if len(v) > 40:
                print(f"  ... {len(v) - 40} more")
        elif isinstance(v, dict):
            print(f"{k}:")
            for kk, vv in v.items():
                print(f"  {kk}: {vv}")
        else:
            print(f"{k}: {v}")


def cmd_tracks(path, as_json=False, obj=None):
    path = p(path)
    ext = os.path.splitext(path)[1].lower()
    data = open(path, "rb").read()
    out = {"file": path, "size": len(data)}
    if ext == ".omp":
        n, recs, short = track.load_omp(data)
        out.update(format="omp", count=n)
        if short:
            out["declared_extra"] = short
        out["types"] = dict(sorted(collections.Counter(r["type"] for r in recs).items()))
        out["records"] = [{"chunk": r["chunk"], "type": r["type"],
                           "pos": f32s(r["pos"]), "rot_y": round(r["rot"][1], 3),
                           "name": r["name"]} for r in recs[:40]]
    elif ext == ".cmp":
        n, recs = track.load_cmp(data)
        out.update(format="cmp (rails)", count=n)
        out["records"] = [f32s(f) for f in recs[:40]]
    elif ext == ".dmp":
        n, recs = track.load_dmp(data)
        out.update(format="dmp (driver map / waypoints)", count=n)
        out["records"] = recs[:40]
    elif ext == ".pmp":
        n, chunks = track.load_pmp(data)
        out.update(format="pmp (chunk table)", chunks=n)
        out["total_rails"] = sum(len(c["rails"]) for c in chunks)
        out["total_wp"] = sum(len(c["wp_left"]) + len(c["wp_right"]) for c in chunks)
        out["records"] = [{"pos": f32s(c["pos"]), "trigger0": c["trigger0"], "trigger1": c["trigger1"],
                           "rails": c["rails"], "wp_left": c["wp_left"],
                           "wp_right": c["wp_right"]} for c in chunks[:8]]
    elif ext == ".rmp":
        hint, n, chunks = track.load_rmp(data)
        counts = [sum(len(c[i]) for c in chunks) for i in range(track.RMP_LISTS)]
        out.update(format="rmp (road mesh)", chunk_count=n, plane_count_hint=hint)
        out["planes_per_list"] = dict(zip(track.RMP_LIST_NAMES, counts))
        out["planes_total"] = sum(counts)
        out["triangles"] = sum(counts) * 2
        out["chunk0_plane0"] = chunks[0][0][0] if chunks[0][0] else None
        if obj:
            with open(obj, "w") as f:
                f.write(track.rmp_to_obj(chunks))
            out["obj"] = obj
    elif ext == ".pth":
        version, paths = track.load_pth(data)
        out.update(format="pth", version=version, paths=len(paths),
                   points=sum(len(p["points"]) for p in paths),
                   path_table=[{"chunks": [p["first_chunk"], p["last_chunk"]],
                                "f2": p["f2"], "f3": p["f3"], "f4": p["f4"],
                                "points": [f32s(q["pos"]) for q in p["points"]]}
                               for p in paths])
    else:
        sys.exit(f"unknown extension {ext}")
    if as_json:
        print(json.dumps(out, indent=1))
    else:
        dump_text(out)


def cmd_img(img, ind):
    img_path, ind_path = p(img), p(ind)
    img_data = open(img_path, "rb").read()
    ind_data = open(ind_path, "rb").read()
    kind = archives.detect_ind(ind_data)
    print(f"{ind_path}: {kind} index, img {len(img_data)} bytes")
    if kind == "binary":
        count, offsets = archives.parse_binary_ind(ind_data)
        print(f"count={count}")
        for i, off in enumerate(offsets):
            end = offsets[i + 1] if i + 1 < count else len(img_data)
            head = img_data[off:off + 16].hex(" ")
            print(f"  [{i:4d}] off={off:8d} size={end - off:8d} head={head}")
    else:
        count, entries = archives.parse_named_ind(ind_data)
        sizes = archives.entry_sizes(entries, len(img_data))
        print(f"count={count}")
        for i, ((name, _raw, off), size) in enumerate(zip(entries, sizes)):
            print(f"  [{i:4d}] {name:24s} off={off:9d} size={size:9d}")


def cmd_extract(img, ind, outdir):
    img_path, ind_path = p(img), p(ind)
    img_data = open(img_path, "rb").read()
    ind_data = open(ind_path, "rb").read()
    os.makedirs(outdir, exist_ok=True)
    kind = archives.detect_ind(ind_data)
    if kind == "binary":
        count, offsets = archives.parse_binary_ind(ind_data)
        entries = [(f"{i:04d}.bin", off) for i, off in enumerate(offsets)]
        ends = offsets[1:] + [len(img_data)]
        entries = [(n, o, e) for (n, o), e in zip(entries, ends)]
    else:
        _count, parsed = archives.parse_named_ind(ind_data)
        ends = [e[2] for e in parsed[1:]] + [len(img_data)]
        entries = [(nm, off, e) for (nm, _f, off), e in zip(parsed, ends)]
    for name, off, end in entries:
        safe = name.replace("/", "_").replace("\\", "_").replace("..", "_") or f"entry_{off:08d}"
        out_path = os.path.join(outdir, safe)
        with open(out_path, "wb") as f:
            f.write(img_data[off:end])
    print(f"extracted {len(entries)} entries to {outdir}")


def cmd_pcx(path, out=None):
    path = p(path)
    data = open(path, "rb").read()
    img = pcxmod.load_pcx(data)
    print(f"{path}: {img['width']}x{img['height']}x{img['bpp']} planes={img['planes']}"
          f" palette={'yes' if img['palette'] else 'no'}")
    if out and img["palette"]:
        pcxmod.write_png(out, img["width"], img["height"], img["pixels"], img["palette"])
        print(f"wrote {out}")


def cmd_tga(path, out=None):
    path = p(path)
    data = open(path, "rb").read()
    img = tgamod.parse(data, name=os.path.basename(path))
    print(f"{path}: {img['width']}x{img['height']}x{img['bpp']}")
    if out:
        image.write_png_rgb(out, img["width"], img["height"], img["rows"],
                            alpha=img["bpp"] == 32)
        print(f"wrote {out}")


def cmd_d3d(path, out=None):
    path = p(path)
    data = open(path, "rb").read()
    name = os.path.splitext(os.path.basename(path))[0]
    mesh = d3dmod.parse(data, name=os.path.basename(path))
    nv, nt = len(mesh["vertices"]), len(mesh["triangles"])
    print(f"{path}: {len(data)} bytes, {nv} vertices, {nt} triangles")
    if mesh["offset"]:
        print(f"  note: header relocated +{mesh['offset']} (malformed archive entry)")
    lo, hi = d3dmod.bounds(mesh)
    print(f"  bounds: ({lo[0]:.3f}, {lo[1]:.3f}, {lo[2]:.3f}) .. ({hi[0]:.3f}, {hi[1]:.3f}, {hi[2]:.3f})")
    us = [v[6] for v in mesh["vertices"]]
    vs = [v[7] for v in mesh["vertices"]]
    print(f"  uv range: u [{min(us):.3f}, {max(us):.3f}]  v [{min(vs):.3f}, {max(vs):.3f}]")
    print(f"  discarded padding: vertex={mesh['vertex_pad']} triangle={mesh['triangle_pad']}")
    if out:
        with open(out, "w") as f:
            f.write(d3dmod.to_obj(mesh, name=name))
        print(f"wrote {out}")


def cmd_sprites(img_path, ind_path, pal_path, outdir):
    img_path, ind_path, pal_path = p(img_path), p(ind_path), p(pal_path)
    img_data = open(img_path, "rb").read()
    ind_data = open(ind_path, "rb").read()
    palette = pcxmod.load_pcx(open(pal_path, "rb").read())["palette"]
    if not palette:
        sys.exit(f"{pal_path} has no palette")
    count, offsets = archives.parse_binary_ind(ind_data)
    sprites = spritemod.parse_archive(img_data, offsets)
    os.makedirs(outdir, exist_ok=True)
    for i, sp in enumerate(sprites):
        image.write_png_rgb(os.path.join(outdir, f"{i:04d}.png"),
                            sp["width"], sp["height"],
                            spritemod.to_rgba_rows(sp, palette), alpha=True)
    print(f"wrote {len(sprites)} sprites to {outdir} (palette {os.path.basename(pal_path)})")


def cmd_cars(path):
    path = p(path)
    data = open(path, "rb").read()
    entries = carsmod.parse(data)
    print(f"{path}: {len(entries)} car slots "
          f"({sum(1 for c in entries if not c['empty'])} populated)")
    for c in entries:
        if c["empty"]:
            print(f"  [{c['slot']}] (empty)")
            continue
        print(f"  [{c['slot']}] {c['name'] or '?'}")
        print(f"       accel        {f32s(c['accel'])}")
        print(f"       accel  (upg) {f32s(c['accel_upgraded'])}")
        print(f"       max m/s      {f32s(c['max_speed'])}"
              f"  -> {carsmod.top_speed_kmh(c):.0f} km/h")
        print(f"       max m/s(upg) {f32s(c['max_speed_upgraded'])}"
              f"  -> {carsmod.top_speed_kmh(c, True):.0f} km/h")
        print(f"       handling     {f32s(c['handling'])}")
        print(f"       block_b      {f32s(c['block_b'])}")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    cmd = argv[1]
    if cmd == "tracks":
        obj = argv[argv.index("-o") + 1] if "-o" in argv else None
        cmd_tracks(argv[2], "--json" in argv, obj)
    elif cmd == "img":
        cmd_img(argv[2], argv[3])
    elif cmd == "extract":
        cmd_extract(argv[2], argv[3], argv[4])
    elif cmd == "pcx":
        cmd_pcx(argv[2], argv[3] if len(argv) > 3 else None)
    elif cmd == "sprites":
        cmd_sprites(argv[2], argv[3], argv[4], argv[5])
    elif cmd == "tga":
        cmd_tga(argv[2], argv[3] if len(argv) > 3 else None)
    elif cmd == "d3d":
        out = argv[argv.index("-o") + 1] if "-o" in argv else None
        cmd_d3d(argv[2], out)
    elif cmd == "cars":
        cmd_cars(argv[2])
    else:
        print(__doc__)
        return 1
    return 0


def _quiet_broken_pipe():
    """Piping into head/less closes stdout early; exit quietly like a unix tool."""
    try:
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    except (AttributeError, ValueError):
        pass


if __name__ == "__main__":
    _quiet_broken_pipe()
    sys.exit(main(sys.argv))

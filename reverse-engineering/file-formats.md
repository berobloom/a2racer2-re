# File formats (initial findings)

All game data lives in `data/`. Work in progress — layouts below are partial, to be verified against the loader code in spel.dat.

## Archives: IND + IMG pairs

Two index formats observed:

### Binary IND → sprite-sheet IMG
- `RC.IND` (1814 B): u16 count=453, then 453×u32 offsets → `RC.IMG` (2,940,883 B). First entry starts at IMG offset 2 (right after the u16 count — the count is repeated in the IMG header).
- `MENURC.IND` (1930 B): count=482 → `MENURC.IMG` (4,608,485 B).
- Entry headers look like u16 sprite descriptions, e.g. entry0 of RC.IMG: `31 00 47 00 1c 00 22 00 ff ff` → (49, 71, 28, 34) + FFFF sentinel, then raw 8-bit palette-indexed pixels (long runs of constant values). Sizes vary (3.5 KB … 17 KB). Hypothesis: 2D sprite sheets (possibly per-car art for menus/radar).

### Named IND → named resource IMG (CONFIRMED, session 5)

```c
u16 count;
struct { char name[20]; u32 offset; } entries[count];   // 24 bytes each
```

- File length is an exact fit: `RCS.IND` 19,346 = 2 + 806*24; `Mrcs.ind`
  2,954 = 2 + 123*24. Offsets are strictly increasing; entry size is implicit
  (next offset − this offset, IMG length for the last entry).
- `name` is NUL-terminated *inside* the 20-byte field. The trailing bytes are
  **uninitialised leftovers of the writer's shared name buffer** — they spell
  fragments of `")*+,-./0123"` and of earlier entries' names (`".D3D"`,
  `"e04_apsl.pcx"`). They mean nothing; do not parse them.
- Verified against the game itself (Raser.ini `DEBUGON=1`): `winlib
  Fileio.cpp(296)` logs `[name] IDX/806 pos OFFSET` per read — 216/216 entries
  match index *and* offset — and the startup directory dump (`Bestand /
  Grootte`) matches 801/801 sizes.
- Anomaly: `RCS.IND[561] E04_koor.D3D` has an offset 24 bytes too low, so its
  blob starts with 24 stray zero bytes.
- `Mrcs.img` (26,603,015 B) = menu resources: PCX images (bedien, hoofd,
  Newgame, PAL, garage, showroom, opties, hiscore), WAV audio (click1-3, speed,
  print, glow, flash, energy, jammin/moody/rock/speed music, Help_*.wav voice),
  IMG fonts (ascii12, cyber, BUTFNT, STDFONT, midfont, SMALLBT, CARVED).
- `RCS.IMG` (34,505,163 B) = race course scenery: 806 entries — 447 `.d3d`
  models plus `.tga`/`.pcx` textures and `.wav` sounds. Entry 0 (WIEL.d3d) is at
  offset 0.

## .d3d — 3D models (inside RCS.IMG) — **FULLY CONFIRMED** (session 5)

```c
u32 vertexCount;
u32 triangleCount;
struct { float x, y, z, nx, ny, nz, tu, tv; u8 pad;    } vertices [vertexCount];  // 33 B
struct { u16 a, b, c;                       u8 pad[3]; } triangles[triangleCount]; // 9 B
```

Confirmed directly from the loader (`MESH.CPP`, `fcn.00455B50` in spel.dat):

- vertex loop: `read(dst, 1, 0x21=33, file)` then `mov ecx,8 / rep movsd` —
  **33 bytes read, only the first 32 used**;
- triangle loop: `read(dst, 1, 9, file)`, then stores `word[+0]`, `word[+2]`,
  `word[+4]` into a destination advanced by `add esi, 6` —
  **9 bytes read, only the first 6 used** (a stride-6 `WORD` index buffer, which
  is exactly the `lpwIndices` later handed to `DrawIndexedPrimitive`).
- The trailing bytes are therefore **dead padding**. Empirically they are
  constant within a model and arbitrary across models (370 models carry
  `vertex_pad=1, tri_pad=(0,7,1)`; 76 carry `vertex_pad=109` with an arbitrary
  pair) — i.e. uninitialised exporter memory, the same habit that left garbage
  in the IND name buffers. **Do not attribute meaning to them.**

The 8 floats are exactly a **`D3DVERTEX`**. The mesh's type field selects how
they are consumed at load time:

| mesh type | destination | behaviour |
|---|---|---|
| 1 | `D3DVERTEX` | all 8 floats copied through (position, normal, uv) |
| 2 | `D3DLVERTEX` | position + uv copied, **normals dropped**, `color` forced to 0x00FFFFFF and `specular` to 0 |

Bounds checks in the loader name the target array "mesh LVERTEX array"
(`mesh.inl` lines 154/166); `mesh.inl(148)` is "mesh has not been initialised".

**Verified:** all 447 `.d3d` entries in RCS.IMG parse, 24,548 vertices and
22,618 triangles, with **zero** triangle indices out of range. Rendering the
decoded geometry produces recognisable models (police car with light bar,
helicopter with tail rotor, Trabant body, bus).

Reader + Wavefront OBJ exporter: `port/a2tools/d3d.py`
(`python3 a2tools/__main__.py d3d <file.d3d> -o out.obj`).

Note `RCS.IMG`'s `E04_koor.D3D` entry has an index offset 24 bytes too low, so
its blob carries 24 stray leading bytes; the reader relocates the header.

## Per-track files — etappe0..6.*  ("etappe" = stage) — **ALL CONFIRMED** (session 5)

Every one of these parses with **byte-exact** consumption on all 7 tracks, and
the counts match the game's own runtime allocations (`DEBUGON=1`):

| runtime allocation (ETAPPE5) | file | count |
|---|---|---|
| `T_ARRAY : alloc c_rail g_rails [3364]` | `.cmp` | 3364 |
| `T_ARRAY : alloc c_waypoint g_driver_map [4911]` | `.dmp` | 4911 |
| `m_chunks alloc 810` / `g_chunks [810]` | `.pmp` / `.rmp` | 810 |
| 503 x `Loading static object` | `.omp` | 503 |

Load order (from the runtime trace): **`.cmp`, `.dmp`, `.pmp`, `.rmp`, `.omp`**.
`rceng_load_etappe` @ 0x441EA0 -> 0x444BF0; the per-chunk loader holds **two open
file handles at once** and reads the chunk table from `.pmp` (`fcn.00442B00`)
and the chunk geometry from `.rmp` (`fcn.004431B0`) in lockstep, one chunk at a
time, printing `loading chunk: %d`.

A track is a chain of 726-840 *chunks* spaced 128 world units apart.

### .rmp — road mesh (CONFIRMED)

```c
u32 plane_count_hint;        // stale; see note
u32 chunk_count;
chunk[chunk_count] {
    u32 n_road; plane road[n_road];   // rd_chunk.cpp: "Too many road-planes" max 16
    u32 n_land; plane land[n_land];   // "Too many land-planes" max 24
    u32 n2;     plane l2[n2];         // max 12
    u32 n3;     plane l3[n3];
}
plane {                               // 104 bytes
    struct { float x, y, z, u, v; } v[4];   // 20 B each -- a textured quad
    u32 tri[2][3];                          // 2 triangles, indices 0..3 local to the plane
}
```

The four lists correspond to the four `fcn.0044BCB0` call sites in
`fcn.004431B0` (each allocates a 140-byte `c_plane` from `g_planes` and fills it
from the file). Typical triangles are `(0,2,1)` and `(0,3,2)`.

`plane_count_hint` equals the true total plane count only for etappe2 and
etappe4; elsewhere it is a few hundred low. It is not needed to parse — treat it
as a stale authoring figure. Per-track totals (road/land/l2/l3):

| track | chunks | road | land | l2 | l3 | total |
|---|---|---|---|---|---|---|
| etappe0 | 778 | 4757 | 6958 | 1738 | 3206 | 16659 |
| etappe1 | 818 | 5480 | 10550 | 569 | 7050 | 23649 |
| etappe2 | 726 | 4610 | 7021 | 1370 | 1707 | 14708 |
| etappe3 | 774 | 4596 | 10938 | 204 | 6248 | 21986 |
| etappe4 | 840 | 5639 | 7512 | 2022 | 2234 | 17407 |
| etappe5 | 810 | 5861 | 12236 | 472 | 6553 | 25122 |
| etappe6 | 778 | 4757 | 6958 | 1738 | 3206 | 16659 |

Rendering the `road` list top-down reproduces the track layout: etappe0 is a
closed city circuit, etappe3 a point-to-point motorway stage.

### .pmp — chunk table (CONFIRMED)

```c
u32 chunk_count;
chunk[chunk_count] {
    float x, y, z;                              // chunk world position
    u32   trigger0, trigger1;                   // per-chunk trigger slots (f3/f4)
    u32 n_rails;    u32 rail    [n_rails];      // indices into .cmp
    u32 n_wp_left;  u32 wp_left [n_wp_left];    // indices into .dmp, max 12
    u32 n_wp_right; u32 wp_right[n_wp_right];   // indices into .dmp, max 12
}
```

`trigger0`/`trigger1` (stored at chunk+0xEC/+0xF0) are the AI trigger slots:
0 = NO_TRIGGER, 4/5/6/8 = speed trigger, 18-21 and 40-43 = lane trigger; other
values belong to the remaining trigger kinds. See research/discoveries.md.

Both waypoint overflow checks print `rd_chunk.cpp - chunk waypoint left
overflow` (the "right" message was never corrected). The rail indices across all
chunks **partition `.cmp` exactly**, and `wp_left + wp_right` **partition `.dmp`
exactly** (etappe5: 2319 + 2592 = 4911) — a strong cross-check that the three
formats are read correctly.

Earlier documents described `.pmp` as "N x 12 B positions then variable 64-byte
records with `2` markers". Those `2`s were simply the `n_wp_*` counts; the
12-byte "positions" are the first three fields of each chunk record.

### .cmp — rails (CONFIRMED)

```c
u32 count;
struct { float x, y, z, dx, dy, dz; } rails[count];   // 24 B
```

Named `c_rail g_rails` at runtime (`rd_rail.cpp`), referenced per chunk from
`.pmp` — this is the guard-rail / barrier set, not a general collision map.
`dz` is typically +/-128, i.e. one chunk length.

### .dmp — driver map / AI waypoints (CONFIRMED)

```c
u32 count;
struct { u32 type; u32 f1; float x, y, z; i32 link[4]; } wp[count];   // 36 B
```

Named `c_waypoint g_driver_map` at runtime — this is the **AI waypoint graph**
(`ai_wmap.cpp`), not a "draw map" as earlier documents guessed. `link` entries
are waypoint indices, -1 = none. `.pmp` splits the waypoints into a left and a
right set per chunk.

**`y` (session 7, was "f3"):** the dword between `x` and `z` is the waypoint's
**height**, so the three coordinates are in natural order. It equals the owning
chunk's `y` for **28452 of the 28461 waypoints** across all 7 tracks, and it
follows bridges — on etappe0 chunks 411-419 it runs 1, 2, 4, 6, 7, 6, 4, 2, 1,
and on etappe3 chunks 527-536 it runs 1, 3, 6, 10, 15, 15, 10, 6, 3, 1. The nine
that differ from their chunk are waypoints genuinely off the chunk centre — two
of them are the etappe1 fuel-station slip road at chunks 399/400 — which is
exactly what a per-waypoint height is for, so they corroborate the reading
rather than weaken it.

**`f1` is a per-waypoint trigger** (CONFIRMED, session 13). It goes to the
runtime waypoint's +0x04 (constructor 0x4443a0), and check_racer_trigger
(0x40af90) tests it after the chunk's two trigger slots (0x40b01b), in the same
number space. The earlier "0 in all but 4" was wrong:
- every stage has a pair of adjacent type-130 waypoints with f1 = 2 (trigger
  2, BENZINE_STATION, the service station), and etappe5 a second pair;
- etappe2 has sixteen with f1 = 10 and one with 9 (the zero-chunk marker).

`type` takes values 0, 2 and 4, sometimes with bit 7 (128) set — the flag bit is
not yet identified.

Note: the `.dmp` *loader* at 0x448BA0, which rewrites the extension to `DMG`, is
a different code path (`rd_drmap.cpp`) — see open questions.

### .omp — static objects (CONFIRMED)

```c
u32 count;
struct {
    i32   chunk;            // owning chunk
    i32   type;             // 0 dominates; 1..10, 21, 43 also occur
    float x, y, z;
    float rot_x, rot_y, rot_z;   // degrees; only rot_y is ever non-zero
    char  anim_name[36];    // e.g. "A_LNTRN0_ANIM" -> resolved to an anim index
} objects[count];           // 68 B
```

Confirmed field-by-field against the game's own log
(`Loading static object 0 / chunk 46 / type 0 / position (-4101.373047,
100.000000, 66775.281250) / animname A_LNTRN0_ANIM / animindex 28`).

Caveat: `etappe2.omp` and `etappe6.omp` declare one record more than the file
contains; clamp to `(len-4)/68`.

### .pth — scripted object paths (CONFIRMED, session 7; named session 10)

```c
i32 version;              // must be -1; the loader rejects anything else
u32 path_count;
path[path_count] {
    u32 first_chunk;      // the two bracket the stretch of road the path covers
    u32 last_chunk;       //   (0 = unset; etappe2 path 3 and etappe3 path 1)
    u32 f2;               // the path TYPE, 0..7 (pth_main.cpp:440)
    u32 f3;               // the LAP it plays on, 1-based; 0 = every lap
    u32 f4;               // the path's TIME in ms, 5000..50000
    u32 point_count;
    point[point_count] {
        u32   flag;       // always 0 in the shipped tracks
        float x, y, z;
    }
}
```

All 7 tracks consume **byte-exactly**. Sizes range from 8 B (etappe5, which
declares zero paths) to 1200 B (etappe2, 7 paths / 64 points); etappe0 and
etappe6 are identical, as they are for every other per-track file.

Evidence: the loader is **fcn.00433710** in `C:\work\A2racII\Game\pth_main.cpp`.
It opens `etappe%d.pth` with mode `"r+b"`, `fread`s the version and compares it
against -1 (`Incorrect version path file.\nversion: %d track: %d`), reads the
count into 0x4E47C0, then reads **six dwords per record** into 0x4E0710+ before
the points, bounds-checking each against the count
(`Path does not exist! \nIndex: %d`). The in-memory field order matches the file
order exactly.

`first_chunk`/`last_chunk`: for **158 of the 169 points** across all tracks, the
chunk nearest the point lies inside `[first_chunk, last_chunk]`; the 11
exceptions overshoot an end by 1-4 chunks, which is what a camera lead-in and
lead-out looks like.

`f2` is the **path type**, bounded `0 <= f2 < 8` at 0x4341c6
(`pth_main.cpp:440`, "Unknown path type!"). `f4` is a **time in milliseconds**,
settled by `pth_main.cpp`'s own "Time not set for path %d": the authored
5000..50000 are 5 to 50 seconds. That confirms the session-7 observation that
`f4 / arc_length` clusters by `f2` — paths of a kind are paced alike — and
retires the alternative reading of `f4` as a distance.

`f3` is the **lap the path plays on** (session 12): 1-based, with **0 = every
lap**. fcn.00434460 runs when the player's chunk (0x4d9e44) changes and starts
path *i* (calling its type's start function) when all of these hold:
`first_chunk != 0`, `first_chunk == player chunk`, `f3 == laps_done + 1` or
`f3 == 0`, and the path has not run yet this lap (the counter at record+0x338).
All the counters reset when `laps_done` (0x4d9ad4, incremented by
`Tr_finis.cpp` at the line) changes. The data fits. The two-lap city circuits
use only 1 and 2, giving a different set of paths per lap. The single-pass
highway stages use 0 or 1. A path with `first_chunk` 0 can never start.

Each type has a three-function entry `{start, +4, +8}` in a table at
**0x4966F0** (session 12; session 10 read it from 0x4966F4, one dword off,
which attributed every function to the wrong type). The start function is the
one the chunk trigger calls (`[type*12 + 0x4966f0]` at 0x434515). Five types
are named by the meshes their start functions load:

| f2 | start fn | what | evidence |
|---|---|---|---|
| 0 | -- | nothing | null start entry |
| 1 | 0x432a10 | unnamed | |
| **2** | 0x432bd0 | a **police car** | loads `MPOLIZEI_ANIM` (0x432bd5) |
| **3** | 0x434800 | a **canal tour boat** (STRONGLY SUPPORTED: seen in the port, session 12) | loads `E00_ROND_ANIM` (13.5 x 4.8 x 2.1 m), points at a flat y = -34, below the streets |
| **4** | 0x434a70 | the **train** | loads `C_TREIN_ANIM` (87 m long) |
| **5** | 0x433170 | the **helicopter** | `pth_heli.cpp`, `C_HELI_ANIM` + two rotors, "Helicopter paths only available in city tracks!"; points at y 67..217 |
| **6** | 0x434cf0 | a **traffic vehicle** | picks one of `N_BUS`, `N_VRTAUT`, `N_BESTEL`, `N_GOLF`, `N_ASTRA` at random (`rand() % 5`, table 0x4969F8) |
| **7** | 0x434550 | the **aeroplane** | loads `A_VLIEGT_ANIM` (0x434553) |

So these are **object** paths. No type is a camera path.

Usage in the shipped tracks:

| stage | types |
|---|---|
| etappe0, etappe6 | 3 x4 |
| etappe1 | 7, 6 x6 |
| etappe2 | 5 x2, 4 x3, 6 x2 |
| etappe3 | 6 |
| etappe4 | 4 x2 |
| etappe5 | none |

Types **0, 1 and 2 are authored in no track**. The helicopter appears only on
etappe2, a city stage, which fits its assert. (Session 10's "3/4/5 city, 6/7
highway, exactly" was wrong: type 6 is on etappe2 too.)

Runtime: the path array is at **0x4E0710** (records; the type is at +8, 0x4E0718) with an **828-byte** stride, the
count at **0x4E47C0**, and a path holds at most **50 points**.

Consumers: `pth_main.cpp` (fcn.00433710 loads, 0x434170 dispatches) and
`pth_heli.cpp` (fcn.004328B0).

Parsers for all of the above: `port/a2tools/track.py`
(`python3 a2tools/__main__.py tracks <file> [-o road.obj]`).

## Sprites — RC.IMG / MENURC.IMG entries (CONFIRMED, session 5)

These two archives use the **binary** IND (u16 count + count x u32 offsets; the
IMG repeats the count in its first u16, so entry 0 starts at offset 2). Each
entry is one 8-bit paletted sprite:

```c
u16 width, height;
u16 anchor_x, anchor_y;     // hotspot; == w/2, h/2 for 322/453 and 185/482 entries
u8  pixels[width * height]; // index 0xFF = transparent
```

`8 + width*height` equals the entry size for **all** 453 RC.IMG and 482
MENURC.IMG entries. Index 0xFF is by far the most common value overall
(752,650 pixels in MENURC) — it is the transparency key, used to pad the corners
of rotated sprites.

- `RC.IMG` = **in-game HUD/dashboard art**. Paired with the track palette
  (Raser.ini `[PALET] ETAPPE0=e00_pal.pcx,e02_pl16.pcx` — first is the 8-bit
  palette, second the 16-bit one) it decodes correctly: entry 1 is the
  speedometer dial with its 20..250 km/h markings and needle.
- `MENURC.IMG` = menu art (panels, buttons, headers, turntable cars, stage
  maps). **Palette (session 12, CONFIRMED):** the one shared by all seven
  full-screen menu PCXs (hoofd, garage, bedien, Newgame, showroom, opties,
  hiscore have byte-identical palettes). With it every MENURC sprite renders
  correctly, and sprites template-matched in Wine captures of menu.dat come
  out with zero colour error. `PAL.PCX` differs in 234 entries and is not it.

Reader: `port/a2tools/sprite.py`
(`python3 a2tools/__main__.py sprites RC.IMG RC.IND e00_pal.pcx out/`).

## TGA textures (CONFIRMED)

All 106 TGA entries in RCS.IMG are **uncompressed true-colour** (header type 2,
no colour map): 105 at 24 bpp and one at 32 bpp, from 32x32 up to 640x480. Most
carry the standard 26-byte TGA 2.0 footer, which is why the payload is usually
26 bytes short of the entry size. Pixels are BGR(A), bottom-up unless bit 5 of
the descriptor is set. All 106 parse.

The game logs `Reading TGA file` / `creating surface %d,%d` and uploads them as
Direct3D textures in the 5:6:5 format chosen at startup.
Reader: `port/a2tools/tga.py`.

## CARS.DAT — player-car performance (CONFIRMED)

`init_player_car` (plr_car.cpp, fcn.004256A0) does exactly two reads:

```c
fread(0x004A5EE0, 124, 10, f);   // 10 x 124 = 1240
fread(0x004A5DF0,  24, 10, f);   // 10 x  24 =  240   -> 1480 = whole file
```

So there are **10 car slots**; the 7 buyable cars from `garage.ini` are filled
and slots 7-9 are all zero. Block A (31 floats) splits into four per-gear arrays
plus 7 unidentified handling values:

```c
float accel[6], accel_upgraded[6];
float max_speed[6], max_speed_upgraded[6];   // metres/second (x3.6 = km/h)
float handling[7];
```

The speeds are real: Trabant 41.889 m/s (151 km/h) stock, 45.889 (165 km/h)
upgraded; Diablo 66.667 (240 km/h) / 70.944 (255 km/h). Stock cars have 0 in the
6th slot — a 5-speed box — and the upgraded array fills it in, matching the
`VERSNELLINGSBAK` (gearbox) item in garage.ini.

Block B (6 floats) is three (a, b) pairs — (3,20)x3 for the Trabant, (2,30)
(3,15) (5,40) for the Diablo. Not yet identified.
Reader: `port/a2tools/cars.py`.

## Other data files

- `CARS.DAT` (1480 B): floats — player car performance/gear data ("init_player_car ... CARS.DAT"). Layout TBD.
- `game.ini` (10,452 B): dev-documented; game+menu shared state (see discoveries).
- `Raser.ini` (7466 B): menu/program settings (movies, resolution, palette refs, sound, mouse, IMG file).
- `garage.ini` (3263 B): garage/shop config (car list, prices?). TBD.
- `language.ini` (3278 B): [SETTINGS] LANGUAGE=0/1/2 (NL/EN/DE), TEXT_COUNT=56, [NEDERLANDS]/[ENGELS]/[DEUTSCH] sections.
- `menu.ini` (43 B), `menu.dat` is NOT data — it's a PE exe (see executables.md).

## Media

- Smacker video: `SMK2` magic, v2, 320x240, flags @=0x40? (intro 16 MB / 1,339? frames; win 236 frames; lose; exit).
- `setup.wav` (1.6 MB) — installer audio.

## Versioning

- No magic/version words seen in the map formats so far; counts-first design (C-style struct dumps).

## SLOTS.RAS (menu.dat career saves)

4824 bytes = 9 slots × 536 (0x218), packed little-endian. menu.ini
`[PLAYER] save_game` selects the active slot. Per slot: +0x16 next etappe
(int), +0x1e money (int), +0x2b damage (int), +0x3b seven ints of controls
(LEFT, RIGHT, UP, DOWN, EXTRA1, EXTRA2, sensitivity), +0x00 in use, +0x04
play order (the latest chosen player has the highest), +0x1a always 0,
+0x57 (byte) the standings tab last shown, +0x58 eight standings
tables (TOTAAL + 7 stages) of four 14-byte rows {u8 car; char name[9]; i32
seconds}. See "The menu's career file, SLOTS.RAS" in research/discoveries.md.


## Menu fonts — the nested .IMG files in Mrcs.img (CONFIRMED, session 12)

`CARVED.IMG`, `SMALLBT.IMG`, `ascii12.img`, `cyber.img`, `BUTFNT.IMG`,
`STDFONT.IMG` and `midfont.img` are fixed-cell bitmap fonts, not archives:

```c
u8  present[256];      // 1 = the character has a glyph
u16 cell_w, cell_h;    // 10x26 BUTFNT/midfont, 8x17 STDFONT/cyber,
                       // 8x12 ascii12, 10x17 CARVED/SMALLBT
u16 colour;            // 0xc8, 0x3f, 0xfd, 0x70 ... (LIKELY the main ink index)
u16 key;               // 0xff, the transparent index
u8  glyph[256][cell_h][cell_w];   // 8-bit pixels in the menu palette
```

`264 + 256 * w * h` equals the file size for all seven.

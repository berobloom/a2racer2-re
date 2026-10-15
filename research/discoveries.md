# Confirmed discoveries

Evidence-backed findings from the A2 Racer II (Netherlands, v1.01) CD. Confidence levels per AGENTS.md.

## Source media

- CONFIRMED: Project input is a Redump-style CD image of **A2 Racer II (Netherlands) (v1.01)**: `A2 Racer II (Netherlands) (v1.01).bin` (528,967,152 bytes, MODE2/2352, 224,901 sectors) + `.cue`, plus a `.7z` repack containing the same pair.
- CONFIRMED: ISO9660 filesystem (extracted via bchunk → 460,597,248 bytes) contains 1,259 files / 280 dirs.
- CONFIRMED: CD layout: `Autorun.exe`, `autorun.inf`, `setup.exe` (installer), `service/setup.exe` (16-bit NE helper), `data/` (the game), `dxsetup/` (DirectX 5 redistributable, files dated 1997-07-14..1998-01-27), `Helpdesk/` (empty driver folders: DirectX/Joystick/Sound/Video), `Leesmij.HLP/.cnt` + `leesmij.txt` (Dutch readme), `versie.txt` = "03-11-1998 - 1.01".
- CONFIRMED: `versie.txt` in `data/` contains "03-11-1998 - 1.01".

## Executables (all PE32 i386, MSVC, subsystem GUI)

| file | size | timestamp | role |
|---|---|---|---|
| `data/a2_II.exe` | 28,672 | 0x351229C0 (1998-03-20) | launcher stub; hardcodes "spel.dat"; CreateProcessA + WaitForSingleObject |
| `data/qrace.exe` | 56,320 | 0x362475BD (1998-10-14) | small orchestrator/dialog; reads `game.ini`, spawns `<x>.dat`; DirectDrawEnumerateA (mode probe) |
| `data/spel.dat` | 669,696 | 0x364180B3 (1998-11-05) | **main game engine** (renamed PE exe) |
| `data/menu.dat` | 253,440 | 0x363DD150 (1998-11-01) | **menu front-end** (renamed PE exe), plays Smacker video |
| `data/instlng.exe` | 67,584 | 0x36235BD4 (1998-10-14) | setup/install helper (dialog, COMCTL32, DDRAW probe) |
| `data/SMACKW32.DLL` | 66,048 | 1996-07-10 | Rad Game Tools Smacker 2.x playback library (ordinal imports by menu.dat) |
| `setup.exe` | 1,781,325 | 0x33305FF8 | installer bootstrapper ("GLBSInstall", "Corrupt installation detected") |
| `service/setup.exe` | 1,346,982 | — | NE (Win16) installer component |

Hashes (MD5): a2_II.exe 4f411290…, qrace.exe aa5fec96…, spel.dat 556416bf…, menu.dat 4cb67aa2…, SMACKW32.DLL d71d47e9….

## The game is split into two programs that communicate via game.ini

- CONFIRMED: `menu.dat` and `spel.dat` both read/write the same keys in `game.ini` (`etappe`, `finished`, `money`, `damage`, `position`, `tracktime`, `MOTOR`, `BANDEN`, `ABS`, `TURBO`, racer results, best times).
- CONFIRMED: neither `menu.dat` nor `spel.dat` imports CreateProcessA. Only `a2_II.exe` and `qrace.exe` spawn processes.
- STRONGLY SUPPORTED: flow = a shell (qrace.exe / a2_II.exe) launches menu.dat; menu exits; shell launches spel.dat; game exits; shell shows menu again. Game state (win/lose, times) is passed through game.ini.

## Rendering

**CORRECTION (session 5).** Sessions 3-4 concluded that spel.dat contains a
*software* 3D rasterizer. That is **DISPROVEN**. The game uses **real Direct3D 2
immediate mode** (DirectX 5). The old reasoning rested on "the binary has exactly
one LoadLibraryA and it loads user32.dll, therefore no D3D DLL is ever loaded" —
but Direct3D 1-5 is obtained by `IDirectDraw::QueryInterface(IID_IDirect3D2)`,
and `ddraw.dll` pulls in `d3dim.dll` itself. No `LoadLibraryA` is involved, so
that evidence never ruled out D3D. Claims below are re-derived.

- CONFIRMED (static): `spel.dat` embeds these interface GUIDs (file offsets →
  VAs): `IID_IDirect3D2` 0x7E998 → 0x480398, `IID_IDirect3DRGBDevice` 0x7E9C8 →
  0x4803C8, `IID_IDirect3DHALDevice` 0x7E9D8 → 0x4803D8,
  `IID_IDirectDrawPalette` 0x7E9E8 → 0x4803E8, `IID_IDirectDraw2` 0x7E9F8 →
  0x4803F8. A fifth GUID at 0x4803B8 is `IID_IDirect3DRampDevice`
  ({F2086B20-259F-11CF-A31A-00AA00B93356}).
- CONFIRMED: 0x44C760 (called from the dplib device init 0x44C7B0) is
  `IDirectDraw2::QueryInterface(IID_IDirect3D2, &g_d3d)` — `call dword [ecx]`
  (vtable+0, QueryInterface) on the IDirectDraw2 at [0x4EABC4]; the resulting
  **IDirect3D2\* is stored at 0x4EA9CC**. Failure logs
  `D3DLIB.CPP(34) : D3DError : %s` via the real-D3D error-name table (0x460520).
- CONFIRMED (runtime, Wine + DEBUGON=1): the game enumerates real D3D devices
  and prints `D3DDevice Name : <Direct3D HEL>` / `<Direct3D HAL>` with
  `Desc : <RGB Direct3D emulation>` / `<Wine D3DDevice using WineD3D and
  OpenGL>`, then `Creating d3d_device with GUID:-2065285664` (= the HAL device).
  It also queries alpha-blend caps (`BOTHINVSRCALPHA source blend mode
  supported.` …) and enumerates texture formats, selecting
  `TXF_16BPP_RGB selected = 3` (RGB 5:6:5) out of formats 0-5
  (5:5:5, 5:5:5:1, 4:4:4:4, 5:6:5, 8:8:8, 8:8:8:8).
- CONFIRMED: device selection is 0x460340 (explicit) / 0x4601E0 (automatic),
  both in `D3DEV.CPP`. Type 1 → HAL, 3 → RGB, 4 → Ramp/Mono, else
  `D3DEV.CPP(270) : requested unknown device type`. The automatic chooser
  branches on the display bit depth global [0x49E734]: **>8 bpp → HAL (falling
  back to RGB), <=8 bpp → the Ramp/Mono device**.
- CONFIRMED: the draw call at **0x455AB8 is
  `IDirect3DDevice2::DrawIndexedPrimitive` (vtable+0x78)**, pushed as
  `(this, dptPrimitiveType=4 D3DPT_TRIANGLELIST, dvtVertexType=3 D3DVT_TLVERTEX,
  lpvVertices=[obj+0x88], dwVertexCount=[obj+0x8C], lpwIndices=[obj+0xA0],
  dwIndexCount=[obj+0xA4], dwFlags=8)`.
  **The device vtable is IDirect3DDevice2, not DirectDraw** — so vtable+0x5C is
  `SetRenderState` (used at 0x455A57 for the fill mode, global [0x4EA864]=2 =
  D3DFILL_SOLID, error string `d3dev.inl(462) : *FILL MODE* error setting light
  state`), +0x18 `AddViewport`, +0x24 `EnumTextureFormats`, +0x64 `SetLightState`.
  The earlier "mirrors DirectDraw layout (0x18 CreateSurface, 0x5C
  GetAvailableVidMem)" reading was wrong.
- CONFIRMED: vertices handed to D3D are **`D3DTLVERTEX` (32 bytes)** — already
  transformed and lit. This retroactively explains session-4's measurements:
  the painter sort key "dword at [obj+0x88] + idx*0x20 + 8" is the `sz` field of
  D3DTLVERTEX (sx@0, sy@4, sz@8, rhw@0xC, color@0x10, specular@0x14, tu@0x18,
  tv@0x1C), and "triangles = 3x int16" is the `LPWORD` index buffer that
  `DrawIndexedPrimitive` requires — **vertex indices, not screen columns**.
- CONFIRMED: consequently the split is: **the game does transform, projection,
  lighting and visibility in x87 float on the CPU; Direct3D rasterizes.**
  The x87 findings from session 3 stand (**correction, session 14: 0x49B014
  is not a focal constant** -- it is the sine table's size, set to 4096 at
  runtime by 0x450280; see "The racing cars' wheels"),
  camera pos 0x4D9E50 / target 0x4D9E4C, matrices 0x4EA4B4/0x4EA418) — that is
  the game's own vertex pipeline producing screen-space TLVERTEX.
- **RETRACTED**: "affine texture mapping (no per-pixel perspective correction)".
  The inference was "no per-pixel divides in .text" — but the rasterizer is not
  in .text at all. D3DTLVERTEX carries `rhw`, so perspective correction is
  available and is the driver's decision.
- **RETRACTED**: "software z-buffer". The z-buffer is a real DirectDraw
  `DDSCAPS_ZBUFFER` surface attached for D3D's use (creation in D3DINFO.CPP
  0x44C930; runtime log `init z-buffer` / `z-buffer initialized : 1` /
  `clear z-buffer`). `GetAvailableVidMem` with caps 7
  (3DDEVICE|ZBUFFER|TEXTURE) at 0x417870 is the standard DX5 way to decide
  whether a hardware z-buffer fits.
- CONFIRMED: DRWORDER.CPP draw-order sorting is still real and still the game's
  own code (T_ARRAY 0x4EA8A4; runtime `T_ARRAY : alloc draw_info_t draw_order
  [1000]`), used together with the z-buffer — the app sorts, D3D depth-tests.
  Dual paths in 0x454AB0 (z-buffer path 0x454EE0 writes unsorted; painter path
  0x454F69 sorts via 0x470860 with comparator 0x454A60) stand.
- CONFIRMED: DirectDraw surface management via own wrapper
  `C:\work\winlib\surface.cpp` (blit_surf_2_surf, clear surface, primary
  surface, "Flipping pages", flip @ 0x458340); `Skelet2D` owns the 2D/blit side
  and the primary surface is locked once at init (0x662D0).
- CONFIRMED: track is chunk-based: `rd_chunk.cpp`, strings "Adac chunk = %d.
  Player chunk = %d", "Data-chunk count does not match draw-chunk count",
  Game.ini `visiblechunks=30`.
- CONFIRMED: 8-bit (320x200, 640x480) and 16-bit (640x480) modes (Raser.ini
  [PROGRAM] RESOLUTION); paletted PCX art per track (Raser.ini [PALET]
  ETAPPE0..6 → e0x_pal.pcx). In 16-bit mode the palette is skipped
  (`Pal.cpp(42) : 16 bit screen mode -> ignore palette`).
- CONFIRMED: 0x448BA0 = .dmp (draw-map) loader; rewrites last 3 filename chars
  to 'D','M','G' before opening.
- CONFIRMED: render_game throttles to >=100 ms via GetTickCount → ~10 fps design
  target. (This is a *cap on redraw*, not evidence of a slow software rasterizer.)
- CONFIRMED: screen is column-organized: "bufferscreen" (0x4EA9C8) = bounds-checked
  T_ARRAY of per-column sub-surface rect objects + per-column activity byte
  flags; each column cleared/blitted separately (0x45D3A0).

## Execution architecture (spel.dat) — see reverse-engineering/spel-architecture.md

- CONFIRMED: entry = MSVC CRT (0x471400) → `main` @ 0x431710 (Main.cpp).
- CONFIRMED: `init_main` @ 0x431610 sequence: Raser.ini config → Skelet2D init (0x420FE0, creates DirectDraw) → dplib device init (0x44C7B0) → `init_game` @ 0x423D20 (gm_init.cpp: display, ini_load_all, 3D lib init, video-mem check, game.ini FILTER, set_game_input) → game timer init (0x441AB0) → progress bar → start_framecount_timing.
- CONFIRMED: `start_game` @ 0x424E30 sets the loop flag 0x4D9A78 (=g_game_running) and initializes object arrays.
- CONFIRMED: main loop = while(0x4D9A78): `handle_game` @ 0x424A70 → `handle_framecount_timing` @ 0x425470 → `my_handle_win_msg` @ 0x431540 (+ CTRL-Q emergency exit, "lvl%2.2d_%2.2d.bmp" screenshots).
- CONFIRMED: per-frame pipeline order inside handle_game: gt_update_timer → rceng_handle_objects → ai_check_cars_outside_handling_range → handle_game_cameras → render_game → check_game_keys → handle_game_actions → check_player_time → check_player_damage → draw_2d_display → update_game_screen (+ ~14 unnamed sub-updates). Input is polled after render (affects next frame).
- CONFIRMED timing: game clock uses QueryPerformanceCounter + timeGetTime (only sites 0x441BE0/0x441C0B/0x441D20/0x441D4E); render_game throttles itself to ≥100 ms via GetTickCount; progress bar + gm_clock timers use GetTickCount; framecount timing uses CRT time(). Input polling via GetAsyncKeyState wrapper 0x466910.
- CONFIRMED shutdown order: exit_main (0x425480) → exit_game (0x421AF0) → race-engine exit → exit objects (0x457320) → exit 3d lib (0x44CC80) → exit skelet2d (0x4211C0).
- CONFIRMED: all game.ini access goes through 4 thin wrappers (0x46AAD0 get-int, 0x46AB10 get-string, 0x46AA30/0x46AA70 write-int/string) — the only call sites of the Win32 PrivateProfile APIs.
- CONFIRMED key globals: 0x4D9A78 g_game_running; 0x4EA5A0 game object; 0x4E48E0 object array (stride 0xA0); 0x4D8D64 12 game timers (gm_clock); 0x4EA7FC IDirectDraw*; 0x4EA9C8 bufferscreen; 0x4D9E50/0x4D9E4C camera pos/target; 0x484AA8/0x484AAC fog values; 0x4895E5/0x4895D5 .img/.ind filename buffers.
- CONFIRMED: skelet2d image archive setup (0x420C00-region) reads [IMG] RCSFILE from Raser.ini (default "Resource"), builds "<base>.img"/"<base>.ind"; MESH.CPP loader region at 0x441DB0 ("loading triangle data", "vertices : %d") parses .d3d models.

## Runtime (Wine) — the game runs, no CD needed

See `reverse-engineering/runtime-wine.md` for the full recipe and evidence.

- CONFIRMED: the CD check (`C:\work\winlib\Filepath.cpp`, `find_program_cdrom`
  @ menu.dat 0x417260) consults **`getenv("NOCD")` first**; if the variable
  exists it takes `NOCD[0]` as the program drive letter and returns immediately,
  skipping the drive scan and the probe-file check. `NOCD` is present in both
  `menu.dat` and `spel.dat` and is a developer bypass shipped in the retail
  binaries.
- CONFIRMED: the probe is `<drive>:\data\intro.smk`; `Hwinfo.cpp`
  (`find_cdrom_drive` @ 0x418D20) supplies the drive letter from a
  `GetDriveTypeA` drive list.
- CONFIRMED: `resolve_file_path` @ 0x417330 tries the **bare filename in the
  current directory first** and only falls back to the CD path — so running with
  the data directory as CWD needs no install.
- CONFIRMED: with `NOCD=C`, an Xvfb root window of exactly 640x480x24, and
  `Raser.ini RESOLUTION=3`, **both `menu.dat` and `spel.dat` run and render
  correctly under wine-9.0** (main menu and in-game 3D + HUD both captured).
- CONFIRMED: 8-bit modes (`RESOLUTION=0/1`) crash under Wine because the device
  chooser requests `IID_IDirect3DRampDevice`, which Wine does not implement; the
  fault occurs inside `EnumTextureFormats`.
- CONFIRMED: `Raser.ini [HELPDESK] DEBUGON=1` writes a very detailed `debug.txt`
  (312k lines for a ~40 s run), including per-file archive index+offset
  (`Fileio.cpp(296)`), a full archive directory dump with sizes, mesh
  vertex/triangle counts, D3D device/texture-format/blend-cap enumeration, named
  init stages and T_ARRAY allocations (meshes 1186, objects 2500, draw_order
  1000, textures 100, damage objects 1186).

## Engine lineage

- CONFIRMED (from Game.ini dev comments): options `stuur_model` and `ai_model` switch between "Old (Autobahn Raser I)=1" and "New (A2 Racer II)=0" — the engine descends from Davilex **Autobahn Raser I**.
- CONFIRMED: game code lives in `C:\work\A2racII\Game\` (full source file list leaked via assert strings; see analysis/spel_src.txt), shared libs in `C:\work\winlib\` ("Skelet2D" 2D lib: sprite, surface, pal, pcx, forms, mouse, sound, mixer, inifile, fileio, ascii fonts) and 3D lib in `c:\visual~1\dplib\` / `n:\libraries\a2racerii\win3ddp\`.
- CONFIRMED: menu code lives in `C:\work\A2racII\menu\Ned\prog\` (m_main, m_garage, m_opties, m_kopen, m_nspeler, m_highscore, m_bediening, slotio, janeereq, bestand…).

## Gameplay systems (from strings/INI)

- CONFIRMED subsystems: AI (ai_fsm, ai_path, ai_wmap [waypoints], ai_lane, ai_racer, ai_cops), police/cops + fines, roadside assistance ("wegenwacht"/ADAC: `adac_lane`, "Adac chunk"), fuel/tanking (GETANKT), damage with 8 damage zones (DAMAGE_MESH + DAMAGE_VALUE in game.ini), upgrades (MOTOR/BANDEN/ABS/TURBO/VERSNELLINGSBAK), radar (gfx_radr), skidmarks (gfx_skid), clouds (gfx_wolk), shadows (gfx_shdw), sparks (gfx_sprk), rain particles, helicopter camera path (pth_heli), cameras 1-4 (follow/cockpit/look-back/far-follow), dashboard ('D' cycles detail), 2D overlay (overlay_detail 0-3), "lan_main.cpp" compiled in but **no network imports found** (no winsock/dplay strings).
- CONFIRMED audio: DirectSound (DirectSoundCreate) + DirectInput + WINMM mixer/joystick; 60 concurrent buffers; music tracks in Mrcs.img (jammin.wav, moody.wav, rock.wav, speed.wav, energy.wav, glow.wav, flash.wav, print.wav…), engine sounds, 1git/2git/3git.wav, checkp.wav (checkpoint), heli.wav, kerkklok.wav, flits.wav (speed camera flash), etc.
- CONFIRMED videos (Smacker "SMK2" magic, 320x240): intro.smk (16 MB), win.smk (5.7 MB), lose.smk (7.8 MB), exit.smk (1.2 MB) — played by menu.dat via SMACKW32.DLL ordinals.
- CONFIRMED language.ini: NL/EN/DE localized strings (56 strings; fines, police, arrest, fuel, radar bonus, INVINCIBLE / COP MODE / QUICK SERVICE hints at cheat/debug modes).
- CONFIRMED dev features: Raser.ini [HELPDESK] DEBUGON=0 (debug.txt), TWEAK_MODE=0 (tweak menu/extra cameras/fps print), game.ini `collisionoff`, `startchunk` test var, demo mode.

## Proprietary data files

- CONFIRMED archive formats (IND index + IMG blob):
  - Binary IND: u16 count + count×u32 offsets — RC.IND (453 entries → RC.IMG 2.94 MB), MENURC.IND (482 → MENURC.IMG 4.6 MB); IMG begins with the same u16 count; entries = sprite sheets (u16 dims + FFFF sentinel + 8-bit pixels)
  - **Named IND — FULLY RESOLVED (session 5), fixed layout:**
    `u16 count; struct { char name[20]; u32 offset; } entries[count];` (24 bytes
    per entry). File length is an **exact fit** for `2 + count*24`: RCS.IND
    19,346 = 2 + 806*24, Mrcs.ind 2,954 = 2 + 123*24. Entry size is implicit
    (next offset − this offset; IMG length for the last).
    The bytes after the name's NUL are **uninitialised leftovers from the
    writer's shared 20-byte name buffer** — which is why they trail off into
    `")*+,-./0123"` and into pieces of earlier entries' names (`".D3D"`,
    `"e04_apsl.pcx"`). They carry **no meaning**; the previous "char-range
    type/attribute marker" hypothesis is DISPROVEN.
    Evidence: the game's own log (DEBUGON=1, `Fileio.cpp(296)`) gives
    `name IDX/806 pos OFFSET` for every archive read — **216/216 match the
    parsed index *and* offset exactly** — and the loader's startup directory
    dump (`Bestand / Grootte`) gives sizes — **801/801 match**.
- CONFIRMED RCS.IMG (34.5 MB) = per-track scenery library: WIEL.d3d, bus.D3D, mpolizei.D3D, n_astra/n_bestel/n_bus/n_calibr/n_golf/n_polo/n_vrtaut/n_wals, r_astra/r_beetle/r_bmw/r_clk/r_porch(e)/r_trabbi, R_VECTOR, VPOLIZEI + e0x_*_hi.TGA wall textures.
- CONFIRMED (session 5) **`.d3d` fully decoded** — see file-formats.md:
  `u32 vertexCount; u32 triangleCount;`
  then 33-byte vertices (`float x,y,z,nx,ny,nz,tu,tv; u8 pad`) and 9-byte
  triangles (`u16 a,b,c; u8 pad[3]`). Proven from the loader `MESH.CPP`
  fcn.00455B50: it reads 0x21 bytes per vertex and `rep movsd`-copies only 32,
  and reads 9 bytes per triangle storing only the three leading u16 into a
  stride-6 WORD index buffer. The trailing bytes are dead exporter padding
  (constant per model, arbitrary across models) — same uninitialised-buffer
  habit as the IND names. The 8 floats are a `D3DVERTEX`; mesh type 1 keeps all
  8, type 2 converts to `D3DLVERTEX` (drops normals, color := 0xFFFFFF).
  Verified on all 447 models (24,548 verts / 22,618 tris, zero out-of-range
  indices) and visually by rendering (police car, helicopter, Trabant, bus).
- CONFIRMED (session 5) **all five per-track map formats**, each parsing
  byte-exactly on all 7 tracks and matching the game's own runtime allocations
  (see file-formats.md for full layouts):
  - `.rmp` road mesh: `u32 hint; u32 chunkCount;` then per chunk **four**
    count-prefixed lists of 104-byte **planes** (a plane = 4 vertices of
    `float x,y,z,u,v` + 2 triangles of `3x u32`). The lists are road-planes
    (max 16), land-planes (max 24), a third (max 12) and a fourth — the four
    `fcn.0044BCB0` call sites in `fcn.004431B0` (`rd_chunk.cpp`).
  - `.pmp` chunk table: `u32 chunkCount;` then per chunk `float x,y,z; u32 f3,f4;`
    plus three count-prefixed index lists — rails (into `.cmp`) and left/right
    waypoints (into `.dmp`, max 12 each).
  - `.cmp` **rails** (`c_rail g_rails`): `u32 count;` + 24-byte records
    (`float x,y,z,dx,dy,dz`; dz typically ±128 = one chunk length).
  - `.dmp` **AI waypoint graph** (`c_waypoint g_driver_map`, ai_wmap.cpp):
    `u32 count;` + 36-byte records (type, ?, float x, ?, float z, 4 link
    indices). NOT a "draw map" as previously guessed.
  - `.omp` static objects: `u32 count;` + 68-byte records
    (`i32 chunk; i32 type; float pos[3]; float rot[3]; char anim_name[36]`),
    confirmed field-by-field against the loader's own log output. Only `rot_y`
    is ever non-zero.
- CONFIRMED: the index lists cross-validate the whole set — per-chunk rail
  indices **partition `.cmp` exactly**, and left+right waypoint indices
  **partition `.dmp` exactly** (etappe5: 2319 + 2592 = 4911).
- CONFIRMED: track load order is `.cmp`, `.dmp`, `.pmp`, `.rmp`, `.omp`; the
  per-chunk loader holds `.pmp` and `.rmp` open simultaneously and reads one
  chunk from each in lockstep (two distinct file-handle arguments — the reason
  earlier analysis mis-assigned the readers to one file).
- CONFIRMED: decoded road geometry renders as the real track layout — etappe0 a
  closed city circuit, etappe3 a point-to-point motorway stage.
- CONFIRMED `CARS.DAT` (1,480 B): player car tuning data as floats (gear ratios etc.) — referenced by "init_player_car".
- CONFIRMED INI files: `game.ini` (richly documented dev config + game state handoff), `Raser.ini` (program/menu config: movies, resolution, palettes, sound), `garage.ini` (car shop config), `language.ini` (localization), `menu.ini`.
- CONFIRMED `menu.dat` (253,440 B): menu program data? No — it is a PE exe; `menu.ini` is 43 bytes; `spel.dat` is the game exe. (Both .dat files ARE executables.)

## Texturing (session 5)

- CONFIRMED: the texture **colour key is RGB (255, 0, 127)** — a rose pink, not
  magenta. It is the single dominant colour in 41 of the 106 TGA textures in
  RCS.IMG and present in 54 of them; pure magenta (255,0,255) appears in exactly
  one. Keying it and bleeding neighbouring colour into the keyed texels removes
  the cut-out fringes under bilinear filtering.
- CONFIRMED: sprites in RC.IMG/MENURC.IMG use palette **index 0xFF** as their
  key instead (752,650 pixels of it in MENURC alone).
- **CORRECTION (session 6):** `e0X_w_hi` / `e0X_w_lo` are **loading/wait
  screens**, not track textures at all. The debug trace reads
  `Loading wait screen for 16-bit mode` immediately before opening
  `e05_w_hi.tga` and creating a 640x480 surface; `w_lo` is the 320x200 variant,
  matching the two 8-bit resolutions. Session 5 called `w_hi` "the motorway
  signage texture" — it is a loading screen that happens to depict motorway
  signs, which is why rendering a stage with it produced a corridor of road
  signs. Either way the "single per-track atlas" hypothesis stays DISPROVEN.
- CONFIRMED: `.pmp` per-chunk fields f3/f4 are **not** texture indices — they
  are 0 for 724/778 chunks on etappe0 and 761/810 on etappe5, where a texture
  selector would have to vary everywhere.
- CONFIRMED (session 6): **texture binding runs through the anim system.**
  During `loading etappe N anims` the trace loads a texture and then
  immediately the meshes that use it, repeatedly:
  `[e05_gras.tga]` -> `[e05_brgj.d3d]`; `[etapp51.tga]` -> `[e05_dsco.d3d]`,
  `[e05_rcl1.d3d]`, `[e05_bruk.d3d]`, `[e05_alx1.d3d]`; and so on. So an anim
  owns one texture plus the frame meshes drawn with it.
- CONFIRMED (session 6): the anim/frame/texture registries are **static tables
  inside spel.dat**. `fcn.0042B360` (called from the per-etappe anim loader
  fcn.004236D0) static-initialises them:
  `init_frames(0x48E5E8, 386)` and `init_anims(0x492050, 312)` — both counts
  match the runtime log exactly (`frame3d_ptr [386]`, `anims3d_ptr [312]`).
- CONFIRMED (session 6): **frame3d = 386 entries = 295 meshes + 91 textures**,
  in that order. The names live in 12-byte records `{u32 handle; char *name;
  u32 flags(-1)}`: meshes at file 0x8ABC4..0x8B998 (295 records, first
  `A_CHECK2.d3d`, last `e05_elek.d3d`), then a 4-byte gap, then textures at
  file 0x8B99C..0x8BDE0 (91 records, first `s_sfx.pcx`, last `e05_vang.pcx`).
  So **frame index f < 295 is a mesh and f >= 295 is texture f-295.**
  The texture registry is ordered by track: shared art first (s_sfx, s_letter,
  s_shadow, loradar, hiradar, a_trffc1, algemeen, a_wgwerk, a_radar, C_*,
  cars01..03, racer01..02), then the per-track blocks `e00_*` ... `e05_*`.
  Within each track block the last two entries are always `*_weg` (road) and
  `*_vang`, with `*_gras` / `*_gra2` nearby.
- **RESOLVED (session 6): how a mesh or plane gets its texture.** The binding
  is the anim system, and all three tables are static data in spel.dat:

  | table | VA | count | record |
  |---|---|---|---|
  | anim3d | 0x492050 | 312 | **9 bytes**: `i32 frame_count; i32 *frame_indices; u8 flag` |
  | frame3d | 0x48E5E8 | 386 | **16 bytes**: `i32 mesh_handle(-1); i32 texture_handle(-1); i32 mesh_index; i32 texture_index` |
  | anim names | 0x490430 | 312 | `char *`, parallel to anim3d |
  | mesh names | file 0x8A8DC | 357 | 12 bytes `{u32; char *name; u32(-1)}` |
  | texture names | file 0x8B99C | 91 | same |

  `T_ARRAY::static_init` (fcn.0044D320 / fcn.0045D650) stores the caller's
  pointer as the array base, so the runtime arrays *are* these static ones.
  The 9-byte anim stride comes from the walk in fcn.0042B360
  (`lea edi,[eax+eax*8]` then `[ecx+edi*1]` — a byte offset of idx*9), and it
  checks out exactly: 312 x 9 = 0xAF8, so the array ends at 0x492B48, precisely
  where the first anim-name string begins. 311 of 312 records parse
  (`OBJ_LIGHT01_ANIM` alone has a runtime pointer).
  `frame_indices` point into a 396-int array at VA 0x48FE00.

  So: **anim -> frames -> (mesh_index, texture_index) -> names.** Verified:
  `E00_MUNT_ANIM` -> `e00_munt.d3d` + `e00_munt.pcx`;
  `E00_OUDK_ANIM` -> `e00_oudk.d3d` + `e00_oudk.pcx`;
  `A_LNTRN0_ANIM` -> `A_LNTRN0.d3d` + `a_trffc1.pcx`;
  `MPOLIZEI_ANIM` and `WIEL_ANIM` -> their meshes + `cars01.pcx`.

- **CONFIRMED (session 6): the four `.rmp` plane lists take their textures from
  `E0x_ALGEMENE_TEXTURES_ANIM`,** which has exactly **four** frames on every one
  of the six tracks, in the same order each time:

      [0] e0x_gras    [1] e0x_gra2    [2] e0x_weg    [3] e0x_vang

  ("weg" = road, "gras"/"gra2" = ground, "vang" = an atlas of building
  facades.) The set deliberately does **not** include `e0x_bomn` (trees) — a
  session-5 guess that put trees on building walls was wrong. Applying this set
  in the port turns etappe0 from tree-textured walls into the real Amsterdam
  street with shop fronts and facades.
  Still unconfirmed: which of the four lists takes which of the four textures.
  The loader names only list 0 "road-planes" and list 1 "land-planes", so
  road -> `_weg`, land -> `_gras` is the natural reading and renders correctly.
- CONFIRMED (session 6): the `.rmp` plane really carries **no** texture id. Its
  two triangles are 6 u32s whose values across all 16,659 etappe0 planes are
  exactly {0,1,2,3} — no spare high bits to hide an index in. The 104 bytes are
  fully accounted for by 4 vertices x 20 + 2 triangles x 12.
- CONFIRMED (session 6): a single 256x256 texture can serve many surfaces —
  `e00_vang.tga` is an **atlas of nine Amsterdam canal-house facades**, which is
  why the building walls all sample one texture through their u/v sub-rects.
- CONFIRMED: `.omp` object names resolve to meshes by stripping the `_ANIM`
  suffix and appending `.d3d` — this works for **all 1860** objects across the
  shipped tracks, so frame 0 of an animation is a mesh of the same base name.

## World scale and player cars (session 6)

- CONFIRMED: **the world is about 10 units per metre.** Four vehicle meshes with
  known real-world sizes agree: `r_trabbi.d3d` is 16.4 units wide and 39.2 long
  (a Trabant 601 is 1.51 x 3.36 m -> 10.9 / 11.7 u/m), `n_bus.d3d` 25.5 x 93.6
  (a city bus ~2.55 x 12 m -> 10.0 / 7.8), `r_bmw.d3d` and `mpolizei.d3d`
  likewise. So a 128-unit chunk is roughly **12.8 m** of road, and the object
  coordinates in `.omp` are in the same space as the mesh vertices (objects are
  placed without any scale factor).
- CONFIRMED: **`RACER_<n>_ANIM` corresponds to `CARS.DAT` slot n-1**, i.e. the
  garage.ini purchase order: `RACER_1_ANIM` -> `r_trabbi.d3d` + `racer02.pcx`
  (slot 0, the starting Trabant), `RACER_2_ANIM` -> `r_astra.d3d`,
  `RACER_7_ANIM` -> `r_diablo.d3d`. So a car's performance row, mesh and texture
  are all reachable from its slot number.
- CONFIRMED: the traffic and service cars resolve the same way —
  `N_GOLF_ANIM` -> `n_golf.d3d` + `cars02.pcx`, `ADAC_ANIM` -> `adac.d3d` +
  `racer02.pcx`, `WIEL_ANIM` -> `wiel.d3d` + `cars01.pcx` (the shared wheel).
- CONFIRMED: **`FONT3D_ANIM` has 49 frames**, `LTR_0.d3d` .. `LTR_Z.d3d` etc. all
  textured with `s_letter.pcx` — the 3D font used for on-track text, one frame
  per glyph. This is the only anim with a large frame count; almost every other
  anim has exactly one frame, so "anim" here usually means "a mesh plus its
  texture" rather than a moving animation.

## CARS.DAT fully decoded (session 6)

The **TWEAK_MODE menu is still present in the retail spel.dat**, and it names
every tunable car parameter in Dutch. The strings sit together around file
0x83600 and read:

```
Tweak Menu
  1. Acceleratie constanten        6. Afremmen op motor
  2. Max. snelheid/versnelling     7. Rolhoek factoren   (demping / factor)
  3. Schakelpunt vooruit           8. Pitch factoren     (demping / Factor)
  4. Schakelpunt achteruit         9. Hoogte factoren    (demping / Factor)
  5. Remkracht                    10. Stuursnelheid
 11. Opslaan                      12. Stoppen
```

- CONFIRMED: each name is tied to a field by the address its editor reads or
  writes (`fcn.00412E90` prints them, `fcn.00411BD0` / `fcn.00411FB0` edit the
  suspension pairs):

  | field | address (slot 0) | tweak name | meaning |
  |---|---|---|---|
  | A[0..5] | 0x4A5EE0.. | Acceleratie constanten | per-gear acceleration |
  | A[6..11] | | (second set) | upgraded |
  | A[12..17] | 0x4A5F10.. | Max. snelheid/versnelling | per-gear ceiling, m/s |
  | A[18..23] | | (second set) | upgraded |
  | A[24] | 0x4A5F40 | Schakelpunt vooruit | upshift point |
  | A[25] | 0x4A5F44 | Schakelpunt achteruit | downshift point |
  | A[26] | — | — | always 0, unused |
  | A[27] | 0x4A5F4C | Remkracht | braking force (stored negative) |
  | A[28] | 0x4A5F50 | Motor vertraging | engine deceleration |
  | A[29] | 0x4A5F54 | Rolhoek versterking (in the roll sub-menu) | the body's roll rate into a turn, 0x40F90D (settled sessions 13-14) |
  | A[30] | 0x4A5F58 | Stuursnelheid | steering speed |
  | B[0,1] | 0x4A5DF0 | Rolhoek demping / factor | roll |
  | B[2,3] | 0x4A5DF8 | Pitchhoek demping / Factor | pitch |
  | B[4,5] | 0x4A5E00 | Hoogte demping / Factor | ride height |

- CONFIRMED: records are indexed by car slot with a **124-byte stride**, which
  the code computes as `idx*31` dwords (`shl eax,5; sub eax,edx` then
  `[eax*4 + field]`) — independent confirmation of the 31-float block A.
- CONFIRMED: `Stuursnelheid` (A[30]) is used at 0x40F644 as
  `fabs; fsin; fmul [steer]` — a sine-weighted steering/cornering term, which
  is what makes it the steering *rate*.
- CONFIRMED: A[24]/A[25] are consumed as a compare ladder against an
  incrementing counter at 0x40F20B / 0x40F24C — the gear shift logic.
- The seven cars, as decoded:

  | car | top km/h | upgraded | upshift | downshift | brake | eng.dec | steer |
  |---|---|---|---|---|---|---|---|
  | Trabant | 151 | 165 | 0.70 | 0.40 | -0.30 | 3.00 | 9 |
  | Astra | 166 | 180 | 0.60 | 0.10 | -0.50 | 3.00 | 8 |
  | VW | 180 | 195 | 0.80 | 0.20 | -0.20 | 2.00 | 9 |
  | VW | 195 | 210 | 0.80 | 0.20 | -0.20 | 2.00 | 12 |
  | BMW | 210 | 226 | 0.80 | 0.20 | -0.20 | 3.50 | 12 |
  | Porsche | 225 | 241 | 0.30 | 0.10 | -0.20 | 2.00 | 10 |
  | Diablo | 240 | 255 | 0.80 | 0.20 | -0.50 | 2.00 | 12 |

  (The Porsche's 0.30 upshift point is an outlier against every other car's
  0.6-0.8 — possibly a data slip in the original.)

## Rails and the waypoint graph (session 6)

- CONFIRMED: `.cmp` rails are **barrier segments**, not points: each record is a
  start position plus a delta, and the delta's length is **128 units — exactly
  one chunk**. A dual-carriageway chunk on etappe5 carries four of them (the two
  outer edges and the two median sides), and they run diagonally with the road
  rather than axis-aligned. Treating them as line segments and pushing the car
  out of any segment closer than its half-width gives working collision.
- CONFIRMED: the `.dmp` waypoint graph is a **lane network**, one waypoint per
  lane per chunk (etappe5 chunk 4: left 24/26/28, right 25/27/29).
- CONFIRMED (session 7, refining an earlier "~35 units" read off a single
  chunk): **lane spacing is exactly 40 units**, on every track. Projecting each
  chunk's waypoints onto the road normal and histogramming adjacent gaps gives
  40 almost to the exclusion of everything else — etappe3: 3002 gaps of 40 next
  to 8 of 39 and 7 of 41; etappe1: 3603 of 40. At the established 10 units per
  metre that is a 4 m lane, a plausible width for the roads being modelled.
- CONFIRMED (session 7, refining "exactly 3 left and 3 right"): **the lane count
  varies by chunk and by track.** The three motorway stages run mostly 3 lanes
  per carriageway (etappe3 is 3 on the left for all 773 chunks), the four city
  circuits mostly 2. Some chunks carry 4, 5 or 6, and those extra waypoints sit
  far off the road normal (up to 1694 units) rather than 40 apart like lanes —
  they are **not running lanes**.
- CONFIRMED (session 7): some of those off-road waypoint runs are the **fuel
  station slip road**. Every `TRIGGER_BENZINE_STATION` chunk begins a run of
  off-road waypoints whose first waypoint is within ~70 units of an `A_TANK*`
  object, on all four stage fuel stations:

  | stage | trigger 2 / run | run length | nearest `A_TANK*` |
  |---|---|---|---|
  | etappe1 | 549 | chunks 549-563 | 29 units |
  | etappe3 | 397 | chunks 397-399 | 60 units |
  | etappe5 | 399 | chunks 399-408 | 65 units |
  | etappe5 | 642 | chunks 642-648 | 66 units |

  Their lateral offset is a consistent ~250-310 units (25-31 m off the
  centreline), which is forecourt-sized.
- Not all off-road runs are forecourts, though: etappe1 also has runs at chunks
  0-104 and 683-816, and etappe5 at 487-520 and 707-808, all tens of thousands
  of units from any fuel object. SPECULATIVE: those are service roads, scenery
  paths or unused authoring data — they have not been identified.
  Consequence for anything following the graph: treat the lane count as
  per-chunk, and do **not** assume a high lane index is a running lane. Filter
  by lateral offset (running lanes are multiples of 40 from the centreline).
- CONFIRMED: **the two carriageways run in opposite directions**, which shows up
  in the link arrays. For a left-set waypoint `links[0]` is the previous chunk
  and `links[3]` the next (wp24 -> links [18, -1, 20, 30], with 18 in chunk 3
  and 30 in chunk 5); for a right-set waypoint the two are swapped (wp25 ->
  links [31, -1, 33, 19], with 31 in chunk 5 and 19 in chunk 3). The remaining
  links point at the adjacent lanes of the same neighbour chunk, which is what
  `ai_lane.cpp` / `change_lane` would need.
  Consequence for anything following the graph: do **not** assume a fixed link
  slot means "forward" — resolve direction against the chunk index, which is
  what the game counts progress in ("Player chunk").

## Collision response (session 6)

- CONFIRMED: the player's collision handler is **`player_car_collision` =
  fcn.00427DE0** (plr_car.cpp), reached from the dispatcher at 0x427BD0 which
  logs `World-type collision.` / `Object-type collision.`, then
  `Handling damage for object %d.` and `Passing control to
  player_car_collision.` The runtime trace shows it firing repeatedly during a
  race, with `carsoft.wav` played on the softer hits.
- CONFIRMED: **the response bumps the car back.** For each axis the handler
  compares a velocity component against 0.0 (`[0x47F798]`), selects `-1.0`
  (`[0x47F808]`) or `+1.0` (`[0x47F7B4]`) accordingly, then **negates it with
  `fchs` (0x427EB7)** before applying it to the object — i.e. a unit impulse
  directed *against* the direction of travel. The same is done for the second
  axis at 0x427ED2, and for **both** colliding objects (indices at 0x4D8E50 and
  0x4D8E54).
- CONFIRMED: before applying the impulse the handler sets two per-object values
  on both participants: `fcn.00435B80(obj, 200000)` writes `[obj+0x28]` and
  `fcn.00436070(obj, 10000)` writes `[obj+0x174]` — plausibly a collision
  timer/force budget, not yet identified.
- CONFIRMED: `-> Inverting collision structure.` (fcn.00427730) exists for the
  case where the two objects are considered in the opposite order.
- Practical consequence, verified in the port: a response that merely *scrubs*
  speed on contact makes a car grind to a permanent halt against a barrier,
  and a car that stops while mis-aligned can deadlock. Reversing part of the
  motion on impact, as the original does, lets it recover and drive on.

## AI vehicle table (session 6)

- CONFIRMED: spel.dat holds a static **AI vehicle table at VA 0x484B18**
  (file 0x82318): **16 records of 238 bytes**, each beginning with a
  NUL-terminated name. The records are, in order:
  `RACER_1_ANIM` .. `RACER_7_ANIM` (the seven buyable cars, in `CARS.DAT` slot
  order), `MPOLIZEI_ANIM`, `ADAC_ANIM`, then the seven traffic types
  `N_GOLF`, `N_ASTRA`, `N_BUS`, `N_VRTAUT`, `N_POLO`, `N_BESTEL`, `N_CALIBR`.
- CONFIRMED layout: `struct { char name[14]; u32 field[56]; }` = 238 bytes.
  The name field is 14 bytes because the longest names (`MPOLIZEI_ANIM`,
  `N_VRTAUT_ANIM`) are 13 characters plus a NUL. Scanning `.text` for absolute
  addresses inside the table yields **exactly 56 distinct offsets, +14 through
  +234 in steps of 4** — an exact match for 14 + 56*4 = 238, and independent
  confirmation of both the stride and the start of the field area.
  (Per-record access is done with the record index in a register, which is why
  only record 0's field addresses appear as constants.)
- The consumers sit in `ai_*.cpp` (0x40C9xx-0x40E8xx), the object setup around
  0x4284xx, and a few in `render_game` (field +218 is read at 0x425D85..0x425E10).
- **CONFIRMED (session 6): the table is the per-vehicle *handling* table, and
  the player's purchased upgrades are applied to it in place** by
  `fcn.00428400` (called from `init_player_car`). That function indexes the
  table as `238 * g_player_car_index` — computed as
  `eax*7 -> *29 -> *59 -> *119 -> <<1`, independently re-confirming the
  238-byte stride — resets field +218 to 1.0f, and then:

  | game.ini `[PLAYER]` key | global | effect on the car's record |
  |---|---|---|
  | `motor` | 0x4D9EC0 | +110 x= 1.3, +114 x= 1.1, +106 x= 1.1 |
  | `banden` (tyres) | 0x4D9EC4 | +74, +82, +86, +94 x= 1.2 |
  | `turbo` | 0x4D9EC8 | (read, used elsewhere) |
  | `abs` | 0x4D9ECC | sets the global flag at 0x4A63C8 |
  | `versnellingsbak` (gearbox) | 0x4D9ED0 | copies fields +170..+206 over +122..+158 |
  | `injectie` | 0x4D9ED4 | (read, used elsewhere) |

  All six keys are read consecutively by `ini_load_all` at 0x421D89..0x421E0E.
- CONFIRMED consequences for the field groups:
  - **+106, +110, +114 are engine/acceleration** parameters (scaled by `motor`);
  - **+74, +82, +86, +94 are grip/cornering** parameters (scaled by `banden`) —
    which fits the data, since these are exactly the fields where the seven
    racers differ (10.7-16.0) while every traffic type shares one value (8.426);
  - **+122..+158 is a 10-field gearbox block with an alternate set at
    +170..+206**, swapped in wholesale by the gearbox upgrade — the same
    stock/upgraded pattern `CARS.DAT` uses for its gear arrays.
- So the two tables are complementary: `CARS.DAT` holds **performance** (per-gear
  acceleration and speed ceilings) and this table holds **handling**, both keyed
  by the same car slot / `RACER_<n>_ANIM` identity.
- Remaining field semantics are still unidentified; a "Difficulty modifier has
  invalid value" string sits immediately before the table and the multipliers
  used here are 1.1/1.2/1.3.

## In-game HUD sprites (session 6)

`RC.IMG` is the in-game HUD art (453 sprites, binary IND). Grouping consecutive
entries by size reveals the gauges, since the original blits pre-rendered frames
rather than rotating anything:

| entries | size | content |
|---|---|---|
| 0, 32..50 | 49x71 | **20 needle positions of a round gauge** (0 = rest) |
| 1 | 148x114 | the large speedometer dial, 20..250 marked, needle baked in at rest |
| 11..21 | 91x8 | an 11-state horizontal bar |
| 22..31 | 61x31 | a 10-state slanted icon (fuel pump artwork) |
| 54..64 | 49x3 | an 11-state thin bar |
| 187..212, 213..238, 239..264, 267..292 | — | four runs of exactly 26 frames |
| 354..401 | 123x85 | a 48-frame animation |

- CONFIRMED: the gauges are frame sequences, not rotated sprites — sprite 0 and
  32..50 are the same 49x71 dial with the needle drawn at 20 successive angles.
  Driving the frame index from speed reproduces a working speedometer.
- Note the in-game *numbers* (speed, times) are not in RC.IMG: they are drawn
  with the 3D font, `FONT3D_ANIM`'s 49 `LTR_*.d3d` glyph meshes on
  `s_letter.pcx`.

## The 3D font (session 6)

- CONFIRMED: the numbers and text drawn over the track (speed, gear, race and
  checkpoint times, position) are **extruded 3D glyph meshes**, not sprites.
  `FONT3D_ANIM` is the only anim with a large frame count -- **49 frames**, each
  a `LTR_*.d3d` mesh textured with `s_letter.pcx`. Full charset in frame order:

  | frames | glyphs |
  |---|---|
  | 0..9 | `LTR_0` .. `LTR_9` (digits) |
  | 10..35 | `LTR_A` .. `LTR_Z` |
  | 36 | `LTR_T` again (a duplicate entry) |
  | 37..40 | `LTR_AE`, `LTR_OE`, `LTR_SS`, `LTR_UE` -- the German umlauts and eszett |
  | 41..48 | `LTR_IS`, `LTR_AANH`, `LTR_LINE`, `LTR_MAAL`, `LTR_PLUS`, `LTR_PUNT`, `LTR_UITR`, `LTR_VRAG` |

  The tail names are Dutch: *aanhalingsteken* (quote), *line* (dash), *maal*
  (multiply), *punt* (full stop), *uitroepteken* (!), *vraagteken* (?). The
  umlaut glyphs exist because `language.ini` supports a German build.
- Glyph height is ~16.9 world units; each glyph's advance width comes from its
  own mesh bounds, so the font is proportional rather than fixed-pitch.
- Practical note for a port: these glyphs are extruded several units in Z, so
  drawing them in a 2D overlay needs a generous orthographic depth range. With
  the usual `glOrtho(..., -1, 1)` every glyph is clipped away entirely and the
  text silently does not appear.

## Track object types and race furniture (session 6)

- CONFIRMED: the `.omp` record's **`type` field is a behaviour/collision class**,
  not a trigger id. Grouping all 7 tracks' objects by type against their anim
  names gives an unambiguous reading:

  | type | count | contents |
  |---|---|---|
  | 0 | 4584 | ordinary static scenery (buildings, lamp posts, trees) |
  | 1 | 9 | assorted one-offs |
  | 2 | 144 | **pylons / cones** (`A_PILON1`, `C_PILON1`) |
  | 3 | 506 | **construction barriers and fences** (`C_CNSTR0`, `C_HEK1..6`) |
  | 4 | 49 | **traffic lights** (`C_STPLR2`, `C_STOPLG`) |
  | 5 | 496 | **columns / pillars** (`E0x_COL1..3`, `E0x_MID2`) |
  | 7 | 37 | **speed cameras** (`A_FLTSRP`, `C_FLTSRP` -- a *flitspaal*) |
  | 8 | 56 | **direction arrows** (`A_PIJL1`, `C_PIJL1`, `A_LPIJL1`) |
  | 9, 10, 21, 43 | 95 | **police placements** -- all `MPOLIZEI_ANIM` |

- CONFIRMED: every stage carries **five or six speed cameras**; police
  placements range from 3 (etappe3) to 31 (etappe6). Traffic lights appear only
  on the city stages (etappe2: 13, etappe4: 23, etappe6: 13) and pylons only on
  the motorway ones. The four police types presumably distinguish behaviours
  (parked, patrolling, ambush) -- not yet confirmed.
- CONFIRMED: **speed limits are signposted by scenery objects whose name carries
  the number** -- `A_MAX50S`, `A_MAX80S`, `A_MX120S`, `C_MAX60S` -> 50, 80, 120,
  60 km/h. They alternate along the route (etappe5 has 12 of them), so the limit
  in force at a chunk is the most recent preceding sign. Combined with the type-7
  cameras this is the whole `flits` mechanic: `flits.wav` is the flash and
  `language.ini` has the fine and "radar bonus" strings.

## Triggers and AI speeds (session 6)

Earlier notes called the `tr_*.cpp` trigger volumes "unlocated" and assumed they
were checkpoint geometry. That framing was wrong.

- CONFIRMED: the triggers are an **AI system**, not checkpoint volumes. The
  strings around `check_racer_trigger` give the whole enum:
  `TRIGGER_BENZINE_STATION`, `TRIGGER_FINAL_APPROACH`, `TRIGGER_FUEL_APPROACH`,
  **`TRIGGER_ZERO_CHUNK_MARKER`**, **`TRIGGER_FINISH_VALID`**,
  **`TRIGGER_FINISH_POSITION`**, plus `Racer (obj %d) has finished.`
  Alongside them sit per-chunk **lane triggers** and **speed triggers**
  (`Lane trigger value = %d`, `Speed trigger value = %d`,
  `BACKTRACK_SPEED_TRIGGER`, `ILLEGAL LANE TRIGGER!!!`), which steer the AI's
  lane and speed choice rather than marking race progress.
- CONFIRMED: **lap and finish detection is chunk-based in the original too** —
  all of these markers live in the per-chunk trigger slots.
  `TRIGGER_FINISH_VALID` and `TRIGGER_FINISH_POSITION` handle finishing and
  placing.
  (Revised session 7: an earlier version of this bullet went further and called
  "the chunk index wrapping past the end" *the* lap mechanism, on the strength
  of the name `TRIGGER_ZERO_CHUNK_MARKER`. That was an inference from a string,
  not from code, and it is not safe: on every circuit triggers **13 and 14 share
  a single chunk** — the start/finish line — which is the more natural place to
  count a lap. No code has been found that settles which one the original uses,
  so the marker's exact role stays open; see the open questions.)
- CONFIRMED: **AI speed comes from the AI state machine, not the vehicle table.**
  The log prints `Current ai_state = %d. Max speed allowed = %f.` and the
  runtime allocates `t_ai_state AI_States [50]` and `t_ai_behavior AI Behaviors
  [16]`. Observed `ai_chosen_speed` values in a real race: **24.0 and 25.0 m/s
  (86-90 km/h) for traffic, and 49.9 / 53.9 / 63.9 / 70.0 m/s (180-252 km/h)
  for racers** — the same m/s units as CARS.DAT.
  This redirects open question #33: the traffic-speed figure was never going to
  be in the 238-byte handling table.
- The AI also labels road width as `1-baans` .. `4-baans` (1- to 4-lane), which
  matches the varying lane counts in the `.dmp` graph.

## The .pmp per-chunk trigger fields (session 6)

The two per-chunk `.pmp` fields previously logged only as "f3/f4, small ints
0..45, meaning unidentified" are the game's **per-chunk trigger slots**.

- CONFIRMED: the trigger lookup `fcn.00408080` addresses them as
  `chunk_index * 388 + 0xEC` — 388 is the known `c_chunk` stride and **+0xEC is
  exactly where the `.pmp` reader (fcn.00442B00) stores f3**, with f4 at +0xF0.
  The reader filled those two in a 2-iteration loop, so a chunk carries two
  trigger slots.
- CONFIRMED: **0 means NO_TRIGGER** (the function prints `NO_TRIGGER = %d` with
  0). Non-zero values are dispatched through a jump table at 0x408390 with a
  byte case-index table at 0x4083A4 covering values 4..43:

  | trigger values | handler |
  |---|---|
  | 4, 5, 6, 8 | **speed trigger** (`Speed trigger found: %d.`) — but see the police section below: in ai_cops.cpp the real values are **4..7** and 8 is the illegal case |
  | 18, 19, 20, 21 | **lane trigger** (`Lane trigger found: %d.`) |
  | 40, 41, 42, 43 | **lane trigger**, second path |
  | anything else in 4..43 | ignored (default case) |

- CONFIRMED by the data: across all 7 tracks' 5524 chunks, f3 is 0 in 5136 and
  f4 in 5420, and the non-zero values fall almost entirely inside those
  recognised ranges — 261 lane and 25 speed triggers in f3, 74 lane and 2 speed
  in f4. The exact clustering on {4,5,6}, {18..21} and {40..43} is what
  confirms the switch reading.
- A tail of out-of-range values also occurs (1, 2, 9, 13-15, 22-25, 31-34, 45,
  86, 94, 107, 130, 150, 196). **Session 10 split that tail in two:** 1, 2, 9,
  13-15, 22-25, 31-34 and 45 all have registered handlers (see "The master
  trigger-handler table"), while **86, 94, 107, 130, 150 and 196 are inert** --
  nothing is registered above 48 and the dispatcher skips a null slot.
  Those are not lane/speed triggers; the trigger
  enum has several other kinds (`TRIGGER_BENZINE_STATION`,
  `TRIGGER_FINAL_APPROACH`, `TRIGGER_FUEL_APPROACH`,
  `TRIGGER_ZERO_CHUNK_MARKER`, `TRIGGER_FINISH_VALID`,
  `TRIGGER_FINISH_POSITION`) which are checked by other functions, so those
  values are most likely theirs.

## Race markers: the trigger values decoded (session 6)

`check_racer_trigger` (fcn.0040B030) dispatches trigger values 2..47 through a
byte case-index table at 0x40B294 and a 7-entry jump table at 0x40B278.
Resolving each branch to the name it logs gives:

| value | branch | meaning |
|---|---|---|
| **9** | 0x40B122 | `TRIGGER_ZERO_CHUNK_MARKER` |
| **14** | 0x40B050 | `TRIGGER_FINISH_POSITION` |
| **15** | 0x40B0FB | `TRIGGER_FINISH_VALID` |
| **2** | 0x40B1FE | `TRIGGER_BENZINE_STATION` |
| **46** | 0x40B13E | `TRIGGER_FUEL_APPROACH` |
| **47** | 0x40B19E | `TRIGGER_FINAL_APPROACH` |
| all others in 2..47 | 0x40B256 | not a racer trigger |

The same method re-derives the already-known lane/speed switch
(4/5/6/8 speed, 18-21 and 40-43 lane), which is what validates it.

- CONFIRMED: these markers live in the `.pmp` per-chunk trigger slots, and their
  placement matches the stage types exactly:

  | stage | zero-chunk (9) | finish (14) | finish-valid (15) |
  |---|---|---|---|
  | etappe0 | chunk 0 | 524 | 525 |
  | etappe1 | — | 763 | 48 |
  | etappe2 | chunk 0 | 16 | 17 |
  | etappe3 | — | 539 | 103 |
  | etappe4 | chunk 0 | 329 | 330 |
  | etappe5 | — | 795 | 72 |
  | etappe6 | chunk 0 | 524 | 525 |

- CONFIRMED: **only the circuits carry the zero-chunk marker, always at chunk
  0**, which independently confirms the circuit/point-to-point split derived
  earlier from object spans and checkpoint gantries. (Revised session 7: this
  bullet also used to conclude that laps are therefore counted at the chunk-0
  wrap. Chunk 0 being where the marker sits does not establish that — see
  above.)
- CONFIRMED: **point-to-point stages carry `FINISH_VALID` near the start**
  (chunks 48 / 103 / 72) **and `FINISH_POSITION` at the end** (763 / 539 / 795)
  — so `FINISH_VALID` plausibly arms the finish and `FINISH_POSITION` is the
  line itself. On the circuits the two sit adjacent mid-track instead
  (524/525, 16/17, 329/330), consistent with a lap line rather than an end.
- etappe0 and etappe6 carry identical marker chunks (524/525), matching their
  shared geometry.

### Fuel stations: trigger 2 sits on the pump model (session 7)

Cross-checking every `TRIGGER_BENZINE_STATION` (value 2) chunk against the
fuel-station scenery gives an **exact match on all 7 tracks, 8/8 triggers**:

| stage | trigger 2 at chunk | pump object on that chunk | approach props (earlier chunks) |
|---|---|---|---|
| etappe0 | 764 | `C_POMPEN_ANIM` 764 | — |
| etappe1 | 549 | `A_TANK1/2_ANIM` 549 | `A_TANKEN` 530, `A_TANK3` 542 |
| etappe2 | 315 | `C_POMPEN_ANIM` 315 | — |
| etappe3 | 397 | `A_TANK1/2_ANIM` 397 | `A_TANKEN` 382/392, `A_TANK3` 393 |
| etappe4 | 72 | `C_POMPEN_ANIM` 72 | — |
| etappe5 | 399, 642 | `A_TANK1/2_ANIM` 399, 642 | `A_TANKEN` 384/622, `A_TANK3` 393/635 |
| etappe6 | 764 | `C_POMPEN_ANIM` 764 | — |

- CONFIRMED: the trigger is placed on the chunk carrying the **pump** mesh, not
  on the forecourt approach props. etappe5 is the only stage with two stations,
  and it carries exactly two triggers.
- CONFIRMED: the two naming families are **`C_POMPEN_ANIM` on the four circuits**
  and **`A_TANK1/2/3` + `A_TANKEN` on the three point-to-point stages** — the
  `C_` / `A_` prefixes track the circuit/stage split seen throughout the scenery
  (Dutch *pompen* = pumps, *tanken* = to refuel).
- This is independent confirmation of the value-2 branch decode above: the name
  `TRIGGER_BENZINE_STATION` was read out of the disassembly, and the data places
  it on exactly the objects that name predicts.

### Trigger 45 is the checkpoint (session 7)

Value 45 was previously unclassified (it is outside the lane/speed ranges and
falls into `check_racer_trigger`'s default branch). Its placement identifies it:

| stage | trigger 45 chunks | `A_CHECK*` gantry chunks |
|---|---|---|
| etappe1 | 148, 295, 443, 595 | 148, 295, 442, 594 |
| etappe3 | 207, 312, 425 | 206, 311, 424 |
| etappe5 | 195, 329, 473, 627 | 194, 328, 472, 626 |
| circuits (0, 2, 4, 6) | none | none |

- CONFIRMED: **11 triggers, 11 gantries, 1:1 on every track**, each trigger on
  the gantry's own chunk or the one immediately after it, and both absent from
  all four circuits.
- STRONGLY SUPPORTED: value 45 is the checkpoint trigger, i.e. the game's own
  checkpoint mechanism is the trigger slot and the `A_CHECK*` gantry is only its
  visible marker. The port previously derived checkpoints from the gantry
  objects; it now reads trigger 45, which is the authoritative source.

### Trigger census, and two values that never appear in data (session 7)

Every non-zero trigger slot across all 7 tracks:

| value | total | meaning |
|---|---|---|
| 1 | 37 | unidentified; 5-6 per track, evenly spaced, on **every** track |
| 2 | 8 | `TRIGGER_BENZINE_STATION` |
| 4, 5, 6 | 27 | speed trigger — 60 / 80 / 120 km/h respectively (corrected session 12) |
| 9 | 4 | `TRIGGER_ZERO_CHUNK_MARKER` (circuits only) |
| 13 | 7 | unidentified; **exactly once per track**, see below |
| 14, 15 | 14 | `TRIGGER_FINISH_POSITION` / `TRIGGER_FINISH_VALID` |
| 18-21, 40-43 | 332 | lane trigger |
| 22-25, 31-34 | 42 | unidentified; stages only, in two parallel 4-value groups |
| 45 | 11 | checkpoint |
| 86, 94, 107, 130, 150, 196 | 7 | **inert** -- no handler is registered above 48 and the dispatcher skips a null slot (session 10) |

- CONFIRMED: **values 46 and 47 (`FUEL_APPROACH`, `FINAL_APPROACH`) occur in no
  track file at all.** Since `check_racer_trigger` handles them, they must be
  raised at runtime by the AI rather than read from a chunk slot — consistent
  with their names describing a racer's *state* (approaching fuel, approaching
  the finish) rather than a place.
- CONFIRMED: value **13 appears exactly once per track**, and its position
  relative to 14/15 splits cleanly by stage type:

  | stage | 13 | 14 (finish) | 15 (finish-valid) |
  |---|---|---|---|
  | circuits 0/6 | 524 | 524 | 525 |
  | circuit 2 | 16 | 16 | 17 |
  | circuit 4 | 329 | 329 | 330 |
  | etappe1 | 47 | 763 | 48 |
  | etappe3 | 101 | 539 | 103 |
  | etappe5 | 71 | 795 | 72 |

  On the circuits 13 and 14 share a chunk; on the stages 13 sits at the start
  with 15 right behind it while 14 is at the far end.
- CONFIRMED (session 7, via the STARTCHUNK path — see the Tr_finis.cpp
  section): 13 is the **player's start line**; the game starts one chunk before
  it when `Game.ini`'s `startchunk` is unset. That reading makes one mechanism
  out of both columns: `FINISH_VALID` (15) always sits one or two chunks *after* the start
  marker, arming the finish so that crossing the line at the start of a lap
  cannot immediately end the race on a circuit where start and finish are the
  same chunk.

## `.dmp` field 3 is the waypoint height (session 7)

The `.dmp` waypoint record was parsed as
`u32 type; u32 f1; float x; u32 f3; float z; i32 link[4]`, with `f1` and `f3`
unidentified. **`f3` is the waypoint's `y`**, so the three coordinates are in
natural order.

- CONFIRMED: `f3`, read as a float, equals the owning chunk's `y` for **28452 of
  the 28461 waypoints** across all 7 tracks (99.97%).
- CONFIRMED: it follows **bridges**. On etappe0 chunks 411-419 it runs
  1, 2, 4, 6, 7, 6, 4, 2, 1; on etappe3 chunks 527-536, 1, 3, 6, 10, 15, 15, 10,
  6, 3, 1 — up and back down over the span.
- The nine waypoints whose `y` differs from their chunk's are **off the chunk
  centre**, among them the etappe1 fuel-station slip road at chunks 399/400
  already identified from its lateral offset. A per-waypoint height is exactly
  what those need, so the exceptions corroborate the reading.
- `f1` remains unidentified but is **0 in all but 4 of the 28461 waypoints**
  (value 2 in those), so almost nothing is left to identify.
- `type` takes 0, 2 and 4, sometimes with bit 7 (128) set; the flag is not
  identified.

The port now seeds each lane point's height from the waypoint's own `y` before
refining it against the road planes, so a lane over a viaduct is at the right
height even where the plane lookup misses.

## The only animated props, and what the light sequence is (session 7)

Walking every anim's frame list shows that **13 anims have more than one
frame**, and only five of them are placed as `.omp` objects:

| anim | frames | placed |
|---|---|---|
| `C_STPLR2_ANIM` | 3 | 35 |
| `C_STOPLG_ANIM` | 3 | 27 |
| `C_PIJL1_ANIM` | 2 | 38 |
| `A_PIJL1_ANIM` | 2 | 11 |
| `A_LPIJL1_ANIM` | 2 | 8 |

(The other eight are `FONT3D_ANIM`'s 49 glyphs, the six
`E0x_ALGEMENE_TEXTURES_ANIM` plane-texture sets, and `RADAR_TEXTURES_ANIM`'s
lo/hi pair — none of which is scenery.)

- CONFIRMED: **the traffic-light frame order is green, amber, red**, because the
  meshes are named for the Dutch colours: `c_stoplg / c_stoplo / c_stoplr` and
  `c_stplg2 / c_stplo2 / c_stplr2` — **g**roen, **o**ranje, **r**ood — in that
  order in the frame list.
- CONFIRMED: the arrows are two-state flashers (`A_pijl0` / `A_pijl1`, and the
  same for `A_lpijl` and `c_pijl`).
- CONFIRMED: **119 animated objects across the 7 tracks, 300 frames in total.**
- Still unknown: **frame durations.** No frame-time field has been found in the
  anim tables, so how long each light phase lasts is not recoverable from the
  data examined so far.

## The 0x484B18 table is a full vehicle physics model (session 8)

The 16 x 238-byte table at VA 0x484B18 was recorded as "`char name[14]` + 56
fields, ~35 unidentified". Dumping all 56 as floats across the 16 vehicles
identifies most of them outright, because the table is in **real-world units**
and three fields are physical constants:

| field | offset | meaning | evidence |
|---|---|---|---|
| 2 | +22 | **mass, kg** | Trabant 600, Astra 1070, Diablo 1050, van 1700, **bus and lorry 15000** |
| 3, 4 | +26, +30 | **length and width, m** | 4.50 x 1.70 for every car, **11.50 x 2.50 for the bus** |
| 12 | +62 | **gravity** | constant **9.81** |
| 13 | +66 | **air density** | constant **1.2** (kg/m^3) |
| 22 | +102 | **drag coefficient Cd** | 0.29-0.33 for cars, **0.50 for the bus** |
| 23 | +106 | **redline, rpm** | 9000 racers, 6500 traffic, **2200 bus** |
| 24 | +110 | **power** | Trabant 45, Astra 148, BMW 280, Porsche 350, Mercedes 400, **Diablo 580**, bus 1000 |
| 25 | +114 | **rpm at peak torque** | 3500 (Trabant/Astra/VW) rising to 5200 (Diablo), 1600 bus |
| 28-33 | +126..+146 | **gear ratios 1-6** | Diablo 2.31 / 1.52 / 1.12 / 0.88 / 0.68, -1 = absent |
| 34 | +150 | **final drive** | 5.00 Trabant, 3.83 Diablo, 3.50 bus |
| 40-45 | +174..+194 | **the alternate (upgraded) gear set** | same shape; where the stock set has -1 in 6th, this one has a ratio |
| 46 | +198 | alternate final drive | |

- CONFIRMED by physical plausibility across the whole fleet: a 600 kg Trabant
  with 45 power against a 15 t bus with Cd 0.50 and a 2200 rpm redline is not a
  coincidence, and the gear ratios and final drives are realistic sets.
- This supersedes the earlier partial reading (which had +106/+110/+114 as
  "engine" and +122..+158 as "a gearbox block"): those are redline, power, peak
  torque rpm, and the gear ratios plus final drive, with the alternate set
  being the **VERSNELLINGSBAK upgrade**.
- Further fields identified while building the port's car on this table:
  **field 5 is height in metres** (cars 1.01-1.35, **bus 4.00**), so width x
  height gives a frontal area of ~1.7 m^2 for a car and 10 m^2 for the bus;
  **field 8 is centre-of-gravity height** (Diablo 0.33, ordinary cars 0.38,
  **bus 0.65**); **field 21 is the rolling-resistance coefficient**, constant
  0.02; and **field 10 is the 0-100 km/h time in seconds** (Trabant 13, Astra
  10, BMW 7, Porsche 5, Diablo 5) — which makes it an independent check on any
  model built from the rest.
- CONFIRMED (session 8): **fields 15-20 are two per-axle triples, front then
  rear**, and the rear is the stiffer of the two in every racer. The third of
  each triple -- **fields 17 and 20 -- is cornering grip**, because it orders
  exactly by how quick the car is and is flat across all traffic:

  | car | front (f17) | rear (f20) |
  |---|---|---|
  | Trabant | 0.59 | 0.65 |
  | Astra, VW | 1.52 | 1.90 |
  | BMW | 1.90 | 2.05 |
  | Porsche | 2.05 | 2.35 |
  | Mercedes, Diablo | 2.95 | 3.50 |
  | every traffic vehicle | 1.05 | 1.05 |

  The first of each triple (fields 15/18: 8.43 for all traffic, 10.7-16.0 for
  racers) and the second (16/19: 1.11 racers, 1.70 traffic) are **not**
  identified.
- Structure of the rest (session 8): **fields 35-39 and 47-52 mirror each other
  exactly**, and sit immediately after the stock gearbox block (28-34) and the
  upgraded one (40-46), so they belong to those blocks rather than standing
  alone. Within each, one field is a constant 1.0, one is a 0/1 flag set only
  for the Astra and the VW, and the pair 51/52 is 1.00/1.50 for every vehicle
  **except the Diablo, which has 2.00/4.50**. Field 55 is a small integer
  (31-36) that varies with vehicle type including across otherwise-identical
  traffic entries, which fits a sound-sample id. Field 53 ascends with
  performance like the grip pair (0.75 Trabant to 2.25 Diablo, 1.00 for all
  traffic) and field 54 is 1.20 for racers against 1.00 for traffic.
  None of these is named here: the shapes are suggestive, the meanings are not
  established.
- Still unidentified: fields 1, 6, 7, 9, 11, 15, 16, 18, 19, 26, 27, 35-39,
  47-55. Field 9 is 1.80 for every car but **9.00 for the bus** and slightly
  lower for the Diablo and Porsche, which suits a wheelbase-like length but does
  not match real figures closely enough to name.
- **The table has no torque curve and no wheel size.** Power, the peak-torque
  rpm and the redline are there, but not the shape between them, so any
  simulation has to supply one. Calibrating a synthesised curve against the two
  figures the game does publish — CARS.DAT's rated top speed and this table's
  own field 10 — pins it well: in the port, all seven cars then reach their
  rated top speeds exactly and five of the seven their 0-100 times exactly.

### SPECULATIVE: this may be the *other* steering model

Game.ini's `stuur_model` selects "Old (Autobahn Raser I)=1" or "New (A2 Racer
II)=0", and the game therefore carries two vehicle models. `CARS.DAT` holds
per-gear **speed ceilings and acceleration constants** for the 7 player cars
only; this table holds a **physical** model (mass, power, rpm, ratios, Cd,
frontal-area terms) for all 16 vehicle types. That is the natural pairing for
two selectable models, but no code has been traced from `stuur_model` to either
table, so this is a hypothesis, not a finding.

## A speed-dependent drag curve, and what CARS.DAT A[29] does (session 8)

Following `CARS.DAT` field 29 to its only use (0x40F90D) lands in the per-frame
car update, and two adjacent terms decode.

### STRONGLY SUPPORTED: the drag on `[car+0x74]`

At 0x40F8B4..0x40F8D5, guarded by `fabs; fcomp qword [0x47F3D0]` (**20.0**):

    if (|v| > 20)  v -= (0.25 - 0.003 * v) * v * dt;

with 0.25 at 0x47F3A4 and 0.003 at 0x47F3D8. The coefficient falls to zero at
**v = 83.33**, and the threshold is **20**. Read as metres per second those are
**300 km/h** and **72 km/h** — both round numbers in km/h, which is why the
field is read as the car's speed in m/s (the same units CARS.DAT uses). The
resulting deceleration peaks near 5.2 m/s^2 at about 151 km/h and eases off
above that, so it adds weight in the mid range without capping the fast cars.
Not CONFIRMED: the identity of `+0x74` rests on those two round values plus a
`> 0` test on it nearby (a natural forward/reverse check), not on a name.

### CONFIRMED: A[29] is a per-frame decay rate on `[car+0x7C]`

    [car+0x7C] -= A[29] * x * dt          ; fmul [ecx*4 + 0x4A5F54]

The address settles the index: 0x4A5F40 is field 24 and the fields are 4 bytes
apart, so **0x4A5F54 is field 29**, with the usual 124-byte per-car stride.
`+0x7C` is itself driven from `+0x74` a few instructions later (a `-0.05`
coefficient at 0x47F3DC, applied with the opposite sign depending on whether
`+0x74` is positive), so the two are a coupled pair.

Its per-car values are strikingly ordered:

| car | A[29] |
|---|---|
| Trabant | 1.6 |
| VW / BMW / Porsche / Mercedes | 1.0 |
| Astra | 0.9 |
| Diablo | 0.1 |

A Trabant sheds this quantity **16x faster** than a Diablo, which marks it out
as a handling characteristic rather than bookkeeping. **What `+0x7C` is has not
been identified**, so A[29] is named only as "the decay rate of car state
+0x7C". A[26] remains 0 in every car and unused.

## The HUD's actual layout, and the bitmap fonts behind it (session 8)

A screenshot of the running game settles what the in-race overlay looks like,
and it is not what sprite-spelunking suggested.

- CONFIRMED (from the game's own display): everything is **stacked down the
  right-hand edge, right-aligned**:
  `RACE TIJD` + time; `RONDE INFO` with `TIJD` / `RECORD` / `LAATSTE`;
  `POSITIE n/m`; `RONDE n/m`; then lower down `SCHADE` with a bar and a `%`
  figure, `TURBO` with a chevron bar, and `SNELHEID` as **large digits** with a
  `km/h` unit. The **radar sits bottom-left** as a thin curved line tracing the
  road ahead -- which matches `create_radar` / `clear_radar` /
  `draw_road_in_radar_surface` being *procedural*, not a sprite.
- CONFIRMED: the labels are exactly language.ini strings **33 RACE TIJD, 34
  RONDE INFO, 35 TIJD, 36 RECORD, 37 LAATSTE, 38 POSITIE, 39 RONDE, 53 TURBO,
  54 SNELHEID, 55 SCHADE**.
- CONFIRMED: **the HUD is drawn with bitmap fonts**, not the 3D mesh font.
  RCS.IMG holds a whole family, each in a hires and a lores variant:
  `HiKMTel.fnt` / `LoKMTel.fnt` -- *km-teller*, the speedometer digits --
  plus `HiMid`/`LoMid`, `Higeel`/`Logeel` (*geel* = yellow),
  `HI_GELD`/`LO_GELD` (*geld* = money), `hi_vlag`/`lo_vlag` (*vlag* = flag),
  `hi_bts`/`LO_BTS`, `hi_req`/`lo_req`, `LO_FNT1..9`, `OLFNT01..09`,
  `MARC10.FNT`, `SLIDER13/15.FNT`. The HUD code references `lo_fnt3.fnt` by
  name. (The format is decoded below.)
- **Correction to the port:** its HUD had been built by looking at which sprites
  existed rather than at the layout, and put a speedometer **dial** in the
  bottom-right -- RC.IMG sprite 1 (marked 20..250) with the 19 needle frames at
  32..50. That art is genuine, but it does not appear in this HUD at all. The
  dial may belong to the other steering/HUD model or be unused; nothing
  establishes which. The port now follows the layout above and leaves the dial
  undrawn.

## The HUD has four detail levels, cycled with D (session 8)

- CONFIRMED: `Game.ini [GFX OPTIONS] overlay_detail` lands in the global
  **0x4D9B00** (written by fcn.00421FD0), and fcn.00424EE0 cycles it on the key
  **'D'** (0x44): `++detail; if (detail > 3) detail = 0`. So there are **four
  levels, 0-3**, exactly as the ini's own comment says ("min=0, max=3",
  "adjusted by the player when pressing the 'detail' key").
- CONFIRMED: individual HUD elements are gated on it, and they come in at
  different levels:

  | drawing function | condition | appears at |
  |---|---|---|
  | fcn.0041A410 | `detail > 1` | levels 2-3 |
  | fcn.00419B10 | `detail > 2` | level 3 only |
  | fcn.00419E00 | `detail > 2` | level 3 only |
  | fcn.0041A5F0 (the `CHECKPOINT` split) | `detail > 2` | level 3 only |

  which matches the ini calling level 3 "full detail: radar, all overlay
  graphics".
- CONFIRMED: `disable_overlay` is separate — its comment says that with it set
  "the user can't enable it by pressing the 'detail' key".
- CONFIRMED: **what the level-3 overlay adds is a set of labelled bars.**
  language.ini strings **53 = TURBO, 54 = SNELHEID (speed), 55 = SCHADE
  (damage)** are HUD labels, and the two level-3 drawing functions push sprite
  indices **11, 12, 13** — the head of the 11-frame chevron bar (sprites 11-21)
  in RC.IMG, which runs 0..10. So the full HUD shows turbo, speed and damage as
  bars beside the dial.
- **Searched for and NOT found: a switch between an "old" (Autobahn Raser I) and
  a "new" HUD.** Game.ini's `stuur_model` and `ai_model` do offer exactly that
  choice, but only for the **steering and AI models**, not the overlay. No ini
  key, global or menu string selects a dashboard style; `menu.dat` has no
  dashboard/HUD/cockpit strings either. What the game does have is the four
  detail levels above. `draw_dashboard` (fcn.0041A780) is a single routine
  drawing a time delta (`-%02d.%d`), not a selectable style, and the only
  branch in it is a TWEAK_MODE-gated debug display on the key 'L'.
- Also confirmed while looking: the original's driving keys are the **arrow
  keys** (`Game.ini [INPUT]` LEFT=37, RIGHT=39, UP=38, DOWN=40 — Windows VK
  codes), which is what leaves D free for the overlay.

## The camera system: ten slots, four player views, six behind TWEAK_MODE (session 8)

- CONFIRMED: `init_game_cameras` (fcn.00413ED0) calls **`init_cameras(10)`** —
  ten camera slots — then a named initialiser for each:
  `init_player_camera` (fcn.004147F0), `init_start_camera`,
  `init_temp_camera(7)`, `init_ai_cockpit_camera`, `init_topview_camera(3)`,
  `init_tankstation_camera`, `init_any_camera`, `add_camera`, and finally
  `change_active_camera` (fcn.004142B0). The two indices passed literally give
  **topview = slot 3** and **temp = slot 7**.
- CONFIRMED: **the player can switch view with the number keys.**
  `handle_game_cameras` (fcn.004144B0) polls keys through fcn.00466910 and calls
  the view setter fcn.00414B70:

  | key | view |
  |---|---|
  | `1` | 1 |
  | `2` | 3 (the topview camera) |
  | `3` | 5 — a **toggle**: it stores the current view in 0x4863CC and restores it on the second press |
  | `4` | 0 |

- CONFIRMED: **six further cameras are gated behind TWEAK_MODE.** The global
  0x4D9B24 (filled from `Raser.ini [HELPDESK] tweak_mode` by fcn.00421B70) is
  tested at 0x41455F, and only past that test does the function poll keys
  `5`, `6`, `7`, `8`, `9`, `0` for views 4, ?, 6, 7, 8 and 9. This is exactly
  what Raser.ini's own comment promises — "Enables special features: Tweak
  menu/**extra camera's**/framerate printing".
- CONFIRMED: `init_player_camera` places the camera at the car's position with
  **+15 units in Y** (`fsub` of the -15.0 at 0x47F4D0), i.e. 1.5 m up. No
  follow *distance* constant appears in it; the offset is applied per frame in
  `handle_game_cameras`.
- Earlier notes listed "cameras 1-4 (follow/cockpit/look-back/far-follow)". The
  count and the key bindings are now confirmed from code; **which slot is which
  view is not** — only topview = 3 is established.
- **Superseded in part (session 10):** the key table above reads key `2` as
  "view 3 (the topview camera)", which conflates two index spaces.
  `init_topview_camera(3)` assigns a camera **slot**; fcn.00414B70 takes a view
  **preset**. Preset 3 is a **cockpit view** (1 behind, 15.5 up) that activates
  slot **0**. "Slot 3 is the topview" still stands; "key 2 shows the topview"
  does not. See "The camera view presets, and a correction".

## The collision handler decoded: damage, and a retraction (session 8)

fcn.00427DE0 — recorded in earlier sessions as `player_car_collision` — splits
into two halves, and only the second is physics.

### Retraction: `[obj+0x28]` / `[obj+0x174]` are sound, not an impulse

- CONFIRMED: both are written through **C++ setters**, not directly:
  fcn.00435B80 writes `+0x28`, fcn.00436070 writes `+0x174`, and each then sets
  a bit in a dirty mask at `+0x170` and calls a notify routine (fcn.00435B20).
  The objects come from a **pointer table at 0x4E47C8**, not a flat stride.
- CONFIRMED: the third call, fcn.00435D80, takes **three** arguments and clears
  a 128-byte block at `+0xEC`. At init the five objects get unit triples
  (1,0,0), (0,1,0)…; on a collision it gets `(0, 0, ±1)` where the sign comes
  from comparing a float against **0.0** (0x47F798) andselecting **-1.0**
  (0x47F808) or **+1.0** (0x47F7B4).
- **Retracted:** an earlier note read the `fchs` at 0x427EB7 as "a unit impulse
  opposing the direction of travel, applied to both objects", and the port's
  collision bump-back was modelled on it. That reading does not survive: the
  value negated is one of ±1.0, fed to a three-component setter on a C++ object
  whose siblings are set to 30000/200000/10000 constants — the shape of
  **positional sound** (Game.ini has `3dsound=1`, "Enables/disables positional
  sound"), i.e. frequency, volume and a left/right **pan** for the impact
  sample, applied to both participants. **No collision impulse has been found
  in this function.** The port's bump-back is therefore an invention, not a
  reproduction.

### CONFIRMED: the damage model

The tail of fcn.00427DE0 is the part that changes game state:

    mag = (int)[0x4E6788]                  // impact magnitude
    if (mag < 18000)        damage = 0     // 0x4650: below this, nothing
    else {
        if (mag > 35000) mag = 35000;      // 0x88B8
        a    = (int)(mag * 1/350);         // 0x47F86C = 0.0028571428 -> 51..100
        maxd = (difficulty == 1) ? 12 : 6;
        dmg  = (int)(maxd * 0.01 * a);     // 0x47F84C = 0.01
        clamp dmg to [0, maxd];
    }
    add_damage(dmg);                       // fcn.00428390

- CONFIRMED: the 6-vs-12 selector is **`Game.ini [OPTIONS] difficulty`** — the
  global 0x4D9AB0 is filled from that key by fcn.00421B70 (0=EASY, 1=HARD). So a
  maximum-strength impact costs **12 damage on HARD and 6 on EASY**, against the
  0-100 `damage` value Game.ini stores, i.e. 8-16 heavy impacts to total a car.
- CONFIRMED: impacts below **18000** do no damage at all, and everything at or
  above **35000** does the same maximum; between them the cost rises linearly
  from about half the maximum to all of it (`a` spans 51..100).
- The branchless `dec/neg/sbb/and/add` at 0x427F7C..0x427F87 is the usual
  compilation of `maxd = difficulty ? 12 : 6`.

## garage.ini names the seven cars (and contradicts itself) (session 7)

`garage.ini` is commented like the other ini files and settles which car each
CARS.DAT slot is — a question left open when the slot names were first recovered
from the TWEAK_MODE menu.

- CONFIRMED: the file names the cars **twice, and the two disagree**. The
  `[prices]` block comments read
  `car1 Trabant, car2 Astra, car3 VW, car4 VW, car5 BMW, car6 Porsche, car7 Diablo`,
  while the item-section headers read
  `== TRABANT / ASTRA / VW / BMW / PORSCHE / MERCEDES / DIABLO ITEMS ==`.
  From car4 on they differ by one name.
- CONFIRMED: **the item headers are the ones that match the meshes.** RACER_1..7
  name `r_trabbi, r_astra, r_beetle, r_bmw, r_porche, r_clk, r_diablo`, and
  `r_clk` is a Mercedes CLK — there is no second VW mesh. So the seven cars are
  **Trabant, Astra, VW, BMW, Porsche, Mercedes, Diablo**, and the `[prices]`
  comments carry a developer slip.
- This also makes the CARS.DAT speed ladder read sensibly against the price
  ladder:

  | slot | car | top speed | garage.ini price |
  |---|---|---|---|
  | 0 | Trabant | 151 km/h | 15 000 |
  | 1 | Astra | 166 | 25 000 |
  | 2 | VW | 180 | 40 000 |
  | 3 | BMW | 195 | 60 000 |
  | 4 | Porsche | 210 | 100 000 |
  | 5 | Mercedes | 225 | 150 000 |
  | 6 | Diablo | 240 | 200 000 |

- CONFIRMED: the six purchasable items per car are commented
  `motor / banden / reparatie per procent / turbo / remmen / versnellingsbak`
  (engine, tyres, repair-per-percent, turbo, brakes, gearbox). Five of them are
  the `[PLAYER]` flags in Game.ini — **`remmen` is what `ABS` buys** — and the
  sixth, repair, is priced per point of the 0-100 `damage` value.

## Raser.ini `[MAPS]`: the track table, with the developers' own comments (session 7)

`Raser.ini` had been mined only for `[HELPDESK]`, `[PROGRAM]` and `[PALET]`.
Its `[MAPS]` section is a **fully commented track table** and settles several
things that had been inferred:

    ; Arg 0:  0=highway(snelweg), 1=city(stad)
    ; Arg 1:  Filenames for maps (without extension)
    ; Arg 2, 3, 4, 5:  Textures to load for map (without ext.)
    ; Arg 6:  Lores unaccelerated filename of the sky
    ; Arg 7:  Hires unaccelerated filename of the sky
    ; Arg 8:  Name of the track (shown in main menu)
    ; Arg 9:  Zero-based index in music array
    ; Arg 10: Timer (should be kept 10000!)
    0=1,etappe0,e00_weg,e00_gras,e00_gra2,e00_vang,scrl_lo,scrl_hi,AMSTERDAM,0,10000
    ...
    count=9

- CONFIRMED: **the four `.rmp` plane lists take their textures in the order
  `weg, gras, gra2, vang`** — arg 2..5, listed per track. This closes open
  question #31, which had been left as "the natural reading, not proven".
  Note this is **not** the order of `E0x_ALGEMENE_TEXTURES_ANIM`'s frames
  (`gras, gra2, weg, vang`), so binding plane lists by anim frame index
  mis-assigns all four.
- CONFIRMED (from the decoded images): the atlases hold
  `_weg` road surface, `_gras` ground, **`_gra2` building facades**, and
  **`_vang` guardrails, viaduct spans, treelines and fences** — all
  colour-keyed. An earlier note in the port had `_gra2` and `_vang` swapped,
  which drew building facades along the guardrail planes.
- CONFIRMED: **arg 0 is the circuit flag** (`1` = city/stad, `0` = highway/
  snelweg), and it reads 1,0,1,0,1,0,1 for etappe0..6 — independently matching
  the circuit/point-to-point split derived from the trigger markers and object
  spans, and coming straight from the game's own config rather than inference.
  The port now reads both and warns if they disagree; on all 7 shipped tracks
  they never do.
- CONFIRMED: **the table declares 9 tracks, not 7.** `count=9`, with
  `7=0,etappe7,...,MÜNCHEN - BERLIN` and `8=1,etappe8,...,BERLIN`. Both reuse
  Dutch texture sets (etappe7 uses `e03_*`, etappe8 uses `e02_*`). **No
  `etappe7.*` or `etappe8.*` data files exist on the Dutch CD**, so the entries
  are present but unshipped — consistent with the `[DUITS]`/Autobahn Raser
  branding already found in `language.ini`.
- CONFIRMED: the **sky** is `scrl_lo` / `scrl_hi`, shared by every track;
  `SCRL_LO.pcx` and `SCRL_HI.pcx` are both present in `RCS.IMG`. The comment
  calls them the "unaccelerated" (software-renderer) sky files.
- CONFIRMED: `[MUSIC]` lists six tracks — `M_FUNK, M_HEAVY, M_HOP, M_REMIK,
  M_TRANS, M_ZZTOP` — and arg 9 selects one per stage (etappe0 -> M_FUNK,
  etappe1 -> M_ZZTOP, etappe2 -> M_TRANS, etappe3 -> M_FUNK, etappe4 ->
  M_HEAVY, etappe5 -> M_REMIK, etappe6 -> M_TRANS). A developer comment adds
  **"Music is currently disabled in start_game()"**, which explains why no
  music plays; no `M_*.wav` files ship on the CD either.
- CONFIRMED: `[PROGRAM] FOCUS` selects the render target — `0=BACK_SCREEN`
  (video memory), `1=BUFFER_SCREEN` (system memory) — and ships as 1.
- CONFIRMED: the `[wait_hi]` comment states that in 8-bit mode the given
  extension is used and **in 16-bit mode the extension is replaced with
  `.TGA`**, which is the general rule for this game's art: every `.pcx` has a
  true-colour `.TGA` twin for 16-bit mode.

## `load_track_info`: the in-memory track record (session 7)

`fcn.004225D0` = **`load_track_info`** in `C:\work\A2racII\Game\Gm_ini.cpp`
parses Raser.ini's `[MAPS]` rows into a static array. Every field offset below
is read off the stores in that function, and the two sky fields land exactly
where `init_moving_sky` independently reads them — which is what validates the
layout.

- CONFIRMED: the array base is **0x4D8E9C**, the stride **137 bytes**
  (`add ebx, 0x89`), the count is at **0x4D8E98**, and the loader rejects
  anything outside 1..20 tracks (`Invalid nmb of tracks %d`). Each row must have
  exactly **11 arguments** (`Invalid number of arguments for track %d`).
- CONFIRMED layout (offsets from the record start; the loader's `ebx` points at
  start+4):

  | offset | type | arg | meaning |
  |---|---|---|---|
  | +0x00 | u32 | 0 | circuit flag — **1 = city/stad, 0 = highway/snelweg** |
  | +0x04 | char[14] | 1 | map stem, e.g. `etappe0` |
  | +0x12 | char[14] | 2 | road texture, stored as **`<arg>.pcx`** |
  | +0x20 | char[14] | 3 | gras texture, `<arg>.pcx` |
  | +0x2E | char[14] | 4 | gra2 texture, `<arg>.pcx` |
  | +0x3C | char[14] | 5 | vang texture, `<arg>.pcx` |
  | +0x4A | char[14] | 6 | sky, lores, `<arg>.pcx` |
  | +0x58 | char[14] | 7 | sky, hires, `<arg>.pcx` |
  | +0x66 | char[30] | 8 | track name shown in the menu |
  | +0x84 | u8 | 9 | music index into `[MUSIC]` |
  | +0x85 | u32 | 10 | timer (unaligned; Raser.ini says "should be kept 10000") |

- CONFIRMED: the six texture/sky fields are written with `sprintf(dst,
  "%s.pcx", token)`, so **the record holds the filename with `.pcx` already
  appended**, not the bare stem. The map stem (arg 1) and the track name
  (arg 8) are plain copies.
- CONFIRMED: string arguments are extracted with a 13-character limit (arg 8
  with 30), which is why the fields are 14 and 30 bytes wide.
- Cross-check: `init_moving_sky` reads the sky filename from
  `0x4D8EE6 + idx*137` (lores) and `0x4D8EF4 + idx*137` (hires). With the base
  above those are exactly **+0x4A** and **+0x58** — the two sky fields. The two
  functions were reversed independently, so this is a genuine confirmation of
  both.

## Laps, time checks and trigger 13 (`Tr_finis.cpp`) (session 7)

Chasing Game.ini's `[BEST TIMES]` keys into the binary gives the lap system and
settles what trigger value 13 is.

- CONFIRMED: **trigger 13 marks the player's start line.** fcn.0042A460 walks a
  list of chunk indices, tests each chunk's **trigger slots at +0xEC and +0xF0
  against the literal 13** (0x42A48B, 0x42A49C), and returns the chunk
  *immediately before* the match (`dec edx`). `init_game` (0x423D20) calls it at
  0x4241EB **only when `Game.ini`'s `STARTCHUNK` is unset**, storing the result
  in the STARTCHUNK global 0x4D9A88 — the same global fcn.00421B70 fills from
  `[OPTIONS] STARTCHUNK`. So the player starts one chunk before the trigger-13
  chunk, and the ini key overrides it. This also re-confirms the trigger-slot
  offsets and gives the runtime chunk stride: `eax*97` dwords = **388 bytes per
  chunk record**.

  (Correction to the first version of this entry, which said only "trigger 13 is
  the start/finish line marker, confirmed by a direct comparison". The
  comparison is real, but the function that is *named* for the start position is
  a different one — see the next point — so the evidence for 13 is the
  STARTCHUNK path above, not a name in the binary.)
- CONFIRMED: **fcn.00429A20 is `trigger_find_start_position`** (named in its own
  assert, `(trigger_find_start_position) unable to find finish trigger in map`)
  and it scans for **trigger 14**, not 13. So the two "start" notions are
  distinct: 13 drives the player's STARTCHUNK, while 14
  (`TRIGGER_FINISH_POSITION`) is what this function locates. Its only caller is
  fcn.00406800.
- Trigger **1** is *cleared*, not read, by fcn.00428DE0: given an object index it
  walks the **next 20 chunks** and zeroes any trigger slot holding 1
  (0x428E47 compares against 1, 0x428E60/0x428E86 store 0). Object records have
  a **160-byte stride** with the chunk index at **+0x18**, and the player's
  object index is at 0x4D9E28. Its only caller is fcn.00428BC0. **What reads
  trigger 1 before it is disarmed is still unidentified**, so its meaning
  remains open — all that is established is that it is a one-shot marker
  consumed and cleared ahead of a car.
- CONFIRMED: source file **`C:\work\A2racII\Game\Tr_finis.cpp`** handles lap
  completion (fcn.00429860) — another source name not previously recorded.
- CONFIRMED: **at most 10 laps and 10 time checks are stored.** Both are bounds
  checked against 10 with their own asserts (`Illegal index for nr of laps!
  Index was: %d`, `Incorrect nr of time checks! Time checks: %d`), even though
  Game.ini's `Nrlaps` accepts 1-999. Globals: lap times 0x4DC970, lap index
  0x4D9AD4, lap limit (from `Nrlaps`) 0x4D9AD8, time-check count 0x4DCA9C,
  time-check times 0x4DC9F0, saved count 0x48C88C (initialised -1), lap record
  0x4D9AC8, race record 0x4D9ACC.
- CONFIRMED: the time checks are copied into the saved set **only when a new lap
  record is set**, which is exactly what Game.ini's comment says
  ("only saved when the player had a lap record").
- Note on an apparent contradiction: Game.ini says time checks are saved "in
  city tracks", while trigger **45** (the checkpoint, 1:1 with the `A_CHECK*`
  gantries) appears **only on the three point-to-point stages**. These are two
  different systems, not a conflict — 45 is a stage checkpoint, while the time
  checks are a *per-lap* split feature and laps only exist on circuits.

## Game.ini's developer comments (session 7)

Game.ini is commented as thoroughly as Raser.ini. Facts not previously recorded:

- CONFIRMED: **`Nrlaps`** — "Number of laps to drive in city tracks. Range:
  1-999 (default=2)". The circuits are therefore **2 laps** by default, though
  only 10 lap times can be stored (above).
- CONFIRMED: **`benzineprijs=1` is marked "Not used!"** by the developers, as is
  `[MOUSE] TYPE` and `[PALET] SHOW`. `particles=0` carries "Currently the module
  isn't used". So the fuel *price* is dead code even though the fuel triggers
  and `GETANKT` / `GEEN BENZINE MEER` strings are live.
- CONFIRMED: **`dif_mod`** — "Difficulty tweak var. 0-3 easier, 4=normal, 5-9
  harder. Should be set to 4", alongside `Difficulty` (0=EASY, 1=HARD).
- CONFIRMED: default input is the arrow keys as Windows VK codes (LEFT=37,
  RIGHT=39, UP=38 gas, DOWN=40 brake) with **EXTRA1=17 (Ctrl) = turbo** and
  **EXTRA2=18 (Alt) = handbrake**; `DEVICE` selects 1=Keyboard, 2=Joypad,
  3=Wheel/Joystick, 4=Sidewinder ForceFeedback.
- CONFIRMED: `[PLAYER]` carries `damage` (0-100), `money`, `tracktime` (msecs),
  `position`, the five upgrades, and the 8-element `DAMAGE_MESH` /
  `DAMAGE_VALUE` pair; `[RACERS]` stores each CPU racer's car index, finishing
  position and time in msecs, plus its own 8-element damage arrays.
- CONFIRMED: `texturecopy` exists to copy textures for "brake lights, sirenes,
  traffic signs, radar" — i.e. those four are animated by swapping texture
  contents, which is why they are not separate anims.

## Trigger groups 22-25 and 31-34 are region boundaries (session 7)

These eight values occur only on the three point-to-point stages, each value
twice except the last of each group (once). Their chunk positions show what they
are — **abutting region boundaries**:

| stage | 22 / 31 | 23 / 32 | 24 / 33 | 25 / 34 |
|---|---|---|---|---|
| etappe1 | 50, 161 | 162, 277 | 278, 645 | 646 |
| etappe3 | 121, 204 | 205, 268 | 269, 487 | 488 |
| etappe5 | 210, 599 | 600, 669 | 671, 739 | 740 |

- CONFIRMED: each value marks the **start and end of a span, and the next value
  resumes exactly one chunk later** (etappe1: 161 -> 162, 277 -> 278, 645 ->
  646). So the four values delimit **three consecutive regions** covering the
  middle of each stage — chunks 50-646 of 818 on etappe1, 121-488 of 774 on
  etappe3, 210-740 of 810 on etappe5. (etappe5 has a one-chunk gap at 670.)
- CONFIRMED: **groups 22-25 and 31-34 sit on exactly the same chunks** on all
  three stages — two parallel sets of markers for the same boundaries, which
  fits the two trigger slots per chunk holding one of each.
- CONFIRMED: they are **not** lane or speed triggers. Re-decoding the switch in
  fcn.00408080 shows 22-25 and 31-34 landing in its default case
  ("Checking chunk %d"), alongside 7, 9-17 and 26-39; only 4/5/6/8 (speed) and
  18-21 / 40-43 (lane) have handlers there.
- **Still open: what the three regions are.** No consumer has been found, so
  the meaning is unidentified; they may equally be authoring data. What is
  established is the geometry above, not a purpose.

## HANDLE_TRAFFIC_AI located (session 7)

- CONFIRMED: **`HANDLE_TRAFFIC_AI` = fcn.0040B770**, named by its own trace
  lines (`START --> HANDLE_TRAFFIC_AI(object %d)` /
  `FINISH -> HANDLE_TRAFFIC_AI(object %d)`). It reads the current chunk's
  trigger slots at **+0xEC and +0xF0** (0x40B872, 0x40B8B0), logs
  `Current ai_state = %d. Max speed allowed = %f.`, then calls `check_triggers`,
  `change_lane`, `handle_basic_ai` and `sndfx_play_object_sample` in that order.
  This is the entry point named in open question 37 as the place traffic speed
  is decided.

## `.pth` decoded: the camera paths (session 7)

The last unparsed per-track format. `.pth` holds **camera paths**, not a
centreline: `i32 version (-1); u32 count;` then per path six dwords
(`first_chunk, last_chunk, f2, f3, f4, point_count`) followed by
`point_count` x `{u32 flag; float x, y, z}`.

- CONFIRMED: **all 7 tracks consume byte-exactly** — 8 B for etappe5 (which
  declares zero paths) up to 1200 B for etappe2 (7 paths, 64 points). etappe0
  and etappe6 are identical, as with every other per-track file.
- CONFIRMED against the loader **fcn.00433710** (`pth_main.cpp`): it opens
  `etappe%d.pth` `"r+b"`, reads the version and rejects anything but -1
  (`Incorrect version path file`), reads the count into 0x4E47C0, then reads
  six dwords per record into 0x4E0710+ before the points, bounds-checking each
  (`Path does not exist! Index: %d`). The in-memory order matches the file.
- CONFIRMED: the points are **not** on the road — they sit 300-1800 units off
  it — and `first_chunk`/`last_chunk` bracket the stretch of road the path
  covers: for **158 of 169 points** the nearest chunk falls inside the range,
  and all 11 exceptions overshoot an end by 1-4 chunks, i.e. camera lead-in and
  lead-out. Together with the documented `pth_heli.cpp` consumer, that makes
  these the **helicopter//flyby camera** paths.
- Still unidentified: `f2` (3-7), `f3` (0/1/2), `f4` (round 5000..50000).
  SPECULATIVE only: `f4 / arc_length` clusters by `f2` (every `f2 == 3` path
  falls in 0.039-0.051, every `f2 == 4` in 0.279-0.315), which would fit `f2`
  selecting a speed regime and `f4` a duration.
- The `flag` dword on every point is 0 in all 7 shipped tracks, so its purpose
  cannot be read off the data.

**Every per-track format is now parsed byte-exactly**: `.rmp`, `.pmp`, `.cmp`,
`.dmp`, `.omp` and `.pth`.

## The scrolling sky (`gfx_sky.cpp`) (session 7)

Following the `[MAPS]` sky filenames into the binary gives the whole backdrop
system. The trace line `init_moving_sky( )` and the assert string
`(init_moving_sky) %s(%d) : Invalid screen size` locate it.

- CONFIRMED: source file **`C:\work\A2racII\Game\gfx_sky.cpp`** (from the
  assert at 0x4893BC) — a source name not previously in the inventory.
- CONFIRMED: `init_moving_sky` = **fcn.0041E740**, called from `init_game`
  (0x423D20, the address already documented).
- (Session 12: this whole section is the **software renderer's** sky. The
  Direct3D game uses a still `back<N>.pcx` viewport background instead -- see
  "The accelerated game's sky is a still backdrop, not the panorama".)
- CONFIRMED: **the sky is a 2D panorama blitted behind the scene, not a mesh or
  a dome.** It is loaded by filename straight from the track record, outside
  the anim system — there is no `scrl` entry in spel.dat's texture-name table.
  (`E01_SKYL_ANIM` is unrelated: a small etappe1 skyline prop.)
- CONFIRMED: `init_moving_sky` indexes a **137-byte per-track record** at
  0x4D8EE6 with the track index from 0x4D9A74 (`lea edx,[eax+ecx*8+base]` with
  `ecx = eax*17`, i.e. `base + idx*137`). The lores and hires sky filename
  fields sit **14 bytes apart** (0x4D8EE6 vs 0x4D8EF4); which is used is chosen
  by 0x4D9A6C (0 = lores, 1 = hires, anything else is the "Invalid screen size"
  assert). This is the in-memory form of the Raser.ini `[MAPS]` row.
- CONFIRMED: the band height is **`screen_h - screen_h/3`** (the `0x55555556`
  imul at 0x41E7E0 is the usual divide-by-3). At the shipped 640x480 that is
  320, exactly `SCRL_HI.pcx`'s height, so the hires panorama is drawn 1:1.
- CONFIRMED: the horizontal scroll, from fcn.0041EAC0, is

      scroll_x = (T[heading] + 1.0) * -(imgW / 2)        wrapped into [0, imgW)

  where the `-1.0` is the constant at 0x47F6F0 and `T` is a **1024-entry
  full-circle trig table** — the index is `atan2(...) * 1024 / 6.2831855`
  (constants 1024.0 at 0x49B014 and 2*pi at 0x47FC70) masked with 0x3FF
  (0x49B018). The table itself is built at runtime (a pointer at 0x4EA4B4), so
  **whether it holds sine or cosine cannot be read out of the static image**;
  SPECULATIVE: sine, which makes the sky sway with heading rather than rotate
  continuously.
- CONFIRMED: fcn.0041E9D0 splits the source window into **one or two blit
  rectangles** (flag at 0x4CC038) depending on whether `scroll_x` runs past the
  right edge of the panorama. That is only DirectDraw's inability to wrap a
  blit; a GL_REPEAT texture does the same in a single quad.
- Sky state block, for reference: 0x4CC018 image handle, 0x4CC01C/0x4CC020
  scroll x/y, 0x4CC028 image width, 0x4CC030 screen width, 0x4CC034 band
  height, 0x4CC038 two-rect flag, 0x4CC03C.. the rectangles.

## The AI speed model (session 6)

`init_ai_states` = **fcn.00405BC0(50)** (3124 bytes) builds `AI_States[50]` and
`AI Behaviors[16]` at runtime — they are filled by code, not static tables, so
the values live in the function. `init_cop_ai_states` = fcn.004025C0(40).

- CONFIRMED: the behaviour records are written at a stride of **0x2C = 44 bytes**
  (stores at +0x178, +0x1A4, +0x1D0, +0x1FC, +0x228, all seeded with 14.8).
- CONFIRMED: the function references a ladder of speed constants in `.rdata`
  at 0x47F154..0x47F198, in metres per second:

      47, 49, 52, 55, 62, 68, 72, 80, 90, 100      (169 .. 360 km/h)

  interleaved with small values 4.1, 4.7, 7.5, 9.0, 9.5, 10.5 (plausibly the
  paired acceleration or lane-change rates) and large ones 6031..13948, which at
  10 units/metre are 600-1400 m — AI sight or scan distances.
- CONFIRMED: it also stores two ladders of six multipliers as immediates:

      A: 0.88  0.90  0.92  0.94  0.96  0.98
      B: 0.74  0.78  0.83  0.87  0.91  0.96

- (Superseded in session 11 by "How the CPU racers pick their speed": the
  numbers are behaviours' preferred speeds, built as constant x difficulty
  multiplier.)
- **STRONGLY SUPPORTED: a racer's chosen speed is `ladder_speed x multiplier`.**
  Three of the four racer speeds logged in a real race reproduce *exactly*:

  | observed `ai_chosen_speed` | = |
  |---|---|
  | 49.919998 | 52 x 0.96 |
  | 53.900002 | 55 x 0.98 |
  | 63.919998 | 68 x 0.94 |

  (70.0 does not decompose and is presumably a raw value; the 24.0 and 25.0 seen
  for traffic come from a different path, as traffic is not on this ladder.)
  The two multiplier sets are most likely the difficulty tiers, which fits the
  "Difficulty modifier has invalid value" string and the 1.1/1.2/1.3 upgrade
  scalars found elsewhere.

## Stage types: circuits vs point-to-point (session 6)

- CONFIRMED: the seven stages split into two kinds, and the `.omp` object data
  says which is which:

  | stage | objects span | checkpoint gantries | kind |
  |---|---|---|---|
  | etappe0 | chunks 0..777 | none | circuit |
  | etappe1 | 29..762 | 4 | point-to-point |
  | etappe2 | 0..725 | none | circuit |
  | etappe3 | 86..553 | 3 | point-to-point |
  | etappe4 | 0..832 | none | circuit |
  | etappe5 | 46..806 | 4 | point-to-point |
  | etappe6 | 0..777 | none | circuit |

  A circuit's objects start at chunk 0 and run to the last chunk, because the
  chunk chain closes into a loop; a point-to-point stage's objects begin well
  inside the track. The even-numbered stages are the circuits, matching the
  menu's "Stedenrace" (city race) label for etappe0 = Amsterdam.
- CONFIRMED (corroborating): only the point-to-point stages carry
  `A_CHECK*_ANIM` gantries, and language.ini carries the lap vocabulary
  (RONDE INFO / RONDE / LAATSTE RONDE / NIEUW RONDE RECORD) for the circuits.
- On a circuit, driving past the last chunk wraps the chunk index back to 0,
  which is a usable lap-completion test (verified in the port: chunk 777 -> 0).
  Whether the original counts laps this way or with a finish trigger volume is
  not established -- `tr_*.cpp` is still unlocated. `RCS.IMG` does hold
  `hi_vlag.fnt` / `lo_vlag.fnt` (*vlag* = flag), presumably the chequered flag.

## Checkpoints (session 6)

- CONFIRMED: **checkpoints are ordinary `.omp` scenery objects**, identified by
  name rather than by `type`: the overhead gantries are `A_CHECK2_ANIM` /
  `A_CHECK3_ANIM` / `A_CHECK4_ANIM`, all stored with `type = 0`. Their `chunk`
  field is where the line sits.
  - etappe5: chunks 194, 328, 472, 626; etappe3: 206, 311, 424
  - etappe0 and etappe2 have none -- etappe0 is the closed city circuit, so it is
    presumably lap-scored.
- The original drives these through `tr_*.cpp` (`init_all_triggers`,
  `check_racer_trigger`, the "EINDE_WEG trigger" string) rather than by chunk
  comparison, so the trigger volumes themselves are still unlocated; matching on
  the owning chunk gives the same result at track resolution.
- `game.ini`'s documented `startchunk` test variable is what it sounds like --
  the chunk the player is placed at. The port mirrors it as `--start-chunk`.

## Localization

- CONFIRMED: Netherlands release; Dutch readme/help; language.ini supports Dutch/English/German; menu source tree has `Ned` subdir (per-language builds). Traffic cars are German brands (Golf, Polo, Trabant, Astra, BMW, Porsche, Mercedes "Calibr"), cop car "polizei" (mpolizei/VPOLIZEI) — game content is Germany/A2-Autobahn themed (A2 = Dutch motorway though; title "A2 Racer" refers to the Dutch A2 highway).

### language.ini decoded (session 6)

`[SETTINGS] LANGUAGE` picks 0=Dutch, 1=English, 2=German and `TEXT_COUNT=56`
numbered strings are read from `[NEDERLANDS]`, `[ENGELS]` or `[DUITS]`. The
table is the game's whole UI vocabulary and names mechanics that were otherwise
only guessed at:

| id | Dutch | meaning |
|---|---|---|
| 0, 1 | GEFLITST, BOETE: FL %d | speed camera caught you, and the fine |
| 4, 5 | GETANKT, KOSTEN: FL %d | refuelled and what it cost |
| 6-11 | POLITIE + BOETE x3 | **three police fine tiers** |
| 12-15 | WEGENWACHT, TOTAL LOSS, GEEN BENZINE MEER | roadside assistance, write-off, out of fuel |
| 17-19 | GEFINISHT, POSITIE: %d, TOTALE TIJD | finish summary |
| 20-23 | BUDGET, PRIJZENGELD, BOETES, NIEUW BUDGET | the post-race money screen |
| 30, 31 | NIEUW RACE RECORD, NIEUW RONDE RECORD | records |
| 33-39 | RACE TIJD, RONDE INFO, TIJD, RECORD, LAATSTE, POSITIE, RONDE | the HUD labels |
| 41-45 | TIJD IS OM, GEARRESTEERD, RACE OVER, EXTRA TIJD, LAATSTE RONDE | end conditions |
| 46-48 | INVINCIBLE, COP MODE, QUICK SERVICE | 46 and 47 are cheat labels; **48 is not** -- it is the trigger-2 service station's banner (session 10) |
| 49, 50 | EERSTE/LAATSTE WAARSCHUWING | police warnings before arrest |

- CONFIRMED: **the circuit stages are lap-based.** Strings 34/39/45 are
  RONDE INFO / RONDE / LAATSTE RONDE and 31 is NIEUW RONDE RECORD. This explains
  why etappe0 and etappe2 carry no `A_CHECK*` gantries while the point-to-point
  stages do — they are scored by laps, not checkpoints.
- CONFIRMED: **the same engine shipped under three titles**, visible in string
  32 (the CD-not-found message): Dutch *"A2-Racer II CD-ROM niet gevonden."*,
  English *"Unable to find **M25** CD-ROM."*, German *"Kann die **Autobahn
  Raser** CD-ROM nicht finden."* The M25 branding is the UK release; the German
  one corroborates the Autobahn Raser lineage already visible in Game.ini's
  `stuur_model` / `ai_model` "Old (Autobahn Raser I)" option.
- CONFIRMED: **the shipped English translation is half German.** 34 of the 56
  `[ENGELS]` entries are byte-identical to `[DUITS]`, including GEBLITZT,
  "STRAFE: DM %d", GETANKT and STRASSENWACHT — untranslated leftovers, with the
  currency left as DM. Leakage goes the other way too: `[DUITS]` string 42 is
  the Dutch GEARRESTEERD. A genuine defect in this release.


## The `.FNT` bitmap font format (session 9)

- CONFIRMED: `Load_new_ascii_set` (fcn.0046A2D0, `gfx_asci.cpp`) does
  `fread(table, 8, 256, f)` before it touches a pixel, so a `.FNT` is

      struct { u16 width; u16 height; u32 file_offset; } glyph[256];   // 2048 B
      ... the glyph bitmaps, at the offsets named above ...

  one entry per ASCII code, each bitmap `width * height` palette indices with
  **0xFF transparent** — the same key the sprites use. An absent glyph has every
  byte 0xFF, which is why these files open with a long run of them.
- CONFIRMED arithmetically: in `LoKMTel.fnt` the glyph for `'0'` is 26x24 at
  offset 2056 and `'1'` starts at 2680, and 2680 - 2056 = 624 = 26*24.
- CONFIRMED: spel.dat holds **34 font descriptors at VA 0x486D08**, 148 bytes
  each, indexed as **resolution*17 + font** — fcn.00418330 bounds-checks both
  and complains with "Incorrect resolution index" / "Incorrect font index". A
  record is just the file name followed by 132 zero bytes (runtime scratch), so
  it carries no colour or metrics of its own. The 17 fonts, lores / hires:

      0 lo_fnt3 / olfnt03    1 lomid / himid      2 logeel / higeel
      3 lo_geld / hi_geld    4 lo_vlag / hi_vlag  5 lo_bts / hi_bts
      6 lo_fnt1 / olfnt07    7 marc10             8 slider13
      9 slider15            10 lo_fnt1 / olfnt01  11 lo_fnt2 / olfnt02
     12 lo_fnt4 / olfnt04   13 lo_fnt1 / olfnt06  14 lo_fnt8 / olfnt08
     15 lo_fnt9 / olfnt09   16 lo_fnt1

- CONFIRMED (negative): `HiKMTel.fnt` / `LoKMTel.fnt` are **not in that table
  and their names appear nowhere in spel.dat**, so nothing loads them. They hold
  only `'0'`..`'9'` and look like leftovers; the speed readout is olfnt03 (see
  below), not the km-teller.

## Which font draws which part of the HUD (session 9)

Each face carries exactly the glyphs its job needs and nothing else, which
identifies all four without ambiguity:

| font | h | glyphs | drawn as |
|---|---|---|---|
| `olfnt01` (lo_fnt1) | 17 | `' '` `A`–`Z` | every label, flat white |
| `olfnt02` (lo_fnt2) | 25 | `0`–`9` `.` `-` `/` `:` | race time, `POSITIE n/m`, `RONDE n/m` |
| `olfnt03` (lo_fnt3) | 47 | `0`–`9` | the speed readout |
| `olfnt04` (lo_fnt4) | 15 | `0`–`9` `.` `-` `:` | the lap-info times |
| `slider13` | 13 | full ASCII | the `n%` inside the damage bar (nothing else has a `%`) |

- CONFIRMED: olfnt02/03/04 are **blue with a bevel** and olfnt01 is flat white,
  which is exactly the split visible on screen. The colour is in the palette,
  not applied at draw time: under the UI palette olfnt03's busiest indices
  (236, 240, 241) are (143,165,206), (107,140,189), (107,132,189).
- CONFIRMED by shape: rendering `233` from all 31 fonts and comparing with the
  screenshot picks out olfnt03 alone. The km-teller is a visibly different,
  lighter, more slanted face.
- CONFIRMED: **the hires set is drawn 1:1 at 800x600, not scaled to the
  screen.** Measuring the screenshot and dividing by its 1.21 capture scale,
  olfnt01 puts `SNELHEID` at 90.1 pixels against the font's own 90, `POSITIE` at
  74.4 against 76 and `RONDE` at 58.7 against 60 — no scaling and no tracking.
  olfnt03's `233` matches at 1.21 in **both** width and height (138/114 and
  57/47), which is what fixes the capture's scale in the first place.

## The HUD's brackets, bars and radar are RC.IMG art (session 9)

- CONFIRMED: an RC.IMG entry is `u16 width, height, anchor_x, anchor_y;` then
  `width * height` palette indices — `8 + w*h == size` holds for **all 453**
  entries.
- CONFIRMED: the white brackets live in two little sheets, each packing several
  pieces with gaps between them. Every piece is a rounded left cap with a rail
  that runs out to the piece's own right edge, and the game simply **stretches
  the whole piece** to the size the element needs (so a wide bracket has a
  thicker rail than a narrow one, as the screenshot shows).

  | sprite | sub-rect | piece |
  |---|---|---|
  | 425 | (0,22,27x7) | rail + short down-stub: `RACE TIJD`, `RONDE INFO` |
  | 425 | (6,0,21x11) | tall C |
  | 425 | (0,51,78x7) | stub + long rule: closes the `RONDE INFO` box |
  | 425 | (37,68,11x11), (37,91,17x11) | the small C: `POSITIE`, `RONDE` |
  | 426 | (25,0,55x13) | the damage bar frame |
  | 426 | (25,19,55x13) | the turbo bar frame |
  | 426 | (0,37,34x12) | the `SNELHEID` bracket |
  | 426 | (63,53,16x6) | the **km/h** mark |

  427 is 425's lores partner. The frames carry a placeholder fill of their own
  (index 33) which the real fill is drawn over.
- CONFIRMED: both bars are an **11-frame 91x8 ladder in 10% steps**, and in
  neither case is the step order the index order. Measuring how far the lit
  colour reaches in each frame gives
  **damage: 431, 129, 124, 126, 127, 128, 123, 130, 131, 132, 133** and
  **turbo: 14, 12, 11, 15, 16, 17, 18, 19, 20, 21, 13**. The empty frames sit
  well away from their runs — 431 is the all-dark damage bar and 14 the all-dark
  chevron strip. 122/125 and 134..144 are the same two ladders at 49x3, for
  320x240. (This corrects the earlier note that sprites 11..21 are "11 frames of
  a 91x8 bar" with no order given.)
- CONFIRMED: **the radar really is procedural.** `hiradar.tga` (128x128) and
  `loradar.tga` (64x64) are blank canvases filled entirely with the (255,0,127)
  key colour — there is no radar artwork to draw. The screenshot shows what
  `draw_road_in_radar_surface` paints into them: the road as a white ribbon with
  a black edge, the player a yellow dot at the middle of it and the other cars
  red ones.

## The HUD measured against the running game (session 9, runtime)

Session 9 ran `spel.dat` under Wine (NOCD=C, Xvfb 640x480x24, `RESOLUTION=3`,
`Game.ini etappe=0 demo=1`) and captured an in-race frame. This is much better
evidence than the compressed reference screenshot and settles several things.

- CONFIRMED (Raser.ini `[PALET]`, read directly): each track names **two**
  palettes, "first arg is the version for 8-bit mode, second is the version for
  16-bit mode", and **all seven tracks name the same 16-bit one, e02_pl16.pcx**.
  So in 16-bit mode there is no per-track HUD palette at all.
- CONFIRMED: the live HUD's structure matches the layout documented above
  exactly -- the two right-hand groups, the `RONDE INFO` box with its vertical
  connector and closing rule, the small C brackets beside `POSITIE` / `RONDE`,
  the two bars, the big speed digits with the km/h mark, and the radar ribbon
  bottom-left.
- CONFIRMED: **the HUD sprite colours on screen are not a direct palette
  lookup.** Measured off the live frame:

  | index | e02_pl16 | on screen | what it is |
  |---|---|---|---|
  | 15 | (255,255,255) | (255,255,255) | bracket white |
  | 0 | (0,0,0) | (8,0,0) | bracket outline |
  | 73 | (255,137,20) | (255,73,0) | the lit turbo chevrons |
  | 81 | (85,0,0) | (25,0,0) | the dark damage bar |

  That is a **gamma of about 2.0** per channel: it fixes black and white, and
  predicts 73 as (255,74,2) against the measured (255,73,0). Index 81 was *not*
  fitted to and comes out (28,0,0) against the measured (25,0,0), so the fit is
  independently corroborated. Against gamma 1.0 the summed error over the three
  coloured samples is 3 versus 84.
- CONFIRMED: **the fonts do not follow it.** olfnt02's blue reads (140,158,214)
  on screen against (143,165,206) in the file — essentially unchanged, and
  nothing like the (80,107,166) a gamma of 2 would give. So the glyph and sprite
  paths differ, which is one of the two possibilities open question 44 named.
- This also explains the teal that had no business being there: index 147, the
  brackets' one-pixel anti-alias edge, is (28,105,105) in the file but
  **(3,43,43) through the transform**, and the live frame shows the brackets as
  white and near-black with no teal pixel anywhere.
- CONFIRMED: **sprites scale with the resolution, fonts do not.** The damage bar
  frame is 98 pixels wide in the 640x480 capture and 123 in the 800x600
  reference shot — a ratio of 1.25, exactly 800/640 — while olfnt01's glyphs are
  the same pixel size in both. At 640x480 the fill strip is drawn at its own
  native 91x8 while the frame piece (55x13 in the sheet) is stretched to 98x22,
  so frame and fill really are scaled independently.

### Still open: whether the gamma is the game's or Wine's

- The UI palettes (`e02_pl16`, `palet`, `init`, `hiradar`, …) agree on the whole
  UI index band and get the fonts blue, the damage bar dark red (81) and the
  turbo chevrons orange (73) — but they put the brackets' anti-alias index
  **147** at a teal (28,105,105), where the screenshot clearly shows a dark
  edge. The per-track palettes have 147 at (21,21,23), which is right, but then
  the bar colours come out brown. **No shipped PCX gets both**, so the runtime
  palette must be composed from more than one source, or the fonts are
  palettised separately from the sprites. The runtime measurement above answers
  the practical half of this (a gamma of about 2.0 on the sprite path, and the
  fonts on a different path), but whether that transform is the game's own or an
  artefact of Wine's 8-to-16-bit conversion is **not** settled — it was observed
  only under Wine. The port applies it because it reproduces both the running
  game and the reference screenshot where the raw palette reproduces neither.
  See open question 44. **Settled (session 14): the game's own**, Game.ini
  gamma; see "Game.ini gamma squares every colour".


## The police: what the cops enforce, and where the limits come from (session 9)

Open question 25 suggested attacking the police mechanics with the debug trace.
The trace turned out to be the wrong tool -- a full demo run logs 1,045,857 lines
of waypoint and lane internals and **not one** police event -- but the debug
*strings* and the code around them give the structure. (The reason for the empty
trace is not that a demo driver stays lawful: **demo mode does not drive the
player's car at all.** It rolls off the line at about 38 km/h, coasts to a stop
in roughly five seconds and sits there while the clock runs. See question 34.) VAs are in `spel.dat` (.data maps paddr 0x7f800 to vaddr 0x482000).

- CONFIRMED: the active cops live in a `T_ARRAY` of element type **`t_cop`**
  named **"Cop States"**, constructed at fcn.004020d0. Its globals are
  0x4a5238 (data), 0x4a523c (count), 0x4a5244 (init flag), 0x4a5248 (alloc);
  the error `Array of active cops not initted yet!` guards 0x4a5238.
- CONFIRMED: **`t_cop` is 32 bytes** -- every access indexes it with
  `shl reg, 0x05`. Fields identified so far:

  | offset | type | meaning |
  |---|---|---|
  | +0x00 | int | **the cop's state** (see the state machine below) |
  | +0x04 | int | the suspect's object index ("Suspect code number is %d.") |
  | +0x0c | float | accumulator, reset to 0 when a pursuit starts or ends |
  | +0x10 | float | **pursuit timer**, compared against 40.0 |
  | +0x14 | float | **max speed** this cop enforces |
  | +0x18 | float | **max lanes** this cop enforces |

  (The 36-byte records also in play, indexed `lea r,[r+r*8]; shl r,2` off
  0x4a5b40, are a different array -- the AI/driver records, not cops.)

### The per-chunk triggers are the speed limit the police enforce

This connects two findings that were separate until now: the `.pmp` trigger
slots (see "Race markers" above) are what set each cop's limits.

- CONFIRMED (fcn at 0x402200ff, jump table at 0x4024e0): the **speed trigger**
  switch takes `value - 4` and accepts 0..4, writing a float into `[cop+0x14]`:

  **WRONG, corrected in session 12** -- the table below is one case off. The
  jump table at 0x4024e0 reads `0x40228d, 0x4022b9, 0x4022e4, 0x402310,
  0x402256` for triggers 4..8, i.e. **4 -> 60, 5 -> 80, 6 -> 120, 7 ->
  ILLEGAL, 8 -> 2 x [0x4d9e68]**. See "RESET_COP, BACKTRACK_SPEED_TRIGGER and
  cop_lane". Original (wrong) table:

  | trigger | max speed | km/h | occurrences in the 7 shipped tracks |
  |---|---|---|---|
  | 4 | `2 * [0x4d9e68]` | (2x a global default) | 7 |
  | 5 | 16.67 m/s | **60** | 7 |
  | 6 | 22.22 m/s | **80** | 13 |
  | 7 | 33.33 m/s | **120** | **0 -- implemented but never used** |
  | 8 | — | `ILLEGAL SPEED TRIGGER!!!!` | 0 |

  16.67, 22.22 and 33.33 m/s are 60.0, 80.0 and 120.0 km/h exactly, and they
  match the limit signs in RC.IMG (sprites 406/407/408 read 120/60/80).
- CONFIRMED (jump table at 0x4024f4): the **lane trigger** switch takes
  `value - 0x12` and accepts 0..3, writing 1.0/2.0/3.0/4.0 into `[cop+0x18]`
  for trigger values **18/19/20/21**. Counts across the 7 tracks: 30/94/64/8,
  with the parallel group 40..43 at 21/69/46/3.
- This corrects the earlier note that the speed triggers are "4, 5, 6, 8": 8 is
  the *illegal* case in this switch, and the fourth real value is 7.
  (**Retracted in session 12**: the earlier note was right. 7 is the illegal
  case, and 8 is the 2x-default one.)

### Three kinds of offence

- STRONGLY SUPPORTED (from the debug strings emitted in source order around
  0x48284c..0x482a0c): a cop judges the target on **three** counts, each with its
  own OK/violation message:
  1. **speed** -- `Target speed: %f.` against `Cop max speed: %f.`, giving
     `-> Speed OK.` or `Speeding!`
  2. **lanes** -- against `Cop max lanes: %d.`, giving `-> Lane OK.`
  3. **side** -- `Cop side: %d.` against `Target side: %d, lane %d.`, giving
     `-> Side OK.` or **`Ghost!`** -- *spookrijder*, driving against the traffic
     on the wrong carriageway
  with `No violation.` when all three pass. The side check uses the same
  `obj_side` notion the AI uses elsewhere.
- CONFIRMED: **the cop state machine**, from `HANDLE_COP_AI` (fcn at 0x4027a0):
  the state in `[cop+0x00]` is bound-checked with `cmp eax, 4` and dispatched
  through a 5-entry jump table at **0x4029fc**, with the default arm raising
  `Cop state %d corrupted!` at **ai_cops.cpp:241**:

  | state | handler | what it prints |
  |---|---|---|
  | 0 | 0x402847 | `On the lookout` |
  | 1 | 0x402875 | `In pursuit of suspect`, then calls the pursuit fcn.00402b40 |
  | 2 | 0x4028b2 | `Got him!` |
  | 3 | 0x402948 | `Quiting chase!` |
  | 4 | 0x40297e | `Calling it quits for today.` |

  (This replaces the earlier reading of the ordering from the string block,
  which was marked LIKELY; the case numbering above is the switch's own.)

### How a cop drives once it is chasing

- CONFIRMED (fcn.00402b40, which opens with `Cop in pursuit of suspect!`): the
  cop compares its own chunk with the target's (`My chunk = %d, target chunk =
  %d.`) and branches on the gap:
  - more than **5 chunks** behind -> `Speeding...`, and it takes its speed from
    the AI speed table rather than the target's;
  - otherwise -> `Approaching...`, speed = the target's scaled by a constant,
    **floored at 15.0**.
  Note `Speeding...` here is the *cop* accelerating to close the gap; the
  offence message is the separate `Speeding!`. The two are easy to confuse.

### The thresholds

All of these are floats in `.rdata` (paddr 0x7d600 maps to vaddr 0x47f000).

- CONFIRMED, **old model only** (dead with the shipped Game.ini; see "The new
  AI model" below, where the shipped check at 0x409060 has no 10% margin):
  **the speeding threshold is 10% over the limit.** The check at
  0x403257 is `[cop+0x14] * 1.1 < target_speed` -> `Speeding!`, with the 1.1 at
  **0x47f0e0**. Against the posted 60 and 80 km/h that is 66 and 88 km/h.
- CONFIRMED: **the lane check is a no-op in the shipped build.** Between the
  `Cop max lanes: %d.` printf at 0x4031f2 and the `-> Lane OK.` printf at
  0x4031ff there is nothing but an `add esp, 0x0c` -- no comparison and no
  branch, so the lane arm can never report a violation however `[cop+0x18]` is
  set. The lane triggers still set the field; nothing reads it.
- CONFIRMED: the **side** check is a plain equality at 0x4031b9 --
  `object[cop].field_0x28` against the target's side -- and on a mismatch it
  prints `Ghost!` and returns 1 (violation) immediately, before the lane and
  speed arms run.
- CONFIRMED: **the pursuit times out at 40.0.** `[cop+0x10]` accumulates and is
  compared against the 40.0 at 0x47f0cc; once it is reached the cop sets its own
  state to **3** (`Quiting chase!`).
- CONFIRMED: while closing, a cop more than **5 chunks** behind its target takes
  its speed from the AI speed table (`Speeding...`); otherwise it runs at the
  target's speed times **1.25** (0x47f0d8), floored at **15.0** (0x47f0dc)
  (`Approaching...`).
- CONFIRMED: on `Got him!` the target's speed is multiplied by **0.5**
  (0x47f0c8) into the AI record -- the arrest halves the player's speed -- and
  the cop drops to state 3. The whole arrest body is guarded by
  `[0x4d9a7c] != 8`, so some global mode (8) suppresses it.

### Being caught: three strikes (ev_cops.cpp)

`ev_cops.cpp` asserts `Illegal value for g_times_caught: %d`, and the variable
it names is the global at **0x4a70e8**. The handler at fcn.00415300 increments
it and branches on the new value:

- CONFIRMED: **the first two catches are warnings, the third ends the race.**
  The handler writes a language.ini string id into **0x4882c0**:

  | g_times_caught | writes | language.ini string |
  |---|---|---|
  | 1 | 49 | `EERSTE WAARSCHUWING` (first warning) |
  | 2 | 50 | `LAATSTE WAARSCHUWING` (last warning) |

  and the sequence function at 0x415420 tests `g_times_caught >= 3` and takes a
  different arm entirely (0x415438: `push 6`, `[0x4867a4] = 2`,
  `call 0x424d20`). The two warning ids land exactly on the pair language.ini
  carries for the purpose, which is what makes the reading solid rather than a
  guess about a magic number.
- CONFIRMED: **a random police voice line is played, from a set chosen by the
  offence count.** The switch at 0x415381 picks a table by `g_times_caught`
  (1 -> 0x486678, 2 -> 0x486660, otherwise a six-entry table), takes
  `rand() % n` and calls fcn.004286a0 with the entry. That callee is the sound
  player: it bounds-checks the index against 86 and indexes the sample-name
  table based at `werk01.wav`, raising `SNDFX sample out of bounds`. The tables
  hold sample ids **59..63** for the first catch and **54..58** for the second.
- CONFIRMED: the whole cop sequence is gated on the global **0x4d9a7c == 8** --
  when it is 8 the sequence runs and the `Got him!` handler **skips** its own
  inline bookkeeping, and when it is not the sequence returns immediately. The
  same global guards the arrest body in ai_cops.cpp, so the two are two halves
  of one decision. LIKELY a game-mode selector, but which mode 8 is has not been
  identified.
- Also on a catch: if `[0x486640] == 3`, the player object's field at +0x2c is
  set to 1.
- The small function fcn.00425980 that the `Got him!` path calls is only
  bookkeeping -- it zeroes the player object's +0x38 and the global 0x4d9e64,
  ending the pursuit. It is not where the penalty lives.

### Where the limits actually are

Counting the speed triggers per track (the port's own loader, all 7 stages):

| stage | trigger 4 (unrestricted) | 5 (60) | 6 (80) | 7 (120) |
|---|---|---|---|---|
| etappe0 | 0 | 0 | 0 | 0 |
| etappe1 | 2 | 1 | 4 | 0 |
| etappe2 | 2 | 0 | 0 | 0 |
| etappe3 | 2 | 1 | 2 | 0 |
| etappe4 | 1 | 0 | 0 | 0 |
| etappe5 | 0 | 5 | 7 | 0 |
| etappe6 | 0 | 0 | 0 | 0 |

- CONFIRMED: **etappe0 and etappe6 carry no speed trigger at all** (they share
  geometry), and etappe2 and etappe4 carry only the unrestricted one, so the
  police can only ever act on etappe1, 3 and 5 -- **etappe5 is where the system
  really lives**, with 12 of the 27 triggers in the game.
- This is worth holding against the **limit signs** standing by the road
  (`A_MAX50S` / `A_MAX80S` / `A_MX120S`, which the port had been reading): the
  signs say 50/80/120 and the triggers say 60/80/120, they do not line up track
  for track, and only the trigger is wired to anything. The signs are scenery.

## Which binary owns which archive, and why there are two index formats

- CONFIRMED by the strings each binary carries:

  | binary | named index | binary index |
  |---|---|---|
  | spel.dat | `rcs.img` / `rcs.ind` (and the built-in default `Resource.img`/`.ind`, which Raser.ini `[IMG] RCSFILE=rcs` overrides) | `rc.img` / `rc.ind` |
  | menu.dat | `Mrcs.img` / `Mrcs.ind` | `menurc.img` / `menurc.ind` |

  So the two halves of the game mirror each other, and **question 15's pairing
  is just that**: Mrcs and MENURC are menu.dat's archives exactly as rcs and rc
  are spel.dat's.
- The reason for **two index formats** (question 24) follows from how each is
  used, which this session's work shows directly: the **named** archives are
  looked up by file name -- `a2_archive_find(&rcs, "olfnt01.fnt")`,
  `"e02_pl16.pcx"`, `"WIEL.d3d"` -- while the **binary** ones are addressed by
  **number**: the HUD reads RC.IMG sprites 425 and 426 for its brackets and the
  ladders at 11..21 and 123..133, and nothing ever names them. A name table
  would be dead weight for art the code indexes numerically.
- CONFIRMED: `Mrcs.img` **nests further archives** -- seven of its entries are
  themselves `.img` files (`CARVED.IMG`, `SMALLBT.IMG`, `ascii12.img`,
  `cyber.img`, `BUTFNT.IMG`, `STDFONT.IMG`, `midfont.img`), and menu.dat names
  all of them in its own strings. The menu's fonts and button art are archives
  within an archive.

## The three executables: how the game is actually started (session 9)

`a2_II.exe`, `qrace.exe` and `instlng.exe` are small Win32 apps beside the two
big `.dat` binaries; disassembling the first two settles questions 1, 2 and 3.

### a2_II.exe is the shell, and menu.dat exits

- CONFIRMED: **a2_II.exe alternately launches `menu.dat` and `spel.dat`**, each
  with `CreateProcessA` + `WaitForSingleObject(INFINITE)` +
  `GetExitCodeProcess`, so each child runs to completion before the next starts.
  It is a windowed app whose class name is `MOTOR`.
- CONFIRMED: **the handshake is exit code 40.** At 0x401292 the shell compares
  menu.dat's exit code with `0x28`; on a match it falls to 0x4012d4 and launches
  `spel.dat`, and on anything else it builds an error message
  ("File or path is not found"). After spel.dat returns it jumps back into the
  message loop, which runs menu.dat again -- so the pair alternate for as long
  as the session lasts.
- **So menu.dat does exit and the shell relaunches it; it does not stay
  resident** (question 3), and a2_II.exe is what a shortcut points at
  (question 2).

### qrace.exe is the Quick Race dialog

- CONFIRMED: it is a dialog app (window class `3DS2N64`) that writes settings
  into **game.ini** with `WritePrivateProfileStringA` and reads them back with
  `GetPrivateProfileIntA`. Its `.data` carries the keys it touches --
  **`PLAYER`/`car`, `ETAPPE`/`etappe`**, plus the upgrades `versnellingsbak`,
  `turbo`, `abs`, `banden`, `motor`, and `copland` and `damage`.
- CONFIRMED: it then builds a filename by appending **`.dat`** to text taken
  from dialog control 40 and launches that with `CreateProcessA`, failing with
  "Failed to launch A2Racer 2". **No game.ini key names the program to launch**
  -- question 1 assumed one did, and it does not; the name comes from the
  dialog.
- Its `.data` also holds the game's own menu labels, which are a useful
  cross-check: the cars read **Automatisch, Trabant, Astra, Kever, BMW, Porsche,
  Mercedes, Lamborgini** -- *Kever* being the Beetle, which agrees with the VW
  in slot 2 read out of garage.ini -- and seven stage names, **Amsterdam start,
  Den Haag, Den Haag, Rotterdam, Rotterdam, Amsterdam, Amsterdam finish**, one
  per etappe.

## Trigger value 1 is the speed camera (session 9)

The last unidentified trigger, settled by correlating it against the `.omp`
objects rather than by finding its reader.

- CONFIRMED: **trigger 1 and the speed cameras match one for one on every
  track** -- same count, same places. Listing the chunks carrying trigger 1
  beside the chunks of the type-7 objects (`A_FLTSRP` / `C_FLTSRP`, the
  *flitspaal*):

  | stage | n | trigger 1 | cameras |
  |---|---|---|---|
  | etappe0 | 6 | 18 137 257 304 458 632 | 17 136 257 303 457 631 |
  | etappe1 | 5 | 123 235 344 462 602 | 122 235 344 462 602 |
  | etappe2 | 5 | 93 246 353 430 723 | 92 245 352 429 721 |
  | etappe3 | 5 | 116 181 250 325 482 | 115 180 249 324 481 |
  | etappe4 | 5 | 13 256 450 571 698 | 12 255 449 570 696 |
  | etappe5 | 5 | 158 211 317 572 670 | 157 210 317 572 668 |
  | etappe6 | 6 | 15 256 311 457 632 725 | 13 255 311 456 631 724 |

  37 of each in total. The trigger sits on the camera's own chunk or one or two
  past it, which is what an object standing near a chunk boundary does -- the
  object's chunk comes from its position, the trigger from the author's slot.
- That also explains the behaviour already noted of fcn.00428DE0, which zeroes
  trigger 1 in the 20 chunks around an object: it is a **one-shot**, cleared once
  tripped so a camera flashes once per pass.
- The port fires its camera off the type-7 objects instead. The two agree to
  within a chunk, but the trigger is what the game actually reads.

## Triggers 22-25 and 31-34 are traffic signs (session 9)

Question 42 called these "three regions" because their geometry had been mapped
but no consumer found. They are not regions.

- CONFIRMED: **`tr_vbord.cpp`** -- *verkeersbord*, traffic sign -- holds **18
  handler functions**, evenly spaced at `0x42a640 + n * 0x70`, with their
  pointers in a table at **VA 0x48C4E0** (immediately after the posted-limit
  global 0x48C480). None is called directly; they are reached through that
  table.
- CONFIRMED: **every one of the 18 is gated to etappe 1, 3 or 5.** Each opens by
  testing the stage number at 0x4d9a74 against 1, then 3, then 5, and otherwise
  raises
  `Illegal trigger called! Trigger is only used for highway tracks! Etappe:%d`
  at tr_vbord.cpp:51.
- CONFIRMED by the data: triggers **22-25 and 31-34 occur on etappe 1, 3 and 5
  and nowhere else** -- 2 each of 22/23/24 and 31/32/33 per stage, and 1 each of
  25 and 34:

  | value | total | where |
  |---|---|---|
  | 22, 23, 24 | 6 each | e1 2, e3 2, e5 2 |
  | 25 | 3 | e1 1, e3 1, e5 1 |
  | 31, 32, 33 | 6 each | e1 2, e3 2, e5 2 |
  | 34 | 3 | e1 1, e3 1, e5 1 |

  The stage set matches the handlers' gate exactly, and **22..39 is exactly 18
  values** for 18 handlers, of which the shipped tracks use eight.
- STRONGLY SUPPORTED, therefore: these are **sign-change triggers on the
  highway stages**, switching what the overhead gantries display, and the two
  groups that "duplicate each other chunk for chunk" are the two gantries -- one
  per carriageway. The speed-zone setters documented above (50/70/100/120 into
  0x48C480) are the same file's work.
- Not established: which value selects which sign. That needs the code that
  indexes the 0x48C4E0 table, which is not reached by any direct reference.

## A caveat on the checkpoint reading

- CONFIRMED (negative): `check_racer_trigger`'s case table at 0x40B294 maps
  value-2 to a case index, and only **2, 9, 14, 15, 46 and 47** reach a real
  handler -- everything else, **value 45 included**, falls to the default arm at
  0x40b256. So the reading of 45 as the checkpoint rests on the data
  correlation with the `A_CHECK*` gantries, not on this dispatcher. Some other
  dispatcher must consume it, or nothing does.

## How a lap is counted (session 9)

Question 40 asked which marker the original counts laps at. `check_racer_trigger`
answers it directly.

- CONFIRMED: **trigger 15 (`TRIGGER_FINISH_VALID`) does one thing** -- its
  handler at 0x40b0fb sets `racer[obj].field_0x40 = 1`, an armed flag.
- CONFIRMED: **trigger 14 (`TRIGGER_FINISH_POSITION`) is where the lap is
  counted.** Its handler at 0x40b050 tests that flag, and only when it is 1 does
  it increment `racer[obj].field_0x68` -- the lap counter -- and compare the
  result with the global at 0x4d9ad8 (Nrlaps) to decide whether the race is
  over.
- CONFIRMED: **trigger 9 (`TRIGGER_ZERO_CHUNK_MARKER`) has nothing to do with
  laps.** Its handler calls fcn.0042ae20, which sets a crossed flag for the
  player and *toggles* three per-object booleans -- chunk-index wrap
  bookkeeping, which is what the name says. The earlier suspicion that laps
  might be counted at the wrap is laid to rest.
- The shipped circuits put the two triggers **one chunk apart**: 524 and 525 on
  etappe0 and etappe6, 16 and 17 on etappe2, 329 and 330 on etappe4. So crossing
  the line disarms and the very next chunk re-arms for the following lap. The
  car starts one chunk *before* the line, so its first crossing is unarmed and
  scores nothing -- which is exactly what makes the first fire the true end of
  lap 1, with no special-casing anywhere.

## The eight damage zones (session 9)

`Game.ini [PLAYER]` carries `DAMAGE_MESH` and `DAMAGE_VALUE`, each eight
entries, and the game writes them back itself:

    DAMAGE_MESH=-1,-1,-1,-1,-1,0,-1,-1
    DAMAGE_VALUE=-1.000000,...,0.000000,...

- CONFIRMED: the eight entries are the **eight directions round the car**. The
  collision handler in `plr_car.cpp` (fcn around 0x427430, asserts at lines 1755
  and 1781) prints `Determining area.` and `l_zone = %d.`, then dispatches a
  signed value in **[-11, +11]** through a 23-byte case table at 0x42770c into a
  10-entry jump table at 0x4276e4. Only eight values are live:

  | value | zone | | value | zone |
  |---|---|---|---|---|
  | +1 | 0 | | -1 | 4 |
  | +11 | 1 | | -11 | 5 |
  | +10 | 2 | | -10 | 6 |
  | +9 | 3 | | -9 | 7 |

  **+/-1, +/-9, +/-10, +/-11 are the eight neighbours of a cell in a 10-wide
  grid**, so the incoming value is the difference between two cell indices and
  the zone is the direction the impact came from. Everything else, 0 included,
  falls through to `%s(%d): Impossible collision! (Center of object)` -- a
  difference of zero means the impact has no direction, which is exactly what
  that assert is complaining about.
- CONFIRMED: each arm adds the impact to that zone's accumulator scaled by
  **0.5** (the float at 0x47f870), and when the object is the player
  (`[0x4d9e28]`) the accumulator is **`0x4d9e74[zone]`** -- the array behind
  `DAMAGE_VALUE`.
- CONFIRMED: the mesh side is separate and keyed the same way --
  `Applying regional damage to object %d, region %d, level %d.`,
  `Entering handle_mesh_damage(%d).` and `Object has no damage frames,
  returning...` -- so `DAMAGE_MESH[zone]` is the damage frame currently shown
  for that direction, with **-1 meaning undamaged**, as the shipped Game.ini's
  seven -1s and one 0 show.
- Not established: how the grid cell indices are produced (which fixes the
  zones' orientation on the car), and the level thresholds that step a zone from
  one damage frame to the next.

## The turbo, watched in the running game (session 9)

The first use of input injection (see "Driving the original under Wine"): hold
`Game.ini [INPUT] EXTRA1` -- 17, the VK code for Control -- while driving.

- CONFIRMED: the **TURBO bar starts full** at the beginning of a race, drains
  steadily while the key is held, and **does not refill**: six seconds after
  release it was still at the level it had reached. It drains whether or not the
  car is moving (one run kept draining after a crash at 7 km/h).
- CONFIRMED: the boost is **large and lifts the car past its rated top speed**.
  One run read 153 km/h before the key went down, then 169 after a second and
  **202 after four**; on release the speed decayed again (202 -> 190 over two
  seconds). The car in that run was Game.ini's slot 0, whose CARS.DAT top speed
  is 151 stock and 165 upgraded.
- Two runs put a full bar at somewhere between eight and eighteen seconds of
  continuous use; the spread is measurement error, from counting chevrons in a
  640x480 capture and from one run spending most of its time crashed.
- Not established: whether the drain rate varies with speed or throttle, whether
  the reserve carries between stages, and where `turbo.wav` is triggered.
  `Game.ini [PLAYER] TURBO=1` is the garage upgrade that grants it; the key does
  nothing without it (untested).

## The 0x484B18 table, second pass (session 9)

Re-dumping all 56 fields across all 16 vehicles and cross-checking against the
code that reads them corrects two earlier readings and settles several more.

### Fields 5 and 6 are not the height: they are the CoG-to-axle distances

- CONFIRMED: **field 5 is the distance from the centre of gravity to the front
  axle and field 6 to the rear**, so **5 + 6 is the wheelbase**. Three things
  establish it:
  1. the game **adds them** (0x40d042: `fld [f6]; fadd [f5]`);
  2. they are **not equal** for the traffic vehicles -- 1.06 and 1.46, a
     rearward centre of gravity -- where every racer has them equal;
  3. the sums land on the real cars almost exactly:

  | entry | real car | f5 + f6 | real wheelbase |
  |---|---|---|---|
  | RACER_1 | Trabant 601 | **2.02** | 2.02 |
  | RACER_2 | Opel Astra | 2.50 | 2.52 |
  | RACER_5 | Porsche 911 | 2.30 | 2.27 |
  | RACER_7 | Lamborghini Diablo | 2.66 | 2.65 |
  | N_BUS | (an 11.5 m bus) | 8.00 | — |

  With **field 8, the CoG height**, that is the standard trio for axle load and
  weight transfer, which is exactly the cluster the code reads them in
  (0x40d5f0 pulls fields 7, 12 gravity, 6, 5 and 2 mass together).
- This **supersedes** the session-8 reading of field 5 as the vehicle height.
  The values are the same either way -- a bus is about 4 m tall and has about an
  8 m wheelbase -- which is why height looked right; the sum and the traffic
  vehicles' asymmetry are what rule it out.

### Field 9 is the frontal area

- CONFIRMED: **field 9 is the frontal area in square metres** -- 1.80 for a car,
  1.77 for the Porsche, 1.70 for the Diablo, **9.00 for the bus** -- and the
  code reads it in the same breath as **field 13, the air density** it pairs
  with in the drag term (0x40ce2d/0x40ce37/0x40ce41 read 13, 8 and 9 in a row).
  So the drag is `0.5 * f13 * f22 * f9 * v^2` with every term in the table.
- The port had been deriving the area as width x height, which came out within
  about 5% (1.72 against 1.80 for a car, 10.0 against 9.00 for the bus) and so
  never looked wrong. It still uses that estimate -- see "why the port does not
  use any of this" below.

### Structure and dead fields

- CONFIRMED: **the gearbox block is fields 27-36, mirrored by 39-48** -- not
  28-34 and 40-46 as recorded before. The upgrade copier at 0x4284e1 pairs them
  off one by one: (39,27) (40,28) ... (47,35) (48,36). So the block is
  [unused 27] + [six gears 28-33] + [final drive 34] + [**35, 36**], and the
  VERSNELLINGSBAK upgrade swaps all ten.
- CONFIRMED constant across all 16 vehicles: **0, 14, 26, 27, 39 are 0.00**
  (unused), 12 is 9.81, 13 is 1.2, 21 is 0.02, and **37 and 49 are 1.00**.
- **Field 11 duplicates field 10 exactly** in every vehicle and no direct
  reference to it was found, where field 10 is read at 0x40d474 -- so 11 looks
  dead.
### Field 22 is the wheel radius, not the drag coefficient

- CONFIRMED: **field 22 is the wheel radius in metres.** The driveline formula
  settles it: at 0x40e299 the game computes `gear * final * 9.554 / field22`,
  and **9.554140 is 60/(2*pi)** (9.549297) -- the constant that turns road speed
  into engine rpm, with the wheel radius as the divisor.
- The values are the radii they look like: 0.29-0.33 m for a car, **0.50 m for
  the bus**. Feeding each car's rated top speed back through the formula gives
  **4350-6610 rpm against a 9000 redline** for all seven racers, which is what
  cars geared to be drag-limited should do.
- This **supersedes** the session-8 reading of field 22 as Cd, which rested only
  on the values looking like drag coefficients (0.29-0.33 cars, 0.50 bus -- they
  do, which is the trap).
- LIKELY, in consequence: **field 8 is the drag coefficient**, the field the
  drag cluster loads beside the air density and the frontal area
  (0x40ce2d/0x40ce37/0x40ce41 read 13, 8 and 9 in a row). Its values are better
  Cds than the alternative reading of CoG height: 0.38 for an ordinary car,
  0.33 for the Diablo and **0.65 for the bus** -- a bus's Cd really is about
  0.65, while its centre of gravity is nowhere near 0.65 m off the ground. Not
  confirmed: the multiply has not been traced end to end.

### The gear array is 1-based from field 27

- CONFIRMED: the force code indexes the gears as **`[0x484b92 + gear*4]`**
  (0x40e271, 0x40e2a1), and 0x484b92 is **field 27**. So the array runs
  field 27 + gear, field 27 is the unused index 0 (it is 0.00 in every vehicle)
  and the six gears are fields 28-33. That is consistent with the upgrade
  copier pairing (39,27) first.

### Fields 15-20 are two Pacejka tyre triples

The six per-axle fields are read together at 0x40e55d..0x40e5b3, and the
instructions give the model away: **`fpatan`**, then **`fsin`**, then a multiply
by the grip field. That is the **Pacejka "Magic Formula"**,

    F = D * sin(C * atan(B * slip - ...))

with one triple per axle:

| field | Pacejka term | front / rear | values |
|---|---|---|---|
| 15 / 18 | **B**, stiffness factor | 0x484b62 / 0x484b6e | 8.43 traffic, 10.70-16.00 racers |
| 16 / 19 | **C**, shape factor | 0x484b66 / 0x484b72 | 1.11 racers, 1.70 traffic |
| 17 / 20 | **D**, peak factor (grip) | 0x484b6a / 0x484b76 | 0.59 Trabant to 3.50 Diablo, 1.05 traffic |

- The order in the code is exactly the formula's: field 15 multiplies the slip
  quantity, `fpatan` takes the arctangent, field 16 scales it, `fsin` takes the
  sine, and field 17 scales the result -- then the same again for the rear with
  18, 19 and 20.
- CONFIRMED by the value ranges, which are textbook: Pacejka **B** is normally
  8-15 and these are 8.43-16.00; **C** is 1.3-1.8 and these are 1.11 and 1.70;
  **D** is the peak friction coefficient, and it orders exactly by how quick the
  car is. The rear B is the higher of the two on the Astra, VW, BMW and Porsche,
  which is how a car is set up to understeer.
- Every traffic vehicle shares one flat tyre set (8.43, 1.70, 1.05), bus
  included -- background vehicles do not need distinct handling.
- **Field 54** is loaded in the middle of the same cluster (0x40e59d, right
  beside the front grip) and is **1.20 for every racer and 1.00 for every
  traffic vehicle** -- a blanket grip multiplier that gives the player's class
  of car 20% more than the scenery.
- This supersedes the session-8 note that fields 15/16 and 18/19 were "not
  identified": they are the stiffness and shape factors, and only the third of
  each triple had been recognised.

### Fields 35 and 36 are the gearbox's shift points

Tracing fcn.0040e190 with the corrected anchors -- [esp+4] is the final drive
and [esp+8] the **wheel radius** -- shows what it is: **the gear selector**.

- It builds `gear * final * (60/2pi) / radius`, the rpm-per-unit-speed for the
  current gear, and multiplies by road speed to get engine rpm.
- It clamps that to a floor of **800 rpm** (the constant at 0x47f2f8) -- idle.
- It forms a **downshift** threshold as **`(f36 - 0.05) * rpm_torque`** (0x40e213
  subtracts the 0.05 at 0x47f2e8, then 0x40e225 multiplies by field 25): a
  Trabant downshifts near 2100 rpm, a Diablo near 3900, and the 0.05 is the
  hysteresis that stops it hunting.
- It forms an **upshift** threshold from **field 35 against the redline**
  (0x40e22b loads f35, 0x40e231 the redline at field 23, and they are divided at
  0x40e23f). The exact arrangement is not pinned down, but both candidate
  readings give sensible points well inside the rev range.
- It then adjusts the gear index `edx` and re-reads the gear array at
  `[field27 + gear*4]`.

That explains everything about the pair: why they sit in the gearbox block, why
the **upgraded box carries its own pair in 47/48** (a close-ratio box shifts
differently), and why their values are small multiples either side of 1.

- **This corrects the guess made earlier in this same session** that 35/36 were
  the likeliest home of the torque curve. They are not. **The table carries no
  torque curve at all** -- it gives power, the peak-torque rpm and the redline,
  and nothing about the shape between them -- so the port's existing note that
  the rated top speed and the rated 0-100 cannot both be satisfied from this
  table stands, and is now explained rather than merely observed.

### Why the port does not use any of this

Correcting the inputs made the **behaviour worse**, and the port keeps its
fitted pairing as a result:

- With the table's own radius (0.303 m rather than the fitted 0.203) and field 8
  as Cd, the solver has to hand the engine far more torque to reach the rated
  top speed, and a Trabant that the table says takes **13 s** to 100 km/h does
  it in **5.9**.
- Adding a traction limit off the grip and CoG fields -- cap the drive force at
  `mu * m * g * axle share` -- recovers the Trabant (9.9 s rear-driven, 11.1
  front) but leaves the quick cars roughly three times too fast, because for
  them the cap never binds: the Astra reaches 100 in 3.4 s against a stated 10.
- The cause is the **torque curve**, and the table does not carry one (see the
  section above: fields 35/36 turned out to be the shift points). So no model
  built from this table alone can satisfy both the rated top speed and the rated
  0-100; a curve has to be assumed either way, and the port's fitted pairing is
  the assumption that reproduces the game's own figures most closely.
- So the real radius, Cd and frontal area are **recorded in the port's handling
  struct and deliberately unused**; the model stays on the fitted pairing, whose
  race timings are unchanged (etappe5 still finishes in 558.0 s).

- LIKELY: **field 7 is the centre-of-gravity height.** It is loaded at 0x40d5f8
  together with field 12 (gravity), fields 6 and 5 (the two axle distances) and
  field 2 (mass) -- which is precisely the set an axle-load calculation needs,
  `Fz = m*g*b/(a+b) -/+ m*ax*h/(a+b)`, with field 7 in the role of **h**. Its
  values fit: 0.40 m for an ordinary car and **0.30 for the Porsche**, the one
  low-slung sports car in the list. The bus keeps the traffic default of 0.40,
  which is wrong for a bus but consistent with how little care the traffic rows
  get elsewhere.
- Still unidentified:
  **50** (0 except the Astra and VW, mirroring 38), **51/52**
  (1.00/1.50 for everything **except the Diablo, 2.00/4.50**, read at half a
  dozen sites including the player-car init), **53** (which orders exactly by
  car performance -- 0.75 Trabant to 2.25 Diablo, a flat 1.00 for traffic -- and
  at 0x40d801 scales two components symmetrically) and **55** (0.00 in every
  vehicle, read once at 0x410c43).

## CARS.DAT A[29] is a visual rate, not a handling one (session 9)

The last unidentified car field, chased through `fcn.0040f3c0` -- the 2010-byte
per-object yaw update, and the only place A[29] is read.

- CONFIRMED: the function is `yaw_update(obj_index, float a, float steer)`. It
  opens by calling **0x441cd0** (a method on the object at 0x4e4970) for the
  **frame delta** -- the same getter the cop timers use -- and on the game object
  at 0x4e48e0 (stride 160) it works with
  **+0x6c heading, +0x74 speed, +0x7c yaw rate**. The third argument is the
  steering input: it is added into +0x7c, and the heading is finally rotated
  through the sin/cos of the yaw rate.
- CONFIRMED: at 0x40f8a1 the function takes `|a|` and compares it against the
  **double 20.0** at 0x47f3d0. Below that it applies the ordinary speed drag
  `speed -= (0.25 - 0.003 * speed) * speed * dt` (0.25 at 0x47f3a4, 0.003 at
  0x47f3d8, so the drag falls to zero around speed 83); **above it the drag is
  skipped entirely**.
- CONFIRMED: both arms then reach the A[29] code, which does

      d3d_objects[obj.field_08].f_7c -= dt * A[29] * a

  The array is the T_ARRAY built at fcn.00456ed0, and its own constructor names
  it: **`_d3d_objects`, element type `c_3dobject`**, stride 153 bytes (the code
  computes `field_08 * 17 * 9`).
- So **A[29] acts on the car's renderable model, not on its physics object.**
  That is consistent with everything else about it: (correction, session 14:
  the tweak menu does expose it, as "Rolhoek versterking" in its roll
  sub-menu 0x411710, ±0.1) and
  two runtime experiments found nothing -- patching it between 0.0 and 40.0
  changed neither a coast-down (038/017/000 against 037/015/000 km/h at matching
  race times) nor straight-line acceleration (131 against 138 km/h).
- LIKELY: it is a **per-car visual body-rotation rate** -- how far the mesh is
  swung relative to the physics heading, in proportion to `a`. The authored
  values fit that reading: **Trabant 1.6** (a lot of body movement), VW/BMW/
  Porsche/Mercedes 1.0, Astra 0.9, **Diablo 0.1** (almost none). Confirming
  which rotation would mean finding what reads `c_3dobject+0x7c` in the draw
  path; that has not been done.

## Driving the original under Wine (session 9)

Session 5 established screenshots; this adds **input**, which makes runtime
experiments on the physics possible.

- CONFIRMED: `Game.ini [INPUT]` holds Windows virtual-key codes --
  `LEFT=37 RIGHT=39 UP=38 (gas) DOWN=40 (brake) EXTRA1=17 (turbo)
  EXTRA2=18 (handbrake)` -- and `xdotool keydown Up` against the Xvfb display
  drives the car. Two runs reached 131 and 138 km/h on etappe0's start straight.
- CONFIRMED: **demo mode does not drive the player's car.** With `demo=1` the
  car rolls off the line at about 38 km/h, coasts to a stop within roughly five
  seconds and sits there while the race clock runs and the opponents lap it
  (POSITIE goes 2/4 to 4/4). Any code path behind throttle or steering is
  therefore invisible to a demo run -- which is why the DEBUGON trace holds a
  million lines of waypoint internals and not one police event.
- Two cautions for anyone repeating this: the countdown means the race clock is
  **not** at a fixed offset from launch, so frames must be matched by the
  on-screen RACE TIJD rather than by sleeps; and the car crashes into traffic
  within a second or two of any steering input, so collision damage swamps small
  effects unless the input is gentle.

## Speed cameras, posted signs and the radar bonus (session 9)

Chasing the fines turned up a second, entirely separate speed system.

- CONFIRMED: `txt_vbrd.cpp` (*verkeersbord*, traffic sign) holds a **speed zone**
  switch (fcn.0042b190, bounds `cmp eax,4`, assert at txt_vbrd.cpp:69
  `Unknown speed zone:%d!`). Four small functions each set a **posted limit** in
  the global **0x48c480** and then select the sign's frame from anim group 7:

  | zone | posted limit | sign frame |
  |---|---|---|
  | 0 | **50** km/h | 2 |
  | 1 | **70** km/h | 3 |
  | 2 | **100** km/h | 4 |
  | 3 | **120** km/h | 1 |
  | 4 | — | 0 |

  The zone-to-frame map is a jump table at 0x42b200 and is deliberately not the
  identity.
- CONFIRMED: **the speed camera reads that posted limit, not the cops'.**
  fcn.0042a2e0 compares the player's speed against 0x48c480 and takes the
  violation arm only when it is **strictly greater** -- there is no tolerance
  here, unlike the cops' 10%. On a violation it plays sound sample **11** and
  runs a three-callback sequence (0x4156d0 / 0x415750 / 0x415710), which is the
  **camera flash**: a 0x404-byte buffer is filled with 0xeb over 256 dwords and
  faded through the palette calls at 0x469dd0 / 0x469e90 / 0x469d10.
- So the game has **two independent speed systems that do not share a value**:

  | | limits | tolerance | enforced by |
  |---|---|---|---|
  | posted / camera | 50, 70, 100, 120 | none | the flitspaal, a flash |
  | police | 60, 80, 120, 2x top speed | **x1.1** | the cop AI |

  This is worth keeping in mind before "fixing" either to match the other.

### The radar bonus is for knocking the camera over

- STRONGLY SUPPORTED: **language.ini 51 `RADAR BONUS` is raised from a collision
  handler, not from driving under a limit.** fcn.00428d30 switches on the
  object's `field_0x10`; when it is 0 it runs
  `object_flyup_check_collision(obj, 10.0)` -- the function that asserts
  `(object_flyup_check_collision) massa is zero`, so 10.0 is a **mass** -- and on
  success sets the object's state at +0x14 to **18**, increments a counter at
  **0x4dc964** and raises banner 51 at 0x428da3. Objects with `field_0x10 == 1`
  take a different arm (mass 4.0, state 19) and award nothing. What is not
  nailed down is that `field_0x10 == 0` means specifically a flitspaal, which
  would need tracing how the field is assigned from the `.omp` object type.
  **Correction (session 13):** +0x10 is not a kind. It is the object's
  phase: 0 standing, 1 flying, 2 down. 0x428d30 is the handler registered
  for `.omp` **type 7** (the cameras) in the table at 0x48c2b4. Phase 0
  launches the camera (mass 10, the bonus); phase 1 is its flight
  0x429020(obj, 0x400, 4.0). The 4.0 is the settle height, not a mass, and
  19 is the bounce sample. See "Knock-over props" below.

### Every banner the game raises

Scanning for `mov dword [0x4882c0], imm32` -- the global the HUD's banner reads
-- gives the complete set, which is a useful closed list:

| address | id | language.ini |
|---|---|---|
| 0x4298d5 | 31 | NIEUW RONDE RECORD |
| 0x424fac, 0x42a621 | 44 | EXTRA TIJD |
| 0x429921 | 45 | LAATSTE RONDE |
| 0x424fe8 | 46 | INVINCIBLE |
| 0x416ccd | 47 | COP MODE |
| 0x42a51d | 48 | QUICK SERVICE |
| 0x41535e | 49 | EERSTE WAARSCHUWING |
| 0x415352 | 50 | LAATSTE WAARSCHUWING |
| 0x428da3 | 51 | RADAR BONUS |

- CONFIRMED (negative): **0 `GEFLITST` and 1 `BOETE: FL %d` are not in that
  list**, so the fine notice does not go through the banner global at all and
  must be built somewhere else -- the fine carries a `%d`, so it needs an
  sprintf the banner path does not do. **The port currently draws GEFLITST plus
  an invented fine as a banner, and that is not how the original shows it.**

### There is no fine system in this build

The hunt for the fine amount ends in a negative result, and a well-supported one.

- CONFIRMED: `lan_main.cpp` has a single text getter, **fcn.00431d30**, which
  builds the key from the index and reads it out of the chosen language section
  (its assert at lan_main.cpp:109 is `Couldn't find text for index %d,
  language: %s`). Everything that shows a language.ini string goes through it.
- CONFIRMED: there are exactly two ways to reach it.
  1. **Directly, with a constant index.** Scanning for `push imm8` immediately
     followed by a call to it gives
     `{17, 24, 25, 26, 27, 28, 32, 34, 36, 37, 38, 39, 42, 43, 52, 53, 54, 55}`.
  2. **Through the banner slot.** 0x4882c0 holds a pending id; 0x4882c4 latches
     it (0x41a96a) and 0x41a9a6 feeds that to the getter with a variable index,
     blinking it on a divide-by-100. The complete set ever stored into 0x4882c0
     is `{31, 44, 45, 46, 47, 48, 49, 50, 51}` -- no other code writes either
     global.
- CONFIRMED, therefore: **`0 GEFLITST`, `6/8/10 POLITIE` and all three
  `7/9/11 BOETE: FL %d` (and `1`) are never fetched and never displayed.** The
  same goes for the fuel and breakdown set (`2` NIET GENOEG, `3` GELD,
  `4` GETANKT, `5` KOSTEN, `12`/`14` WEGENWACHT, `15` GEEN BENZINE MEER) and the
  money lines (`16` FL %06d, `20`-`23` BUDGET / PRIJZENGELD / BOETES / NIEUW
  BUDGET).
- CONFIRMED: **menu.dat cannot be where they are used either** -- it contains no
  `lan_main.cpp`, no `NEDERLANDS`/`ENGELS`/`DUITS` and no `LANGUAGE.INI`, so the
  shell does not read language.ini at all.

So the fines, the refuelling messages and the budget screen are **vestigial
strings**: a career/money layer that language.ini still describes but that no
shipped binary implements. This closes the question of "where do the fine
amounts come from" -- nowhere. **The port's invented fine has no original to
match**, and should either be dropped or kept openly as an addition.

### The game-mode sequences, and what mode 8 is

The global the cop code gates on turns out to be the game's **mode**, and the
"cop sequence" is one of nine.

- CONFIRMED: **fcn.00424d20(mode) sets 0x4d9a7c** and dispatches through a
  9-entry jump table at **0x424e00** (`cmp eax, 8` bounds it). Each live arm
  starts a sequence with `fcn.00416760(seq_id, cb1, cb2, cb3)` -- the same
  three-callback shape the camera flash uses.

  | mode | arm | seq | callbacks |
  |---|---|---|---|
  | 0 | 0x424d68 | — | sets 0x4d9a9c / 0x4d9e58 to 1 |
  | 1 | 0x424d39 | 2 | 0x416ce0 / 0x416e00 / 0x416fe0 |
  | 2 | 0x424d5d | — | clears 0x4d9aa0 |
  | 3 | 0x424d82 | 4 | 0x4154d0 / 0x4155d0 / 0x415690 |
  | 4 | 0x424da6 | — | clears 0x4d9a9c / 0x4d9e58 / 0x4d9a78 |
  | 5, 7 | 0x424dff | — | bare `ret`, no-ops |
  | **6** | 0x424db8 | **12** | 0x416060 / 0x4162e0 / 0x416440 |
  | **8** | 0x424ddc | **11** | 0x415300 / 0x415420 / 0x415490 |

- CONFIRMED: **mode 8 is the police chase.** Its callbacks are exactly the
  functions documented above -- 0x415300 is the handler that increments
  `g_times_caught`, and 0x415420 is the one that gates on
  `[0x4d9a7c] == 8` and tests `g_times_caught >= 3`. So the `== 8` guards in
  ai_cops.cpp mean nothing more than "while the chase sequence is running".
- CONFIRMED: **mode 6 is the arrest.** The third catch does
  `fcn.00424d20(6)`, and mode 6's callbacks span 0x416060..0x416440 -- the range
  that holds the only two fetches of language.ini **42 GEARRESTEERD**
  (0x416186) and **43 RACE OVER** (0x416201). So the third strike switches the
  game into the arrest sequence, which shows those two messages and ends the
  race.

### Not established

`RESET_COP` (0x402200) and `cop_lane` (0x4051f0) were read in session 12; see
"RESET_COP, BACKTRACK_SPEED_TRIGGER and cop_lane". The other sequences (modes 1, 3) are
unidentified.

## The 0x484B18 table, third pass: the last five fields (session 10)

Fields 50, 51, 52, 53 and 55 were the only ones left unnamed. Four of them are
now CONFIRMED from the code that reads them, and with them the table's whole
**longitudinal** force law closes.

### Field 38 (and its mirror 50) is the driven-axle split

The drive-force function is **0x40da90**. It builds

    F = f37 * f34(final drive) * gear[f27 + current_gear] * torque / f22(wheel radius)

(`torque` arrives at `[esp+0x14]`, the current gear index in 0x4a63d4) and then
splits it between two output structures:

    0x0040db4a  fstp dword [ecx+0x08]   ; = f38       * f51 * F
    0x0040db4d  fstp dword [esi+0x08]   ; = (1 - f38) * f51 * F

- CONFIRMED: **field 38 is the fraction of drive torque sent to one axle**, and
  it is the **front** one, because f38 is 1.00 for exactly two vehicles --
  **RACER_2 (Opel Astra) and RACER_3 (VW Golf)** -- and 0.00 for all fourteen
  others. Those two are the fleet's front-wheel-drive cars; the BMW, Porsche,
  Mercedes and Diablo are not. So 1 = FWD, 0 = RWD, and the field is a
  general split that the data only ever uses at the extremes.
- The `fsubr qword [0x47f2c0]` at 0x40db0e is the `1 - f38`, with 0x47f2c0 = 1.0.
- **Field 50 carries the same 0.00/1.00 pattern** because it is field 38's
  counterpart in the upgraded gearbox block -- an upgrade does not change which
  axle drives.
- Worth noting: the real Trabant 601 is front-wheel drive, and the table gives
  it 0.00. Either a deliberate simplification or the data's own error.

### Field 51 is the turbo's drive-force multiplier

- CONFIRMED: **field 51 multiplies the total drive force**, is 1.0 normally and
  is forced to **6.0 while the turbo is held**.
- The turbo tick is **0x425cf0**. With the tank 0x4d9e3c > 0 it decrements the
  tank, and then:

      0x00425d82  fld   dword [ecx*2+0x484bf2]   ; field 51
      0x00425d89  fcomp dword [0x47f7d0]         ; = 6.0
      0x00425d94  jnz   0x425da3                 ; already 6.0? don't re-save
      0x00425d96    fld  dword [ecx*2+0x484bf2]
      0x00425d9d    fstp dword [0x4d9cf0]        ; stash the original
      0x00425da3  mov   dword [ecx*2+0x484bf2], 0x40c00000   ; f51 := 6.0
      0x00425dae  mov   dword [0x4d9dfc], 0x01   ; "turbo active"

  and sound **84** starts on a channel kept in 0x48b464 (`push 0x54;
  call 0x4286a0` at 0x425d47). When the tank hits 0 it clears 0x4d9dfc, stops
  the sound (0x4670f0) and restores the original:

      0x00425e0d  fstp  dword [edx*2+0x484bf2]   ; f51 := [0x4d9cf0]

- **The tank holds 100 units.** 0x427fd0 sets the capacity 0x4d9e40 = 100 and
  the tank 0x4d9e3c = `turbo_owned ? 100 : 0` (via the `neg`/`sbb`/`and 0x64`
  idiom); 0x428050 is the getter and 0x428060 the setter, which **clamps to
  [0, 100]**. One unit is spent per tick, and 0x425cf0 is called from seven
  sites -- 0x426606, 0x426a2c, 0x426a3c, 0x426f27, 0x426f3a, 0x42704d,
  0x427061 -- one per car.
- CONFIRMED and notable: the upgrade applier writes **1.0 into field 51
  unconditionally** (`mov dword [eax+0x484bf2], 0x3f800000` at 0x428429), so
  the Diablo's table value of 2.00 **never takes effect**.

### Field 52 is the longitudinal traction cap

- CONFIRMED: **`f52 * 0.8` is the maximum drive force as a multiple of the axle
  load.** Immediately after storing the split force, 0x40db50 takes
  `f52 * 0.8 * [edx+0x04]` (the axle load) and compares `|F|` against it; if `F`
  is the larger, 0x40dbdb..0x40dbec rebuilds it as `+/- f52 * 0.8 * load` and
  writes it back. 0x484ab0 = 0.80 is a global; 0x47f250/0x47f254 are the
  +1.0/-1.0 that carry the sign.
- f52 is **1.50 for every vehicle** -- a cap of 1.20 g -- except the **Diablo at
  4.50**, i.e. 3.60 g. Unlike f51, f52 is not overwritten by the applier, so the
  Diablo really does get it.

### Field 53 is the brake coefficient

- CONFIRMED: **brake force per axle = `-load * f53 * brake_input`.** The brake
  function is **0x40d7d0**, and it is the drive function's shape with the gear
  train removed and both axles treated alike:

      0x0040d801  fld   dword [edx*2+0x484bfa]   ; field 53
      ...
      0x0040d822  fstp  dword [esi+0x08]         ; = -[edi+4] * f53 * brake
      0x0040d82e  fstp  dword [ebp+0x08]         ; = -[ebx+4] * f53 * brake
      0x0040d831  fmul  dword [0x484ab0]         ; f53 * 0.8, the same clamp

- The values are peak braking g and order exactly by how quick the car is:
  **0.75** Trabant, **1.05** Astra and Golf, **1.20** BMW, **1.35** Porsche,
  **1.65** Mercedes, **2.25** Diablo, **1.00** every traffic vehicle.
- The `fmul [0x484ab0]` on f53 mirrors the drive path's clamp exactly, so
  LIKELY the brake force is capped at `0.8 * f53 * load` the same way; the
  comparison itself was not traced to the end.

### Field 54, exactly

Already recorded as "a blanket grip multiplier". Tracing the FPU stack through
0x40e530 gives the whole lateral law and shows f54 multiplies **both** axles:

    [edi] = -load_front * f17 * f54 * sin(f16 * atan(f15 * slip_front))
    [ebx] = -load_rear  * f20 * f54 * sin(f19 * atan(f18 * slip_rear))

f54 is loaded at 0x40e59d, stashed at `[esp+0x14]` (0x40e5af) and multiplied
into the front at 0x40e5bb and the rear at 0x40e5d6. It is **1.20 for all seven
racers and 1.00 for all nine traffic vehicles** -- a flat 20% cornering
advantage for the racers. Note the force is proportional to the **axle load**,
which is how weight transfer enters the tyre model.

### Field 55 is an integer, not a float

- CONFIRMED: f55 is read as an **integer**, not a float
  (`mov ecx, dword [esi+0x484c02]` at 0x410c41), which is why it dumps as 0.00 --
  the values are **31 to 36**:

  | value | vehicles |
  |---|---|
  | 31 | Trabant, Porsche, MPOLIZEI, ADAC, N_ASTRA, N_POLO |
  | 32 | Astra, BMW, Mercedes, N_CALIBR |
  | 33 | Golf, Diablo, N_GOLF |
  | 34 | N_BESTEL (van) |
  | 35 | N_BUS |
  | 36 | N_VRTAUT (lorry) |

- It is pushed, together with field 0 and the width field 4, into **0x43f770**,
  which allocates a new element of the **160-byte live-vehicle array at
  0x4e48e0** (its uninitialised guard prints `T_ARRAY::get_new %s[%d] of type
  %s not initialized` from `c:\visual~1\dplib\array.hpp` -- the game's vendored
  C++ template array).
- LIKELY a per-vehicle-**class** resource id rather than a per-model one: the
  van, the bus and the lorry each get a value of their own while the twelve cars
  share just three, and the BMW shares with the Astra. An engine-sound set is
  the obvious candidate. **Not identified** -- this is the one field of the 56
  still open.

### The mirrored block is fields 27-36, settled instruction by instruction

The gearbox copier runs from 0x4284df to 0x428567 as eleven
`fld dword [eax+src]; fstp dword [eax+dst]` pairs:

    f39->f27  f40->f28  f41->f29  f42->f30  f43->f31  f44->f32
    f45->f33  f46->f34  f46->f34  f47->f35  f48->f36

- CONFIRMED: the block is **f27-f36 mirrored by f39-f48**, as recorded in
  session 9. **f46->f34 is emitted twice** -- a duplicated line in the original
  source, harmless because it is idempotent.
- So f37/f49 (both 1.00) and f38/f50 (the FWD flag) are **not** copied; they
  only happen to carry equal values, which is what made the block look wider.

### What this closes

The longitudinal model is now entirely in the table:

    F_drive(axle) = split * f51 * (f37 * f34 * gear / f22) * torque
                    capped at +/- 0.8 * f52 * axle_load
    F_brake(axle) = -axle_load * f53 * brake_input
    F_lat(axle)   = -axle_load * D * f54 * sin(C * atan(B * slip))

with `split` = f38 front / `1 - f38` rear. The one input still not in the table
is `torque` itself -- the **no torque curve** finding stands.

## The upgrade shop (session 10)

`GAME.INI [PLAYER]` holds **six** purchasable upgrades, read one after another
at 0x421d7c..0x421e0e, each into its own global. The applier is **0x428400**,
which runs per race and rewrites the player's row of the 0x484B18 table in
place:

| ini key | flag VA | effect | site |
|---|---|---|---|
| `motor` | 0x4d9ec0 | field 24 (power) x **1.3**, field 25 (peak-torque rpm) x **1.1**, field 23 (redline) x **1.1** | 0x428435 |
| `banden` (tyres) | 0x4d9ec4 | fields 15, 17, 18, 20 x **1.2** -- the Pacejka **B** and **D** on both axles | 0x428475 |
| `turbo` | 0x4d9ec8 | fills the 100-unit turbo tank 0x4d9e3c | 0x428563 |
| `abs` | 0x4d9ecc | sets 0x4a63c8 = 1, read only in the brake code (0x40d85a, 0x40d982) | 0x4284c5 |
| `versnellingsbak` (gearbox) | 0x4d9ed0 | copies the upgraded gear set f39-f48 over f27-f36 | 0x4284d1 |
| `injectie` (injection) | 0x4d9ed4 | **no physics effect** | -- |

- CONFIRMED: the multipliers are doubles at 0x47f880 = **1.3**, 0x47f888 =
  **1.1** and 0x47f890 = **1.2**.
- CONFIRMED: the applier also writes **field 51 := 1.0 unconditionally** before
  any of the branches (0x428429).
- STRONGLY SUPPORTED: **`injectie` buys nothing in the physics.** A scan of
  `.text` for every absolute form (`A1`, `8B /r`, `89 /r`, `A3`, `C7 05`) finds
  0x4d9ed4 touched in exactly two places: the ini read at 0x421e0e, and
  **0x410940**, where it only contributes to a count `edi` of the three
  drivetrain upgrades (`motor`, `versnellingsbak`, `injectie`). That count then
  feeds a variant index, not a force.
- `abs` is the only upgrade that does not touch the table: it sets a flag the
  brake function reads.

## ABS, the handbrake and the input mask (session 11)

- CONFIRMED: **what ABS does.** The brake function 0x40d7d0 computes each
  axle's force `F = f53 * load * input`, then compares `|F|` with
  `0.8 * f53 * |load|` (0.8 = 0x484ab0; front at 0x40d84d, rear at 0x40d975).
  Past it the wheels have locked: unless the ABS flag 0x4a63c8 is set
  (0x40d85a, 0x40d982) the force is rebuilt at exactly the cap, signed as
  before, and a skid intensity goes to 0x4a63dc. So **without ABS an input
  above 0.8 brakes no harder than 0.8**; with ABS the full input applies and no
  skid is laid. (The axle "load" is the y component of the axle force vector,
  negative, which is why `-load * f53 * input` keeps the sign the right way.)
- CONFIRMED: **the input mask.** 0x4246c0 returns a per-frame bitmask built from
  the Game.ini `[INPUT]` bindings stored at 0x4d8e60.. by 0x424490:
  LEFT 0x1, RIGHT 0x2, UP 0x4, DOWN 0x8, EXTRA1 0x10, EXTRA2 0x20 (key names
  at 0x48ac2c..0x48ac08). **Correction (session 13):** it has two callers,
  0x4265b0 (0x4265b4) and 0x4269c0 (0x4269c5). With the shipped `DEVICE=1` the
  race runs **0x4265b0**, not 0x4269c0; see "Input devices" below.
- CONFIRMED, **joypad only (DEVICE 2)**: **holding the turbo is holding the
  gas.** 0x4269c0 calls the turbo tick 0x425cf0 with 1 when bit 0x10 is set
  and then ORs in the UP bit (`or ebx, 4` at 0x426a34). The keyboard handler
  0x4265b0 does not (runtime check in session 13: holding only Ctrl on the
  grid left the car at 0 km/h).
- CONFIRMED: **the handbrake flag is 0x4d9df8**. The keyboard handler sets it
  from bit 0x20 (EXTRA2, Alt) alone (0x4265e5). The joypad handler 0x4269c0
  sets it when that bit is up **or** when the raw key VK 0x20 -- Space -- is
  down (`push 0x20; call 0x466910` at 0x426a02).
- CONFIRMED: **what the handbrake does.** With the flag set, the car step calls
  0x40e870 (at 0x40d3d2) right after the drive/brake function 0x40dd00, and it
  overwrites the **rear** axle's outputs with sliding friction of magnitude
  `0.8 * f20 * |rear_load|` (f20 = rear Pacejka D, 0x484b76; the two 0.8s are
  0x484ac4 / 0x484ac8): longitudinal `-sign(v_z) * |cos a| * F`, lateral
  `-sign(v_lat) * |sin a| * F`. Together that is Coulomb friction straight
  against the rear axle's motion. It also writes a skid intensity of 10 above
  13.89 m/s (50 km/h, 0x484abc) and `|v_z| / 13.89 * 10` below, and zeroes the
  lateral velocity when the lateral force reverses with a jump over 1000
  (0x40e984), which damps a sign-flip oscillation.
- STRONGLY SUPPORTED: the angle `a` is the **rear slip angle**. 0x40e3d0,
  called earlier in the same step, computes bicycle-model slip angles with
  `fpatan` of `(v_lat +- yaw_rate * cog_distance) / v_long` (fields 5/6 at
  0x484b3a/0x484b3e), front minus the steer angle. That `a` is that rear output
  was not traced argument by argument.
- Other handbrake reads (0x40d24b, 0x40e5ef, 0x40f2a7 and on) gate low-speed
  snap-to-rest code off while the handbrake is held. Not followed further.

## Visible damage does not exist in the shipped game (session 11)

- CONFIRMED by the runtime trace: for **every** model the damage loader
  (`obj_dmg.cpp`, "Adding new damage object at index %d, filename = %s") tries
  to open `<mesh>.DMG` -- `r_trabbi.DMG`, `s_licht.DMG`, ... -- and **every one
  fails**: 2724 "Damage file does not exist." lines in the shipped
  `debug.txt`, and not one "Damage file locked and loaded.".
- CONFIRMED: there is no `.DMG` file anywhere on the CD -- not in the extracted
  data, not in the archives (RC.IMG holds only HUD sprites, RCS.IMG only
  d3d/pcx/tga/wav/fnt), and no `.dmg` name in the image.
- What a damage frame *would* be, from the applier 0x43fc40: per zone and frame,
  a list of `{vertex index, dx, dy, dz}` entries (16 bytes) added to the base
  vertex positions of a deformed copy of the mesh (0x43fe9f..0x43ff45). So the
  machinery is per-vertex dents, complete in the code, with **no data**. The
  DAMAGE_VALUE/DAMAGE_MESH bookkeeping does **not** run either (corrected in
  session 12): the regional handler returns before it for any model without
  damage frames, so nothing changes on screen or in those arrays. The port's lack of visible damage
  therefore **matches** the original.

## What the damage number does: TOTAL LOSS, two sounds, a cheat (session 11)

The 0-100 damage lives at **0x4d9e30**: `set_damage` 0x4281c0 and `add_damage`
0x428390 both clamp it to [0, 100], and every reader goes through the getter
0x4281b0.

- CONFIRMED: **damage >= 99 ends the race.** 0x424be8 compares it with 0x63
  and, in game mode 0 (`[0x4d9a7c] == 0`, racing), sets the game-over sub-mode
  0x4867a4 = 0 and calls `set_game_mode(6)`.
- CONFIRMED: the game-over sub-mode picks the text through the jump table at
  **0x4162cc**: -1 asserts "No game over mode set!", **0 -> 40 TOTAL LOSS**
  (`push 0x28` at 0x4160ed), **1 -> 41 TIJD IS OM** (0x41613a), **2 -> 42
  GEARRESTEERD** (0x416184).
- CONFIRMED: `add_damage` does nothing when `0x4d9a7c != 0` (not racing) or
  when **0x4d9eb8** is set. That flag is a cheat: **Ctrl + Space + I**
  (VK 0x11, 0x20, 0x49 at 0x424fbe..0x424fe6) sets it and raises banner 46
  **INVINCIBLE**.
- CONFIRMED: two **damage sounds**, from the 3 x 12-byte table at 0x48b440
  `{handle, threshold, sample}`, walked every frame at 0x425fbe: at damage
  **>= 65** sample **37 `rammel.wav`** loops, at **>= 75** sample **38
  `rammel2.wav`** ("rattle"); each stops again if the damage drops below its
  threshold (a QUICK SERVICE repair).
- The other readers are the HUD bar (0x41aabf, frame = damage * (frames-1) /
  100), the HUD percentage (0x41ab38) and the ini save (0x422f1d).

## The checkpoint clock on the highway stages (session 11)

The point-to-point stages run against a **countdown**, and running out ends the
race.

- CONFIRMED: game timer **11** is the clock. At race init (0x424344..0x42439a)
  the game computes a per-checkpoint bonus and starts timer 11 at it:

      bonus_s = (int)( (int)(10 * dist[etappe]) / (top * k) )      (x1000 -> ms)

  - `dist` is a 7-entry table built on the stack at 0x4242e7: **142, 110, 142**
    for etappe **1, 3, 5**, and 0 for the four circuits;
  - `top` is `0x410b00(car)`: the CARS.DAT top-gear ceiling in m/s -- the
    upgraded 6th gear (0x4a5f3c) if the gearbox is owned, otherwise the stock
    5th (0x4a5f20) -- **plus 4.0 if the motor is owned** (`fsub` of -4.0 at
    0x47f44c);
  - `k` is **0.4 on EASY and 0.65 on HARD** (0x47f76c, 0x47f770, picked on
    `[OPTIONS] difficulty`).

  It reads as a distance over a speed: the time to cover `10 * dist` metres at
  40% or 65% of the car's top speed.
- CONFIRMED: the **checkpoint** handler (trigger 45, 0x42a530) ignores a chunk
  within one of a checkpoint already taken. On a highway stage it **adds the
  bonus** to timer 11 (0x42a59e), plays sample 83 `checkp.wav` and raises 44
  **EXTRA TIJD**; on a circuit it records split times instead.
- CONFIRMED: on a highway stage, timer 11 expiring in game mode 0 sets
  sub-mode 1 and calls `set_game_mode(6)` (0x424b80..0x424b9a): **41 TIJD IS
  OM**.
- CONFIRMED: the HUD picks a **highway panel** (0x41a5f0) over the circuit one
  (0x41a780) on Raser.ini's circuit flag (0x41a94d). It draws the literal
  **CHECKPOINT** (0x48830c, not a language string) over timer 11 as
  `%02d:%02d:%d`.
- CONFIRMED in the running game (Wine, etappe5, `Difficulty=1`, car 0 with every
  upgrade): **CHECKPOINT 00:42:4 at RACE TIJD 00:00:5**, i.e. the clock starts
  at **43.0 s**, the formula's figure (1420 / ((45.83 + 4) * 0.65) = 43.8,
  truncated), and counts down in real time. The panel sits **top centre**.
- CONFIRMED: a second cheat, **Ctrl + Space + T**, on highway stages only
  (gated on the circuit flag at 0x424f9a), adds 180 000 ms to timer 11 and
  raises 44 EXTRA TIJD (0x424fa5). The code polls raw key state with no edge
  latch, so holding it presumably keeps adding (not tried).
- The 43 s is tight: the gaps after the first checkpoint are about 147 chunks.
  At ~12.8 m per chunk that is ~1.9 km, which needs about 157 km/h on average
  in a car topping out at 165. The port's autopilot runs out before the first
  checkpoint on all three highway stages on HARD.
- Raser.ini `[MAPS]` arg 10 (10000 on every row) goes x1000 into **timer 0**
  (0x424318..0x424331), not into the stage clock. What timer 0 is for was not
  followed.

## How the CPU racers pick their speed (session 11)

This supersedes the session-6 reading "a racer's chosen speed is
`ladder_speed x multiplier`". That reading had the right numbers, but the
structure behind them is a behaviour table and a speed controller.

### The behaviour table, by emulation

`init_ai_states` (0x405bc0) builds `AI Behaviors[16]` (0x2c bytes each, pointer
at 0x4a5b88) in code. It was **run in an emulator** (Unicorn, with malloc and
the trace and assert calls stubbed; `reverse-engineering/tools/emu_ai.py`) for
both difficulties, and the table read back out. The fields calc_speed names:
**+0x08 bh_brake_speed, +0x10 acceleration, +0x14 bh_preferred_speed, +0x18
deceleration**.

| behaviour | used by | preferred HARD / EASY (m/s) | brake | accel | decel |
|---|---|---|---|---|---|
| 0-6 | traffic and others (2, 3 = 25 m/s traffic) | | | | |
| 7 | racer in the Trabant | 37.00 / 37.00 | 33.00 | 2.50 | 15.0 |
| 8 | Golf | 49.92 / 47.32 | 45.12 | 3.94 | 14.8 |
| 9 | Astra | 53.90 / 52.80 | 48.02 | 4.61 | 14.8 |
| 10 | BMW | 63.92 / 59.16 | 58.28 | 7.05 | 14.8 |
| 11 | Porsche | 73.60 / 66.40 | 66.24 | 8.74 | 14.8 |
| 12 | Mercedes | 81.00 / 70.20 | 72.00 | 8.10 | 14.8 |
| 13 | Diablo | 88.00 / 74.00 | 79.20 | 9.24 | 14.8 |
| 14 | police | 70.00 | 58.00 | 12.0 | 18.0 |

(Brake and accel are given for HARD.)

- CONFIRMED: the two multiplier ladders are the **difficulty**, picked at
  0x405bcd on `[OPTIONS] difficulty`: HARD (1) uses 0.96 / 0.98 / 0.94 / 0.92 /
  0.90 / 0.88 for behaviours 8-13, EASY (0) uses 0.91 / 0.96 / 0.87 / 0.83 /
  0.78 / 0.74. This closes "which ladder is selected when". The
  speeds logged in the shipped debug.txt (49.92 / 53.90 / 63.92, with
  `Difficulty=1`) are HARD's behaviours 8 / 9 / 10 exactly.
- CONFIRMED: the behaviour is picked **by car** through the jump table at
  0x4113f0: car 0 -> 7, 1 -> 9, 2 -> 8, 3 -> 10, 4 -> 11, 5 -> 12, 6 -> 13 (the
  Astra and Golf are swapped against the car order).
- CONFIRMED: the racers' **cars are chosen next to the player's**
  (0x411200..0x4112a6). The player's car is marked used, then racer 0 takes the
  nearest free car at or above it (0x411410), falling back to the nearest at or
  below (0x411460); racer 1 the reverse; racer 2 like racer 0. From the
  Trabant that is Astra, Golf, BMW, i.e. behaviours 9, 8, 10: every race in
  the shipped debug.txt shows exactly that set.

### calc_speed (0x407140), from its own trace strings

    desired = ai_chosen_speed - bh_brake_speed * |cross| * ai_chosen_speed / bh_preferred_speed
    desired = min(desired, ai_max_speed_allowed)
    if desired > current:  new = current + accel * frame_time;  cap at ai[+0x5c] * preferred
    if desired < current:  new = current - decel * frame_time;  floor 0

- `ai_chosen_speed` (ai+0x1c) is set to `bh_preferred_speed` at creation
  (0x4069da). `|cross|` is the sine of the angle between the object's forward
  axis and the normalised vector to its target waypoint (0x4071d3..0x407329).
  `ai[+0x5c]` is initialised to 1.0 (0x4068c8) and nothing else writes it.
- The AI instance records are 108 bytes at `[0x4a5b40]`: +0x08 ai_state, +0x18
  ai_max_speed_allowed, +0x1c ai_chosen_speed, +0x24 behaviour, +0x34 racer
  state, +0x3c lane count ("1-baans".."4-baans"), +0x60 chunks travelled.

### Speed limits are for traffic, not racers

- CONFIRMED: the AI trigger routine 0x407ba0 (fed both `.pmp` trigger slots by
  0x407fc0) sets ai_max_speed_allowed from the speed triggers. Its mapping
  is a **third** one, distinct from the police's and the camera's:
  **4 -> 16.67 m/s (60), 5 -> 22.22 (80), 6 -> 33.33 (120), 8 -> 500 (none),
  10 -> 0 and ai_state 1 (stop)**, with 7 and 9 ignored. The data fits it: on
  etappe1 and etappe3 a 4 or 5 is followed 12-15 chunks later by a 6, i.e. a
  zone and then back to 120.
- CONFIRMED: 0x407fc0 has **one caller**, in HANDLE_TRAFFIC_AI (0x40b847).
  HANDLE_RACER_AI (0x40acc0, `ai_racer.cpp`) does not call it, and a racer's
  max-allowed starts at 500. **Racers ignore speed limits.**

### The rubber band

HANDLE_RACER_AI rewrites ai_max_speed_allowed each update according to the
racer state at ai+0x34:

- **state 0** (0x40af1a): `lead = ai[+0x60] - [0x4d9e70]`, the racer's chunks
  travelled minus the player's. If `lead > 5`: `max = preferred - (2*lead -
  10)` m/s, floored at 5 (0x47f230). So a racer that pulls more than five
  chunks ahead is held back. A racer behind gets no catch-up.
- **state 1** (0x40ad90) would pace the player: more than 9 chunks behind ->
  `[0x4d9e68]` capped at preferred; 3-9 behind -> preferred and back to state
  0; level -> the player's speed; ahead -> `(1 - 0.2 * floor(gap/15)) * player
  speed`, floored at 15. **Nothing ever writes state 1**: the only stores to
  ai+0x34 are 0 at creation and on leaving state 1, and 2 at the finish
  (0x40b0e0). So it is dead code.
- **state 2** is finished (max-allowed 0, set by the finish trigger handler).

## How traffic is spawned and driven (session 11)

- CONFIRMED: the traffic spawner (0x40ca70; the loop at 0x40cacd..0x40cb2e)
  makes **two independent `rand() % 12` draws** into the 12 x 8-byte table at
  **0x484a48**: one picks the vehicle from the first column, the other the
  behaviour from the second. The table:

  | vehicle (0x484B18 index) | 9 Golf | 9 Golf | 10 Astra | 10 Astra | 13 Polo | 13 Polo |
  |---|---|---|---|---|---|---|
  | behaviour | 2 | 3 | 2 | 3 | 2 | 3 |

  | vehicle | 11 Bus | 12 Vrachtauto | 14 Bestel | 14 Bestel | 15 Calibra | 15 Calibra |
  |---|---|---|---|---|---|---|
  | behaviour | 2 | 2 | 2 | 3 | 2 | 3 |

  So the mix is Golf, Astra, Polo, Bestel and Calibra at 2 in 12 each, and the
  bus and the lorry at 1 in 12. Behaviour 2 comes up 8 times in 12 and
  behaviour 3 the other 4. The shipped debug.txt's 234 x behaviour 2 against
  150 x behaviour 3 fits that.
- CONFIRMED: **the road roller (N_WALS) is never random traffic**: it is not in
  the table.
- Both traffic behaviours prefer **25 m/s** (behaviour 2: brake 11.5, accel
  2.55, decel 13.0; behaviour 3: brake 21.0, accel 3.90, decel 14.5, from the
  emulated table). A car's speed therefore does not depend on its model: a bus
  cruises like a Golf. Traffic runs through the same calc_speed as the racers,
  and unlike them it **obeys the AI speed triggers** (0x407fc0 is called from
  HANDLE_TRAFFIC_AI).

## The motor upgrade accounts for CARS.DAT's upgraded top speeds (session 11)

An experiment with the port's model: calibrate each car on its **stock**
CARS.DAT top speed, then apply 0x428400's motor multipliers (power x1.3,
peak-torque rpm and redline x1.1) and read off the drag/rev-limited top speed.

| car | CARS.DAT stock | CARS.DAT upgraded | stock + motor |
|---|---|---|---|
| Trabant | 151 | 165 | 165.6 |
| Astra | 166 | 180 | 182.5 |
| Golf | 180 | 195 | 197.6 |
| BMW | 195 | 210 | 214.4 |
| Porsche | 210 | 226 | 230.8 |
| Mercedes | 225 | 241 | 247.1 |
| Diablo | 240 | 255 | 262.8 |

- LIKELY: **CARS.DAT's upgraded top speed is the motor upgrade's effect**. The
  match is 0-3% across all seven cars, overshooting more on the quick ones. This
  is a model result: the torque curve is synthesised, so it is evidence and not
  proof.
- Under the port's fit the **gearbox** swap alone cannot be judged this way.
  The fitted wheel radius puts every stock car exactly on its redline at top
  speed, so the shorter upgraded ratios *lower* the top speed (Diablo 240 to
  216), which the real radius (field 22) says should not happen.

## The difficulty modifier is a 10-step table (session 10)

- CONFIRMED by the binary's own error text: **0x484aec is "Difficulty modifier
  has invalid value"**, printed at 0x41097e when `[0x4d9b1c] > 9`. So 0x4d9b1c
  is a **difficulty modifier with legal values 0-9**.
- It indexes a 10-entry float table built on the stack at 0x4108dd..0x41092f:
  **0.60, 0.70, 0.80, 0.90, 1.00, 1.10, 1.20, 1.30, 1.40, 1.50.**
- That factor multiplies **six** per-gear floats at `0x4a5f28 + k*124` when the
  gearbox upgrade is owned, or **five** at `0x4a5f10 + k*124` when it is not
  (the loops at 0x4109bc and 0x4109de) -- six against five being the gear count
  with and without the upgraded box. It runs for each of **three** 96-byte
  entries walked from 0x4a55d0 to 0x4a56f0.
- Each entry also receives `clamp(car_slot + drivetrain_upgrades - [0x4a55d0],
  0, 3) * 4.0` at offset +0x2c (0x410991..0x4109a2, with 0x47f368 = 4.0).
- LIKELY: the three 96-byte entries are the **three AI opponents**, so the game
  scales them both by the chosen difficulty and by how far the player has got
  (car slot plus purchases) -- rubber-banding by purchase. Not confirmed: the
  block's identity is inferred from the count, and **where 0x4d9b1c is set has
  not been traced**. `[OPTIONS] difficulty` goes to a *different* variable,
  0x4d9ab0 (0x421e32).
- **Session 13:**
  - CONFIRMED: 0x4d9b1c is `[OPTIONS] dif_mod` (read 0x421f7a, stored
    0x421f82; Game.ini says "0-3 easier, 4=normal, 5-9 harder", and ships
    4 = ×1.00).
  - CONFIRMED: the entries are the three racers, whose car index 0x4112b6
    writes to +0.
  - The scaled arrays at 0x4a5f10 / 0x4a5f28 are the racer car type's
    CARS.DAT max-speed rows, normal and upgraded (the 124-byte car stride).
  - STRONGLY SUPPORTED: +0x2c is never read, so the purchase bonus is dead.
  - The upgrades counted are 0x4d9ec0, 0x4d9ed0 (gearbox) and 0x4d9ed4.

## The complete sound-effect table, and field 55 (session 10)

Field 55 of the 0x484B18 table is the **engine sound sample index**, CONFIRMED,
and the evidence settles the whole sound table with it.

### The table: 87 samples of 19 bytes, at VA 0x48BAF0

The SNDFX sample player **0x4286a0** takes a sample index in `[esp+0x0c]`,
bounds-checks it (`0 <= i <= 0x56`, printing **"SNDFX sample out of bounds"**
from 0x48c1a4 otherwise) and then indexes a table of fixed-width filenames:

    0x004286cc  lea eax, [esi+esi*8]                  ; eax = 9*i
    0x004286d1  lea ecx, [esi+eax*2+0x48baf0]         ; ecx = 19*i + 0x48BAF0

- CONFIRMED: **87 entries of 19 bytes each at VA 0x48BAF0**, index 0-86.

| # | sample | | # | sample | | # | sample |
|---|---|---|---|---|---|---|---|
| 0 | werk01.wav | | 29 | carhard.wav | | 58 | aanh23.wav |
| 1 | werk02.wav | | 30 | carhard2.wav | | 59 | aan31a.wav |
| 2 | kerkklok.wav | | **31** | **auto1.wav** | | 60 | aan31b.wav |
| 3-10 | tick.wav (x8) | | **32** | **auto2.wav** | | 61 | aan32a.wav |
| 11 | flits.wav | | **33** | **auto3.wav** | | 62 | aanh33.wav |
| 12 | 3git.wav | | **34** | **best_bus.wav** | | 63 | aanh34.wav |
| 13 | 2git.wav | | **35** | **bus.wav** | | 64-76 | achter1..achter19.wav |
| 14 | 1git.wav | | **36** | **vracht.wav** | | 77-81 | opgev1..opgev9.wav |
| 15 | gogit.wav | | 37 | rammel.wav | | 82 | schuur2.wav |
| 16 | pionb1.wav | | 38 | rammel2.wav | | 83 | checkp.wav |
| 17 | pionb3.wav | | 39 | sliploop.wav | | **84** | **turbo.wav** |
| 18, 19 | paal.wav | | 40 | psirene1.wav | | 85 | toeren2.wav |
| 20 | tanken.wav | | 41 | toeter.wav | | 86 | (empty) |
| 21 | juich.wav | | 42 | getset.wav |
| 22 | start.wav | | 43 | go.wav |
| 23 | motor.wav | | 44 | vliegt.wav |
| 24 | toeren1.wav | | 45 | trein.wav |
| 25, 26 | carsoft.wav | | 46 | heli.wav |
| 27 | carmid.wav | | 47 | repair.wav |
| 28 | carmid2.wav | | 48-57 | aan11a..aanh22.wav |

The duplicates are deliberate -- eight copies of `tick.wav`, two of `paal.wav`,
two of `carsoft.wav` -- so the table is addressed by index and some indices are
meant to share a sound.

### Field 55 is the engine sound

- CONFIRMED: **field 55 is an index into this table**, and the names match the
  vehicles one for one:

  | f55 | sample | vehicles |
  |---|---|---|
  | 31 | `auto1.wav` | Trabant, Porsche, MPOLIZEI, ADAC, N_ASTRA, N_POLO |
  | 32 | `auto2.wav` | Astra, BMW, Mercedes, N_CALIBR |
  | 33 | `auto3.wav` | Golf, Diablo, N_GOLF |
  | 34 | `best_bus.wav` | **N_BESTEL** -- *bestelbus*, a delivery van |
  | 35 | `bus.wav` | **N_BUS** |
  | 36 | `vracht.wav` | **N_VRTAUT** -- *vrachtauto*, a lorry |

  The three large vehicles each get the sample named after them, and the twelve
  cars share three generic engine notes. Nothing else in the table's range
  31-36 could produce that correspondence.
- This closes the field map: **all 56 fields of the 0x484B18 table are now
  identified.**

### A second, independent confirmation of the turbo

Sample **84 is `turbo.wav`**, and 84 (`push 0x54`) is exactly the sample the
field-51 tick pushes at 0x425d47 while it forces field 51 to 6.0. So the sound
table confirms, from a completely separate direction, that field 51 is the
turbo's multiplier.

### What else the table gives away

Read as Dutch, the names answer several things cheaply. These are **LIKELY**
readings from the filenames, not traced to their call sites:

- **11 `flits.wav`** is the speed-camera flash -- the same *flits* the HUD and
  the trigger-1 camera use.
- **25-30 are collision impact sounds in three severities**, `carsoft`,
  `carmid`, `carhard`, two variants each. That is a graded impact response,
  which is worth noting against the standing finding that **no collision
  impulse has been found** in the physics: the game grades the *sound* by
  severity even if it does not grade the push.
- **12-15 `3git`/`2git`/`1git`/`gogit`** and **42/43 `getset`/`go`** are two
  separate countdown sets.
- **40 `psirene1.wav`** is the police siren, **41 `toeter.wav`** the horn.
- **20 `tanken.wav`** is refuelling and **47 `repair.wav`** the repair, which
  fits the trigger-2 service station, which plays sample 47 itself (session 10).
- **24 `toeren1.wav`** and **85 `toeren2.wav`** are rev sounds (*toeren* =
  revs), **39 `sliploop.wav`** a tyre-slip loop, **37/38 `rammel`** a rattle
  (*rammelen* = to rattle) and **82 `schuur2.wav`** a scrape (*schuren*).
- **44/45/46 `vliegt`/`trein`/`heli`** are aeroplane, train and helicopter
  ambience, and **0/1 `werk01`/`werk02`** roadworks (*werk* = work), matching
  the `a_wgwerk` roadworks texture.
- **48-63** are a block of `aan..` names and **64-76** of `achter..`, **77-81**
  of `opgev..`. *Achter* is "behind" and *opgeven* "to give up", so these look
  like spoken lines -- 30 of the 87 samples -- but which voice track they
  belong to is **not established**.

## The texture-blit table, and which value picks which traffic sign (session 10)

Question 42 left one thing open: *which* trigger value selects *which* sign.
The answer is that the traffic-sign triggers do not place a sign at all -- they
**blit an animation frame into a texture** -- and the trigger value is the frame
number.

### 0x42ae90 is `blit_anim_frame_to_texture(slot, frame)`

CONFIRMED from the function's own two asserts, both in **`txt_main.cpp`**:

    0x0042aeb1  "%s (%d) Index in array incorrect: %d"              (line 89)
    0x0042af66  "%s (%d) Incorrect animation frame while trying to
                 blit to texture!\n Texture index: %d, frame: %d"   (line 125)

- `arg1` is bounds-checked to **0 <= slot < 16** (0x42aeac).
- `arg2` is bounds-checked against a per-texture frame count read from
  `[0x4efc64 + 12*tex]` (0x42af39..0x42af4a), and the assert names it **frame**.

### The table: 16 records of 88 bytes at VA 0x48C968

`arg1` indexes it as `0x48C968 + 88*slot` (0x42aecc..0x42aed2, the
`5*n`/`11*n`/`shl 3` chain). The fields the function reads are:

| offset | meaning | read at |
|---|---|---|
| +0x00 | destination texture index, **lores** | 0x42af17 |
| +0x04 | destination texture index, **hires** | 0x42af1f |
| +0x08 | blit **x** | 0x42af25 |
| +0x0C | blit **y** | 0x42af2b |
| +0x10 | the source **PCX filename**, inline (not a pointer) | -- |
| +0x50 | a flag, 0 or 2 | -- |
| +0x54 | read when 0x4d9aac is zero | 0x42aed7 |

| slot | texture lo/hi | position | source | used for |
|---|---|---|---|---|
| 0-6 | 26 / 27 | 7 positions | RACER01/RACER02.PCX | -- |
| 7 | 24 / 25 | (190, 220) | a_TRFFC1.PCX | -- |
| **8** | **28 / 29** | **(9, 122)** | a_TRFFC1.PCX | **triggers 22-30, etappe 1** |
| **9** | **30 / 31** | **(9, 122)** | a_TRFFC1.PCX | **triggers 22-30, etappe 3** |
| **10** | **32 / 33** | **(9, 122)** | a_TRFFC1.PCX | **triggers 22-30, etappe 5** |
| **11** | **34 / 35** | **(9, 7)** | a_TRFFC1.PCX | **triggers 31-39, etappe 1** |
| **12** | **36 / 37** | **(9, 7)** | a_TRFFC1.PCX | **triggers 31-39, etappe 3** |
| **13** | **38 / 39** | **(9, 7)** | a_TRFFC1.PCX | **triggers 31-39, etappe 5** |
| 14 | 40 / 41 | (99, 62) | CARS01.PCX | -- |
| 15 | 42 / 42 | (197, 173) | RACER02.PCX | -- |

Slots 0-6, 14 and 15 are other dynamic textures (the RACER01/RACER02 and
CARS01 sheets); what blits into them is **not traced here**.

### So: the trigger value is the sign face

CONFIRMED. Each of the 18 `tr_vbord.cpp` handlers at `0x42a640 + n*0x70` is the
same shape -- a three-way switch on the etappe global **0x4d9a74**:

    0x0042a640  mov ecx, [0x4d9a74]        ; etappe
    0x0042a648  dec eax ; jz  -> slot 8    ; etappe 1
    0x0042a64b  sub eax,2 ; jz -> slot 9   ; etappe 3
    0x0042a650  sub eax,2 ; jz -> slot 10  ; etappe 5
              otherwise: "Trigger is only used for highway tracks!"
    each arm:  push <handler index> ; push <slot> ; call 0x42ae90

so `arg1` (the slot) comes from the **etappe** and `arg2` (the frame) is the
**handler's own index, 0-8**. Handlers 0-8 use slots 8/9/10 and handlers 9-17
use slots 11/12/13.

- **Trigger 22+k blits frame k** into the etappe's slot-8/9/10 texture at
  (9, 122); **trigger 31+k blits frame k** into the slot-11/12/13 texture at
  (9, 7).
- The data only ever uses 22-25 and 31-34, so **frames 0-3** -- four sign faces
  per gantry, with the two groups being the two carriageways, which is what the
  session-9 reading already inferred from the chunk-for-chunk duplication.
- Both groups blit into **a_TRFFC1.PCX**, the traffic texture the port already
  loads as `a_trffc1.tga`. So the gantry geometry is static and only its
  texture changes -- the sign "appears" by having its face stamped in.

### 0x4d9a70 is the lores flag

The lores/hires choice in each record comes from **0x4d9a70**, set by
**`set_game_display`** in `gm_init.cpp` (the assert at 0x423db3 gives the file,
line **568**) through a 6-entry jump table at **0x4243D4** on the RESOLUTION
value:

| RESOLUTION | 0x4d9a6c | 0x4d9a70 (lores) | 0x4d9aac |
|---|---|---|---|
| 0 | 0 | **1** | 0 |
| 1 | 1 | **1** | 0 |
| 2, 3 | 1 | 0 | 1 |
| 4 | *invalid* | | |
| 5 | 0 | 0 | 0 |

- CONFIRMED by the error text at 0x48aca4: **"(set_game_display) %s(%d) :
  Invalid resolution GAME supports only 320x200 OR 640x480"**. So RESOLUTION 0
  and 1 are the 320x200 modes and 2, 3 and 5 the 640x480 ones, with **4 not a
  valid value at all**.
- 0x4d9aac gates a coordinate-scaling block in the blit routine (0x42aee3,
  skipped when the flag is set), so it reads as "the blit coordinates are
  already in destination space". **0x4d9a6c is not identified.**

## The master trigger-handler table at 0x48C488 (session 10)

Chasing the sign handlers backwards found the table they live in, and it is the
**whole trigger dispatch table**: a flat array of function pointers **indexed
directly by the trigger value**, based at **VA 0x48C488**. The sign table found
in session 9 at 0x48C4E0 is simply this table's entry for trigger 22
(0x48C488 + 22*4), which is why it looked like a table of its own.

| trigger | handler | what it is |
|---|---|---|
| 0 | -- | null |
| **1** | 0x42a2e0 | **the speed camera** |
| **2** | 0x42a4d0 | **the service station** |
| 3 | 0x415990 | (shared with 48) |
| **4** | 0x42a340 | camera limit **50**, sign frame 0 |
| **5** | 0x42a370 | camera limit **70**, sign frame 1 |
| **6** | 0x42a3a0 | camera limit **100**, sign frame 2 |
| **7** | 0x42a3f0 | sets a flag, `0x4a70d4 = 1` -- **not a limit** |
| **8** | 0x42a3d0 | camera limit **120**, sign frame 3 |
| 9 | 0x42ae20 | `ZERO_CHUNK_MARKER` |
| 10-12 | -- | null |
| 13 | 0x42a420 | start line; clears `0x48c888` |
| 14 | 0x429950 | `FINISH_POSITION` |
| 15 | 0x429a10 | `FINISH_VALID` |
| **16** | 0x429ad0 | **not recorded before** -- see below |
| **17** | 0x429b20 | **not recorded before**; clears `0x4ea81c` |
| 18-21 | -- | **null** |
| 22-39 | 0x42a640 + n*0x70 | the 18 traffic-sign handlers |
| 40-44 | -- | **null** |
| **45** | 0x42a530 | **the checkpoint** |
| 46, 47 | -- | null |
| 48 | 0x415990 | (shared with 3) |

Three things this settles:

- **Trigger 1 is the speed camera, CONFIRMED** rather than inferred. Its handler
  plays **sample 11**, which the sound table names **`flits.wav`**
  (0x42a30d: `push 0x0b; call 0x4286a0`).
- **Trigger 45 is the checkpoint, CONFIRMED** by having a handler at all. It was
  previously read only from the data's correlation with the A_CHECK gantries,
  because `check_racer_trigger`'s case table does not cover it -- this is a
  different dispatcher.
- **The lane triggers 18-21 and 40-43 are null here.** They are the most
  numerous values in the track data (332 of them) and they have no handler in
  this table at all, which confirms the lane system reads them straight from
  the chunk slots rather than through the trigger dispatcher.

### The camera limit lives in 0x48C480

The four limit handlers each write the posted limit into **0x48C480** and blit
the matching sign face:

| trigger | `0x48C480` | sign frame | site |
|---|---|---|---|
| 4 | 50 | 0 | 0x42a345 / 0x42a361 |
| 5 | 70 | 1 | 0x42a375 / 0x42a391 |
| 6 | 100 | 2 | 0x42a3a5 / 0x42a3c1 |
| 8 | 120 | 3 | 0x42a3d2 / 0x42a3d0 |

and the camera at 0x42a2e0 compares the speed global **0x4d9e34** against
0x48C480, flashing only when it is the greater (0x42a2ff). The first three
handlers are gated on a per-etappe word at `0x4d8e9c + 137*etappe`; the 120 one
is not.

- This puts addresses on the **two separate limit systems** already recorded
  ("Two limit systems" above): the **posted/camera** set is 50/70/100/120 with
  no tolerance, read here; the **police** set is 60/80/120/2x-default with a
  x1.1 tolerance (corrected session 12), read by the cop AI. Nothing here changes that reading -- and
  note the camera set comes from triggers 4, 5, 6 and **8**, not 4-7.
- The slot-7 dispatcher at 0x42b1a2 bounds its frame to **0..4**, so there is a
  **fifth sign face no handler ever uses**.

### Trigger 2 is the service station, in full

- CONFIRMED. `0x42a4d0` checks the chunk's object type, then:

      0x0042a4f6  if ([0x4d9ec8])            ; the turbo upgrade is owned
      0x0042a501    set_turbo_tank(100)      ; 0x428060, the clamping setter
      0x0042a50b  call 0x4281c0(0)           ; reset the damage
      0x0042a515  call 0x4286a0(0x2f)        ; sample 47 = repair.wav
      0x0042a51d  [0x4882c0] = 48            ; banner 48 = "QUICK SERVICE"

  So the `TRIGGER_BENZINE_STATION` **refills the turbo tank, repairs the car,
  plays `repair.wav` and raises the QUICK SERVICE banner**. The eight of them in
  the track data are service stops.
- **This corrects an earlier reading.** QUICK SERVICE was listed among "the
  cheat labels" alongside INVINCIBLE and COP MODE, and hypotheses.md had all
  three as "player-selectable cheat/debug toggles". **QUICK SERVICE is not a
  cheat: it is what the service-station trigger shows.** (The banner table
  above already had the right address, 0x42a51d; only the interpretation was
  wrong.)
- It also sharpens the standing negative about fuel: the station exists and does
  something, but **what it refills is the turbo, not a fuel tank**. That is
  consistent with GETANKT, KOSTEN and GEEN BENZINE MEER never being fetched.

### Trigger 16 sets a blue haze

`0x429ad0`, gated to one car (`[0x4d9e28] == arg`), writes a block:

    0x4dc9b0 = 0        0x4dc9b4 = 0        0x4dc9b8 = 255
    0x4dc9bc = 3.5f     0x4dc9c0 = 2000.0f
    0x4dc9a8 = 1        0x4dc9ac = 0

- **LIKELY a fog or haze setting**: an (R, G, B) of **(0, 0, 255)** -- pure blue
  -- a near value of 3.5 and a far value of 2000.0, with an enable flag.
  Trigger 17 looks like the matching "off", clearing 0x4ea81c. Not confirmed:
  nothing has been traced that reads 0x4dc9a8.
- Neither 16 nor 17 appears in any shipped track file (they are absent from the
  trigger census), so this may be unused.

## Every texture-blit slot assigned (session 10)

With the callers of `0x42ae90` enumerated -- 81 of them, 72 being the sign
handlers' four stubs each -- all 16 slots of the 0x48C968 table are accounted
for:

| slot | destination | position | used by | CONFIRMED? |
|---|---|---|---|---|
| **0-6** | RACER01/RACER02.PCX | 7 places | **the seven racers' brake lights** | CONFIRMED |
| **7** | a_TRFFC1.PCX | (190, 220) | **the posted speed-limit sign face**, frames 0-4 | CONFIRMED |
| **8-13** | a_TRFFC1.PCX | (9,122) / (9,7) | **the two sign gantries x 3 etappes** | CONFIRMED |
| **14** | CARS01.PCX | (99, 62) | **the police car's flashing light** | CONFIRMED |
| **15** | RACER02.PCX | (197, 173) | a second flashing light, LIKELY the **ADAC** car's | LIKELY |

- **Slots 0-6 are the brake lights.** `0x42b100` is in **`txt_reml.cpp`**
  (*remlicht*), named by its own assert `"%s (%d) Illegal car! \nCar was: %d"`
  at 0x42b14e. It builds a local slot array {0, 1, 2, 3, ...} indexed by the car
  and blits frame `2*car` or `2*car + 1` (0x42b166/0x42b16b) -- **unlit and
  lit**. So each racer's brake light is stamped into its own texture sheet at
  its own position, which is why the table's first seven records point at
  RACER01/RACER02 with seven different coordinates.
  `0x42b0e0` in front of it is a lores/hires thunk: `if ([0x4d9a70]) jmp
  0x431bf0 else jmp 0x431ce0` -- two whole implementations.
- **Slots 14 and 15 are `GetTickCount` flashers**, identical in shape
  (0x40c860 and 0x40c4a0):on each interval they blit the stored frame and then
  toggle it (`xor ecx,ecx; test eax,eax; setz cl`), so each is a **two-frame
  alternation**.

  | slot | interval | enable | frame | gives |
  |---|---|---|---|---|
  | 14 | **200 ms** | 0x4a5d30 | 0x4a5d3c | the police light |
  | 15 | **150 ms** | 0x4a5cd0 | 0x4a5cd4 | LIKELY the ADAC light |

  Slot 14 is pinned to the police because the cop routine at 0x40c640 -- the one
  that logs `COP: object = %d. ai = %d. ON/OFF = %d.` and
  `SNDFX_POLITIE_SIRENE` -- blits slot 14 frame 0 itself (0x40c76c) to reset the
  light. Slot 15's identification rests only on `"Initing adac-car."` sitting
  just past its function, so it is LIKELY, not confirmed.

## The trigger dispatcher, and why 86/94/107/130/150/196 do nothing (session 10)

The master handler table at 0x48C488 is not read directly: the game **registers**
each of its entries with the engine, and the engine dispatches.

### Registration

`0x42a2b0` walks the table and registers every slot by its index:

    0x0042a2b4  mov esi, 0x48c488
    0x0042a2b9  mov eax, [esi]           ; the handler
    0x0042a2bc  push edi                 ; the trigger number
    0x0042a2bd  call 0x441fc0            ; rceng_set_trigger_handler
    0x0042a2c9  cmp esi, 0x48c54c        ; until the end of the table

- CONFIRMED: the table spans **0x48C488..0x48C54C = 0xC4 bytes = 49 entries**,
  so the game registers handlers for trigger values **0 through 48** and nothing
  else. Null entries are registered as null.

### The engine's handler array

- **`0x441fc0` is `rceng_set_trigger_handler(index, fn)`** and **`0x441ff0` is
  the matching getter**, named by their own messages, `" trying to set an
  illegal trigger handler: %d"` (0x4994dc) and `" trying to get an illegal
  trigger handler: %d"` (0x49950c).
- Both bound the index to **0 <= index <= 256** and store into
  **`0x4E5928 + index*4`**, so the array has **257 slots**.
- CONFIRMED: the array is **zeroed at startup** -- 0x441e01 is the
  `mov ecx, 0x100; xor eax, eax; mov edi, 0x4e5928` clear.

### The dispatcher

At 0x442607, per chunk, for each of its **two** trigger words:

    0x00442624  mov eax, [edx+ecx*4+0xec]    ; trigger slot 0
    0x0044262b  mov eax, [eax*4+0x4e5928]    ; look it up -- NO bounds check
    0x00442632  test eax, eax
    0x00442634  jz   0x44263c                ; nothing registered: skip
    0x00442636  push ebp ; call eax          ; ebp = the car index
    ... then the same again for +0xf0, trigger slot 1

- CONFIRMED: the handler takes **one argument, the car index**, which is what
  every handler read so far expects (`[esp+0x04]`).
- CONFIRMED: the two trigger words live at **+0xEC and +0xF0** of the engine's
  runtime chunk record, whose stride is **388 bytes** (the `*3`, `shl 5`, `+1`
  chain gives 97, times 4), in the array at `[0x4e65e0]`, reached through a
  visible-chunk indirection at `[0x4e5d2c]`. That independently confirms the
  **two trigger slots per chunk** the `.pmp` reading is built on.
- The dispatcher does **not** bounds-check the index, so a value of 257 or more
  would read past the array.

### So the stray values are inert

- CONFIRMED: **86, 94, 107, 130, 150 and 196 do nothing at all.** Every one is
  <= 196, so it indexes **inside** the 257-slot array; no handler is ever
  registered above 48; the slots are zero from startup; and the dispatcher skips
  a zero. They are dead values in the track files -- editor leftovers -- not an
  undiscovered feature.
- The same holds for the values **inside** 0..48 whose table slot is null:
  **0, 10, 11, 12, 18-21, 40-44, 46 and 47**. For 18-21 and 40-43, the lane
  triggers, that is the point: with 332 occurrences in the track data they are
  by far the most common values, and they are consumed by the lane system
  reading the chunk slots directly rather than through this dispatcher.
- No track value reaches 257, so the missing bounds check is never exercised.

## The flitspaal threshold is below the painted sign (session 10)

The posted-limit triggers and the painted sign objects line up chunk for chunk
across all three highway stages, and they **disagree on the number**.

### The limit is km/h, CONFIRMED

The camera handler compares two integers:

    0x0042a2ea  mov edx, [0x4d9e34]    ; the speed
    0x0042a2f1  mov esi, [0x48c480]    ; the posted limit
    0x0042a2ff  cmp edx, esi
    0x0042a307  jle 0x42a330           ; speed <= limit: no flash

and 0x4d9e34 is built at 0x425eb1 as `(int)(record[+0x74] * 3.6)`, with
**0x47f7d4 = 3.6** -- so it is the speed in **km/h as an integer**, and the
limit is in km/h too. 0x48C480 is written in exactly four places, the four
handlers, and its static initial value is **-1**.

### The triggers sit on the signs, one for one

Reading the posted limit at each trigger chunk against the `A_MAX*`/`A_MX*` sign
objects:

| stage | trigger chunk / limit | nearest sign object | the sign says |
|---|---|---|---|
| etappe1 | c436 = **50** | c433 `A_MAX50S_ANIM` | 50 |
| etappe1 | c631 = **50** | c626 `A_MAX50S_ANIM` | 50 |
| etappe1 | c245 = **70** | c245 `A_MAX80S_ANIM` | **80** |
| etappe1 | c139 = **100** | c139 `A_MX120S_ANIM` | **120** |
| etappe1 | c257 = **100** | c254 `A_MX120S_ANIM` | **120** |
| etappe3 | c320, c413 = **50** | c319, c412 `A_MAX50S_ANIM` | 50 |
| etappe3 | c331, c428 = **100** | c331 `A_MX120S_ANIM` | **120** |
| etappe5 | c94, c231, c293, c480, c533 = **70** | c92, c230, c293, c479, c533 `A_MAX80S_ANIM` | **80** |
| etappe5 | c148, c260, c336, c523, c568 = **100** | c148, c259, c336, c523, c567 `A_MX120S_ANIM` | **120** |

- CONFIRMED by the correlation being exact: every trigger-5 chunk has an
  `A_MAX80S` within two chunks and every trigger-6 chunk an `A_MX120S`, on all
  three stages, with no exceptions and no unmatched pairs apart from
  etappe5's c1 and c808 (the stage's first and last chunk, no sign) and a few
  signs with no trigger.
- So the mapping is **trigger 4 -> a 50 sign, trigger 5 -> an 80 sign, trigger 6
  -> a 120 sign**, while the limits the code posts are **50, 70 and 100**.
- **The camera is therefore stricter than the sign** in the 80 and 120 zones:
  it flashes above 70 where the sign reads 80, and above 100 where the sign
  reads 120. Only the 50 zone matches. The comparison direction has been checked
  twice (`cmp edx, esi; jle` skips when the speed is the lower), so this is not
  a sign error in the reading.
- That also reconciles the two limit systems: the **police** set already
  recorded -- 60/80/120 -- is the *painted* family, while these are the
  camera's own numbers. The 10-to-20 km/h gap between them is the game's, not
  a misreading of either.

### Which limit triggers the shipped tracks actually use

A full trigger census of all seven stages (now printed by `a2_dump track`):

| stage | limit triggers | cameras (trigger 1) | inert strays |
|---|---|---|---|
| etappe0 | **none** | 6 | 86 |
| etappe1 | 4 x2, 5 x1, 6 x4 | 5 | -- |
| etappe2 | 4 x2 | 5 | 94, 107, 150, 196 |
| etappe3 | 4 x2, 5 x1, 6 x2 | 5 | -- |
| etappe4 | 4 x1 | 5 | 130 |
| etappe5 | 5 x5, 6 x7 | 5 | -- |
| etappe6 | **none** | 6 | 86 |

- CONFIRMED: **trigger 8 (the 120 limit) and trigger 7 occur in no track file.**
  An earlier note said "7 is unused by the shipped tracks and 8 is
  ai_cops.cpp's illegal case"; on the dispatcher side it is **8** that carries
  the 120 limit and **both 7 and 8** are unused by the data.
- CONFIRMED: **etappe0 and etappe6 post no limit at all**, yet both carry six
  cameras. With 0x48C480 statically **-1**, the speed gate `speed > -1` passes
  for any speed, so on those two stages the first gate never stops a flash. The
  only thing that can is the handler's second gate, `record[+0x1c] <
  record[+0x18]` (0x42a309). **Both fields are now identified** -- previous and
  current chunk -- so the gate is a direction check, and the answer is that
  **the city cameras flash everyone who passes them going forwards**. See "The
  vehicle record's +0x18 and +0x1c are current and previous chunk".

## The police do most of the talking: 34 of the 87 samples (session 10)

Enumerating every call site of the sample player **0x4286a0** -- there are
**16** -- identifies the whole `aan..`/`achter..`/`opgev..` block that the sound
table left open, and it all belongs to the police.

### The module is `ev_cops.cpp`, CONFIRMED

Two asserts name it: **"Illegal value for g_times_caught: %d"** at 0x41539d
(`ev_cops.cpp` line **241**) and **"Illegal mode for cop sequence! Mode was:%d"**
at 0x415466 (line **379**), both with the file string at 0x486714.

### `g_times_caught` picks the voice group

The warning routine at 0x415300 switches on **`g_times_caught` (0x4A70E8)** twice
-- once for the banner, once for the voice:

| catch | banner | voice table | entries | picked with |
|---|---|---|---|---|
| 1 | **49** EERSTE WAARSCHUWING (0x41535e) | 0x486648 | 6 | `rand() % 6` (0x4153dc) |
| 2 | **50** LAATSTE WAARSCHUWING (0x415352) | 0x486660 | 5 | `rand() % 5` (0x4153c6) |
| 3 | -- (the arrest arm) | 0x486678 | 5 | `rand() % 5` (0x4153b0) |

and anything else falls into the `ev_cops.cpp:241` assert. The tables hold:

    0x486648  48 aan11a  49 aan14a  50 aanh11  51 aanh12  52 aanh13  53 aanh14
    0x486660  54 aan21a  55 aan22a  56 aanh21  57 aanh22  58 aanh23   (0 pad)
    0x486678  59 aan31a  60 aan31b  61 aan32a  62 aanh33  63 aanh34   (0 pad)

- CONFIRMED, and this is the nice part: **the first digit in each filename is the
  catch number.** The `aan1*`/`aanh1*` files are all and only in the catch-1
  table, `aan2*`/`aanh2*` in catch 2, `aan3*`/`aanh3*` in catch 3. That comes out
  of the table layout, not out of reading the names.
- The two tables of five are zero-padded to six, so the block is a 3 x 6 array
  with two holes.

### Two more pools, from the same module

| site | table | entries | samples | pick |
|---|---|---|---|---|
| 0x415147 | 0x486690 | 13 | **64-76 `achter1..achter19`** | `rand() % 13` |
| 0x4151c7 | 0x4866c8 | 5 | **77-81 `opgev1..opgev9`** | `rand() % 5` |

Both are gated the same way: the shared channel handle **0x4866DC** is checked
against `0x467050` (is-playing) and the line is only started if nothing is
already speaking (0x415120..0x415130).

- (Session 12: **when** they play is now CONFIRMED. The `achter` pool,
  0x4150f0, is called at 0x403584 just before the cop's light goes **on**,
  i.e. as a pursuit starts. The `opgev` pool, 0x415170, is called at 0x401e88
  and 0x401ed4 right after it goes **off**, i.e. when the cop **gives up**, so
  it is not a demand to surrender.)
- LIKELY, from the Dutch: *achtervolging* is a **pursuit** and *opgeven* is **to
  give up**, so these are pursuit chatter and a demand to surrender, while
  *aanhouden* ("to pull over", "to detain") fits the `aanh` family. The module
  and the catch-number mapping are CONFIRMED; the sense of each pool rests on
  the filenames.
- Counting it up: **48-63, 64-76 and 77-81 are 34 of the 87 samples**, so about
  **39% of the game's audio is the police speaking** -- by far the largest block
  in the table, and all of it reached through these five random pools.

### The warning sequence, exactly

- 0x415300 sets the banner, writes **`0x4a70d8 = 0`** (the cop-sequence
  sub-mode, the one the line-379 assert guards) and plays the voice line.
- **0x415420 is the gate.** `if (game_mode != 8) return 1;` then
  `if (g_times_caught >= 3) { 0x4867a4 = 2; set_game_mode(6); return 1; }` --
  the arrest -- otherwise it returns whether
  **`GetTickCount() - 0x4a70e4 >= 4000`**, so a warning holds the sequence for
  **4 seconds**. That independently confirms the 4.0 s message timeout the port
  had already settled on.
- **0x415490 ends it:** `change_active_camera([0x4a70e0])`, then
  `0x414b70([0x4a70dc])`, then `set_game_mode(0)`. So the chase sequence
  **saves and restores the camera**, which is why it needs a game mode of its
  own rather than running inside the race.
- **0x4142b0 is `change_active_camera(index)`**, named by its own
  "(change_active_camera) illegal camera index(%d) max. (%d)" (0x486594), with
  the index bounded to 0..10 and a 9-arm jump table at 0x41448C. **0x414b70**
  takes the same 0..10 index through a table at 0x41507C and stores it in
  0x486640, building a six-dword block with a 10.0f in it -- LIKELY the matching
  camera-parameter setter.
- These are **not** `RESET_COP` and `cop_lane` (read in session 12).

## Impact sounds are graded by magnitude (session 10)

`0x427cf0` is the collision audio, and it splits two ways.

**Scraping** is one looping sample: if the channel handle **0x48B468** is -1, or
`0x467050` says the sound has finished, it (re)starts sample **82
`schuur2.wav`** (*schuren*, to scrape) and keeps the new handle (0x427d07,
0x427d31).

**Impacts** are graded on the magnitude in `esi`, each band picking at random
between two samples with `abs(rand()) & 1`:

| magnitude | table | samples |
|---|---|---|
| < **18000** | 0x48B470 | 25 `carsoft.wav`, 26 `carsoft.wav` |
| < **25000** | 0x48B478 | 27 `carmid.wav`, 28 `carmid2.wav` |
| otherwise | 0x48B480 | 29 `carhard.wav`, 30 `carhard2.wav` |

- CONFIRMED from the two literal thresholds (0x427d4a, 0x427d7d) and the three
  table bases (0x427d63, 0x427d96, 0x427dc1).
- The soft band's two entries are **both** `carsoft.wav` -- 25 and 26 are
  duplicates in the sample table -- so only the mid and hard bands really have
  two variants. The "random of two" is uniform across all three bands anyway.
- **18000 and 25000 are the same thresholds the damage model uses**, so one
  impact magnitude drives both how much damage is taken and which of the three
  crunches is played.
- Worth keeping beside the standing negative that **no collision impulse has
  been found**: the game grades the *sound* of an impact in three bands while
  apparently not grading the *push*.

### The full sample-play census

All 16 call sites of 0x4286a0:

| sample | site(s) | what |
|---|---|---|
| 10 `tick.wav` | 0x424bd3 | the countdown tick -- (session 12) of the **checkpoint clock**, game timer 11, once per second while under 6 s left |
| 11 `flits.wav` | 0x42a30f | the speed camera |
| 21 `juich.wav` | 0x4155ba | (not identified) |
| 41 `toeter.wav` | 0x424ef0 | the horn |
| 47 `repair.wav` | 0x42a515 | the trigger-2 service station |
| 82 `schuur2.wav` | 0x427d09, 0x427d33 | the scrape loop |
| 83 `checkp.wav` | 0x42a5a8 | the trigger-45 checkpoint |
| 84 `turbo.wav` | 0x425d49 | the field-51 turbo tick |
| 25-30 | 0x427d6b, 0x427d9e, 0x427dc9 | the three impact bands |
| 48-63 | 0x4153f1 | the catch-N warning voice |
| 64-76 | 0x415147 | the `achter` pool |
| 77-81 | 0x4151c7 | the `opgev` pool |
| (computed) | 0x416c82 | indexes 0x4869e2 on an 18-byte stride; not identified |

## The complete game-mode machine (session 10)

`fcn.00424d20` is `set_game_mode(n)`: it stores `n` in **0x4D9A7C** and jumps
through a 9-entry table at **0x424E00** (bounded to `n <= 8`). Each arm either
sets a couple of flags or registers a **sequence** with
`0x416760(steps, begin, tick, end)` -- a step count and three callbacks. Reading
all nine arms, and the module each callback set belongs to:

| mode | arm | registers | module | what it is |
|---|---|---|---|---|
| **0** | 0x424d68 | -- (sets 0x4d9a9c = 1, 0x4d9e58 = 1) | -- | **racing** |
| **1** | 0x424d39 | `(2, 0x416ce0, 0x416e00, 0x416fe0)` | **`ev_stcam.cpp`** | the **start camera** fly-in |
| **2** | 0x424d5d | -- (only clears 0x4d9aa0) | -- | a bare mode, no sequence |
| **3** | 0x424d82 | `(4, 0x4154d0, 0x4155d0, 0x415690)` | (0x415xxx) | a camera sequence that plays **21 `juich.wav`** |
| **4** | 0x424da6 | -- (zeroes 0x4d9a9c, 0x4d9e58, 0x4d9a78) | -- | **teardown** |
| **5** | 0x424dff | -- | -- | **nothing: a bare `ret`** |
| **6** | 0x424db8 | `(12, 0x416060, 0x4162e0, 0x416440)` | **`ev_g_ovr.cpp`** | **game over** |
| **7** | 0x424dff | -- | -- | **nothing: a bare `ret`** |
| **8** | 0x424ddc | `(11, 0x415300, 0x415420, 0x415490)` | **`ev_cops.cpp`** | the **police chase** |

- CONFIRMED by the modules' own asserts: mode 1's callbacks hold
  `"Illegal mode start camera!\nMode was:%d"` from **`ev_stcam.cpp`** (0x416e28),
  and mode 6's hold **`"No game over mode set!"`** and
  **`"Illegal game over mode set: %d!"`** from **`ev_g_ovr.cpp`**
  (0x416091, 0x4161b1).
- So **mode 6 is not "the arrest" as such -- it is the game-over sequence**, with
  a sub-mode of its own. The arrest reaches it by writing **0x4867A4 = 2** and
  then calling `set_game_mode(6)` (0x41543a), so 2 is the arrest's game-over
  sub-mode. That sub-mode selector is what picks between language.ini 40
  TOTAL LOSS, 41 OUT OF TIME, 42 GEARRESTEERD and 43 RACE OVER.
- **Modes 5 and 7 do nothing at all** -- both jump-table entries point at the
  same bare `ret` at 0x424dff. They are holes in the enum.
- **Mode 3** is LIKELY the finish or victory sequence: its begin callback builds
  a `(0, 20.0, 0)` offset and repositions a camera, and plays sample **21
  `juich.wav`** (*juichen*, to cheer) at 0x4155ba. No module string falls in its
  range, so the identification is from the sample and the camera work, not from
  a name.
- This answers question 18's "the other sequences (modes 1 and 3) are
  unidentified" for mode 1 outright and for mode 3 as far as LIKELY, and it
  closes the enum: 0 race, 1 start camera, 2 bare, 3 (victory), 4 teardown,
  5 unused, 6 game over, 7 unused, 8 police chase.

## The vehicle record's +0x18 is a chunk index (session 10)

The speed camera's second gate was left open: it skips unless
`record[+0x1c] < record[+0x18]` in the 160-byte live-vehicle record (0x42a309).

- CONFIRMED that **`record[+0x18]` is a chunk index.** Mode 3's begin callback
  uses it as exactly that: at 0x415549 it loads
  `[0x4e48e0 + 160*[0x4d9e28]] + 0x18` into `ecx` and immediately indexes the
  chunk table with it, `[[0x4e5d2c] + ecx*4 + 0x0c]` -- the same `0x4e5d2c`
  indirection the trigger dispatcher uses to turn a chunk index into a record.
- **0x4D9E28 is the player's car index**: it is the record index here, and
  trigger 16's handler gates on `[0x4d9e28] == <the car argument>` (0x429ad9).
- So the camera's gate compares **two chunk positions**, not a counter against a
  maximum. **Resolved later in session 10** -- see "The vehicle record's +0x18
  and +0x1c are current and previous chunk": `+0x1c` is the **previous** chunk,
  written by the engine at fcn.00442020, so the gate is a **direction check**
  (fire only while advancing forwards) and a once-per-crossing guard. The guess
  that it held "the chunk where the camera last fired" was wrong.
- This matters for the open question of whether the city cameras fire at all.
  With 0x48C480 statically -1 and no limit trigger on etappe0 or etappe6, the
  speed gate always passes there, so the answer hangs entirely on this second
  gate -- and it is now known to be about position rather than about a budget.

## The camera view presets, and a correction (session 10)

The view setter **fcn.00414B70** dispatches through an **11-entry** table at
**0x41507C**, and each arm builds a three-float offset and hands it to
**0x447770** on the engine camera record. Pulling the inline float constants out
of all eleven arms:

| preset | offset constants | activates slot | reading |
|---|---|---|---|
| **0** | 25.0, **-100.0** | 0 | **far chase** -- 100 behind, 25 up |
| **1** | 25.0, **-60.0** | -- | **near chase** -- 60 behind, 25 up |
| 2 | 38.0 | -- | one constant only |
| **3** | **15.5, -1.0** | 0 | **cockpit / bonnet** -- 1 behind, 15.5 up |
| 4 | 25.0, 15.0, -50.0 | 0 | |
| 5 | 23.0, 40.0 | -- | |
| 6 | -30.0, 15.0, 60.0 | -- | ahead of the car (z positive) |
| 7 | 25.0, 15.0, -50.0 | -- | same offsets as preset 4 |
| 8 | -20.0, 9.0 | -- | |
| 9 | -25.0, 55.0, -30.0 | 0 | high and off to one side |
| 10 | 45.0, -55.0 | 0 | |

- The two-constant arms are `(x_from_caller, y, z)`: the caller's argument
  supplies X and the arm supplies Y and Z. Preset 0 is the one the port already
  had confirmed, and it matches.
- Presets **0, 3, 4, 9 and 10** end by calling
  **`change_active_camera(0)`** -- all five select the **same camera slot 0**.
  The others reposition whatever camera is active without switching.

### The correction: a view preset is not a camera slot

- Session 8 recorded "**topview = slot 3**" from `init_topview_camera(3)`, and
  then read the key-2 binding as "view 3 (the topview camera)". **Those are two
  different index spaces.** `init_topview_camera(3)` assigns a **camera slot**;
  fcn.00414B70's argument is a **view preset**. Preset 3 builds an offset of
  1 unit behind and 15.5 up and then activates **slot 0**, so it is a
  near-the-car view and has nothing to do with the topview camera.
- So the player's four views are:

  | key | preset | offset |
  |---|---|---|
  | `1` | 1 | near chase, 60 behind / 25 up |
  | `2` | **3** | **cockpit, 1 behind / 15.5 up** |
  | `3` | 5 | the toggle (stores the previous view in 0x4863CC) |
  | `4` | 0 | far chase, 100 behind / 25 up |

  The "slot 3 is the topview" fact still stands on its own; what is withdrawn is
  the inference that **key 2 shows it**.
- The port had implemented key 2 as a topview with an invented height of 600 and
  a -89 degree pitch, explicitly flagged as not recovered. It is now the
  cockpit view off the game's own numbers.

### The camera slot bookkeeping

`change_active_camera` (fcn.004142B0) has a **9-arm** table at 0x41448C, and
every arm has the same shape: take the camera's target car from **0x486644**,
index the 160-byte vehicle record, then

- write the slot number into **0x4A70C8**, the **active camera**;
- write the car's chunk -- `record[+0x18]`, the field identified above -- into
  the shared **0x4A70CC** and into a per-slot slot at **0x4A70A0 + slot*4**.

- CONFIRMED: **the arm index equals the slot number it writes** for every valid
  index, and **index 2 shares the out-of-range arm at 0x414480**, which resets
  the active camera to 0. So slot 2 is a hole, exactly as game modes 5 and 7 are.
- A second array, **0x4A7078 + slot*4**, holds each slot's **engine camera
  object id**, which indexes the engine's **352-byte** camera records at
  `[0x4E66A0]` (the `*11`, `shl 5` chain). The camera update at 0x414611 reads
  the record's position from `+0x08`, passes it to **0x445a10** -- a
  world-position-to-chunk lookup -- and stores the result back into both the
  shared and the per-slot chunk slots.
- Camera preset 3's arm also writes **`camera_record[+0x7c] = 1`** (0x414e17),
  the only arm that does. Not identified; note question 43 asks about
  `c_3dobject+0x7c`, but that is a different structure from this 352-byte
  camera record, so this is not an answer to it.

## The `.pth` camera paths are not cameras: they are the helicopter, the aeroplane and the police (session 10)

**Session 12 correction: the type table below is one dword off, and so are the
type identities and the usage table.** Read the table from **0x4966F0**: the
helicopter is type **5**, the aeroplane **7**, the police car **2**, plus
**3** a boat, **4** the train and **6** a random traffic vehicle. See "`.pth`
f3 is the lap, and the path types re-read".

`pth_main.cpp` is the `.pth` subsystem, and its own messages give up most of the
format's remaining unknowns.

### The loader and the runtime shape

- The file is opened as `etappe%d.pth` (0x433721) in mode `"r+b"`, with a
  **version check** -- `"Incorrect version path file.\nversion: %d track: %d"`
  (0x433782). The port reads the version as -1 in every shipped track, which is
  therefore the value the check accepts.
- CONFIRMED: the **path type is bounded `0 <= type < 8`**
  (0x4341c6, `pth_main.cpp` line **440**, `"Unknown path type! \nPath: %d,
  path type: %d"`).
- The runtime path array is at **0x4E0718** with a stride of **828 bytes** (the
  `*3`, `shl 3`, `-1`, `*9`, `*4` chain at 0x4341b0 gives 207*4), and the count
  is in **0x4E47C0** (`"Path does not exist! \nPath: %d, paths available: %d"`,
  line 432).
- Each type has a **three-function entry in a table at 0x4966F4**, 12 bytes per
  type:

  | type | fn A | fn B | fn C |
  |---|---|---|---|
  | 0 | 0x434140 | -- | 0x432a10 |
  | **1** | 0x432a80 | 0x432a70 | **0x432bd0** |
  | 2 | 0x432db0 | 0x432d80 | 0x434800 |
  | 3 | 0x4348f0 | 0x4348d0 | 0x434a70 |
  | **4** | 0x434b60 | 0x434640 | **0x433170** |
  | 5 | 0x4333b0 | 0x433380 | 0x434cf0 |
  | **6** | 0x434df0 | 0x434640 | **0x434550** |
  | 7 | 0x434670 | 0x434640 | *-1* |

  Type 0 has no fn B and type 7 no fn C; types 4, 6 and 7 share one fn B.

### Three types are named, from the meshes they load

- **Type 4 is the helicopter.** Its fn C is in **`pth_heli.cpp`** and loads
  **`C_HELI_ANIM`**, **`C_LBLADE_ANIM`** and **`C_SBLADE_ANIM`** (0x4331d0,
  0x433289, 0x433303) -- body, large rotor, small rotor. It asserts
  **"Helicopter paths only available in city tracks!"** (0x4331c3).
- **Type 6 is the aeroplane.** Its fn C loads **`A_VLIEGT_ANIM`** at 0x434553
  (*vliegtuig*), and the `A_` prefix marks it a highway-stage mesh.
- **Type 1 is a police-car path.** Its fn C loads **`MPOLIZEI_ANIM`** at
  0x432bd5.
- Those are the only four mesh names the whole path region loads, so types
  0, 2, 3, 5 and 7 animate something they fetch another way. **Not identified.**

### `.pth` field 4 is a time

- CONFIRMED by the message **"Time not set for path %d"** (0x496980), which
  `pth_main.cpp` raises when the field is absent. The authored values run
  **5000 to 50000**, so they are **5 to 50 seconds in milliseconds**.
- `.pth` field 3 (0, 1 or 2) is still **unidentified**.

### Which types the shipped tracks actually use

| type | stages | kind | what |
|---|---|---|---|
| 3 | etappe0, etappe6 | city | unnamed |
| **4** | etappe2, etappe4 | **city** | **the helicopter** |
| 5 | etappe0, etappe2, etappe6 | city | unnamed |
| **6** | etappe1, etappe3 | **highway** | **the aeroplane** |
| 7 | etappe1 | highway | unnamed |

- The split is exact: types 3, 4 and 5 appear only on the city stages (0, 2, 4,
  6) and types 6 and 7 only on the highway ones (1, 3, 5). That **independently
  confirms** the helicopter assert -- type 4 occurs on etappe2 and etappe4, both
  city -- and fits the aeroplane's `A_` mesh prefix.
- CONFIRMED: **types 0, 1 and 2 appear in no `.pth` file**, so the police-car
  path is registered but never authored. Worth noting beside the parked
  `MPOLIZEI_ANIM` scenery objects: those are static placements, not path-driven.
- **etappe0 and etappe6 have byte-identical `.pth` files** (664 bytes, the same
  four paths with the same chunk ranges and times), which is the same reuse
  already established for etappe6's anim set.
- **etappe5 has no paths at all** -- an 8-byte file, header only. So the
  Rotterdam-Amsterdam stage has neither helicopter nor aeroplane.
- Sample **45 `trein.wav`** has no counterpart here: no train mesh is loaded in
  the path code, so the train is not path-driven on this evidence.
  (**Wrong, session 12**: type 4's start function loads `C_TREIN_ANIM`.)

## What Game.ini's own comments settle (session 10)

`Game.ini`'s `[GFX OPTIONS]` is commented by the developers, and three of those
comments answer things reached from the other direction.

- **`object_lights` -- "Enables/disables alpha blended object lights. This
  module is currently only used for brakes."** That is an independent
  confirmation of the `txt_reml.cpp` reading above: the only object-light in the
  shipped game is the **brake light**, which is exactly what the blit slots 0-6
  stamp into the seven racers' texture sheets. Two unrelated sources, the same
  answer.
- **`particles` -- "Enables/Disables entire particle module. Currently the
  module isn't used, therefore default=0."** So `gfx_sprk` (sparks) and the
  particle system are **compiled in but switched off and unused**. A useful
  negative: there is nothing to recover there. `part_rain` is a separate key and
  defaults to 1.
- **`alpha_blending`** is a master switch, and its comment enumerates exactly
  what depends on it: **car shadows, clouds and lights** (`gfx_shdw`,
  `gfx_wolk`, `object_lights`).
- **`gamma` -- "0=light, 1 = dark"**, so it is a two-value switch rather than a
  scale, and the shipped default is **1**. The port applies a gamma of 2.0 to
  the sprite palette, which is the same direction.
- `overlay_detail` is documented as **0-3**, "full detail: radar, all overlay
  graphics", with `disable_overlay` locking the player out of changing it.
- `finish_flag` and `skidmarks`/`fade_skmarks` are plain on/off switches; the
  skidmark pair chooses between fading out and vanishing when a timer expires.

`[PLAYER]` adds two small things:

- **`tracktime` is in milliseconds** ("Finish time of player in msecs saved by
  the game"), which is the unit the finish time is stored in.
- **`copland` is commented "??" by the developers themselves.** The game's own
  configuration file does not know what it does. It is the ini key behind
  language.ini 47 COPLAND. ~~It remains a SPECULATIVE cheat toggle.~~ It is
  answered in "COP MODE" (session 15).

## Skidmarks: the condition is a 0-10 skid intensity, and the threshold is 4 (session 10)

`gfx_skid.cpp` occupies 0x41D700..0x41E700 and has exactly **four** entry points
from outside, which makes the subsystem easy to read off:

| entry | caller | what |
|---|---|---|
| 0x41d810 | 0x424186 | (setup) |
| **0x41e130** | 0x4242af | **`init_skidmarks()`** -- the debug print sits immediately before the call |
| **0x41dd00** | 0x424b0b | **lay/update, once per frame** |
| 0x41e3a0 | 0x425201 | the **draw**, in the per-frame render chain beside the other gfx modules |

### The per-frame call carries a skid intensity

    0x00424afd  mov ecx, [0x4a63dc]      ; the skid intensity
    0x00424b03  mov edx, [0x4d9e28]      ; the player's car index
    0x00424b0b  call 0x41dd00

and inside, after the `skidmarks` ini flag at **0x4D9AE8**:

    0x0041dd17  mov ebx, [esp+0xd8]      ; the intensity
    0x0041dd1e  cmp ebx, 0x04
    0x0041dd21  jl  0x41e080             ; below 4: lay nothing

- CONFIRMED: **a skidmark is laid only when the intensity is 4 or more.**
- A second gate compares **0x4CC014 against 4** (0x41dd27) before walking a
  table at **0x4AE3D4** -- a cap on how many marks are live, not traced further.

### Where the intensity comes from

**0x4A63DC** is written from seven places, all in the tyre and force code, and
the scale is **0 to 10** -- fixed by the clamp at 0x40fb48:

    0x0040fb3b  fld  dword [0x4a6488]
    0x0040fb41  fadd st0, st0            ; x2
    0x0040fb43  call 0x4700e0            ; ftol
    0x0040fb48  cmp  eax, 0x0a
    0x0040fb4b  mov  dword [0x4a63dc], eax
    0x0040fb52  if (> 10) mov dword [0x4a63dc], 0x0a

| site | value | condition |
|---|---|---|
| **0x40dc11** | `min(ratio, 1.0) * 6.0` | the **drive-force traction clamp** -- the ratio by which the force overshot `0.8 * f52 * load`, capped at 1.0 (0x40dbf1) and scaled by the 6.0 at 0x47f2c8 |
| 0x40e511, 0x40e9e5, 0x40fb4b, 0x40fdda | computed | in the Pacejka and force code |
| 0x40e9c2, 0x40f815 | **10** | set outright |
| 0x40f830 | **5** | when the car's speed field `[+0x74]` is under **10.0** (0x47f2b4) and a second test holds |
| 0x40e51b, 0x40e728, 0x40e7ab, 0x40f095 | **0** | cleared |

- So the **traction clamp is the main source**: because it scales by 6, the
  overshoot ratio has to pass **2/3** before the intensity reaches the
  threshold of 4. The clamp itself is the one already identified from field 52,
  so the skidmark condition and the traction limit are the same mechanism read
  at two points.
- Two sites write a flat 10, and one writes 5 as a **low-speed** case (under
  10.0 in the car's own speed units).
- The port has field 52's traction cap recorded but unused -- its longitudinal
  model is single-body -- so laying skidmarks faithfully would need that clamp
  first. The threshold, the scale and the 6.0 are all recoverable numbers.

## The damage frames have no thresholds: it is a linear map (session 10)

Question 17 left "the level thresholds that step a zone from one damage frame to
the next" open. The answer is that **there are none.**

The regional-damage applier is at 0x4275f5, announced by its own trace
**"Applying regional damage to object %d, region %d, level %d."** (0x48b544),
and for the player's car (`object == [0x4d9e28]`) it does exactly this:

    0x00427617  fld   dword [esp+0x44]            ; the impact for this hit
    0x0042761b  fadd  dword [esi*4+0x4d9e74]      ; += DAMAGE_VALUE[zone]
    0x0042762b  fstp  dword [esi*4+0x4d9e74]
    0x00427632  call  0x43fc20                    ; the model's damage-frame count
    0x0042763b  fild  dword [esp+0x40]
    0x0042763f  fmul  dword [esi*4+0x4d9e74]      ; count * value
    0x00427646  call  0x4700e0                    ; truncate to int
    0x00427657  call  0x43fc40                    ; apply frame to the mesh
    0x0042765c  mov   dword [esi*4+0x4d9e94], edi ; DAMAGE_MESH[zone] = frame

- CONFIRMED: **`frame = (int)(frame_count * DAMAGE_VALUE[zone])`**. So the
  accumulated per-zone value is a **0..1 fraction**, and it indexes linearly
  into however many damage frames that zone's mesh happens to have. A zone steps
  to its next frame whenever the fraction crosses `1/frame_count` -- there is no
  authored ladder anywhere.
- `0x43fc20` returns the count: it reads the model id from `record[+0x98]`,
  looks it up in the table at `[0x4E6718]` and returns that descriptor's `+0x04`.
  So the number of damage steps is **per model**, not global.
- `0x43fc40(record, zone, frame)` applies it; its guards print
  "Checking object type..." and bail on -1 ids.

### Where the two arrays live

- **`DAMAGE_VALUE` is 8 floats at 0x4D9E74** and **`DAMAGE_MESH` is 8 ints at
  0x4D9E94**, immediately after it. Both are saved to and loaded from
  `Game.ini [PLAYER]` by `load_player_info` in **`Gm_ini.cpp`**, which insists on
  exactly **8** values ("Invalid number of arguments for player damage",
  Gm_ini.cpp:640) and complains "No valid ini settings for damage" on an empty
  string (line 633).
- The shipped `Game.ini` carries `DAMAGE_MESH=-1,-1,-1,-1,-1,0,-1,-1` and
  `DAMAGE_VALUE=-1.000000,...,0.000000,...,-1.000000`, so **-1 is undamaged**
  and zone 5 is saved at frame 0 -- the first damage frame. Damage therefore
  **persists between races** through the ini, per zone.
- Picked up on the way: **`0x4281C0` is `set_damage`**, confirmed by
  `load_player_info` calling it with `[PLAYER] DAMAGE` straight from the ini
  (0x422972). That is the same function the trigger-2 service station calls with
  0 to repair the car.

### What this means for the port

The port keeps a single 0-100 scalar. The game keeps **eight independent 0..1
fractions and eight frame indices**, with the frame count varying per car model,
and persists all sixteen numbers in the ini. Reproducing the visible damage needs
the per-model frame counts out of the mesh data; the arithmetic itself is now
fully known and has no magic numbers in it.

## The eight damage zones are a compass ring (session 10)

Question 17's last detail -- how the grid cell indices are produced, and so how
the zones sit on the car -- comes out of the handler's own debug trace,
**"l_width_zone = %d, l_height_zone = %d, l_zone = %d."** (0x48b634).

### The formula

Three sign comparisons each yield a component in **{-1, 0, +1}**, comparing a
scaled car dimension against a scaled impact offset:

    fld dword [esi+0x18]   fmul dword [0x47f7bc]   ; the car's extent  x 10.0
    fld dword [esp+0x18]   fmul dword [0x47f7c0]   ; the impact offset x 0.6

(Corrected in session 12: the operands are the other way round. `[esi+0x10]` /
`[esi+0x18]` are the **collision point** x and z -- the handler prints them as
"Collision point: x = %f, z = %f." -- and the `[esp+...]` values are the
**mesh's own vertex extents**. See "The regional damage code never runs".)
    fcompp ... -> ecx, edx each set to -1, 0 or +1

and then

    0x004273ae  lea eax, [ecx+ecx*4]    ; 5 * l_width_zone
    0x004273b1  lea esi, [edx+eax*2]    ; l_height_zone + 10 * l_width_zone

- CONFIRMED: **`l_zone = l_height_zone + 10 * l_width_zone`**, with both
  components in {-1, 0, +1}. That is where the "10-wide grid" shape comes from,
  and it fixes the roles: the **width** component carries the factor of 10 and
  the **height** component is the unit, so a pure +/-10 is a sideways hit and a
  pure +/-1 an end-on one.
- The dispatch is `l_zone + 11` into a **23-byte** map at **0x42770C**, then a
  10-entry jump table at **0x4276E4**. Only the eight neighbours are live;
  l_zone **0** lands on `plr_car.cpp` line **1755**,
  **"Impossible collision! (Center of object)"**, and every other value falls
  through to the default.

### The ring

Putting the map and the cases together:

| l_zone | (width, height) | zone | direction |
|---|---|---|---|
| **+1** | (0, +1) | **0** | one end |
| **+11** | (+1, +1) | **1** | corner |
| **+10** | (+1, 0) | **2** | one side |
| **+9** | (+1, -1) | **3** | corner |
| **-1** | (0, -1) | **4** | the other end |
| **-11** | (-1, -1) | **5** | corner |
| **-10** | (-1, 0) | **6** | the other side |
| **-9** | (-1, +1) | **7** | corner |

- CONFIRMED: the zones go **round the car in order** -- 0 to 7 is a full
  rotation through the eight compass points, with 0 opposite 4 and 2 opposite 6.
  Nothing in the numbering is arbitrary.
- What is still **not** fixed is which absolute direction zone 0 points: that
  needs the sign convention of the impact offset at `[esp+0x18]`, which has not
  been traced. So the ring is known; its phase is not.
  (Session 12: the phase is fixed in **model space** -- zone 0 is the mesh's
  +z end, zone 2 its +x side -- and the whole question turns out moot, because
  this code never runs in the shipped game.)
- Every live case applies the same scale, **0.5** (0x47f870) -- there is no
  extra weighting for a corner over a flat side.

### Every constant in the scalar damage model, confirmed

The same function re-derives the session-8 scalar model from its own constants,
which is worth recording because the port is built on them:

    0x004273d3  cmp eax, 0x4650          ; 18000 -- below this, zero damage
    0x004273e8  cmp eax, 0x88b8          ; 35000 -- clamped to this above
    0x004273fb  fmul dword [0x47f86c]    ; 0.002857 = 1/350
    0x00427406  edi = [0x4d9ab0]         ; the difficulty
    0x00427410  dec/neg/sbb/and 0xfffffffa/add 0xc  ->  12 on HARD, 6 on EASY
    0x00427423  fmul dword [0x47f84c]    ; 0.01

- CONFIRMED: the thresholds are **18000** and **35000**, the divisor is **350**
  (as 1/350 = 0.002857), the per-impact maximum is **12 on HARD and 6 on EASY**
  selected by `[OPTIONS] difficulty`, and the final multiply is **0.01** -- i.e.
  `damage = max * 0.01 * (clamped_magnitude / 350)`.
- That is exactly the arithmetic the port implements. What remains unrecovered is
  only the **unit** of the magnitude itself, so the port's mapping of its own
  impact measure onto the 18000..35000 band is still its own.

## The vehicle record's +0x18 and +0x1c are current and previous chunk (session 10)

The speed camera's second gate was the last thing standing between the code and
an answer to "do the city cameras fire?". The engine settles it.

**fcn.00442020** takes a car index and does nothing but update its chunk:

    0x00442034  mov edx, [ecx+eax+0x18]      ; the current chunk
    0x00442038  mov [ecx+eax+0x1c], edx      ; previous := current
    ...                                       ; build the car's world position
    0x00442069  mov ecx, 0x4e5d28            ; the chunk map
    0x00442074  mov eax, [esi+0x1c]          ; the previous chunk, as a search hint
    0x00442078  call 0x445a10                ; position -> chunk
    0x0044207d  mov [esi+0x18], eax          ; current := the result

- CONFIRMED: **`record[+0x18]` is the car's current chunk and `record[+0x1c]` the
  chunk it was in on the previous update.** The previous value doubles as the
  search hint for the position-to-chunk lookup, which is why it is kept at all.
  `0x445a10` is the same lookup the camera subsystem uses at 0x414653.
- This **supersedes** the earlier note, which had `+0x1c` as "LIKELY a
  once-per-pass guard, with nothing found that writes it". It is written, once
  per update, by the engine rather than by game code -- which is why a scan of
  the game's own writes came up empty.

### So the camera's gate is a direction check

The handler's second gate (0x42a309) skips unless
`record[+0x1c] < record[+0x18]`, i.e. unless **the current chunk is higher than
the previous one**. That is true only on the frame a car advances a chunk going
**forwards**, so the gate does two jobs at once:

- it stops a flash when the car is travelling **backwards** along the route;
- it fires **at most once per chunk crossing** rather than every frame the car
  spends in the camera's chunk.

### And the city cameras do fire -- on everyone

Putting both gates together, a flitspaal flashes when

    (int)(speed * 3.6) > [0x48C480]      &&     previous_chunk < current_chunk

and **0x48C480 is statically -1**, written only by the four posted-limit
handlers. On **etappe0 and etappe6** no limit trigger exists anywhere in the
track, so the global stays at -1 for the whole stage and the first test reduces
to `speed > -1`, which any forward motion satisfies.

- CONFIRMED: **both city stages flash every car that crosses a camera chunk
  going forwards, at any speed.** Each carries six cameras. There is nothing
  conditional left for the answer to hang on.
- The same holds for the stretch of any stage **before** its first limit
  trigger, since nothing initialises the global per race.
- The port had this backwards: it started its limit at 0 and required
  `limit && kmh > limit`, so it never flashed where nothing was posted. Fixed to
  start at -1, which reproduces the original including this behaviour.

## The regional damage code never runs in the shipped game (session 12)

`handle_mesh_damage` is **fcn.00427190** (its own entry trace, "Entering
handle_mesh_damage(%d).", 0x48b788). The collision dispatcher calls it at
0x427b76, 0x427bd7 and 0x427c26 -- for object1, object2 and world-type hits --
with the collision record at **0x4e6760**, before it hands over to
`player_car_collision`.

### It returns at once for every model

    0x004271d6  cmp dword [eax+ebp+0x98], -1   ; the object's damage-frame handle
    0x004271de  jnz 0x4271f5
                push "Object has no damage frames, returning..."
                ... ret

- CONFIRMED (static): the zone computation, the l_zone dispatch, and the only
  runtime writes of **DAMAGE_VALUE** (0x42761b) and **DAMAGE_MESH**
  (0x42765c) all lie after this guard. Every other reference to the two arrays
  is `Gm_ini.cpp`'s load and save (fcn.004228b0, fcn.00422ec0) or the
  re-apply loop at 0x424e67, which skips -1 entries.
- CONFIRMED (runtime): the shipped `debug.txt` has **190** "Entering
  handle_mesh_damage" lines and **190** "Object has no damage frames,
  returning..." lines, and **no** "Collision point", "Edges:" or
  "l_width_zone" lines at all. This fits the session-11 finding that no `.DMG`
  file loads.
- So in the shipped game the **eight-zone ring is dead code**, and
  DAMAGE_VALUE/DAMAGE_MESH only ever hold what `Game.ini [PLAYER]` hands in
  and gets written back. The **0-100 damage** is unaffected: it is computed
  separately in `player_car_collision` (fcn.00427de0), which repeats the same
  18000/35000, 1/350, 12-or-6 and 0.01 arithmetic (0x427f45..0x427f99) and
  calls `add_damage` (0x427fab). The regional handler's copy of that
  arithmetic (0x4273d3..0x427423) only scales the per-zone increment.

### The comparison, read correctly

Before the zones, the handler takes four vertices of the object's original
mesh (index list built at 0x4271a6, walked at 0x427277) and sorts their x and z
by sign against 0.0 (0x47f798) into **l_x_max / l_x_min / l_z_max / l_z_min**.
It prints them as "Edges: l_x_max = %f, l_x_min = %f, l_z_max = %f,
l_z_min = %f." Then:

    l_width_zone  = +1 if cp.x * 10 >  l_x_max * 0.6;  -1 if cp.x * 10 <  l_x_min * 0.6;  else 0
    l_height_zone = +1 if cp.z * 10 >  l_z_max * 0.6;  -1 if cp.z * 10 <  l_z_min * 0.6;  else 0

with `cp` = `[esi+0x10]`, `[esi+0x18]`, printed as "Collision point: x, z".

- CONFIRMED: **zone 0 is the mesh's +z end, zone 4 its -z end, zone 2 the +x
  side and zone 6 the -x side**, all in the model's own axes.
- LIKELY: zone 0 is the **front** of the car. The port draws car meshes
  nose-toward +z and they look right, but nothing here proves that the
  collision point is in model space rather than world space. The comparison
  against model-space extents only makes sense if it is.
- Unexplained: the factor of 10 on the collision point against 0.6 on the
  extents. Taken at face value a hit counts as an "end" or "side" once it is
  past 6% of the half-extent. That suggests the collision point is in a
  different unit from the mesh, but this was not traced.

## RESET_COP, BACKTRACK_SPEED_TRIGGER and cop_lane (session 12)

The last two unread police helpers, plus the lookup they rely on. Reading them
**overturns the police speed table** recorded in session 9, which was one case
off, and that error had reached the port.

### The police limits are 4 -> 60, 5 -> 80, 6 -> 120

`RESET_COP` (**0x402200**, "RESET_COP()") is called once, from the AI update
at 0x406b3b, for a driver of type 14. It looks up the car's speed and lane
triggers and writes the cop's limits:

    0x40224f  jmp [ (trigger-4)*4 + 0x4024e0 ]
    0x4024e0: 0x40228d  0x4022b9  0x4022e4  0x402310  0x402256

| trigger | arm | `[cop+0x14]` | km/h |
|---|---|---|---|
| 4 | 0x40228d | 16.67 (0x41855c29) | **60** |
| 5 | 0x4022b9 | 22.22 (0x41b1c28f) | **80** |
| 6 | 0x4022e4 | 33.33 (0x420551ec) | **120** |
| 7 | 0x402310 | -- | `ILLEGAL SPEED TRIGGER!!!!` |
| 8 | 0x402256 | `2 * [0x4d9e68]` | unrestricted (see below) |

- CONFIRMED: the session-9 table had these shifted by one case, giving
  "4 = 2x default, 5 = 60, 6 = 80, 7 = 120, 8 = illegal". The corrected
  mapping is the **same** one traffic uses (`4 -> 60, 5 -> 80, 6 -> 120,
  8 -> none`). So the police and the traffic AI read the signs alike, and the
  shipped tracks' 4/5/6 triggers are 60/80/120 zones for the police too.
- The lane limit is also chosen by **carriageway**. If the car's side
  (`record[+0x28]`) is 1, triggers **18..21** give 1..4 lanes (table 0x4024f4).
  If it is 0, triggers **40..43** do (table 0x402504). That explains the two
  parallel lane-trigger groups in the census: one per carriageway.

### BACKTRACK_SPEED_TRIGGER: the last sign behind the car

**0x408080** (`"BACKTRACK_SPEED_TRIGGER"`) takes a vehicle and two out-pointers.
It walks chunks from the car's current one (`record[+0x18]`), stepping **-1**,
or **+1** when `[0x4d9ac4]` is set. That global is **`Game.ini [ETAPPE]
DIRECTION`** (written at 0x421be1). In each chunk it tests both trigger slots
through a byte map at 0x4083a4 / jump table 0x408390:

- **4, 5, 6 and 8** are speed triggers. **7 is not** (it maps to the
  default arm), which agrees with RESET_COP.
- 18..21 count as lane triggers only when side == 1, and 40..43 only when
  side == 0.
- The first of each kind found wins. It stops once both are found ("All
  triggers found, returning.").
- If it runs off the track: "Illegal chunk, returning defaults...", with the
  speed trigger defaulting to **8** and the lane trigger to **19** (side 1)
  or **41** (side 0), i.e. **2 lanes**.

So a cop enforces **the last sign the car passed**. Before the first sign it
enforces trigger 8.

### What trigger 8's "2x default" is

`[0x4d9e68]` is written once, in `init_player_car` (0x425802), from fcn.0040ce10.
That function reads the player car's row of the 0x484B18 physics table
(index `[0x4d9e2c]`). It is also the divisor at 0x42628a, and HANDLE_RACER_AI's
dead state-1 arm uses it as a speed cap.

- LIKELY: it is the **player car's top speed**, so a cop's limit before any
  sign, and on the city stages, is twice that: in effect **no limit**. No
  track carries a literal trigger 8.

### cop_lane: the least-blocked lane, starting from the suspect's

`cop_lane` (**0x4051f0**) is the cop branch of the per-driver lane chooser in
`ai_lane.cpp`'s `change_lane` (fcn.00403660). That function switches on driver
type: traffic 0x404960, racer 0x404ee0, cop 0x4051f0, ADAC 0x4053f0. It
receives the lane count and an array of 32-byte **`t_lane_info`** records,
whose layout the caller's own trace at 0x403a84 names:

| offset | field |
|---|---|
| +0x00 | m_dead_end |
| +0x04 | m_dead_range |
| +0x08 | m_front_obstacle (vehicle, -1 none) |
| +0x0c | m_front_range (float) |
| +0x10 | m_front_status |
| +0x14 | m_rear_obstacle |
| +0x18 | m_rear_range (float) |
| +0x1c | m_rear_status |

`m_front_status` is set by fcn.00404720 (0x4047e4..0x40484d):

| status | condition |
|---|---|
| 0 | no obstacle, no dead end |
| 1 | obstacle ahead and **faster** than us (`[+0x74]` speed) |
| 2 | obstacle ahead, same speed or slower |
| 3 | dead end, `m_dead_range >= 2` |
| 4 | dead end, `m_dead_range < 2` |
| 5 | obstacle whose direction dotted with ours is < 0 -- **oncoming**; also raises a flag for the caller |

CONFIRMED, the choice itself:

1. The cop's target is `cop[+0x04]`. Its side and lane come from
   fcn.00402520(chunk, waypoint), which finds the waypoint in the chunk's
   side-0 list (+0x124) or side-1 list (+0x154). That function asserts
   "Waypoint %d not found in chunk %d" at ai_cops.cpp:163.
2. **Start** with the target's lane, mapped into the cop's chunk by
   fcn.00442730, if the cop is on the same carriageway. Otherwise start with
   the **last** lane (`lanes - 1`).
3. Scan lanes `0 .. lanes-2`. Take lane *i* if its front status is **lower**
   than the current pick's. If both are 1 or 2 and equal, take *i* when its
   `m_front_range` is **larger**, i.e. more room ahead.
4. Log "Picked lane %d." and return it.

So a chasing cop heads for the suspect's lane unless another one is clearer
ahead. Note that the scan never examines the last lane as a challenger; it can
only win as the starting pick. That reads like an off-by-one in the original
rather than a design choice (SPECULATIVE).

## `.pth` f3 is the lap, and the path types re-read (session 12)

### f3: which lap a path plays on

**fcn.00434460** (called from 0x424b37) runs when the player's chunk
`[0x4d9e44]` differs from the last value it saw (0x496750, initially -1):

    if laps_done [0x4d9ad4] != last_laps [0x4e47c4]:
        every path's run counter (record+0x338, 0x4e0a48 + i*828) = 0
    for each path i:
        if first_chunk != 0 and first_chunk == player_chunk
           and (f3 == laps_done + 1 or f3 == 0)
           and run_counter == 0:
            if start_fn[type](i) != 0: run_counter++

- CONFIRMED: `0x4d9ad4` is **laps completed**. `Tr_finis.cpp`'s finish-line
  handler 0x429860 stores the lap time at `0x4dc970[laps]`, increments it
  (0x4298a9) and clamps it to Nrlaps (0x4d9ad8). So **f3 is a 1-based lap
  number, with 0 meaning every lap**, and each path fires **at most once per
  lap**, when the player reaches its `first_chunk`.
- The data fits. The city circuits (2 laps by default) use only **1 and 2**.
  On etappe2, chunks 140-180 hold one train path for lap 1 and a different one
  for lap 2; etappe0's four boat paths are two per lap. The highway stages run
  one pass with laps_done = 0, so they use **0** or **1**.
- A path with `first_chunk == 0` never starts. Two shipped paths have
  `last_chunk == 0` (etappe2 path 2, etappe3 path 0), which does not affect the
  trigger.

### The type table starts at 0x4966F0

The dispatcher reads `[type*12 + 0x4966f0]` (0x434515). Session 10 read the
table from 0x4966F4, one dword later, so every function was credited to the
wrong type. Correctly, `{start, +4, +8}`:

| type | start | +4 | +8 | what |
|---|---|---|---|---|
| 0 | -- | 0x434140 (bare ret) | -- | nothing |
| 1 | 0x432a10 | 0x432a80 | 0x432a70 | unnamed |
| 2 | 0x432bd0 | 0x432db0 | 0x432d80 | **police car** (`MPOLIZEI_ANIM`) |
| 3 | 0x434800 | 0x4348f0 | 0x4348d0 | `E00_ROND_ANIM` -- LIKELY a canal boat |
| 4 | 0x434a70 | 0x434b60 | 0x434640 | **train** (`C_TREIN_ANIM`) |
| 5 | 0x433170 | 0x4333b0 | 0x433380 | **helicopter** (`pth_heli.cpp`) |
| 6 | 0x434cf0 | 0x434df0 | 0x434640 | **random traffic vehicle** |
| 7 | 0x434550 | 0x434670 | 0x434640 | **aeroplane** (`A_VLIEGT_ANIM`) |

- CONFIRMED: the start column is what the chunk trigger calls, and each named
  start function loads its mesh at its top (0x432bd5, 0x434803, 0x434a73,
  0x4331d0, 0x434553). Type 6 picks `rand() % 5` from the name table at
  0x4969F8: `N_BUS`, `N_VRTAUT` (lorry), `N_BESTEL` (delivery van), `N_GOLF`,
  `N_ASTRA`.
- LIKELY: +4 is the per-frame update (dispatched by 0x434150) and +8 a
  release (types 4, 6 and 7 share 0x434640, which calls 0x428a70 and
  0x43f950). Not traced.
- STRONGLY SUPPORTED by the point heights, independently of the code: type 5
  (helicopter) paths fly at **y 67..217**, type 4 (train) runs near ground
  level (**-5..61**), and type 3 sits at a constant **y = -34**, below the
  street surface. That is water level for a boat. `E00_ROND` measures 13.5 x
  4.8 x 2.1 m: long, narrow and low. *Rondvaartboot* is the Amsterdam canal
  tour boat, and E00 is the Amsterdam stage's prefix, so LIKELY that is what it
  is; the name is not proven.

### Corrected usage

| stage | paths |
|---|---|
| etappe0 / etappe6 | four boats (3), laps 1, 1, 2, 2 |
| etappe1 | one aeroplane (7, lap 1), six traffic vehicles (6, every lap) |
| etappe2 | two helicopters (5), three trains (4), two traffic vehicles (6) |
| etappe3 | one traffic vehicle (6) |
| etappe4 | two trains (4), laps 1 and 2 |
| etappe5 | none |

- The **helicopter flies only on etappe2**. That is a city stage, so its
  assert holds, but session 10's "3/4/5 city, 6/7 highway, exactly" was wrong:
  type 6 runs on etappe2 too.
- The police-car path type (2) is still authored nowhere.
- Sample 45 `trein.wav` now has its train.

## Car shadows: one shared quad under every vehicle (session 12)

`gfx_shdw.cpp` is small and fully read.

- CONFIRMED: **init_car_shadow_object** (0x410df0, called from 0x424181)
  creates **one** 3D object from `S_SHADOW_ANIM`, keeps its index in
  **0x4d9a90**, sets its render flag (+0x95) to 2, and asserts the mesh is a
  4-vertex quad ("shadow mesh (%d) is not square", Car_main.cpp:447). It insets
  the quad's UVs by half a texel (0.5 / texture width, 0x47f450 / 0x47f454).
- CONFIRMED: **every vehicle gets it.** The vehicle constructor 0x410bc0
  (called by the player, racer and traffic spawners) and 0x4110e0 store
  0x4d9a90 in `record[+0x24]`. The engine's vehicle draw loop (0x440aa2,
  0x440d6b) calls the hook in 0x4e5dc8 for each active vehicle with
  `+0x24 != -1`, just before drawing the vehicle. The hook is **0x41d460**,
  installed by 0x41d810.
- CONFIRMED, what the hook does for car *n*:
  - the quad's four corners take the x and z of four bounding-box corners of
    the car's own mesh (mesh record +0x10, +0x1c, +0x40, +0x4c), so the shadow
    is the car's footprint;
  - position = the car's position **+ (2, 2)** in x and z (0x47f6c4 = -2.0,
    subtracted), a fixed sun offset;
  - height = the road under the car (0x442f00) **+ 0.5** (0x47f6c0 = -0.5,
    subtracted);
  - the car's yaw and orientation matrix;
  - drawn with ALPHABLENDENABLE (27) on, SRCBLEND (19) = SRCALPHA (5),
    DESTBLEND (20) = INVSRCALPHA (6), and only when **0x4d9b18** is set,
    i.e. Game.ini's `alpha_blending`.
- The texture (`s_shadow.tga`, 64x64, 24-bit) is a black rounded rectangle on
  the (255,0,127) key, with no alpha channel. Measured off a capture of the
  original (`hud.png`), the shadow brings the road from (128,106,68) to about
  (68,52,31): an **effective opacity of about 0.5** (measured, from a
  compressed capture).
- The port now draws it for the player, racers, traffic and police.

## Brake lights: the player's only, and the sprite numbers are off by one (session 12)

`txt_reml.cpp`'s 0x42b100 was read in session 10. Here are its condition and
its art.

- CONFIRMED: **only the player's car.** 0x42b100 takes the slot from
  `[0x4d9e2c]`, the player's car type (0..6, asserted "Illegal car!"), and
  blits frame `2*car` when its argument is 1 and `2*car + 1` when it is 0.
- CONFIRMED: **when.** The car step (0x425f0b..0x425faa) calls it with 1 when
  `[0x4a63cc] == 1` or `[0x4d9df4] == 1`, and with 0 otherwise. It calls only on
  a change, tracked in 0x4d9e10. `0x4a63cc` is written by the drive function at
  0x40ddee as `force / velocity < 0`, the commanded force opposing the motion:
  **braking**. It is 0 at rest (0x40ddc9). `0x4d9df4` is an input-handler state
  (0x4265b0 / 0x4269c0), LIKELY reversing; not traced further.
- CONFIRMED: in hires mode with `[0x4d9b0c]` set (LIKELY `object_lights`) the
  same two states drive two alpha-blended light objects (0x48b458 / 0x48b45c,
  via 0x41b560) instead of the blit.
- **The 88-byte table's first two fields are the source anim, not a
  texture.** For slots 0-6 they are 26 (lores) and 27 (hires): entries of the
  static sprite-anim table at **0x4961E8** (12-byte records `{frame count, ?,
  frame list}`, read by 0x46ad60 into 0x4efc64). Anim 26 and 27 each list **14
  frames**, 7 cars x {lit, unlit}, as `(2, sprite)` pairs. The destination is
  the PCX named at +0x10, at (x, y).
- STRONGLY SUPPORTED: **the anim frame lists use RC.IND index + 1.** Anim 27
  reads `339 340 334 328 329 330 331 332 337 338 333 327 335 336`. Subtract one
  and every car gets a lit/dark pair of the same size in the right art:
  `338/339, 333/327, 328/329, 330/331, 336/337, 332/326, 334/335`. The police
  flasher anim 40 `(323, 341)` likewise becomes the pair 322/340. That is 16 of
  16. Four slots (0, 3, 5, 6) then match the shipped RACER sheets **pixel for
  pixel** with their unlit sprite at the table's (x, y). So the sheets ship
  unlit, and the shift is real rather than a coincidence of sizes.
  - Caution: any RC.IMG sprite number taken **from game code** should be
    checked for the same +1. The HUD numbers in the port (425, 426, 431...)
    were found by looking at the images, so they are 0-based.
- Car-by-car (0-based sprites, sheet, position): 0 Trabant 338/339 RACER02
  (246,13); 1 Astra 333/327 RACER01 (109,24); 2 Beetle 328/329 RACER02
  (35,163); 3 BMW 330/331 RACER01 (102,164); 4 Porsche 336/337 RACER02
  (95,39); 5 CLK 332/326 RACER01 (242,42); 6 Diablo 334/335 RACER01
  (143,132).
- The lores set (342..353 shifted, anim 26) needs the 8-bit per-track palette.
  It is not used by the port.

## gfx_wolk.cpp "clouds" are tyre smoke; rain and fog are off in the shipped game (session 12)

### The "clouds" are smoke puffs behind a skidding car

`Game.ini`'s `clouds` key (into **0x4d9ae0**, 0x421fef) is described as
"alpha blended clouds", but gfx_wolk.cpp draws no sky.

- CONFIRMED: **init_clouds** (0x4201a0) runs only in hires mode (0x4d9aac),
  with the needed alpha caps (0x4d9b04 & 0x10, 0x4d9b08 & 0x02) and
  `alpha_blending` (0x4d9b18). Otherwise it logs "Clouds disabled: Hardware
  doesn't support used alpha modes" and clears 0x4d9ae0. It prepares **70
  puff records** of 300 bytes at 0x4d3a18 (active flag at +0x128) and loads
  `S_SFX.PCX` (0x4d3a10).
- CONFIRMED: **the puff shape** (0x4200c0) is a 6-vertex `D3DLVERTEX` triangle
  fan. The centre is colour **0x508484a4**: blue-grey (132,132,164) at alpha
  0x50 (~0.31). The four corners at **+/-3.5** are 0x00646464, fully
  transparent. So it is a soft radial puff made by vertex colour.
- CONFIRMED: **the spawner** 0x41fff0(pos, vel) does nothing unless 0x4d9ae0
  is set and the frame rate is above **12 fps** (`1/dt > 12`, 0x47f750 /
  0x47f754). Otherwise it starts **two** puffs (0x41fd80) in free slots.
- CONFIRMED: **who spawns them.** 0x425a90 (each frame, from 0x424af8) spawns
  when the **skid intensity `[0x4a63dc]` >= 4**, the same threshold as
  skidmarks, and at most every **100 ms** (GetTickCount, 0x4d9e14). It makes two
  calls, at points beside the **player's** car, placed with a physics-table
  offset (`-10 * [0x484b3e + row]`), the 0.6 factor and the car's heading. A
  third caller is in the trigger 3/48 handler (0x415b04), not traced.
- CONFIRMED: **a puff's life** (0x41fd80, 0x41fae0, 0x41f810):
  - it lasts `4 / dt` frames (0x47f740 = 4), i.e. **4 seconds**;
  - its velocity starts as the given one scaled by a random 0.75..1.0 in x and
    z, plus a random upward term; it is multiplied by **0.9** every frame
    (0x47f73c) and added to the position;
  - its size starts at **7** (0x40e00000) in both axes and grows by
    `29 / lifetime_frames` per frame (0x47f734), so it ends at **36** units;
  - its alpha falls from 0x50 by `max(1, 0x50 / lifetime_frames)` per frame;
  - it is re-oriented to the active camera every frame (copied from 0x4e66a0),
    so it is a billboard. Drawn by 0x4203d0 with SetTransform + DrawPrimitive.
- So the effect is **tyre smoke**: grey-blue puffs that billow and fade behind a
  sliding car. It needs the skid intensity, which has **28 writers** across
  the physics code (0x40d217 .. 0x4260e3). The port does not model it yet, so
  smoke waits for that, as skidmarks do.

### Rain is part of the disabled particle module

- CONFIRMED: `part_rain` (into **0x4d9af8**) has exactly one reader, the
  emitter set-up 0x4328b0. That is called only from 0x431f20, which returns at
  once unless `particles` (**0x4d9af4**) is set. The shipped Game.ini has
  `particles=0` ("the module isn't used"). So **no rain falls with the shipped
  settings**.
- The emitter itself (one 284-byte descriptor at 0x4e0460, callback 0x432990)
  takes 200 particles over an angle of 0..2pi (6.2832) at speed 2.0, greys
  0.5 -> 0.1, and gravity -0.1. LIKELY rain; it is never active.

### Fog is off unless a trigger turns it on

- CONFIRMED: the D3D device constructor (0x45f6e0, and its twin at 0x45f960)
  sets fog **enable = 0** (`xor ebp,ebp`, stored at +0x27c), colour 0xFFFFFF,
  start 3.25, end 50.0, density 0.8, table mode 3. The device restore at
  0x45fd36 / 0x45fd76 re-applies those (render states 28 FOGENABLE and 34
  FOGCOLOR).
- The only code that turns it on is the trigger 16/17 handlers
  (0x429ad0 .., the "blue haze" block), and **triggers 16 and 17 occur in no
  shipped track**. So the original races **without fog**, as the port does.
- The "*FOG* error setting ... state" strings are a generic label in the
  engine's error macro. The puff draw, the shadow and other code use them too,
  so they do not by themselves mean fog.

## How the race sounds: engines, loops and positional samples (session 12)

`snd_main.cpp` read end to end, enough to rebuild the soundscape.

### Levels are DirectSound hundredths of a dB, offset by SFXVOLUME

- CONFIRMED: `0x420740` returns **`Raser.ini [SOUND] SFXVOLUME`** (0x489631,
  read at 0x420c2d). Every player returns at once when it is 0, and subtracts
  it from every volume. The shipped value is **100**. The ini comment says
  "100=minimum, 3100=maximum", but the code makes a **larger value quieter**:
  100 is the loudest setting.
- CONFIRMED: one-shots (`0x4286a0`, sample index 0..86) and loops (`0x428700`,
  same table, looped) both play at **-500 - SFXVOLUME** (0x4286de / 0x42873e),
  i.e. -6 dB with the shipped ini.
- `[SOUND] ON=1`, `BUFFERS=60`, `MUSICVOLUME=0`. `[MUSIC]` lists six tracks
  (M_FUNK .. M_ZZTOP) but says "Music is currently disabled in start_game()".
  **There is no music in a race.**

### The player's engine is two cross-faded loops on rpm

`init_player_car` starts two loops (0x425821 .. 0x425847): **sample 23
`motor.wav`** into 0x4d9ce4, and **sample 24 `toeren1.wav`**, or **85
`toeren2.wav`** when `Game.ini [PLAYER] motor` (0x4d9ec0) is set, into
0x4d9ce8. 0x427070 updates both every frame from the **engine rpm 0x4a63d8**,
which the drive function's gear code writes. The rpm is pinned to **800**
(0x44480000) at standstill (0x40f391). With `S = SFXVOLUME`:

| loop | frequency (Hz) | volume (1/100 dB) |
|---|---|---|
| `motor` | `clamp(24000 - rpm, 11000, 24000)` | `clamp(-rpm/8, -2000, -100) - S - 200` |
| `toeren` | `min(10000 + 3*rpm, 40000)` (0x47f7cc = -3) | `clamp(rpm/2 - 2000, -2000, -100) - S - 200` |

So the low `motor` rumble fades and drops as the revs climb, while the
`toeren` whine rises in pitch and fades in. At 800 rpm the two sit at 23200 Hz
/ -1 dB and 12400 Hz / -16 dB.

### Tyre slip is a loop on the skid intensity

0x4259a0 (every frame): while the skid intensity `[0x4a63dc]` is **>= 4** it
keeps **sample 39 `sliploop.wav`** looping in 0x48b460, at frequency
`clamp(15000 + 700*i, 15000, 28000)` and volume
`clamp(100*(i - 11), -1100, -100) - S - 100`. Below 4 it stops the loop.

### Other vehicles are positional loops on 20 object slots

- CONFIRMED: every vehicle's `record[+0x14]` is its **object sample**: the
  engine note from physics field 55 (auto1/2/3, best_bus, bus, vracht), or
  **40 `psirene1.wav`** while a police car's light is on (0x40c640 swaps it
  via 0x4289e0, and back when it goes off). Roadside objects use it for their
  knock-over sounds: 18/19 `paal.wav` for posts and 16/17
  `pionb1`/`pionb3` for cones (0x428d95, 0x42942a, 0x42980a).
- CONFIRMED: `sndfx_play_object_sample` **0x428bc0(obj, loop)**:
  - distance = **|dx| + |dz|** to the player's car (integer world units);
  - pan, only with `3dsound` (0x4d9a94): the angle to the object in the
    listener's frame, times 90, clamped to **-90..+90** and mapped through
    0x4d9ed8 to DirectSound pan **`111*i - 9990`** (about -10000..+10000);
  - under 2500 units it starts the sample on a free slot of **20**
    (0x428780) or updates the slot already playing it (0x428920).
- CONFIRMED: the volume table 0x4da254 (built by 0x428590) has **2500
  entries**, `-exp(ln 100 + i * (ln 3000 - ln 100) / 2500)`, clamped to
  -100..-3000. Distance in world units indexes it directly, so a sound falls
  from -1 dB beside the player to -30 dB at 250 m. The slot plays at
  `table[d] - S - 100`.
- CONFIRMED: on start, a slot's frequency is the sample's own rate plus
  **`rand() % 500`**, clamped to 8000..40000 Hz. That only detunes vehicles
  from each other. Other vehicles' engines **do not follow their speed**.

### One-shot and event details

- **Horn**: key **T** (VK 0x54) plays **41 `toeter.wav`** (0x424ee0).
- **Turbo**: **84 `turbo.wav`** plays once when the turbo engages
  (0x425d47), guarded by 0x4d9e18 until it is released.
- **Countdown**: `3git`/`2git`/`1git`/`gogit` (12-15) are in the sample
  table but **not in the archive**. The countdown plays only **10
  `tick.wav`** (0x424bd3).
- Per-frame **damage rattles** (37/38 at damage >= 65 / 75), the **scrape
  loop** (82), the **graded impacts** (25-30), **flits** (11), **repair**
  (47), **checkpoint** (83) and the **police voice** pools are as recorded in
  the sessions above.
- **Formats**: the 74 WAVs in RCS.IMG are PCM at 8000, 22050 or 44100 Hz,
  8- or 16-bit, mono or stereo.
- **The collision sound's timing** (0x427cc0): if less than **300 ms** has
  passed since the last collision call (0x4d9e20), a magnitude under 18000
  (with 0x4e6764 clear) restarts the scrape 82 if it is not playing. Otherwise
  a graded impact plays. **Every** call refreshes the 300 ms (0x427dd4), so a
  sustained contact scrapes and a new crunch needs 300 ms clear of contact.
- **The police voices' timing**: the catch warning plays on each catch
  (0x4153f1). The pursuit chatter plays as a chase starts and the give-up line
  as it ends, both only when the shared police channel is free.

## How a path object moves (session 12)

The runtime half of `pth_main.cpp` / `pth_heli.cpp`, enough to animate all
five shipped types.

### Starting: ten slots, equal time per segment

- CONFIRMED: active path objects live in **10 records of 40 bytes** at
  **0x4e0580**. The start helper **0x434220(params)** takes the first free
  one, copies a 9-dword parameter block into +4, and sets:
  - +8, the frame count: **`time_ms * 0.001 / dt`** (0x47f988 = 0.001; dt
    from 0x441cd0), rounded **down to a multiple of the segment count**
    (points - 1 for an open path, points for a closed one);
  - +0xc, the current frame: 0.
- The parameter block (offsets from +4): +0 active, +0xc **tension 0.5**,
  +0x10 the object, +0x14 -1, +0x18 the path index, **+0x1c repeat**, **+0x20
  closed loop**. Every shipped start function passes **closed = 0**.
- So every segment between two authored points takes the **same number of
  frames**, however long it is. The time is measured in frames at the frame
  rate when the path started.

### The curve: Catmull-Rom

- CONFIRMED: **0x433b10(out, path, frames, frame, tension, closed)** maps a
  frame to a segment `i` and a fraction `t`, takes points i-1, i, i+1, i+2
  (wrapped when closed), forms tangents **`(p[i+1] - p[i-1]) * tension`** and
  **`(p[i+2] - p[i]) * tension`**, and blends with the Hermite basis
  (constants 2, 3, -1, -2 at 0x47f978..0x47f984). With tension 0.5 that is a
  **Catmull-Rom spline** through the authored points. A path with one point
  returns that point.

### Each frame (+4) and the end (+8)

- CONFIRMED (train 0x434b60, plane 0x434670, boat 0x4348f0): while
  `frame < frames - 2`:
  - position = the curve at `frame`;
  - heading = towards the curve at `frame + 1`, as
    **`(atan2(dx, dz) - pi/2) * 1024 / 2pi`**, in the engine's 1024-per-turn
    units (0x47fc6c pi, 0x47f990/0x47f998/0x47f9a0 = 0.5, 0x49b014 = 1024,
    0x47fc70 = 2pi);
  - the object's chunk is pinned to the **player's chunk** (0x4d9e44), so it
    is never culled;
  - frame += 1.
  At the end the object **stays at its last point**.
- CONFIRMED: an object is released only when the path's **last_chunk is
  non-zero and the player's chunk equals it**. The update then clears the
  record and returns 1, and the dispatcher calls +8. For types 4, 6 and 7 that
  is 0x434640: stop the object's sound (0x428a70) and free it (0x43f950).
  With last_chunk 0 the object stays until the race ends.
- Every type except the boat plays its object sample as a **positional loop**
  each frame (0x428bc0(obj, 1)).

### Per type

| type | start | object | sound | special |
|---|---|---|---|---|
| 3 | 0x434800 | `E00_ROND` | none | as the train |
| 4 | 0x434a70 | `C_TREIN` | 45 `trein` | as above |
| 5 | 0x433170 | `C_HELI` + `C_LBLADE` + `C_SBLADE` | 46 `heli` | see below |
| 6 | 0x434cf0 | random of 5 (`N_BUS`, `N_VRTAUT`, `N_BESTEL`, `N_GOLF`, `N_ASTRA`) | 32 `auto2`, whatever the model | **repeats**, pitches with the path |
| 7 | 0x434550 | `A_VLIEGT` | 44 `vliegt` | as the train |

- **The helicopter** (update 0x4333b0): the curve point is lowered by **5**
  (0x47f970). The body gets a full orientation matrix built from the
  normalised direction of travel (0x4075b0 / 0x407590 / 0x45e270..0x45e2b0),
  so it noses up and down along its 3D path. Both blades are children
  attached by 0x441630:
  - `C_LBLADE` is modelled in place over the body (y 10.6..18.2) and attaches
    at the origin;
  - `C_SBLADE` is modelled at the origin, a disc in the y/z plane, and
    attaches at **(0.026, 7.06, -77.58)**, the tail (the body reaches
    z = -83.8).
  Every frame the main blade's yaw (+0x78) and the tail blade's pitch (+0x74)
  advance by **512 units, half a revolution** (0x47f974 = -512, subtracted).
  It sets the repeat flag, but its update never reads it.
- **The scripted traffic car** (update 0x434df0): oriented from the full 3D
  direction like the helicopter. At the end of its path, with repeat set
  (+0x1c), it **starts over** (0x434e9d..0x434f1f): a new model,
  `rand() % 5` from the table at 0x4969F8, a new duration of **`rand() % 5 + 5`
  seconds** (0x47f9ac = -5, subtracted) converted to frames and re-quantised
  (0x433a70), and frame 0. So a type-6 path is a **continuous stream** of
  vehicles for as long as it is active.
- None of them has a car shadow. The shadow hook is installed only by the
  vehicle constructor 0x410bc0, and path objects are made by 0x43f770.

### Seen in the port (session 12)

Rebuilt from the above, every shipped type renders sensibly with the meshes
authored nose-toward +z and the heading `atan2(dx, dz)`:

- type 3 is a glass-roofed **Amsterdam canal tour boat** on the water of
  etappe0's canal. `E00_ROND` as a *rondvaartboot* is now STRONGLY SUPPORTED
  by sight, on top of its size and the y = -34 water level;
- type 4 is a yellow **NS train** on the embankment track beside etappe2,
  under the overhead wires, aligned with the rails;
- type 5 is the **helicopter** in level flight over the city, its main rotor
  on the cabin and the tail rotor on the boom at the recovered offset;
- type 7 is a **KLM 747** low over etappe1's motorway.

The starts follow the lap field exactly: on etappe2, lap 1 runs the two
every-lap traffic paths, the two lap-1 trains and the lap-1 helicopter, and lap
2 runs the traffic again, the lap-2 train and the lap-2 helicopter.

## Two driving models, and the shipped game uses the other one (session 12)

**Major correction.** Much of the earlier physics work (sessions 8-11) traced
functions that the shipped game never runs.

- CONFIRMED: the per-frame player update (0x426238) branches on **`Game.ini
  [OPTIONS] stuur_model`** (0x4d9af0, read at 0x421f3a). The ini describes it
  as "Selects steeringmodel: Old (Autobahn Raser I)=1, New (A2 Racer II)=0",
  and the shipped value is **0**:
  - `stuur_model = 1` (old, Autobahn Raser I) calls **0x40cfd0**, a two-axle
    bicycle model (0x42632c);
  - `stuur_model = 0` (new, A2 Racer II, **the shipped setting**) calls
    **0x40fc30** (0x426262).
- CONFIRMED by cross-reference, every one of these has **0x40cfd0 as its
  only caller** (directly or through 0x40dd00), so **none of them runs in the
  shipped game**:
  - 0x40dd00, the longitudinal controller (torque `f24*x*(2-x)` with
    `x = rpm/f25`, engine braking `-0.5*f24`);
  - 0x40e190, the gearbox;
  - 0x40da90, the drive force with the **field-52 traction clamp**;
  - 0x40d7d0, the brake force, field 53 and **ABS** (0x4a63c8);
  - 0x40e870, the **handbrake** sliding friction;
  - 0x40e530, the **Pacejka** lateral forces (fields 15-20, 54);
  - 0x40e3d0 (slip angles), 0x40d5d0 (axle loads), 0x40d6e0, 0x40e7f0.
- So these session-10/11 findings describe **the old model only**: the
  longitudinal force law, "Field 52 is the longitudinal traction cap", "Field 53
  is the brake coefficient", ABS, the handbrake, the Pacejka law, and "the
  traction clamp is the main skidmark source". They are correct readings of
  code that is **dead with the shipped Game.ini**. Whether the new model uses
  the same fields differently is the next thing to establish.
- The new model 0x40fc30 calls **0x40f0d0** (writes the "force opposes
  motion" flag 0x4a63cc and pins the rpm 0x4a63d8 to 800 at rest), **0x40f3c0**
  (the yaw update, where A[29] was found in session 9), **0x40ebd0 /
  0x40ece0 / 0x40ee80** (shared with the AI drivers 0x409ed0..0x40b770) and
  **0x412e90**. The skid-intensity writers 0x40f80a..0x40f832, 0x40fb4b and
  0x40fdda are in this model.

### The old model, for the record

Traced symbolically under emulation (`reverse-engineering/tools/symx87.py`)
before the branch was found:

- axle loads `-m*g*(b*cos t - h*sin t)/L` front, `-m*g*(a*cos t + h*sin t)/L`
  rear, with a = f5, b = f6, L = a + b and **f7 the centre-of-gravity height**;
- slip angles with a **half-wheelbase** lever arm, `steer - atan((v_lat -
  r*L/2)/v)` front and `-atan((v_lat + r*L/2)/v)` rear;
- skid intensity from the body slip angle, `(int)(min(|atan(v_lat/v)|, 0.3) *
  33.3)` above 0.2 m/s;
- below 7 m/s, no tyre forces and a kinematic yaw rate;
- lateral forces averaged with the previous frame, and zeroed on a sign flip
  greater than 2000;
- **f24 is peak torque in Nm and f25 its rpm** (the Diablo's 580 Nm at 5200
  rpm are its real figures), torque `f24*x*(2-x)`.

### The engine's integrator (shared by both models)

- CONFIRMED: **0x4407e0(F, r)** accumulates a force into vehicle `+0x84..0x8c`
  and a torque into `+0x90`.
- CONFIRMED: **0x4404a0** (from 0x43f900, per vehicle per frame) integrates.
  With dt, the heading psi (object `+0x78`, 1024 units per turn) and the local
  velocity (vehicle `+0x6c` lateral, `+0x74` forward):
  - **position** `x += 10*dt*(cos psi*v_lat - sin psi*v_fwd)` and
    `z += 10*dt*(sin psi*v_lat + cos psi*v_fwd)`, from the old velocity,
    quantised to 1/10000 (10 world units per metre);
  - **heading** `psi += dt * r * 1024/2pi`, with r the yaw rate `+0x7c`;
  - unless vehicle `+0x38` is set, the local velocity is **rotated back by the
    heading change** (truncated to whole units), so momentum keeps its world
    direction;
  - then `v += F*(1/m)*dt` and `r += tau*(1/I)*dt`, with 1/m at `+0x60` and 1/I
    at `+0x64`.
- CONFIRMED: **0x4403b0** sets 1/m and **1/I with I = m*(a^2 + b^2)/12**, a
  box inertia. The vehicle constructor 0x410bc0 passes mass f2, **length f3**
  and **width f4** (and 0.2).
- **dt** is the clock's `+0xc` (0x441cd0), 0.1 when that is zero.

### The shipped model, ported and verified (session 12, in progress)

- CONFIRMED at runtime init: the sine table has **4096** entries (0x431688
  pushes 0x1000; 0x450280 overwrites 0x49b010/0x49b014/0x49b018). Headings are
  **4096 units per turn**. The 1024 / 0x3ff in the notes above are the file's
  static defaults.
- The shipped model (0x4269c0 keys, 0x40fc30, 0x40f0d0, 0x40e9f0, 0x40f3c0,
  0x40ebd0/0x40ece0/0x40ee80, integrator 0x4404a0) is transliterated in
  `port/src/drive.c` and checked by `reverse-engineering/tools/diff_drive.py`
  (Unicorn runs the originals on the same random states). Over 1000 states,
  longitudinal, steering, suspension, integrator and keys match exactly; ride
  height differs in 2 of 1000 (near the airborne threshold, not yet examined).
- Notable: ABS adds 0.5 to the brake decel; the tyres upgrade doubles lateral
  grip; the suspension shake is +-4*v*dt with a random sign; the ride-height
  prediction uses `(sin+cos)*v_fwd` for z (kept as in the original).

## Skidmarks and tyre smoke, exactly (session 12)

### Skidmarks (gfx_skid.cpp)

- CONFIRMED: **20 strips** of 0x1910 bytes at 0x4ae3d4. Each holds two tracks
  (left and right rear wheel, at 0x4acad4 and 0x4ad754 within the record) of
  up to **100 vertices** of 32 bytes, with a shared count (`+0x4acad0`).
- CONFIRMED, the lay (0x41dd00, every frame, the player only): nothing below
  intensity 4. If the previous frame's intensity (0x4cc014) was below 4, a
  free strip is started (count 2, lifetime `20/dt` frames at +0x4ae3d8). Its
  index is kept in 0x4892dc, and with no free strip the previous intensity is
  forced to 3 so it retries. Otherwise the current strip gets one more
  cross-section per wheel while `count + 2 < 100`.
- CONFIRMED, the points (0x41d9b0 -> 0x41d880): in the car's frame, at the
  **rear axle** (`-10 * f6` forward) and **+-7 units** sideways, then **+-2
  units across the direction of motion** (`(-v_fwd, v_lat)/|v|`, with |v|
  including the vertical component), 1 unit up, through the object matrix.
  So a mark is a 4-unit (0.4 m) band laid across the actual motion.
- CONFIRMED, the look: vertex colour **0x404040**, alpha `clamp(200 - 16 *
  (intensity - 4), 0, 255)` (0x41da70), texture `S_SFX.PCX` at u 0.453 /
  0.539 across the mark and v 0.15625 (0x41dad0, a grey tread strip), drawn
  with **SRCBLEND = INVSRCALPHA, DESTBLEND = SRCALPHA** (0x41e44d), so a mark
  darkens the road by `(255 - alpha)/255`: a harder skid is darker.
- CONFIRMED, the fade (0x41e1e0, with Game.ini `fade_skmarks`): every frame
  each vertex's alpha rises by `max(1, (255 - alpha) / frames_left)` toward
  255, and the strip is freed when its 20 s are up (0x41e0a0).

### Tyre smoke (gfx_wolk.cpp)

- CONFIRMED (traced symbolically): 0x425a90 calls the spawner twice, at the
  rear axle (`10 * f6` behind the centre of gravity) **+-9 units** sideways
  and 3 up, with velocity **0.6 x the car's world velocity**. The spawner's
  third velocity argument is `car_y + 3`, but the puff constructor
  **ignores** it.
- CONFIRMED (0x41fd80): a puff's horizontal velocity is scaled by a random
  0.75..1.0 per axis, its rise is `(rand() % 35 + 9) / frames` per frame,
  its lifetime is `4/dt` frames, and its size starts at 7.
- CONFIRMED (0x41fae0): per **frame**, not per second, velocity x 0.9,
  position += velocity, size += `29/frames`, and alpha -= `max(1, 0x50 /
  frames)`. That last is an *integer* division, so in practice one per frame.
  Because the motion is per frame, at a high frame rate the puffs outrun the
  car.

### The port's view was mirrored (settled, session 12)

In the original, positive steering (the RIGHT key) **decreases** the heading.
The port first had to flip the steering input to make right turn right. A side-by-side
capture now settles why: **the port drew every stage mirrored left to right.**

- CONFIRMED (runtime): at the etappe1 start, the original under Wine
  (RESOLUTION=3, Xvfb 640x480x24) shows the gantry banner reading
  **START/FINISH**, the opposite road and the tree line on the **right**. The
  port at the same chunk drew the banner as `HSINIF\TRATS` with the road and
  trees on the **left**. (Captures: o2/o3 against p1, session 12 scratch.)
- CONFIRMED (arithmetic): the original's car frame has lateral axis
  (cos psi, sin psi) and forward (-sin psi, cos psi) in world x/z (the integrator
  0x4404a0). In Direct3D's left-handed view the lateral axis is screen right.
  The port's GL camera has forward (sin yaw, -cos yaw) = (-sin psi, cos psi), and
  its screen right is forward x up = (-cos psi, 0, -sin psi). That is the
  **negated** lateral axis, so the image was mirrored.
- The fix is in the projection: `glScalef(-1, 1, 1)` after `glFrustum`, plus
  `glFrontFace(GL_CW)` because the reflection had been hiding Direct3D's
  clockwise front faces. With that in place the keyboard maps straight through
  (LEFT -> the original's LEFT bit). The autopilot keeps its sign flip because
  it steers toward the camera-right vector, which is the model's -lateral.
  Object-sound panning used the same mirrored "right" and was flipped too.
- Correction: the earlier note that "the Trabant's licence plate reads the right
  way round" in the port was a misreading. At 11 pixels high the plate cannot
  be read either way. Pixel comparison shows the plate before and after the fix are
  exact mirror images (RMSE 6 flipped against 657 direct).

### The player's start lane

- CONFIRMED: the player is created at 0x425709 by the vehicle constructor
  0x410bc0 with chunk `STARTCHUNK - [0x4d9e60]` and **lane 4** (the `push 4` at
  0x4256f0 / 0x425705). The constructor passes it to **0x442150(chunk, lane,
  side)**, which clamps the lane to `count - 1`, with the count at chunk +0xE8
  for side 1. It then returns that entry of the side's lane list (+0x154 for side 1).
- At etappe1's start the side-1 list runs left to right in world terms
  (dumped waypoints at chunks 35-37 step along +lateral). So "lane 4" is the
  **outermost lane on the right**, which matches the Wine capture: the car
  stands against the right-hand guardrail. The port had used the middle lane.
  It now takes `min(4, lanes - 1)` of its own lane list, which is sorted by
  lateral offset. LIKELY the same entry as the raw list wherever no off-road
  waypoint is involved.

### The accelerated game's sky is a still backdrop, not the panorama

- CONFIRMED: the race setup at 0x4234e0 tests 0x4d9aac. When it is set (the
  Direct3D path), it calls `rand() % 7` (0x42356f..0x42357a) and passes
  `back<N+1>.pcx` from the pointer table **0x48a790** to **0x45ce90**. That
  function loads the texture (0x45a460), builds a material from it (0x44f1a0)
  and keeps the handle in 0x49b02c ("setbkg: setting bkg material in
  viewport", VIEWPORT.CPP). **0x42a210** then calls `SetMaterial` (vtable +0xC)
  and `IDirect3DViewport::SetBackground` (vtable +0x20). 0x429c90 and 0x45ff3b
  re-apply it.
- CONFIRMED (runtime trace): a Wine debug.txt of the accelerated game opens
  `back7.pcx` and `back2.pcx` and never opens `SCRL_HI.pcx`. The `scrl_lo` /
  `scrl_hi` panorama is what the Raser.ini comment says it is, the
  **unaccelerated** sky. The port's scrolling sky came from the software path.
- `back1..7.pcx` are 128x128 cloud photographs. LIKELY Direct3D stretches a
  background-material texture over the viewport, so it stands still whatever
  the heading. The port now does that (`--sky 0` restores the panorama).
- Not settled: under Wine the background shows as a smooth blue-to-pink
  gradient that matches none of the seven images. The stage palettes are
  identical at the sky's indices, so it is not a palette swap. SPECULATIVE:
  Wine's ddraw does not draw viewport background textures, and the gradient
  is its own fallback. Wine captures cannot show the real sky.

## What spel.dat writes back to Game.ini (session 12)

All of spel.dat's ini traffic goes through four wrappers on `GAME.INI`
(0x4efb30 holds the selected file): **0x46aa30** write_string, **0x46aa70**
write_int (itoa base 10), **0x46aad0** read_int and **0x46ab10** read_string.
Every call site was listed with its string arguments.

### At start-up

- CONFIRMED: `[PLAYER] finished=0` is written straight away (0x421b59). A quit
  or a crash therefore reads as "not finished" to the menu.
- CONFIRMED: `[PLAYER]` **money** (0x4228dc, through the setter 0x428150,
  clamped 0..999999 into 0x4d9e38), **copland** (0x4d9e0c), **position**
  (0x4d9e60) and **damage** (0x42296a, through 0x4281c0, clamped 0..100 into
  0x4d9e30), then `DAMAGE_MESH` / `DAMAGE_VALUE` (eight values each, asserted).
- CONFIRMED: **position is a grid offset.** The player is created at chunk
  `STARTCHUNK - position` (0x4256e0 / 0x4256f5, both DIRECTION arms). This is
  what the Game.ini comment "Start position of player (same as last finish
  position)" means: finishing 3rd puts you 2 chunks back next time.
- CONFIRMED: `[BEST TIMES] LAP_RECORD_E<n>` and `RACE_RECORD_E<n>` load into
  0x4d9ac8 / 0x4d9acc. Absent or 0 becomes **0x0FFFFFFF** ms (0x422268).
  `TIME_CHECK_COUNT_E<n>` (asserted 0..9) and the times load into 0x4dc9f0.

### During the race

- CONFIRMED: **position** 0x4d9e60 = fcn 0x408c00(), the number of racers
  ahead (0-based), recomputed every frame (0x409051) until `finished` is set.
- CONFIRMED: at the finish line (0x4299b1) the track time is read from clock 3
  into 0x4d9e5c (0x4283e0). If it beats RACE_RECORD it replaces it. Then the
  mode becomes 3 (the cheering camera sequence) and `finished` 0x4d9e54 = 1.
  An arrest or the clock running out never sets it.
- CONFIRMED: mode 3's end callback **0x415690** adds the prize,
  **0x4280c0**, then `[0x4dc964] * 2000` (the radar-bonus count, "The radar
  bonus is for knocking the camera over"), and switches to mode 4. The prize by
  0-based position is **55000 / 35000 / 15000 / 0**. It is ×4 on etappe 6
  (the final stage), and last place pays **5000** when Difficulty is 0
  (EASY, 0x4d9ab0). These are the only two calls to the money adder 0x428180,
  so a race can only earn money in spel.dat. Nothing in it charges any.
- CONFIRMED: lap completion (Tr_finis.cpp, 0x429860): a lap faster than
  LAP_RECORD becomes the record (banner 31, else 45). The lap's checkpoint
  times (0x4dc9c8) are copied to the saved set **only** then. The saved count
  0x48c88c is set to the lap's count on every lap. The checkpoint handler
  (0x42a5b2) stores lap-clock times only on circuits, and the shipped circuits
  carry no checkpoints, so the saved count is always 0.

### At exit

deinit_game **0x421af0** calls **0x422d30** first, which runs:

1. **write_player_info 0x422ec0**: `finished` always. Only when finished:
   `money`, `damage`, `tracktime` (ms), `position`, and `DAMAGE_MESH`
   (`%d,...` ×8, 0x4d9e94) / `DAMAGE_VALUE` (`%f,...` ×8, 0x4d9e74), which
   come back unchanged because the regional damage never runs.
2. **racers 0x422d40**, only when finished. First **0x4114a0** settles the
   results:
   - **Extrapolate.** A racer that has not finished takes its clock (8, 9
     or 10) and adds `(t / (total - gap)) * gap` in integer ms. `total` is
     chunk count × laps (0x4e65e4 × 0x4d9ad8), and `gap` is the player's
     progress 0x4d9e70 minus the racer's.
   - **Break ties.** Equal times are split by +1 ms, slot 0 against 1, 0
     against 2, then 1 against 2.
   - **Rank.** Each racer's position is the number of racers with a smaller
     time, plus one if that is ≥ the player's position.

   Then for each of the **three** slots at 0x485b20 (stride 0x58: +0 car,
   +4 time, +8 position, +0xc vehicle, +0x10 finished) it writes
   `<i>=car,position,time` and `DAM<i>` / `VAL<i>`. Those arrays are zero in
   the static data and no code changes them.
3. **best times 0x422c10**, only when finished: `LAP_RECORD_E<n>`,
   `RACE_RECORD_E<n>` (0x0FFFFFFF if never set), `TIME_CHECK_COUNT_E<n>`
   (clamped to 0 when outside 0..9) and `TIME_CHECK_E<n>_C<i>`.

Raser.ini `[PROGRESS]` (COUNT, percent<i>, written at 0x41c37c) is only the
loading bar's calibration (gfx_pbar.cpp) and has nothing to do with results.

### The menu's side (first look)

menu.dat's ini wrappers are **0x410a10** (select file), **0x410ae0**
write_int, **0x410b40** read_int and **0x410b80** read_string. It reads
`finished`, `money`, `tracktime`, `position`, `damage` and the `racers` rows
back (`ini_io.cpp`, 0x401d30 / 0x401ff0 / 0x401ce0 via 0x402390). It writes
`etappe`, `Car`, `COPLAND`, `money`, the INPUT keys and the Raser.ini sound
and resolution settings. It also keeps its own **menu.ini** (`start_budget=35000`,
`save_game`). How it turns a result into the next stage, and when it plays
win.smk / lose.smk, has not been read yet.

### The menu's career file, SLOTS.RAS, and a full loop with the port (session 12)

- CONFIRMED: menu.dat keeps the careers in **`SLOTS.RAS`** (slotio.cpp:
  `load_slot`, `save_slot`, `init_slots`). menu.ini `save_game` names the
  active slot (2 in the shipped file), and `start_budget=35000` is a fresh
  slot's money. The file is **9 slots × 536 (0x218) bytes = 4824**. In memory
  it is the player table indexed `slot * 0x218` from 0x43ec28 (the code
  addresses fields as 0x43ec30 + ...).
- Slot layout. The offsets are CONFIRMED by a before/after diff; the
  names are LIKELY where marked. The struct is packed and little-endian:

  | offset | size | field |
  |---|---|---|
  | +0x08 | string | **player name** (CONFIRMED by the win test below; empty in every shipped slot) |
  | +0x16 | 4 | **etappe**: the next stage of the career |
  | +0x1e | 4 | **money** (35000 fresh) |
  | +0x2b | 4 | **damage** |
  | +0x3b | 7 × 4 | controls: LEFT 37, RIGHT 39, UP 38, DOWN 40, EXTRA1 17, EXTRA2 18, sensitivity 50 |
  | +0x58 | 8 × 56 | **standings**: TOTAAL then the seven stages (LIKELY order). Each is 4 rows of 14 bytes: car (1), name (9, "racer N" or the player's), time in whole seconds (4) |

  0x58 + 8 × 56 = 0x218, so the slot is fully accounted for apart from the
  bytes before +0x16 and the upgrade flags (not seen set yet).
- CONFIRMED by runtime, menu.dat under Wine:
  1. A Game.ini written by **the port** after an etappe1 race was handed to
     the original menu (slot 2 fresh, `finished=1`).
  2. The menu showed Schade 7 % and FL 812000 from it, and the STANDEN screen
     listed racer 3 04:09, racer 1 04:11, racer 2 04:12 and the player 04:14.
     Those are the port's 249991 / 251624 / 252224 / 254392 ms, truncated to
     seconds, and the car pictures match the indices the port wrote.
  3. "Volgende etappe" moved from stage 0 to 1 (finished advances the career
     whatever the position; see Q49).
  4. Pressing START made the menu save the slot and write Game.ini (`etappe`,
     the upgrades MOTOR/BANDEN/ABS/TURBO/VERSNELLINGSBAK from the slot, here
     all 0), then exit with **40**, the code a2_II.exe answers by launching
     spel.dat.

  So the port can stand in for spel.dat in the original game loop.
- Note: Wine's WritePrivateProfileString rewrites the whole file, dropping
  blank lines and trailing whitespace. That is Wine, not the game.

### When the Smacker videos play (session 12)

- CONFIRMED: menu.dat builds the video names from bare strings (`lose`,
  `win`, `intro`, `exit` at 0x434204..0x434218) through one player, 0x409f70.
  That is why no binary contains `win.smk` as a literal. The wrappers are
  0x40a080 lose, 0x40a090 win, 0x40a0a0 intro, 0x40a0b0 exit. Raser.ini
  `[MOVIES] SIZE` (0x4341f8) picks full screen or a centred 320x240 window.
- CONFIRMED: **intro** plays when the menu is started with `\FIRST` or `/FIRST`
  on the command line (0x409b2a..0x409b6a). That path also skips reading the
  last race's results. **exit** plays when the menu loop returns 0xff, i.e.
  quitting (0x409bbe).
- CONFIRMED: **win / lose** play only at the end of a career (0x407580). After
  a finished race on the last stage, 0x409870 sets 0x43e70c. The menu then
  string-compares the name in **row 0 of standings group 0** (TOTAAL, slot
  +0x58: the overall leader) with the slot's player name at +0x08. If they are equal it plays
  win.smk, otherwise lose.smk. Then it sets the slot's etappe to 0, so the
  career starts over, and shows the main menu again.
- The standings indices are bounds-checked as group < 8 (0x40b060) and row < 4
  (0x40b090), and a row is 14 bytes from 0x43ec80, matching the SLOTS.RAS
  layout above.

### menu.dat rebuilt (session 12)

The front end is now documented screen by screen in
**reverse-engineering/menu.md** and rebuilt as `a2menu`. Headline facts, all
with addresses there:

- CONFIRMED: menu.dat is data-driven: Forms.cpp form tables (28-byte records
  {x, y, type, descriptor, state, 0, 0}), an animation table at 0x433e60
  (frames are MENURC index + 1), a car table at 0x42df54 {anim, name[40],
  index}. Animated buttons take their sprite animation from the button id.
- CONFIRMED: text is drawn at even x; get_centered_text centres in
  (x2 - x1 + 1); the set-colour call replaces a font's own ink index.
- CONFIRMED: the requester and HELP darken the screen by blacking every
  pixel with (x + y) odd.
- CONFIRMED: garage prices / upgrade effects / car values come from garage.ini
  with the formulas in menu.md (car value = price / 2 - repair * damage +
  half of each owned part; bars as percents of 100..300 km/h and 0..100 %).
- CONFIRMED: options.ras layout and defaults; ini_write_options writes
  SFXVOLUME / MUSICVOLUME = 3100 - 30 * volume (0 when off) at START.
- CONFIRMED: the standings are rebuilt from per-stage tables by driver name;
  win.smk / lose.smk by who leads TOTAAL after the last stage.
- CONFIRMED: Ctrl + Space + M on opening the main menu sets the budget to
  700000 (0x4076ea).

### Menu focus is kept in the form records (session 13)

- CONFIRMED (disassembly): set_control_state 0x410ec0 stores a control's
  state in its form record (+0x10, the previous state in +0x14). States: 1 = SetFocus,
  2 = disabled, 3 = enabled, 4 = pressed. draw_button 0x41b41c rewrites the state
  on each owner-draw (focus 1, focus+selected 4, disabled 2, else 0), and
  create_button 0x41b2be reapplies it, with 4 turned into 1, when the
  screen is built again.
- Consequence (CONFIRMED against the Wine capture f0_main): a screen opens
  on the button it had focused when it was left (GARAGE after returning
  from the garage). A new menu.dat process starts from the form's state-1
  defaults again.
- The port (a2menu) keeps a per-screen focus and clears it when it comes
  back from a race. The main screen after a garage round trip matches the
  capture's button column.

## Input devices (session 13)

- CONFIRMED: init reads `Game.ini [INPUT]` at 0x423910. set_keys 0x424490
  stores LEFT, RIGHT, UP, DOWN, EXTRA1, EXTRA2, 0, 0 at 0x4d8e60..0x4d8e7c,
  binds them to joystick 1's eight slots, and keeps SENSITIVITY in
  0x4d8e88. set_device 0x424590 (table 0x4246b0) sets the mode 0x4d8e80 and
  the source 0x4d8e84 (1 keyboard, 2 joystick; 0x466960 tests
  GetAsyncKeyState only for source bit 1 and the joystick only for bit 2):
  | DEVICE | mode | source | per-frame handler (0x42614d, table 0x42658c) |
  |---|---|---|---|
  | 1 keyboard | 1 | 1 | 0x4265b0 |
  | 2 joypad | 2 | 2 | 0x4269c0 |
  | 3 wheel/joystick | 3 | 2 | 0x426e00 |
  | 4 Sidewinder FF | 4 | 2 | 0x426f50 (DirectInput 0x4e4810) |
- CONFIRMED: 0x4265b0 and 0x4269c0 have identical throttle, brake and
  steering arms (an instruction diff shows only stack offsets). 0x4269c0 adds
  two things: the Space handbrake and turbo-implies-gas. So the keyboard has
  neither. Runtime check under Wine: holding only Ctrl from the grid for 6 s
  left the speed at 000.
- CONFIRMED, joypad: set_device(2) rebinds the slots (0x4245d2):
  | control | key |
  |---|---|
  | stick left | LEFT |
  | stick right | RIGHT |
  | stick up | none |
  | stick down | DOWN |
  | button 1 | UP |
  | button 2 | EXTRA1 |
  | buttons 3-4 | none |

  EXTRA2 is not bound to any control. The poll 0x46ebc0 calls joyGetPosEx with
  JOY_RETURNX|Y|BUTTONS. 0x46eac0 marks a direction past 10 % of the
  calibrated centre (`x < 0.9c`, `x > 1.1c`).
- CONFIRMED, wheel (0x426e00, the stuur_model 0 arm 0x426e88):
  get_joystick_position 0x46ed90 / 0x4247f0 gives x and y on 0..100. Inside
  ±10 % of the centre the value is 50. Outside it, raw / ((max − centre)/50),
  with Y reversed. Then:
  - steer 0x4d9cd8 = 2x − 100;
  - y ≥ 50: throttle 0x4d9cdc = 2y − 100 and brake 0;
  - y < 50: brake 0x4d9e08 = 2(50 − y) and throttle 0;
  - button 1 is the handbrake (bit 0x10) and button 2 the turbo (0x20). The
    turbo does not imply gas.
- CONFIRMED: SENSITIVITY builds the 0x4d9c80..0x4d9d70 table at 0x42584c with
  k = (100 − s)·3/100 + 1. Only the stuur_model 1 arm of the wheel (0x426e0d)
  and the FF handler read the table, so **sensitivity does nothing in the
  shipped game** (stuur_model 0). The FF handler 0x426f50 has no stuur_model
  test. The menu never writes DEVICE 4 (its tiles give 1/2/3/3).
- The port (a2view) follows the keyboard, joypad and wheel handlers with SDL
  joystick 0. It treats the axis centre as 32767; the original reads the
  centre from calibration. DEVICE 4 falls back to the keyboard.

### Menu sliders are a stretched Blt (session 13)

- CONFIRMED: draw_slider 0x40b880 stretches the fill sprite (anim 42) onto
  the slider's current length with one IDirectDrawSurface::Blt between two
  rects (0x4122e0). Earlier we guessed scrolling through 0x40be30, which is
  in fact showroom-only. The full geometry and the drag formula are in
  reverse-engineering/menu.md "Sliders". The port's options sliders now
  match the Wine capture with 0 differing pixels.
- CONFIRMED: the controls screen's sensitivity slider (hot area 7, slot
  +0x53) is drawn only when a joystick or wheel is chosen. LIKELY the same
  holds for dragging it: 0x4037ed tests 2 / 3 before the drag. Its value has
  no effect on the race (see "Input devices").

### The last SLOTS.RAS fields (session 13)

- CONFIRMED by every reference in menu.dat:
  - +0x04 is the play order (0x40b710 / 0x40b780 / 0x40b820);
  - +0x1a is written with 0 only, and nothing reads it;
  - +0x57 is the standings tab last shown.

  Details are in reverse-engineering/menu.md. a2menu now keeps all three as
  the original does: a tab chosen on the standings screen is still selected
  after leaving and coming back.

### The showroom slides the cars (session 13)

- CONFIRMED (0x40be30, and a Wine capture taken mid-slide): the showroom
  arrows run a 20-step, sine-eased horizontal slide. The old car leaves on
  one side and the new car comes in from the other, both still turning on
  the turntable. The texts change afterwards. a2menu reproduces it, blocking
  input as the original's loop does (`--slide-step N` freezes a frame for
  `--shot`).

## Knock-over props: pylons, barriers and speed cameras (session 13)

- CONFIRMED: 0x429310 registers a handler per `.omp` type with 0x441f20
  (table 0x48c2b4 of {type, handler, extra}, stored at 0x4e5dd0):

  | type | objects | handler |
  |---|---|---|
  | 1 | assorted | 0x429380 |
  | 2 | pylons | 0x4293f0 |
  | 3 | barriers and fences | 0x4297d0 |
  | 4 | traffic lights | 0x429480 |
  | 5 | columns | 0x429380 |
  | 6 | — | 0x4295d0 |
  | 7 | cameras | 0x428d30 |
  | 8 | arrows | 0x4296b0 |
  | 9 | — | none |
  | 10 | police placements | 0x4293a0 |

- CONFIRMED: the static-object loader (0x4453bb) looks up the handler and
  makes every `.omp` object a live object (0x43f770, stride 160 at
  0x4e48e0) with the handler at +0x44. The per-frame loop 0x4408fc calls
  +0x44 for the objects whose chunk is inside a window, or whose +0x40 flag
  is set. Type 1 and 5 handlers only play an object sample, so **no type
  handler pushes the car**; the props never block it.
- CONFIRMED, the knockable types:

  | type | hit: mass, sample | bounce sample | settles at pitch, lift |
  |---|---|---|---|
  | 2 pylons | 4, 16 pionb1 | 17 pionb3 | 0x4fb, 4.0 |
  | 3 barriers | 8, 18 paal | 19 paal | 0x400, 2.0 |
  | 7 cameras | 10, 18 paal, plus 0x4dc964 += 1 and banner 51 | 19 paal | 0x400, 4.0 |

- CONFIRMED, the contact test 0x446930 (via 0x429350, only for the
  player 0x4d9e28): the centres are closer than the sum of the two meshes'
  +0x74. +0x74 is sqrt(hx² + hz²), the horizontal half-diagonal of the
  vertex bounding box (0x455f20, stored at 0x4560f8).
- CONFIRMED, the launch 0x428ea0 (object_flyup_check_collision):
  - a = the player's heading + 125 − rand()%250 (4096 units a turn);
  - with the player's v_lat (+0x6c) and v_fwd (+0x74):
    - v.x = cos a·v_lat − 1.5·sin a·v_fwd;
    - v.z = sin a·v_lat + 1.5·cos a·v_fwd;
    - v.y = (v_lat + v_fwd) / mass;
  - gravity +0x30 = 1 and phase 1. Sin and cos are the tables 0x4ea418 and
    0x4ea4b4 (filled at 0x45034e / 0x450367).
- CONFIRMED, the flight 0x429020, once per frame with no dt:
  - v.x and v.z × 0.98, v.y −= gravity, position += v;
  - ground = 0x442f00 at the new position. When (y − ground)² ≤ r²: v.y × −0.6,
    y = ground + 1, gravity × 0.5, return 1. If v.y < 2 the prop settles:
    y = ground + lift, pitch = the settle value, roll 0, phase 2;
  - then pitch += 64, yaw += 32 and roll += 48 (4096 units a turn).
- CONFIRMED: +0x14 is the pending sample. 0x428bc0 plays it through the
  20-slot positional object sounds with loop 0 (0x428ac0) and clears it
  when it ends, when the prop is beyond 2500 units, or when no slot is free.
- The port (render.c `a2_scene_props_step`, session 13) implements types 2, 3
  and 7 as above and steps them once per frame, as it does the smoke. Port
  choices:
  - the chunk window, −5..+15 around the player;
  - off the road, the ground under a flying prop is its own starting height,
    because the port's road height covers the road only.

  On the etappe5 autopilot run the car clipped a camera on the median at
  255.7 s: +2000 and RADAR BONUS.

### Which scenery blocks the car (session 13)

- CONFIRMED: moving objects collide through 0x440fb0 → 0x44a0f0, with
  candidates from two sources:
  - the world polygons of the object's chunk and the one before it
    (0x449c10, the `.cmp` rails);
  - a 500-entry list of registered objects at 0x4e4988, taken when they are
    within 10 chunks (0x44a088 → 0x449a30).

  0x4426c0 registers an object there. The static loader calls it only for
  `.omp` types 5 (columns), 8 (arrows) and 10 (police placements), right
  after 0x43f8a0 with (0, 0.1, 4, 1), (13000, 0.2, 4, 1) and
  (1300, 0.2, 4, 1); LIKELY mass first. Type 9 goes through 0x40c920, which
  registers it as a cop at 0x40ca2a. The vehicles register through
  0x410dbc and 0x432cad.
- So **lamp posts, trees and the rest of type 0 never block the car**, and
  neither do pylons, barriers or cameras (they are knocked over instead).
  This corrects the port's earlier rule, its own invention, that any tall,
  narrow scenery mesh was solid.
- The port now makes only types 5, 8 and 10 solid. It cannot push the arrows
  or the parked police cars the way the original can, so objects within 25
  units of a lane waypoint stay passable (port choice).
- CONFIRMED (session 13, with the bbox routine 0x455f20 run under Unicorn
  on known vertices): a registered object's collision entry (0x449a30,
  76-byte entries at 0x4e6790, up to 200) holds:
  - four corners of the mesh's bounding box, bbox +0x4 / +0x10 / +0x40 /
    +0x34, which are corners 0, 1, 5 and 4: the bottom face, i.e. the full
    XZ rectangle at minimum y;
  - the object's position, its heading rounded to an integer, and the mesh
    radius.

  So the shape is the mesh's XZ bounding rectangle, turned by the object's
  heading. The port now tests that rectangle against the car's circle (it was
  a circle of half the smaller extent).
  The bbox struct is {flag 1, 8 corners × 3 floats, +0x64 unused, +0x68
  radius}, corner k at +4 + 12k: (minx, miny, minz), (maxx, miny, minz),
  (maxx, maxy, minz), (minx, maxy, minz), and the same four at maxz.

### Traffic-light and arrow timing (session 13)

- CONFIRMED: the type-4 handler 0x429480 sets every traffic light's anim
  frame (transform +8) from one global state, 0x48c484:
  | state | frame | colour |
  |---|---|---|
  | 0 | 2 | red |
  | 3 | 0 | green |
  | 2 | 1 | amber |

  The state steps on timer 1: red 5000 ms, then green 5000 ms, then amber
  500 ms (0x42951d). 0x4295a0 starts the cycle red.
- CONFIRMED: the type-8 handler 0x4296b0 damps an arrow's motion by 0.95 per
  frame and flips 0x4dc96c on timer 7. The arrow shows frame 0 for 1000 ms
  and frame 1 for 500 ms.
- The port's earlier 6/2/6 s cycle and 0.5 s flash were guesses; they are
  replaced by these values.

## Traffic lane choice and following (ai_lane.cpp, session 13)

- CONFIRMED: `change_lane` (0x403660) runs on every tick of
  HANDLE_TRAFFIC_AI (0x40b8e7). For each lane of the vehicle's chunk,
  scan_lane (0x403f60) fills a t_lane_info:
  - a dead end (0x4040b0);
  - the nearest vehicle on the first waypoint ahead that holds one, up to
    **10 waypoints** ahead (ai record +0x10, set to 10 at 0x406a13; link
    +0x14);
  - the same behind, up to **2 waypoints** (link +0x20).

  find_obstacle (0x404600) takes the vehicle on that waypoint that is
  nearest in 3D, and the range is that distance in world units.
- CONFIRMED, statuses (0x404720):

  | status | meaning |
  |---|---|
  | 0 | nothing |
  | 1 | the obstacle is faster than us (+0x74) |
  | 2 | it is not faster |
  | 3 | dead end, 2 or more waypoints away |
  | 4 | dead end, nearer |
  | 5 | oncoming (forward vectors dot < 0) |

- CONFIRMED, the traffic chooser PICK_LANE (0x404960). The arguments are
  (object, lane count, info, current lane, "illegal lane" flag); the
  highest lane index is the slowest lane. Two tests recur:
  - **rear-ok**: nothing behind, or the one behind is more than behaviour
    +0x1c away;
  - **front-ok**: the one ahead is at least +0x24 away.

  The decisions:
  - **In the slowest lane:**
    - free or faster ahead: stay;
    - slower ahead: move left if the left lane is free with rear-ok, faster
      with front-ok and rear-ok, or slower but faster than us with front-ok
      and rear-ok;
    - brake, stop or ghost ahead: move left on the same tests, without the
      faster-than-us case.
  - **In any other lane:**
    - if blocked (status 2 or higher), move left when that lane is free with
      rear-ok, or faster with front-ok and rear-ok;
    - otherwise, return right when the right lane is free with rear-ok, or
      faster with front-ok and rear-ok.
  - **With the illegal flag set:** move to lane − 1 on similar tests. Ghost
    there gives lane − 2.

  The switch retargets the waypoint to the next lane (0x406d90 left,
  0x406d10 right).
- CONFIRMED, the speed it then sets (0x4055c0, into ai_chosen_speed
  +0x1c; while changing lane, the mean of the old and the new lane, 0x403e38):

  | front status | speed |
  |---|---|
  | free or ghost | preferred (+0x14) |
  | faster or slower, range < +0x20 | 0.75 × the obstacle's speed |
  | faster or slower, range < +0x24 | the obstacle's speed |
  | faster or slower, further | preferred |
  | brake | 0.75 × the current speed, at least 2 m/s |
  | stop | 2 m/s |

  calc_speed then starts from the chosen speed.
- Behaviour values (emu_ai.py):

  | behaviours | +0x1c | +0x20 | +0x24 |
  |---|---|---|---|
  | traffic 2 and 3 | 200 | 200 | 300 |
  | racers 7–13 | 80 | 75 | 85 |

  So traffic slows to three quarters of the vehicle ahead inside 20 m and
  matches it inside 30 m.
- The port (traffic.c) now follows this every tick, with the player and the
  racers as obstacles too. Port choices: no dead ends; "ahead" within one
  chunk is decided by segment progress; the change is the port's 1.2 s
  slide; the illegal-lane case is not used. LIKELY the port's lane order
  (sorted outward from the centreline) matches the original's, which puts
  the slowest lane outermost.

### The racers' lane choice, race_lane (session 13)

- CONFIRMED (0x404ee0): from the same t_lane_info as traffic, a racer:
  1. keeps the lanes with room behind (nothing there, or further than
     behaviour +0x1c);
  2. keeps those that are free ahead, or whose vehicle ahead (status 1 or 2)
     is further than +0x24;
  3. with none left, stays in its lane;
  4. otherwise starts from the last candidate and takes the lowest front
     status. For equal status 1 or 2 it takes the longer range. So among
     free lanes the outermost wins, and a free lane always beats an
     occupied one.
- Racer behaviours 7–13: +0x1c = 80, +0x20 = 75, +0x24 = 85 (8 / 7.5 /
  8.5 m). change_lane gives them the same follow speed (0x4055c0): three
  quarters of the vehicle ahead inside 7.5 m, its speed inside 8.5 m.
- The port (racers.c, with the shared scan in ai_lane.c) now does this every
  tick, against the other racers, the traffic and the player. Before, the
  racers kept their grid lane all race and drove through traffic. Port
  choice: a change slides over 1.2 s, as traffic's does.

## The police are the placed cars, and the catch is a block (session 13)

- CONFIRMED: the `.omp` **type-9** objects are the police. The static
  loader passes them to 0x40c920. It finds the nearest waypoint and lane
  (0x443da0, 0x402520), sets up physics (0x43f8a0), gives the object a car
  entry of kind 4 (0x411120, LIKELY "cop"), registers it for collision
  (0x4426c0) and creates its AI with **behaviour 14** (0x406800):
  preferred 70 m/s, accel 12, decel 18, brake 58. A stage has 3
  (etappe3) to 17 (etappe0, etappe6). Types 10, 21 and 43 are parked
  police cars without AI.
- CONFIRMED, HANDLE_COP_AI (0x4027a0, states from the table 0x4029fc):
  - **Lookout** (0x402a10): the cop stays parked. When |target chunk − cop
    chunk| < 2 and the offence check 0x403130 passes, the pursuit starts:
    state 1, behaviour 14, maximum 70 m/s, siren on (0x40c640(obj, 1)).
  - **Pursuit** (0x402b40): +0x0c and +0x10 both add frame time; +0x10 ≥ 40
    ends the chase (state 3, behaviour 2 at 25 m/s, siren off).
    - **Ahead of the target**, on the other side: maximum 0.8 × the
      target's speed.
    - Ahead, same side, other lane: change lane (0x406d90 on both arms) and
      maximum 0.5 × the target's speed.
    - Ahead, same lane, **exactly one chunk ahead**, +0x0c > 5 and game
      mode 0: **caught**. The cop takes the target's speed, the siren goes
      off, 0x424d20(8) fires, and every other cop chasing the same target
      quits.
    - **Behind by more than 5 chunks**: the behaviour's preferred speed
      ("Speeding...").
    - Behind by 5 or fewer: 1.25 × the target's speed, at least 15
      ("Approaching...").
  - **Got him** (0x4028b2): the cop's maximum becomes 0.5 × the target's
    speed (0x4028e3; this is the cop's own +0x18, not the player's speed)
    and 0x425980 runs the catch. Then state 3: behaviour 2 at preferred 25
    (0x4a5b88 + 0x98), change_lane with 2 lanes.
  - **Quitting** (state 3): change_lane with 4 lanes and normal driving. No
    path leads back to the lookout, so each cop chases once.
- The port (cops.c) now follows all of this. Before, it spawned one cop 8
  chunks behind, caught within 110 units, chased at an invented 60 m/s and
  halved the player's speed on a catch; all of that is gone. Not modelled:
  the side check (one carriageway), cop_lane while driving, and pushing the
  parked type-10 cars.
- CONFIRMED, **a catch pulls the player over**:
  - right after the catch, 0x4258f0 (called at 0x402e5f) gives the player's
    car an AI driver, behaviour 0 on its current waypoint (0x406800), clears
    player +0x38 and sets 0x4d9e64 = 1;
  - while 0x4d9e64 is set, the player's per-frame update drives the car with
    handle_basic_ai (0x426004 → 0x4076b0);
  - the catch also starts game mode 8, the warning sequence: banner 49/50,
    the voice line, a camera switch (0x4151f0) and a 4 s hold (0x415420);
  - during mode 8 the cop's "got him" arm skips its body (the
    `[0x4d9a7c] == 8` test at 0x4028f7). When the sequence ends it runs
    0x425980, which clears 0x4d9e64 and +0x38 and gives control back, and
    then the cop quits;
  - the cop in front holds half the speed it caught the car at.

  The port drives the car down its lane to that half speed for the 4 s.
  LIKELY, not exact: the player's AI follows the cop ahead with the 0x4055c0
  rule. The third catch still goes to the arrest (mode 6).
- The port's driving cops now also run change_lane's cop arm: cop_lane
  (0x4051f0), and the 0x4055c0 follow speed with behaviour 14's ranges
  (+0x1c 115, +0x20 70, +0x24 80), or behaviour 2's after the chase, capped
  by the pursuit's maximum (calc_speed takes the minimum of the chosen speed
  and ai +0x18).
- CONFIRMED, the warning's camera (0x4151f0, called from the warning start
  0x415342): camera 6's slot ([0x4a7090], camera array 0x4e66a0):
  - looks at the player's car offset (0, 20, 0) (0x4477f0);
  - stands at the point of the chunk after the player's (chunk record +0x24
    via the 0x4e5d2c map), raised 20 units (0x4474f0);
  - then becomes the active camera (change_active_camera(6)).

  0x415490 restores the saved camera when the sequence ends. So the warning
  shows the pull-over from a fixed point just down the road. The port does
  this for the 4 s, using the `.pmp` chunk point (LIKELY the same point as the
  chunk record's +0x24).
- CONFIRMED, the offence check 0x403130, in order:
  1. **side:** the target's side from 0x402520 (its chunk and current
     waypoint +0x20) against the cop object's +0x28. If they differ, it is
     "Ghost!", an offence whatever the speed;
  2. **lanes:** prints only;
  3. **speed:** cop +0x14 × 1.1 < the target's +0x74.

  A cop's side is its placement's. Counted in the port (nearest waypoint
  list): every stage's cops stand by the right carriageway, except 3 on
  etappe2 and 1 on etappe4 by the left. Those start a chase whenever the
  player passes within 2 chunks. Seen on etappe2: cop 2 at chunk 297 chased
  as "Ghost!" and made the second catch.

## Racers and the service station (session 13)

- CONFIRMED: check_racer_trigger (0x40af90) runs when a racer enters a
  chunk (object +0x18 ≠ +0x1c, 0x40ad45). It dispatches the chunk's two
  trigger slots and the current waypoint's `.dmp` f1 through 0x40b030
  (byte table 0x40b294):

  | trigger | handler |
  |---|---|
  | 2 | BENZINE_STATION: maximum and speed 0, driver state 7 (refuelling) |
  | 9 | the zero-chunk marker |
  | 14, 15 | the finish triggers |
  | 46 | FUEL_APPROACH: state 5, maximum ½ the preferred speed |
  | 47 | FINAL_APPROACH: state 1, maximum ¼ the preferred speed |

  No track places 46 or 47, and trigger 2 sits on one chunk per stage (two
  on etappe5). change_lane's arms 5, 6 and 7 are the refuelling driver
  types: outer lane, race_lane, keep the lane.
- CONFIRMED by a Wine run (STARTCHUNK = 535 on etappe1, DEBUGON = 1): the
  three racers (objects 393-395 in debug.txt's per-frame waypoint lookups)
  pass the trigger-2 chunk 549 at a steady 42-44 units a frame. **They do not
  stop.** Why the BENZINE_STATION arm does not take effect is not
  established. The racing state rewrites the maximum every frame
  (0x40af1a), but the speed reset to 0 would still show, so LIKELY the
  handler is never reached for them on that chunk.
- The port's racers ignore triggers, which matches what the game does.

## The collision impulse (session 13)

This corrects "No collision impulse has been found" (session 8). That note had
looked only at player_car_collision. The impulse lives in the physics engine
and every collision goes through it.

- CONFIRMED (0x4403b0, init_physics): a mass below 0.1 makes an object
  immovable (+0x60 = +0x64 = 0). Otherwise +0x60 = 1/m and +0x64 = 1/I with
  I = m (a² + b²) / 12 for an a × b box. +0x68 is the coefficient of
  restitution, asserted to be within 0..1 ("Invalid coef. of restitution",
  obj_main.cpp). The callers:

  | object | mass | restitution | box |
  |---|---|---|---|
  | vehicles (0x410cdd) | table field 2 | 0.2 | fields 3 × 4 |
  | type-9 cops (0x40ca02) | | 0.3 | |
  | columns (type 5) | 0 (immovable) | 0.1 | |
  | arrows (type 8) | 13000 | 0.2 | 4 × 1 |
  | parked police (type 10) | 1300 | 0.2 | 4 × 1 |

- CONFIRMED by symbolic and concrete runs under Unicorn
  (reverse-engineering/tools/emu_collide.py, fit_collide.py, fit_wall.py),
  for the object-object response 0x448eb0 (called from 0x44981a with the
  record 0x4e6760: objects +0x08/+0x0c, contact +0x10, normal +0x1c, contact
  scaled by 0.1 to metres; velocities rotated by each heading, 0x448e30):
  - v = the contact point's relative velocity, v1 + ω1 × r1 − (v2 + ω2 × r2)
    with ω × r = ω (−rz, rx);
  - **j = −(1 + e) (v · n) / (M1 + M2)**, e = 0.5 (e1 + e2), stored at
    0x4e6788. Spin does not enter the denominator;
  - Δv1 = M1 j n, Δv2 = −M2 j n;
  - Δω1 = 0.34 I1 (r1 × n) j and Δω2 = −0.34 I2 (r2 × n) j, with
    r × n = rx·nz − rz·nx and the result clamped to ±6;
  - Δv_y = 1.5 M2 j on **both** objects, using the second object's M for
    both (LIKELY a slip in the original), unclamped.
- CONFIRMED, the world response 0x449570 (called from 0x449807): the same
  with M2 = 0 and the object's own restitution, j = −(1 + e)(v · n)/M with n
  the outward normal, and Δv_y = +1.5 M j. A 10 m/s head-on hit at e = 0.5
  comes back at −5 m/s and hops 15 units/s. An immovable object is left
  alone.
- So **the impact magnitude** the damage (18000 / 35000) and the crunch
  bands (18000 / 25000) read is this impulse in kg·m/s. A 600 kg Trabant
  needs about 25 m/s of closing speed into a wall before it takes damage
  (18000 / (1.2 · 600)).
- The port (main.c collide_impulse, session 13) now uses this for barriers,
  solid scenery and traffic. It replaces the bump-back and IMPACT_SCALE,
  which were the port's own. The car is the vehicle table's box, with the
  contact at the corner deepest along n.

  Port choices:
  - the original's contact point and normal come from its rectangle tests
    (0x441490), which are not traced; the port uses its own push-out normal;
  - the traffic's sideways motion does not move (columns are immovable in
    the original too). Later in session 13 the arrows and parked police cars
    became pushable: they take Δv2 and the spin −0.34 I2 (r2 × n) j (4 × 1 m
    box), move by their velocity, and are damped by their type handlers,
    ×0.95 a frame for arrows (0x4296b0) and ×0.9 for police cars (0x4293a0);
  - the damage uses |j|, because the object-object j comes out negative in
    the fits.

## Two AI models, and the shipped game runs the other one (session 13)

**Major correction**, the same trap as stuur_model. Much of this session's
police and racer work traced code the shipped game does not run.

- CONFIRMED: `Game.ini [OPTIONS] ai_model` ("Old (Autobahn Raser I) = 1,
  New (A2 Racer II) = 0", shipped **0**) is read at 0x421f4c into 0x4d9b10.
  - HANDLE_RACER_AI (0x40ac90, installed as the racers' object handler at
    0x41134e) runs its AI only when ai_model == 1 (0x40ac9f). Otherwise it
    goes to 0x40ae74, which only calls 0x413be0.
  - HANDLE_COP_AI (0x402790, installed at 0x40c9a2 for the type-9 cops)
    runs its state machine (lookout, pursuit, got him, quitting) only when
    ai_model == 1 (0x40279c). Otherwise it goes to 0x4029d2, which calls
    0x409060, 0x403630 and 0x40fba0.
  - With ai_model == 0, the frame update calls **0x403340** and
    **0x408ed0** (0x424c0f).
- CONFIRMED at runtime (gdb_count.py, 553 frames of etappe1, gas held):

  | path | hits |
  |---|---|
  | old cop state machine (0x4027a4) | **0** |
  | old racer AI (0x40aca8) | **0** |
  | cop new path (0x4029d2) | 5369, about one per placed cop per frame |
  | racer new path (0x40ae74) | 1659 (3 × 553) |
  | 0x403340 / 0x408ed0 | 553 each, once per frame |
  | HANDLE_TRAFFIC_AI (0x40b770) | 5001 (not gated) |

- So these findings describe the **old model only**:
  - "The police are the placed cars, and the catch is a block": the
    lookout, the pursuit speeds, the one-chunk-ahead catch, cop_lane as
    driven by pursuit, and the warning's pull-over and camera as triggered
    from there;
  - HANDLE_RACER_AI's triggers (BENZINE_STATION, so Q52 is answered: that
    code never runs), its rubber band (0x40af1a) and its change_lane call.

  Still valid: traffic and ai_lane.cpp (traffic is not gated); the
  offence-check and sequence code (0x415300 / 0x415420 / 0x4151f0) is shared
  by whatever triggers a catch, not yet established for the new model; the
  collision impulse, the props and the scenery are not AI.
- The new model, first look:
  - 0x408ed0 walks fifteen 96-byte records at 0x4a55a0 (stride 0x60;
    records 0-2 are the racers, 3-14 LIKELY the cops). Each gets
    **HandleAIObject 0x408a50** (named by its assert), an **18-state
    machine** on record +0x04 (table 0x408bb8), followed by 0x409540:

    | states | handler |
    |---|---|
    | 0, 1 | 0x409d10 |
    | 2 | 0x409e00 |
    | 3 | 0x409df0 |
    | 4 | 0x409f60 |
    | 5 | 0x40a390 |
    | 6 | 0x40a480 |
    | 7 | 0x40a610 |
    | 8 | 0x40a4f0 |
    | 9 | 0x409ed0 |
    | 10 | 0x40a190 |
    | 11 | 0x40a290 |
    | 12, 13 | illegal |
    | 14 | 0x40aab0 |
    | 15 | 0x40a9d0 |
    | 16 | 0x40a6a0 |
    | 17 | 0x40a8a0 |

  - The racers' trigger handler 0x4088f0 (chunk slots and the waypoint's
    `.dmp` f1): trigger 4 → maximum 12.5 m/s, 5 → 17.5, 6 → 25, 8 → 30
    (record +0x2c); 14 counts laps (+0x0c against [0x4d9ad8]) and finishes
    (0x411650, state 9); 15 arms it (+0x28 = 15).
  - The "96-byte entries at 0x4a55d0" of the dif_mod finding are +0x30 in
    these records.
- The port's cops.c and racers.c are built on the old model. Rebuilding them
  on the new one is the next job.

## The new AI model, traced (session 13, continued)

Source files named by its asserts: `ai_fsm.cpp` (the state machine),
`ai_cfun.cpp` (its conditions), `ai_path.cpp` (ScanPath), `ai_wmap.cpp` /
`rd_drmap.cpp` (the waypoint graph). Every address below was read in the
disassembly; runtime confirmation is noted where it exists.

### Correction to the first look

- CONFIRMED by cross-reference: record **+0x2c**, the "maximum speed" the
  racers' trigger handler 0x4088f0 writes (trigger 4 → 12.5, 5 → 17.5,
  6 → 25, 8 → 30 m/s), has exactly four absolute references, all writes in
  0x4088f0, and one read, in 0x409060 (the cop's speeding check below). The
  racers' driving never reads it. So the new racers **do not** obey the speed
  limits; the value only decides whether a cop chases them. (The first look
  said they were capped. That was an inference and it was wrong.)

### The record (15 × 96 bytes at 0x4a55a0)

| off | meaning | evidence |
|---|---|---|
| +0x00 | object index, -1 = free | 0x408ef5, 0x4035b0 frees |
| +0x04 | state | HandleAIObject 0x408a5b |
| +0x08 | target object (cops; racers' "player behind me") | 0x403578, 0x401878 |
| +0x0c | laps counted | 0x408956 |
| +0x10 | general timer (s) | 0x401b00, 0x401e20 |
| +0x14 | time spent in the current chunk (s) | 0x409540 |
| +0x18 | reversing timer, state 10 | 0x40a190 |
| +0x1c | target waypoint (ScanPath result) | 0x409d8c |
| +0x20 / +0x24 | current / previous chunk | 0x409540 |
| +0x28 | 15 = finish armed, 14 = lap counted | 0x4089ad, 0x408969 |
| +0x2c | speed limit for the cop check (above) | 0x4089c3.. |
| +0x30 | car type at spawn ([0x4d9e2c]) | 0x40357e |
| +0x34 | steering, -100..100 | 0x401230, 0x4013c0 |
| +0x38 | throttle, 0..100 | 0x4012c0 |
| +0x3c | brake, 0..100 | 0x4012c0 |
| +0x40 | engine off (coast) flag | 0x409ed0, 0x40a9d0 |
| +0x44 | gear | 0x40ff90 |
| +0x48 / +0x4c / +0x50 | drift timer / drift heading sum / drift start heading | 0x410701.. |
| +0x58 | "stopped" latch | 0x410213 |

### The state machine is table-driven

- CONFIRMED: 0x403380 (called for every live record by 0x403340, once per
  frame) walks a 25-entry table of `{from, to, value, fn}`. When the record's
  state equals `from` and `fn(record) == value`, the state becomes `to` and the
  walk stops. Racers (records 0-2) use the table at **0x4a5410**, filled at
  0x409447; cops (3-14) the one at **0x4a5280**, filled by 0x409200.
- Racer rules (`from → to`, condition):

  | from → to | fn | fires when |
  |---|---|---|
  | 0 → 11 | 0x401b60 == 2 | (not in demo mode) the racer is farther than the rank's distance from the player (sqrt 2e7 / 2e7 / 1.75e7 / 1.5e7 by rank 0-3) **and ahead of the player** |
  | 11 → 0 | 0x401cf0 | within sqrt(1.5e7) = 3873 units of the player |
  | 7 → 0 | 0x401b00 | 4 s have passed (it also switches the siren off) |
  | 0 → 10 | 0x402050 | 3 s in the same chunk (stuck) |
  | 0 → 1 | 0x401780 | the player is within 100 units **and behind** (bearing 1500..2500 of 4096) and the racer **leads the race** (rank 0x485b28 == 0); sets +0x08 = player. Resets the timer when it exceeds 1 s |
  | 1 → 2 | 0x4018b0 | the racer leads, its speed ≥ 30, `rand() % 100 < 50·dt`, timer < 1 s |
  | 2 → 0 | 0x401780 == 0 | the player is no longer close behind |

- Cop rules:

  | from → to | fn | fires when |
  |---|---|---|
  | 3 → 4, 4 → 3 | 0x401960 = 1 / 0 | the target is behind the cop, bearing 1700..2300 |
  | 3 → 5, 4 → 5 | 0x4019f0 | the target has stopped (`|v_fwd| + |v_lat| < 7`) within sqrt(5000) = 71 units; never in game mode 3; never for a racer in state 9 |
  | 5 → 6 | 0x434140 | always |
  | 6 → 17 | 0x401b00 | after 4 s; siren off |
  | 17 → 8, 15 → 8 | 0x401ef0 | own speed ≤ 5 (zeroes its velocities) |
  | 3 → 16, 4 → 16 | 0x401f60 | the target drives against its waypoint (0x40a100 == -1) and 0x410a10(target) |
  | 16 → 3 | 0x402080 | 0x410a10(target) and the target drives with its waypoint again |
  | 4 → 14, 16 → 14 | 0x401d70 | 0x410a10(target) == 0 |
  | 14 → 3 | 0x401d90 | 0x410a10(target), within 2 chunks, target moving forward |
  | 16 → 10 | 0x401ff0 | 2 s in one chunk while the target does > 10 |
  | 3 → 10 | 0x402050 | 3 s in one chunk |
  | 3 → 15 | 0x401e20 | **the chase gives up**: after 30 s (60 on HARD) or when more than 50 chunks apart; siren off and an "escaped" radio line (0x415170) |
  | 3/4/16 → 14 | 0x401fb0 | the target reverses (speed < 0) |

  0x410a10 and 0x40a100 are below ("faces along the road", "drives with its
  waypoint's lane").

### What each state does (HandleAIObject 0x408a50)

Every state ends with the shared 0x409540 (chunk tracking: the time-in-chunk
timer, and for racers the progress counter in `0x4a5b40[obj+0xc].+0x60`,
moved by 0x442700's chunk distance, signed by 0x407660's direction).

| state | handler | behaviour |
|---|---|---|
| 0, 1 (racer), 3 (cop) | 0x409d10 | **drive the road**: ScanPath from its own waypoint; speed control toward the far point unless a slower car sits on the next waypoint ahead; steer toward the near point |
| 2 (racer) | 0x409e00 | **cut in front of the player**: ScanPath from the *player's* waypoint, so the racer steers into the path just ahead of the player; +0x10 counts |
| 4 (cop) | 0x409f60 | the cop is ahead of its target: ScanPath prefers the lane that moves toward the target's lane; if the target is slower it brakes at 50, otherwise drives, and **brakes at 100 when the target is right behind** (bearing 1800..2200) |
| 5 | 0x40a390 | **caught**: siren off; the player → `set_game_mode(8)` (the arrest sequence) when in mode 0; a racer → racer state 7; every other cop on the same target → 6 |
| 6, 7 | 0x40a480, 0x40a610 | stand still (velocities zeroed) |
| 8 | 0x40a4f0 | coast down ×(1 − 3·dt) per frame; freed (0x4035b0) once more than 30 chunks from the player |
| 9 | 0x409ed0 | engine off and steering eased out; finished racers and retired cops end here |
| 10 | 0x40a190 | **unstick**: set speed -15 (reverse), back for 1 s, then state 0 (racer) or 3 (cop) |
| 11 (racer) | 0x40a290 | far ahead of the player: drives the road but its speed is pinned to **16.67 (60 km/h)** |
| 14, 16 | 0x40aab0, 0x40a6a0 | path-follow variants with braking when the target is slower; 0 speed stops it |
| 15 | 0x40a9d0 | engine off, drive the road (coasting) |
| 17 | 0x40a8a0 | target behind (1024..3072): drive at 10 for 3 s, then stop |

### Starting a chase (the placed cops)

- CONFIRMED: a placed type-9 cop (HANDLE_COP_AI's ai_model-0 arm, 0x4029d2)
  calls **0x409060** every frame, and when it is not in a record it is parked:
  0x40fba0 damps its velocities by ×(1 − 10·dt).
- 0x409060 (only when the cop is not already chasing):
  - each **racer in the cop's chunk** whose speed ≥ its record +0x2c is chased;
  - the **player**, when the player's chunk (0x4d9e44) is the cop's chunk:
    the speed trigger is backtracked from the cop's chunk (0x408080) and
    the player is chased above **12.5 / 17.5 / 25 / 30 m/s for triggers
    4 / 5 / 6 / 7**; trigger 8 (posted 120) sets no limit.
- CONFIRMED: **0x4034a0(cop, target)** opens a record (state 3, target,
  car type) unless the target already has its maximum of cops — the player
  **1 on EASY, 3 on HARD**, a racer 2 — and, on city stages (track flag
  0x4d8e9c+137·etappe == 1), only with probability about dt per frame
  (`rand() % 1000 <= 1000·dt`). It plays a radio line (0x4150f0; always for
  the player, one time in three for a racer) and switches the siren on
  (0x40c640).
- CONFIRMED (0x408ed0): a chasing cop whose waypoint or chunk carries trigger
  14 or 15 goes to state 9 (retires) on highway stages (flag 0).

### Driving: ScanPath and the waypoint graph

- CONFIRMED: the runtime waypoint (80 bytes, 0x4e6628) holds the `.dmp` links
  reordered: the loader 0x443c90 pushes them as L0, L3, L2, L1 and the
  constructor 0x4443a0 stores those at +0x14, +0x20, +0x18, +0x1c. So
  **+0x14 = L0 (same lane, next chunk), +0x18 = L2 (lane +1), +0x1c = L1
  (lane -1), +0x20 = L3 (previous chunk)**. From the data (etappe1, every
  waypoint): on the right-hand list L0 is the same lane index in chunk+1,
  L1/L2 the index -1/+1 in chunk+1 and L3 the same index in chunk-1; on the
  left-hand list the same with chunk-1 and chunk+1 swapped. The graph is
  directed along each carriageway's traffic.
- CONFIRMED: **ScanPath 0x409670(rec, mapper, depth 5, wp, near, far)** is a
  depth-first search five waypoints deep. From each waypoint it tries three
  links of the graph (0x40bf90 with the mapper in its default mode:
  direction 0 = +0x18 lane +1, 1 = +0x14 straight, 2 = +0x1c lane -1,
  3 = +0x20 back), in the order 1, 2, 0 (straight, lane -1, lane +1), rejecting any waypoint whose obstacle list is not empty
  (0x40c3a0). It backtracks when all three fail. It returns the first
  waypoint of the path, `near` = the position of the second and `far` = the
  fourth. In cop state 4 the order changes to move toward the target's lane
  (0x40c440 gives a waypoint's index in its chunk's `.pmp` list): 0, 1, 2
  when the target's index is higher, 2, 1, 0 when lower.
- CONFIRMED: **0x410a10(obj)** is "faces along the road": the dot product of
  the object's forward axis (matrix row at +0x20/+0x28 of its 64-byte matrix)
  with the direction from the chunk's first waypoint (0x442150) to its next
  (0x4075f0) is not negative. So cop state 14 is LIKELY "the target has turned
  round" and 16 "the target is a ghost driver" (facing the road's way while
  0x40a100 finds its waypoint's lane running the other way).
- When every path is blocked it takes the first existing links regardless
  (0x409a2c). That fallback passes a float where a chunk index is expected
  (0x409ae9, `[esp+0x44]` is the position's x); its rotation step reads an
  arbitrary chunk record. LIKELY an original bug; rarely reached.
- CONFIRMED: obstacle lists are filled by
  `rceng_add_object_in_waypoint_obstacle_list` (0x4424b0): the object's
  nearest waypoint on its side (0x443da0) goes to object +0x20 and the object
  into that waypoint's list (max 10). The engine calls it as objects move
  (0x44115e, 0x4412e9), and the vehicle constructors call it.
- **Speed control 0x4012c0(rec, far)**: `b = |bearing to far|` in 4096 units
  (500 if over 1000); the wanted speed is **70 − b/15** m/s. Below it the
  throttle rises 75/s to 100 and the brake is released; above it a racer's
  brake rises 400/s to 100, while a cop simply **sets its speed** to it.
- **Steering 0x4013c0(rec, near)**: steer = bearing·0.5 clamped to ±100
  (±200 units saturate), then the vehicle model below.

### The vehicle model (0x40ff90, 0x410280)

A copy of the player's shipped model (0x40f0d0 / 0x40f3c0) with these
differences, CONFIRMED instruction by instruction:

- **It uses the player's car**: CARS.DAT block A is indexed by **[0x4d9e2c]**,
  the player's car type, not by the AI car (0x40eb5b, 0x41002c, 0x410590).
- Gear acceleration 0x40eae0: `vmax = A[12+g] · f`, `acc = f·(1 − v/vmax)·A[g]`
  (upgraded arrays when the player has the gearbox upgrade, no motor +4 m/s),
  floored at 0, **+0.1 for a cop in state 3**. f is the rubber band below.
- The gearbox, brake (A[27]), coast (A[28]) and the -13.333 reverse cap are the
  player's; the brake "dive" is not halved; no rpm, no wheelspin skid.
- Steering: the same rate, grip and turn-the-velocity terms, except that the
  low-speed branch tests the **player's** skid global 0x4a63dc, there is no
  `|steer| > 20` scrub, the non-turning branch adds `0.004·steer` when
  reversing and takes `0.5·dt·k·v_lat` off v_fwd, and the drift branch (taken
  when the engine-off flag is set) uses 0.18 and ×0.5 for racers, ×1.25 and a
  ×(1 − 3·dt) scrub for cops.

### The new rubber band (0x4014e0)

- CONFIRMED: `lead = racer progress − player progress` (chunks), clamped to
  ±50. With a row `{upper, lower, gain}` from **0x482258**, indexed
  `(difficulty·7 + etappe)·3 + racer`:
  - `lead > upper` or `lead < lower`: **f = 1 − lead·gain·0.02**;
  - `0 < lead ≤ upper`: f = {0.95, 0.92, 0.90} (EASY) or {1.02, 1.01, 1.00}
    (HARD) by racer;
  - `lower ≤ lead ≤ 0`: f = {1.00, 0.98, 0.95} (EASY) or {1.15, 1.10, 1.05}
    (HARD);
  - clamped to 0.2..2. In demo mode (Game.ini `demo`, 0x4d9b20) f is at
    least 1. Cops get f = 1.

  | etappe | EASY upper, lower, gain | HARD |
  |---|---|---|
  | 0 | 25, -25, 1 | 35, -20, 2 |
  | 1 | 25, -25, 1.5 | 35, -20, 2.5 |
  | 2 | 30, -20, 2 | 40, -15, 3 |
  | 3 | 30, -20, 2.5 | 40, -15, 3.5 |
  | 4 | 35, -15, 3 | 45, -10, 4 |
  | 5 | 35, -15, 3 | 45, -5, 4.5 |
  | 6 | 45, -10, 4 | 50, -5, 5 |

  The step at `upper` is abrupt (EASY etappe0: 0.95 at 25 chunks, 0.48 at 26).

### Runtime check of the new model (session 13, gdb_sample.py)

`reverse-engineering/tools/gdb_sample.py` reads the fifteen records every
15th frame from a breakpoint on 0x408ed0 (log:
`reverse-engineering/captures/ai_records_etappe1_s13.txt`, etappe1, HARD,
150 s, 14745 frames; the gas key did not reach the game, so the player stood
near the start and the racers ran away).

- CONFIRMED: racers far ahead of the player sit in **state 11 at exactly
  16.7 m/s** with the throttle at 100, for the whole run.
- CONFIRMED: a racer's +0x2c follows the speed triggers it passes (30, then
  25, then 17.5).
- CONFIRMED: **cops chase racers**: record 3 (object 9) was on racer 1
  (object 394) from the start, went 3 → 4 (throttle 0, **brake 50**, the
  target slower) → 3 → 5/6, the racer went to **state 7 at speed 0** (held),
  the cop to 17 (driving off at about 11 m/s) and then out of the table;
  the racer resumed in 11.
- The frame rate under the debugger was about 98 fps.

### The original's grid (0x411170..0x4113e2)

- CONFIRMED: the racers' grid places are not read from Game.ini [RACERS].
  The player's position (0x4d9e60) is reserved first (0x4111a9), and each
  racer then takes the first free place 0..3 in order. Racer i stands at
  `0x4426e0(STARTCHUNK - place)` (0x4112c3), in lane 4 on side
  `[0x4d9ac4] == 0` (0x442730 / 0x442150 clamp it to the chunk's last lane).
  So every car starts a chunk apart, in a line in the player's lane, behind
  STARTCHUNK. On etappe2 this matters: trigger 14 is chunk 16 and the arming
  trigger 15 chunk 17, so a car placed past 17 would need an extra lap.

### In the port (session 13)

`port/src/ai_car.c` (the vehicle, rubber band and controls; matched to the
original under Unicorn by `reverse-engineering/tools/diff_ai_car.py`, 3000 of
3000 random states for each function), `port/src/ai_world.c` (graph, obstacle
lists, ScanPath), and `racers.c` / `cops.c` rebuilt on the two transition
tables. The old-model racers and cops (lane logic, behaviours, the
one-chunk-ahead catch) are gone from the port; traffic still uses
ai_lane.cpp's lane logic, which is not gated by ai_model.

### Runtime check: chases at etappe1 chunk 440 (session 13)

`reverse-engineering/tools/wine_chase_run.sh` starts the original at
`startchunk=430` (Game.ini), holds the gas 9 s, brakes 15 s and samples the
records (gdb_sample.py, every 10th frame). Chunk 440 holds two placed cops
(objects 214, 215), and speed trigger 4 at chunk 436 gives a 12.5 m/s limit.
Logs: `reverse-engineering/captures/ai_records_etappe1_c430_*_s13.txt`.

- CONFIRMED: **racers come first.** With the racers as shipped, both cops took
  racer 2 (object 395, at about 41 m/s against its 12.5 limit), which made 2,
  the maximum for a racer. When the player went through at about 33 m/s, no
  cop was free. Both cops ran 3 → 4 → 10 (reversing at exactly -15.0) → 3 → 4
  → 6/17, reached 63-65 m/s on the way, and held the racer in state 7.
- CONFIRMED: **the player's chase**, with the racers' +0x2c overwritten to
  1000 each frame (gdb_sample.py RACER_LIMIT) so that no racer counts as
  speeding:
  - both cops started on the player as it was in their chunk at 41.7 m/s;
  - 215 went 3 → 4;
  - both went **3/4 → 14** when the player bounced backwards off them
    (v -9.8), and **14 → 3** when it rolled forward again (17.4);
  - **3 → 5 → 6** as the player stopped beside them, and the game mode went
    to **8 for about 4 s** (the first warning), then back to 0;
  - each cop left 6 after its own 4 s timer for **17, at exactly 10.0 m/s
    for 3 s**, then **8**, parked and never freed while the player stayed
    within 30 chunks.
- CONFIRMED: the original refuses any racer count but 3: `(load_racer_info)
  Gm_ini.cpp(337) : Invalid nmb of racers 0 (3)`.
- Not settled by this run: the exact speeding thresholds (the player was far
  over), and state 16.
- (later in session 13) The port's AI cars collide with the barriers,
  traffic, the player and each other through the player's impulse code (now
  `a2_drive_impulse` in drive.c). The cops update before the racers, which
  follows the original's order (all transitions, then all states, then the
  engine). With that, the port's cops catch a racer through 5 → 6 → 17 → 8, as
  in the Wine run. Before, a racer held at 16.67 m/s in state 11 never counted
  as stopped. The new `--gas-brake G B` option, with `--start-chunk 430`,
  puts the port through the Wine chase scenario.

### Runtime check: the speeding threshold, obstacle lists, state 16 (session 13)

Same scenario as above (etappe1 from chunk 430, cops at 440, trigger 4), but
with no keys: gdb_sample.py pins the player's forward speed every frame
(PLAYER_SPEED) and keeps the cops off the racers (RACER_LIMIT). Logs:
`reverse-engineering/captures/ai_records_etappe1_c430_v{12.0,13.0}_s13.txt`.

- CONFIRMED: **the trigger-4 threshold lies between 12 and 13 m/s.** At 12.0
  the player drove through chunk 440 and on to 447 with no chase; at 13.0
  both cops started at once. This fits the 12.5 m/s in 0x409100, that is 45
  km/h at a sign posted 50.
- CONFIRMED (disassembly): the comparisons in 0x409060 have **no margin**.
  The player is chased when `v > limit`, strictly (0x409111 `fld v; fcomp
  [0x47f1e8]`; `test ah,0x41; jne` skips on less or equal; likewise 17.5 /
  25 / 30 at 0x47f1ec / 0x47f1f0 / 0x47f1f4). A racer is chased when
  `v >= rec+0x2c` (0x4090a5 `fld [rec+0x2c]; fcomp v`, taken on less or
  equal). The session-9 "10% over the limit" (0x403257) is the old model's
  check, which the shipped game never runs, so at trigger 4 a 13.0 m/s player
  is speeding.
- CONFIRMED: **the player is in the waypoint obstacle lists.** Player
  object 368 is always listed on its own waypoint's list (+0x24 / +0x28),
  next to cops 214 / 215 when they share the waypoint. So ScanPath steers
  the AI around the player too.
- CONFIRMED: **state 16 at runtime.** Both cops went 3 → 16 at the start of
  the chase and back to 3 about 80 frames later. The player was not on the
  wrong carriageway; it was driving straight with no steering. So 16 comes from 0x40a100's
  test, "the waypoint the target's lane leads to is behind it", which a car
  at an angle to its lane meets. It is not a ghost driver as such.
- With the speed pinned at 13 m/s the cops still made the catch (mode 8).
  The pin is written at 0x408ed0, after the transition pass, so the
  transitions saw the speed the collisions left, and the cops blocked the
  car.

### Runtime check: the racer cut-in, state 2 (session 14)

`reverse-engineering/tools/gdb_cutin.py` runs etappe1 from the grid. Every
frame, from a breakpoint on the transition pass 0x403340, it holds the
player 75 units (7.5 m) behind the racer ranked 0, with the racer's heading and
speed. It writes the player's `c_3dobject` (`0x4ea9e0 + obj+0x08 * 153`;
+0x50 / +0x58 position, +0x78 heading) and its +0x6c / +0x74 / +0x7c. When
the leader reaches state 2 the player is released for 90 frames, with its
speed pinned. 0x409060 is patched to `ret` in memory so that no cop starts a
chase. Logs (one line per frame):
`reverse-engineering/captures/ai_cutin_etappe1_{behind,side40}_s14.txt`.

- CONFIRMED: **the 0 → 1 → 2 → 0 cycle.** Held behind the leader at 38-65
  m/s, the leader enters 1 on the first frame (bearing 2047, d² 5625). Each
  state-1 spell ends in 2. Over both runs (43 episodes), state 1 lasted 1 to
  235 frames, 36 and 45 frames on average. The code's chance is
  `rand() % 100 < 50·dt`, which at dt ≈ 0.03 s passes for 0 and 1 only, so
  2% per frame and a 50-frame mean. The difference is within noise
  (SE ≈ 8 frames).
- CONFIRMED: **state 1 does not advance the timer** (+0x10 stays 0.00), and
  **state 2 does** (0.03 per frame). State 2 ends through the 2 → 0 rule
  (0x401780 == 0): at timer > 1.0, where 0x401780 zeroes the timer, so the
  next frame goes 0 → 1 again, or earlier once the player is no longer within
  10 m and the bearing window. Timers at exit: 1.00-1.02, or 0.03-0.97 for the
  early exits.
- CONFIRMED: **state 2 steers the racer into the player's lane.** With the
  player one lane to the side (SIDE -40: the racer 4.0 m to the player's +side,
  7.5 m ahead), the racer's target waypoint (+0x1c) switches to the path
  from the player's waypoint. It steers at up to 100 and its lateral offset
  from the player goes 40.0 → 0.0 units in 18 frames (about 0.5 s). It then
  weaves ±0.7 m in front of the player until the 1 s runs out.
- CONFIRMED: **every racer is listed on its own waypoint's obstacle list**
  (the logged list of the leader's waypoint always holds the leader, plus
  the player 368 when they share it).
- CONFIRMED in session 15 (see "Runtime check: the swerve with the player
  straight behind" below; was LIKELY): with the player **directly behind**
  (SIDE 0) the racer instead swerved about 2.6 m *out of* the lane and back. The ScanPath from the
  player's waypoint rejects any occupied waypoint (0x40c3a0), and the racer's own
  waypoint ahead is occupied by the racer itself, so the path goes round it.
  The reasoning chain is confirmed piece by piece but not traced for this case.
  So "cut in" means "drive the free path that starts at the player's
  waypoint". A player in the next lane gets the racer in front of it. A
  player straight behind makes the racer weave.
- The port's 0x401780 / 0x4018b0 / 0x409e00 (`racers.c` c_player_behind,
  c_block_chance, st_block) match the code. The port already lists the player,
  racers and cops on their waypoints (main.c), so it needs no change.

## The racing cars' wheels: car_whl.cpp (session 14)

The seven player-car meshes (`r_*.d3d`, CARS.DAT slots 0-6) have **no
wheels**. Their lowest vertices sit 1.1-2.9 units above y = 0. Traffic, police,
ADAC and R_VECTOR meshes reach y = 0, so their wheels are LIKELY modelled in.
The original hangs the shared `WIEL_ANIM` (`WIEL.d3d` + `cars01.pcx`; radius
3.5, y ±3.5, axle along x, ±1.4 wide) on each racing car four times as
sub-objects. Every address below was read in spel.dat.

- CONFIRMED: **0x4136c0(type, obj)** asserts `0 <= type <= 6`
  (`car_whl.CPP` line 50) and reads four positions from the table at
  **0x486198** (48 bytes a type: front left, front right, rear left, rear right,
  x/y/z in the car's frame, +z the nose). Each wheel is created with
  0x42b310(`WIEL_ANIM`, position, zero rotation) and attached with 0x441630, which
  holds up to 4 sub-objects per game object (+0x5c count, +0x4c ids;
  `obj_main.cpp: No more sub-objects available`). 0x43fbb0(i) returns
  sub-object i's 3D object. Called for the player at 0x42572b (type [0x4d9e2c],
  or 0 when [0x4d9e0c] is set) and for each racer at 0x41139b.

  Car names are the game's own (menu.dat, see reverse-engineering/menu.md).

  | type | front (x, y, z) | rear (x, y, z) |
  |---|---|---|
  | 0 Brabant | ±7.0, 3.7, 13.3 | ±7.0, 3.7, -10.0 |
  | 1 Opol Astro | ±7.4, 3.7, 13.8 | ±7.5, 3.7, -14.5 |
  | 2 Wolfswagen | ±7.15, 3.7, 11.2 | ±7.15, 3.7, -11.72 |
  | 3 BWM Z3.5 | ±7.0, 3.7, 12.6 | ±6.6, 3.7, -12.5 |
  | 4 PARCHE 913 | ±7.75, 3.7, 13.0 | ±7.5, 3.7, -12.5 |
  | 5 Mersedes SLK GTR | ±7.83, 3.7, 13.5 | ±7.83, 3.7, -13.0 |
  | 6 Lambdaghini | ±8.0, 3.7, 14.0 | ±8.3, 3.7, -14.8 |

- CONFIRMED: **every frame, 0x413900 (the player, called at 0x426029 and
  0x426577) or 0x413be0 (a racer, called at 0x40ae61 / 0x40ae79)**:
  - **spin**: each wheel's +0x74 -= `min(dt · v_fwd · S·10 / (7π), S · 0.497213)`,
    with S = [0x49b014]. Only the forward value is capped; reversing is not.
  - **steer** the front pair's +0x78: the player's is
    `S · 0.0833333 · -0.01 · steer_input` ([0x4d9cd8], -100..100; full lock
    S/12 = 341 units = 30°). A racer's is `clamp(yaw_rate (obj+0x7c) · 400,
    ±500)` (±44°).
  - **height**: all four wheels' y = `clamp(road height under the car
    (0x442f00 on its chunk) − the car's y, ±1.05) + 3.5`.
- CONFIRMED at runtime (Wine, gdb): **S = [0x49b014] = 4096.0**. It is the
  sine table's size, set by 0x450280 (fstp at 0x4502be; [0x49b010] = 4096,
  [0x49b018] = 4095). The file holds 1024 until then, so reading it statically
  gives the wrong value. With S = 4096 the spin is that of a radius-3.5 wheel
  (`10·v·dt/3.5` rad), the same 3.5 as the height offset and the mesh, and the
  cap is just under half a turn a frame.
- STRONGLY SUPPORTED: the wheels are drawn in the car's frame. Their positions
  are written once, in car-local coordinates, and never moved again.
- Port: `port/src/wheels.c` follows all of the above. The player's wheels
  inherit the body's yaw, pitch and roll. The racers' bodies are drawn with yaw
  only, as before.

## The ground is one plane per chunk (session 14)

- CONFIRMED: **0x442f00 (chunk::height(x, z))** is a plane equation, not a
  mesh search: `y = ([+0xdc] − [+0x3c]·x − [+0x44]·z) / [+0x40]`, with the
  chunk record's normal at +0x3c/+0x40/+0x44 and its distance at +0xdc.
  Callers: the body ride height and pitch (0x40ee32, 0x40efba, shared by the
  player and the AI drivers), the player's ground test (0x40fd70), the wheels
  (0x413ad8, 0x413dcb), the shadow (0x41d68c, 0x41d77d) and the flying props
  (0x4290fa). So **every height on the ground** in the original comes from
  this plane.
- CONFIRMED: **0x442cb0(chunk, target)** builds the chunk's frame. Forward
  (+0x48) = target − position (+0x24), normalised. Right (+0x30) =
  (0, 1, 0) × forward, normalised, so it is always horizontal and the plane
  never banks. Up (+0x3c) = forward × right, normalised. d (+0xdc) =
  up · position (0x442ecb). The loader calls it for chunk i toward chunk
  i+1's position (0x444fb7, in the loop at 0x444f76) and for the last chunk
  toward chunk 0's (0x44504b). It then calls 0x442f20 and 0x442fe0 on the same
  chunk (not traced).
- Checked against the data (etappe1): the plane at every waypoint matches the
  waypoint's own y to within 0.21 units for 5345 of 5347 waypoints. The two
  exceptions are the fuel-station slip road (chunks 399/400), whose waypoints
  sit off the chunk centre at a different height.
- CONFIRMED (data): the `.rmp` road list (list 0) is **not** always the
  surface the cars drive on. Under etappe1's overpass, chunks 338-341 have no
  list-0 planes at road level. The road there is in **list 2**, at y 0. List 0
  around it (337, 338) holds the overpass road at y 45-114. The port's old
  triangle lookup fell back to the nearest list-0 vertex there and put every
  car on the overpass deck (a one-frame jump of 114.5 units, then a fall).
  The plane gives 0.0. `reverse-engineering/tools/road_overlap.py` checks a
  track for overlapping road heights. (List 2's role in general is not
  established; this is one observed use.)

## Game.ini gamma squares every colour (session 14)

- CONFIRMED: `[GFX OPTIONS] gamma` ("Gamma correction: 0=light, 1 = dark",
  shipped **1**) is read into **0x4d9b14** (0x4220be). When it is set,
  0x420ffa and 0x423597 call winlib's **0x468220(1)**, which only stores
  **0x4eacc0**.
- CONFIRMED: **0x468230** converts an RGB colour to the surface's 16-bit
  format. With 0x4eacc0 set it first replaces each channel with
  `c*c >> 8` (0x4682a2..0x4682f6: `imul`, `sar 8`), a gamma of 2, and then
  shifts it down to the format's bits. 0x4eacc0's only other reader is
  0x468436, in the 42-caller converter 0x468380.
- CONFIRMED (callers): the converters are reached from winlib's
  **Rcpcx.cpp** (PCX loading), **Sprite.cpp** (sprite loading, "converting
  from 24 to 16 bits"), ascii.cpp, the radar and the 3D engine's texture
  handle code (0x45be58).
- So with the shipped setting **every texture and sprite is darkened**. This
  is what open question 44 measured on the HUD sprites (about 2.0), and it is
  the game's own, not Wine's. Checked on a capture of etappe1's start (Wine,
  640x480x16): the road reads (28,24,18) in the original against
  (32,26,23) predicted from the port's undarkened (91,81,77).
- Not settled: the HUD fonts measured unchanged (open question 44), though
  ascii.cpp calls the converters. LIKELY its glyph colours take a path without
  the squaring; not traced. (The player's car looked brighter in the original
  than the port's squared texels; that was the port's own shading, see "The
  meshes are not shaded".)
- Port: `a2_gfx_set_gamma` / `a2_gfx_gamma_rgba` (gfx.c), applied to the 3D
  textures, the sky and every stamped sprite. The HUD sprites already had it.
- CONFIRMED (code, session 15): the converter **0x468380(colour, source
  bpp)** squares an **8-bit** source through the current palette
  (0x4eace0, 0x468436) and a **24-bit** one through 0x468230. A **16-bit**
  source is returned unchanged (0x4683f0), so 16-bit art would escape the
  gamma.
- STRONGLY SUPPORTED (code, session 15): **the sky is darkened.**
  gfx_sky.cpp's init loads its panorama with **0x46b500** (0x41e7c3), the
  Rcpcx.cpp PCX loader, whose pixel conversion calls 0x468380 with an 8-bit
  source (0x46b6c0..0x46b7f9). The port already darkened it; that was LIKELY
  and is now backed by the code path.
- CONFIRMED (code + data, session 15): **the fonts are squared too.**
  The ascii.cpp loader converts glyph indices through 0x468380 (8-bit at
  0x46a6ed, 24-bit at 0x46a658, raw copy only in mode 0, picked by
  [0x4eacbc]). olfnt02's main index **230** is **(189,202,231)** in
  e02_pl16, which squares to (140,160,209); session 9 measured
  (140,158,214) in the original. Session 9's "the fonts are not squared"
  compared against the wrong reference, a colour that was not index 230's.
  The port now squares its font atlases (font.c).

## Traffic sign faces in the port (session 14)

The mechanism was traced in sessions 9-10 (see "Every texture-blit slot
assigned"). The sign art, read from the source anims (0x4961E8, RC.IMG
index + 1):

- Direction boards, hires anims 29/31/33 (one carriageway, etappe 1/3/5) and
  35/37/39 (the other). On etappe1, sprites 368, 367, 366, 369 ("Den Haag /
  Rotterdam" with Schiphol; the same with distances 49/56 with Hoofddorp /
  Aalsmeer N207; Leiden / Den Haag / Rotterdam with Assenheim / Leiden-west /
  Den Haag centrum; Rijswijk / Den Haag-zuid / Hoek v Holland with Den Haag /
  Rotterdam / Rijswijk-wijd A20). The second carriageway uses 391, 390, 389, 392.
  Each is 123x85, stamped at (9, 122) or (9, 7) in a_TRFFC1, whose shipped
  boards are plain blue.
- CONFIRMED: the matrix speed sign (slot 7, anim 25: sprites 409 blank,
  410 X, 416 "50", 413 "70", 418 "100") is set through **0x42b190**, gated on
  `texturecopy` ([0x4d9aa4], read at 0x421d53). Its jump table (0x42b200)
  maps the four limit handlers' arguments 0/1/2/3 to frames 2/3/4/1, so
  50 → "50", 70 → "70", 100 → "100" and 120 → the X face. The +1 sprite
  shift gives the right faces here too.
- Port: `stamp_traffic_sign` (main.c) stamps them when the player crosses
  the trigger's chunk.
- CONFIRMED (session 14): **only the player fires these triggers.** The
  engine's dispatcher 0x442590 (the loop at 0x442607) has a single caller,
  0x425fff, in the player's per-frame car update, called with the player's
  object. The racers' triggers go through their own handler (0x4088f0).

## The shipped brake lights are object lights, gfx_lght.cpp (session 14)

- CONFIRMED: the brake-light check 0x425f0b (on when 0x4a63cc or 0x4d9df4,
  tracked by 0x4d9e10) has two arms. With the 640x480 flag [0x4d9aac]
  **and** Game.ini `object_lights` ([0x4d9b0c], shipped 1) it calls
  **0x41b560(light, on)** for the two lights [0x48b458] and [0x48b45c];
  otherwise it calls the texture stamp 0x42b100. So **with the shipped
  settings the stamp never runs**.
- CONFIRMED: gfx_lght.cpp (assert path 0x4883e8) keeps 20 lights of 16 bytes
  at **0x4aa860**: +0 used, +4 on, +8 owner object, +0xc a 3D object.
  0x41b560 writes +4 only. 0x41b5a0 (only with object_lights) fills a free one:
  the 3D object gets the owner as parent (+4), the anim's mesh, a position
  (+0x50, car-local) and a rotation (+0x74) and is tinted by
  **0x41b400(object, ARGB)**. 0x41b720 draws them each frame (hires and
  object_lights only) with ALPHABLENDENABLE on, SRCBLEND = SRCALPHA and
  DESTBLEND = INVSRCALPHA.
- CONFIRMED: **0x425500** makes the player's two lights from
  **`OBJ_LIGHT01_ANIM`** = `s_licht.d3d`, a six-vertex card about 5.8 x 5.5
  units facing back, whose UVs fall on the radial glow of `s_sfx`. Per car
  (CARS slot):

  | car | left light (right at -x) | pitch | ARGB |
  |---|---|---|---|
  | 0 Brabant | (-7.62, 6.76, -18.91) | 0 | ff960000 |
  | 1 Opol Astro | (-7.10, 8.85, -20.63) | 0 | ff960000 |
  | 2 Wolfswagen | (-6.25, 7.70, -16.70) | -500 | ffff0000 |
  | 3 BWM Z3.5 | (-5.77, 6.92, -19.92) | 0 | ffff0000 |
  | 4 PARCHE 913 | (-4.39, 5.66, -21.90) | 0 | ffff0000 |
  | 5 Mersedes SLK GTR | (-6.29, 5.00, -22.50) | 0 | a4ff0000 |
  | 6 Lambdaghini | (-6.47, 8.77, -22.90) | -180 | a4ff0000 |

  (positions 0x485c28 / 0x485c34, 24 bytes a car; pitch and colour from
  0x425500's locals.)
- CONFIRMED at runtime (Wine, gdb): lights 0 and 1 are in use and owned by
  object 0x170 (the player); with their on flags written to 1 the original
  draws **a soft red glow over each tail light**, the gradient's dark part
  invisible. How its Direct3D setup turns the glow's brightness into
  coverage is not traced.
- Runtime caveat: under Wine with xdotool the Down key never set the brake
  input (0x4d9e08 stayed 0 while the car coasted down), so the lights
  could not be seen switching on by themselves. The keys only reach the
  game after `xdotool windowfocus` and a click on the window.
- Port: main.c `draw_brake_lights` draws the two cards in the car's frame
  while braking, with a glow texture made from `s_sfx` (white, alpha = the
  squared brightness, fitted to the capture). The stamp is kept for
  `object_lights=0`.

## The cameras, in full (session 14)

Extends "The camera view presets, and a correction" (session 10), whose
offset table read the constants but not what they do.

- CONFIRMED: every arm of fcn.00414B70 works on the engine camera record
  (352 bytes at [0x4E66A0], slot 0's object from 0x4A7078) through five
  setters: **0x447770** offset (+0xa4, in the attached object's frame),
  **0x4477f0** look target (+0x74 = 1, +0xb0 the object, +0xb4 an offset),
  **0x4478c0** no target (look along the object), **0x4475a0(f)** lag on
  (+0x6c = 1, +0x84 = f) and **0x4476d0** rigid (+0x6c = 0, +0x84 = 1).

  | preset | offset | look at | lag | key |
  |---|---|---|---|---|
  | 0 | (0, 25, -100) | car + (0, 10, 0) | 0.245 | 4 |
  | 1 | (0, 25, -60) | car + (0, 10, 0) | 0.245 | 1 (and the race start, 0x414995) |
  | 2 | (0, 38, -55) | car + (0, 10, 0) | 0.245 | -- |
  | 3 | (0, 15.5, -1) | along the car | rigid | 2 |
  | 4 | (25, 15, -50) | along the car | rigid | 5 (tweak mode) |
  | 5 | (0, 23, 40) | car + (0, 10, 0) | unchanged | 3, held |
  | 6 | (-30, 15, 60) | car + (0, 10, 0) | unchanged | 7 (tweak mode) |
  | 7 | (25, 15, -50) | car + (0, 10, 0) | unchanged | 8 (tweak mode) |
  | 8 | (-20, 9, -30) | car + (0, 10, 0) | unchanged | 9 (tweak mode) |
  | 9 | (-25, 55, -30) | car + (0, 10, 0) | unchanged | 0 (tweak mode) |
  | 10 | (0, 45, -55) | car + (0, 10, 0) | 0.245 | -- |

- CONFIRMED: **key 3 is hold-to-look, not a toggle** (0x414500): while it
  is down and the view is not 5, the view is saved in 0x4863CC and preset 5
  set; when it is up and the view is 5, the saved view is set again. Key 6 is
  not polled; keys 5, 7, 8, 9, 0 need TWEAK_MODE ([0x4d9b24]), which the
  shipped Raser.ini sets to **1**.
- CONFIRMED: the targeted presets set the car object's **+0x2c = 1**, preset
  3 sets it to 0, and the vehicle draw loop (0x440aa2) skips a vehicle whose
  +0x2c is 0, its shadow included: **the cockpit view hides the car**.
- CONFIRMED, the per-frame update (0x447bd0..0x447f40): the wanted position
  is the attached object's position plus the offset turned by its matrix
  (0x45e4e0); the look point is the target object plus its offset the same
  way, or position + forward without a target. With the lag on, the position
  moves `+0x84 x (wanted - previous)` each frame: the delta is divided by dt
  and multiplied by dt again (0x441cd0 both times), so the lag is **per
  frame**, not per second.
- CONFIRMED (code), **the lag never changes the distance behind the car**
  (0x447f78..0x448111): when the camera is attached to an object (+0x70),
  has one (+0xa0 != -1) and +0x80 is 1, the lagged position minus the
  object's is turned into the object's frame by the transposed matrix
  (0x45e970), its z (along the car) is **overwritten with the offset's z**
  (`mov [esp+0x34], [ebx+0xac]`, 0x44805d), and the result is turned back and
  stored as both the position and the previous position (+0x88). Only the
  sideways and height parts lag. +0x80 is set to 1 by the constructor
  (0x447119) and no other instruction in the code section writes a camera's
  +0x80 (a full scan of `mov [reg+0x80], ...`), so this holds for every
  lagged preset. The branch sits inside the lag-on test (0x447dc7), so rigid
  presets skip it (they need no correction).
- CONFIRMED (code): only the position is lagged. The look point (+0x54) is
  rebuilt every frame and merely copied to +0x94 (0x447f5e), with no blend.
- The port had lagged the whole position and the look point, so at speed the
  camera fell back: about 3x the per-frame travel at 30 fps, i.e. the car
  looked smaller the faster it went (user report, session 15). Fixed in
  main.c; screenshots at 84 and 179 km/h now show the car at the same size.
- MEASURED (not from code): the projection's **vertical view angle is about
  65.6 degrees** at 640x480. The Trabant on the start grid in preset 1, at
  rest, is 136 px wide in a Wine capture. The port, with the same camera, gives
  width x tan(fov/2) = 87.6 (152 px at 60 degrees, 125 at 70). The
  projection code is not traced.
- Port: main.c `cam_preset` and the camera block follow all of the above; the
  lag is scaled to 30 frames a second (the original's step is its frame
  rate), a port choice.
- CONFIRMED (code, session 15): the object the camera hangs on is a
  **c_3dobject** in the array at [0x4ea9e0]. The update computes `idx * 17`,
  then `* 9`, a 153-byte stride, and reads the matrix at +0x10 and the
  position at +0x50. That is the same `_d3d_objects` array whose +0x7c the
  A[29] body roll writes ("Two driving models" above), so the camera turns
  with the drawn body, pitch and roll included. The port now builds the car's
  matrix as it draws it (yaw, then pitch, then roll) and uses it for the
  offset, the look point and the hold-distance correction. How the matrix at
  +0x10 is rebuilt from the angles is not traced, so the rotation order is the
  port's.

## The meshes are not shaded (session 14)

- CONFIRMED: MESH.CPP's loader (0x455b50) writes every vertex's colour as
  **0x00FFFFFF** and its specular as **0** (0x455cfa, 0x455d08), and the
  per-frame transform copies the source vertex's colour and specular into
  the D3DTLVERTEX (0x454dfc..0x454e14). So the original draws every mesh at
  its texture's colour, with no directional shading. The only recolouring
  found is the deliberate tint of 0x41b400 (the object lights).
- Correction: **0x455080 is not lighting** (spel-architecture.md said so).
  It rewrites each vertex's texture coordinates as `|u| * [0x4eab9c] +
  table[+0x34]` and `|v| * [0x4eaba0] + table[+0x38]`, i.e. a remap into a
  region of a texture atlas.
- The port had shaded every mesh by a key light of its own, down to 45% on
  the faces turned away. Without it the player's car matches a capture of the
  original (etappe1 start, Wine, 640x480) in colour as well as size.

## The contact geometry, for boxes (session 14; open question 53)

- CONFIRMED (code): a collision entry (0x449a30, 76 bytes) is a box: the
  mesh's bottom-face corners at +0x00..+0x2c (bbox corners 0, 1, 5, 4: -x-z,
  +x-z, +x+z, -x+z), the position at +0x30, the object at +0x40, the heading at
  +0x44 and at +0x48 a flag that selects the record's arm in 0x449840 (LIKELY
  "has physics").
- CONFIRMED (code, and under Unicorn): **0x44a520(A, B, move)** works in A's
  frame. A's corners are copied as they are (0x44ae80). B's are turned by B's
  heading (0x44aef0), moved by B.pos - A.pos (0x44b070) and turned by
  -A.heading (0x44aff0). Each of B's corners is swept along `move` against
  A's rectangle (0x44b6f0 -> 0x44b480 -> 0x44b390), and the earliest time
  below 1 wins (strictly less, corners in order 0..3). It returns that time as
  a fraction of the move and leaves the corner (B's frame) at 0x4e667c with
  the side of A at 0x4e6698. A corner already inside A and still inside at
  the end of the move hits at t = 0 on A's **nearest** side. One that starts
  inside and ends outside does not count.
- CONFIRMED (Unicorn): **0x44b860** gives side k (the edge from corner k to
  k+1) its **outward** normal, 0 -z, 1 +x, 2 +z, 3 -x, turned by an angle in
  the engine's convention (x' = x c - z s, z' = x s + z c).
- CONFIRMED (code): **0x44a0f0** tests each candidate twice. First the
  moving object's corners into the entry, with the move = its velocity turned
  into the entry's frame (0x44ae10) x dt x 10. Then the entry's corners into
  the object, with the move reversed. The earlier hit goes through 0x449840
  into the contact record 0x4e6760: the point is the struck corner, the
  normal is the crossed side's (negated in one arm), and the objects are at
  +8 / +0xc.
- CONFIRMED (code), **the record builder 0x449840(A, B, hit)**, A the box
  whose side was crossed, B the box whose corner crossed it, +0x48 the
  "moves" flag (0 for barriers and static scenery):

  | A | B | +4 | +8 | +0xc | point | normal |
  |---|---|---|---|---|---|---|
  | static | moving | 0 | B | 0 | B's corner, B's frame | A's side normal turned by A.heading - B.heading (B's frame) |
  | moving | static | 0 | A | 0 | B's corner, B's frame | A's side normal in A's frame, negated |
  | moving | moving | 1 | B | A | B's corner, B's frame | as the first row |
  | static | static | assert (line 0xff) | | | | |

  So object +8 is the one whose corner touched, except in the second row,
  where it is the moving A. That row still gives B's corner in B's frame, a
  point in the wrong object's frame, LIKELY a slip in the original (it
  would skew the impulse's lever arm for a moving box hit by a barrier slab's
  corner).
- CONFIRMED: **0x441490 is not a frame bisection.** It reads the record,
  returns 0 if the object already separates (v . n > 0), otherwise adjusts the
  velocity along n and runs 0x44a0f0 again.
- Fit: `reverse-engineering/tools/fit_contact.py` (with emu_contact.py and
  contact_cli.c) compares a model, and the port's C, with the original under
  Unicorn on 1500 random box pairs and moves. **1499 agree** on time, side
  and corner. The other is a two-corner tie at the same time, where float32
  rounding picks the other corner.
- Port: `src/contact.c` (a2_contact_sweep, a2_contact). The player's and the
  AI cars' collisions with traffic, racers, cops and solid scenery take their
  normal and contact point from it (a2_drive_impulse_at); the push-out still
  separates overlaps, and stands in when the engine test finds nothing (the
  other moved into us; the original handles that in the other's own pass).
  On an etappe5 run 697 of 897 player contacts came from the engine test.
- CONFIRMED (code): **the barriers are boxes too.** 0x44a010 fills the same
  candidate list (count 0x4ea2f0, entries at 0x4e6790, at most 200) from the
  object's chunk and the one before, and 0x449c10 turns each `.cmp` rail
  (runtime record 96 bytes at [0x4ea340]: start +0, angle +0x18, length
  +0x5c) into an entry: corners (-10, 0, -1), (0, 0, -1), (0, 0, L+1),
  (-10, 0, L+1), a slab 10 units thick on the rail's -x side, 1 unit past
  both ends. The position is the rail's start and the heading its angle,
  truncated. It has no object (+0x40 = -1) and +0x48 = 0. They go through
  the same 0x44a520.
- Checked on the data: with forward along the rail, **the road is on the
  rail's +x side** for 3282 of 3315 etappe1 rails (1936 of 1986 on etappe2,
  3292 of 3364 on etappe5), so the slab sits behind the barrier. The
  exceptions are LIKELY median rails whose nearest lane is on the other
  carriageway.
- 0x44acf0 is a static overlap test (is any corner of B strictly inside A),
  used later in 0x44a0f0; 0x449de0 adds the registered objects.
- CONFIRMED (code): **the engine moves each object in steps, each only as
  far as its earliest contact.** In 0x440fb0 the object at [0x4e65d8] (the
  player) takes **8 steps of dt/8**, every other object one. Each step runs
  0x44a0f0 for the contact time t, then 0x441490 (0 when the contact
  separates; otherwise it re-tests with the velocity's normal part set to
  10), then 0x43f900 -> 0x4404a0, which moves the object by its velocity x
  dt x 10 x t (or by 0x441490's vector) and turns it over the whole step.
  The impulse follows (0x4497f0). The integrator truncates each step's turn
  to whole units, so 8 steps do not equal one.
- Port (session 14, after the attempt below): the player's driving model runs
  once, then 8 steps of dt/8 (a2_drive_integrate_move), each stopped at its
  earliest contact the car moves into (contact point velocity . n > 0, as the
  impulse takes it) against the barrier slabs, solid scenery, traffic,
  racers and cops. After a contact the rest of the frame slides along it (the
  move minus its part into the obstacle), and the frame's first contact
  gives the impulse. Port's simplifications: the impulse once per frame,
  after the move, and the slide as a projection rather than 0x441490's
  re-test. Regressions: etappe1 292.0 s with 0 unwedges (was 2), etappe5
  352.7 s with 1 (was 8), etappe2 1251.7 s with 24 (was 70).
- CONFIRMED (code, session 15), **0x441490(obj, dt, &vec, t, &flag)** in full,
  as 0x440fb0 calls it (0x441062..0x441067):
  - it returns 0 when **t = 0**, or when 0x4499e0 finds no contact;
  - it returns 0 when the object's velocity (+0x6c) **separates**
    (v . n > 0, with the record's normal);
  - otherwise it sets **v' = v + (10 - v . n) n** (the normal part becomes
    exactly 10, outward), re-runs 0x44a0f0 with it, and returns
    **vec = v' x t2**. It restores the real velocity and the contact record,
    so only the move uses v'.
  - 0x43f900 then moves by `vec` when 0x441490 returned 1. Otherwise it moves
    by the velocity x t, which stops the object at the contact; at t = 0 it
    does not move it at all. The impulse 0x4497f0 follows within the same
    step.
- Port (session 15): the sub-step uses that re-test in place of the slide.
  There are two deviations, both because the impulse comes once a frame
  rather than in every step. The re-test also runs at t = 0; without it a
  car stayed pinned to a slab on etappe2 chunk 117. And a box the car
  already overlaps and moves away from does not hold it (etappe1 chunk 442,
  with the police on). Regressions: etappe1 283.8 s, 2 unwedges; etappe5
  342.8 s, 1; etappe2 1224.0 s, 25; etappe1 with police 327.4 s, 2 (was
  345.8 s, 8).
- Port (session 15, later): **the impulse is now applied in the step**, as
  0x440fb0 does (0x4497f0 right after the move). step_impulse in main.c uses
  each obstacle's own mass and restitution, and the blocks after the move
  keep only the push-out, the damage and the sound for those contacts. This
  removed the t = 0 deviation for a while. It came back once the start
  heading was fixed: the autopilot then pinned the car to a barrier at
  etappe1 chunk 330 with the police on, the impulse undone each step by the
  grip. Both deviations stay.
  Regressions: etappe1 295.3 s, 1 unwedge; etappe5 342.1 s, 1; etappe2
  1206.9 s, **10** (was 25); etappe1 with police 323.8 s, 4.
  The AI cars' one step then got the same move: 0x441490's re-test replaces
  their projection slide. Regressions: etappe1 312.5 s, 2 unwedges; etappe5
  351.2 s, 1; etappe2 1144.2 s, 7; etappe1 with police 344.2 s, 1. Every
  racer finishes on every stage. The drive-test runs are **deterministic**:
  a second etappe1 run gave the same times to 0.1 s. So the player's time
  moving by up to about 20 s between these versions comes from the changes
  themselves; a different contact early on changes everything after it.
- Port: the AI cars (racers, cops) take the same contact-limited step, one
  a frame as the engine moves every object but the player: put back by
  their move (velocity x dt x 10), stopped at their earliest contact,
  sliding on, with the impulse at that contact. The racers on etappe2 now
  finish in about 1190 s against 1055 s, consistent with them no longer
  cutting into the barriers on the city corners.
- The earlier attempt (session 14): `a2_scene_rail_boxes` built the slabs but
  the port did not stop at the contact time. Tried in session 14: 68% of the barrier contacts (2022 of 2958
  on etappe2) hit a slab's end face where two slabs meet. That brakes a
  scraping car, and every regression got slower with more unwedges (etappe2
  1407 s against 1251 s). The original never lets a car into the slabs: it
  moves it only to the time of impact (0x441490). The port pushes out after
  the fact, so the barriers keep the wall's own normal until the port steps
  to the time of impact.

## What TWEAK_MODE does in a race (session 14)

The shipped Raser.ini sets `TWEAK_MODE=1` ([0x4d9b24], read at 0x421fca).
Everything it gates, from the flag's six readers:

- **Keys 5, 7, 8, 9, 0** (0x41455f): camera presets 4, 6, 7, 8, 9 (see "The
  cameras, in full").
- **M** (0x424ef8, key 0x4d, waits for release): steps [0x4d9b2c] 0 -> 1 ->
  2 -> 3 -> 0 and calls **0x4149b0(object)**, which sets the camera's car
  (0x486644) and re-attaches it: 0-2 a racer's record object (0x4a55a0 +
  96 x n), 3 the player.
- **F12 held** (0x42524a, key 0x7b): prints `frameRate: %f` (1/dt) at
  (10, 80) and `vchunks: %d` ([0x4d9a8c]) at (10, 100).
- **Tab** (0x40fe6c, key 9, in the player's step 0x40fc30): the tweak menu,
  **0x412e90**, one modal screen per press, chosen by the state [0x4a7074]
  (jump table 0x41368c):

  | state | screen | field (player's record) | step |
  |---|---|---|---|
  | 0 | main menu 0x412c00, Up/Down '>', Space picks | | |
  | 1 | per-gear list 0x412820 | A[gear + 6 x gearbox] (0x4a5edc + 4 n) | ±0.05 |
  | 2 | per-gear list 0x412390 | A[12 + gear + 6 x gearbox] (0x4a5f0c + 4 n) | ±1, Right/Left ±10 |
  | 3 | Punt vooruitschakelen | upshift 0x4a5f40 | ±0.1 |
  | 4 | Punt terugschakelen | downshift 0x4a5f44 | ±0.1 |
  | 5 | Remkracht | 0x4a5f4c | ±0.1 |
  | 6 | Afremmen motor | 0x4a5f50 | ±0.5 |
  | 7 | Rolhoek list 0x411710 | versterking A[29] 0x4a5f54 ±0.1, demping B[0] ±0.5, factor B[1] ±1 | |
  | 8 | Pitchhoek list 0x411bd0 | demping B[2] ±0.5, factor B[3] ±1 | |
  | 9 | Hoogte list 0x411fb0 | demping B[4] ±0.5, factor B[5] ±1 | |
  | 10 | Draaihoek | steering speed 0x4a5f58 | ±0.1 |
  | 11 | Opslaan: fwrite(0x4a5ee0, 124, 10) and (0x4a5df0, 24, 10) to CARS.DAT | | |
  | 12 | Stoppen | | |

  Numpad + (0x6b) raises by a step and numpad - (0x6d) lowers, one step a
  press (each waits for the release); Space ends a screen. The "gearbox"
  index is [0x4d9ed0], the gearbox upgrade. The pitch list's items read
  "Rolhoek demping/factor" in the game's own strings.
- **L held** (0x41aa15, key 0x4c, at the end of draw_dashboard): the
  player's chunk number ([0x4d9e44], "%d") at (0, 100) in font 11
  (olfnt02).

Port: all of it -- the cameras, M, F12, L and the tweak menu (src/tweak.c,
its text in slider13 at the original's rows). How the per-gear and
suspension lists leave their editors on Space is not traced (the port goes
back to the list).

## COP MODE, the police light and the banner slot (session 15)

### COP MODE: name yourself COPLAND

- CONFIRMED (code): Game.ini `[PLAYER] copland` is read into **0x4d9e0c**
  (0x422944), next to `car` into 0x4d9e2c (0x42290c). menu.dat writes it as 1
  when the player's name is "COPLAND" (0x4019ee; port a2menu mcareer.c).
- CONFIRMED (code): it has exactly three readers.
  - **0x4256a0**, the player's init. It sets the player's car type
    [0x4d9e2c] = **6** and writes **0x85** into **0x4850ba**. That is
    field 0 of type 6's record in the 0x484B18 table (0x484B18 + 6 x 238 +
    14). Field 0 is each record's anim id, filled at load by 0x410b60 with
    0x42b310(name). Anim 0x85 = 133 is **MPOLIZEI_ANIM** (anim name table
    0x490430). So type 6 keeps the Lambdaghini's CARS.DAT and handling
    record, and is drawn with the police body.
  - **0x42570e**: the wheels are hung as **type 0's** (0x4136c0(obj, 0)), the
    Brabant's wheel positions, not type 6's.
  - **0x416cc1**: the start camera's end callback (0x416cb0: set_game_mode(0),
    view preset 1) raises banner **47** (0x416ccd) when copland is set.
- Nothing else reads it: the police, the AI and the menu's prizes treat the
  player as type 6.
- CONFIRMED (runtime, Wine): .work/menutest Game.ini with `copland=1`, then
  spel.dat started on its own. The player's car on the grid is the police car,
  and **COP MODE** appears at the race start, centred about (320, 80) of the
  640x480 screen, in the white label font.
- So language.ini 47 is "COP MODE" in Dutch and "COPLAND" in English and
  German.

### The police light bar is one texture stamp

- CONFIRMED (code): **0x40c640** (a cop's siren on/off) keeps a count of
  sirens on in **0x4a5d38**. When it falls to 0 it stamps slot 14 frame 0
  (0x40c76c), and it publishes `count != 0` in **0x4a5d30** (0x40c855).
  **0x40c860**, called from the AI update, stamps slot 14's current frame
  (**0x4a5d3c**) while 0x4a5d30 is set, once every **200 ms** of GetTickCount
  (0x4a5d40), and flips the frame.
- CONFIRMED (code + data): slot 14 is CARS01.PCX at (99, 62). Its hires anim
  is **41 = (326, 325)**, i.e. RC.IMG **325** (dark) and **324** (lit) once
  the +1 is taken off (lores anim 40 = 322 / 340).
- STRONGLY SUPPORTED: CARS01.PCX **ships with the lit frame**. The sheet's
  (99, 62) region is a mean 7/7/11 off sprite 324 against 29/29/58 off 325;
  the rest is colour-conversion noise. So a police car shows the lit light bar
  until the first chase of the race. After that it alternates while any
  siren is on, and goes dark when the last siren goes off.
- Because it is one sheet, **every police car flashes together**, chasing or
  parked, and so does the player's car in COP MODE. The Wine capture above
  shows the player's light bar alternating between orange/red and blue
  frames while racers were being chased.
- 0x42ae90, the stamp itself, does nothing without Game.ini `texturecopy`
  ([0x4d9aa4]).

### How the banner slot is drawn

- CONFIRMED (code), **0x41a95b** in the HUD:
  - A pending id in 0x4882c0 (-1 = none) is latched into 0x4882c4, with
    GetTickCount into 0x4a7d84, and 0x4882c0 is cleared.
  - For **2000 ms** after the latch (`cmp eax, 0x7d0`), the id is drawn while
    **(ticks / 100) is odd** (`mul 0x51eb851f; shr edx, 5; test dl, 1`): a
    100 ms on / 100 ms off blink.
  - It is drawn with **0x41bfc0(surface, element 0x4c, mode 1, text, font)**.
    Mode 1 centres the text on the element's point.
- Element 76 in the 12-byte layout table at 0x488510 (`{anim, x, y}`): the
  reader picks the half at +84 elements when the lores flag [0x4d9a70] is
  **0** (0x41c018: `sete`), and that half has the 640x480 coordinates
  (element 0: (380, 431) against (190, 180)). In the file that half holds
  **zeros from element 50 on**, element 76 included. The first half has (-1,
  160, 40).
- CONFIRMED (session 15): **the hires half from element 118 on (elements
  34..83) is filled at run time by 0x41b860**, which is referenced only from
  the data word at 0x482034 (LIKELY an init table). It writes immediates and
  values derived from the screen size [0x49e72c] / [0x49e730] (640 / 480 at
  RESOLUTION=3, read from the running game), so it is a hand-made 640x480
  layout, not twice the lores one: 46 of the 50 entries differ from a plain
  doubling. Element 160 (76 + 84) is written at 0x41bc61..0x41bc6c as **(-1,
  width / 2, 80)**, read back as (-1, 320, 80) from the running game. So the
  banner is centred at **(width / 2, 80) in screen pixels**, which is the
  (320, 80) measured from the Wine capture. The port's position is therefore
  exact at 640x480. (The [0x4d9a6c] +320 adjustment only applies in lores.)
- The font is **font 10** of the 148-byte font records at 0x486d08:
  **olfnt01.fnt** (hires) / lo_fnt1.fnt (lores), the HUD's label font.
- **0x429860** (Tr_finis.cpp, lap completion) raises **31 NIEUW RONDE
  RECORD** when the lap beats LAP_RECORD (0x4298d5). **Otherwise it raises
  45 LAATSTE RONDE** (0x429921). That happens on every lap, with no test for
  the last one, so on a circuit every non-record lap says "last lap"
  (CONFIRMED from the code; not seen at runtime).
- The port draws all nine banner-slot ids this way now. It dropped its own
  checkpoint-split and lap-time banners, which the original does not have.

## Which DirectDraw device the game opens (session 15)

Completes open question 5 (session 9), which found the driver-name scan
(0x46d130) and the selector 0x46d290 but not who uses them.

- CONFIRMED (code): the **only DirectDrawCreate** (0x466271, through the
  import thunk 0x47d416) takes as its **GUID** the record returned by
  **0x46d290([0x49e740])**. The record's first 16 bytes are the enumerated
  device's GUID (copied at 0x46d23e). When the selected class of device was
  not found, 0x46d290 returns **NULL**, i.e. the primary display driver.
- CONFIRMED (code): **[0x49e740]** is set only by 0x464ff0(2, sel, 0)
  (0x465022), called from **0x420770** on **Raser.ini [PROGRAM] RESOLUTION**
  ([0x4895b0], read at 0x420a37). The jump table is at 0x4208dc:

  | RESOLUTION | Raser.ini comment | driver selector | 0x464ff0(0, n) |
  |---|---|---|---|
  | 0 | 320x200x8 | 0, primary | 0 |
  | 1 | 640x480x8 | 0, primary | 3 |
  | 2 | -- | 0, primary | 9 |
  | **3** | **640x480x16** (shipped) | **1, the 3Dfx device** | 9 |
  | 4 | -- | 1, the 3Dfx device | 10 |
  | 5 | -- | 0, primary | 6, and 0x44c750(1) |

  So the shipped 16-bit mode asks for the 3Dfx board's DirectDraw device
  (found by the names `3DFX`, `DD3Dfx`, `R3D`, `mnstr2`) and falls back to the
  primary display when there is none.
- CONFIRMED (code): a **user-chosen device overrides it.** 0x420eae reads
  **Raser.ini [DIRECTX] INDEX** into 0x489639, **NAME** into 0x48964d and
  **GUID** (parsed as `%d,%d,...`) into 0x48963d. With INDEX non-zero,
  0x420888 switches to selector **4** ("using user defined driver %d",
  [0x4a0ed0]), after adding that GUID as an enumeration record (0x46cfd0).
- The shipped Raser.ini has **no [DIRECTX] section**. The setup program
  **instlng.exe** writes one: its strings include `DIRECTX`, `index`, `guid`,
  `raser.ini`, the GUID format `%d,%d,%d,%d,%d,%d,%d,%d` and the choice
  **"Automatisch"**. So the installer lets the player pick a Direct3D device,
  and "automatic" leaves the RESOLUTION rule above in charge (LIKELY: the
  installer's own code is not traced).
- Under Wine there is no 3Dfx device, so RESOLUTION=3 opens the primary
  display, which is what every capture so far has used.

## The TURBO bar is the orange ladder (session 15; open question 46)

- CONFIRMED (data): the HUD bars are sprite anims in the 0x4961E8 table
  (12-byte `{count, ?, list}` records, pairs `(2, RC.IND + 1)`):

  | anim | frames (0-based RC.IMG, step order) | what |
  |---|---|---|
  | 20 | 440, 435, 436, 437, 438, 434, 441, 442, 443, 444, 56 | turbo, lores |
  | **21** | **449, 433, 439, 445, 446, 447, 448, 432, 450, 451, 13** | **turbo, hires** |
  | 22 | 137, 125, 134, 135, 136, 122, 138, 139, 140, 141 | damage, lores |
  | 23 | 431, 129, 124, 126, 127, 128, 123, 130, 131, 132, 133 | damage, hires |

  Anim 23 is exactly the damage order measured in session 9. The gradient
  chevrons **11, 12, 14..21 are in no anim at all**; only 13, the full
  gradient strip, is used, as anim 21's 100% frame.
- CONFIRMED (runtime, Wine, RESOLUTION=3): with Ctrl held, the bar at
  (540, 388) matched, with gamma c*c/255 applied, 432 at 3 s, 447 at 5 s and
  446 after release (mean abs error 4.4 / 3.3 / 3.4 against 11+ for every
  other frame of either ladder), i.e. steps 7, 5 and 4. At the race start
  the full bar matched 13 (3.3, against 11.0 for 451).
- CONFIRMED (code): the damage bar's frame is `damage / (100 / (frames - 1))`
  through _ftol (0x4700e0, truncating), at 0x41aad1. 13 is shown only at
  exactly 100% (LIKELY the same rule for the turbo bar; its draw is not
  traced).
- This corrects session 9's "turbo: 14, 12, 11, 15..21, 13". That was read off
  the art, not the game, and it called the orange ladder the unused third
  set. The port now draws anim 21 and truncates both bars.

## The HUD at 640x480: 800x600 units, and the highway panel (session 15)

- MEASURED (Wine capture, RESOLUTION=3, 640x480): the HUD labels come out
  narrower than the font: SCHADE 58 px against the font's 72, RACE TIJD 80
  against 100, SNELHEID 71 against 90. **The reading below that this is a 0.8
  scale and an 800x600 layout is wrong** (corrected later in session 15, see
  "The 640x480 HUD: element table, panel sprites, letter spacing"): the text
  is 1:1 with a negative letter spacing. The bar fills come out 91-92 px wide on screen
  (113.75 units). The frames are 55x13 regions of sprite 426, stretched
  to about 98x22 px.
- CONFIRMED (code): the highway top-right panel is **0x419b10** (overlay
  detail > 2): bracket element 0x39, then RACE TIJD (33) over the race
  timer (3) as `%02d:%02d:%d` (elements 0x26 / 0x27, fonts 10 / 11), then
  **RACE RECORD (52)** over [0x4d9acc], the race record in ms, or the
  literal `--:--:-` (0x4882e8) while it is 0x0FFFFFFF (elements 0x3a /
  0x3b), then POSITIE (38). Every HUD label is font 10 (SCHADE 0x41a4d7,
  TURBO 0x41a438, SNELHEID 0x41a487, POSITIE 0x419d5a). The circuit panel
  is 0x419e00 (RONDE INFO).
- CONFIRMED (code): the time panels (0x41a5f0 highway, 0x41a780 circuit)
  have a flasher of their own. The checkpoint handler sets **0x4dcaa0**
  (0x42a62b) right after raising banner 44 (0x42a621). The panel latches it
  with GetTickCount (0x4a7d7c) and for **2000 ms** draws the literal
  **"EXTRA TIJD"** (0x488300, not language.ini) at element 0x4c in font 10,
  while (ticks / 100) is odd. That is the banner's own place, time and blink
  phase, so in Dutch the two coincide exactly. With the English or German
  strings, banner 44 (EXTENDED TIME) is overprinted by the Dutch literal
  (LIKELY visible; not seen at runtime). Ported.
- MEASURED: COP MODE is 85 px wide in the capture against the font's 100,
  centred at about (318, 80). (Not a 0.8 scale: olfnt01 1:1 with its -2
  letter spacing, see "The 640x480 HUD".)
- CONFIRMED (data): the panels' brackets are **whole sprites**, one anim
  each: the highway panel's element 0x39 is anim 13 = **sprite 427**
  (42x63, the time, record and position brackets in one image), the circuit
  panel's element 0x33 is anim 8 = sprite 425, and the bottom group's element
  52 is anim 10 = sprite 426. On the capture the pieces come out at about 1.6
  of their art size, but no single stretch of the whole of 427 fits (best 49%
  of its white pixels). **Resolved later in session 15:** those are the
  lores entries. At 640x480 the elements' hires halves name anims 12, 9 and 11,
  the whole panels 313, 311 and 312, drawn 1:1 (see "The 640x480 HUD").
- The circuit panel (0x419e00) was checked against a Wine capture of
  etappe2. RONDE INFO, TIJD, RECORD, LAATSTE, POSITIE and RONDE sit within
  2 px of the port's rows, so they are unchanged. (The original then stopped
  with "Unhandled exception" under Wine, a few seconds into the stage. Not
  investigated; it may be the reduced drive mapping of the Wine prefix.)
- The port's fonts were point-sampled, which at 0.8 dropped whole glyph
  rows and columns; the original's text stays solid. They are now
  linear-filtered. The original's own filtering is not known.
- The port had no RACE RECORD row on the highway stages and placed POSITIE
  as on a circuit. Both are fixed. The bottom group is refitted to the
  capture, in 800x600 units (capture px x 1.25).
- The element table (0x488510) gives these elements in its first half in
  320x240-like numbers (0x3a = (254, 25)). They do not map onto the capture
  by a simple x2 (RACE RECORD's label sits at about (522, 57)), so the port's
  numbers are measured, not read from the table.

## The radar, gfx_radr.cpp (session 15)

- CONFIRMED (code): **init_radar 0x41cf20** picks the radar texture record
  ([0x48e1c8] / [0x48e1d4] by [0x4d9a6c]) into 0x4ac99c, builds the quad
  (0x41c5a0) and creates the map (create_radar 0x41cda0).
- CONFIRMED (code): **the map is the whole track, north up, at 60 units a
  pixel.** get_road_bounding_box 0x41cc90 takes the chunk positions'
  (runtime chunk +0x24, 388-byte records) min and max and pads them by 3840
  units (0xf00) on every side. A chunk at (x, z) is pixel
  `((int)x - minx) / 60, (maxz - (int)z) / 60` (0x41c790), clamped to the
  map. draw_road_in_radar_surface 0x41c9a0 marks each chunk. On a 16-bit
  screen it draws a 7x7 square of palette colour 0 that leaves pixels already
  in the road colour alone, then a 3x3 square of colour 13 (0xd); both go
  through the gamma converter. In 8-bit mode it is 5x5 / 3x3. With 128 units
  between chunks this makes a solid 3-pixel road with a 2-pixel black edge.
  The rest of the surface is the key colour 0xfe.
- CONFIRMED (code), **each frame (0x41d360 -> 0x41d040)**:
  - a 128x128 window (the 640x480 rectangle at 0x489050) is cut from the map,
    centred on the **player's chunk** and clamped to the map's edges
    (0x41cf90), and copied into the radar surface;
  - **dots** are added with 0x41c880, 8x8 frames of sprite anim 44 (43 lores)
    centred on a point and dropped within 4 pixels of the edge, all at the
    cars' **chunks**, not their positions:
    - the three racers, frame 0, sprite 114 (red);
    - every cop in the siren list 0x4a5ce0, frame 2, sprite 305 (green);
    - the player, frame 1, sprite 303 (yellow).
  - **Traffic and parked cops are not shown.**
  - 0x45bae0 uploads the surface to its texture, and 0x41d360 draws it as a
    4-vertex TLVERTEX fan (DrawPrimitive 6, 3, 0x4ac9a8, 4).
- CONFIRMED (code): **the quad (0x41c5a0)** starts at the rectangle's corner
  (30, 361) and is **0.7** of its size: an 89.6-pixel square centred on
  (74.8, 405.8), with UVs 0..1 and white diffuse colour. **0x41d280** turns
  its corners about that centre by the **active camera's** view (look - eye
  of camera [0x4a70c8], height dropped, normalised; cos = vz, sin = -vx),
  so the camera's forward is up.
- MEASURED (Wine captures): the player's dot is at (72, 402) (top-left) on
  etappe1 and etappe2, which is the quad's centre. The visible road runs
  about 105 px. Fitting the port's map to the captures first gave a turn
  about 16 degrees short of the camera's yaw + 180. **Explained:** the
  port's own start heading was 17 degrees off the road. It aimed from the
  car's lane at the middle lane of the next chunk, 40 units across over 128
  along. The original starts the car along its lane (Wine capture, etappe1).
  With that fixed (render.c a2_scene_start_pose_on_lane), the port's radar
  matches the capture at the start.
- The port had drawn its own radar: a short heading-up ribbon of the 15
  nearest chunks with large dots, traffic included. It now builds the map and
  draws it as above (main.c radar_build / hud_radar), and the result matches
  the etappe1 capture.

## Reversing, and the keyboard brake ramp (session 15)

- CONFIRMED (runtime, Wine + gdb, `reverse-engineering/tools/gdb_reverse.py`
  on 0x40f0d0): **the car reverses on the brake.** It was logged rolling at
  14.4 m/s with Down held. The stop latch [0x4a63e4] set at |v| < 0.4 and v
  went to 0, then on through **negative to -13.333 m/s**, the clamp at
  0x40f39f, and held there. After release it coasted back toward 0 by the
  engine braking. The latch is cleared only by a positive throttle (0x40f1ab).
- CONFIRMED (same log): **reversing on the brake does not light the brake
  lights.** 0x4a63cc ("force opposes motion") stayed 0 throughout, because
  0x40f38b clears it whenever v <= 0. The other light condition **0x4d9df4
  is dead**: its only writes store 0 (0x4266b3, 0x426ae8).
- CONFIRMED (runtime, Wine, no debugger): **on a fast machine the keyboard
  brake does nothing.** With Down held, the car slowed from 26 km/h to 0 in
  about 2 s and then stayed at 000 with the scene still, twice. The keyboard
  handler ramps the brake as an integer, `[0x4d9e08] = (int)(old + 40 * dt)`
  capped at 100 (0x426725..0x426763; the same in 0x4269c0's arm). At more
  than 40 fps the step truncates to 0, the brake stays 0, only the engine
  braking (CARS.DAT A[28], 3.0 m/s^2 for the Brabant) slows the car, and it
  never reverses. Under the debugger the frames were slow (dt 0.008-0.03 and
  longer), the ramp reached 16-18 and the brake worked. At the 20-30 fps of
  1998 hardware the ramp ran at 20-30 a second.
- The port had: a -6 m/s cap of its own (REVERSE_MAX, from its old impulse
  scale) that also capped reversing at 21.6 km/h; brake lights on whenever the
  car went backwards; and the truncating ramp, copied, which at the port's
  60 fps broke braking just as it does in the original. Now: no cap (the
  drive model's -13.333 stays), lights by the force rule, and the ramp keeps
  its fraction (port choice, so the brake works at any frame rate).


## The waypoint -1 read in 0x40b650 (session 15; open question 55)

- CONFIRMED (WINEDEBUG=+seh): the "Unhandled exception!" on etappe2 under Wine
  is an access violation at **0x40b6d7**, `mov edi,[esi+edx+0x24]`, reading
  0x050bfff8 with edx (the waypoint array, [0x4e6628]) = 0x050c0024 and
  ecx = -1. The waypoint was **#291** of etappe2.dmp, links (297, -1, 299,
  -1): its previous link (+0x20, L3) is -1, and 297 is in eax as at the fault.
- CONFIRMED: **0x40b650(wp, obj, mode)** asks whether a waypoint is free for a
  car. Mode 0 passes only one call in 30 (`rand() % 30`). It then returns 0
  if the waypoint's obstacle list (+0x24) is not empty, or if the list of its
  next (+0x14) or previous (+0x20) waypoint is not empty, and finally checks
  the objects in 0x4a64a4 (12-byte entries, to 0x4a705c) through 0x446a10.
  **The next/previous tests run twice**: first unguarded (0x40b6bc,
  0x40b6d1), then again with a -1 test (0x40b6df, 0x40b6f8). The first pair
  is the bug: with a link of -1 it reads 0x2c bytes before the array.
- CONFIRMED: its only caller is **0x40b480** (call at 0x40b52e), the
  placement of one AI car at a chunk's lane-0 waypoint (0x442150). 0x40b480 is
  called from the loop at 0x40b31e..0x40b46e over the same 0x4a64a4 entries:
  cars that fall outside a chunk window around the player are moved to its
  far end. The port's traffic does not follow this (it respawns 20+ chunks
  ahead of the player, its own rule).
- CONFIRMED: every track has waypoints with no previous or no next link
  (lane starts and ends): etappe0 31 each, etappe1 38, etappe2 76, etappe3 19,
  etappe4 30, etappe5 23, etappe6 47. Etappe2 has the most, which fits it
  being the one that crashes.
- CONFIRMED (gdb_count.py, 50 s of etappe2 on the patched copy): 0x40b650
  reached its link tests 289 times; the next link was -1 four times and the
  previous link -1 nine times. Each of those would have read waypoint -1. The
  patched game ran on (91 s in another run) where the unpatched one died at
  about 9 s and 20 s.
- STRONGLY SUPPORTED: why only Wine fails: the 0x24-byte array prefix sits at
  the start of a page (0x050c0000), so the read falls on the page before,
  which Wine leaves unmapped for a large block. On a Windows 9x heap the same
  read lands in heap memory and returns whatever is there (see hypotheses.md).

## Traffic on both carriageways: the spawner and the recycler (session 15)

- CONFIRMED, **the traffic spawner 0x40ca70** makes two groups, each from the
  12-entry table at 0x484a48 (two `rand() % 12` draws per car, as before):
  - **[0x484aa8] = 8 cars** on side `DIRECTION == 0` (with the shipped
    DIRECTION 0, side 1, the player's carriageway);
  - **[0x484aac] = 12 cars** on side `DIRECTION != 0` (side 0, the other
    carriageway, driven against the chunk order).

  In each group, half stand at `0x4426e0(STARTCHUNK - vchunks - 5 - 3i)` and
  half at `0x4426e0(STARTCHUNK + vchunks + 5 + 3i)` (vchunks = [0x4d9a8c],
  Game.ini visiblechunks = 30), on lane 0 (0x442150). Every one is made with
  **+0x34 = 0** (0x40cb42, 0x40cc18, 0x40cd06, 0x40cddc): out of play.
- CONFIRMED, +0x34 is "in play": the vehicle loop at 0x440a42 (the draw loop
  0x440aa2) skips objects whose +0x34 is 0, and `handle_basic_ai` has a sanity
  message for one ("SANITY CHECK ENTERING handle_basic_ai() Car (object %d)
  is", 0x4077bf).
- CONFIRMED, **`ai_check_cars_outside_handling_range` 0x40b2d0** (named by
  the log line at its call, 0x424ade), once a frame after rceng_handle_objects:
  - The window is the player's chunk ± (vchunks - 1) (0x445d70). When it runs
    below 0 it becomes [lo + N - 1, N - 1] and [0, (c + r) % N]. When it runs
    past N - 1 it becomes [lo, N - 1] and [0, (c + r) % N]. Then N / 2 decides
    which part a car is beyond.
  - For every entry of the AI table (0x4a64a0, 12 bytes: in use, object,
    kind) except the player, if the car's chunk (+0x18) is outside the window:
    - kind 3 (LIKELY the racers) is never moved;
    - kind 4 calls 0x40c640(obj, 0) only when its cop record says state 1;
    - kind 5 calls 0x40c5f0;
    - anything else, the traffic, calls **0x40b480(obj, edge, 0)**.

    A car behind the window goes to its front edge, one ahead of it to its
    back edge.
- CONFIRMED, **0x40b480(obj, chunk, 0)** wraps the chunk (0x4426e0) and
  returns at once if the chunk's side-1 lane count (+0xe8, side 0's +0xe4
  when DIRECTION is set) is exactly 1. Then it takes lane 0 of the car's side
  (0x442150) and asks 0x40b650. When that says free, it:
  1. moves the car onto the waypoint (0x4420f0 / 0x442230);
  2. sets +0x18 and +0x1c to the chunk and +0x20 to the waypoint;
  3. resets its lane trigger (0x406bf0);
  4. re-enters it in the obstacle lists (0x4424b0);
  5. sets **+0x34 = 1**.

  Otherwise it sets **+0x34 = 0**, so a car that leaves the window and cannot
  be placed straight away drops out of play until a later frame places it.
- CONFIRMED, 0x40b650 in mode 0 gives up unless `rand() % 30 == 0`, and also
  when 1 / (frame time) < 14.5 (0x40b672..0x40b68d), i.e. below 14.5 fps.
- So in the original no traffic car is ever placed near the player. Every car
  enters 29 chunks ahead or behind. A Wine capture of etappe1 at 5 s
  (rev_3.png, session 15) shows a car far off on the other carriageway, and
  the shipped debug.txt logs "Side = 0" objects 136122 times against 115175
  for side 1.
- The port now does this (traffic.c: spawn, recycle, put_at, wp_free), with
  four deviations, all port choices:
  - the 1-in-30 chance is a chance of dt per frame (the same at 30 fps);
  - the 14.5 fps floor is left out;
  - a -1 link is skipped instead of read through (question 55);
  - the overlap test is centre distance against the sum of half lengths
    (LIKELY close to 0x446a10, which was not traced).

## The recycler's cop and ADAC arms are dead in the shipped game (session 15)

- CONFIRMED (code): the recycler's kind-4 arm (0x40b3c3 / 0x40b40b) tests the
  cop's `t_cop` state ([0x4a5238] + 32 * [ai + 0x44], +0x00) for 1 and then
  only calls **0x40c640(obj, 0), the siren off** ("COP: object = %d. ai = %d.
  ON/OFF = %d."); the cop is not moved. The only writers of `t_cop` +0x00 are
  0x402755 (0, init), 0x402ad4 (1, the old lookout) and 0x40290b (3, the old
  pursuit). Under ai_model 0, HANDLE_COP_AI takes the new path at 0x4029d2,
  so STRONGLY SUPPORTED: **state 1 never happens and the arm never fires**.
- CONFIRMED (code): **the ADAC (wegenwacht) car** is made at init by
  0x40c4f0 ("Initing adac-car.", called from 0x410b9b). It is vehicle 8,
  object kind 5, at `0x4426e0(0)`, lane 4 (0x442730) on side
  `DIRECTION == 0`, behaviour 15, handler **0x401000** ("Handling Adac AI."),
  index in [0x4a5ccc]. +0x2c and +0x34 are set to 0 and the flag [0x4a5cc8]
  ("has arrived") to 0. The recycler's kind-5 arm calls **0x40c5f0
  `turn_off_adac`**, which takes it out of the obstacle lists and sets +0x2c
  and +0x34 to 0 again. Nothing in the binary writes the ADAC car's +0x34 = 1:
  the only writer of 1 is 0x40b61d in 0x40b480, which the recycler never calls
  for kind 5.
- CONFIRMED (runtime, gdb_count.py, 40 s of etappe1 under Wine): **the ADAC
  handler 0x401000 ran 0 times** and `turn_off_adac` 1872 times (once a frame;
  the car stands at chunk 0, outside the window). In the same run 0x40b480
  was called 3614 times and placed a car (0x40b61d) 55 times.
- So the roadside-assistance car is **never seen in the shipped game**; the
  port leaves it out. Its handler would follow the player
  (AI_ADAC_RANGE, "Adac chunk = %d. Player chunk = %d.", stopping within 3
  chunks of a stopped player, 0x40113d..0x401183); only the game-state flags
  [0x4d9a9c] / [0x4d9a78] gate it. What was meant to switch it on is not in
  the binary.

## Runtime check: the swerve with the player straight behind (session 15; question 54)

- Setup: gdb_cutin.py with SIDE=0 and the new SCAN=True option. The player
  is held 75 units straight behind the leader. SCAN logs every ScanPath
  (0x409670) that a racer in state 2 runs, and every waypoint it tests at the
  three returns of 0x40c3a0 (0x40986d, 0x4098ce, 0x409917), with the list
  copied to 0x4a5bd8.
- CONFIRMED (frame 21): player on waypoint 318, leader (object 394) on 324,
  straight ahead.
  - ScanPath from 318 tests **324 first, and its list is [394], the racer
    itself**, so the waypoint is rejected.
  - The path goes on through 322 (lane -1), 328, 334, 340 and 346.
  - The racer's target (+0x1c) becomes 322, it steers 100, and its side
    offset grows 0 → -2.5 → -9.1 units over the next frames.
  - At frame 24 the same happens with 330 [394] → 328.
- CONFIRMED (whole run, 107 state-2 ScanPaths): the first tested waypoint
  was blocked by **the racer's own entry 50 times**, by any other vehicle 0
  times, and clear 57 times. The swerve is the racer steering round its own
  obstacle-list entry. 0x40c3a0 makes no exception for the asking object.
- The port's ScanPath (ai_world.c) also rejects any occupied waypoint, and
  the racers register on their own waypoints, so it behaves the same.

## The 640x480 HUD: element table, panel sprites, letter spacing (session 15)

- CONFIRMED (code + runtime): every HUD piece at 640x480 is placed by the
  hires half of the element table (0x488510, 12-byte `{anim, x, y}`,
  element + 84), filled at run time by 0x41b860 for elements 34..83 (see the
  banner entry above). Read back from the running game, the ones the HUD
  uses:

  | element | what | (x, y) | mode |
  |---|---|---|---|
  | 57 (0x39) | highway panel, anim 12 = sprite 313 (67x127) | (485, 15) | sprite |
  | 51 (0x33) | circuit panel, anim 9 = sprite 311 (153x211) | (474, 15) | sprite |
  | 52 (0x34) | bottom group, anim 11 = sprite 312 (148x134) | (484, 340) | sprite |
  | 73 (0x49) | CHECKPOINT bracket, anim 0 = sprite 412 (170x26) | (235, 15) | sprite |
  | 38 / 39 | RACE TIJD label / race time | (546, 4) / (502, 24) | 0 |
  | 40 | RONDE INFO | (532, 54) | 0 |
  | 41..46 | TIJD, RECORD, LAATSTE labels / values | (484 / 553, 75, 92, 109) | 0 |
  | 47 / 48 | POSITIE / value, circuit | (567, 139) / (571, 159) | 0 |
  | 49 / 50 | RONDE / value | (580, 189) / (571, 209) | 0 |
  | 58 / 59 | RACE RECORD / value, highway | (522, 56) / (514, 76) | 0 |
  | 60 / 61 | POSITIE / value, highway | (564, 107) / (567, 127) | 0 |
  | 74 / 75 | CHECKPOINT label / clock | (320, 12) / (320, 24) | 1 (centred) |
  | 7 / 53 / 55 | SCHADE / TURBO / SNELHEID labels | (571, 329) / (582, 368) / (559, 409) | 0 |
  | 56 | speed, `%03d` of abs [0x4d9e34] | (505, 428) | 0 |
  | 3 | damage bar, anim 23 | (541, 349) | sprite |
  | 54 | turbo bar, anim 21 | (540, 388) | sprite |
  | 83 | damage, `"%2d@"` of [0x4d9e30], font 16 | (580, 349) | 0 |

- CONFIRMED (code): sprites go through **0x41c0f0(surface, element, frame,
  mode, dx, dy)**. With mode 0 it blits with flags 3 (0x41c1ca), which skips
  the anchor, so the element is the top-left. The blit 0x468db0 is a 1:1
  software copy (0x415990) of the 2D surface record ([0x4eff90], 32 bytes:
  pixels, width +8, height +0xc, anchor +0x10 / +0x14). The surfaces are RC.IMG
  sprite + 1 (anim 21's surfaces 450, 434... are sprites 449, 433...). At
  640x480 the lores anims (8, 10, 13: sprites 425-427) are not even loaded
  (0x0 surfaces in the running game).
- So the brackets are **whole hand-drawn panels**. That answers why no
  single stretch of 427 fitted the capture: the hires HUD never uses it.
- CONFIRMED (code): **text is blitted 1:1**. The glyph loop 0x46a180 copies
  each glyph unscaled (0x4693e0) and advances by its width plus
  [0x4ead00], the current font's letter spacing (+0x88; the line spacing is
  +0x8c), set through 0x469f70 / 0x4182f0(font, lores, hires) at load
  (0x4183c0..0x418737). In the hires set: font 10 (olfnt01) -2, 11
  (olfnt02) -4, 12 (olfnt04) -3, **0 (olfnt03, the speed) -8**, 13 -2, 14
  -14, 15 -2, 2 -4, 3 -4 (line -3), 5 -4 (line -2), 6 -2. Checked on the
  speed: "048" is 114 px of glyphs, 114 - 3 x 8 = 90, and the capture's
  digits span 90 px and 40 rows, which the port's olfnt03 at 1:1 also gives
  (at 0.8 it gave 31 rows).
- CONFIRMED (code): the speed is `"%03d"` (0x4882f8, at 0x41a535) of
  |[0x4d9e34]|, which 0x425eb1 builds as `(int)(v * 3.6)`, truncated. The
  damage percentage is `"%2d@"` (0x488364) in font 16 (lo_fnt1 in both
  sets), whose '@' glyph is the percent sign.
- The port now draws all of this from the table at height / 480 and kept to
  the right edge. Compared with Wine captures of etappe1 and etappe2 the
  brackets, labels and values sit on the original's to within a pixel or two.
- CONFIRMED (code): mode 1 centres on x less half the string width and y
  less half of `[[[0x4ead08] + 0x68] + 0x30c] + 8` (0x41c097..0x41c0cd). That
  is the current font's glyph table (12 bytes a character, as the glyph loop
  0x46a180 reads it), entry 0x30c / 12 = 65, **'A'**, and its height. olfnt01's
  'A' is 17 high, so the CHECKPOINT label's top is 12 - 8 = 4. olfnt02 has
  no letters (LIKELY height 0 for the missing glyph), so the clock's top is
  24, which the capture fit (24.8) agrees with. The port uses 4 and 24.

## The two collision deviations re-tested (session 15, after the traffic change)

- The port's re-test at **t = 0** (added after a car pinned at etappe1 chunk
  330) is **gone**. With traffic on both carriageways, running the original's
  rule (0x441490 returns 0 at t = 0; the car is not moved and the step's
  impulse turns it) gives:
  - unwedges 0 / 0 / 13 / 2 on etappe1 / etappe5 / etappe2 / etappe1 with
    police, against 0 / 0 / 12 / 2 with the deviation;
  - fewer samples under 10 km/h on etappe2 (26 against 40) and on etappe1
    with police (11 against 20).

  The AI cars' step never had it.
- The **overlap** deviation (a box the car already overlaps and moves away
  from does not hold it) **is still needed**. Without it (A2_FAITHFUL_OVERLAP),
  etappe5 did not finish within 13000 frames and etappe2 needed 30 unwedges
  instead of 13.
- What the player overlaps (A2_OVERLAP_LOG, one line per sub-step taking
  the deviation):

  | obstacle | etappe5 | etappe2 |
  |---|---|---|
  | barrier slabs | 80 | 506 |
  | traffic | 0 | 953 |
  | racers | 0 | 21 |

  - LIKELY: the traffic share is a port artefact. The port's traffic follows
    its lane points kinematically and never stops at a contact, so it drives
    into the player's box. In the original, traffic cars are engine objects
    moved by 0x440fb0 with their own contact pass (one step each).
  - LIKELY: the slab overlaps come from the turn. 0x4404a0 turns the object
    over the whole step after the swept move, unswept, in the original as in
    the port, so a corner can swing into a slab. How the original then gets
    free (if it does) was not traced.

## Traffic driving: handle_basic_ai (session 15)

- CONFIRMED (code): HANDLE_TRAFFIC_AI (0x40b770) calls the body (0x40ece0,
  0x40ebd0), the trigger check (0x407fc0), change_lane (0x403660),
  **handle_basic_ai (0x4076b0)** and the sample player. handle_basic_ai is
  **not a vehicle model**. It sets the object's velocity and yaw rate, and
  the engine (0x440fb0) moves the object in one step, stopping at its first
  contact and applying the impulse, as it moves every object. Per frame:
  - **W0** is AI record +0 (the waypoint the car is on) and **W1** =
    0x4075f0(W0). **far** = W0 advanced behaviour+0 times (8 for behaviour
    2, 4 for behaviour 3).
  - 0x4075f0(wp) gives the straight link (+0x14). At a lane's end it gives
    lane +1 (+0x18) or lane -1 (+0x1c), picked by the parity of a timer
    reading (0x441cd0 / _ftol), and failing those the waypoint itself. Its
    second argument is never written, so the record's +0x14 (the target
    lane, below) gains nothing from it.
  - The speed +0x74 is reset to the preferred speed if it is over four
    times that (0x4078f5). The lateral velocity +0x6c / +0x70 is zeroed.
    **calc_speed(far, ai, obj)** (0x407140) adds its change, so the corner
    term looks at `far`.
  - **The yaw rate +0x7c = behaviour+0xc x (fx dz - fz dx) / |d|** (0x406fd0;
    symbolic run in reverse-engineering/tools/emu_traffic.py), with d =
    W1 - car (3D), f the car's forward axis (matrix +0x20) and the gain
    5.890 (behaviour 2) or 8.099 (behaviour 3). It is zeroed below
    1.5 m/s (and for the player's car in game mode 7).
  - **Arrival** (0x406e10, bisected under Unicorn): W0 becomes W1, and
    record +4 steps on, when |W1 - car| < behaviour+4 x |W1 - W0| x
    (1 + (8 - min(|128 - |W1 - W0||, 8)) / 16). behaviour+4 is 1.0 / 1.2,
    so on a regular 128-unit segment that is 192 / 230 units: W1 stays one
    to two segments ahead.
  - At a dead end (W1 or far = -1, only when W0 is -1 since 0x4075f0 falls
    back to the waypoint itself) the speed drops by behaviour+0x18 a frame
    until 0.
- CONFIRMED (code), the lane change: change_lane passes the chooser the
  **physical lane**, the index of object +0x20 (the nearest waypoint, kept
  by 0x4424b0 as objects move) in the chunk's side list (0x403844..0x403873).
  It compares the wanted lane with AI record **+0x14, the target lane**
  (0x403c36). One step a tick: 0x406d90 (down) or 0x406d10 (up) moves W0 to
  the neighbouring lane's waypoint in W0's chunk (W0's back link, then that
  waypoint's lane link), zeroes record +0xc and steps +0x14. So a change is
  made once and steered out, and it is undone only if the chooser changes
  its mind.
- Port (traffic.c `basic_ai`, `sync_car`; main.c): each traffic car is now
  an a2_drive driven this way. The engine's integrator moves it, and it
  takes part in the AI contact pass as a body: it stops at barriers,
  objects, racers, cops, other traffic and **the player**, who is now a
  still body of that pass (the others stop at it and push it). The player's
  own steps treat traffic as boxes, as before, and their impulses go into the
  traffic car's velocity.
  - Lane hold (A2_TRAFFIC_DEV): with lane changes off, a mean of 3-5 units
    off the W0 -> W1 line, worst 12-40 in curves; with the yaw sign flipped
    the cars leave the road, which confirms the sign.
  - The traffic now weaves about as much as the original measured in
    question 50 (6.9 % of traffic ticks change lane): about 7400 lane
    changes on an etappe1 run, roughly 8 % of the ticks. The port's old
    1.2 s slide had damped that.
  - Regressions: etappe1 201.8 s, 0 unwedges; etappe5 271.6 s, 1; etappe2
    1036.8 s, 15; etappe1 with police 220.4 s, 1. All finish.
  - The overlap deviation is still needed. Without it etappe2 and etappe1
    with police stall. On etappe1 the car is held by a barrier slab (chunk
    421, 77086 sub-steps), entered just after a crash at 108 s (178 -> 43
    km/h). See the next section for how it gets in.

## How the car's corner gets into a barrier slab (session 15)

- Port trace (A2_SLAB_TRACE): after every sub-step and between frames, is
  a corner of the player's box inside a `.cmp` slab (a2_contact with a
  vanishing move giving t = 0)? When that changes, the trace logs whether the
  translation alone would have done it.
  - etappe1 with police, faithful (A2_FAITHFUL_OVERLAP): the one entry is
    at 110.9 s, in a **sub-step's turn**. The translation alone left the car
    clear, and the turn of -1 heading unit (0.09 degrees) put a corner about
    0.04 units in.
  - etappe2, the whole race: 215 entries in a sub-step's turn, 1 on a move
    (a float edge), none from the push-outs or the AI pass. 14 apparent
    entries between frames with no motion are the trace's own artefact (its
    nearest chunk, and so its set of slabs, changed).
- So the overlaps come from the original's own rules: 0x4404a0 turns the
  object over the step **unswept**, after the swept move. LIKELY the original
  gets into the same state, since its contact code is the one the port
  matches (1499 of 1500 random cases, fit_contact.py).
- Once a corner is inside:
  - a move that leaves it inside hits at t = 0 on the slab's nearest side;
  - if the velocity points **away** (v . n > 0 in the record's sign)
    0x441490 returns 0 and 0x43f900 moves by velocity x 0, so the car does
    not move at all;
  - only a velocity **into** the slab takes 0x441490's re-test, which sets
    the normal part to 10 outward and moves the car clear.

  The trap is a car sliding along a barrier with a corner a hair inside and
  its velocity slightly away from it (etappe1: v . n = -0.17 m/s, about 0.007
  units a sub-step). LIKELY a player in the original gets out by steering
  into the wall or braking. The port's autopilot steers away, and the car
  stays put. That is why the port keeps its deviation: a car overlapping a
  box it moves away from is allowed to move.

## How a race ends, pause and the quit question (session 15)

- CONFIRMED (code): **the finish** (mode 3, tick 0x4155d0) waits **1500 ms**
  (0x5dc against GetTickCount from the mode's start), then for VK 0x1b (Esc),
  0x0d (Enter) or 0x20 (Space) through 0x466910, held and then released.
  The end callback 0x415690 books the prize and sets mode 4, the teardown,
  and spel.dat ends. **The game over** (mode 6, 0x4162e0) does the same
  after **1000 ms** (0x3e8).
- CONFIRMED (code), check_game_keys (0x425040..0x425084), outside modes 3
  and 6:
  - **Esc** sets [0x4d9ab4] = 2, the quit question. The modal loop at
    0x424938 waits for **J (0x4a) or Y (0x59)**, which set mode 4 and so end
    the race unfinished, or **Esc / N (0x4e)**, which go back.
  - **P** sets [0x4d9ab4] = 1, the pause. It waits for P to be released,
    then for any key to be pressed and released (0x4249d4..0x424a21).
- CONFIRMED (code), the screen 0x419300: element 34, anim 14 (sprite 307,
  345x139, the chequered panel) at (320, 240) in mode 1, then language.ini
  **24-26 (PAUZE / DRUK OP EEN TOETS OM DE / RACE TE VERVOLGEN.)** or
  **27-29 (EINDE / WEET JE ZEKER DAT JE DE / RACE WILT BEEINDIGEN? (J/N))**
  centred at elements 35, 36 and 37 ((320, 170 / 220 / 246)), the first in
  font 10 (olfnt01), the others in font 6 (olfnt07, hires spacing -2).
- Port: `a2view --game` (the menu passes it) does all of this.
  `port/play.sh` runs the whole game from a private play copy of the data.

## The speed camera ignores its pole (session 15)

- CONFIRMED (code): 0x42a2e0, the handler for chunk trigger 1 (referenced
  from the trigger table at file offset 0x89c8c), takes the car, compares
  [0x4d9e34] (km/h) with the posted limit 0x48c480 and the record's
  +0x1c < +0x18 (entered the chunk going forwards). Then it plays sample 11
  and starts the flash sequence. It never looks at the camera object. The
  trigger lies on the pole's chunk or one or two past it, so in the original
  a car that knocks the pole over (the radar bonus, 0x428d30) is flashed as
  well, a moment after or before the hit. Seen in the port's etappe1 run:
  FLITS at chunk 462, then the camera knocked over at 124.2 s.
- Port choice (asked for by the player): the flash is held for up to 0.5 s
  or two chunks and dropped if that camera has been knocked over
  (a2_scene_camera_down). Every other flash is unchanged.

## The load screen (session 15)

- CONFIRMED (debug trace, session 6): spel.dat shows **e0N_w_hi.tga**
  (640x480; w_lo 320x200) while it loads ("Loading wait screen for 16-bit
  mode"). For etappe1 it is the route map "Amsterdam / Den Haag".
- CONFIRMED (code + Wine capture): **the progress bar is the original's
  own.** handle_progress_bar (0x41c410, "Calling 'handle_progress_bar'" at
  0x4316d7) takes the step's percentage from a table at 0x4ab9e8 (indexed
  by the step counter 0x4ab9e4; with [0x4ac98c] set it instead records the
  steps' times into 0x4aaa40, a calibration mode). It calls **0x41c4c0
  (percent)**, which:
  1. redraws the wait screen (0x41bde0(1, etappe));
  2. blits the anim of the resolution's entry at 0x4885b8 (stride 0x3f0;
     lores anim 18 at (246, 177), **hires anim 19 at (489, 425)**) 1:1,
     cut to width * percent / 100 (at least 2 pixels), with 0x4697c0.

  Anim 19 is surface 430 = **RC.IMG sprite 429, 124x33: ten blocks from
  dark red to yellow**. The picture's bottom-right box is drawn to hold it.
  The Wine capture shows it 90 % full on etappe1's map. The picture is
  24-bit, so it goes through the gamma square (0x468380) like the sky.
- CONFIRMED (code + data): the picture's name is **Raser.ini [wait_hi]
  <etappe>** (read into the 13-byte table 0x4aa9b0 by Gm_ini.cpp 0x4220e6;
  [wait_lo] for 320x200). In 16-bit mode the extension is replaced with
  .TGA. The shipped list maps 0 -> **e06** (there is no e00_w_hi; etappe 0
  and 6 are both Amsterdam), 1-6 -> e01..e06, 7 and 8 -> e01. The bar's
  percentages per load step are **[progress] percent0..10**: 10, 23, 72,
  74, 75, 91, 91, 91, 94, 94, 100. They were written by a calibration run
  (calculate=1 records the steps' times into 0x4aaa40).
- Port (game mode): the same picture with the gamma, and sprite 429 revealed
  at (489, 425) as the port loads. The port's four steps take percent0, 2,
  5 and 8 from [progress], then 100. The sprites need the track palette,
  so the bar appears from the third step on. Then "DRUK OP ENTER OM DE RACE TE
  STARTEN" blinks between the title and the map until Enter. The prompt is
  the port's own.

## The start countdown, 3-2-1-GO (session 15)

- CONFIRMED (code): the start camera's end (ev_stcam.cpp, 0x416fe0)
  registers a sequence 0x416b00 / 0x416bd0 / 0x416cb0.
  - **Begin:** it notes the time and puts the start point **400 units ahead
    of the player along its forward axis, 30 up**, with the forward axis as
    the text's direction.
  - **Tick:** every **1000 ms** it launches the next entry of the table at
    0x4869d8 (18 bytes: text, sample, style) through 0x417140: "3", "2", "1"
    with **42 getset.wav**, style 1, then **"GO?"** with **43 go.wav**, style
    2. After the fourth it ends.
  - **End:** it sets game mode 0 (the race), selects camera 1 (0x414b70) and
    raises COP MODE for copland. So the race stands still for 4 s from the
    start of the countdown.
- CONFIRMED (runtime, gdb_countdown.py, etappe1): all four texts spawn at
  (-62939.6, 30.0, 6667.1) with direction (0.8, 0, 0.5) while the player
  stands at (-63278.6, 0, 6454.7), i.e. 400 ahead and 30 up.
- CONFIRMED (code), 0x417140 lays a text out as one engine object per letter,
  **12.5 units apart, centred**, across the direction, facing the player. Its
  glyph comes through the table 0x486b68 ("0123456789A..Z" ÄÖßÜ =\"-*+.!?)
  into FONT3D_ANIM. That anim has LTR_T twice (frame 36), so every index from
  36 on is one glyph late, and '?' (47) shows **LTR_UITR, the '!'**. That is
  why "GO?" reads "GO!" on screen. The glyph meshes are 9 x 13.6 x 5 units.
- CONFIRMED (code + runtime), the styles (0x486ae0, 44 bytes: phase count,
  then phase ids) and the phases (init / update pairs at 0x486ab8):
  - **Style 1 (digits), phase 2** (0x417da0 / 0x417d50): for 0.8 s
    (frames = 0.8 / frame time) the letter moves each frame by (letter -
    player) x -0.9 / frames. It ends 90 % of the way to the car, about 40
    units ahead, and pitches one full turn (+0x68, 4096 / frames a frame).
    Runtime: "3" went from 400 to about 50 units ahead in a straight line.
  - **Style 2 ("GO"), phase 4** (0x417f50 / 0x418050): for 1 s each letter
    turns round the text's centre at its own distance (once, -4096 /
    frames), and spins on its vertical axis with it (+0x6c). Meanwhile the
    centre closes on the player by (player - centre) / frames left, so it
    reaches the car (30 up) at the end. Runtime: the three letters G, O, !
    moved in from 400 ahead.
  - **Phase 3** (both, 0x417ec0 / 0x417f10): holds still for 0.2 s, then
    0x4179e0 removes the letter.
- Port (game mode): the same countdown, with the same timing, motions and
  sounds, drawn with the 3D font's meshes in the world. The race (clock,
  cars, input) stands still for the 4 s. The start camera's fly-in before it
  is not ported. A2_CD_AT pins the countdown for screenshots.

## How many stages (session 15)

- CONFIRMED (data): the game has **7 playable stages**, `etappe0` to
  `etappe6`, and the career runs through them in order. Stage 6 is the
  final, with the prize x4.

  | stage | name | type |
  |---|---|---|
  | 0 | Amsterdam | city |
  | 1 | Amsterdam - Den Haag | highway |
  | 2 | Den Haag | city |
  | 3 | Den Haag - Rotterdam | highway |
  | 4 | Rotterdam | city |
  | 5 | Rotterdam - Amsterdam | highway |
  | 6 | Amsterdam | city |

- Raser.ini [MAPS] also lists **7 MÜNCHEN - BERLIN** (highway, e03's
  textures) and **8 BERLIN** (city, e02's). [wait_hi] / [wait_lo] give them
  e01's map. But neither the CD data nor RCS.IMG has any etappe7.* or
  etappe8.* file, and the career ends after stage 6. LIKELY leftovers of a
  planned German edition (the predecessor was "Autobahn Raser"), not
  playable.
- The port loads and drives all seven (etappe0, 3, 4 and 6 checked under the
  autopilot for 40 s; 1, 2 and 5 are the regression races).


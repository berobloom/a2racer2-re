# spel.dat — initialization & main-loop architecture

Static analysis of `data/spel.dat` (PE32 i386, MSVC, .text = 0x401000..0x47F0B5, entry 0x471400).
All addresses are VA (image base 0x400000). Confidence levels per AGENTS.md.

> **Session-5 correction.** Sections D/I/J below were written on the premise that
> spel.dat contains a *software* rasterizer. That premise is **wrong**: the game
> drives **real Direct3D 2** (DirectX 5), acquired via
> `IDirectDraw2::QueryInterface(IID_IDirect3D2)` at 0x44C760 — which needs no
> `LoadLibraryA`, because `ddraw.dll` loads `d3dim.dll` itself. The corrected
> account is in §K and in `research/discoveries.md`; the sections below are kept
> for their (still valid) address work, but their rendering *conclusions* are
> superseded.

## A. Initialization call graph

```
crt_mainCRTStartup @ 0x471400            (MSVC CRT: GetVersion, heap/ioinit,
   │                                        GetCommandLineA, _setargv loop)
   └─ call main @ 0x431710  (Main.cpp)   [0x47153A]
        │
        ├─ init_main @ 0x431610  (Main.cpp)           [0x431741]
        │    ├─ 0x4209B0 ("raser.ini")      config: reads Raser.ini (incl. [IMG] RCSFILE,
        │    │                               [PROGRAM] RESOLUTION/FOCUS) via ini wrappers
        │    ├─ 0x420750, 0x464C80, 0x431D60, 0x421B40, 0x421FD0   (window/display prep)
        │    ├─ 0x420FE0 (2 args: hInst?)    "Initing 2d skelet" — Skelet2D init
        │    │    └─ creates the DirectDraw object (call site 0x421029 → 0x466110)
        │    ├─ 0x420760
        │    ├─ 0x44C7B0 (device, 10, 10, 0x1000)   dplib 3D device init (D3DLIB.CPP-style):
        │    │    ├─ 0x44C930  D3DINFO.CPP: device enumeration
        │    │    ├─ "init d3d device" (0x45F720/0x45F710)
        │    │    ├─ "setting palette"     0x469D10
        │    │    ├─ "enum texture formats" 0x4628A0/0x4629F0 (TXFORMAT.CPP)
        │    │    ├─ "init lights"         0x44D5B0
        │    │    ├─ "init materials"      0x44E990
        │    │    ├─ "add a viewport"      0x45CAE0/0x45CBE0
        │    │    └─ "clear z-buffer"      0x44CAB0  (real DDSCAPS_ZBUFFER surface — see §K)
        │    ├─ 0x41C1F0
        │    ├─ init_game @ 0x423D20  (gm_init.cpp)  "==== Entry init_game ===="
        │    │    ├─ set_game_display: 0x420730 (mode 0..5) + 0x441E90
        │    │    ├─ ini_load_all:     0x421B70  (all game.ini data)
        │    │    ├─ init_3d_library:  0x4234E0
        │    │    ├─ IDirectDraw2::GetAvailableVidMem([ecx+0x5c], caps=0x15) video-mem check
        │    │    ├─ read game.ini [OPTIONS] FILTER (0x46A960+0x46AAD0)
        │    │    ├─ 0x41B250 / 0x4243F0 (ecx=game object 0x4EA5A0)
        │    │    └─ set_game_input:   0x423910
        │    ├─ game timer init: 0x441AB0 (ecx=&timer object 0x4E4970)  "Initing game timer."
        │    ├─ progress bar:   0x41C410 (handle) → 0x41C2B0 (end)
        │    └─ start_framecount_timing: 0x425450 → 0x470B10 (CRT time 0x474110)
        │
        ├─ start_game @ 0x424E30
        │    ├─ 0x424D20(1), 0x427FD0()
        │    ├─ **sets 0x4D9A78 = 1**  (g_game_running — main-loop keepalive)
        │    ├─ init 8 objects: ids from 0x4D9E94, objects at 0x4E48E0 stride 0xA0, via 0x43FC40
        │    ├─ init 8 more (0x485B58, stride 0x58) via 0x43FC40
        │    └─ debug-key checks (key_pressed 0x466910): 'T','M',CTRL,SPACE,'I'
        │
        └─ main loop (below)
```

## B. Main loop (main @ 0x431710)

```
while (0x4D9A78 != 0)                    ; g_game_running
{
    handle_game              @ 0x424A70
    handle_framecount_timing @ 0x425470
    my_handle_win_msg        @ 0x431540      ; PeekMessageA/TranslateMessage/DispatchMessageA
    key_pressed checks (0x466910): CTRL-Q → Emergency exit (Main.cpp assert path)
    screenshot debug: "lvl%2.2d_%2.2d.bmp" via 0x470AA0
}
exit_main   @ 0x425480
exit_game   @ 0x421AF0      ("Exiting game")
            @ ...           ("Exiting race engine lib")
exit_objects @ 0x457320     ("Exiting objects")
exit_3d_lib @ 0x44CC80      ("Exiting 3d lib")   [D3DLIB/D3DEVDSC shutdown]
exit_skelet2d @ 0x4211C0    ("Exiting 2d skelet")
"Ready"
```

### handle_game @ 0x424A70 — per-frame pipeline, ACTUAL order (from call sites):

| # | call | name (debug string) | role |
|---|---|---|---|
| 1 | 0x441BD0 | gt_update_timer | game clock: QueryPerformanceCounter + timeGetTime (call sites 0x441BE0/0x441C0B) |
| 2 | 0x440850 | rceng_handle_objects | race-engine: objects/physics update |
| 3 | 0x40B2D0 | ai_check_cars_outside_handling_range | AI |
| 4 | 0x4144B0 | handle_game_cameras | camera |
| 5 | 0x425A90 | render_game | 3D render (see §D) |
| 6 | 0x41DD00 | check_game_keys | input |
| 7 | 0x425090 | handle_game_actions | game actions/events |
| 8 | 0x424EE0 | check_player_time | lap/checkpoint timing |
| 9 | 0x434460 | check_player_damage | damage |
| 10 | 0x434380 | draw_2d_display | HUD overlay |
| 11 | 0x4168C0 | update_game_screen | radar surface, blits, flip to screen |
| 12+ | 0x421870, 0x424D20, 0x4214D0, 0x4286A0, 0x4281B0, 0x424D20, 0x403340, 0x408ED0, 0x41BDD0, 0x420760, 0x4689F0, 0x464EF0, 0x441CD0, 0x4248D0 | (unnamed) | sound/timer/sky/etc. sub-updates — to be identified |

Ordering note: input handling (steps 6-7) runs AFTER render (step 5) in the same
iteration — input affects the NEXT frame's render. No frame-skip logic found in
handle_game itself; render_game has an internal 100 ms minimum interval.

### Timing sources (CONFIRMED by import call-site scan)

- QueryPerformanceCounter / timeGetTime: only inside the game-timer functions
  (0x441BD0 region, sites 0x441BE0/0x441C0B/0x441D20/0x441D4E).
- GetTickCount: 19 sites — progress bar (0x41C1F0 region), update_game_screen (0x416B05),
  render_game throttle (0x425AA6), texture anims (0x41AAF9), gm_clock timers (0x4218EC).
- Framecount timing: CRT time() via 0x474110 (start/stop at 0x425450/0x425470).
- input polling: GetAsyncKeyState via wrapper 0x466910 (key_pressed).

## C. Function map (important, verified)

| VA | probable name | evidence |
|---|---|---|
| 0x471400 | crt_mainCRTStartup | PE entry; CRT prologue; calls main |
| 0x431710 | main (Main.cpp) | "Entering main loop", "C:\work\A2racII\Game\Main.cpp" asserts |
| 0x431610 | init_main | "init_main completed.", "Initing 2d skelet/game", renderfocus assert |
| 0x423D20 | init_game (gm_init.cpp) | "==== Entry init_game ====" |
| 0x424E30 | start_game | "Calling 'start_game'"; sets 0x4D9A78 |
| 0x424A70 | handle_game | "handle_game()", per-stage "calling -> X()" strings |
| 0x425A90 | render_game | "calling -> render_game()" caller |
| 0x4168C0 | update_game_screen | "Creating radar: width=%d", blits, GetTickCount |
| 0x441BD0 | gt_update_timer | QPC + timeGetTime |
| 0x425480 | exit_main | "Calling 'exit main'" |
| 0x421AF0 | exit_game | "Exiting game\n" (also contains rd_*/obj_*/cam_* shutdown refs) |
| 0x44C7B0 | dplib device init | "init d3d device", z-buffer/viewport/lights/materials |
| 0x44C930 | D3DINFO.CPP enum | "Type : HARDWARE/SOFTWARE RGB/RAMP driver" strings |
| 0x44CC80 | dplib exit | "Exiting 3d lib", D3DEVDSC.CPP |
| 0x45CAE0 | viewport add | "add a viewport" |
| 0x44CAB0 | z-buffer clear | "clear z-buffer", bufferscreen object 0x4EA9C8 |
| 0x442700 | OBJECT3D.CPP execute | asserts "OBJECT3D.CPP"; called 2× from render_game |
| 0x441DB0 | MESH.CPP loader region | "loading triangle data", "vertices : %d" |
| 0x466910 | key_pressed(vk) | GetAsyncKeyState wrapper |
| 0x46A960 | ini file open/default | called with "game.ini" |
| 0x46AAD0 | ini get int | single GetPrivateProfileIntA call site 0x46AB01 |
| 0x46AB10 | ini get string | single GetPrivateProfileStringA call site 0x46AB4E |
| 0x46AA30/0x46AA70 | ini write int/string | WritePrivateProfileStringA sites 0x46AA61/0x46AABB |
| 0x466110 | DirectDraw create wrapper | single DirectDrawCreate call site 0x466271 |
| 0x46D270 | mode enumeration | DirectDrawEnumerateA site 0x46D284; EnumDisplayModes [ecx+0x20] |
| 0x4650C0 | 3Dfx driver detect | "Righteous dd3ddfx driver found %d" |
| 0x467560 | DirectSound create | DirectSoundCreate sites 0x4675BE/0x46761B/0x467656 |
| 0x436570 | DirectInput create | DirectInputCreateA site 0x436581 |
| 0x420C00-region | skelet2d_set_image_filename | builds "<base>.img"/"<base>.ind" from [IMG] RCSFILE (default "Resource") |
| 0x431540 | my_handle_win_msg | message pump |
| 0x458340 | surface flip | "error flipping surfaces" |

## D. Renderer map (current understanding)

```
handle_game
 └─ render_game @ 0x425A90
     ├─ throttle: GetTickCount; skip if < 100 ms since last (0x4D9E14 last tick,
     │            counter 0x4A63DC)
     ├─ camera matrix math (x87 float): camera pos 0x4D9E50, target 0x4D9E4C,
     │   rotation matrices at 0x4EA4B4/0x4EA418
     ├─ 0x4700E0 (transform helper)
     ├─ 0x41FFF0 ×2  (world/scenery render? rd_* region)
     ├─ 0x467050 ×2, 0x4670F0   (per-frame sound fx)
     ├─ 0x4286A0
     ├─ 0x407660
     └─ 0x442700 ×2  OBJECT3D execute (object render via virtual "objectfuncion")
          └─ object function table dispatch (call eax) → mesh draw

dplib (SUPERSEDED heading — it is a wrapper over REAL Direct3D 2; see §K):
  device (0x44C7B0/0x44CC80) → viewport (0x45CAE0, VIEWPORT.CPP) → lights (LIGHT.CPP),
  materials (MATERIAL.CPP), meshes (MESH.CPP, .d3d loader), textures (TEXT.CPP/TGA.CPP,
  TXFORMAT.CPP formats), draw-order (DRWORDER.CPP: "clipped execute failed"/"execute
  failed" @ 0x448BA0), z-buffer [NOT software — real DDraw surface, see §K]
  (D3DLIB.CPP "init z-buffer"/"clear z-buffer",
  attached to "bufferscreen"/"backscreen"/"screen")
  └─ final pixels to DirectDraw surfaces (surface.cpp wrappers), flip at 0x458340

update_game_screen @ 0x4168C0: radar redraw, sprite/dashboard blits via [esi]/[esi+4]
  indirect surface calls, GetTickCount, 0x459360, then page flip.
```

**D3D question — the earlier answer here was WRONG; see §K.** It is true that
spel.dat has exactly one `LoadLibraryA` site (0x47AE63, loading `user32.dll` from
the MSVC CRT) and no d3dim/d3drm/glide strings. But that proves nothing about
D3D: Direct3D 1-5 is reached through `IDirectDraw::QueryInterface`, and
`ddraw.dll` loads `d3dim.dll` on its own. The "HARDWARE RGB / SOFTWARE RGB /
SOFTWARE RAMP driver" lines are **real `D3DDEVICEDESC` device-class labels**
printed from a genuine `EnumDevices` callback, not Davilex's own naming.

## E. Structures / globals (from reads/writes)

| addr | interpretation | evidence |
|---|---|---|
| 0x4D9A78 | g_game_running flag | main-loop condition; start_game sets =1 |
| 0x4D9A74 | global state/level? | used with "lvl%2.2d_%2.2d.bmp" screenshot |
| 0x4EA5A0 | main game object (this) | ecx for 0x41B250/0x4243F0 in init_game |
| 0x4E48E0 | object array base, stride 0xA0 | start_game: idx*5*0x20 |
| 0x4D9E94 | array of 8 object ids | start_game loop |
| 0x485B58 | array of 8 records, stride 0x58 | start_game loop |
| 0x4E4970 | game timer object (this) | init_game "Initing game timer." ecx= |
| 0x4D8D64 | array of 12 timers (gm_clock) | 0x421870: index 0..0xC, entry size 0x14 |
| 0x4EA7FC | IDirectDraw object ptr | init_game COM call base |
| 0x4EA9C8 | bufferscreen object {w@+4, flags@+0xC, name@+0x14} | z-buffer attach/clear |
| 0x4EA4B4 / 0x4EA418 | rotation matrix rows | render_game float math |
| 0x4D9E50 / 0x4D9E4C | camera position / target | render_game |
| 0x4D9E14 | last render tick | render_game throttle |
| 0x4A63DC | render throttle counter | render_game |
| 0x484AA8 / 0x484AAC | fog near/far (5,7 or 10,14) | init_game sets per mode |
| 0x4895E5 / 0x4895D5 | global "<base>.img"/"<base>.ind" filename buffers | skelet2d image setup |

## F. Resource loading path (first traced path: IMG archive setup)

```
init_main → 0x4209B0("raser.ini") → 0x420C00-region (skelet2d_set_image_filename):
   0x46AAD0: GetPrivateProfileIntA("[IMG]", ...)      (config values)
   0x46AB10: GetPrivateProfileStringA("[IMG]", "RCSFILE", ...)  → "rcs"
   default filename "Resource.img"/"Resource.ind" (globals 0x4895E5/0x4895D5)
   builds "<base>.img" + "<base>.ind" by copying base + appending ".img"/".ind"
open helpers: 0x46DC50(name, x) — file path/open (winlib Filepath/Fileio);
IND parsing and entry lookup not yet fully traced (next on the list).
Mesh load: MESH.CPP region 0x441DB0 ("loading triangle data {\n", "vertices : %d\n")
— .d3d models read from RCS.IMG entries.
```

## G. Evidence highlights

- "Calling 'X()'" / "Entry X ====" debug strings precede every stage call in
  main/init_main/handle_game — direct, unambiguous function naming.
- Call sites of all DirectX/API imports verified individually (capstone scan).
- Exactly 1 LoadLibraryA (user32.dll) + 1 GetProcAddress site in the CRT region.
- The 11 named handle_game stages are in exact call order in 0x424A70's body.
- start_game sets the loop flag that main() tests — loop semantics proven.

## H. Unknowns

- The 14 unnamed per-frame callees in handle_game (steps 12+).
- What the dd3ddfx (3Dfx) detection actually switches (mode table? rendering flag?).
- The byte-level pixel fill loop (texture fetch addressing, wrap/clamp,
  z-buffer width per pixel) — not yet disassembled.
- .d3d / rmp / pmp loader layouts (MESH.CPP loader partially identified).
- Input-device selection logic (game.ini [INPUT] DEVICE=1..4 → di_dev.cpp).

## I. Renderer internals (session 3 findings)

### Projection & math (CONFIRMED)
- Entire pipeline is **x87 float** — 0 `fistp`/fixed-point conversions anywhere in
  .text; ~255 total x87 divides, all in per-vertex/projection/normalization code
  (divides that sit in loops belong to collision routines like 0x4246C5, not pixel loops).
- Perspective focal constant **1024.0 at 0x49B014**; projection = `fdiv [0x49B014]`
  (sites 0x405D93..0x4065F4 etc.). Camera float math in render_game (0x425A90):
  camera pos 0x4D9E50 / target 0x4D9E4C, matrices 0x4EA4B4/0x4EA418.
- ~~**No per-pixel divides exist → texture mapping is AFFINE**~~ **RETRACTED in
  §K**: the rasterizer is not in this binary at all. Original wording: (incremental
  interpolation, no perspective correction). Consistent with 1998 engine,
  chunked OutRun-style road renderer, and the 100 ms frame throttle.

### Z-buffer (CONFIRMED)
- The z-buffer is a **real DirectDraw surface** created with `DDSCAPS_ZBUFFER`
  flags (0x20000/0x20800) via `IDirectDraw::CreateSurface` (vtable+0x18) in
  D3DINFO.CPP (0x44C930, "Z-buffer creation" errors, 0x6C-byte DDSURFACEDESC).
- Attached to the "bufferscreen" through the winlib surface layer; cleared per
  column (0x44CB70) via 0x45D3A0 (surface rect clear → vtable+0x30).
- Depth testing therefore exists, alongside DRWORDER's draw-order sort
  (painter's algorithm) — hybrid visibility.

### Screen composition (CONFIRMED)
- "bufferscreen" (0x4EA9C8) is a T_ARRAY (bounds-checked array template) of
  **per-column sub-surface objects** (one per screen column X): each element is a
  rect {x,y,w,h} + surface interface ptr (+0x2C); a parallel byte-array of
  per-column activity flags at +0x10. Columns are cleared/blitted individually —
  consistent with a chunked/column-based screen update scheme.
- Primary surface is **locked once** at init (0x662D0, `IDirectDrawSurface::Lock`
  vtable+0x50 — the single Lock call site in the binary); blits: 74 sites of
  vtable+0x5C (`Blt`), 5 of +0x58 (`BltFast`), unlock ×2 (+0x54).

### dplib device (CONFIRMED)
- Virtual-device API with D3D-style semantics: draw-primitive dispatched via
  function pointer (0x44D470 has zero direct callers); error names
  ("D3DERR_INVALIDPRIMITIVETYPE", "D3DERR_*") recorded into a state machine at
  0x44D480; error-string lookup table at 0x460520.
- DRWORDER.CPP = draw-order list manager (T_ARRAY at 0x4EA8A4; element walk
  around 0x463590; "clipped execute failed"/"execute failed").
- ~~Per-vertex lighting in 0x455080~~ (corrected session 14): 0x455080 remaps texture coordinates, `u = |u| * scale + offset` per vertex, for a texture region in an atlas. The meshes are not lit: MESH.CPP writes every vertex colour as 0x00FFFFFF (0x455cfa).
- OBJECT3D execute (0x442700) is object name/list management (string helpers
  0x470230/0x470240), not the pixel path.
- render_game calls: 0x4259A0, 0x41FFF0 (×2, world/scenery), 0x442700 (×2,
  OBJECT3D), 0x4286A0, 0x467050/0x4670F0 (sound), 0x4700E0 (transform helper).

### Rasterizer conclusion
**SUPERSEDED — see §K.** This paragraph concluded "software, float-math,
affine-mapped, painter+z-buffer hybrid ... remaining unknown is the byte-level
inner fill loop". There is no inner fill loop in spel.dat: the game submits
D3DTLVERTEX triangle lists to Direct3D. What survives from it: float math,
DRWORDER painter sort combined with a z-buffer, column-organized screen
composition, DirectDraw surface target, ~10 fps redraw cap.

## J. Triangle pipeline (session 4 findings)

The complete polygon path is now identified:

```
mesh render (0x453CF0 region)
 ├─ 0x455080 / 0x4553B0   vertex processing (float per-vertex transforms)
 ├─ 0x455970             draw execution
 │    ├─ 0x454AB0        build draw order
 │    │    ├─ z-buffer path  (0x454EE0 loop): triangles written directly to
 │    │    │     index buffer [obj+0xA0] — no sort (depth via z-buffer)
 │    │    └─ painter path   (0x454F69): triangles + depth key collected,
 │    │          sorted via 0x470860 (comparator 0x454A60), then written
 │    │          to [obj+0xA0]
 │    └─ DrawPrimitive: call device vtable+0x78 with
 │          (flags=4, type=3=TRIANGLELIST, vertex buf [obj+0x88],
 │           ?, index buf [obj+0xA0], count [obj+0xA4], stride=8)
 │          — the device object lives at [0x4EA7FC], vtable offsets mirror
 │            real DirectDraw (0x18 CreateSurface-like, 0x5C GetAvailableVidMem,
 │            0x78 DrawPrimitive)
 └─ 0x455700             texture memory management (vid-mem checks, caps 1/2/8)
```

### Triangle data format (CONFIRMED)
- Triangles = 3×int16 column indices (6-byte records at [obj+0x98]) — each int16
  is a screen COLUMN X coordinate; per-column byte arrays at [obj+0x90] gate
  visibility (a triangle is drawn only if the sum of its 3 column bytes ≠ 0).
- Vertex records carry int16 u/v texture coordinates (reads at 0x443560:
  [v+0x80], [v+0x82], [v+0x84]).
- Draw order = int16 index buffer at [obj+0xA0], count at [obj+0xA4].
- Painter sort key = dword from [obj+0x88] + idx*0x20 + 8 (depth).

### Other session-4 findings
- 0x448BA0 = draw-map (.dmp) loader: rewrites the last 3 filename chars to
  'D','M','G' before opening (rd_drmap.cpp; extension quirk to investigate).
- 0x417870 = z-buffer decision function: calls IDirectDraw::GetAvailableVidMem
  with caps 7 (DDSCAPS_3DDEVICE|DDSCAPS_ZBUFFER|DDSCAPS_TEXTURE) to decide
  whether the z-buffer mode is used.
- Vtables located in .data/.rdata (0x482004 device-ish, 0x482080 texture/object,
  0x47F9B0 T_ARRAY-ish) — entries are thunks into the 0x44xxxx/0x43xxxx regions.


## K. Renderer — corrected account (session 5)

### Direct3D acquisition and device choice (CONFIRMED)

```
init_main
 └─ 0x44C7B0  dplib device init (D3DLIB.CPP)
      ├─ 0x44C760  IDirectDraw2(@0x4EABC4)->QueryInterface(IID_IDirect3D2 @0x480398,
      │              out → g_d3d @0x4EA9CC)          ; call [ecx] = vtable+0 = QueryInterface
      │              on failure: "D3DLIB.CPP(34) : D3DError : %s" (0x460520 = D3DERR name table)
      ├─ 0x44C930  D3DINFO.CPP  IDirect3D2::EnumDevices
      │              logs "D3DDevice Name/Desc/GUID", "Type : HARDWARE|SOFTWARE RGB driver",
      │              render bit depths, light + texture caps
      ├─ 0x4601E0  automatic device choice (D3DEV.CPP):
      │              display bpp [0x49E734] > 8  → HAL  (IID_IDirect3DHALDevice @0x4803D8)
      │                                            fallback RGB (@0x4803C8)
      │              display bpp <= 8            → Ramp/Mono (@0x4803B8), sets [esi+0x2B0]=1
      ├─ 0x460340  explicit device choice: type 1=HAL, 3=RGB, 4=Ramp,
      │              else "D3DEV.CPP(270) : requested unknown device type"
      ├─ 0x4628A0/0x4629F0  TXFORMAT.CPP  IDirect3DDevice2::EnumTextureFormats
      │              → picks "TXF_16BPP_RGB selected = 3" (RGB 5:6:5) in 16-bit mode
      ├─ 0x44D5B0 lights · 0x44E990 materials · 0x45CAE0 AddViewport
      └─ 0x44CAB0 z-buffer clear (real DDSCAPS_ZBUFFER surface)
```

GUIDs embedded in `.data` (VAs): 0x480398 `IID_IDirect3D2`,
0x4803B8 `IID_IDirect3DRampDevice`, 0x4803C8 `IID_IDirect3DRGBDevice`,
0x4803D8 `IID_IDirect3DHALDevice`, 0x4803E8 `IID_IDirectDrawPalette`,
0x4803F8 `IID_IDirectDraw2`.

### The device vtable is IDirect3DDevice2 (CONFIRMED)

Device object at [0x4EA7FC]. Offsets that were previously read as DirectDraw
slots are actually:

| offset | IDirect3DDevice2 method | evidence |
|---|---|---|
| 0x18 | AddViewport | "add a viewport" path |
| 0x24 | EnumTextureFormats | TXFORMAT.CPP; the Wine crash point in 8-bit mode |
| 0x5C | **SetRenderState** | 0x455A57, sets fill mode ([0x4EA864]=2 = D3DFILL_SOLID); error `d3dev.inl(462) : *FILL MODE* …` |
| 0x64 | SetLightState | light state paths |
| 0x78 | **DrawIndexedPrimitive** | 0x455AB8, see below |

### The draw call (CONFIRMED)

```asm
0x455A94  push 0x08                 ; dwFlags
0x455A96  mov  eax, [0x4EA7FC]      ; device
0x455A9B  push ecx                  ; dwIndexCount   = [esi+0xA4]
0x455A9C  mov  ecx, [esi+0xA0]
0x455AA4  push ecx                  ; lpwIndices     = [esi+0xA0]   (LPWORD)
0x455AA5  mov  ecx, [esi+0x8C]
0x455AAB  push ecx                  ; dwVertexCount  = [esi+0x8C]
0x455AAC  mov  ecx, [esi+0x88]
0x455AB2  push ecx                  ; lpvVertices    = [esi+0x88]
0x455AB3  push 0x03                 ; dvtVertexType  = D3DVT_TLVERTEX
0x455AB5  push 0x04                 ; dptPrimitiveType = D3DPT_TRIANGLELIST
0x455AB7  push eax                  ; this
0x455AB8  call [edx+0x78]           ; IDirect3DDevice2::DrawIndexedPrimitive
```

So the vertex buffer at [obj+0x88] is an array of **D3DTLVERTEX (32 bytes)**:
`sx@0, sy@4, sz@8, rhw@0xC, color@0x10, specular@0x14, tu@0x18, tv@0x1C`.
This explains session-4's two misreadings:

- "painter sort key = dword at [obj+0x88] + idx*0x20 + 8" → the `sz` (depth)
  field of D3DTLVERTEX at stride 0x20. Correct measurement, right meaning now.
- "triangles = 3× int16 screen-column indices" → the `LPWORD` **vertex index
  buffer** that DrawIndexedPrimitive requires. Not columns.

### Division of labour (CONFIRMED)

The game does transform, projection, lighting and visibility **on the CPU in x87
float** and hands Direct3D screen-space, pre-lit vertices; Direct3D rasterizes.
Session-3's x87 findings therefore stand and describe the *vertex* pipeline:
focal constant 1024.0 @ 0x49B014, camera pos 0x4D9E50 / target 0x4D9E4C,
matrices 0x4EA4B4 / 0x4EA418 (0x455080 is a texture-coordinate remap, not lighting; corrected session 14).

Two session-3 conclusions are **retracted**:

- *"affine texture mapping, because there are no per-pixel divides"* — the
  rasterizer is not in this binary; D3DTLVERTEX carries `rhw`, so perspective
  correction is available and is the driver's choice.
- *"software z-buffer"* — it is a real DirectDraw `DDSCAPS_ZBUFFER` surface for
  D3D's use (`init z-buffer` / `z-buffer initialized : 1` at runtime), with
  0x417870's `GetAvailableVidMem(caps=3DDEVICE|ZBUFFER|TEXTURE)` being the
  standard DX5 fit check.

DRWORDER.CPP sorting is still the game's own code and still real (runtime:
`T_ARRAY : alloc draw_info_t draw_order [1000]`): the app sorts draw order *and*
D3D depth-tests.

### Runtime-verified allocations (DEBUGON=1)

`_d3d_mesh_ptr c_mesh [1186]` · `g_damages c_damage* [1186]` ·
`_d3d_texture_ptr c_texture [100]` · `c_3dobject _d3d_objects [2500]` ·
`draw_info_t draw_order [1000]` · `light_t [10]` · `material_t [10]` ·
`viewport_t [10]` · `frame3d_t [386]` · `anim3d_t [312]`.

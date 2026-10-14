# Architecture map (preliminary)

Derived from imports, embedded source paths, INI documentation, and data files. Confidence levels per AGENTS.md.

```
                          ┌─────────────────────────────┐
                          │  Shell layer                │
                          │  a2_II.exe → spel.dat       │
                          │  qrace.exe → <x>.dat        │   (CONFIRMED components;
                          │  (reads/writes game.ini)    │    exact flow TBD)
                          └──────────┬──────────────────┘
                                     │ CreateProcess
              ┌──────────────────────┴──────────────────────┐
              ▼                                             ▼
   ┌──────────────────────┐                     ┌──────────────────────┐
   │ menu.dat  (menu FE)  │                     │ spel.dat (game engine)│
   │ C:\work\A2racII\menu │                     │ C:\work\A2racII\Game  │
   │ \Ned\prog\*.cpp      │                     │                       │
   │                      │                     │  gm_main/gm_init/gm_ini│
   │  m_main  (main menu) │                     │  gm_body (game loop)   │
   │  m_garage (shop)     │◄───── game.ini ────►│  gm_clock (timing)     │
   │  m_kopen (buy car)   │   state handoff     │  plr_main/plr_car      │
   │  m_opties (options)  │   (etappe, finished,│  Car_main/car_whl      │
   │  m_nspeler (player)  │   money, damage,    │  ai_main/ai_fsm/ai_path│
   │  m_highscore         │   times, upgrades)  │  ai_lane/ai_wmap/ai_cop│
   │  slotio (save slots) │                     │  obj_main/obj_phys     │
   │  Menuanim (bg anim)  │                     │  rd_chunk/rd_main      │
   │  Smacker.cpp         │                     │  gfx_* (radar, skid,   │
   │  Buttons/Controls/…  │                     │   sky, shadow, spark,  │
   │                      │                     │   clouds, flag, light) │
   │  plays: intro/win/   │                     │  snd_main, prt_main,   │
   │  lose/exit .smk      │                     │  pth_main/pth_heli     │
   └──────────┬───────────┘                     │  txt_main (textures)   │
              │                                 │  tr_* (finish/checkpt) │
              ▼                                 │  ev_* (events)         │
   ┌──────────────────────┐                     └──────────┬─────────────┘
   │ winlib "Skelet2D"    │                                ▼
   │ C:\work\winlib\      │         ┌──────────────────────────────────────┐
   │  surface.cpp (DDraw) │         │ "dplib" / win3ddp 3D software pipe   │
   │  Sprite, Pal, Rcpcx  │         │  c:\visual~1\dplib: MESH, MATERIAL,  │
   │  Forms, Mouse, Msg,  │         │  LIGHT, VIEWPORT, MATH3D, OBJECT3D,  │
   │  Inifile, Fileio,    │         │  DRWORDER (poly sort), TXFORMAT,     │
   │  Mixer, Sound, Font, │         │  TGA, D3DEV (device abstraction)     │
   │  ascii, Clipblit,    │         │  n:\libraries\a2racerii\win3ddp\     │
   │  Bmp, Pformat        │         └──────────┬───────────────────────────┘
   └──────────────────────┘                    │
                                               ▼
   ┌───────────────────────────────────────────────────────────┐
   │ DirectX 5: DDRAW (surfaces) + D3D2 immediate mode via      │
   │ QueryInterface(IID_IDirect3D2) → HAL / RGB / Ramp device    │
   │ DSOUND (60 bufs) · DINPUT · WINMM (mixer/joy)               │
   │ GDI32 (text) · SMACKW32 (menu videos)                       │
   └───────────────────────────────────────────────────────────┘

   Data:
   ├─ etappe0..6.{rmp,pmp,cmp,dmp,omp,pth}   (road mesh, chunk table,
   │                                          rails, AI waypoint graph,
   │                                          static objects, path)
   ├─ RCS.IMG/RCS.IND  → .d3d models + .tga textures (scenery)
   ├─ RC.IMG/RC.IND    → 453 sprite entries (car art?)
   ├─ MENURC.IMG/.IND  → 482 sprite entries (menu art)
   ├─ Mrcs.img/Mrcs.ind → menu PCX/WAV/IMG assets
   ├─ CARS.DAT (player car data) · garage.ini (shop)
   ├─ game.ini / Raser.ini / language.ini / menu.ini
   └─ intro/win/lose/exit.smk (Smacker video)
```

## Subsystem notes (spel.dat)

- **Game modules** `gm_*`: init, ini, clock (QPC/GetTickCount), main loop, body. Function names recovered from debug strings: `handle_game`, `render_game`, `update_game_screen`, `handle_game_cameras`, `handle_game_actions`, `check_game_keys`.
- **Player**: plr_main/plr_car (CARS.DAT), Car_main/car_whl (wheels), 8-zone damage (`handle_mesh_damage`, `mesh_damage_collision_func`, DAMAGE_MESH in game.ini).
- **AI**: FSM per car (`init_ai_states`, `handle_basic_ai`, `ai_chosen_lane`, `ai_chosen_speed`, `change_lane`, `scan_lane`, `race_lane`, `cop_lane`), waypoint map (ai_wmap/ai_path), ADAC/roadside service (adac_lane), cops (init_cop_ai_states).
- **World/objects**: obj_main/obj_phys/obj_dmg; triggers (`init_all_triggers`, `check_racer_trigger`, "EINDE_WEG trigger"); track objects from .omp (`init_track_object_types`); bbox.cpp for bounds.
- **Renderer**: the app transforms/projects/lights in x87 float and submits
  `D3DTLVERTEX` triangle lists to `IDirect3DDevice2::DrawIndexedPrimitive`
  (see spel-architecture.md §K — it is NOT a software rasterizer).
  rd_chunk/rd_main/rd_drmap/rd_rail; chunk culling (visiblechunks=30, "Adac chunk vs player chunk"); textures via txt_main/txt_reml/txt_vbrd (E0x_ALGEMENE_TEXTURES_ANIM animated textures); skidmarks, clouds, radar (surface-drawn road map), dashboard, 2D overlay, 3D font (init_3dfont/init_ascii_fonts).
- **Sound**: snd_main, engine fx (init_sndfx_engine), WINMM mixer volume control, DSOUND positional (3dsound=1).
- **Paths/cameras**: pth_main (track path from .pth), pth_heli (helicopter camera), cameras 1-4, ev_stcam (static cam), ev_start.
- **Cheat/debug**: collisionoff, demo, startchunk, TWEAK_MODE, DEBUGON → debug.txt, INVINCIBLE/COP MODE/QUICK SERVICE (language.ini strings).

## Timing/loop evidence

Imports: QueryPerformanceCounter/QueryPerformanceFrequency + GetTickCount + timeGetTime (WINMM). Game clock module gm_clock.cpp; WAITVBL=0 option (vertical blank wait toggle).

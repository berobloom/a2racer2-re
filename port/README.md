# A2 Racer II — Linux port

Reimplementation effort. The original game data is read as-is; nothing here
modifies or redistributes original files. All format knowledge comes from
`../research/` and `../reverse-engineering/` (living spec).

## Playing the game

```sh
port/play.sh <game data dir>                 # 1280x960 window
port/play.sh <game data dir> --fullscreen
port/play.sh <game data dir> --fresh         # forget the saved careers
```

This runs the whole game as the original does: the intro video, the menu
(new player, garage, showroom, controls, options, standings), the race
(START), back to the menu with the results, and after the last stage the
standings and the win or lose ending. It builds the port first if needed
(CMake, SDL2, OpenGL).

The data directory is never written. The game plays from a copy in
`~/.local/share/a2racer2` (or `$A2_PLAY_DIR`). The data there is linked, and
the files the game saves (Game.ini, Raser.ini, menu.ini, SLOTS.RAS,
options.ras, CARS.DAT, garage.ini) are real copies, so careers persist
between runs.

In the race (`a2view --game`, as the menu starts it) the keys are the
original's:

- the arrows, Ctrl (turbo) and Alt (handbrake), or what the controls screen
  binds;
- 1, 2 and 4 for the camera views; D for the overlay detail;
- **P** pauses (any key resumes);
- **Esc** asks "EINDE ... (J/N)": J or Y ends the race, N or Esc goes back.
  An ended race does not count as finished, as in the original;
- after the finish (1.5 s) or a game over (1 s), Esc, Enter or Space
  returns to the menu.

The viewer's extras (Tab free camera, mouse look, O, G, T, H, [ ], F2) are
off in this mode.

## Architecture mapping (from spel.dat analysis)

| original | port equivalent |
|---|---|
| DirectDraw surfaces (winlib surface.cpp) | SDL2/3 textures + software surface abstraction |
| dplib = real **Direct3D 2** device (`DrawIndexedPrimitive`, D3DTLVERTEX, hardware z-buffer, 5:6:5 textures) | renderer backend: OpenGL/Vulkan taking pre-transformed, pre-lit vertices + WORD index buffers — a 1:1 match, since the game already does all transform/lighting on the CPU |
| DirectSound (60 buffers) | SDL_audio / OpenAL |
| DirectInput + WINMM joystick | SDL input |
| game.ini handoff between menu.dat/spel.dat | the same Game.ini keys, read and written as spel.dat does (`src/results.c`); `a2menu` plays menu.dat's part |
| Smacker SMK2 (menu videos) | `src/menu/msmacker.c`, checked against ffmpeg |

## Track data model (confirmed)

A stage is a chain of 726-840 **chunks**, 128 world units apart:

- `.rmp` per chunk: four lists of 104-byte textured **quads** ("planes") —
  road (max 16), land (max 24), and two more
- `.pmp` per chunk: world position + index lists into the rail and waypoint arrays
- `.cmp` rails (guardrails), `.dmp` the AI waypoint graph, `.omp` static scenery
  with a per-object chunk id, yaw and animation name
- `.pth` scripted object paths, each tagged with a **type**, the chunk range it
  covers and a **time in milliseconds**. Three of the eight types are named:
  **4 the helicopter** (`pth_heli.cpp`, city stages only — the game asserts it),
  **6 the aeroplane** and **1 a police car**. The city stages use types 3/4/5
  and the highway ones 6/7; etappe5 has none at all. Not used by the viewer yet.

The per-chunk index lists partition `.cmp` and `.dmp` exactly, so chunk-local
streaming (`visiblechunks=30`) falls straight out of the data.

## Roadmap

- **Phase A — asset tooling** (`a2tools/`): dump/extract every proprietary
  format to plain formats. *Done for everything the engine needs.*
  - done: IND/IMG archives (both index formats), `.d3d` meshes (+OBJ export),
    **all six per-track map formats** `.rmp/.pmp/.cmp/.dmp/.omp/.pth` (+road
    OBJ export), PCX, TGA, RC/MENURC sprites, `CARS.DAT`, `Raser.ini [MAPS]`.
  - todo: the MENURC palette pairing, `.wav`/Smacker passthrough.
- **Phase B/C — engine + first track** (`src/`, `a2view`): *working.*
  SDL2 + OpenGL viewer: chunked road and land geometry, per-chunk culling,
  colour-keyed and anim-resolved textures, `.omp` scenery placed with its yaw.
- **Phase D — gameplay**: *started.* Working:
  - **The car** drives on the original's own model: the "new (A2 Racer II)"
    steering model, `Game.ini stuur_model=0`, the shipped setting. It is a
    line-by-line port of spel.dat's code (`src/drive.c`), checked against the
    original running under emulation (see "The driving model" below). It runs
    on **CARS.DAT**: per-gear acceleration and top speed, shift points, brake,
    coasting, steering speed and the body's roll, pitch and ride springs.
    The 0x484B18 physics table the port used before belongs to the *old*
    Autobahn Raser I model (`stuur_model=1`), which the shipped game never
    runs.
    Mesh and texture come from `RACER_1_ANIM`. It spawns on lane `min(4, lanes-1)`
    of the `.dmp` waypoint graph, the outermost lane on the right, as the
    original's constructor call (lane 4, clamped by 0x442150) does.
  - **Collision** against the `.cmp` barriers, the solid scenery and traffic,
    with the original's impulse (0x448eb0 / 0x449570; see discoveries.md "The
    collision impulse"): masses and restitutions from the vehicle table and
    the scenery setup, spin from the box corner that hits, and the impulse
    as the impact magnitude for damage and sound.
    Vehicles collide as **capsules** along their own body axis, sized from each
    mesh's bounding box, because the fleet is not one size: a bus is 93.6 units
    (9.4 m) long against a Golf's 41.4. Treating them all as one small circle
    let the player drive into the middle of a bus and let the overtake check
    read a lane as clear when the bus's centre fell between two probe points.
  - **Traffic**: the original's 20 cars of the seven `N_*_ANIM` types
    (Golf, Astra, Polo, Calibra, van, bus, lorry), drawn from its own spawn
    table and driven by its speed model (see "Traffic speeds" below),
    advancing along their lane's waypoint segments and changing lane to get
    round slower vehicles. **Both carriageways** carry traffic: 8 cars drive
    the player's way and 12 come the other way, as the original's spawner
    (0x40ca70) makes them. Every car starts out of play outside the visible
    window. The original's `ai_check_cars_outside_handling_range` (0x40b2d0)
    then brings each one in at the window's far edge (the player's chunk ±
    29), on lane 0, once that waypoint and its neighbours are free. It does
    the same whenever a car falls out of the window. So cars appear 29 chunks
    ahead or behind, never next to the player. Each car is driven as the
    original's handle_basic_ai does it: an engine object whose velocity and
    yaw rate the AI sets (steering at the waypoint after the one it is on,
    calc_speed toward one 4 or 8 waypoints on), moved with a contact pass,
    so traffic stops at the player, the racers, the cops and the barriers
    instead of driving through them.
  - **CPU racers**: three opponents (`Game.ini` `[RACERS] count`), each drawn
    with its own `RACER_<n+1>_ANIM` mesh and driven by the **shipped game's AI**
    (Game.ini `ai_model = 0`, see "The new AI model" below): a state machine
    over the `.dmp` waypoint graph steering a physical car. They **finish**,
    each with a position and a
    time — the two things Game.ini's `[RACERS]` block records per opponent.
    **Race position** is shown in the game's own wording (`POSITIE`,
    language.ini string 38) and reported at the finish.
  - **Autopilot** (`P`, or `--drive-test`) following the lane graph, easing off
    for corners, and **changing lane to pass traffic**: it probes each lane's
    own waypoints a few chunks ahead and holds a clear neighbouring lane until
    the car has crossed into it. It drives all 7 stages end to end.
  - **HUD**: the RC.IMG speedometer gauge plus speed, gear and race time in the
    game's own **3D font** (`FONT3D_ANIM`'s 49 `LTR_*.d3d` glyphs), worded from
    `language.ini` so it follows the game's Dutch/English/German text.
  - **Race scoring**, entirely from the `.pmp` trigger slots, so the scoring
    geometry is the game's own: point-to-point stages by **checkpoints**
    (trigger 45, 1:1 with the `A_CHECK*_ANIM` gantries) finishing at
    `TRIGGER_FINISH_POSITION` (14); city circuits by **laps** off
    the **start/finish line**, which on every circuit is the one chunk carrying
    both trigger 13 and trigger 14, ending after `Game.ini`'s `Nrlaps` (the
    game's own default is 2).
    The car starts where the game starts it: `Game.ini`'s `startchunk` if set,
    otherwise **one chunk before the trigger-13 start line**, which is what
    `init_game` does, less `[PLAYER] position` chunks (the last finishing
    position, 0-based, as the original's `STARTCHUNK - position`).
  - **Results go back to Game.ini** exactly as spel.dat writes them (see
    "What spel.dat writes back to Game.ini" in research/discoveries.md):
    - **At start-up:** `finished=0` is written, and money, damage, position
      and the track's records are read.
    - **At the finish:** the track time, the race record, and the prize for
      the position (55000/35000/15000/0, ×4 on etappe 6, 5000 for last on
      EASY) plus 2000 per radar knocked over.
    - **On exit:** `[PLAYER]`, the three `[RACERS]` rows (unfinished racers
      extrapolated, then ranked around the player) and `[BEST TIMES]`.

    Edits are in place: comments and CRLF line ends survive. `--drive-test`
    runs leave the file alone unless `--save` is given; `--no-save` turns it
    off for play. The original pays the prize at the end of the cheering
    sequence, and the port pays it on the line.
    **Speed cameras** (type-7 objects) flash and fine you against the limit in
    force, read from the `A_MAX50S` / `A_MX120S` sign objects.

  - **Suspension**: the body rolls into corners and pitches under acceleration
    and braking, driven by CARS.DAT's three (damping, factor) pairs — the
    developers' "Rolhoek/Pitch/Hoogte factoren". The cars differ as their data
    says: a Diablo rolls further and settles more slowly (2, 30) than a Trabant
    (3, 20), and the Mercedes' damping of 4 settles fastest.

  Still to do: the per-axle physics behind skidmarks, tyre smoke, the slip sound and the tyre
  model.
- Phase E — menus + videos.

## Building and running the viewer

Needs a C99 compiler, CMake, SDL2 and OpenGL.

```
cmake -S . -B build && cmake --build build -j
./build/a2view <data> etappe0
./build/a2view <data> etappe5 --start-chunk 188     # overrides game.ini startchunk
```

Two modes, toggled with `Tab`:

- **drive** (the default): the keys `Game.ini [INPUT]` binds, as Windows VK
  codes -- by default the arrows (LEFT=37/RIGHT=39/UP=38/DOWN=40), Ctrl for
  turbo and Alt for the handbrake; a2menu's controls screen rebinds them.
  `[INPUT] DEVICE` 2 (joypad) and 3 (wheel/joystick) use the first SDL
  joystick as spel.dat does (stick and buttons 1-2; see discoveries.md
  "Input devices"), falling back to the keyboard when none is attached. `D` is left free because it is the original's
  overlay-detail key. The
  window title shows speed, gear and chunk. Speeds come straight from
  `CARS.DAT`, so the Trabant tops out at its real 151 km/h.
- **fly**: free camera — `W/A/S/D`, `R/F` up/down, click to capture the mouse
  and look around, `shift` to move faster.

**Camera views** are the original's, on keys `1`-`4`. `handle_game_cameras`
(fcn.004144B0) polls those keys and passes a view index to a setter whose switch
writes a three-float offset per view:

| key | preset | offset |
|---|---|---|
| `1` | 1 | +25 up, 60 behind — the near chase, and the default |
| `2` | 3 | **+15.5 up, 1 behind — the cockpit view** |
| `3` | 5 | a toggle: press again to return to the previous view |
| `4` | 0 | +25 up, 100 behind — the far chase |

**All three of those offsets are the game's own**, pulled out of the 11-arm
table at `0x41507C` that the setter `fcn.00414B70` dispatches through. Key 2
used to be a topview here, at an invented 600 up and −89°, because
`init_topview_camera(3)` passes a literal 3. That conflated two index spaces:
that 3 is a camera **slot**, while the setter's argument is a view **preset**,
and preset 3 builds a 1-behind/15.5-up offset and then activates slot **0**.
Slot 3 really is the topview; key 2 just does not show it. All eleven presets
are the game's own now (session 14): an offset in the car's frame, a look
point 10 units above the car (or along it, for the cockpit and preset 4), and
the original's per-frame follow lag of 0.245. Key 3 is **hold** to look back
(preset 5, 4 m ahead), as the original polls it. The cockpit view hides the
car, as the original's draw loop does. With `Raser.ini`'s `TWEAK_MODE=1` (the
shipped setting, "extra camera's") keys `5`, `7`, `8`, `9`, `0` give presets
4, 6, 7, 8, 9. The vertical view angle, 65.6 degrees, is measured off a
capture of the original; `A2_FOV` and `A2_WIN=<w>x<h>` override it and the
window for comparisons.

`D` cycles the **HUD detail level 0-3**, which is what the original does
(fcn.00424EE0 wraps `overlay_detail` past 3 back to 0). The starting level comes
from `Game.ini [GFX OPTIONS] overlay_detail`, and `disable_overlay` locks it off.
In the original, level 2 adds the graphical overlay and level 3 the rest —
including the radar, the checkpoint split, and **labelled bars for TURBO,
SNELHEID and SCHADE** (language.ini strings 53-55, drawn with the 11-frame
chevron bar at RC.IMG sprites 11-21). The port draws the speed and damage bars
at level 3, and the TURBO bar with it. Which of the port's
other elements belong at each level is ours.

Common: `O` toggles scenery, `T` toggles traffic, `G` overlays the `.cmp`
barrier segments, `H` toggles the HUD, `[` / `]`
change the draw distance (defaults to the game's own `visiblechunks=30`), `F2`
screenshots, `Esc` releases the mouse then quits.

`--drive-test` runs a headless, fixed-timestep lap attempt that prints speed,
gear, chunk and position every 40 frames — handy for checking physics and
collision without a display. `--no-traffic` leaves the traffic out, which
isolates lane-following from overtaking when something goes wrong,
`--no-objects` leaves the `.omp` scenery out, which tells you whether something
on screen is a placed object or part of the road mesh, and `--no-racers` leaves
the opponents out.

World scale is ~10 units per metre, derived from vehicle meshes of known real
size (see research/discoveries.md).

Headless (CI, or a machine with no display):

```
Xvfb :97 -screen 0 1280x720x24 &
DISPLAY=:97 LIBGL_ALWAYS_SOFTWARE=1 ./build/a2view <data> etappe0 \
    --frames 40 --screenshot shot.ppm
```

`./build/a2_dump` prints what the C parsers see (archives, meshes, tracks) so
their output can be diffed against the Python tools.

### Source layout

| file | role |
|---|---|
| `src/a2_io.*` | bounds-checked little-endian cursor over a file buffer |
| `src/archive.*` | IND/IMG, both index formats |
| `src/mesh.*` | `.d3d` meshes |
| `src/track.*` | `.rmp/.pmp/.cmp/.dmp/.omp/.pth` |
| `src/pcx.c`, `src/tga.c`, `src/image.h` | texture decoding + colour key |
| `src/gfx.*` | GL texture helpers |
| `src/render.*` | scene build, chunk culling, drawing |
| `src/anims.c` | anim tables read out of spel.dat (mesh + texture per anim) |
| `src/cars.c` | `CARS.DAT` performance table |
| `src/drive.c` | the player car's driving model, ported from the original (see "The driving model") |
| `src/handling.c` | the 0x484B18 physics table: the old model's, read now only for the rear axle and the engine samples |
| `src/skid.c`, `src/smoke.c` | skidmarks and tyre smoke |
| `src/wheels.c` | the racing cars' wheels (car_whl.cpp): placed, spun, steered and stood on the road |
| `src/contact.c` | the engine's box contact test (0x44a520 / 0x44a0f0): time, corner and side of impact |
| `src/audio.c` | the sound effects, on SDL audio |
| `src/paths.c` | the `.pth` scripted objects |
| `src/traffic.c` | traffic cars driving the `.dmp` lane graph |
| `src/racers.c` | the CPU opponents (the new AI's records 0-2) and race position |
| `src/cops.c` | the police: the placed cops, the chase (records 3-14), three strikes |
| `src/ai_car.c` | the new AI's vehicle: the player's car model, driven by throttle, brake and steering, and the rubber band |
| `src/ai_world.c` | the new AI's road: the waypoint graph, obstacle lists and ScanPath |
| `src/font.c` | the game's bitmap fonts (`.FNT`), used by the HUD |
| `src/results.c` | the Game.ini handoff: what spel.dat reads at start-up and writes on exit |
| `src/sky.c` | the sky: the Direct3D `back<N>.pcx` backdrop, or the software panorama (`gfx_sky.cpp`) |
| `src/trackinfo.c` | Raser.ini `[MAPS]` rows, plus a generic ini integer reader |
| `src/sprites.c` | RC.IMG HUD sprites and the 2D overlay pass |
| `src/font3d.c` | the game's 3D font (49 `LTR_*.d3d` glyph meshes) |
| `src/lang.c` | `language.ini` UI strings (Dutch/English/German) |
| `src/main.c` | window, camera, vehicle, main loop |

`a2core` (the four format modules) has no SDL or GL dependency, so it builds and
runs headless on its own.

### Texturing

The game binds textures through its *anim* system, and those tables are static
data inside `spel.dat` rather than any data file. `src/anims.c` reads them
straight out of the executable in the data directory:

- `anim3d` (312 x 9 B) -> frame list; `frame3d` (386 x 16 B) -> a mesh index and
  a texture index; parallel name arrays give the actual filenames.
- Each `.omp` object names an anim, so its mesh *and* texture both follow. All
  79 meshes on etappe0, 54 on etappe5 and 74 on etappe2 resolve.
- The four `.rmp` plane lists take their textures from `Raser.ini`'s `[MAPS]`
  row for the track (args 2-5, in plane-list order: `weg / gras / gra2 /
  vang`). That is deliberately *not* the frame order of
  `E0x_ALGEMENE_TEXTURES_ANIM` (`gras / gra2 / weg / vang`). `_gra2` is the
  building-facade atlas, `_vang` the guardrail/viaduct/treeline atlas.
  The same row gives the circuit flag and the menu name, so those are read
  rather than inferred; `src/trackinfo.c` parses it, and the guessed
  prefix+suffix table in `render.c` is only a fallback for a missing ini.

The offsets are specific to the retail `spel.dat`; the reader validates the
table it finds and falls back to untextured meshes on anything else.

### The new AI model (racers and police)

Game.ini `[OPTIONS] ai_model` chooses between "Old (Autobahn Raser I) = 1" and
"New (A2 Racer II) = 0"; the game ships with 0. Until session 13 the port
followed the old model (HANDLE_RACER_AI and HANDLE_COP_AI's state machine),
which the shipped game never runs; a breakpoint count under Wine showed it.
Now both follow the new one, from `ai_fsm.cpp` / `ai_cfun.cpp` /
`ai_path.cpp`; the evidence is in `research/discoveries.md`, "The new AI
model, traced".

- **One table of fifteen records** (0x4a55a0): 0-2 the racers, 3-14 the cops
  that are chasing. Each frame a transition table (`{from, to, value, fn}`,
  first match wins) moves each record between states, and HandleAIObject
  runs the state.
- **Driving:** ScanPath searches the `.dmp` waypoint graph five waypoints
  ahead, straight on first and then a lane either way, and skips any waypoint
  another vehicle is on. The car steers at the second waypoint, and the
  throttle aims at `70 − angle/15` m/s toward the fourth. The car is a
  physical one: **the player's car model, with the player's car and
  upgrades** (0x40ff90 / 0x410280 index CARS.DAT by the player's car type).
  `reverse-engineering/tools/diff_ai_car.py` checks the port's `ai_car.c`
  against the original's functions under Unicorn, and they match over 3000
  random states.
- **Racers:** states 0 race, 1 / 2 the leader cuts in front of a player
  close behind it, 7 held by the police, 9 finished, 10 unstick, and 11 far
  ahead (over about 450 m), where it holds 60 km/h until the player is
  within about 390 m. They ignore speed limits. The rubber band (0x4014e0)
  scales both the top speed and the acceleration by 0.2 to 2, from the
  racer's lead in chunks, by stage and difficulty. The grid is the original's:
  one car a chunk back from STARTCHUNK, in the player's lane.
- **Police:** the type-9 placements are parked. **A chase starts in the cop's
  own chunk**: a racer there at or over its last speed trigger's limit, or
  the player over 12.5 / 17.5 / 25 / 30 m/s for speed trigger 4 / 5 / 6 / 7
  (8, the posted 120, sets none). At most one cop chases the player on EASY
  and three on HARD; on a city stage a chase starts only with a chance of about
  dt each frame. The cop drives flat out, and once ahead it moves into the target's lane and
  brakes in front of it. The catch is the target **standing still within
  about 7 m** of the cop. After 30 s (60 on HARD), or 50 chunks apart, the cop
  gives up. Catching the player is game mode 8, which shows the warnings and
  then the arrest (three strikes, as before). A caught racer stands for 4 s.
- Runtime check: `reverse-engineering/tools/gdb_sample.py` logs the original's
  records under Wine. It shows the racers at 16.7 m/s in state 11, and a cop
  chasing and holding a racer through states 3, 4, 6 and 17.

`--gas-brake G B` holds the gas for G s of race time and then the brake for B s, with
the autopilot steering, for comparing with `wine_chase_run.sh` on the
original. Combined with `--start-chunk 430` on etappe1, the two cops at chunk
440 chase the player as they do in the original.

`A2_AI_LOG=1` prints each racer's state, speed, inputs, progress, rank and
rubber band once a second.

The police car's collision box is half its mesh's XZ bounds (mpolizei.d3d,
8.21 x 19.39), the original's shape for a registered object.

Ours: the tie-break between racers on the same chunk count, and leaving out ScanPath's fallback rotation (it reads a
chunk record at a float index in the original). The cops update before the racers, so a cop judges a racer by its speed
after the last collisions, as the original's transition pass (0x403340) does
before any state runs. A racer held to 60 km/h in state 11 can therefore be
blocked and caught, as one was in the Wine run. `--no-cops` disables the
police.

### Scripted path objects

The `.pth` paths run as the original's `pth_main.cpp` runs them: the canal
tour boats on etappe0/6, the trains on etappe2/4, the helicopter on etappe2,
the KLM 747 over etappe1, and streams of scripted traffic on etappe1/2/3.

- A path starts when you enter its first chunk on its lap (the `.pth` lap
  field, 0 = every lap), once per lap, on one of ten slots.
- The object follows a Catmull-Rom curve (tension 0.5) through the authored
  points, every segment taking an equal share of the path's time. It stays at
  the end and is removed when you reach the path's last chunk.
- The helicopter flies 5 below its curve, pitches with it, and spins both
  rotors half a turn per frame. The tail rotor sits at the original's
  (0.026, 7.06, -77.58).
- Scripted traffic repeats: each pass picks one of five vehicles and a fresh
  5-9 s duration.
- Each object plays its positional loop (`trein`, `heli`, `vliegt`, and
  `auto2` for the scripted traffic). The boat is silent, as in the original.
  Like the original, path objects cast no car shadow.

Ours: the end-point handling of an open path (the curve clamps to the first
and last points) and timing in seconds rather than frames counted at the
frame rate the path started with. `A2_PATH_CAM=1` (with
`A2_PATH_CAM_DIST=<units>`) is a debug view that watches the first active
object. `A2_CAR_CAM=<deg>` (with `A2_CAR_CAM_DIST=<units>`, default 45) watches
the player's car from that angle off its nose, low down: 90 / 270 are its
sides, which is how the wheels were checked.

### Sound

The race sounds the way the original's `snd_main.cpp` makes it: same samples,
same levels in DirectSound units (hundredths of a dB, offset by `Raser.ini
[SOUND] SFXVOLUME`), mixed in software on an SDL audio device. `--no-sound`
turns it off. Headless `--drive-test` runs are always silent, and the sound
uses its own random generator, so test runs stay reproducible.

- **Your engine**: two loops cross-faded on the engine rpm, the low
  `motor.wav` rumble and the `toeren1.wav` whine (`toeren2.wav` with the motor
  upgrade). Pitch and level follow the original's formulas at 0x427070.
- **Every other vehicle** is a positional loop of its own engine note
  (physics field 55: auto1/2/3, bus, lorry, van) on the original's **20
  slots**. Level comes from the original's distance table (|dx|+|dz|, out to
  2500 units) and pan from the angle (Game.ini `3dsound`). A chasing police car
  swaps its engine for the siren.
- **Events**: graded crunches (carsoft / carmid / carhard at the game's 18000 /
  25000 bands) and the scrape loop with the original's 300 ms rule; turbo
  engage; horn on **T**; flits at speed cameras; repair at the service
  station; checkpoint; the last-five-seconds tick of the checkpoint clock; the
  damage rattles from 65 and 75.
- **The police talk**: a warning line for catch 1, 2 and 3 from the original's
  three pools, chatter as a chase starts, and a give-up line when it ends
  without a catch.

The knock-over props play their hit and bounce samples once each on the
positional slots (`pionb1`/`pionb3` for pylons, `paal` for barriers and
cameras).

**Knock-over props.** Pylons, construction barriers and speed cameras (`.omp`
types 2, 3, 7) fly when the car touches them, using the original's launch,
per-frame flight, bounces and final resting angle (see discoveries.md
"Knock-over props"). They no longer block the car; in the original, no prop
does. A speed camera knocked over raises RADAR BONUS and adds 2000 to the
prize.

The tyre-slip loop (`sliploop.wav`, sample 39) plays while the skid intensity
is 4 or more (0x4259a0).
There is no in-race music in the
original (Raser.ini: "Music is currently disabled"), so none here. The
countdown samples `3git`..`gogit` are in the table but were never shipped.

Ours: stereo samples are folded to mono before panning. (The impact
magnitude is the original's collision impulse now, for barriers, scenery and
traffic alike.)

### The driving model

`src/drive.c` transliterates the shipped model function by function:

| original | what |
|---|---|
| 0x4269c0 | keyboard ramps: throttle +70/s, brake +40/s, steering from +-15 at 200*k/s, back to centre at 400/s |
| 0x40fc30 | the step: ground test, forced handbrake in game sequences, wheelspin, the airborne case |
| 0x40f0d0 | coasting, shifting, accelerating and braking, directly on the forward speed |
| 0x40e9f0 | per-gear acceleration `A[gear] * (1 - v / Vmax[gear])` and the rpm, 800..9000 across the gear |
| 0x40f3c0 | steering, grip, handbrake drifts and burnouts, and the skid intensity |
| 0x40ebd0 / 0x40ece0 / 0x40ee80 | body roll, pitch and ride-height springs (CARS.DAT block B) |
| 0x4404a0 | the engine's integrator: position, heading (4096 units a turn), velocity rotation |

`reverse-engineering/tools/diff_drive.py` runs each original function under
Unicorn on random states next to the port's and compares every output. All
seven, the full step included, **match exactly over 2000-3000 random states**.

The port's view used to be mirrored relative to the original's Direct3D one,
and the steering keys were swapped to make up for it. A Wine capture settled
it: the START/FINISH banner read backwards in the port. The projection now
carries the mirror (`glScalef(-1, 1, 1)`, CW front faces), and the keys map
straight through. Collisions push the car out (the port's own contact
test) and then apply the original's impulse to the model's velocity, spin
and vertical speed.

**Upgrades** (Game.ini `[PLAYER]`), as the shipped model uses them:

| key | effect |
|---|---|
| `motor` | +4 m/s on every gear's top speed (and the `toeren2` engine note) |
| `banden` | doubles the lateral grip |
| `turbo` | fills the turbo tank |
| `abs` | brakes 0.5 harder; it does not prevent anything locking |
| `versnellingsbak` | the upgraded CARS.DAT arrays and their 6th gear |
| `injectie` | nothing |

**Turbo**, on **Ctrl** (`EXTRA1=17`). On the keyboard it does not hold the
gas (handler 0x4265b0); on a joypad it does (0x426a34). While it runs, the model holds the throttle at 100 and
adds 0.05 per throttle percent to the acceleration, and lifts the nose. The
tank is 100 units spent one per frame (0x425cf0), which makes its length
frame-rate dependent; the port keeps the **12 s** measured under Wine.

**Handbrake**, on **Alt** (`EXTRA2=18`); with a joypad, **Space**
(0x426a02), and with a wheel, button 1. It cuts drive and brakes and puts the model into its drift: the
car keeps its momentum while the steering swings the heading, and the slide
lays skidmarks. Held at a standstill with full throttle it is a burnout, and
letting go launches the car.

**Skid intensity** (0..10) comes from the model: turning hard, drifting, and
wheelspin above 80% throttle in the low gears. It drives the **skidmarks**,
the **tyre smoke** and the **slip sound**, as in the original.

### The checkpoint clock and how a race can end

On the **point-to-point stages** (etappe 1, 3, 5) the original races against a
countdown, and the port now does too. The CHECKPOINT clock is drawn top centre
as `mm:ss:t`. It starts at one checkpoint's worth of time, and every checkpoint
adds that much again (with 44 EXTRA TIJD). If it runs out, the race ends with
41 TIJD IS OM. The allowance is the game's own formula (0x424344):

    seconds = 10 * dist[stage] / (top_speed * k)     dist = 142 / 110 / 142,
                                                      k = 0.4 EASY, 0.65 HARD

where `top_speed` is the car's CARS.DAT top-gear ceiling (+4 m/s with the
motor upgrade). It was checked against the original under Wine: etappe5, HARD,
with the shipped save, starts at **00:43**, exactly as computed. HARD is
tight: the autopilot runs out before the first checkpoint. **`--no-clock`**
turns the clock off for physics regressions, the way `--no-cops` turns off the
police.

The race can end in three ways, the three arms of the original's game-over
mode:

| ending | cause |
|---|---|
| 40 TOTAL LOSS | damage reaches 99 (0x424be8) |
| 41 TIJD IS OM | the checkpoint clock runs out |
| 42 GEARRESTEERD | caught by the police a third time |

**Reversing**, as the original's drive function does it (0x40f0d0): hold
the brake until the car stops, keep holding, and it rolls backwards up to
13.333 m/s (48 km/h). The brake lights light only while the force opposes the
motion, so not when reversing on the brake. The original builds the
keyboard brake up as `(int)(brake + 40 * dt)` per frame. Above 40 fps that
never rises, so on a fast machine it brakes only by engine braking and
cannot reverse; at the 20-30 fps of its day it worked. The port keeps the
fraction, so the brake builds at its intended 40 a second at any frame rate.

**Cheats**, the original's: **Ctrl + Space + I** gives INVINCIBLE (no more
damage), and **Ctrl + Space + T** adds 180 s to the clock on a highway stage.

**COP MODE**: naming the player **COPLAND** in a2menu sets Game.ini
`[PLAYER] copland=1`. The race then gives you the police car: the
Lambdaghini's handling under the MPOLIZEI body, with the COP MODE banner at the
start (0x4256a0, 0x416ccd; checked under Wine). The player's car is otherwise
Game.ini's `car=` (0-6), as in the original.

**The police light bar** flashes while any police car has its siren on. As in
the original (0x40c860), it is one texture stamp into CARS01.PCX every 200 ms,
so every police car flashes together, yours too in COP MODE.

**Banners** (EXTRA TIJD, INVINCIBLE, COP MODE, QUICK SERVICE, RADAR BONUS, the
two police warnings, and on circuits NIEUW RONDE RECORD or LAATSTE RONDE at
every lap) go through the original's one banner slot. The latest one is shown for
2 s, blinking every 100 ms, centred at y = 80 of 480 in the label font
(0x41a95b). The port's own checkpoint-split and lap-time banners are gone,
because the original has neither.

**There is no visible car damage, and that matches the game.** The original's
loader tries to open a `<mesh>.DMG` file of per-vertex dents for every model,
and no such file ships: all 2724 attempts in a real run's `debug.txt` fail.
What the damage number does change is the TOTAL LOSS ending, and two rattle
sounds at 65 and 75, which the port now plays too.

`--drive-test` now exits on its own five seconds after the race ends, however
it ends.

### Regression runs

With the shipped save (every upgrade owned), `--drive-test --autopilot
--no-cops --no-clock`, on the ported driving model:

| stage | result |
|---|---|
| etappe1 | **finishes in 201.8 s**, 4th, 0 unwedges, 2 damaging impacts |
| etappe5 | **finishes in 271.6 s**, 4th, 1 unwedge, 2 damaging impacts |
| etappe2 (city, 2 laps) | **completes both laps**, 1036.8 s, 3rd; 15 unwedges on the city corners |
| etappe1 with police | finishes in 220.4 s, 4th, 1 unwedge |

These are on the new AI model (session 13), rerun in session 14 with the
ground planes, the engine's box contact (src/contact.c), the player's 8
contact-limited steps and the AI cars' one. The runs are chaotic: one different
bounce early on changes traffic, wedges and crashes for the rest of the race,
so compare counts and orders of magnitude rather than tenths of seconds.
Before session 14: etappe1 249.7 s, etappe5 383.2 s, etappe2 1253.7 s,
police 271.5 s.

The etappe1 run took 233.7 s (racers 229.7-231.8 s) before the start moved to
the original's lane (session 12). The player lost the time in traffic around
chunks 385-425. The racers finish about 4 s ahead of the player in both runs,
because a racer waits for the player once it is more than 5 chunks ahead
(racers.h). etappe5 took 309.2 s before the lane change and 335.5 s after it
(session 13; the same with or without the knock-over props, which do not
touch the car). The police run is from before the lane change.
Later in session 13, scenery collision changed to the original's object types
(columns, arrows and police cars instead of every tall, narrow mesh), with the
turned bounding rectangle as the shape. etappe1 went from 254.4 s to 235.2 s
and etappe5 from 335.5 s to 310.6 s, mostly because the autopilot no longer
hits lamp posts. The autopilot's stuck check now also uses the distance
actually covered: on etappe2 it sat wedged against a roadworks arrow at chunk
241, reading 11 km/h, until the run was stopped.
Then traffic and racers took the original's lane logic (ai_lane.cpp: lane
choice every tick, and slowing behind the vehicle ahead). The racers now
follow traffic instead of driving through it, which costs them 30-35 s.
Traffic changes lane about 25 times as often as under the old periodic rule;
the original does too: counted in the running game it switches lanes on about
7 % of its traffic ticks (open question 50, answered).
Then collisions took the original's impulse (session 13). Hard hits are no
longer cushioned, so the autopilot loses more time and needs more
recoveries; its times now move by tens of seconds between versions, and they
measure the test driver more than the port.

Figures from before the driving model was replaced
(etappe1 294.3 s, etappe5 365.7 s) came from the old model and do not compare.

**`--no-cops` is now the configuration for a physics regression**, because an
arrest ends the race: the autopilot speeds freely, collects its three catches
and is stopped, so a with-cops run measures how fast it gets arrested rather
than how fast it laps. That is the original's behaviour, not a port defect --
see the arrest note below.

**`--autopilot` runs are now actually reproducible, and were not before.**
Three separate things were reading the wall clock:

- the fixed 1/30 s timestep was gated on `drive_test` alone, so `--autopilot`
  used real `dt` and depended on the machine's frame rate -- the same stage came
  in at 354.6 s with a second viewer competing for the GPU and crawled when run
  alone;
- the override was applied *after* the autopilot's own stuck timer had already
  accumulated `dt`, so the unwedge fired at a different moment each run and two
  invocations differing in nothing but a log string finished 20 s apart;
- animation time came off `SDL_GetTicks()` unless `drive_test`, so the animated
  props -- the traffic lights among them -- ran on wall-clock phase and the
  traffic behaved differently every time.

All three now take the fixed step. Two consecutive etappe5 runs agree
line for line and both finish at 423.4 s with 133 lane changes, so figures from
different sessions can finally be held against each other. Any autopilot number
reported before this point should be treated as a one-off sample.

### Known gaps

- **Animated props now run.** Only five anims in the game have more than one
  frame and are placed as objects: the two traffic lights (3 frames) and the
  three roadworks arrows (2). The mesh names decode the light sequence —
  `c_stoplg` / `c_stoplo` / `c_stoplr` and `c_stplg2` / `c_stplo2` / `c_stplr2`,
  i.e. **g**roen, **o**ranje, **r**ood — so it cycles green, amber, red. 119
  objects across the seven tracks, 300 frames.
  The durations are in the code. The traffic-light handler (0x429480) cycles
  red 5 s, green 5 s and amber 0.5 s on a global timer. The arrows (0x4296b0)
  show frame 0 for 1 s and frame 1 for 0.5 s. All lights and all arrows switch
  together. The lights are decoration: they do not stop traffic or the player.
  (`FONT3D_ANIM`'s 49 glyphs go a different route — `src/font3d.c` loads them
  all by mesh name rather than walking the anim's frame list.)
- The vehicle collides with the `.cmp` barriers and with the scenery the
  original registers as collision candidates: `.omp` types 5 (columns and
  pillars), 8 (roadworks arrows) and 10 (parked police cars). That is
  0x4454a7 → 0x4426c0; see discoveries.md "Which scenery blocks the car".
  Lamp posts, trees, bus shelters and the rest of type 0 never block in the
  original, and now don't here either. Pylons, construction barriers and speed
  cameras are knocked over instead.
  Port choices:
  - the shape is the original's (the mesh's XZ bounding rectangle, turned
    by the object's heading), but the car is a circle against it;
  - anything within 2.5 m of a driving-lane waypoint stays passable, because
    the autopilot has no way round an obstacle on its own line.

  Arrows (13000 kg) and parked police cars (1300 kg) are pushed by the
  impulse and slide to a stop, damped as their type handlers damp them.
- The body's roll, pitch and ride height are the original's springs on
  CARS.DAT block B (with its quirks: the pitch spring reads the roll factor,
  and the road shakes the body by +-4 * v * dt every frame).
- The autopilot is a plain lane-follower with corner lift-off; it recovers from
  barrier hits but is not fast or tidy through etappe0's tight city corners.
  It has no model of a static object, so it noses into posts and sits there.
  These crutches keep a headless run from turning a wedge into a no-result:
  - after 1.5 s below walking pace (by the reported speed or by the distance
    actually covered) it shunts the target lane sideways;
  - after three failed shunts it puts the car on the lane centre, facing down
    the lane with no slide or spin: 3 chunks back the first time, then past
    the spot (3·k chunks ahead) on each repeat within 5 chunks, trying
    another lane;
  - on a barrier hit it turns the car a quarter of the way to the barrier's
    line, because the lane-follower would aim straight back in (a person
    steers away; the original's impulse stops a near head-on hit);
  - after nine repeats it logs `autopilot: stuck at chunk N ... giving up` and
    ends the run. All three are the port's own, for the test runs; nothing like
  them is in the original. The escalation and the give-up matter: before them,
  etappe5 chunk 242 repositioned the car 158 times without ever getting past,
  and the run just spun until the frame cap.
  **Resolved since:** re-run in session 12 (`--drive-test --autopilot
  --no-cops --no-clock`), etappe5 **finishes in 365.7 s**, 4th. The car still
  clips the barrier at chunk 241-243 (115 -> 12 km/h) but drives on without a
  single reposition. The history below is kept for the diagnosis.
  (Without `--no-clock` the run ends at 43 s, TIJD IS OM, before reaching 242.)
  Diagnosed, before that. The give-up message now names the collider, and at 242 it
  is `rail=1 object=1` at (-14759, 46809) -- while chunk 242's nearest driving
  waypoint is (-14711.8, 46806.9). So the car is about **47 units off the road**,
  jammed between a barrier and a parked `MPOLIZEI_ANIM` scenery car at
  (-14770.6, 46797.6). It is not an obstacle sitting on the racing line: the
  lane-follower leaves the road at that spot and cannot get back, and because
  the reposition puts it on a lane centre a few chunks earlier it simply drives
  off at the same place again. Chunk 97 has the same shape (another parked
  MPOLIZEI) but clears on the first reposition.
  `a2_dump track` takes `A2_DUMP_NEAR=<chunk>` to list the objects and driving
  waypoints around a chunk, which is how the above was measured.
  Since braking became real the autopilot is also markedly slower, because it
  now actually slows for corners instead of coasting through them.
- **An arrest ends the race**, as it does in the original: game mode 6
  (`ev_g_ovr.cpp`) with the sub-mode at `0x4867A4` set to 2, whose callbacks are
  the only two fetches of 42 GEARRESTEERD and 43 RACE OVER. The player's car is
  brought to a stop; the race clock keeps running so the opponents' finishing
  times still come out right. The stuck-recovery is gated off while the race is
  over -- without that it read the deliberate stop as a wedge and started
  teleporting a car that was meant to be sitting still.
- **Collisions used to let the car drive through anything it was already
  touching.** The impact penalty fired on the first frame of contact only, so
  for every frame after that the resolver pushed the car back out of the
  obstacle while the throttle kept adding speed: the car sat at a frozen
  position reading a constant 120 km/h indefinitely (etappe5 chunk 97). Both
  the scenery and the traffic paths now bleed the speed off while the car is
  still driving into something.
- Traffic follows **ai_lane.cpp's traffic arm** (session 13): `change_lane`
  runs every tick, scans each lane up to 10 waypoints ahead and 2 behind,
  picks the lane by the original's chooser and follows the vehicle in front
  (`a2_ai_lane_speed`). The player and the racers are in those lanes too, so
  traffic slows behind them and moves over. The slide across takes 1.2 s,
  which is ours. (This replaced a periodic 1.5 s check of the port's own.)
- Vehicle sizes are measured from the meshes for both traffic and the player's
  own car (a Diablo is 18.8 units wide against the Trabant's 16.4). Against traffic,
  racers, cops and solid scenery the contact is the original's (session 14,
  src/contact.c): the earliest corner of either box to cross a side of the
  other over the frame's move, with that side's outward normal and the corner
  as the lever arm. The barriers are boxes too, as the
  original makes them (0x449c10: a 10-unit slab behind each rail). Like the
  original, the player moves in 8 steps of dt/8. A step that would run into
  something is re-tested as 0x441490 does it: the velocity's part along the
  contact normal is set to 10 away from it, and the car moves by that, so the
  car never sinks into a slab (see discoveries.md "The contact geometry, for
  boxes"). The impulse is applied inside the step, as the original's
  0x4497f0 is, so the next step moves with the new velocity. One port
  deviation remains: a box the car is already inside and moving away from
  does not hold it. The original's unswept turn can put a corner a hair
  into a barrier slab. Its contact rule then holds a car that slides along
  the barrier, moving slightly away from it, until the driver steers into
  the wall. The port's autopilot never does, so the port lets such a car
  move. `A2_SLAB_TRACE` logs every entry. The push-out, a circle of the car's half-width, stays as a
  safety net. The AI cars move in one step, as the original moves every object but the
  player, with the same 0x441490 re-test and the impulse at the contact.
- **Traffic speeds are the original's.** Vehicles come from its 12-entry spawn
  table (0x484a48), with the behaviour drawn separately. Both traffic
  behaviours prefer 25 m/s, which is what a real race's debug.txt logs, so a
  bus cruises like a Golf. Traffic slows for corners through the same
  `calc_speed` as the racers, and it obeys speed limits read the AI's way
  (trigger 4 -> 60 km/h, 5 -> 80, 6 -> 120, 8 -> none). The road roller the
  port used to spawn at 8 m/s is not in the original's table, and it is gone.
  Etappe1 under the autopilot dropped from 345.5 s to 294.3 s, and etappe5
  from 418.8 s to 365.7 s, mostly from no longer queueing behind it.
- The HUD follows the game's own layout: a right-aligned stack of RACE TIJD,
  then RACE RECORD and POSITIE on the highway stages (0x419b10) or RONDE
  INFO, POSITIE and RONDE on the circuits (0x419e00), with SCHADE, TURBO and
  SNELHEID lower down. On the highway stages CHECKPOINT sits top centre.
  Every piece sits where the original's 640x480 layout puts it: the hires
  element table that 0x41b860 fills at run time, read back from the running
  game. The brackets are the original's **whole panel sprites** (RC.IMG 313 or
  311 top right, 312 bottom right, 412 for CHECKPOINT), not stretched pieces.
  The text is drawn **1:1 with each font's negative letter spacing** (olfnt01
  -2, olfnt02 -4, olfnt04 -3, olfnt03 -8), as the original's glyph loop does.
  The speed is `%03d` of the truncated km/h and the damage `%2d@` in lo_fnt1,
  whose '@' is the percent sign. The whole is scaled by window height / 480
  and kept to the right edge. It matches 640x480 Wine captures of etappe1 and
  etappe2 (session 15).
  It draws with the game's **bitmap fonts** (`olfnt01..04` for labels, times,
  speed and lap info, `lo_fnt1` for the damage percentage; the `lo_fnt*` set as
  fallback, the 3D mesh font only if neither loads) and the **radar**
  bottom-left as gfx_radr.cpp makes it. It is a north-up map of the whole
  track at 60 units a pixel, built once. Each frame a 128x128 window around
  the player's chunk is shown at 0.7 on a quad turned with the camera. The
  dots sit on the cars' chunks: racers red, chasing cops green, the player
  yellow. Traffic is not shown, as in the original.
- (Superseded in session 13 by "The new AI model": what follows is the
  old model, ai_model 1.) **The racers follow the original's speed model**, recovered from
  HANDLE_RACER_AI and `calc_speed` (0x407140). The behaviour table was read
  out by running `init_ai_states` in an emulator
  (`reverse-engineering/tools/emu_ai.py`):
  - **Which cars:** the ones next to the player's. From the Trabant that is
    Astra, Golf and BMW, the set every race in the shipped `debug.txt` shows.
  - **Speed:** each car has its own behaviour: preferred speed (HARD: Golf
    49.9, Astra 53.9, BMW 63.9 ... Diablo 88 m/s), a corner-brake term, and
    acceleration/deceleration rates. `[OPTIONS] difficulty` picks the HARD or
    EASY set.
  - **Corners:** a racer slows by `brake * |sin(angle to the road ahead)|`.
  - **Speed limits:** ignored, unlike traffic.
  - **Rubber band:** once a racer is more than 5 chunks ahead of the player,
    its speed drops 2 m/s per further chunk. That is why they finish just
    ahead of the autopilot instead of minutes ahead.

  Ours: the corner lookahead (the lane point two chunks ahead), and settling
  on the desired speed instead of stepping across it every frame. Still
  missing: overtaking and reacting to traffic or the player. Their results are
  written back to Game.ini's `[RACERS]` (see `src/results.c`).
- The player's race progress is now **chunks travelled** (the original's
  0x4d9e70). It used to be `(lap - 1) * chunks + chunk`, which on a circuit
  reads a whole lap short between the index wrap and the start/finish line.
  That mis-ranked POSITIE, and with the rubber band it held every racer at
  5 m/s.
- Laps are counted at the start/finish line — the one chunk carrying both
  trigger 13 and trigger 14 on every circuit. This is settled (session 9):
  the original counts at trigger 14, armed by `TRIGGER_FINISH_VALID` (15), and
  the port does the same.
- **The service station works.** `TRIGGER_BENZINE_STATION` (value 2, on the pump
  mesh on all 7 tracks) turns out not to be a fuel stop at all: its handler
  (0x42a4d0) refills the turbo tank to 100, resets the damage, plays sample 47
  `repair.wav` and raises banner 48 QUICK SERVICE. It never touches a fuel
  quantity — which is also why there is no fuel model to miss. The port does the
  three things that matter and shows the banner. Note the trigger sits on a slip
  road, so you have to pull off to use it; the autopilot never does.
- **The city circuits join up.** Their last chunk lies 171-189 units from
  chunk 0 (a step is 128), so the scene treats them as closed
  (`a2_scene_wrap_chunk` / `a2_scene_chunk_delta`): the draw window, the
  barrier and scenery collisions, the props and the vehicles' culling all run
  on round the seam. Before, the window stopped at the last chunk, so the road
  ahead seemed to end and the start appeared all at once after the seam.
- **The scene is darkened as the original darkens it.** Game.ini's `gamma`
  (shipped 1, "dark") makes the original square every colour as it converts
  textures and sprites to 16 bits (`c*c >> 8`, 0x468230). The port does the
  same to the 3D textures, the sky, every stamped sprite and the fonts (gfx.c
  `a2_gfx_gamma_rgba`); the HUD sprites already had it. Without it the port
  looked like bright midday where the original is dark and saturated.
- **Traffic signs show their directions.** The gantry boards and the matrix
  speed sign are stamped into a_TRFFC1 when the player crosses their
  triggers (22-25 / 31-34 and 4/5/6/8), as tr_vbord.cpp does, with Game.ini
  `texturecopy`. The shipped texture has plain blue boards, which is all the
  port showed before.
- **No fog, and none is missing**: the original's device starts with fog off and
  only triggers 16/17 turn it on, which no shipped track uses. **No rain** for
  the same kind of reason: it belongs to the particle module, off in the
  shipped Game.ini (`particles=0`).
- **Tyre smoke works** (Game.ini `clouds`): while the skid intensity is 4 or
  more, puffs at the rear wheels every 100 ms, 0.7 m growing to 3.6 m, rising
  and fading, as gfx_wolk.cpp makes them. The original moves them at 0.6 of the
  car's velocity *per frame*, so at a high frame rate they run ahead of the
  car; that is kept.
- **Skidmarks work** (Game.ini `skidmarks`, fading with `fade_skmarks`): two
  tracks from the rear wheels, 0.4 m wide and laid across the direction of
  motion, darker the harder the skid, textured with the tread strip of
  `S_SFX`, 20 strips of up to 49 segments, each living 20 s.
- **Brake lights work** the way the original's texture path does them: when
  the player brakes (or reverses) the lit tail-light sprite is stamped into the
  player's own RACER sheet, and the unlit one when they stop. As in the
  original, only the player's car has them. With the shipped `object_lights=1`
  the original does not stamp at all: it shows two alpha-blended light cards
  (OBJ_LIGHT01_ANIM, a red glow over each tail light), and so does the port;
  the stamp is the `object_lights=0` path. See discoveries.md "The shipped
  brake lights are object lights".
- **Car shadows work**: every vehicle gets the original's shadow quad, its own
  footprint flat on the road, offset (2, 2), half-transparent, when Game.ini's
  `alpha_blending` is on.
- The particle module is **not** a gap: `Game.ini` says of `particles`,
  "Currently the module isn't used, therefore default=0". `part_rain` is
  separate and does default to 1.
- Every height on the ground comes from the chunk's **ground plane**, as in the
  original (0x442cb0 builds it, 0x442f00 reads it): one plane a chunk, through
  its position and the next chunk's, never banked. Cars, wheels, props and
  shadows all use it. The port used to search the road triangles (list 0) and
  fall back to the nearest vertex. Under etappe1's overpass (chunks 338-341)
  the road the cars drive on is in list 2 and list 0 holds only the deck
  above, so every car jumped 11 m up onto the deck and fell back down. Lane
  points take the waypoint's own height (.dmp), which follows bridges.
- The sky is what the Direct3D game sets as its viewport background: one of
  `back1..7.pcx`, picked at random per race (`rand() % 7` at 0x42357a),
  stretched over the window and still. `--sky N` picks one. `--sky 0` shows
  the software renderer's scrolling panorama (`SCRL_HI.pcx`, band `h - h/3`)
  that the port used before. How the original's hardware drew the background
  texture (stretched or tiled) is not confirmed, because Wine does not draw it.

## The front end: a2menu

`a2menu` rebuilds menu.dat, the game's front end, and runs the race the way
the original shell does: START writes Game.ini, a2view races, and the menu
reads the results back.

```sh
./build/a2menu <data>                   # intro, then the main menu
./build/a2menu <data> --no-intro
./build/a2menu <data> --race path/to/a2view
./build/a2menu <data> --shot garage out.ppm [--click x y]...   # render a screen
./build/a2menu <data> --shot showroom out.ppm --click 530 445 --slide-step 8   # mid-slide
```

- **Data-driven from menu.dat.** The seven screens' layouts are read from
  menu.dat's own form tables, the sprite animations from its animation table
  and the car list from its car table (`src/menu/mdata.c`, v1.01 only). Art
  and fonts come from Mrcs.img and MENURC.IMG in the shared menu palette.
- **Screens:** main, garage (upgrades, repair, car value, property bars),
  showroom (trade-in), controls (device, key binding), options (sound,
  music, resolution, difficulty, options.ras), standings (per stage and
  TOTAAL) and the player list (SLOTS.RAS careers, name entry, delete), with
  the yes/no requester, sounds and per-stage music, the HELP tours (spoken
  help with the explained control highlighted), the Smacker videos (intro,
  win/lose at the end of a career, exit) and the Ctrl+Space+M cheat.
- **The career loop** follows menu.dat: a finished race advances the stage,
  fills that stage's standings and rebuilds TOTAAL; Restart after the last
  stage; win or lose by who leads TOTAAL. Saves are SLOTS.RAS, menu.ini and
  options.ras, byte-compatible with the original.
- **Checked against the original**: every screen was diffed pixel for pixel
  against Wine captures of menu.dat
  (`reverse-engineering/tools/menutools.py diff`); apart from the items
  below they match exactly. The Smacker decoder (`src/menu/msmacker.c`)
  matches ffmpeg frame for frame and sample for sample on all four videos.
- **Approximations**: labels menu.dat draws as Windows text (DrawTextA in the
  system font: the player panel, the property bars, the stage plates, option
  labels) are STDFONT stand-ins. Focus follows the original: a screen reopens on its last
  focused button until the next race.
- The race reads its keys from Game.ini [INPUT] (Windows VK codes), so the
  controls screen's bindings apply in a2view.

See reverse-engineering/menu.md for everything found in menu.dat.

## a2tools usage

Pure Python 3 (stdlib only). Point it at the game's `data/` directory from
your own copy of the CD:

```
python3 a2tools/__main__.py tracks <data>/etappe0.rmp -o road.obj  # road mesh
python3 a2tools/__main__.py tracks <data>/etappe0.pmp                 # chunk table
python3 a2tools/__main__.py d3d   <models>/r_trabbi.D3D -o car.obj    # mesh -> OBJ
python3 a2tools/__main__.py img <data>/RCS.IMG <data>/RCS.IND          # list entries (name/off/size)
python3 a2tools/__main__.py extract <data>/RCS.IMG <data>/RCS.IND out/ # dump entries
python3 a2tools/__main__.py pcx <data>/RCS.IMG -o out/                 # not yet: pcx files
```

Format notes are in the module docstrings, cross-referenced to
`reverse-engineering/file-formats.md`.

## Running the original for reference

The retail binaries honour a `NOCD` environment variable, so the game runs under
Wine with no CD and no install. See `../reverse-engineering/runtime-wine.md` —
useful for A/B-ing the port against the original, and for the very detailed
`debug.txt` trace (`Raser.ini DEBUGON=1`).

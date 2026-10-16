# Running the game under Wine (runtime tracing)

Session 5 result: **both `menu.dat` and `spel.dat` run to completion under Wine,
with no CD mounted, rendering correctly, and emitting a full debug trace.**
This closes open questions 20 (CD check) and 21 (screenshot capture).

Confidence levels per AGENTS.md.

## A. The CD check and its built-in bypass

- CONFIRMED: the CD check lives in `C:\work\winlib\Filepath.cpp`, shared by
  `menu.dat` and `spel.dat`. Both binaries contain the strings `NOCD`,
  `fatal error : no program cdrom was found`, `program installation check failed`.
  `qrace.exe` / `a2_II.exe` / `instlng.exe` do **not**.
- CONFIRMED (menu.dat, VAs): `main` @ 0x4099d6 calls `set_cd_error_message`
  (0x417220) with `"Kan de A2 Racer II CD-rom niet vinden."`, then
  `find_program_cdrom` (0x417260) with `("\data", "intro.smk")`.
- CONFIRMED: `find_program_cdrom` @ 0x417260 does, in order:
  1. `sprintf(buf, "x:%s\%s", subdir, probefile)` (or `"x:\%s"` when subdir is
     NULL) and stores the subdir pointer in the global 0x4456CC;
  2. **`getenv("NOCD")`** (0x4172ba → `fcn.0041c870`, a textbook MSVC `getenv`:
     scans the `environ` block at 0x44F9B0, length-compares the name, requires
     `'='` at name length). If the variable exists, it stores `NOCD[0]` as the
     drive letter in the global 0x4456D0 and **returns immediately** — no drive
     scan, no existence check;
  3. otherwise `find_cdrom_drive` (0x418D20, `Hwinfo.cpp`) returns a drive
     letter from the pre-built drive list, or -1 → the fatal error;
  4. finally it stats the probe path and reports
     `program installation check failed` if missing.
- CONFIRMED: `Hwinfo.cpp` builds the drive list with `GetDriveTypeA`, logging
  `%s = FLOPPY/RAM/HDD/CDROM/NETWORK/UNAVAILABLE` per drive. The loop runs 27
  times, so it also probes a bogus `[:\` drive (harmless off-by-one, visible in
  debug.txt).
- CONFIRMED: `resolve_file_path` @ 0x417330 (`Filepath.cpp`) resolves every data
  file as: `_splitpath` → `_makepath` with empty drive/dir → `fopen(name,"r")`;
  **if the bare filename opens in the current directory it wins**, and only on
  failure does it rebuild the path as `<0x4456D0>:<0x4456CC>\<name>`
  (logging `failed goto path=%s`).

**Consequence:** with `NOCD` set to any drive letter and the game's own `data/`
directory as the working directory, no CD, no install and no patching are
needed. `NOCD` is a developer bypass shipped in the retail binaries.

## B. Reproducible recipe

```sh
# 1. writable copy of the CD's data/ directory (the game writes game.ini,
#    debug.txt and screenshots into the CWD)
bchunk "A2 Racer II (Netherlands) (v1.01).bin" "…​.cue" a2   # → a201.iso
7z x a201.iso -ocd 'data/*'
cp -r cd/data game && chmod -R u+w game

# 2. Raser.ini: RESOLUTION=3 (640x480x16) — see the Ramp-device trap below.
#    DEBUGON=1 enables debug.txt; TWEAK_MODE=1 enables the dev extras.

# 3. an X display whose root window matches the requested mode
Xvfb :98 -screen 0 640x480x24 &

# 4. run from inside the data directory
cd game
DISPLAY=:98 WINEPREFIX=/path/to/prefix NOCD=C wine ./menu.dat   # or ./spel.dat

# 5. screenshots
import -display :98 -window root shot.png
```

Verified with wine-9.0, Xvfb, ImageMagick `import`.

### Two traps

- CONFIRMED: **the X root window depth decides everything.** Wine offers the
  game only the modes its X screen can express (`mode 640,480,32/16/8` in
  debug.txt). On a 1024x768 Xvfb the game logs
  `could not find mode` → `Initlib.cpp(1217) : could not set display mode`
  and hangs. An Xvfb screen of exactly `640x480x24` makes the 8-, 16- and
  32-bit 640x480 modes all available. (The older note that "Xvfb can't do
  640x480x16" is wrong — Wine synthesises the 16bpp mode from a 24bpp root.)
- CONFIRMED: **do not run 8-bit modes under Wine.** `Raser.ini RESOLUTION=0/1`
  (8bpp) makes the device chooser (0x4601E0) request
  `IID_IDirect3DRampDevice` ({F2086B20-259F-11CF-A31A-00AA00B93356}), which Wine
  does not implement; the game then faults inside `EnumTextureFormats`
  (`TXFORMAT : STARTING ENUM TEXTURE FORMATS` is the last line in debug.txt) and
  shows `Unhandled exception!`. `RESOLUTION=3` (640x480x16) selects the HAL
  device and works.

## C. What the debug trace gives you

`Raser.ini [HELPDESK] DEBUGON=1` writes `debug.txt` in the CWD (CRLF, ISO-8859).
A ~40 s run of `spel.dat` produced 312,228 lines. High-value content:

- **Archive ground truth.** `winlib Fileio.cpp(296)` logs every archive read as
  `file open succeeded [name] IDX/COUNT pos OFFSET 3 0` — the entry index and
  absolute byte offset inside the IMG. At startup the loader also dumps the
  whole directory as a `Bestand / Grootte` (name/size) table. Used in session 5
  to prove the named-IND layout (216 offsets + 801 sizes, all exact).
- **Mesh loader.** `**** LOADING MESH **** / mesh : [name] / vertices : N /
  triangles : M` — gives the `.d3d` header meaning directly.
- **D3D device enumeration**, texture-format enumeration, blend-mode caps.
- Named init stages (`init_game`, `ini_load_all`, `set_game_etappe`,
  `init_3d_library`, …) and T_ARRAY allocations with element type + capacity
  (`_d3d_mesh_ptr c_mesh [1186]`, `c_3dobject _d3d_objects [2500]`,
  `draw_info_t draw_order [1000]`, textures [100], damage objects [1186]).
- Per-frame AI/trigger/waypoint tracing (`3 waypoints on the RIGHT`,
  `Checking trigger N`, `WP Position = (x,y,z)`, `Closest = …`).

Note `Closest = 339999995214436420000000000000000000000.000000` is FLT_MAX used
as a "nothing found yet" sentinel.

## D. Gotcha when analysing the log from this repo

`grep` in the Claude Code shell is a wrapper function that silently skips files
under the scratchpad directory. Use `/usr/bin/grep` (or `command grep`) on
`debug.txt` captures, otherwise searches return nothing with no error.

## E. Comparing a scene with the port (session 12)

- Set `Game.ini [ETAPPE] etappe=N` in the writable copy and start `spel.dat`
  directly. With `DEBUGON=0` the race is on screen about 15 s after launch,
  and `import -display :98 -window root` at 16-18 s catches the grid during the
  countdown. Back up both ini files first and put them back afterwards.
- The port side: `a2view .work/game etappeN --frames 30 --screenshot out.ppm`
  on its own Xvfb, with `SDL_VIDEODRIVER=x11` **and `WAYLAND_DISPLAY` unset**.
  Otherwise SDL opens the window on the desktop session.
- Don't guard Xvfb with `pgrep -f 'Xvfb :97' || Xvfb ...` inside a `bash -c`
  string. The pattern matches the shell's own command line, so Xvfb never
  starts. Check `/tmp/.X11-unix/X97` instead.
- Under Wine the sky is unreliable: the Direct3D viewport background
  (`back<N>.pcx`, see discoveries.md) comes out as a blue-to-pink gradient.
  Compare geometry and text, not the sky. Large text such as the START/FINISH
  banner is what settled the mirror question; the licence plate is too small
  to read.

## F. Counting what the running game does (session 13)

winedbg can serve gdb, so a breakpoint can count hits without stopping the
game:

```sh
export WINEPREFIX=$PWD/.work/wp
reverse-engineering/tools/winemenu.sh start .work/menutest spel.dat
winedbg --command "info proc"            # the Windows pid of 'spel.dat', e.g. 0x20
winedbg --gdb --no-start 0x20 &          # serves gdb; ss -ltnp shows the port
gdb -q -batch -ex "python PORT='39021'; OUT='counts.txt'; SECS=60" \
    -x reverse-engineering/tools/gdb_count.py
```

gdb_count.py sets gdb.Breakpoint subclasses whose stop() counts and returns
False, then interrupts after SECS seconds and writes the counts. Breakpoint
`commands ... continue` blocks did not report back. Settled Q50 (traffic lane
changes) this way.

`gdb_sample.py` (same setup) dumps the new AI's fifteen records every Nth
frame from a breakpoint on 0x408ed0, and with `RACER_LIMIT=<m/s>` it also
overwrites the racers' speed limits so that no cop goes after them.
`wine_chase_run.sh` wraps the whole run, from start through gas, brake and
sampling to stop. Holding a key is `DISPLAY=:98 xdotool keydown Up` (Game.ini
[INPUT] has the virtual-key codes); with no steering the car soon hits
something.

`gdb_cutin.py` (session 14) writes the player's position instead: every frame
it holds the car behind the leading racer, from a breakpoint on 0x403340,
which runs before the transitions. The car's position and heading live in its
`c_3dobject`, at `[0x4ea9e0] + obj+0x08 * 153` (+0x50 x, +0x54 y, +0x58 z,
+0x78 heading). **The stride is 153, not 0x88**; with 0x88 the writes land in
other objects. To keep cops out it writes `ret` over 0x409060 in the
process only. Use `ss -ltnp | grep wineserver64` for the gdb port: a stale
`wineserver` socket from an earlier run can be listed first, and gdb then
fails with `vMustReplyEmpty: timeout`.

## G. The test copy's spel.dat is patched (session 15)

`.work/menutest/spel.dat` used to be a symlink to `.work/game/spel.dat`. It is
now a separate copy written by `reverse-engineering/tools/patch_wp_guard.py`,
which changes 32 bytes at 0x40b6bc..0x40b6df. Without it the traffic recycler
reads waypoint -1 and Wine kills the game, on etappe2 within about 10-20 s
(open question 55, discoveries.md "The waypoint -1 read in 0x40b650"). The
patch makes a missing next or previous link count as "not free". Everything
else is byte-identical, so addresses and gdb scripts are unaffected. The
original in `.work/game` is untouched. To go back:
`rm .work/menutest/spel.dat && ln -s ../game/spel.dat .work/menutest/spel.dat`.

## H. Running the menu on a desktop display (session 15)

- On the Xvfb 640x480 display `wine ./menu.dat` (from .work/menutest, prefix
  .work/wp, NOCD=C) reaches the HOOFDMENU.
- On the user's desktop (DISPLAY=:1, XWayland, two monitors 3840x1080) it
  starts, flips about 98 frames and exits with code 40 within 20 s; the log
  shows `xinerama_get_fullscreen_monitors Failed` and DirectDraw texture
  warnings. LIKELY the exclusive 640x480 mode switch or the focus loss.
- `wine explorer /desktop=A2Racer,640x480 menu.dat` opens the desktop
  window but starts nothing: explorer will not launch a `.dat`. The same with
  `a2_II.exe` also started nothing. A registry virtual desktop
  (HKCU\Software\Wine\Explorer Desktop=Default, Desktops\Default=640x480)
  still exited; it was reverted.
- **Settled (later in session 15):** the menu does run on that desktop. The
  exit code 40 was START being pressed (the user was clicking it): menu.dat
  exits with 40 for "start a race" and leaves running spel.dat to the shell
  a2_II.exe, so menu.dat started on its own just closes at START. Start
  **a2_II.exe** instead.
- The race then stopped with `Initlib.cpp(1217) : could not set display mode
  (operation is not supported)`. At START the menu writes its own options
  into Raser.ini: the resolution from **options.ras** (i32 at offset 10;
  0 320x200x8, 1 640x480x8, 3 640x480x16), whose default is 0. The 8-bit
  modes do not work under Wine (discoveries.md), so .work/menutest's
  options.ras now holds 3 (the old file is options.ras.bak_res0). With that,
  shell -> menu -> START -> race ran on Xvfb :98 (etappe0).
- `reverse-engineering/tools/play-original.sh` starts it on the current
  display and puts the resolution back to 3 first each time.

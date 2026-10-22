# Open questions

## Launch / process flow

1. **RESOLVED (session 9).** qrace.exe is the **Quick Race dialog** (window
   class `3DS2N64`): it writes the chosen settings into **game.ini** --
   `[PLAYER] car`, `[ETAPPE] etappe` and the upgrades versnellingsbak / turbo /
   abs / banden / motor, plus copland and damage -- then appends **`.dat`** to
   text from dialog control 40 and launches that with CreateProcessA.
   **No game.ini key names the program to launch**; the premise of the question
   was wrong. See "The three executables" in discoveries.md.
2. **RESOLVED (session 9).** a2_II.exe is the **shell**: a windowed app (class
   `MOTOR`) that alternately launches `menu.dat` and `spel.dat` by those exact
   names, each blocking on WaitForSingleObject, so nothing is renamed. It is
   what a shortcut points at. See "The three executables" in discoveries.md.
3. **RESOLVED (session 9).** **menu.dat exits and the shell relaunches it.**
   a2_II.exe waits for it, reads its exit code, and launches `spel.dat` only
   when that code is **40** (0x28 at 0x401292); after the race it loops back to
   menu.dat. No tracing was needed -- the shell's own code says it.

## Rendering

4. **RESOLVED (session 5), replacing the wrong session-3/4 answer**: the game
   uses **real Direct3D 2**. The call at 0x455AB8 is
   `IDirect3DDevice2::DrawIndexedPrimitive` (vtable+0x78) with
   D3DPT_TRIANGLELIST / D3DVT_TLVERTEX and a WORD index buffer. There is no
   in-binary fill loop to find — rasterization happens in the D3D driver. The
   app-side pipeline (x87 transform/projection/lighting → D3DTLVERTEX, DRWORDER
   sort, hardware z-buffer) is confirmed. See research/discoveries.md.
5. **RESOLVED (session 9).** It switches **nothing about rendering** -- it is
   device *selection*. (0x4650C0 is not it, incidentally: that is a
   DirectDraw HRESULT-to-string mapper.) The real detection is at **0x46d130**,
   a chain of prefix comparisons against the enumerated driver's name --
   **`3dfx`, `DD3Dfx`, `R3D`** (Righteous 3D) and **`mnstr2`** (Diamond
   Monster 3D II) -- each logging "<name> driver found %d" and writing that
   device's **index** into the single global **0x4a0ecc**.
   fcn.0046d290 then takes a selector 1..4 and hands back the matching driver
   record (216-byte stride) out of the array at 0x4f0000: selector 1 uses
   0x4a0ecc and logs "using 3dfx driver %d", selector 2 uses the separate
   0x4a0ec8 and logs "using unknown secondary driver %d". A global still -1
   means that class of device was not found. So the whole mechanism is "which
   enumerated D3D device do we open", not a per-card code path.
   Now STRONGLY SUPPORTED that it selects the Voodoo's *secondary DirectDraw
   device* so the D3D HAL comes from the 3Dfx board (see hypotheses.md).
   ~~To confirm: find the consumer of 0x4650C0's return value and whether the
   chosen DDraw GUID is passed to `DirectDrawCreate` at 0x466110.~~
   **Confirmed (session 15):** it is passed, as the GUID. See discoveries.md
   "Which DirectDraw device the game opens".
6. **PARTLY RESOLVED (session 5)**: 16-bit mode renders to 5:6:5 and the game
   picks the 5:6:5 D3D texture format (`TXF_16BPP_RGB selected = 3`); alpha
   formats 5:5:5:1 / 4:4:4:4 / 8:8:8:8 are enumerated and alpha blending caps
   are queried. The z-buffer is a real DirectDraw DDSCAPS_ZBUFFER surface, not a
   software buffer. **Answered (session 16):** the z-buffer depth is a
   constant 16, and only textures ask for video memory; Raser.ini FOCUS is
   overridden in RESOLUTION 3 / 4, so the game renders to the back buffer.
   See discoveries.md "Surfaces, memory placement and the z-buffer depth".
7. **RESOLVED (session 9).** They are not unnamed -- `handle_game()`
   (Gm_main.cpp) prints `calling -> X()` before each one, so the game labels its
   own frame loop. In order:

   | # | callee | what it is |
   |---|---|---|
   | 1 | 0x441bd0 | `gt_update_timer()` |
   | 2 | 0x440850 | `rceng_handle_objects()` |
   | 3 | 0x40b2d0 | `ai_check_cars_outside_handling_range()` |
   | 4 | 0x4144b0 | `handle_game_cameras()` |
   | 5 | 0x425a90 | unlabelled |
   | 6 | 0x41dd00 | unlabelled |
   | 7 | 0x425090 | `render_game()` |
   | 8 | 0x424ee0 | `check_game_keys()` |
   | 9, 10 | 0x434460, 0x434380 | unlabelled |
   | 11 | 0x4168c0 | `handle_game_actions()` |
   | 12 | 0x421870 | `check_player_time()` |
   | 13 | 0x4281b0 | `check_player_damage()` |
   | 14 | 0x41bdd0 | `draw_2d_display()` |
   | 15 | 0x420760 | `update_game_screen()` |

   Conditional calls in between include **0x424d20** -- the **game-mode
   setter** resolved under question 18, which is how a race switches into the
   arrest sequence -- and **0x4286a0**, the **SNDFX sample player** identified
   with the radar bonus. 0x4214d0, 0x403340 and 0x408ed0 are still unlabelled.
   The loop ends with `end of handle_game()` and an assert,
   `(winmain) - Illegal renderfocus.`

## File formats

8. **RESOLVED (session 5).** `.rmp` is not "groups of vertices+triangles": it is
   `u32 hint; u32 chunkCount;` then per chunk four count-prefixed lists of
   fixed 104-byte planes (4 vertices + 2 triangles). `.pmp` is the chunk table
   (position + rail/waypoint index lists), not "positions + variable records" —
   the `2` markers seen earlier were the waypoint counts. Both byte-exact on all
   7 tracks. See file-formats.md.
9. **RESOLVED (session 5).** `.dmp` is the **AI waypoint graph**
   (`c_waypoint g_driver_map`, 36-byte records with 4 neighbour links), and
   `.cmp` is the **rail/barrier list** (`c_rail g_rails`, 24-byte records =
   position + delta). Both confirmed by runtime allocation names and by the fact
   that the `.pmp` index lists partition them exactly.
   **`.dmp` field 3 is RESOLVED (session 7): it is the waypoint's `y`**, so the
   record's three coordinates are x, y, z in natural order. It equals the owning
   chunk's y for 28452 of the 28461 waypoints and follows bridges; the nine that
   differ sit off the chunk centre (two are the etappe1 fuel-station slip road).
   See file-formats.md. **Still unidentified:** `.dmp` field 1 — though it is 0
   in all but 4 waypoints, so there is little left to identify — and the bit-7
   flag on `type`. On the flag, three hypotheses were **tested and rejected**
   (session 7): it is not a left/right carriageway marker (it appears on both
   sides), not a lane-count-change marker (only 7-9 of ~20 flagged chunks per
   motorway stage are near one), and not a slip-road/off-road marker (only 60 of
   the 1461 flagged waypoints lie more than 250 units off the centreline, while
   2011 off-road waypoints do **not** carry it). It is set on 1461 of the 28461
   waypoints, and the base type it accompanies varies by track (etappe2 is all
   type 2, etappe3 mostly type 4, etappe0 mostly type 0). (`.pmp` f3/f4 were resolved in session 6 — they are the
   per-chunk AI trigger slots, see question 38.)
11. **RESOLVED (sessions 7-12)**: the three header fields are f2 the path type, f3 the lap (session 12) and f4 the duration in ms (session 10). `.pth` is a list of
    **camera paths**: `i32 version(-1); u32 count;` then per path six dwords
    (`first_chunk, last_chunk, f2, f3, f4, point_count`) and `point_count`
    records of `u32 flag; float x,y,z`. All 7 tracks parse byte-exactly, and
    the layout is confirmed against the loader fcn.00433710 (`pth_main.cpp`),
    which reads exactly those fields in that order. The points are **not** the
    centreline: they sit 300-1800 units off the road, and `first_chunk`/
    `last_chunk` bracket the stretch they cover (158 of 169 points' nearest
    chunk falls inside the range; the rest overshoot an end by 1-4 chunks).
    **Session 10: `f4` is a time, and "camera paths" is the wrong name.**
    `pth_main.cpp` raises **"Time not set for path %d"** when the field is
    missing, so f4 is a duration -- the authored 5000..50000 are **5 to 50
    seconds in milliseconds**. And at least three of the eight path types drive
    **objects, not cameras**: type 4 is the **helicopter** (`pth_heli.cpp`,
    loading C_HELI_ANIM / C_LBLADE_ANIM / C_SBLADE_ANIM and asserting
    "Helicopter paths only available in city tracks!"), type 6 the **aeroplane**
    (A_VLIEGT_ANIM) and type 1 a **police car** (MPOLIZEI_ANIM). So `.pth` is a
    list of **scripted paths**, of which some may be camera paths -- the five
    unnamed types leave that open -- but it is not a camera-only file.
    **Session 12: RESOLVED.** `f3` is the **lap** a path plays on (1-based,
    0 = every lap; fcn.00434460). The type table starts at 0x4966F0, not F4,
    so the types are 2 police car, 3 a canal tour boat (STRONGLY SUPPORTED, seen in the port), 4 train, 5
    helicopter, 6 random traffic vehicle, 7 aeroplane. See "`.pth` f3 is the
    lap, and the path types re-read". (Earlier:) See "The `.pth` camera paths are not cameras"
    in discoveries.md, and reverse-engineering/file-formats.md.
12. **RESOLVED (session 5).** The named IND format is a fixed layout —
    `u16 count;` then `{ char name[20]; u32 offset; }` per entry, an exact fit
    for both RCS.IND and Mrcs.ind. Nothing remains: the trailing "field bytes"
    this entry used to ask about are uninitialised leftovers of the writer's
    shared name buffer and carry no meaning. Validated against 216 offsets and
    801 sizes logged by the game itself. See file-formats.md.
13. **RESOLVED (session 5).** RC/MENURC entries are 8-bit sprites:
    `u16 w,h,anchor_x,anchor_y;` + `w*h` pixels, index 0xFF transparent;
    exact for all 453 + 482 entries. RC.IMG is the in-game HUD art (entry 1 is
    the speedometer dial) and pairs with the per-track palette. MENURC.IMG uses
    the palette shared by the menu's full-screen PCXs (answered session 12; see
    file-formats.md).
14. **RESOLVED (session 6).** `CARS.DAT` is fully decoded, with the
    developers' own field names recovered from the TWEAK_MODE menu still
    present in spel.dat and each one tied to a field by its editor's read/write
    address: acceleration and speed per gear (stock + upgraded), Schakelpunt
    vooruit/achteruit, Remkracht, Motor vertraging, Stuursnelheid, and three
    (damping, factor) suspension pairs for roll, pitch and ride height.
    See research/discoveries.md. A[29] is answered by #34 (the body's roll
    rate). **Still open:** A[26] (always zero).

15. **RESOLVED (session 9).** They are menu.dat's two archives, mirroring
    spel.dat's: `Mrcs.img`/`Mrcs.ind` named, `menurc.img`/`menurc.ind` binary.
    menu.dat names both in its own strings, and Mrcs.img **nests seven further
    archives** (CARVED, SMALLBT, ascii12, cyber, BUTFNT, STDFONT, midfont) that
    menu.dat also names. See "Which binary owns which archive" in
    discoveries.md.

## Gameplay

16. **RESOLVED (session 9), as a misreading.** `lan_main.cpp` is not LAN: it is
    the **language** module. Its path string sits immediately before the
    language.ini strings (`Couldn't find text for index %d, language: %s`,
    `text_count`, `NEDERLANDS`/`ENGELS`/`DUITS`), and the text getter that
    asserts against it, fcn.00431d30, has 24 call sites -- it is the most-used
    helper in the UI. There is **no multiplayer code**: spel.dat holds no IPX,
    no winsock and no network API at all. The only "network" strings are
    `%s = NETWORK` from Hwinfo.cpp's GetDriveType enumeration and a Windows
    boot-mode string.
17. **RESOLVED (session 9).** The DAMAGE_MESH/DAMAGE_VALUE 8-element lists are
    **the eight directions round the car**. `plr_car.cpp`'s collision handler
    dispatches a signed value in [-11,+11] through a case table at 0x42770c; the
    only live values are **+/-1, +/-9, +/-10, +/-11** -- the eight neighbours of
    a cell in a **10-wide grid** -- mapping to zones 0..7, and everything else
    (0 included) raises `Impossible collision! (Center of object)`, which is
    what a zero difference means. Each hit adds the impact scaled by **0.5** to
    `0x4d9e74[zone]`, the array behind DAMAGE_VALUE, and DAMAGE_MESH[zone] is
    that direction's damage frame, **-1 for undamaged**. See "The eight damage
    zones" in discoveries.md.
    The scalar model was already resolved in session 8: no damage below an
    impact of 18000, and between 18000 and 35000 from about half to all of a
    per-impact maximum of **12 on HARD / 6 on EASY**.
    **The frame "thresholds" are RESOLVED (session 10), as a negative: there are
    none.** The applier at 0x4275f5 ("Applying regional damage to object %d,
    region %d, level %d.") accumulates the impact into `DAMAGE_VALUE[zone]` and
    then sets **`frame = (int)(frame_count * DAMAGE_VALUE[zone])`**, where the
    count comes from the car model's own descriptor (0x43fc20, via
    `record[+0x98]` and the table at 0x4E6718). So the per-zone value is a
    **0..1 fraction** mapped linearly onto however many frames that model has --
    no authored ladder exists. `DAMAGE_VALUE` is 8 floats at **0x4D9E74** and
    `DAMAGE_MESH` 8 ints at **0x4D9E94**; `Gm_ini.cpp`'s `load_player_info`
    round-trips both through `Game.ini [PLAYER]`, demanding exactly 8 values, so
    **damage persists between races per zone**, with -1 meaning undamaged. Also
    confirmed on the way: **0x4281C0 is `set_damage`**.
    **The grid is RESOLVED too (session 10).** The handler's own trace
    "l_width_zone = %d, l_height_zone = %d, l_zone = %d." gives the formula:
    three sign comparisons yield two components in {-1, 0, +1} and
    **`l_zone = l_height_zone + 10 * l_width_zone`** (0x4273ae). So the width
    component carries the factor of 10 -- a pure +/-10 is a sideways hit, a pure
    +/-1 an end-on one -- and the eight zones turn out to go **round the car in
    order**: 0 (0,+1), 1 (+1,+1), 2 (+1,0), 3 (+1,-1), 4 (0,-1), 5 (-1,-1),
    6 (-1,0), 7 (-1,+1), a full rotation with 0 opposite 4 and 2 opposite 6.
    Every case applies the same 0.5 scale, so corners are not weighted.
    **Session 11: moot for the visuals** -- no `.DMG` damage file ships, so
    no model has damage frames and nothing is ever drawn dented (see
    "Visible damage does not exist in the shipped game"). The phase only matters
    for the DAMAGE_VALUE bookkeeping now.
    **The phase is RESOLVED (session 12), and moot.** Zone 0 is the mesh's
    +z end and zone 2 its +x side (CONFIRMED in model axes; LIKELY the nose
    and one flank). But `handle_mesh_damage` (fcn.00427190) returns before any
    of this for every model without damage frames. The shipped debug.txt shows
    190 entries and 190 "no damage frames" returns, and never a zone line. So
    DAMAGE_VALUE/DAMAGE_MESH are never written at runtime in the shipped
    game. See "The regional damage code never runs" in discoveries.md.
18. **RESOLVED (session 9)**; the "two unread helpers" are named below (change_active_camera and a camera-parameter setter), in the old AI model the shipped game does not run. The structure is now documented (see
    "The police" in discoveries.md): `t_cop` is a 32-byte record in a T_ARRAY
    at 0x4a5238; each cop carries a **max speed** (+0x14) and **max lanes**
    (+0x18) that the `.pmp` **speed and lane triggers set** -- trigger 4/5/6/8
    gives 60 / 80 / 120 / 2x-default km/h (corrected session 12) and 18..21 gives 1..4 lanes -- so the
    per-chunk triggers are the law the police enforce. A cop judges three
    offences: speed, lanes, and side (`Ghost!`, wrong-way driving). The pursuit
    driving rule is known (more than 5 chunks behind -> run on the AI speed
    table; otherwise match the target, floored at 15.0).
    The thresholds are now known too (old model only, which the shipped game
    never runs; the shipped check 0x409060 has no margin, see Q54):
    **speeding is 10% over the limit**
    (`max_speed * 1.1 < target_speed`), the **lane check is a no-op** in the
    shipped build (no comparison between its two printfs), the side check is a
    plain equality, and a pursuit times out at **40.0**. The state machine is
    confirmed from the jump table at 0x4029fc (0 lookout, 1 pursuit, 2 got him,
    3 quitting, 4 calling it quits). Being caught is a **three-strike** rule
    driven by `g_times_caught` (0x4a70e8): catches 1 and 2 raise language.ini
    49/50 (`EERSTE`/`LAATSTE WAARSCHUWING`) and play a random police voice line,
    and the third takes a separate arm that ends the race.
    The **radar bonus** is answered: it is not a driving reward at all but a
    collision one -- knocking a roadside object over (`field_0x10 == 0`,
    `object_flyup_check_collision` with mass 10) sets its state to 18,
    increments a counter at 0x4dc964 and raises banner 51. (Session 13: the
    camera is `.omp` type 7, whose handler is 0x428d30; +0x10 is the prop's
    phase. The whole knock-over system is in discoveries.md "Knock-over
    props".) A separate
    **speed-camera** system was also found, with its own posted limits
    (50/70/100/120 in 0x48c480, from `txt_vbrd.cpp`) and **no tolerance**,
    independent of the cops' 60/80/120 with x1.1.
    The **fines are answered, negatively**: there is no fine system in this
    build. `lan_main.cpp` has one text getter (fcn.00431d30) and exactly two
    ways in -- a constant index, or the banner slot 0x4882c0/0x4882c4. The
    constant set is {17,24-28,32,34,36-39,42,43,52-55} and the banner set is
    {31,44-51}; **0 GEFLITST, 6/8/10 POLITIE and 1/7/9/11 BOETE are in neither**,
    nor are the fuel, breakdown and budget strings. menu.dat has no language
    subsystem at all, so it is not using them either. They are vestigial.
    The last two are answered as well: **0x4d9a7c is the game mode**, set by
    fcn.00424d20 through a 9-entry table at 0x424e00, and **mode 8 is the police
    chase itself** -- its three callbacks are 0x415300 (the `g_times_caught`
    handler) and 0x415420 (the gate), so `== 8` only ever meant "while the chase
    sequence runs". The third catch calls fcn.00424d20(**6**), the **arrest**
    sequence, whose callbacks span the only two fetches of language.ini 42
    GEARRESTEERD and 43 RACE OVER.
    **Session 10 read mode 8 end to end** (see "The police do most of the
    talking" in discoveries.md). The module is **`ev_cops.cpp`**, named by its
    own asserts at lines 241 and 379. The warning routine 0x415300 switches on
    `g_times_caught` **twice** -- once for the banner, once for a **voice line**
    drawn at random from one of three tables (0x486648 / 0x486660 / 0x486678,
    six and five and five entries) -- and the catch number turns out to be
    **the first digit of each sample's filename**, which the table layout
    proves rather than the name. Two further random pools play from the same
    module: thirteen `achter*` lines (0x486690) and five `opgev*` ones
    (0x4866c8), both gated on a shared channel at 0x4866DC. That accounts for
    **34 of the 87 samples -- about 39% of the game's audio is the police
    speaking.** The gate 0x415420 holds a warning for **4000 ms** off
    `GetTickCount`, and 0x415490 ends the sequence with
    `change_active_camera([0x4a70e0])` and a camera-parameter call, then
    `set_game_mode(0)` -- so the chase **saves and restores the camera**, which
    is why it is a mode at all. 0x4142b0 is `change_active_camera`, named by its
    own bounds message.
    **The mode enum is now closed (session 10)** -- see "The complete game-mode
    machine" in discoveries.md. `set_game_mode` jumps through a 9-entry table at
    0x424E00 and each arm registers a sequence with
    `0x416760(steps, begin, tick, end)`: **0** racing, **1** the start-camera
    fly-in (`ev_stcam.cpp`, by its own "Illegal mode start camera!" assert),
    **2** a bare mode, **3** LIKELY the victory sequence (it repositions a
    camera and plays sample 21 `juich.wav`), **4** teardown, **5** and **7**
    nothing at all -- both table entries point at the same bare `ret` -- **6**
    **game over** (`ev_g_ovr.cpp`, "No game over mode set!"), and **8** the
    police chase. So mode 6 is not the arrest as such: the arrest writes
    **0x4867A4 = 2** and calls `set_game_mode(6)`, and that sub-mode is what
    picks between language.ini 40 TOTAL LOSS, 41 OUT OF TIME, 42 GEARRESTEERD
    and 43 RACE OVER.
    **Session 12: `RESET_COP` and `cop_lane` are read**, and RESET_COP
    corrects the speed table above. It is **4 -> 60, 5 -> 80, 6 -> 120,
    7 illegal, 8 -> 2x the (LIKELY) player top speed**, not
    "2x/60/80/120". A cop enforces the last sign behind the car
    (BACKTRACK_SPEED_TRIGGER 0x408080), and cop_lane picks the least-blocked
    lane starting from the suspect's. See "RESET_COP,
    BACKTRACK_SPEED_TRIGGER and cop_lane" in discoveries.md.
    (Earlier note:) `RESET_COP` and
    `cop_lane` are located but unread -- they are *not* the two helpers the
    chase sequence calls at its end, which are `change_active_camera` and a
    camera-parameter setter. Note the debug trace is **not** the tool
    for this (question 25's suggestion): a full demo run logs over a million
    lines and no police event at all -- **because demo mode never drives the
    player's car**, not because a demo driver stays lawful (see question 34).
    Original note: How do police/AI/pursuit mechanics work (fines, arrest, radar
    bonus)? Which functions drive them (ai_cops.cpp, ev_cops.cpp, gfx_radr.cpp)?
    The data side is ready: the `.dmp` lane graph is decoded (3 lanes per
    carriageway, with adjacent-lane links for `change_lane`) and indexed per
    chunk by `.pmp`.

## Tooling

19. **RESOLVED**: `reverse-engineering/tools/rizin` (0.9.1) is vendored and
   works on spel.dat/menu.dat. The game also runs under Wine, so runtime tracing
   is available — see #20/#21 and `reverse-engineering/runtime-wine.md`.

## Runtime (Wine)

20. **RESOLVED (session 5). CD check bypassed.** `find_program_cdrom`
    (`winlib Filepath.cpp`) checks **`getenv("NOCD")`** before scanning drives;
    setting `NOCD=<letter>` skips the check entirely. File lookups already
    prefer the bare filename in the CWD. Both menu.dat and spel.dat now run.
21. **RESOLVED (session 5). Screenshots work.** Run Wine against an Xvfb root
    window of exactly the wanted mode size at depth 24 (`Xvfb :98 -screen 0
    640x480x24`) — Wine then offers the 8/16/32-bit variants of that size — and
    capture with ImageMagick `import -window root`. No modeswitch grab, no
    virtual desktop needed. Caveat: use `RESOLUTION=3` (16-bit); 8-bit modes
    select the D3D Ramp device, which Wine does not implement.

## Texturing

30. **RESOLVED (session 6).** Textures bind through the **anim system**, whose
    three tables are static data in spel.dat: `anim3d` at VA 0x492050 (312 x 9
    bytes: frame count, frame-index pointer, flag), `frame3d` at VA 0x48E5E8
    (386 x 16 bytes: two runtime handles, then a mesh index and a texture
    index), plus parallel name arrays. anim -> frames -> (mesh, texture).
    Verified on `E00_MUNT_ANIM`, `E00_OUDK_ANIM`, `A_LNTRN0_ANIM`,
    `MPOLIZEI_ANIM`, `WIEL_ANIM`. See research/discoveries.md for the full
    table and the evidence for the 9-byte stride.

31. **RESOLVED (session 7).** The four `.rmp` plane lists take their textures
    in the order **`weg, gras, gra2, vang`**, stated outright by `Raser.ini`'s
    `[MAPS]` table (args 2-5 per track, with the developers' own comment
    "Textures to load for map"). This is *not* the frame order of
    `E0x_ALGEMENE_TEXTURES_ANIM` (`gras, gra2, weg, vang`), so binding by anim
    frame index mis-assigns all four. `_gra2` is the building-facade atlas and
    `_vang` the guardrail/viaduct/treeline atlas; the port had those two
    swapped and drew facades along the guardrails. See discoveries.md.

32. **RESOLVED (session 9).** etappe6 uses **etappe0's anim set**. The selector
    at 0x4236fa reads the stage from 0x4d9a74, bounds-checks it against 6 and
    jumps through a 7-entry table at **0x4238e8**, whose entries are
    `0x42370f 0x42374d 0x42378b 0x4237c9 0x423807 0x42383f 0x42370f` -- the
    **last is identical to the first**, so playing etappe6 even logs "loading
    etappe 0 anims". That is why no `E06_ALGEMENE_TEXTURES_ANIM` exists.
    Each arm also branches on 0x4d9aac to pick an 8-bit or 16-bit loader, which
    matches txt_main.cpp's "Loading texture anims for 16-bit/8-bit mode".

## New questions (session 9)

44. **RESOLVED (session 14): it is the game's own gamma.** Game.ini
    `[GFX OPTIONS] gamma` ("0=light, 1 = dark", shipped 1) is read into
    0x4d9b14 (0x4220be) and passed to winlib's 0x468220, which sets 0x4eacc0.
    The colour conversion to the 16-bit format, 0x468230, then squares each
    channel, `c = c*c >> 8` (0x4682a4..), a gamma of exactly 2. The sprite
    loader (Sprite.cpp) and the PCX loader (Rcpcx.cpp) convert through it, so
    the 3D textures are darkened as well as the HUD sprites. See discoveries.md
    "Game.ini gamma squares every colour". **Correction (session 15):** the
    fonts are squared as well. The "fonts do not" below compared olfnt02 with
    a colour that is not its index 230, which is (189,202,231) in e02_pl16
    and squares to the measured blue. Earlier text:
    **PARTLY RESOLVED (session 9, runtime).** Capturing the game under Wine
    settles the practical half: the HUD **sprites** come out as a **gamma of
    about 2.0** on the palette (fixed at black and white; index 73 measured
    (255,73,0) against a predicted (255,74,2); index 81, not fitted to, measured
    (25,0,0) against a predicted (28,0,0)), while the **fonts do not** --
    olfnt02's blue reads (140,158,214) against (143,165,206) in the file. That
    puts glyphs and sprites on different paths and makes index 147's teal into a
    (3,43,43) that reads as the dark edge both references show. Also CONFIRMED
    from Raser.ini `[PALET]`: in 16-bit mode **every** track uses e02_pl16.pcx,
    so there is no per-track HUD palette to choose.
    **Answered by #44 (session 14): the game's own.** Was open: whether that gamma is the game's own or an artefact of
    Wine's 8-to-16-bit conversion. It was only ever observed under Wine. Testing
    it needs either a second renderer (the 3Dfx path, question 5) or reading the
    blit code in `winlib`'s sprite/pal units.
45. **RESOLVED (session 9), as a negative.** It is unused art. The
    `stuur_model` hypothesis is **disproved by experiment**: running the game
    under Wine with `Game.ini [GFX OPTIONS] stuur_model=1` ("Old (Autobahn
    Raser I)") gives a **byte-for-byte identical HUD** -- the same digital
    readouts and the same two bars, no dial anywhere -- and only adds a small
    numeric readout in the bottom-left corner. `stuur_model` selects the
    *steering* model, not a HUD. So sprite 1 and the 19 needle frames at 32..50
    are finished art that nothing draws, most plausibly inherited from the
    predecessor the option is named after.
46. **RESOLVED (session 15), the other way round: the orange ladder IS the
    turbo bar, and the gradient chevrons 11..21 are the unused set.** Sprite
    anim **21** (0x4961E8) lists the orange ladder in step order: 449, 433,
    439, 445, 446, 447, 448, 432, 450, 451 (0-based), then **13** (the
    gradient strip) as the 100% frame. It is HUD element 4's hires anim
    (anim 20 is the lores one). Wine captures of the original with the turbo
    draining match frames 432, 447 and 446 (mean error 3-4 against about 11
    for every other frame, gradient included) once the game's gamma (c*c/255)
    is applied, and the full bar at the start matches 13 (3.3). No anim lists
    11, 12 or 14..21. See discoveries.md "The TURBO bar is the orange ladder".
    Original text follows.
    **PARTLY RESOLVED (session 9).** Not a lores/hires pair -- those are
    separate: RC.IMG holds **32 bar sprites at 91x8 and another 32 at 49x3**, a
    matched hires/lores set. Within each resolution there are **three**
    11-or-10 frame ladders, and the HUD draws only two of them (the gradient
    chevrons for TURBO, the solid bar for SCHADE, both documented above). The
    third is a **plain-orange chevron ladder** -- at 91x8 that is
    432, 433, 439 and 445..451, with 449 empty.
    It was read as **LIKELY the fuel gauge**, on the grounds that the game
    carries an unused fuel layer -- the language.ini strings GETANKT, KOSTEN,
    GEEN BENZINE MEER and WEGENWACHT are never fetched (see "There is no fine
    system") -- while `TRIGGER_BENZINE_STATION` is value 2, *is* handled, and
    the tracks carry eight of them.
    **DOWNGRADED to SPECULATIVE (session 10).** That last leg has gone: the
    trigger-2 handler has now been read end to end (0x42a4d0) and what it does
    is **refill the turbo tank to 100, reset the damage, play `repair.wav` and
    raise banner 48 QUICK SERVICE**. It never touches a fuel quantity. So the
    "benzinestation" is a service stop, not a petrol stop, and nothing outside
    the unfetched strings now argues for a fuel system at all. The third bar
    ladder is still **unidentified**, and still nothing has been found that
    draws it.

## New questions (session 7)

40. **RESOLVED (session 9).** The lap is counted at **trigger 14**
    (`TRIGGER_FINISH_POSITION`), gated by **trigger 15**
    (`TRIGGER_FINISH_VALID`): 15's handler sets an armed flag on the racer, and
    14's handler increments the lap counter only when that flag is set, then
    compares it with Nrlaps. **Trigger 9 is not involved** -- its handler is
    chunk-index wrap bookkeeping (it sets a crossed flag and toggles three
    booleans), so the `TRIGGER_ZERO_CHUNK_MARKER` name was describing exactly
    what it does. The circuits place 14 and 15 one chunk apart, and the car
    starts one chunk before the line, so the first crossing is unarmed and the
    first fire is the genuine end of lap 1. The port now follows this instead of
    the distance window it had been using. See "How a lap is counted" in
    discoveries.md.
41. **RESOLVED (session 9).** Trigger 1 is the **speed camera**. Its chunks
    match the type-7 `.omp` objects (`A_FLTSRP`/`C_FLTSRP`, the *flitspaal*) one
    for one on all seven tracks -- 37 of each, same counts per stage, the
    trigger on the camera's chunk or one to two past it. That it is a one-shot,
    cleared by fcn.00428DE0 in the chunks around an object, is exactly what a
    camera that must flash once per pass needs. See "Trigger value 1 is the
    speed camera" in discoveries.md.
42. **RESOLVED (session 9).** They are not regions: they are **traffic-sign
    triggers on the highway stages**. `tr_vbord.cpp` (*verkeersbord*) holds 18
    handlers at `0x42a640 + n*0x70`, pointers in a table at 0x48C4E0, and
    **every one is gated to etappe 1, 3 or 5**, otherwise raising
    `Trigger is only used for highway tracks!`. The data agrees exactly:
    22-25 and 31-34 appear on etappe 1, 3 and 5 and nowhere else, and 22..39 is
    exactly 18 values for 18 handlers. The two groups that duplicate each other
    chunk for chunk are the two gantries, one per carriageway. See "Triggers
    22-25 and 31-34 are traffic signs" in discoveries.md.
    **RESOLVED (session 10): the trigger value is the sign face.** The signs are
    not placed -- they are **blitted into a texture**. 0x42ae90 is
    `blit_anim_frame_to_texture(slot, frame)` from `txt_main.cpp`, named by its
    own two asserts ("Index in array incorrect" for `0 <= slot < 16`, and
    "Incorrect animation frame while trying to blit to texture! Texture index:
    %d, frame: %d" for the frame). `slot` indexes a **16-record, 88-byte table
    at VA 0x48C968** holding {lores texture, hires texture, blit x, blit y,
    source PCX filename}. Each of the 18 handlers switches on the etappe global
    0x4d9a74 and calls it with the **slot from the etappe** -- 8/9/10 for
    etappe 1/3/5 in the first group of nine, 11/12/13 in the second -- and the
    **frame from its own index, 0-8**. So **trigger 22+k blits frame k** into
    the etappe's texture at (9, 122) and **trigger 31+k blits frame k** at
    (9, 7); the data uses only 22-25 and 31-34, i.e. four sign faces per
    gantry. Both groups blit into **a_TRFFC1.PCX**, so the gantry geometry is
    static and only its texture changes.
    Picked up along the way: **0x4d9a70 is the lores flag**, set by
    `set_game_display` (gm_init.cpp:568) from a jump table at 0x4243D4 --
    RESOLUTION 0 and 1 are the 320x200 modes, 2, 3 and 5 the 640x480 ones, and
    **4 is not a valid value**, per the binary's own "GAME supports only
    320x200 OR 640x480". See discoveries.md for the full table.
43. **PARTLY RESOLVED (session 9).** **f2 is the path *type*.** `pth_main.cpp`'s
    overflow message is
    `Not enough memory allocated for path points! Path nr: %d Path type: %d,
    Nr path points: %d`, and the "Path type" argument is read from
    **`[edi+0x04]`** of the runtime record. Tracing the loader's own field reads
    -- it freads 4 bytes at a time starting at `[edi-0x04]` -- puts that at
    record offset +8, i.e. the **third** field, which is exactly the port's
    `f2`. Its range of **3..7** is therefore five path types. That `f4 /
    arc_length` clusters by `f2` now has an explanation: different kinds of
    path have different length and pacing characteristics.
    Also CONFIRMED: a path holds at most **50 points** (`cmp dword [ebx], 0x32`
    guards the message above), and the loader rejects a bad header with
    `Incorrect version path file. version: %d track: %d` at pth_main.cpp:102.
    **f4 RESOLVED (session 10): it is a time, not a distance.** `pth_main.cpp`
    raises **"Time not set for path %d"** when the field is absent, so the round
    5000..50000 are **5 to 50 seconds in milliseconds**. The distance reading
    (500..5000 m at 10 units per metre) is withdrawn -- the numbers fit either
    way, which is what made it plausible; the message is what decides it.
    **The types are also partly named.** The suspicion about `pth_heli.cpp` was
    right and goes further: there are **eight** types, not five
    (`pth_main.cpp:440` bounds it `0 <= type < 8`), each with a three-function
    entry in a table at **0x4966F4**, and three are identified from the meshes
    they load -- **4 the helicopter** (`pth_heli.cpp`, C_HELI_ANIM /
    C_LBLADE_ANIM / C_SBLADE_ANIM, asserting "Helicopter paths only available in
    city tracks!"), **6 the aeroplane** (A_VLIEGT_ANIM) and **1 a police car**
    (MPOLIZEI_ANIM). The shipped tracks use only 3, 4, 5, 6 and 7, split exactly
    by stage kind: 3/4/5 on the city stages and 6/7 on the highway ones, which
    independently confirms the helicopter's city-only assert. Types **0, 1 and 2
    are authored nowhere**.
    **Session 12: f3 is the lap; the type table was one dword off.** Corrected
    identities: 2 police car, 3 a canal tour boat (STRONGLY SUPPORTED, seen in the port) (`E00_ROND`), 4 train,
    5 helicopter, 6 random traffic vehicle, 7 aeroplane. Only **type 1** stays
    unnamed (and it is authored nowhere); type 0 has no start function.
    **Answered (session 13): type 1 is a camera flight** (table 0x4966f0,
    entries {start, update, end}: 0x432a10 / 0x432a80 / 0x432a70).
    - The start function calls change_active_camera(6) (0x4142b0, arm
      0x41444e).
    - The update moves camera 6 along the path (0x4474f0 on the camera array
      0x4e66a0, 352 bytes a camera) and aims it with 0x4477f0 at the path
      record's +0x10 object, or at a fixed point when that is −1.
    - The end function switches back to camera 0. LIKELY a dev or attract
      flyby; no shipped track has one.

## New questions (session 6, third pass)

*(Note: the original wording of 36, 37 and 38 was lost to a bad edit of this
file in session 9 and is reconstructed below from the entries' opening lines and
from research/discoveries.md, which was not affected. The substance stands; the
phrasing may not be the original's.)*

36. **RESOLVED (session 9).** **Difficulty picks it.** `init_ai_states`
    (fcn.00405bc0) writes ladder **A** (0.88 0.90 0.92 0.94 0.96 0.98) into its
    local array, then at 0x405c00 tests the global **0x4d9ab0** and, only when
    it is **zero**, overwrites the whole thing with ladder **B**
    (0.74 0.78 0.83 0.87 0.91 0.96). And 0x4d9ab0 is set at 0x421e32 straight
    from `GetPrivateProfileInt("OPTIONS", "difficulty", GAME.INI)`.
    So **EASY (0) gives the slower ladder B and HARD gives the faster ladder
    A** -- on HARD the opponents run nearer the full ladder speed. That matches
    the same key already known to pick the 6-or-12 per-impact damage maximum.
    **Session 10 adds a second, separate knob.** There is also a **"difficulty
    modifier"** at **0x4d9b1c**, named by the binary's own error text (0x484aec,
    "Difficulty modifier has invalid value", printed at 0x41097e when it exceeds
    9). It indexes a 10-entry table built on the stack at 0x4108dd --
    0.60 0.70 0.80 0.90 1.00 1.10 1.20 1.30 1.40 1.50 -- and that factor scales
    six per-gear floats at `0x4a5f28 + k*124` when the gearbox upgrade is owned
    or five at `0x4a5f10 + k*124` when it is not, for each of three 96-byte
    entries from 0x4a55d0 to 0x4a56f0. Each entry also gets
    `clamp(car_slot + drivetrain_upgrades - [0x4a55d0], 0, 3) * 4.0` at +0x2c,
    so the opponents are scaled by the player's progress as well -- rubber-
    banding by purchase. **Answered (session 13):**
    - 0x4d9b1c is Game.ini `[OPTIONS] dif_mod` (0x421f7a → 0x421f82). The
      shipped value 4 gives a factor of 1.00, so the scaling does nothing in
      the shipped game.
    - The three 96-byte entries are the racers: the car-assignment loop at
      0x4112b6 writes each racer's car index to their +0, and the AI driver
      code reads them.
    - The +0x2c "progress" value is written and never read (no access to
      0x4a55fc in a full sweep), so there is no rubber-banding by
      purchase. STRONGLY SUPPORTED, since a linear sweep can miss code.
37. **RESOLVED (session 9).** From the **same `.pmp` speed triggers** that set
    the police limits, through a **different dispatcher**. At 0x408901 the game
    does `value - 4`, bounds-checks against 11 and jumps through a 12-entry
    table at **0x408a1c**; four of the arms are one-line setters writing a fixed
    float into the per-object array at **0x4a55cc** (96-byte stride), which is
    the speed HANDLE_TRAFFIC_AI then reports as "Max speed allowed":

    | trigger | traffic speed |
    |---|---|
    | 4 | 12.5 m/s (45 km/h) |
    | 5 | 17.5 m/s (63 km/h) |
    | 6 | 25.0 m/s (90 km/h) |
    | 8 | 30.0 m/s (108 km/h) |

    This is why the earlier trace showed traffic at **25.0** -- trigger 6, exactly.
    Note the two dispatchers **disagree about which values are live**: ai_cops
    treats 8 as `ILLEGAL SPEED TRIGGER` and uses 7 for 120 km/h, while this one
    skips 7 and uses 8. In the shipped tracks only 4, 5 and 6 ever appear, so
    neither disagreement is reachable. Values 14 and 15 take other arms here,
    so the table is a general per-object trigger handler rather than a
    speed-only one.
38. **RESOLVED (session 6).** The lane and speed triggers are the `.pmp`
    per-chunk trigger slots at chunk+0xEC / +0xF0. Session 9 added what their
    values mean: speed triggers 4/5/6/7 set the limit a cop enforces to
    2x-default / 60 / 80 / 120 km/h, and lane triggers 18..21 set max lanes
    1..4. See "The police" in discoveries.md.

## New questions (session 6, continued)


33. **RESOLVED (session 10) -- all 56 fields identified.** The 16 x 238-byte table at 0x484B18 is a
    **full vehicle physics model in real-world units** -- mass in kg, length and
    width in metres, gravity 9.81, air density 1.2, drag coefficient, power,
    redline and peak-torque rpm, six gear ratios plus final drive, and an
    alternate (upgraded) gear set. See discoveries.md for the field map and the
    evidence (a 600 kg Trabant against a 15 t bus with Cd 0.50 and a 2200 rpm
    redline). **Also identified (session 8):** field 5 is height in metres, 8 is
    centre-of-gravity height, 10 the 0-100 km/h time, 21 the rolling-resistance
    coefficient, and **17/20 are front and rear cornering grip** (they order
    exactly by car performance).
    **Second pass (session 9)** corrected two of those readings and settled more
    (see "The 0x484B18 table, second pass" in discoveries.md):
    **5 and 6 are the CoG-to-front and CoG-to-rear distances**, summing to the
    wheelbase (the game adds them; they differ for traffic; Trabant 2.02 against
    a real 2.02) -- **not** the vehicle height; **9 is the frontal area** (1.80
    car, 9.00 bus); **22 is the wheel radius**, proved by the driveline formula
    `gear * final * (60/2pi) / f22` -- **not** Cd; **8 is LIKELY Cd** (0.65 for
    the bus); the **gear array is 1-based from field 27**; and 0, 14, 26, 27 and
    39 are constant 0 while 37 and 49 are constant 1, with 11 duplicating 10.
    Also settled: **35 and 36 are the gearbox's shift points** -- fcn.0040e190
    is the gear selector, clamping engine rpm to an 800 rpm idle and comparing
    it against a downshift threshold of `(f36 - 0.05) * rpm_torque` and an
    upshift threshold built from f35 and the redline, which is why the upgraded
    box carries its own pair in 47/48.
    And **15-20 are two Pacejka tyre triples**, front and rear: the code runs
    `fpatan` then `fsin` then multiplies by the grip field, so 15/18 are the
    **B** stiffness factors, 16/19 the **C** shape factors and 17/20 the **D**
    peak factors already known as grip -- with values in the textbook ranges
    (B 8.43-16.00, C 1.11 and 1.70). **54** is a blanket grip multiplier in the
    same cluster, 1.20 for racers and 1.00 for traffic.
    **7 is LIKELY the CoG height**, loaded with gravity, mass and both axle
    distances -- the exact set an axle-load calculation needs -- and 0.30 for
    the low-slung Porsche against 0.40 for everything else.
    **Third pass (session 10)** named four of the last five (see "The 0x484B18
    table, third pass" in discoveries.md): **38 is the driven-axle split** --
    the drive function 0x40da90 stores `f38 * F` to one axle and `(1 - f38) * F`
    to the other, and f38 is 1.00 for exactly the Astra and the Golf, the
    fleet's two front-wheel-drive cars, so 1 = FWD and 0 = RWD (**50** is its
    counterpart in the upgraded block); **51 is the turbo's drive-force
    multiplier**, 1.0 normally and forced to **6.0** by the turbo tick 0x425cf0
    while the 100-unit tank at 0x4d9e3c drains; **`f52 * 0.8` is the
    longitudinal traction cap** as a multiple of axle load (1.20 g for
    everything, **3.60 g for the Diablo**); and **53 is the brake coefficient**,
    `-load * f53 * brake`, ordering 0.75 Trabant to 2.25 Diablo. **54** is now
    confirmed to multiply **both** axles' Pacejka output.
    **FULLY RESOLVED (session 10): field 55 is the engine sound sample index**,
    and with it all 56 fields are identified. It is an **integer**, not a float
    (read as one at 0x410c41), with values 31-36. The SNDFX player 0x4286a0
    indexes a table of **87 nineteen-byte filenames at VA 0x48BAF0** (bounds
    0-86, "SNDFX sample out of bounds" otherwise), and those six indices are
    `auto1.wav`, `auto2.wav`, `auto3.wav`, **`best_bus.wav`**, **`bus.wav`** and
    **`vracht.wav`** -- the van, the bus and the lorry each getting the sample
    named after them while the twelve cars share three generic engine notes.
    The same table independently confirms field 51: sample **84 is
    `turbo.wav`**, which is the sample the field-51 tick plays. The full table
    is in discoveries.md.
    **Also settled:** the applier 0x428400 writes **field 51 := 1.0
    unconditionally**, so the Diablo's table value of 2.00 is dead; and the
    mirrored block is **27-36 <-> 39-48**, proved pair by pair in the copier at
    0x4284df (which emits `f46 -> f34` twice).
    **Established negative:** the table carries **no torque curve** -- only
    power, the peak-torque rpm and the redline -- so no model built from it
    alone can satisfy both the rated top speed and the rated 0-100, and a curve
    must be assumed either way.
    **Open hypothesis:** this may be the model `stuur_model` selects when set to
    the Autobahn Raser I setting, with CARS.DAT being the other -- untraced,
    though session 9 showed `stuur_model=1` leaves the HUD identical and only
    adds a numeric readout.

34. **RESOLVED (session 13): the body's roll rate.** A[29] is **not a handling value at all**:
    its one use, in the yaw update `fcn.0040f3c0`, is

        d3d_objects[obj.field_08].f_7c -= dt * A[29] * a

    where the array is the T_ARRAY its own constructor names **`_d3d_objects`**
    with element type **`c_3dobject`** -- so it acts on the car's **renderable
    model**, not on its physics object. See "CARS.DAT A[29] is a visual rate"
    in discoveries.md for the full reading, including the 20.0 threshold on `a`
    that also gates the ordinary speed drag.
    That explains why it is the one car field the TWEAK_MODE menu does not
    expose, and why two runtime experiments under Wine found nothing: patching
    it between 0.0 and 40.0 changed neither a coast-down nor straight-line
    acceleration.
    **LIKELY** a per-car visual body-rotation rate, from the authored values --
    Trabant 1.6, Astra 0.9, four cars at 1.0, Diablo 0.1. **Answered (session
    13):** `c_3dobject+0x7c` is the body **roll**. The roll spring 0x40ebd0
    integrates the same field (port drive.c `roll`, checked against the
    original under Unicorn by diff_drive.py), so A[29] is the rate at which
    the body rolls into a turn: roll ± dt · A[29] · steer (0x40f90d). A[26] is
    still always zero and unused.
35. **RESOLVED (session 8), as a retraction.** `[obj+0x28]` and `[obj+0x174]`
    are **not** collision physics. Both are written through C++ setters
    (fcn.00435B80, fcn.00436070) that flag a dirty mask at `+0x170`; the objects
    come from a pointer table at 0x4E47C8. With the sibling three-component
    setter fcn.00435D80 — given `(0, 0, ±1.0)` on impact, sign from a compare
    against 0.0 — they have the shape of **positional sound** parameters
    (frequency, volume, left/right pan), which matches Game.ini's `3dsound`.
    The earlier reading of the `fchs` at 0x427EB7 as a bump-back impulse is
    withdrawn: **no collision impulse has been found**. See discoveries.md.

## New questions (session 5, continued)

26. **RESOLVED (session 9).** The game names **all four** in its own overflow
    messages, which sit consecutively in spel.dat:
    `Too many road-planes`, `land-planes`, **`extra-planes`**, **`rail-planes`**.
    So lists 2 and 3 are **extra** and **rail**, and the four line up with the
    Raser.ini `[MAPS]` texture suffixes **weg / gras / gra2 / vang** that the
    port already loads them with -- rail being the *vangrail*, the guardrail.
    The earlier entry saw only the first two messages.
27. **PARTLY RESOLVED (session 6):** `.pmp` f3/f4 are the per-chunk trigger
    slots (see #38). **CONFIRMED from the engine side (session 10):** the
    dispatcher at 0x442607 reads exactly two trigger words per chunk, at
    **+0xEC and +0xF0** of the 388-byte runtime chunk record, and calls each
    one's handler with the car index. It also settles the stray data values --
    **86, 94, 107, 130, 150 and 196 are inert**, because the engine's 257-slot
    handler array at 0x4E5928 is zeroed at startup, the game registers handlers
    only for 0..48, and the dispatcher skips a null slot. Editor leftovers, not
    a missing feature. **Answered (session 13):** `.dmp` field3 is the height
    (session 7) and field1 is a per-waypoint trigger, tested by
    check_racer_trigger after the chunk's two slots (see file-formats.md).  Original note:
    What are `.pmp` f3/f4 (0..45) and `.dmp` field1/field3? `.dmp` field3 is
    constant within a chunk's waypoint group, so it may be a chunk back-pointer
    or a distance-along-track key.
28. **RESOLVED (session 9).** The engine does **not** use it. `rd_chunk.cpp`
    reads each chunk's list count from the file and checks it against a
    **compile-time maximum** -- 16 road-planes (line 355), 24 land, 12 extra,
    12 rail -- logging `Too many <kind>-planes (%d) of max (%d)` and reading
    into a fixed stack buffer. Nothing is sized from the header hint, so it is
    a stale exporter field; whether the two tracks where it happens to be right
    were re-exported cannot be told from the data, and does not matter.
29. **RESOLVED (session 9).** It is not the `.dmp` loader renaming its own
    file: fcn.00448ba0 overwrites the last three characters of the name with
    `D`,`M`,`G` and then tries to open **`etappeN.DMG` as a separate, optional
    companion**. If the open fails it returns 0 immediately and silently --
    no assert, no log. Since no `.dmg` ships, the path is simply never taken.
    A development-only format, dead at runtime in the retail build.

## New questions (session 5)

22. **RESOLVED (session 5).** `.d3d` vertex = `float x,y,z,nx,ny,nz,tu,tv; u8 pad`
    (33 B); triangle = `u16 a,b,c; u8 pad[3]` (9 B). The padding is dead — the
    loader reads it and discards it. Reader + OBJ exporter in
    `port/a2tools/d3d.py`. See file-formats.md.
23. **RESOLVED (session 9).** Neither a stale entry nor a mis-padded record in
    the index's eyes: the index is **self-consistent**. The entry gives
    off=11186209 size=4826, and the first **24 bytes there are all zero**; the
    real header starts at +24 with 118 vertices and 100 triangles, whose record
    is 4802 bytes -- and **24 + 4802 = 4826 exactly**. So the record carries a
    24-byte zero prefix that the size accounts for.
    The consequence is that any loader reading the header at the stated offset
    sees **0 vertices and 0 triangles and builds an empty mesh** -- the game
    included, and the port, which reports it among its 448 parsed without
    complaint. E04_koor is scenery on etappe4, so an invisible one goes
    unnoticed. A build-tool slip, not an index bug.
24. **RESOLVED (session 9).** Each binary has one of each, and the split is by
    how the art is addressed: the **named** archives are looked up by file name
    (`olfnt01.fnt`, `e02_pl16.pcx`, `WIEL.d3d`) and the **binary** ones purely
    by number -- the HUD reads RC.IMG sprites 425 and 426 and the 11..21 and
    123..133 ladders, and nothing ever names them. A name table would be dead
    weight for art the code indexes numerically.
25. **RESOLVED (session 9), as a negative on the method.** The suggestion was
    tried and does not work: a full run with DEBUGON=1 logs **1,045,857 lines**
    of waypoint and lane internals and **not one police event**, because
    **demo mode never drives the player's car** (see question 34). Question 18
    was answered instead by reading ai_cops.cpp and ev_cops.cpp and confirming
    with *targeted* runtime capture -- screenshots plus `xdotool` input, which
    is the runtime technique that did pay off (see "Driving the original under
    Wine"). The trace remains useful for waypoint and lane work, which is what
    it actually contains.

## New questions (session 12)

47. ~~**Is the port's view mirrored relative to the original's?**~~
    **ANSWERED (session 12): yes, it was.** A Wine capture of the etappe1 start
    shows the START/FINISH banner reading correctly with the other road on the
    right. The port drew it reversed with the road on the left. The fix is a
    projection mirror and CW front faces, and the steering-key swap is gone. See
    "The port's view was mirrored" in discoveries.md.

48. **Answered (session 16):** Wine draws it correctly. The viewport
    background is only a white clear; the sky is Gfx_bkg.cpp's own pair of
    screen quads, a 0.15 x 1/3 window of back<N>.pcx that turns with the
    player's heading under a tint that changes round the lap. See
    discoveries.md "The accelerated sky, drawn by Gfx_bkg.cpp". Original
    question: **What does the Direct3D viewport background actually look like?** The
    accelerated game sets a random `back1..7.pcx` as the viewport background
    (0x42a210). Under Wine it renders as a blue-to-pink gradient that is none of
    those images. A capture on real Windows / DX5-era hardware, or a
    Wine ddraw source check, would show whether the texture is stretched,
    tiled or ignored.

49. **Answered (sessions 12-14; marked in session 15): how menu.dat turns a
    result into the next screen.** Every part below is settled:
    - **Stage advance:** menu main (0x409b87) reads `finished`. If it is set,
      it loads the results (0x402390) and 0x409870 advances the player record's
      etappe by one, whatever the position. When the etappe was already 6 it
      sets 0x43e70c, **the career-completed flag** (session 14, CONFIRMED by its
      readers): 0x4076a6 (the menu loop's start) then shows the standings
      (0x405630 -> mhigh_open_screen 0x405530) and plays the ending
      (0x407580, win/lose and etappe 0); 0x40702c swaps the main screen's
      definition ([0x42ebdd] = 0x42fa08 instead of 0x42ce34, unless [0x43e7d4]
      is set), which is where the "Restart" label comes from (0x40703a). The
      port's a2menu now shows the standings before the ending.
    - **Standings:** 0x40aeb0 builds the standings.
    - **Videos (answered, session 12):** win / lose play after the last
      stage, chosen by whether the player leads the TOTAAL standings. Intro
      plays on `\FIRST`, exit on quitting. `save_game` is the active
      SLOTS.RAS slot and `start_budget` a fresh slot's money. See "When the
      Smacker videos play" and "SLOTS.RAS" in discoveries.md.
    - **Answered (session 12, menu rebuild):** TOTAAL is the sum of each
      driver's stage times by name, inserted in order (0x40aeb0); running out
      of money is not checked anywhere in menu.dat (no code compares the
      budget except to price purchases); the garage purchase logic is in
      reverse-engineering/menu.md. "Kwalificatie" / "Stedenrace" are the
      track types (Raser.ini [MAPS] arg 0).
    - **Answered (session 13):** the focus a screen opens with is the
      button it had when it was left. Control states are stored in the
      form record (0x410ec0; draw_button 0x41b41c), see
      reverse-engineering/menu.md "Forms".
    - **Answered (session 13):** the slider stripes are one stretched
      Blt, not scrolling (draw_slider 0x40b880, reverse-engineering/menu.md
      "Sliders").
    - **Answered (session 13):** slot fields +0x04 (play order), +0x1a
      (written 0 only) and +0x57 (standings tab), see
      reverse-engineering/menu.md.
    - **Answered (session 13):** the showroom's text timing: changing
      candidate first runs a blocking car slide (0x40be30, ~0.7 s), and
      only then are the new texts set and animated; see
      reverse-engineering/menu.md "Showroom".


## New questions (session 13)

50. **Answered (session 13): does the original's traffic flip lanes as often as the port's now
    does?** The port follows ai_lane.cpp's traffic arm (see discoveries.md
    "Traffic lane choice and following") and makes about 2100 lane changes
    per motorway race, against 84 with its old periodic rule.
    - A logged run shows cars ~100 m apart flipping between "faster" (1) and
      "not faster" (2) by fractions of a m/s, and the choice follows the flip.
      All traffic prefers 25 m/s, so only the corner term separates speeds.
    - In the original the chooser also runs every tick (change_lane from
      HANDLE_TRAFFIC_AI), with the same strict `>` test (0x404720) and no
      damping, so it may flip as well. One limit: the current lane is the
      lane of the target waypoint (0x403837), and a switch only succeeds
      when the next waypoint has a neighbour link (0x406d90 / 0x406d10).
    - debug.txt (DEBUGON=1) does not carry the ai_lane traces. Its per-frame
      waypoint lookups cover the police placements and three run-time
      objects (393-395, LIKELY the racers), never traffic.
    - A Wine capture with only the gas held did not get close enough to
      traffic to judge.
    - Next test: drive under Wine (with steering) behind a traffic car for
      a minute and watch whether the cars ahead weave.
    - **Answered (session 13), yes.** Counting breakpoints in the running
      original (winedbg --gdb, tools/gdb_count.py, 60 s of etappe1 with the
      gas held), change_lane ran 9630 times, all through the traffic chooser
      (0x403afb). The lane decision changed 666 times (0x403c40), with 665
      successful switches (321 left 0x403c8c, 344 right 0x403ce1). That is
      **6.9 % of traffic ticks**, against about 1 % in the port. So the
      original's traffic weaves at least as much as the port's now does. The
      per-second rate (LIKELY ~0.5 a car a second against the port's ~0.3)
      depends on the frame rate under the debugger, which was not
      measured.

51. **Answered (session 13): the police numbers the port had invented**
    (the arrest range, where a cop joins, the quiet time, the chase speed).
    The cops are the placed type-9 cars and the catch is a block one chunk
    ahead in the player's lane. See discoveries.md "The police are the placed
    cars". Still open:
    - (answered, below) what a catch does to the player's car;
    - (done in the port) cop_lane while a cop drives, the warning's
      camera (0x4151f0), and the side check;
    - (done in the port) pushing the parked type-10 cars and the arrows.

52. **Answered (session 13): HANDLE_RACER_AI never runs in the shipped game** (ai_model 0; see discoveries.md "Two AI models"). Original question: **Why does a racer not stop on the trigger-2 chunk?** check_racer_trigger's
    BENZINE_STATION arm (0x40b1fe) sets the racer's speed and maximum to 0,
    but a Wine run shows the racers crossing etappe1's chunk 549 at full
    speed. Something in HANDLE_RACER_AI skips the check there (the chunk-entry
    test at 0x40ad45, or the collision gate at 0x40ad1e), or the speed is
    restored before it shows. Not traced.
    Session 13 follow-up: 0x442020 (the chunk update that copies +0x18 to
    +0x1c) runs once per object per frame (0x441147 / 0x4412d2), so the
    chunk-entry test should see the change. The remaining candidate is the
    AI record's +0x2c flag, which skips the whole check at 0x40ad1e when set.

53. **RESOLVED (session 14): the contact geometry.** The record builder's
    arms are in discoveries.md too. Earlier text: **MOSTLY RESOLVED (session
    14): the box-box contact.** See discoveries.md
    "The contact geometry, for boxes"; ported as src/contact.c and checked
    against the original under Unicorn (1499 of 1500). The barriers are
    the same boxes (0x449c10), and the port moves the player in the
    original's 8 contact-limited steps (session 14); see discoveries.md.
    Simplified: the impulse once per frame, and 0x441490's re-test. **Answered (session 16):** the arms of the record builder 0x449840, and
    which call passes which box (discoveries.md "Which box is static, and
    what the slip does"). The arm with the point in the wrong frame is a
    rail-slab corner hitting the car's side; it reaches 0x449570 and, at a
    slab's far end, kicks the yaw rate to the ±6 clamp. The port does not
    reproduce it.
    Earlier notes:
    0x441490 does **not** bisect the frame. It reads the contact record
    0x4e6760 (normal +0x1c, point +0x10), returns 0 when the object
    already separates (v . n > 0), otherwise nudges its velocity along n and
    re-runs the swept test 0x44a0f0. 0x44a520 is a **swept polygon test
    returning the time of impact as a fraction 0..1** (1 = no hit, asserted
    within 0..1 at line 0x167). It builds both shapes as polygons
    (0x44ae80, 0x44aef0), moves one into the other's frame (0x44b070, the
    position difference; 0x44aff0, by -heading), checks bounds (0x44ab40)
    and intersects them over the move (0x44b6f0). On a hit it writes the struck
    edge's 12-byte entry to 0x4e667c.. with 0x4e6678 = 1 and a value to
    0x4e6698. Next: run 0x44a520 under symx87/Unicorn on two known
    rectangles and fit what it returns. The port uses its own
    push-out normal (the edge normal for barriers, centre to body axis for
    traffic, the rectangle side for scenery) and the car box's deepest
    corner as the contact.

54. **Mostly answered (session 13): the new AI model (ai_model 0).** See
    discoveries.md "The new AI model, traced": the record layout, the two
    transition tables, every state's handler, the chase start (0x409060 /
    0x4034a0), ScanPath and the obstacle lists, the vehicle model (the
    player's car!) and the new rubber band. 0x403340 runs the transitions,
    0x408c00 counts the player's position and 0x408cb0 ranks the racers.
    State 16, the player in the obstacle lists and the trigger-4 threshold
    (12-13 m/s; no margin over the limit) are confirmed at runtime; see
    discoveries.md "Runtime check: the speeding threshold, obstacle lists,
    state 16". The racer cut-in (state 2) is confirmed at runtime too
    (session 14, "Runtime check: the racer cut-in"). The "swerve out of the
    lane" with the player straight behind is confirmed in session 15: the
    racer's own obstacle-list entry blocks the first waypoint of ScanPath
    (discoveries.md "Runtime check: the swerve with the player straight
    behind"). **Answered.**
    The chase and catch of the player and of a racer are confirmed at runtime
    (discoveries.md, "Runtime check: chases at etappe1 chunk 440").

## New questions (session 15)

55. **Answered (session 15): why does the original crash on etappe2 under Wine?** With
    .work/menutest Game.ini `[ETAPPE] etappe=2` and spel.dat started on its
    own (RESOLUTION=3, Up held), it ran about 9 s and then stopped with
    "Unhandled exception!". debug.txt shows the exception straight after an
    AI waypoint search (object 127, chunk 281), then
    `(load_racer_info) Gm_ini.cpp(337) : Invalid nmb of racers 0 (3)` during
    the teardown. The HUD was captured before it.
    **Answered (session 15):** a bug in the original. 0x40b650 (is a waypoint
    free for a recycled AI car) reads the obstacle list of the waypoint's
    previous and next links before testing them for -1. On a lane start that
    reads 0x2c bytes before the waypoint array, which under Wine is an
    unmapped page (fault at 0x40b6d7, etappe2 waypoint #291). The racer-count
    message is teardown noise after it. The Wine test copy is now patched
    (patch_wp_guard.py); see discoveries.md "The waypoint -1 read in
    0x40b650". Still open: what the read returned on Windows 9x (hypotheses).

56. **Answered (session 15): `dif_mod` is dead** -- see discoveries.md "What
    Difficulty changes". Original question: **Does `dif_mod` reach the racers' driving?** 0x4108dd..0x4109de scale
    CARS.DAT max-speed rows by 0.6..1.5 (dif_mod 0..9; 4 = x1.00, the shipped
    value) for the three entries' car types (session 13). The port follows
    0x40eb5b, where the AI drives with the **player's** car row. If both are
    right the scaling never touches a row the AI reads, and dif_mod is dead.
    A distinguishing experiment: dif_mod=0 against 9 under Wine, timing the
    racers on etappe1. The port ignores the setting for now (no change at 4).
57. **Answered (session 16): the clock keeps running, but nobody sees it.**
    Timer 3 is paused only at the finish (0x4283e0) and by the P / quit
    loop (0x4248d0). Mode 6 sets [0x4d9aa0], so the HUD (0x41a8b7) draws
    no panels at all during the game over, and the same goes for modes 3
    and 8. See discoveries.md "The game timers, and the HUD in modes 3, 6
    and 8". Original question: **Does the race clock stop in the game
    over?** The port's RACE TIJD keeps counting through mode 6.

58. **Answered (session 16, runtime): yes.** Under Wine the first warning's
    49 stayed pending for the whole 4 s of mode 8 and was latched the frame
    after mode 0 returned; the captures show it blinking only then. See
    discoveries.md "The game timers, and the HUD in modes 3, 6 and 8".
    Original question: **Does the police warning's banner (49 / 50) appear
    only after the 4 s warning camera?**

59. **Are the 3D textures run through the game's gamma?** #44 says the sprite
    and PCX loaders convert through 0x468230's c*c >> 8, "so the 3D textures
    are darkened as well". The session-16 sky fit says back.pcx, loaded as a
    texture by 0x45a460, is **not** (3.6/255 linear against 31+ squared). Either
    the D3D texture loader skips the gamma or back.pcx takes another path; a
    fit of a road or building texture in a Wine capture would tell.

## New questions (session 17)

60. **What do 0x40ede0's other callers (0x4011e7, 0x42605b) set to the
    plane?** The traffic call stores its return value as y (discoveries.md
    "Traffic height, and the position-to-chunk walk"). 0x42605b sits next
    to the handle_basic_ai call at 0x426004, so it is LIKELY the player's car
    when the game drives it. Not followed. The port's player, racers and
    cops still use the global nearest-chunk lookup with a one-chunk step back
    rather than 0x445a10's walk. On the waypoints both give the same height
    (chunk_lookup.py); positions off the waypoints were not checked.

# Hypotheses

Unproven but plausible claims, with the evidence that suggests them. Confidence levels per AGENTS.md.

## Launch flow

- STRONGLY SUPPORTED: `qrace.exe` is the session orchestrator: it reads `game.ini`, spawns `<name>.dat` (menu.dat or spel.dat), and on exit reads game.ini again to decide what to launch next. Evidence: qrace imports CreateProcessA + GetPrivateProfileIntA, contains strings "game.ini", ".dat", "Failed to launch A2Racer 2"; menu.dat/spel.dat have no CreateProcess; game.ini comments describe state handoff between menu and game ("saved by menu"/"saved by game to tell the menu whether the player was finished").
- SETTLED (session 12, see discoveries "When the Smacker videos play"): win.smk / lose.smk are played by menu.dat, but not after every race. They play only at the end of a career (after the last stage), chosen by whether the player's name leads the TOTAAL standings. The earlier guess ("checks finished/position after each race") was wrong.
- LIKELY: a2_II.exe (1998-03-20, older than the rest) is an early/alternate launcher that starts spel.dat directly; may be leftover from development or used for quick-play.

## Rendering

- **RESOLVED (CONFIRMED, session 5) — and the session-3 conclusion was WRONG:**
  the game uses **real Direct3D 2 immediate mode** (DirectX 5), obtained via
  `IDirectDraw2::QueryInterface(IID_IDirect3D2)` at 0x44C760. The earlier
  "pure software 3D" verdict rested on "only one LoadLibraryA, loading
  user32.dll" — but D3D 1-5 needs no `LoadLibraryA`, because `ddraw.dll` loads
  `d3dim.dll` itself. Confirmed statically (embedded `IID_IDirect3D2`,
  `IID_IDirect3DHALDevice`, `IID_IDirect3DRGBDevice`, `IID_IDirect3DRampDevice`)
  and at runtime (the game enumerates and creates a D3D HAL device and calls
  `IDirect3DDevice2::DrawIndexedPrimitive`). See research/discoveries.md.
- CONFIRMED: "dplib" (`c:\visual~1\dplib`) is Davilex's **thin C++ wrapper over
  Direct3D**, not a software rasterizer: its class names (d3dev, viewport,
  light, material, mesh, drworder, txformat) map onto the D3D 1-5 object model,
  and its error state machine stores genuine `D3DERR_*` names.
  `n:\libraries\a2racerii\win3ddp\versie_1` is the version used by A2 Racer II.
  The `.d3d` extension likely still means "Davilex 3D data" (their own mesh
  format, not a Microsoft one).
- STRONGLY SUPPORTED: the **"Righteous dd3ddfx driver found %d" check (0x4650C0,
  reached from the DirectDraw enumeration at 0x46D270) is how the game enables
  3Dfx hardware acceleration.** On Voodoo Graphics / Voodoo Rush cards the 3D
  chip appeared as a *secondary* DirectDraw device (driver name `dd3ddfx`, the
  Diamond "Righteous 3D" board); picking that device and then querying
  `IID_IDirect3D2` from it yields a hardware D3D HAL. This reading only became
  available once real-D3D use was established; previously the check looked
  purposeless. Not yet proven — needs the consumer of 0x4650C0's return value.
- CONFIRMED: 16-bit mode uses 5:6:5 surfaces; the game enumerates D3D texture
  formats and picks `TXF_16BPP_RGB = 3` (RGB 5:6:5), with 5:5:5:1, 4:4:4:4 and
  8:8:8:8 available for alpha. 8-bit mode uses palette-indexed textures with
  per-track PCX palettes and selects the D3D Ramp/Mono device.
- OPEN: whether the retail game ever ran a *software* fallback beyond D3D's own
  HEL/RGB emulation device (it does enumerate and can create the HEL RGB device).

## Track data

- STRONGLY SUPPORTED: etappe0 and etappe6 are the same road geometry with different scenery/draw data (identical .rmp/.pmp/.cmp/.pth sizes; different .dmp/.omp). Possibly the same stretch driven in opposite directions (A2 Amsterdam↔Utrecht?).
- **CONFIRMED (session 5):** `.pmp` is the per-chunk table — position plus index
  lists into `.cmp` (rails) and `.dmp` (left/right waypoints). Its chunk count
  matches `.rmp`'s second header word and the runtime `m_chunks alloc`.
- **RESOLVED (session 5, and the guess was wrong):** `.dmp` is the **AI waypoint
  graph** (`c_waypoint g_driver_map`), not a "draw map"; `.cmp` is the **rail /
  guardrail list** (`c_rail g_rails`), not general collision data. Both names
  come from the game's own runtime allocations, and the `.pmp` per-chunk index
  lists partition both arrays exactly. See file-formats.md.

## Gameplay

- WITHDRAWN (session 10) for QUICK SERVICE: it is **not** a cheat. Banner 48
  QUICK SERVICE is raised by the **trigger-2 service station** handler
  (0x42a51d), which refills the turbo tank, repairs the damage and plays
  `repair.wav`. See "Trigger 2 is the service station, in full" in
  discoveries.md. ~~INVINCIBLE (46) and COP MODE (47) remain SPECULATIVE as
  player-selectable cheat/debug toggles gated by TWEAK_MODE.~~ **RESOLVED
  (session 15), and not gated by TWEAK_MODE:** INVINCIBLE is the Ctrl + Space
  + I cheat (0x424fbe, after the tweak-mode keys, outside their test).
  COP MODE is Game.ini `copland`, which menu.dat sets when the player is
  named COPLAND. It gives the player the police car, confirmed under Wine.
  See discoveries.md "COP MODE".
- SPECULATIVE: `lan_main.cpp` is dead code from a shared codebase (no network imports/strings anywhere).

## Misc

- **DISPROVEN (session 5):** the char-range string after names in the named IND
  format was guessed to be a per-entry type/attribute marker. It is nothing of
  the kind — the name field is a fixed 20-byte buffer and those bytes are simply
  uninitialised leftovers of the writer's shared buffer. It "shrinks toward 23"
  only because later names are longer and overwrite more of the leftover text.
  See research/discoveries.md.

# menu.dat — the front end

Session 12. menu.dat (v1.01, PE timestamp 0x363DD150) is the game's front end.
The shell a2_II.exe runs it, it exits with **40** to start a race (the shell then
runs spel.dat) or with 0xff to quit, and it is run again after every race.
State passes through Game.ini (the race's side: "What spel.dat writes back to
Game.ini" in research/discoveries.md) and the menu's own **SLOTS.RAS** and
**menu.ini**.

The port's rebuild is `port/src/menu/` (`a2menu`). Tools:
`reverse-engineering/tools/menutools.py` (sprite and text location in
captures, contact sheets, diffs), `winemenu.sh` (drive the original under Wine
on a private Xvfb) and `pecalls.py` (call sites and their pushed arguments).

## Source files and screens

From the assert strings (`C:\work\A2racII\menu\Ned\prog\...`):

| screen | source | open / close | form (code's copy) | header (anim 3 frame) |
|---|---|---|---|---|
| main (HOOFDMENU) | m_main.cpp | mm_open_screen 0x406fd0 / mm_close_screen 0x407130 | 0x42ed40 | 1 |
| controls (BESTURING) | m_bediening.cpp | mbed_open_screen 0x4034c0 / 0x403560 | 0x42a238 | 4 |
| garage | m_garage.cpp | — / mr_close_screen 0x404280 | 0x42b628 | 2 |
| standings (STANDEN) | m_highscore.cpp | mhigh_open_screen 0x405530 / 0x4055e0 | 0x42c2f8 | 6 |
| showroom (kopen) | m_kopen.cpp | — / mk_close_screen 0x405ed0 | 0x42da50 | 3 |
| new player | m_nspeler.cpp, req_nsp.cpp | msn_open_screen 0x407d80 / mnsp_close_screen 0x407e50 | 0x430080 | 0 |
| options (OPTIES) | m_opties.cpp | mo_open_screen 0x408e00 / 0x409160 | 0x430f60 | 5 |
| yes/no requester | janeereq.cpp | ja_nee_req_open_screen 0x402430 / 0x402570 | — | — |

Other files: A2menu.cpp, main.cpp (WinMain 0x409a20.. 0x409c6f), ini_io.cpp,
slotio.cpp, bestand.cpp, Menuanim.cpp, special_anim.cpp; the widget library
`C:\work\winlib\` (Forms, Buttons, Animbuts, Tgglbuts, Radiobut, Editbox,
Ownedit, Controls, Help, Mouse, Smacker, Sound, Sprite, Pal, Rcpcx, ...).
Over 100 functions were named from their assert strings (add_animated_button
0x40c470, create_button 0x41b180, create_areabutton 0x41a830,
create_radiobutton 0x419b30, get_centered_text 0x40d040, print_centered_text
0x40cef0, show_car_property_bar 0x40c960, load_slot 0x40b170, save_slot
0x40b120, init_slots 0x40b6c0, play_help 0x40fcb0, playing_smacker 0x416e30,
...).

The form tables exist in several identical copies (one per source file that
includes them); the copies listed above are the ones the code references.

## Assets (CONFIRMED)

- **Palette**: the seven full-screen PCXs in Mrcs.img (hoofd, garage, bedien,
  Newgame, showroom, opties, hiscore) share one palette byte for byte, and it
  is MENURC's. Wine captures of menu.dat match sprites rendered in it with
  zero error, except that palette black shows as (1, 0, 0).
- **Backgrounds** hold the panel frames only; headers, buttons, text, cars,
  icons are drawn on top. Some default-state art is baked in (the green
  damage icon, sprite 136, sits exactly at (349, 180) in hoofd.PCX).
- **Fonts**: the seven nested `.IMG` files are fixed-cell bitmap fonts (format
  in file-formats.md). Text advances one cell per character.
- **Sounds and music**: CLICK1-3, klik, fout, slide, print, glow, flash,
  repair, banden, motor, antiblok, versnelbak, energy/jammin/moody/rock/
  speed/disco/speed2 (music), ALGINTRO, and ~75 spoken help clips
  `Help_NN*.WAV`.

## Forms (Forms.cpp)

A form is an array of 28-byte records `{x, y, type, descriptor*, state, 0, 0}`
ending in `x = -1, type = -1`. `state` 1 marks the control that starts with
the focus (START on the main screen), 3 the others.

Control states (CONFIRMED, set_control_state 0x410ec0, jump table 0x410fc8):
1 = SetFocus, 2 = EnableWindow(FALSE), 3 = EnableWindow(TRUE), 4 = pressed
(remembered in 0x438548). The function writes the new state into the form
record itself (+0x10, the previous one in +0x14). create_button (0x41b2be)
applies the record's state, with 4 turned into 1. draw_button (0x41b41c)
maps the owner-draw itemState: ODS_DISABLED → 2 (frame 2),
ODS_FOCUS → 1 (frame 3), ODS_FOCUS|ODS_SELECTED → 4 (frame 1), else 0
(frame 0). So the record keeps the last-focused button, and a screen opens
again on the button that had focus when it was left. Example: GARAGE is
focused on the main screen after returning from the garage, as in the Wine
capture f0_main. The form arrays live in menu.dat's data, so the shell's
restart of menu.dat after each race brings back the state-1 defaults.
Activating a control also focuses it: mouse clicks go through
check_form_key_pressed 0x411d33 → SetFocus; hotkey paths set it explicitly,
e.g. main 0x407a5b set_focus(6), the EINDE row.

- **type 1, animated button**: descriptor (37 bytes, packed) `{i32 id, -1, -1,
  char hotkey, char *label, i32 1, font*, 0, -1, 0}`. Size 148x42 (the HELP
  badge, id 4, is 52x17).
- **type 4, hot area**: descriptor `{w, h, -1, 0, 0}`.

Main menu: Start (S) 8,63 · Garage (G) 8,123 · Besturing (B) 8,183 · Opties (O)
8,243 · Standen (C) 8,303 · Nieuwe speler (P) 8,363 · Einde (E) 8,423 · HELP
562,14; areas: header 0,0 640x47, player panel 186,66 429x192, next stage
186,277 281x147, controls 468,277 149x147. After a finished career the Start
label is "Restart" (0x40703a).

## Animations (Menuanim.cpp) and sprite numbers

The table at **0x433e60** (registered by 0x413750 from 0x409e15) holds 60
entries `{frames, 1, frame list*}`; a frame is `{2, sprite number}` and sprite
numbers are **MENURC index + 1** (as RC.IMG in the race). Frames advance when
more than 33 ms have passed (0x40bc7d). The ones identified (0-based MENURC
indices):

| anim | frames | use |
|---|---|---|
| 2 | 30 | the hand cursor |
| 3 | 23..29 | headers: new player, main, garage, showroom, controls, options, standings |
| 4 | 21 22 21 21 | HELP badge (normal, lit) |
| 5..11 | e.g. 32 33 52 31 | buttons by row: normal, pressed (bright), blank, focused (blue border) |
| 12..18 | device icons in states (available, selected, crossed) |
| 19 | 135 134 | next-stage picture: road (highway), city (circuit) |
| 20 | 136 138..145 137 | damage icon, ten steps green to red |
| 21 | 148 146 147 149 | control device: keyboard, joypad, joystick, wheel |
| 28..34 | 40 frames each | turntable cars (Lambdaghini, Astro, Wolfswagen, Brabant, Mersedes, BWM, Parche) |
| 37..40 | | small cars for the standings rows |
| 44..46, 41..43 | | check boxes, toggles |
| 47..52 | | garage tiles: motor, banden, reparatie, turbo, abs, versnellingsbak (normal, lit, crossed, bought) |
| 54 | 22 | property bar ripple |
| 0, 57..59 | | stage maps (Dutch; 57 has the German edition's München–Berlin / Berlin) |

## Cars (0x42df54)

48-byte records `{i32 anim, char name[40], i32 index}`:
Brabant (31), Opol Astro (29), Wolfswagen (30), BWM Z3.5 (33), PARCHE 913 (34),
Mersedes SLK GTR (32), Lambdaghini (28). The turntable box is sprite 133 at
(432, 107); frames are drawn at the box's centre (box + its anchor, 522, 178)
minus the frame's anchor (CONFIRMED against the capture).

## Drawing rules (measured on captures, confirmed in code where noted)

- **Text x is rounded down to even.** Every string measured sits at its
  computed x & ~1 (Kwalificatie 278 for 279, the car name 436 for 437,
  button labels), while sprites land on odd x (car frames at 453). LIKELY a
  word-aligned text blit.
- **Centring** is get_centered_text (0x40d040): `x1 + ((x2 - x1 + 1) -
  n * cell) / 2`, y likewise; then the even-x rule. Button labels are centred
  over `x .. x + 148` at `y + 8` (BUTFNT), small buttons in SMALLBT.
- **Ink**: the set-colour call 0x40f4f0 makes the font's own ink index (font
  header u16 at 260: STDFONT 0x3f) draw in another colour; CONFIRMED on the
  garage (labels 0x1f, values and prices 0x3a, the car panel 0x8f).
- **Animated buttons take their sprite anim from their id** (ids 5..11 are
  anims 5..11; the garage's Showroom/OK/Annuleren are ids 7/8/9 and use anims
  7/8/9 although they sit on the rows of 9..11). Frames: normal, pressed,
  blank, focused.
- **Slide-in**: create_sine_table (0x40bd90) gives `(int)(sin(t) * amp)` for t
  from 0 in steps of 1.57 / n while t < 1.57. Buttons: n = 8 from x = -100 to
  their x, a step per > 33 ms after a per-button delay in ticks (the
  add_animated_button argument: main 0 2 2 4 6 8 10, garage 0 2 4, others 0 2
  or 0). Header: n = 10 over its height (47), a step per >= 20 ms, y =
  table[k] - 47.
- **Requester** (janeereq.cpp): the screen is darkened by setting every pixel
  with (x + y) odd to black (0x411ff0, CONFIRMED on two captures); panel
  sprite 150 drawn by its anchor at the screen centre (lands at 120, 129);
  title STDFONT centred in (130, 138)-(510, 163); question midfont, words
  wrapped to 320 px from (165, 170), 26 px lines; JA / NEE plates (151, lit
  152) at (145, 313) / (335, 313) with SMALLBT labels. JA answers yes; NEE and
  Escape no. The port's requester matches a capture pixel for pixel.
- **Panel labels are STDFONT, not Windows text** (CONFIRMED, session 19,
  captures). The player panel, the option labels, "Gevoeligheid" and the
  garage's property-bar labels and values match **STDFONT drawn as it is**,
  pixel for pixel. That means the glyph's own one-pixel black edge, no extra
  outline or shadow, and the ink recoloured.
  - 18 strings were fitted on main_idle, orig_options, orig_controls and
    orig_garage, all at 0 pixels off.
  - Inks: labels 31 (251,251,255), player values 0x3a (179,183,255),
    property-bar labels 47 (255,255,139), Gevoeligheid with the keyboard
    100 (107,107,123).
  - The budget reads **"Fl %d"**, not "FL".
  - Positions are in m_screens.c, m_options.c, m_controls.c and m_garage.c.
  - Not checked: v1.01 (no fit).

  The earlier reading, DrawTextA in the system font with a stand-in, was
  wrong for these labels. Wine's system font cannot produce STDFONT glyphs.
  What draw_control_text 0x4111a0 draws, if anything on these screens, is
  open question 61. The code facts below (session 13) still stand:
  - It imports no CreateFont* call.
  - The style table at 0x439d38 ({font, bk mode, bk colour, text colour} ×
    4) holds font 0 in every entry, and nothing writes to it.
  - So draw_control_text 0x4111a0 skips its SelectObject, SetTextColor,
    SetBkColor and SetBkMode calls and DrawTextA uses the child window DC's
    default font, the Windows **system font**.
  - The alignment is DT_SINGLELINE|DT_VCENTER with DT_CENTER (mode 0),
    DT_LEFT (mode 1) or DT_RIGHT (mode 2), inside the control's client rect.

## Garage (m_garage.cpp)

- Tiles (0x42b8b8, 120-byte records): MOTOR 186,270 · BANDEN 332,270 ·
  REPAREREN 478,270 · TURBO 186,369 · ABS 332,369 · 6 VERSNELLINGEN 478,369;
  anims 47..52, frames normal / lit / crossed / bought. Label STDFONT centred
  in 134 px at y + 3, price "Fl %d" ink 0x3a at y + 74 unless bought.
- State (0x403da0): owned -> bought; repair at 0 damage -> crossed with no
  price; price over budget -> crossed; under the mouse -> lit.
- Prices: garage.ini `item<i>_car<c>`; repair is per percent of damage.
  Sounds per part: motor.wav, banden.wav, repair.wav, banden.wav (turbo),
  antiblok.wav, "versne~1.wav" (an 8.3 name that is not in Mrcs.img).
- Buying (0x4046f0): budget must cover it; requester "Weet je zeker dat je dit
  onderdeel voor Fl %d wilt kopen?\n\n(Budget: Fl %d)" (repair: "... je auto
  voor Fl %d wilt laten repareren?"); yes sets the slot flag (or damage 0) and
  subtracts the price. The slot is copied on entry and Annuleren restores it.
- Value (0x405ac0): price / 2 - rate * damage + sum of owned parts' price / 2,
  "Waarde : Fl %d" (Brabant: 7500 - 700 = 6800, as captured).
- Bars (0x40cef0, show_car_property_bar 0x40c960): the car's percents
  (garage.ini car values converted by (v - min) * 100 / (max - min) at load,
  0x401787) plus owned parts' percents, clamped 1..100; fill = 121 * pct /
  100; shown number min + (max - min) * pct / 100 with speed 100..300 km/h,
  the rest 0..100 %. A ripple (anim 54) runs over the bars.

## Drawing rules measured on captures (main screen)

- Button label: BUTFNT, upper case, at `x + 74 - 5n - (n & 1)`, `y + 8` for an
  n-character label (all seven main buttons).
- Panel titles in STDFONT at (338, 74) "Spelerinformatie", (266, 286)
  "Volgende etappe", (506, 286) "Besturing".
- Next stage: anim 19 picture at (214, 340); "Kwalificatie" (highway) or
  "Stedenrace" (circuit) in STDFONT and the Raser.ini [MAPS] name in midfont,
  centred in a width of 225: `x = 214 + (225 - 8n) / 2` resp. 10n, at y 345
  and 365. (get_centered_text computes `x1 + ((x2 - x1 + 1) - n*cell) / 2`.)
- Damage icon anim 20 at (349, 180); device icon anim 21 at (506, 333).
- The player panel's labels and values (Naam, Budget, Schade, "Fl 812000",
  "7 %") are STDFONT: labels in ink 31 at x 206, values in ink 0x3a at x 296,
  rows y 114 / 145 / 201 (see "Panel labels are STDFONT" above).

## Career: SLOTS.RAS, menu.ini, the race handoff

See research/discoveries.md "The menu's career file, SLOTS.RAS" and "When the
Smacker videos play"; layout in file-formats.md. In short: 9 slots of 0x218
bytes; name +0x08, car +0x12 (LIKELY), etappe +0x16, money +0x1e, damage
+0x2b, keys +0x3b, standings +0x58. On start-up with `finished=1` the menu
takes money and damage from Game.ini and advances the stage (0x409870);
after the last stage the win/lose video plays and the career restarts.

## Other screens (session 12)

- **Showroom** (m_kopen.cpp): candidates are the six other cars from the first
  (0x405cc0), arrows ids 23 / 24 step with wrap; cost = price - own car
  value; KOPEN (id 25) disabled when the budget cannot cover it; requester
  "Weet je zeker dat je je auto voor een %s wilt inruilen? \nInruilkosten: Fl
  %d\n(Budget: Fl %d)" (Meerwaarde when the trade brings money in); yes sets
  the car, subtracts the cost (capped 999999), clears the six part ints.
  Turntable box anim 26 at (432, 311), frames centred on (522, 366).
  Candidate texts at (434, 278/290) are animated text (init_anim_text).
  **Changing candidate slides the cars** (CONFIRMED, code and Wine
  capture). The slide is scroll_special_animation 0x40be30, called with
  (old, new, &turntable, 1) by "next" 0x4064bb and (new, old, &turntable, 0)
  by "previous" 0x406600. It is a blocking loop of 20 steps, one per
  > 33 ms tick:
  1. clear a 2w × h strip (w 181, h 110) to the key 0xff (0x40e2b0);
  2. draw the first car centred at (w/2, h/2) and the second at
     (w + w/2, h/2), both in the turntable's frame, which keeps advancing;
  3. Blt the strip's window [off, off + w) with DDBLT_KEYSRC onto the box
     (anim 26), then put the box on screen at (432, 311).

  off = table[i] for "next" and w − table[i] for "previous", where table is
  create_sine_table(20, w) and its last entry is w. The candidate texts and
  bars change only after the loop, so the animated text starts once the
  slide ends.
- **Small buttons** also take their anim from the id (arrows 23/24, KOPEN 25,
  Verwijderen 40) and are their sprite's size; labels SMALLBT as written,
  centred, one pixel lower than get_centered_text; disabled = frame 2 with a
  grey Windows-text label.
- **Standings** (m_highscore.cpp): plates anim 58/59 frame k at (177, 59 +
  45k) with Windows-text names; rows (0x404f80) only for groups up to the
  stages driven: name x 325 / time "%02d:%02d" x 540 at y 108 + 36r (ink
  0x3a), small car anim 57 anchored at (460, 118 + 36r); picture anim 0 frame
  group at (304, 258). Results go into group etappe + 1 by position
  (0x401d30 racers "racer N", 0x401ff0 the player), TOTAAL (0x40aeb0) is
  rebuilt from groups 1..etappe by name, inserted in order of total.
- **Options** (m_opties.cpp): options.ras = {i32 sfx on, i32 music on, u8 sfx
  vol, u8 music vol, i32 resolution (0 320x200, 1 640x480, 3 3D card), i32
  difficulty}, defaults 1 1 80 80 0 1; written to Raser.ini / Game.ini at
  START by ini_write_options (SFXVOLUME/MUSICVOLUME = 3100 - 30 * vol, 0 when
  off).
- **Sliders** (CONFIRMED, draw_slider 0x40b880, called by the options
  0x409080 / 0x4090cf, the controls 0x40332c and the drag 0x40baab). Its
  arguments are the hot area's form record and descriptor {w 151, h 30}.
  It draws:
  1. the track, anim 43, at (x, y);
  2. the fill, anim 42, as one IDirectDrawSurface::Blt (0x4122e0,
     vtable+0x14) from the sprite's whole rect onto
     (x + 8, y + 5) .. (knob_x, y + h − 5), so the stripes are **stretched**
     with the value (no scrolling, no clipping); the rows it repeats fit a
     16.16 step truncated to (18 << 16) / 20;
  3. the knob, anim 41, at knob_x = x + 8 + (w − knob_w − 16)·v/100, clamped
     to x + 1 .. x + track_w, at y + 2.

  The drag 0x40b9c0 reads the cursor while the button is held:
  v = trunc((mx − x)·(100.0 / (w − knob_w))), clamped to 1..100. The port
  matches the Wine capture pixel for pixel. The controls screen's
  sensitivity slider (hot area 7 at 215,391; slot +0x53) is drawn only for
  a joystick or wheel. (scroll_special_animation 0x40be30, once suspected
  here, is called only from the showroom: 0x4064bb, 0x406600.)
- **Controls** (m_bediening.cpp): device byte slot +0x22 -> DEVICE 1 2 3 3;
  key names from the 44-entry {VK, name} table at 0x42a4b0 (A..Z, NUMPAD
  0..9, Omhoog Omlaag Rechts Links, SPACE ENTER ALT CTRL); actions ink 31 at
  (426, 177 + 31r), keys ink 0x3a at x 528.
- **Player list** (m_nspeler.cpp, req_nsp.cpp): nine rows, a slot's first int
  marks it in use; CARVED columns name / car picture / budget / stage / total
  time; free row -> name requester ("Vul je naam in en wordt lid van de A2
  racing club", default "Speler", 8 chars) -> new player (0x40b1f0); used row
  -> select; OK makes it active; Verwijderen asks "Weet je zeker dat je dit
  spel wilt verwijderen?". Main menu's Nieuwe speler asks "Weet je zeker dat
  je de race wilt onderbreken?" first; Einde asks "Wil je het spel
  beeindigen?".
- **Sound**: effects via 0x40a170 at vol * 30 - 3100 cB when on: klik.wav
  (button), click3.wav (keyboard focus), glow.wav (garage tile lit),
  motor/banden/repair/antiblok (parts; the gearbox's "versne~1.wav" is not in
  Mrcs.img), slide.wav (buttons sliding in), print.wav (animated text),
  fout.wav, motor.wav on START. Music via 0x40a1c0, looped, by next stage:
  speed energy jammin moody disco speed2 moody (0x42ef10).
- **Buttons act on the mouse press**, not the release: holding the button
  down on GARAGE already shows the garage (Wine capture), and the screens'
  event handlers play klik.wav as they take the event, so the click is heard
  as the button goes down.
- **No player** (Wine, session 18): with the shipped SLOTS.RAS (all nine
  slots empty, menu.ini save_game=2) the original opens HOOFDMENU with an
  empty Naam, and START races as nobody. menu.ini save_game is read once at
  startup (0x401ad0, from WinMain 0x409b82, into 0x43ff00) and written only
  by ini_write_options on START (0x401cc0 tail-jumps to 0x401aa0). The port
  (a port choice, at the player's request) opens the player list with the
  name requester when the active slot is empty and no slot is in use, and
  otherwise makes the most recently played player active.
- **START** (0x40786e): after a finished career (etappe >= 7) Restart
  (0x407320: stage 0, +0x1a 0, standings cleared; car and budget kept);
  plays motor.wav; writes the options and player state; exits with 40.
- **Cheat** (0x4076ea): Ctrl + Space + M held when the main menu opens sets the
  budget to 700000.
- Slot fields (CONFIRMED, session 13):
  - +0x00 in use;
  - +0x04 the play order: 0x40b820 gives the chosen or new player the
    highest value + 1 (0x408401, 0x408699); deleting renumbers the rest down
    (0x40b780); the player list focuses 4 + the slot with the highest value
    (0x40b710, 0x4081e6, again after a delete 0x40856f);
  - +0x1a only ever written, with 0 (new player 0x40b2c5, Restart 0x407376);
  - +0x57 a byte, the standings tab last shown (read 0x40559f, written
    0x4057ad).

## Status of the port's rebuild (session 12)

- All seven screens, both requesters (yes/no, name entry), sound and music,
  the race loop (START -> a2view -> results -> next stage, standings) work and
  were tested end to end headless (etappe1 race, menu advanced to Den Haag,
  TOTAAL summed over two stages).
- Pixel diffs against Wine captures: main, garage, showroom, standings,
  options, controls and the player list match except the Windows-text
  stand-ins and animation phases (turntables, bar ripple). The sliders now
  match exactly.
  The focus rule (see Forms) is implemented: each screen keeps its
  last-focused button until the next race.
- HELP tours (play_help 0x40fcb0): per-screen tables of {form control,
  clip} (main 0x42eeb0, garage 0x42b7e8, showroom 0x42db68, controls
  0x42a430, options 0x431190, standings 0x42c468, player list 0x430228);
  the screen is darkened with the checkerboard except the control being
  explained, each clip plays to its end, a key or click ends the tour
  (CONFIRMED on a capture of the original).
- Smacker videos: the port's decoder matches ffmpeg on every frame and sample
  of intro/win/lose/exit.smk. Intro at start-up (the shell's \FIRST), exit
  on quitting, win/lose after the last stage by who leads TOTAAL (then the
  stage goes back to 0, 0x407688).
- Animated text (init_anim_text 0x40c250, next_animated_text_frame 0x40c110,
  drawn by 0x40c020): a character per > 33 ms tick with print.wav for each
  non-space; the newest |to - from| characters ramp from `from` toward `to`,
  the rest are in `from`, and the animation runs |to - from| ticks past the
  end so the tail settles (showroom 0x8f -> 0x85, garage budget 0x3a ->
  0x3e). The port's settled text matches the capture exactly.
- The race (a2view) now takes its keys from Game.ini [INPUT] as VK codes, so
  the controls screen's bindings apply. Space is a handbrake only with a
  joypad (0x426a02 is in the joypad handler 0x4269c0, not the keyboard
  handler 0x4265b0).
- Joystick/wheel input in the race: a2view follows DEVICE 2 and 3 (see
  discoveries.md "Input devices"); the controls screen's sensitivity slider has
  no effect in the shipped steering model.
- Port choices: a2menu marks a taken race result (finished=0) so it is not
  taken twice when the menu is restarted; menu.dat leaves it to spel.dat.

# MATCHING: rebuilding spel.dat's C++ byte for byte

The method is wacky-re's and redcat-rrr-re's (after fnv_decomp, sam-re and the LEGO Island
decompilation's reccmp), adapted to a 1998 Visual C++ 5.0 game in a Win32 PE executable:

1. pin one exact binary;
2. recover the exact toolchain;
3. match the libraries first (they are free matches);
4. keep one list of every function: the plan and the progress meter;
5. one check nobody can argue with: a function is byte-identical or it is not;
6. rewrite functions one at a time in C++ until each compiles to the same bytes;
7. name things with their evidence, and say what is known about every near miss.

This file is the workflow. The numbers live in `PROGRESS.md` (written by
`reverse-engineering/tools/msvc/progress.py`); do not copy them here.

## 0. The target (CONFIRMED)

`data/spel.dat` from the CD (669,696 bytes, MD5 556416bf..., `reverse-engineering/executables.md`),
a PE32 program with a `.dat` name. Addresses are virtual addresses (image base 0x400000). The
program has no relocation table and no debug directory.

| Range | What | Rebuilt from |
|---|---|---|
| 0x401000-0x47007F | **the game's own code**: `C:\work\A2racII\Game\*.cpp`, `C:\work\winlib\*.cpp`, the 3D library (`D3DLIB.CPP`, `MESH.CPP`, ...) and the input library (`di_*.cpp`), objects in alphabetical order of file name | `src/spel/*.cpp` with `src/spel/spel.h` (this file's subject) |
| 0x470080-0x47D95F | VC 5.0's `LIBC.LIB`, the modules the linker pulled in | the library as it is |
| 0x47D960-0x47E0B4 | not identified yet | |

`menu.dat` has the same structure (its own code 0x401000-0x411C8F, LIBC from 0x411C90) and
comes later; it shares `C:\work\winlib\` with spel.dat.

## 1. The toolchain

- **Compiler: Visual C++ 5.0 as first released**, `cl` 11.00.7022 and `LINK` 5.00.7022.
  CONFIRMED: a program linked with this LINK has spel.dat's linker version (5.00), no Rich header
  and the same DOS stub; and `WinMainCRTStartup` from its LIBC.LIB equals spel.dat's entry in all
  111 instructions (discoveries "Build toolchain").
- **C library: `LIBC.LIB` (`/ML`)**, CONFIRMED by the same start-up code (LIBCMT's has an extra
  `_mtinit` check).
- **Options: `/Ox /Ob2`** (with `/ML`). Each part has its evidence:
  - optimised for speed, frame pointers omitted, `__cdecl`: CONFIRMED. `CalculateStearing`
    (0x401230) does not match with `/O1`, `/Od`, `/Oy-` or `/Gr`.
  - intrinsics on (`/Oi`), no `/Op`: CONFIRMED by `ai_speed_control` (0x4012c0, `abs` inline).
  - no string pooling, so `/Ox` and not `/O2` (which adds `/GF`): CONFIRMED. spel.dat keeps
    duplicate literals ("MESH.CPP" 81 times, "cam_main.cpp" 11), and VC 5.0's `/O2` pools two
    `__FILE__` uses into one where `/Ox` keeps two. `/O2 /GF-` would give the same code.
  - `/Ob2` (inline any suitable function): STRONGLY SUPPORTED. `ai_drive` (0x4013c0) has
    `CalculateStearing` inlined, which other files call out of line (0x40a30a, 0x40a7cb,
    0x40abdb), so it is not an `inline` function; `/Ob1` does not inline it, `/Ob2` matches.
    So a function is inlined into the ones after it in its file: keep the address order.
  - not told apart yet: `/G3` `/G4` `/G5` `/GB`, `/Zp`, `/GX` (open question 62).
- **Set-up and Wine:** `reverse-engineering/msvc5.md`.
- **Driver:** `reverse-engineering/tools/msvc5.sh cl|link|dumpbin|lib ...` runs a VC 5.0 tool under
  Wine in the current directory, with `INCLUDE` and `LIB` set.

## 2. Libraries first

`reverse-engineering/tools/msvc/libmatch.py PROG LIB [-v] [--tsv FILE]` searches the game's `.text`
for every code section of every member of a COFF library, equal except for the relocation bytes.
LIBC.LIB's functions are compiled with `/Gy` (one section each), so the matches are per function.
They need no source. The libraries are linked after the game's objects, so a lone early hit is
dropped (spel.dat 0x448d70 equals `exception::exception`, 32 bytes, by chance).

Davilex's own libraries (`winlib`, the 3D library, the input library) were C++ of the same
compiler, and are part of the game's code here: they are rebuilt like the rest. Whether they
came from prebuilt `.lib` files built with other options is part of open question 62.

## 3. The list of work

- `reverse-engineering/analysis/spel_funcs.tsv` (`tools/msvc/funcs.py spel --tsv ...`): every
  function start in the game's own code, with its evidence: `call` (a `call rel32` target), `ptr`
  (a 16-byte-aligned code address in `.rdata`/`.data`: vtables, tables of callbacks), `pad` (a
  16-byte boundary after a `ret`/`jmp` and 0x90 padding; VC 5.0 `/O2` pads only between
  functions). Each function runs to the next start. All 1,107 starts of the older
  `analysis/funcs2.txt` in this range are in it.
- `reverse-engineering/analysis/spel_modules.tsv` (`tools/msvc/modules.py spel --tsv ...`): the
  source file of each function when it is known. `string`: the function uses that file's
  `__FILE__` string (from an assert or an error message). `between`: it lies between two
  functions of the same file. The objects are linked in alphabetical order of their names
  (`ai_cfun`, `ai_cops`, `ai_fsm`, `ai_lane`, ... in address order), so a file is one run of
  functions.

## 4. The check: `src/check.sh`

```sh
src/check.sh        # compile src/spel/*.cpp (one object per file), compare every annotated function
src/check.sh -v     # also the differing offsets, the reference problems and the inferred addresses
```

Each `src/spel/*.cpp` is compiled with `cl /nologo /c /Ox /Ob2 /ML` into `.work/src/spel/`, and
`tools/msvc/objmatch.py` compares each function the objects define with the game at the address
its annotation gives. Annotations are comments in the style of reccmp (isledecomp):

```cpp
// FUNCTION: SPEL 0x401230          before a definition: compared; before a prototype: where it is
void CalculateStearing(int idx, const float *point)

// GLOBAL: SPEL 0x4a55a0            before a variable: where it is
extern AIRecord ai_records[15];
```

A function is **MATCH** only when:

- its bytes equal the game's everywhere except the 4 bytes of each relocation, and the game
  has only 0x90 padding after it up to the next 16-byte boundary;
- every relocation's target, read from the game's bytes, is right: a call or a global must be
  at the address its annotation gives; a string literal (`??_C@...`, `$SG...`) or a constant
  (`$T...`, `__real@...`) must have the same bytes at that address in the game; a symbol with no
  annotation must point at one address everywhere it is used (`-v` lists these as "inferred").

Tested both ways: a wrong string, a wrong call target and a wrong float constant (0.25 for
0.5) each make the match fail.

## 5. Matching one function

1. Find the function in `PROGRESS.md` or `docs/SPELPROGRESS.HTML`, its file in
   `spel_modules.tsv`, and what is known about it in `research/discoveries.md` and the port
   (`port/src` comments name the addresses they come from).
2. Write it in the `src/spel/<file>.cpp` its source file names (`unplaced.cpp` while the file is
   not known), with a `// FUNCTION: SPEL 0x...` line. Put the types, globals and the functions
   it calls in `spel.h` with their annotations.
3. `src/check.sh -v`. For a near miss:
   `.work/venv/bin/python -I reverse-engineering/tools/msvc/fndiff.py spel .work/src/spel/FILE.obj NAME`
   aligns the game's instructions (left) with ours (right).
4. When it matches, rerun `reverse-engineering/tools/msvc/progress.py`.

The other tools:

- `tools/msvc/show.py spel 0xADDR...` (or `--module FILE.cpp`): the game's function with its
  strings, float constants and annotated names resolved.
- `tools/msvc/variants.py spel FILE.cpp NAME VARIANTS.txt`: compile several spellings of one
  piece of the file and count the differing rows of each (blocks separated by `====` lines;
  the first is the text to replace).
- `tools/msvc/optvar.sh spel FILE.cpp "/Ox" "/O2" ...`: the check under other options.
- `src/check.sh` also warns when an object defines functions in another order than the game
  has them: within one file the linker keeps the source order.

What the first match taught (`CalculateStearing`, 0x401230):

- An uninitialised local can be part of the original. On an index of 15 or more the game reports
  the error and reads the argument's stack slot as the object: the compiler gave the local the
  argument's home. Writing it uninitialised in the same place gives the same code.
- `x > a ? c1 : x < b ? c2 : expr` loads the float constants with `fld`/`fstp`; the same choice as
  `if`/`else if`/`else` stores them as integer immediates (`mov dword ptr [...], 42C80000h`). Both
  are plausible C; only the bytes decide.
- `/O2` pools string literals (`/GF`) into COMDAT symbols named `??_C@...`; the game was not
  built that way (`/Ox`, literals are `$SG...`).

What the next nine taught (0x401000-0x4019ef, `src/spel/unplaced.cpp`):

- A pointer formed before a call keeps its place: `C3dObject *o = &...; o->y = f(x);` computes
  the address before the call, `d3d_objects.data[...].y = f(x);` after it.
- Where a pointer is formed matters in other places too: `bearing` (0x401690) needs it after
  the two subtractions that index the array themselves.
- `a *= 4096` as its own statement keeps two multiplies; in one expression the compiler folds
  the constants.
- `fld x; fcomp st(1)` and `fcompp` compares, and a `fld`/`fstp` copy, come from values
  computed in double (`handle_adac_ai` 0x401000). Plain float source gives `fcom [mem]` and
  an integer `mov` copy.
- `__FILE__` is the path the compiler is given. `src/check.sh` compiles each file as
  `C:\work\A2racII\Game\<file>` (a link in the Wine prefix) unless the file names another
  path in a `// COMPILED AS:` line. `__LINE__` uses are put on the game's lines with `#line`.
- `fabs(a) + fabs(b) < 7` compares in double; the game's float compare came from summing into
  a `float` local, which stays unrounded on the FPU stack. Which field the compiler loads first
  depended on reading both into locals first (ai_target_caught 0x4019f0).
- Functions reached only from tables filled in code (`mov [...], offset fn`) and placed right
  after a `ret` were missing from the function list; `funcs.py` now also takes code addresses
  from immediate operands.
- Headers keep their own `__FILE__` too. The 3D library's `array.hpp` (T_ARRAY, all inline)
  is in `src/win3ddp/include/`; `src/check.sh` links it to drive `n:` in the Wine prefix
  and compiles with `/In:\libraries\a2racerii\win3ddp\versie_1\include`, the original path.
- A float copied with `fld`/`fstp` where plain `a = b` gives an integer `mov`: the value came
  back from an inline function (`HANDLE_COP_AI` 0x40292b). `1.0 * x` and casts are folded.
- Which variable gets the argument's stack slot, and which register a load goes through,
  follow the order of the statements: `ret = 1` after `cop = ...` in `HANDLE_COP_AI`.
- Compiler-made functions are annotated `// SYNTHETIC: SPEL 0xADDR` with their symbol on the
  next line as a comment: `$E4` (the file's dynamic initialiser), `$E2` (the destructor it
  hands to `atexit`). They are compared like the others; their names are per file.
- A member function is reached with `this` in `ecx`: `0x401200` is `AIControl::reset` on the
  record's +0x30, which made the second half of `AIRecord` a nested struct.

## 6. Names, after the match, with their evidence

A name is given when there is evidence for it: the game's own strings ("Function:
CalculateStearing"), the debug log's messages, or what the code is shown to do (in
`research/discoveries.md`). Names without evidence keep the address (`fn_401000`). A name
that is ours, not the original's, says so in a comment.

## 7. What is left

Most of spel.dat and all of menu.dat; the numbers are in PROGRESS.md. Next: a source file at a
time in address order, starting with the files the module map already places (`ai_cfun.cpp`,
`ai_cops.cpp`, ...), and the options still open (question 62).

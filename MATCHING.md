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
- **Options: `/O2`** for now. `CalculateStearing` (0x401230) matches with `/O2`, `/Ox` and `/Ox /Ob2`,
  with any of `/G3` `/G4` `/G5` `/GB`, and with `/Zp1`, `/GX` or `/MT` added; it does not
  match with `/O1`, `/Od`, `/Oy-` (frame pointers) or `/Gr` (fastcall). So: optimised for speed,
  frame pointers omitted, `__cdecl` (CONFIRMED). Which of the rest is open question 62: it needs
  functions that tell them apart (an inlined call, a structure copy, integer division by a
  constant, an exception frame).
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

Each `src/spel/*.cpp` is compiled with `cl /nologo /c /O2 /ML` into `.work/src/spel/`, and
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

What the first match taught (`CalculateStearing`, 0x401230):

- An uninitialised local can be part of the original. On an index of 15 or more the game reports
  the error and reads the argument's stack slot as the object: the compiler gave the local the
  argument's home. Writing it uninitialised in the same place gives the same code.
- `x > a ? c1 : x < b ? c2 : expr` loads the float constants with `fld`/`fstp`; the same choice as
  `if`/`else if`/`else` stores them as integer immediates (`mov dword ptr [...], 42C80000h`). Both
  are plausible C; only the bytes decide.
- `/O2` pools string literals (`/GF`), so they are COMDAT symbols named `??_C@...`.

## 6. Names, after the match, with their evidence

A name is given when there is evidence for it: the game's own strings ("Function:
CalculateStearing"), the debug log's messages, or what the code is shown to do (in
`research/discoveries.md`). Names without evidence keep the address (`fn_401000`). A name
that is ours, not the original's, says so in a comment.

## 7. What is left

All but one function of spel.dat; all of menu.dat. Next: pin the remaining options (open
question 62) with functions that tell them apart, then a source file at a time, starting with
the small files the module map already places.

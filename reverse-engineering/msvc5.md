# The Visual C++ 5.0 toolchain

`spel.dat` and `menu.dat` were built with Visual C++ 5.0 as first released
(compiler 11.00.7022, LINK 5.00.7022) and the single-threaded static CRT
(`LIBC.LIB`). The evidence is in research/discoveries.md, "Build toolchain".
This file says how to set that compiler up so it runs here.

## Source media

The Visual C++ 5.0 Professional CD, as an ISO (WinWorld archive):

| file | SHA-256 |
|---|---|
| `VCPP-5.00.iso` | `3328b827ad403957b6b621bffb9d9619e1407950c52d4663e4c9d0315291a1aa` |

The compiler is not included in this repository.

## Set-up (no installer needed)

The tools sit uncompressed on the CD, so the installer (and its product key)
is not needed. Copy them into `.work/toolchains/vc50/`:

| CD path | to |
|---|---|
| `DEVSTUDIO/VC/BIN/*` | `vc50/bin/` |
| `DEVSTUDIO/SHAREDIDE/BIN/*.DLL` | `vc50/bin/` (`MSPDB50.DLL`, `MSDIS100.DLL`) |
| `DEVSTUDIO/VC/REDIST/MSVCP50.DLL`, `MSVCRT.DLL` | `vc50/bin/` (LINK.EXE needs `MSVCP50.DLL` via `MSDIS100.DLL`) |
| `DEVSTUDIO/VC/INCLUDE/*` | `vc50/include/` |
| `DEVSTUDIO/VC/LIB/*` | `vc50/lib/` |
| `DEVSTUDIO/VC/CRT/*` | `vc50/crt/` (CRT source, for telling library code apart) |

`reverse-engineering/tools/msvc5.sh` runs any of the tools under Wine with
its own prefix (`.work/toolchains/wineprefix-vc50`) and `INCLUDE`/`LIB` set.
Tested with Wine 9.0.

```sh
reverse-engineering/tools/msvc5.sh cl /nologo /c /O2 /ML FOO.CPP
reverse-engineering/tools/msvc5.sh link /nologo /subsystem:windows FOO.OBJ user32.lib
```

## Traps

- Without `MSVCP50.DLL` next to it, LINK.EXE exits silently: no message, no
  output file. CL then reports `D2027 : cannot execute link.exe`.

## Checked against the game (session 20)

- A test program linked with this LINK has linker version 5.00, no Rich
  header, and the same first 0x80 bytes (DOS stub) as `spel.dat`.
- `WinMainCRTStartup` from `/ML` (LIBC.LIB) is the same instruction for
  instruction, addresses masked, as the entry of `spel.dat` (0x471400) and
  `menu.dat` (0x41C430): 111 of 111. The `/MT` (LIBCMT) version differs: it
  has an extra `_mtinit` check (`push 0x10; call fast_error_exit`).

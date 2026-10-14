# Executable analysis (initial)

All executables on the CD are MSVC-built PE32 i386 GUI binaries. Section layout is uniform: .text/.rdata/.data(+.idata or .rsrc), ImageBase 0x400000, SectionAlign 0x1000, FileAlign 0x200.

## data/spel.dat — main game engine (renamed PE)

- Size 669,696; timestamp 0x364180B3 (1998-11-05 13:55); MD5 556416bfff29667e455d8b0fc246b75c
- Entry point RVA 0x71400; sections: .text VA 0x1000 VSize 0x7D0B5 (512,181 B of code!), .rdata, .data VSize 0x7A074, .idata (imports). No resources.
- Imports: USER32 (window/msg), KERNEL32 (file/ini/heap/QPC/GetTickCount), GDI32 (text only), **DDRAW.dll** (DirectDrawCreate, DirectDrawEnumerateA), **DINPUT.dll** (DirectInputCreateA), **DSOUND.dll** (DirectSoundCreate/Enumerate), **WINMM.dll** (mixer*, joyGet*, timeGetTime), ADVAPI32 (GetUserNameA). No CreateProcess, no D3D/Glide/OpenGL, no winsock/dplay.
- MSVC runtime (FreeEnvironmentStringsW etc.), `Microsoft Visual C++ Runtime Library` strings. Compiler: MSVC (likely 5.0, 1997-98 era CRT).
- Assert/debug strings embed source paths → full module map (see analysis/spel_src.txt) and many real function names (analysis/spel_funcs.txt).

## data/menu.dat — menu front-end (renamed PE)

- Size 253,440; timestamp 0x363DD150 (1998-11-01); MD5 4cb67aa27fac369fd7f04eb70cfaa1b5
- Entry point RVA 0x1C430; .text VSize 0x24ADD. No resources.
- Imports: same core set + **SMACKW32.DLL ordinals #14,18,19,21,23,25,32,36** (Smacker video playback). DINPUT/DSOUND/WINMM/DDRAW like the game. No CreateProcess.
- Source tree `C:\work\A2racII\menu\Ned\prog\` + `C:\work\winlib\` (Smacker.cpp, Buttons, Controls, Editbox, Radiobut, Animbuts, Tgglbuts, areabuts, Ownedit, Help).

## data/qrace.exe — launcher/dialog

- Size 56,320; timestamp 0x362475BD (1998-10-14); MD5 aa5fec964f77a30e749979a31be07d4a
- Entry 0x19E0; has .rsrc (dialog resources); imports CreateProcessA, GetPrivateProfileIntA, WritePrivateProfileStringA, DDRAW (Enumerate only), COMCTL32 ordinals, USER32 dialog functions.
- Strings: "game.ini", ".dat", "Failed to launch A2Racer 2". Reads game.ini, launches a .dat program. Which key — TBD (small disassembly job).

## data/a2_II.exe — launcher stub

- Size 28,672; timestamp 0x351229C0 (1998-03-20); MD5 4f4112906a8448a6da1e2a9d5b6478c1
- Entry 0x1620; has .rsrc; imports CreateProcessA, WaitForSingleObject, GetExitCodeProcess, LoadLibraryA/GetProcAddress/VirtualAlloc. Hardcoded target string "spel.dat".

## data/instlng.exe — setup helper

- Size 67,584; timestamp 0x36235BD4 (1998-10-14). Dialog-based (CreateDialogParamA, CheckRadioButton…), probes DirectDraw (Enumerate), writes INI. Probably the install-time language/settings chooser.

## setup.exe / service/setup.exe — installer

- setup.exe: PE32, timestamp 0x33305FF8 (1998-03-03), exports "GLBSInstall", strings "Corrupt installation detected." — InstallShield-style bootstrapper that extracts `service/setup.exe`.
- service/setup.exe: NE (Win16) executable — the real installer engine (probably InstallShield 3-era), handles file copy, shortcuts, registry, DirectX install (dxsetup).

## Autorun.exe

- CD-root autoplay stub (PE32, 8 sections). Target: opens setup.exe per autorun.inf (`open=autorun.exe`, `icon=data\a2_ii.ico`).

## SMACKW32.DLL

- Rad Game Tools Smacker runtime, 66,048 bytes, built 1996-07-10. Imported by ordinal only (menu.dat). Needed to play intro/win/lose/exit.smk (SMK2 v2 format, 320x240).

## Notes

- The .dat executables are plain PE files with a .dat extension (obfuscation-by-renaming; they still run because CreateProcess doesn't care).
- DirectX 5.0 is bundled on the CD (dxsetup, dsetup.dll 1998-01-27); d3dim.dll etc. in dxsetup are just redistributables — NOT evidence of D3D use by the game.
- Debug infra present: debug.txt logging (Raser.ini DEBUGON), assert strings with file+line, TWEAK_MODE extras.

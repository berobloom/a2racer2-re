#!/bin/sh
# Compile each module of src/spel/ (src/spel/*.cpp, sharing src/spel/spel.h) with the game's
# compiler and options (Visual C++ 5.0, cl /Ox /Ob2 /Gy /ML; reverse-engineering/msvc5.md) into
# .work/src/spel/<module>.obj, and compare every annotated function with spel.dat
# (reverse-engineering/tools/msvc/objmatch.py).     src/check.sh [-v]
#
# __FILE__ is the path the compiler is given, and the game's asserts carry the original
# paths, so each file is compiled as C:\work\A2racII\Game\<file> (the Wine prefix maps that
# directory to .work/src/spel). A file that came from elsewhere names its path in a line
# "// COMPILED AS: <path>" (e.g. MESH.CPP for the 3D library, compiled from its own directory).
set -eu
cd "$(dirname "$0")/.."
# The 3D library's headers come from its directory on the original machine (array.hpp's
# __FILE__ is n:\libraries\a2racerii\win3ddp\versie_1\include\array.hpp): drive n: in the Wine
# prefix holds that path, linked to .work/src/win3ddp.
CFLAGS="/nologo /c /Ox /Ob2 /Gy /ML /In:\\libraries\\a2racerii\\win3ddp\\versie_1\\include"
top=$(pwd)
w=.work/src/spel; mkdir -p $w; rm -f $w/*
cp src/spel/*.h src/spel/*.cpp $w/
w3=.work/src/win3ddp; rm -rf $w3; mkdir -p $w3; cp -r src/win3ddp/include $w3/
game=.work/toolchains/wineprefix-vc50/drive_c/work/A2racII
mkdir -p $game
[ -L $game/Game ] || ln -sfn "$top/$w" $game/Game
pfx=.work/toolchains/wineprefix-vc50
mkdir -p $pfx/drive_n/libraries/a2racerii/win3ddp
[ -L $pfx/dosdevices/n: ] || ln -sfn ../drive_n $pfx/dosdevices/n:
[ -L $pfx/drive_n/libraries/a2racerii/win3ddp/versie_1 ] || ln -sfn "$top/$w3" $pfx/drive_n/libraries/a2racerii/win3ddp/versie_1
cd $w
for f in *.cpp; do
    m=${f%.cpp}
    as=$(sed -n 's/^\/\/ COMPILED AS: *//p' "$f" | head -1 | tr -d '\r')
    [ -n "$as" ] || as="C:\\work\\A2racII\\Game\\$f"
    "$top/reverse-engineering/tools/msvc5.sh" cl $CFLAGS /Fo$m.obj "$as" > $m.out 2>&1 || true
    grep -E 'error|warning' $m.out || true
    [ -f $m.obj ] || { echo "src/check.sh: no object for src/spel/$f (see $w/$m.out)"; exit 1; }
done
cd "$top"
.work/venv/bin/python -I reverse-engineering/tools/msvc/objmatch.py spel src/spel $w/*.obj "$@"

#!/bin/sh
# Compile each module of src/spel/ (src/spel/*.cpp, sharing src/spel/spel.h) with the game's
# compiler and options (Visual C++ 5.0, cl /Ox /Ob2 /ML; reverse-engineering/msvc5.md) into
# .work/src/spel/<module>.obj, and compare every annotated function with spel.dat
# (reverse-engineering/tools/msvc/objmatch.py).     src/check.sh [-v]
set -eu
cd "$(dirname "$0")/.."
CFLAGS="/nologo /c /Ox /Ob2 /ML"
w=.work/src/spel; mkdir -p $w; rm -f $w/*
cp src/spel/*.h src/spel/*.cpp $w/
cd $w
for f in *.cpp; do
    m=${f%.cpp}
    ../../../reverse-engineering/tools/msvc5.sh cl $CFLAGS /Fo$m.obj $f > $m.out 2>&1 || true
    grep -E 'error|warning' $m.out || true
    [ -f $m.obj ] || { echo "src/check.sh: no object for src/spel/$f (see $w/$m.out)"; exit 1; }
done
cd - >/dev/null
.work/venv/bin/python -I reverse-engineering/tools/msvc/objmatch.py spel src/spel $w/*.obj "$@"

#!/bin/sh
# Compile src/PROG/FILE.cpp with each set of options given and print the check for each:
# which options a set of matched functions can and cannot tell apart (open question 62).
#   reverse-engineering/tools/msvc/optvar.sh spel unplaced.cpp "/O2" "/Ox" "/O2 /G5" ...
set -eu
cd "$(dirname "$0")/../../.."
prog=$1; file=$2; shift 2
w=.work/optvar/$prog; rm -rf $w; mkdir -p $w
cp src/$prog/*.h src/$prog/*.cpp $w/
m=${file%.cpp}
for o in "$@"; do
    rm -f $w/$m.obj
    (cd $w && ../../../reverse-engineering/tools/msvc5.sh cl /nologo /c $o /Fo$m.obj $file >/dev/null 2>&1) || true
    echo "== $o"
    [ -f $w/$m.obj ] && .work/venv/bin/python -I reverse-engineering/tools/msvc/objmatch.py $prog $w $w/$m.obj | sed 's/^/   /'
done

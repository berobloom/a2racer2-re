#!/bin/sh
# Run a Visual C++ 5.0 (RTM) tool under Wine: the toolchain spel.dat and menu.dat were built with.
#   reverse-engineering/tools/msvc5.sh CL /nologo /c /O2 /ML FOO.CPP
#   reverse-engineering/tools/msvc5.sh LINK ...      (also DUMPBIN, LIB, EDITBIN)
# Runs in the current directory. Set-up: reverse-engineering/msvc5.md.
set -eu
top=$(cd "$(dirname "$0")/../.." && pwd)
tc=$top/.work/toolchains
[ -x "$(command -v wine)" ] || { echo "msvc5.sh: wine not found" >&2; exit 1; }
[ -f "$tc/vc50/bin/CL.EXE" ] || { echo "msvc5.sh: no $tc/vc50 (see reverse-engineering/msvc5.md)" >&2; exit 1; }
tool=$(echo "$1" | tr a-z A-Z); shift
export WINEPREFIX=$tc/wineprefix-vc50 WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
v=$(winepath -w "$tc/vc50")
INCLUDE="$v\\include" LIB="$v\\lib" exec wine "$tc/vc50/bin/$tool.EXE" "$@"

#!/bin/sh
# Play the original game under Wine on your own display, the way it starts on
# Windows: a2_II.exe (the shell) runs menu.dat, START makes the menu exit with
# 40, the shell runs spel.dat, and the menu comes back after the race.
#
#   reverse-engineering/tools/play-original.sh
#
# Runs from the test copy .work/menutest (prefix .work/wp).  Its spel.dat has
# the 32-byte waypoint guard (patch_wp_guard.py) so etappe2 does not fault
# under Wine; .work/game stays the untouched original.
#
# Before each start it puts the menu's saved resolution (options.ras, i32 at
# offset 10) back to 3 = 640x480x16.  The menu writes that value into
# Raser.ini RESOLUTION when START is pressed, and the default 0 (320x200x8),
# like 1 (640x480x8), fails under Wine with "could not set display mode".
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
DIR="$ROOT/.work/menutest"
export WINEPREFIX="$ROOT/.work/wp"

res=$(od -An -t d4 -j 10 -N 4 "$DIR/options.ras" | tr -d ' ')
if [ "$res" != 3 ]; then
    printf '\003\000\000\000' | dd of="$DIR/options.ras" bs=1 seek=10 conv=notrunc 2>/dev/null
    echo "options.ras: resolution $res -> 3 (640x480x16)"
fi

cd "$DIR"
exec env -u WAYLAND_DISPLAY NOCD=C WINEDEBUG=-all wine ./a2_II.exe

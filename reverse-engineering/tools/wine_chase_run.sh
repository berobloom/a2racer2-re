#!/bin/bash
# Original under Wine (session 13): gas past the cops at etappe1 chunk 440,
# brake to a stop, and sample the AI records with gdb_sample.py.
#   wine_chase_run.sh <outdir>
# Before: in .work/menutest set Game.ini `startchunk=430` (uncomment it) and
# Raser.ini RESOLUTION=3; back both up and restore them afterwards
# (menutest-reset.sh does not restore Raser.ini).  The RACER_LIMIT=1000 below
# keeps the cops off the racers; drop it to see the shipped behaviour.
# EXTRA='; PLAYER_SPEED=12.0' adds sampler options, NOKEYS=1 skips the keys.
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd); S=$1
export WINEPREFIX=$ROOT/.work/wp
cd $ROOT
reverse-engineering/tools/winemenu.sh start .work/menutest spel.dat
WPID=$(winedbg --command "info proc" 2>/dev/null | awk "/'spel.dat'/{print \$1}" | head -1)
echo "wpid $WPID"
winedbg --gdb --no-start 0x$WPID > $S/wdbg.log 2>&1 &
sleep 3
PORT=$(ss -ltnp 2>/dev/null | awk '/wineserver/{split($4,a,":"); print a[length(a)]}' | head -1)
echo "port $PORT"
gdb -q -batch -ex "python PORT='$PORT'; OUT='$S/ai_chase.txt'; SECS=${SECS:-110}; EVERY=10; RACER_LIMIT=1000.0${EXTRA:-}" -x reverse-engineering/tools/gdb_sample.py > $S/gdbs.log 2>&1 &
G=$!
sleep 4
if [ -z "${NOKEYS:-}" ]; then
DISPLAY=:98 xdotool keydown Up; sleep 9; DISPLAY=:98 xdotool keyup Up
DISPLAY=:98 import -window root $S/c_after_gas.png
DISPLAY=:98 xdotool keydown Down; sleep 15; DISPLAY=:98 xdotool keyup Down
else sleep 24; fi
DISPLAY=:98 import -window root $S/c_stopped.png
sleep 20; DISPLAY=:98 import -window root $S/c_later.png
wait $G
reverse-engineering/tools/winemenu.sh stop

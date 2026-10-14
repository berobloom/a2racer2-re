#!/bin/sh
# Stand-in "race" for testing a2menu's loop headless: a2view on autopilot,
# saving its results to Game.ini as a real race does.
#   a2menu <datadir> --race reverse-engineering/tools/race-autopilot.sh
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
exec "$ROOT/port/build/a2view" "$1" "$2" --drive-test --autopilot --no-cops --no-clock --save --no-sound

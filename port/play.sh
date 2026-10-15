#!/bin/sh
# Play the port the way the original runs: intro video, menu, races, back to
# the menu, the ending after the last stage.
#
#   port/play.sh <game data dir> [--fullscreen] [--fresh]
#
# <game data dir> is the directory with the original's files (spel.dat,
# RCS.IMG, etappe*.*, the .smk videos, Game.ini ...).  It is never written
# to: the game plays from a private copy in $A2_PLAY_DIR (default
# ~/.local/share/a2racer2), where everything is linked except the files the
# game writes (Game.ini, Raser.ini, menu.ini, SLOTS.RAS, options.ras,
# CARS.DAT), which are copied once and then keep your careers.
#   --fullscreen   the whole screen instead of a 1280x960 window
#   --fresh        throw the saved state away and copy it again
set -e
here=$(cd "$(dirname "$0")" && pwd)
[ $# -ge 1 ] || { sed -n 2,16p "$0"; exit 1; }
data=$(cd "$1" && pwd)
shift
fresh=0
for a in "$@"; do
    case "$a" in
    --fullscreen) export A2_FULLSCREEN=1 ;;
    --fresh) fresh=1 ;;
    *) echo "unknown option $a" >&2; exit 1 ;;
    esac
done
[ -f "$data/spel.dat" ] || { echo "$data does not look like the game's data (no spel.dat)" >&2; exit 1; }

# build when needed
if [ ! -x "$here/build/a2menu" ] || [ ! -x "$here/build/a2view" ]; then
    mkdir -p "$here/build"
    (cd "$here/build" && cmake -DCMAKE_BUILD_TYPE=Release .. >/dev/null && make -j"$(nproc)" a2menu a2view)
fi

play=${A2_PLAY_DIR:-$HOME/.local/share/a2racer2}/data
mkdir -p "$play"
for f in "$data"/*; do
    name=$(basename "$f")
    case "$name" in
    Game.ini|Raser.ini|menu.ini|SLOTS.RAS|options.ras|CARS.DAT|garage.ini)
        if [ "$fresh" = 1 ] || [ ! -f "$play/$name" ] || [ -L "$play/$name" ]; then
            rm -f "$play/$name"
            cp "$f" "$play/$name"
        fi ;;
    *)
        [ -e "$play/$name" ] || ln -s "$f" "$play/$name" ;;
    esac
done

cd "$play"
exec "$here/build/a2menu" "$play" --race "$here/build/a2view"

#!/bin/sh
# Drive the original menu.dat under Wine on a private Xvfb, for reference
# captures (session 12).  Never touches the user's display.
#
#   winemenu.sh start <datadir> [prog]   Xvfb :98 (640x480x24) + wine ./menu.dat
#                                        (or <prog>, e.g. spel.dat), waits 14 s
#   winemenu.sh click <x> <y>            press and release the left button
#   winemenu.sh move <x> <y>
#   winemenu.sh key <keysym>
#   winemenu.sh shot <out.png>
#   winemenu.sh stop                     wineserver -k, stop Xvfb :98
#
# The data dir must hold real copies of every file the program writes
# (Game.ini, menu.ini, garage.ini, Raser.ini, SLOTS.RAS): a symlink would let
# it write through to the master copy.  .work/menutest is set up that way.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
export WINEPREFIX="$ROOT/.work/wp"
D=:98
case "$1" in
start)
    [ -e /tmp/.X11-unix/X98 ] || { Xvfb $D -screen 0 640x480x24 >/dev/null 2>&1 & sleep 2; }
    cd "$2"
    for f in Game.ini menu.ini SLOTS.RAS options.ras; do
        [ -L "$f" ] && { echo "refusing: $2/$f is a symlink" >&2; exit 1; }
    done
    env -u WAYLAND_DISPLAY DISPLAY=$D NOCD=C WINEDEBUG=-all wine "./${3:-menu.dat}" \
        >"$ROOT/.work/menu-captures/wine.log" 2>&1 &
    sleep 14
    ;;
click)
    DISPLAY=$D xdotool mousemove "$2" "$3"; sleep 0.3
    DISPLAY=$D xdotool mousedown 1; sleep 0.15; DISPLAY=$D xdotool mouseup 1
    ;;
move) DISPLAY=$D xdotool mousemove "$2" "$3" ;;
key) DISPLAY=$D xdotool key "$2" ;;
shot) DISPLAY=$D import -window root "$2" ;;
stop)
    wineserver -k 2>/dev/null || true
    for p in $(pgrep -x Xvfb); do
        tr '\0' ' ' </proc/$p/cmdline | grep -q " $D " && kill "$p"
    done
    ;;
*) sed -n 2,14p "$0"; exit 1 ;;
esac

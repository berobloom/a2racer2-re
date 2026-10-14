#!/bin/sh
# Put .work/menutest back to the state the reference captures were taken in
# (a finished etappe1 race with 7% damage, fresh SLOTS.RAS): the menu writes
# Game.ini and SLOTS.RAS, so any interactive run of the original or the port
# changes it.  The start copies live in .work/menu-captures.
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cp "$ROOT/.work/menu-captures/Game.ini.start" "$ROOT/.work/menutest/Game.ini"
cp "$ROOT/.work/menu-captures/SLOTS.RAS.start" "$ROOT/.work/menutest/SLOTS.RAS"
cp "$ROOT/.work/game/menu.ini" "$ROOT/.work/menutest/menu.ini"
cp "$ROOT/.work/menu-captures/options.ras.start" "$ROOT/.work/menutest/options.ras"
echo "menutest reset"

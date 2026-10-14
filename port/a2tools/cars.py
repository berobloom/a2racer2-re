"""`CARS.DAT` — player-car performance table.

Layout is CONFIRMED from the reader in `init_player_car` (plr_car.cpp,
fcn.004256A0 in spel.dat), which does exactly two reads and then closes:

    fopen("CARS.DAT", "rb")
    fread(0x004A5EE0, 124, 10, f)    // 10 x 124 = 1240
    fread(0x004A5DF0,  24, 10, f)    // 10 x  24 =  240
    fclose                            // 1240 + 240 = 1480 = the whole file

So the file holds **10 car slots**, of which the 7 buyable cars in `garage.ini`
are populated and slots 7-9 are all zero (spare capacity).

Block A, 124 bytes = 31 floats, interpreted as four 6-element per-gear arrays
plus 7 handling values:

    float accel     [6];   // stock
    float accel_up  [6];   // with the gearbox/engine upgrade
    float max_speed [6];   // stock, metres/second (x3.6 = km/h)
    float max_speed_up[6]; // upgraded -- gains a 6th gear
    float handling  [7];   // not yet identified

The speed arrays read as real gear ratios: the Trabant tops out at 41.889 m/s
(151 km/h) stock and 45.889 (165 km/h) upgraded; the Diablo at 66.667 (240 km/h)
and 70.944 (255 km/h).  Stock cars have a zero in the 6th slot -- a 5-speed box
-- while the upgraded array fills it in, which matches the `VERSNELLINGSBAK`
(gearbox) item in garage.ini.

Block B, 24 bytes = 6 floats, is three (a, b) pairs, e.g. (3, 20) (3, 20)
(3, 20) for the Trabant and (2, 30) (3, 15) (5, 40) for the Diablo.  Not yet
identified -- plausibly per-axle or per-upgrade handling/damage constants.

garage.ini names the 7 populated slots: Trabant, Astra, VW, VW, BMW, Porsche,
Diablo (the comments there disagree slightly with the item blocks, which call
slot 5 Porsche and slot 6 Mercedes).
"""

import struct

SLOTS = 10
BLOCK_A_LEN = 124
BLOCK_B_LEN = 24
GEARS = 6

GARAGE_NAMES = ("Trabant", "Astra", "VW", "VW", "BMW", "Porsche", "Diablo")


def parse(data):
    """Return a list of 10 dicts (slots 7-9 are all-zero placeholders)."""
    want = SLOTS * (BLOCK_A_LEN + BLOCK_B_LEN)
    if len(data) != want:
        raise ValueError(f"CARS.DAT: expected {want} bytes, got {len(data)}")
    cars = []
    for i in range(SLOTS):
        a = struct.unpack_from("<31f", data, i * BLOCK_A_LEN)
        b = struct.unpack_from("<6f", data, SLOTS * BLOCK_A_LEN + i * BLOCK_B_LEN)
        cars.append({
            "slot": i,
            "name": GARAGE_NAMES[i] if i < len(GARAGE_NAMES) else None,
            "accel": a[0:6],
            "accel_upgraded": a[6:12],
            "max_speed": a[12:18],
            "max_speed_upgraded": a[18:24],
            "handling": a[24:31],
            "block_b": b,
            "empty": not any(a) and not any(b),
        })
    return cars


def top_speed_kmh(car, upgraded=False):
    arr = car["max_speed_upgraded"] if upgraded else car["max_speed"]
    return max(arr) * 3.6

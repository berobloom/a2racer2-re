/* CARS.DAT -- the player-car performance table.
 *
 * `init_player_car` (plr_car.cpp, fcn.004256A0) reads exactly two blocks and
 * then closes the file:
 *
 *     fread(block_a, 124, 10, f);   // 10 x 124 = 1240
 *     fread(block_b,  24, 10, f);   // 10 x  24 =  240   -> 1480 = whole file
 *
 * So there are 10 car slots; the 7 buyable cars from garage.ini are populated
 * and slots 7-9 are zero.  Block A is 31 floats: four per-gear arrays plus 7
 * handling values.  Speeds are metres/second (x3.6 = km/h) and read as real
 * gear ratios -- the Trabant tops out at 41.889 (151 km/h) stock and 45.889
 * (165 km/h) upgraded, the Diablo at 66.667 (240) and 70.944 (255).  A stock
 * car has 0 in the 6th slot (a 5-speed box) and the upgraded array fills it in,
 * matching the VERSNELLINGSBAK item in garage.ini.
 *
 * Slot n corresponds to RACER_<n+1>_ANIM in spel.dat's anim table, which names
 * the car's mesh and texture (slot 0 -> r_trabbi.d3d + racer02.pcx).
 *
 * The field names below are the developers' own, recovered from the TWEAK_MODE
 * menu still present in spel.dat ("Tweak Menu": 1. Acceleratie constanten,
 * 2. Max. snelheid/versnelling, 3. Schakelpunt vooruit, 4. Schakelpunt
 * achteruit, 5. Remkracht, 6. Afremmen op motor, 7. Rolhoek factoren,
 * 8. Pitch factoren, 9. Hoogte factoren, 10. Stuursnelheid), and each one
 * confirmed by the address its editor reads or writes:
 *
 *   field 24  0x4A5F40  Schakelpunt vooruit    (upshift point)
 *   field 25  0x4A5F44  Schakelpunt achteruit  (downshift point)
 *   field 26            always 0, unused
 *   field 27  0x4A5F4C  Remkracht              (braking force, negative)
 *   field 28  0x4A5F50  Motor vertraging       (engine deceleration)
 *   field 29            **not a handling value**: its one use, at 0x40F90D in
 *                       the yaw update fcn.0040f3c0, is
 *                         d3d_objects[obj.field_08].f_7c -= dt * A[29] * a
 *                       -- it acts on the car's *renderable model*
 *                       (`_d3d_objects`, element type `c_3dobject`), not on its
 *                       physics object, which is why the tweak menu does not
 *                       expose it and why changing it alters neither
 *                       acceleration nor coasting. It is the **body roll
 *                       rate into a turn**: +0x7c is the roll that the roll
 *                       spring 0x40ebd0 integrates (session 13). Trabant 1.6,
 *                       Astra 0.9, four cars at 1.0, Diablo 0.1. drive.c
 *                       applies it.
 *   field 30  0x4A5F58  Stuursnelheid          (steering speed)
 *
 * Block B is three (damping, factor) pairs -- roll, pitch and ride height --
 * matching tweak menu items 7, 8 and 9.
 *
 * The record is indexed by car slot with a 124-byte stride, which the code
 * computes as `idx*31` dwords (`shl eax,5; sub eax,edx; ... [eax*4+field]`).
 */
#ifndef A2_CARS_H
#define A2_CARS_H

#define A2_CAR_SLOTS 10
#define A2_CAR_GEARS 6

typedef struct {
    float accel[A2_CAR_GEARS];              /* "Acceleratie constanten"      */
    float accel_upgraded[A2_CAR_GEARS];
    float max_speed[A2_CAR_GEARS];          /* "Max. snelheid/versnelling", m/s */
    float max_speed_upgraded[A2_CAR_GEARS];

    float upshift_point;    /* "Schakelpunt vooruit",   fraction of gear max */
    float downshift_point;  /* "Schakelpunt achteruit", fraction of gear max */
    float unused_26;        /* always 0 in every shipped car                 */
    float brake_force;      /* "Remkracht", negative                         */
    float engine_decel;     /* "Motor vertraging"                            */
    float roll_rate;        /* body roll rate into a turn, 0x40F90D          */
    float steer_speed;      /* "Stuursnelheid"                               */

    float roll_damping, roll_factor;     /* "Rolhoek demping / factor"   */
    float pitch_damping, pitch_factor;   /* "Pitchhoek demping / Factor" */
    float height_damping, height_factor; /* "Hoogte demping / Factor"    */

    int empty;
} a2_car;

typedef struct {
    a2_car cars[A2_CAR_SLOTS];
} a2_cars;

/* Load <datadir>/CARS.DAT.  Returns 0 on success. */
int a2_cars_load(a2_cars *c, const char *datadir);

/* Highest non-zero gear speed, in m/s. */
float a2_car_top_speed(const a2_car *car, int upgraded);
/* Gear whose ceiling is the first above `speed` (0-based); the top gear if none. */
int a2_car_gear_for_speed(const a2_car *car, float speed, int upgraded);
/* The anim name for a slot, e.g. "RACER_1_ANIM" for slot 0. */
const char *a2_car_anim_name(int slot);

/* Half the slot's mesh extent across (X) and along (Z), in world units --
 * measured from the `.d3d` bounding boxes, since CARS.DAT carries performance
 * figures but no dimensions.  Used for collision, so the car you are driving
 * has its own size rather than the Trabant's. */
float a2_car_half_width(int slot);
float a2_car_half_length(int slot);

#endif /* A2_CARS_H */

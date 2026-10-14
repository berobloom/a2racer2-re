/* The vehicle physics model at VA 0x484B18 in spel.dat.
 *
 * 16 records of 238 bytes: `char name[14]` then 56 fields.  Unlike CARS.DAT --
 * which gives the seven player cars a per-gear *speed ceiling* and an
 * acceleration constant -- this table is a physical model in **real-world
 * units**, and covers all 16 vehicle types including the police car, the ADAC
 * van and every traffic vehicle.
 *
 * Three fields are physical constants, which is what identified the rest:
 * field 12 is 9.81 (gravity) and field 13 is 1.2 (air density), field 21 is
 * 0.02 (rolling resistance).  The named fields below then read as real figures
 * across the fleet: a 600 kg Trabant with 45 power and a 5.00 final drive
 * against a 15 000 kg bus, 11.5 x 2.5 m, Cd 0.50, redline 2200.
 *
 * See research/discoveries.md for the full field map and the evidence.
 *
 * Fields still unidentified: 1, 6, 7, 9, 11, 15-20, 26, 27, 35-39, 47-52,
 * 54, 55.  Field 10 looks like the 0-100 km/h time in seconds (Trabant 13,
 * Astra 10, BMW 7, Porsche 5, Diablo 5), which makes it a useful check on the
 * model rather than an input to it.
 */
#ifndef A2_HANDLING_H
#define A2_HANDLING_H

#define A2_HANDLING_COUNT 16
#define A2_HANDLING_GEARS 6

typedef struct {
    char name[16];     /* e.g. "RACER_1_ANIM" */
    float mass;        /* field 2,  kg */
    float length;      /* field 3,  m */
    float width;       /* field 4,  m */
    /* Fields 5 and 6 are **not** the vehicle height, as this once read: they are
     * the distances from the centre of gravity to the front and rear axles, and
     * their sum is the wheelbase.  The game itself adds them (0x40d042), they
     * differ for the traffic vehicles (1.06 and 1.46, a rearward CoG) where the
     * racers have them equal, and the sums land on the real cars almost exactly
     * -- Trabant 2.02 against a real 2.02, Porsche 2.30 against 2.27, Diablo
     * 2.66 against 2.65, and 8.00 for an 11.5 m bus.  With field 8 they are the
     * standard trio for axle load and weight transfer. */
    float cog_to_front; /* field 5,  m */
    float cog_to_rear;  /* field 6,  m */
    float wheelbase;    /* fields 5 + 6, m */
    float wheel_radius_table; /* field 22, m -- the radius the table states */
    float cd_table;     /* field 8, the real Cd -- recorded, not used */
    /* Field 9 is the frontal area, which the game reads next to the air density
     * it pairs with in the drag term: 1.80 m^2 for a car, 1.77 for the Porsche,
     * 1.70 for the Diablo and 9.00 for the bus.  This used to be derived as
     * width x height, which was within about 5% and so never looked wrong. */
    float frontal_area; /* field 9,  m^2 */
    /* Fields 15-20 are two per-axle triples, front then rear, and the rear is
     * always the stiffer of the two.  The third of each triple orders exactly
     * by how quick the car is -- Trabant 0.59/0.65, Astra 1.52/1.90, Porsche
     * 2.05/2.35, Diablo 2.95/3.50, every traffic vehicle a flat 1.05 -- which
     * is what marks it as cornering grip.  The first of each (8.43 traffic,
     * 10.7-16.0 racers) and the second (1.11 racers, 1.70 traffic) are not
     * identified. */
    float grip_front; /* field 17 -- Pacejka D, front */
    float grip_rear;  /* field 20 -- Pacejka D, rear  */
    /* Field 54, the blanket lateral-grip multiplier.  The tyre function at
     * 0x40e530 multiplies it into *both* axles' Pacejka output:
     *     F_lat = -axle_load * D * f54 * sin(C * atan(B * slip))
     * (loaded at 0x40e59d, stashed at [esp+0x14], applied to the front at
     * 0x40e5bb and the rear at 0x40e5d6).  It is 1.20 for all seven racers and
     * 1.00 for all nine traffic vehicles -- a flat 20% cornering advantage.
     * Because the force is proportional to the axle load, D * f54 *is* the
     * peak lateral acceleration in g, which is what lateral_limit() uses. */
    float grip_mult;  /* field 54 */
    /* Field 53, the brake coefficient -- peak braking in g.  The brake function
     * at 0x40d7d0 is the drive function with the gear train removed:
     *     F_brake = -axle_load * f53 * brake_input      (0x40d801, 0x40d822)
     * so f53 * gravity is the deceleration directly.  It orders monotonically
     * by how quick the car is: 0.75 Trabant, 1.05 Astra and Golf, 1.20 BMW,
     * 1.35 Porsche, 1.65 Mercedes, 2.25 Diablo, 1.00 all traffic.  CARS.DAT's
     * "Remkracht" does not order that way at all (the Astra matches the Diablo
     * and the Porsche brakes worse than the Trabant), so this is the field the
     * port brakes on. */
    float brake_coef; /* field 53 */
    /* Field 52: `f52 * 0.8` caps the drive force at that multiple of the axle
     * load (0x40db50 compares, 0x40dbdb..0x40dbec rebuilds it clamped; 0.8 is
     * the global at 0x484ab0).  1.50 for every vehicle -- a 1.20 g cap -- and
     * 4.50 for the Diablo, i.e. 3.60 g.  Recorded; the port's longitudinal
     * model is single-body rather than per-axle, so it has nothing to clamp. */
    float traction_cap; /* field 52 */
    /* Field 38: the fraction of drive force sent to the front axle.  The drive
     * function stores `f38 * F` to one output and `(1 - f38) * F` to the other
     * (0x40db4a/0x40db4d, with the `1 -` at 0x40db0e).  It is 1.00 for exactly
     * RACER_2 (Astra) and RACER_3 (Golf) -- the fleet's two front-wheel-drive
     * cars -- and 0.00 for the other fourteen, so 1 = FWD and 0 = RWD.  (The
     * real Trabant 601 is front-wheel drive too; the table says 0.00.)
     * Recorded; the port drives the whole body, not two axles. */
    float drive_front; /* field 38 */
    /* Field 55 is the **engine sound sample index** -- an *integer*, not a
     * float, which is why it reads as 0.00 through a float dump (it is read as
     * an integer at 0x410c41).  The SNDFX player 0x4286a0 indexes a table of
     * 87 nineteen-byte filenames at VA 0x48BAF0, and the six values this field
     * takes land on:
     *     31 auto1.wav     Trabant, Porsche, MPOLIZEI, ADAC, N_ASTRA, N_POLO
     *     32 auto2.wav     Astra, BMW, Mercedes, N_CALIBR
     *     33 auto3.wav     Golf, Diablo, N_GOLF
     *     34 best_bus.wav  N_BESTEL  (*bestelbus*, a delivery van)
     *     35 bus.wav       N_BUS
     *     36 vracht.wav    N_VRTAUT  (*vrachtauto*, a lorry)
     * The three large vehicles each get the sample named after them.  Recorded
     * only: the port has no audio. */
    int engine_sound; /* field 55, a sample index */
    float gravity;     /* field 12, 9.81 */
    float air_density; /* field 13, 1.2 */
    float rolling;     /* field 21, 0.02 */
    float drag_cd;     /* field 22 */
    float rpm_max;     /* field 23, redline */
    float power;       /* field 24 */
    float rpm_torque;  /* field 25, rpm at peak torque */
    float gear[A2_HANDLING_GEARS];     /* fields 28-33, -1 = absent */
    float final_drive;                 /* field 34 */
    float gear_up[A2_HANDLING_GEARS];  /* fields 40-45, the upgraded set */
    float final_drive_up;              /* field 46 */
    float zero_to_100; /* field 10, seconds */
    /* Two numbers the table does not hold, solved for at load time so that the
     * model reproduces the figures the game itself publishes:
     *   wheel_radius -- set so the redline in top gear lands on CARS.DAT's
     *                   rated top speed;
     *   torque_scale -- set so a standing start reaches 100 km/h in this
     *                   vehicle's own `zero_to_100`.
     * The table gives power, peak-torque rpm and the redline but no torque
     * *curve*, so the curve's shape is ours and these two solve it to the
     * game's endpoints. */
    float wheel_radius;
    float torque_scale;
    /* Shape of the synthesised torque curve: torque = peak * (1 - k*x^2) with
     * x the fraction of the way from the peak-torque rpm to the redline.  A
     * smaller k is a flatter curve -- less low-end, more top-end.  Solved
     * together with torque_scale so the car hits both its rated top speed and
     * the table's own 0-100 time. */
    float torque_falloff;
    /* Fields 35 and 36 are the gearbox's **shift points** (47 and 48 in the
     * upgraded block): the gear selector fcn.0040e190 compares engine rpm
     * against a downshift threshold of `(f36 - 0.05) * rpm_torque` and an
     * upshift threshold built from f35 and the redline, which is why the
     * upgraded box carries its own pair.  They are *not* a torque curve --
     * the table has none.  Kept raw here; the port shifts on its own rule. */
    float f35, f36, f47, f48;
    /* The stock rated top speed, so the curve shape can be solved against the
     * stock pairing even when an upgraded car is being calibrated. */
    float stock_top_ms;
    int valid;
} a2_handling;

typedef struct {
    a2_handling v[A2_HANDLING_COUNT];
    int count;
} a2_handling_table;

/* Read the table out of <datadir>/spel.dat.  Returns 0 on success.  The table
 * is validated (name[0] must be "RACER_1_ANIM" and gravity 9.81) so a different
 * build falls back rather than feeding nonsense into the physics. */
int a2_handling_load(a2_handling_table *t, const char *datadir);

/* Index of the entry whose name matches, or -1. */
int a2_handling_find(const a2_handling_table *t, const char *anim_name);

/* Highest gear index in use (the last ratio that is not -1). */
int a2_handling_top_gear(const a2_handling *h, int upgraded);

/* Wheel radius, in metres, derived so that the redline in top gear lands on
 * `top_speed_ms` -- CARS.DAT's own rated top speed for that car.  The table has
 * no wheel size, and calibrating it this way makes the two tables agree instead
 * of letting the physics drift away from the speeds the game advertises. */
float a2_handling_wheel_radius(const a2_handling *h, int upgraded, float top_speed_ms);

/* Solve `wheel_radius` and `torque_scale` for one vehicle against its rated top
 * speed and its tabulated 0-100 time.  Call once after loading. */
void a2_handling_calibrate(a2_handling *h, int upgraded, float top_speed_ms);

/* Simulated 0-100 km/h under the calibrated model, for comparison with the
 * table's own field 10.  Diagnostic only. */
float a2_handling_zero_to_100(const a2_handling *h, int upgraded);

/* Tractive force in newtons at `v` m/s in `gear`, plus the resisting forces.
 * `out_rpm` receives the engine speed. */
float a2_handling_drive_force(const a2_handling *h, int upgraded, int gear, float v,
                              float wheel_radius, float *out_rpm);
float a2_handling_resist_force(const a2_handling *h, float v);

/* Peak lateral acceleration, m/s^2, straight off the tyre model.  Because the
 * game's lateral force is `-axle_load * D * f54 * sin(...)` and the load is the
 * axle's share of m*g, the peak of the sine gives a limit of `D * f54 * g` --
 * no fitted constant needed.  `A2_GRIP_REFERENCE` is only the fallback for a
 * vehicle whose grip fields are missing; it is kept at the value the port used
 * to fit for the Trabant, which the derived law puts at 0.62 * 1.20 * 9.81 =
 * 7.30 -- the fit was right to within 4%. */
#define A2_GRIP_REFERENCE 7.0f
float a2_handling_lateral_limit(const a2_handling *h);

/* Peak braking deceleration, m/s^2: field 53 times gravity, both from the
 * table.  Falls back to 8.0 if the fields are missing. */
float a2_handling_brake_limit(const a2_handling *h);

/* The global at 0x484ab0, 0.8, which the game uses as the locked-wheel
 * fraction of an axle's grip.  Three places read it: the drive-force traction
 * cap (`0.8 * f52 * load`), the brake function's lock threshold, and the
 * handbrake's sliding friction (0x484ac4 and 0x484ac8 are two more copies of
 * 0.8 that 0x40e870 uses for its two components). */
#define A2_LOCK_FRACTION 0.8f

/* The upgrade shop, as its applier 0x428400 rewrites the player's row of the
 * table: `motor` multiplies field 24 (power) by 1.3 and fields 25 (peak-torque
 * rpm) and 23 (redline) by 1.1; `banden` multiplies the Pacejka B and D of both
 * axles (fields 15, 17, 18, 20) by 1.2.  The port's tyre model has no B, so of
 * `banden` only the D -- the grip peak -- has an effect.  Call after
 * a2_handling_calibrate: calibration solves the curve against the *stock*
 * figures, and the upgrade then moves the car away from them as in the game. */
void a2_handling_apply_upgrades(a2_handling *h, int motor, int banden);

/* Static share of the weight on each axle, from the CoG-to-axle distances
 * (fields 5 and 6): the front carries cog_to_rear / wheelbase.  0.5/0.5 for
 * every racer, which have the two equal. */
void a2_handling_axle_shares(const a2_handling *h, float *front, float *rear);

#endif /* A2_HANDLING_H */

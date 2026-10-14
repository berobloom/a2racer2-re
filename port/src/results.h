/* What the race hands back to the menu through Game.ini, after spel.dat.
 *
 * spel.dat and menu.dat are separate programs that the shell a2_II.exe runs in
 * turn; Game.ini is the only channel between them.  At start-up spel.dat reads
 * [PLAYER] money, damage and position (0x4228dc..0x42296a), the per-track
 * records from [BEST TIMES] (0x422220), and writes finished=0 (0x421b59), so
 * a crash or a quit reads as "not finished".  On the way out, deinit_game
 * (0x421af0) calls the saver 0x422d30, which runs three writers:
 *
 *   write_player_info (0x422ec0)  finished always; if finished, also money,
 *                                 damage, tracktime, position, DAMAGE_MESH,
 *                                 DAMAGE_VALUE
 *   racers            (0x422d40)  if finished: "<i>=car,position,time" and
 *                                 DAM<i> / VAL<i> for the three racer slots
 *   best times        (0x422c10)  if finished: LAP_RECORD_E<n>,
 *                                 RACE_RECORD_E<n>, TIME_CHECK_COUNT_E<n> and
 *                                 the time checks
 *
 * See "What spel.dat writes back to Game.ini" in research/discoveries.md.
 */
#ifndef A2_RESULTS_H
#define A2_RESULTS_H

/* A record that was never set: 0x0FFFFFFF ms (0x422268). */
#define A2_NO_RECORD 0x0FFFFFFF

typedef struct {
    int etappe;      /* Raser.ini [MAPS] row, the E<n> of the record keys */
    int money;       /* 0..999999 (0x428150) */
    int damage;      /* 0..100 (0x4281c0) */
    int position;    /* 0-based; the player starts this many chunks back */
    int lap_record;  /* ms */
    int race_record; /* ms */
    int time_checks; /* saved TIME_CHECK count, -1 until a lap is completed */
    int check_ms[10];
    int damage_mesh[8];    /* the regional damage arrays, which the shipped */
    float damage_value[8]; /* game reads and writes back but never changes */
} a2_save;

typedef struct {
    int car;     /* zero-based car index */
    int time_ms; /* finish time, extrapolated if still running */
    int position;
} a2_racer_result;

/* Read the start-of-race state.  Missing keys read as the game's defaults. */
void a2_save_load(a2_save *s, const char *datadir, int etappe);

/* Prize money for a 0-based finishing position (0x4280c0): 55000, 35000,
 * 15000, 0; four times that on etappe 6; last place on EASY gets 5000. */
int a2_save_prize(int position, int etappe, int difficulty);

/* Rank the racers as 0x4114a0 does once the player has finished: equal
 * times are split by a millisecond, and a racer's position counts the racers
 * faster than it, plus one when that is at or behind the player's. */
void a2_save_rank_racers(a2_racer_result *r, int n, int player_position);

/* Game.ini [PLAYER] finished=0, as spel.dat writes at start-up. */
void a2_save_mark_unfinished(const char *datadir);

/* Everything the saver writes; with `finished` 0 only that flag is written. */
void a2_save_write(const char *datadir, const a2_save *s, int finished, int tracktime_ms,
                   const a2_racer_result *racers, int racer_count);

#endif /* A2_RESULTS_H */

/* menu.dat's career state: SLOTS.RAS (slotio.cpp), menu.ini and the Game.ini
 * handoff with the race.
 *
 * SLOTS.RAS is 9 slots of 536 (0x218) bytes, packed little-endian; menu.ini
 * [PLAYER] save_game picks the active one and start_budget is a new slot's
 * money.  Field offsets confirmed by before/after diffs of the original
 * saving a slot (see "SLOTS.RAS" in reverse-engineering/file-formats.md);
 * the slot is kept as raw bytes so fields not yet understood round-trip.
 */
#ifndef A2_MCAREER_H
#define A2_MCAREER_H

#include <stdint.h>

#define M_SLOTS 9
#define M_SLOT_SIZE 0x218

/* Slot field offsets. */
#define M_SLOT_RECENT 0x04  /* play order: the latest chosen player has the
                             * highest (0x40b820), see m_career_touch */
#define M_SLOT_NAME 0x08    /* player name, NUL-terminated */
#define M_SLOT_CAR 0x12     /* car index 0..6 -> Game.ini [PLAYER] Car (0x4019a4) */
#define M_SLOT_ETAPPE 0x16  /* the next stage -> [etappe] etappe (0x401c9f) */
#define M_SLOT_MONEY 0x1e   /* -> [PLAYER] money (0x401a7f) */
#define M_SLOT_DEVICE 0x22  /* byte: control device (0x401be7) */
/* six ints from 0x23, written to Game.ini [PLAYER] in this order (0x401963) */
#define M_SLOT_MOTOR 0x23
#define M_SLOT_BANDEN 0x27
#define M_SLOT_DAMAGE 0x2b
#define M_SLOT_TURBO 0x2f
#define M_SLOT_ABS 0x33
#define M_SLOT_GEARBOX 0x37 /* VERSNELLINGSBAK */
#define M_SLOT_KEYS 0x3b    /* 6 ints: LEFT RIGHT UP DOWN EXTRA1 EXTRA2 (0x401ba3) */
#define M_SLOT_SENS 0x53    /* sensitivity (0x401c38) */
#define M_SLOT_STAND 0x57   /* byte: the standings tab last shown (0x40559f / 0x4057ad) */
/* +0x1a is only ever written, with 0 (0x40b2c5 new player, 0x407376 Restart) */
#define M_SLOT_TABLES 0x58  /* 8 standings tables of 4 rows of 14 bytes */

typedef struct {
    uint8_t slot[M_SLOTS][M_SLOT_SIZE];
    int active;       /* menu.ini save_game */
    int start_budget; /* menu.ini start_budget */
    int finished_career; /* 0x43e70c: the last stage was completed */
} m_career;

int m_career_load(m_career *c, const char *datadir);
int m_career_save(const m_career *c, const char *datadir);

/* A standings row: group 0 is TOTAAL, groups 1..7 the stages (etappe + 1);
 * {u8 car; char name[9]; i32 seconds} (0x43ec80 + (group * 4 + row) * 14). */
#define M_ROW_SIZE 14
uint8_t *m_slot_row(m_career *c, int group, int row);

int m_slot_int(const m_career *c, int off);
void m_slot_set_int(m_career *c, int off, int v);
const char *m_slot_name(const m_career *c);

/* A slot's first int is set while a player lives in it (the player list
 * shows only those). */
int m_career_in_use(const m_career *c, int slot);
/* 0x40b1f0 and a name: start budget, default keys, sensitivity 50, the rest
 * cleared; then marked in use. */
void m_career_new_player(m_career *c, int slot, const char *name);
void m_career_clear_slot(m_career *c, int slot);
/* The play order at +0x04: the slot with the highest value, the first on a
 * tie (0x40b710); choosing or creating a player gives it that value + 1
 * (0x40b820); deleting one moves every higher value down by one (0x40b780). */
int m_career_most_recent(const m_career *c);
void m_career_touch(m_career *c, int slot);
void m_career_forget(m_career *c, int slot);
/* The total of the active slot's stage times for `name` (0x40a6e0). */
int m_career_total_seconds(m_career *c, const char *name);

/* What the menu does on start-up after a race (0x409b87): when Game.ini says
 * finished=1, take the results into the active slot and advance the stage.
 * Returns 1 if a race result was taken. */
int m_career_take_results(m_career *c, const char *datadir, int mark_taken);

/* What 0x401cc0 writes before the menu exits with 40 to start a race:
 * [PLAYER] MOTOR BANDEN DAMAGE TURBO ABS VERSNELLINGSBAK, Car, COPLAND (1
 * when the player's name is "COPLAND", 0x4019ee), money, the [INPUT] keys
 * and sensitivity, [etappe] etappe, and menu.ini save_game.  (The options --
 * resolution, volumes, difficulty -- are the options screen's to write.) */
void m_career_write_race(const m_career *c, const char *datadir);

#endif /* A2_MCAREER_H */

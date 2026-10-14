/* The tweak menu, after spel.dat's 0x412e90 (session 14).
 *
 * With Raser.ini TWEAK_MODE=1 (the shipped setting) the player's step polls
 * Tab (0x40fe6c) and runs one screen of this menu, modally: the race stands
 * still until Space ends the screen.  The screen is picked by the state
 * [0x4a7074]: 0 the main menu (0x412c00), whose Space sets the state to the
 * chosen item; the next Tab opens that item's editor, whose Space goes back
 * to state 0.  Numpad + and - change the value one step a press; Up and Down
 * move the '>' cursor; in the top speeds Right and Left change it by 10.
 * The edits go straight into the player's car record, and "Opslaan" writes
 * CARS.DAT (10 x 124 + 10 x 24 bytes, as 0x4135b3 does).
 */
#ifndef A2_TWEAK_H
#define A2_TWEAK_H

#include "cars.h"

typedef struct {
    int open;    /* a screen is up; the race stands still */
    int state;   /* [0x4a7074], 0..12 */
    int cursor;  /* the '>' row of a list */
    int editing; /* in a list's value editor */
    int item;    /* which list item is being edited */
    char lines[16][64];
    int line_count;
} a2_tweak;

void a2_tweak_init(a2_tweak *t);
/* Tab: open the current state's screen (state 11 saves CARS.DAT and state 12
 * stops, both going back to state 0 without a screen). */
void a2_tweak_open(a2_tweak *t, const a2_cars *cars, const char *datadir);
/* An SDL key while open; edits `car` (the player's record; `gearbox` picks
 * the upgraded acceleration and speed sets, [0x4d9ed0]).  Saving writes
 * <datadir>/CARS.DAT from `cars`.  Returns 1 if it used the key. */
int a2_tweak_key(a2_tweak *t, int sdl_key, a2_cars *cars, int car_slot, int gearbox,
                 const char *datadir);
/* The text to show, one line per row at y = 10 + 20 * row of a 640x480
 * screen, x = 10 (0x466420's calls); refreshed by a2_tweak_key/open. */
void a2_tweak_text(a2_tweak *t, const a2_cars *cars, int car_slot, int gearbox);

#endif

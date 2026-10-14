/* The menu's shared state and screen framework.
 *
 * Each screen is a background, a header (anim 3), a form (menu.dat's own
 * layout table) and its own draw/action code.  Animated buttons slide in from
 * x = -100 along an 8-step quarter-sine (show_button_frame 0x40c660, a step
 * per > 33 ms after a per-button delay in ticks); the header drops in over 10
 * steps of > 20 ms (next_animated_header_frame 0x40c3b0).  See
 * reverse-engineering/menu.md.
 */
#ifndef A2_MENU_H
#define A2_MENU_H

#include "massets.h"
#include "maudio.h"
#include "mcareer.h"
#include "mdata.h"
#include "mgfx.h"

#include "../trackinfo.h"

#include <stdint.h>

enum {
    SCR_MAIN,
    SCR_GARAGE,
    SCR_SHOWROOM,
    SCR_CONTROLS,
    SCR_OPTIONS,
    SCR_STANDINGS,
    SCR_NEWPLAYER,
    SCR_COUNT
};

typedef struct menu menu;

/* A spoken-help step (play_help 0x40fcb0): the form control to show and the
 * clip to play; tables end with control -1. */
typedef struct {
    int control;
    const char *wav;
} m_help;

typedef struct {
    const char *name;   /* for --shot */
    int form;           /* M_FORM_* */
    const char *bg;     /* Mrcs.img PCX */
    int header;         /* anim 3 frame */
    int delay[M_MAX_CONTROLS]; /* slide-in delay (ticks) of the animated buttons, in form order */
    void (*enter)(menu *m);
    void (*draw)(menu *m, m_canvas *c); /* everything but the header, buttons and cursor */
    void (*action)(menu *m, int id);    /* a button with this id was clicked */
    void (*hover)(menu *m, int x, int y); /* optional: the screen's own hot areas */
    int (*click)(menu *m, int x, int y);  /* optional: returns 1 if it took the click */
    int (*key)(menu *m, int sym);          /* optional: returns 1 if it took the key */
    void (*overlay)(menu *m, m_canvas *c); /* optional: drawn over the header and buttons */
    const m_help *help;                    /* the HELP badge's tour */
} m_screen;

typedef struct {
    int step;  /* 0..8; 8 = arrived */
    int delay; /* ticks still to wait */
    uint32_t last;
} m_slide;

/* garage.ini: prices and property percents (read by 0x4014c0 / 0x401610) */
typedef struct {
    int car_price[M_CARS];
    int item_price[6][M_CARS]; /* motor, banden, reparatie per %, turbo, remmen(ABS), versnellingsbak */
    int item_perc[6][4];       /* snelheid, acceleratie, remmen, wegligging */
    int car_perc[M_CARS][4];   /* as percents of each bar's range */
} m_garage_data;

/* options.ras (m_options.c) */
typedef struct {
    int sfx_on, music_on;
    int sfx_vol, music_vol; /* 1..100 */
    int resolution;         /* 0 320x200, 1 640x480, 3 3D card */
    int difficulty;         /* 0 Makkelijk, 1 Moeilijk */
} m_options;

/* Animated text (init_anim_text 0x40c250 / next_animated_text_frame
 * 0x40c110): a character a tick (> 33 ms) with print.wav, the newest
 * |to - from| characters in a ramp of inks from `from` toward `to`. */
typedef struct {
    char text[96];
    int from, to, step;
    uint32_t last;
} m_atext;

/* The yes/no requester (janeereq.cpp): the screen behind is darkened with a
 * checkerboard, the answer goes to `done`. */
typedef struct {
    int active;
    char title[64];
    char text[256];
    void (*done)(menu *m, int yes);
    int arg;
    int hover; /* 0 JA, 1 NEE, -1 */
    int pressed;
} m_request;

struct menu {
    const char *datadir;
    const char *race_cmd;
    m_assets a;
    m_data d;
    m_career career;
    a2_trackinfo tracks[A2_MAX_TRACKS];
    int track_count;
    int device; /* Game.ini [INPUT] DEVICE, 1..4 */

    int screen; /* SCR_* */
    int next_screen; /* -1, or a screen to switch to at the end of the frame */
    uint8_t *bg;
    int focus, pressed, hover; /* form indices, -1 */
    /* the focused control each screen had when it was left, +1 (0 = the form's
     * own state 1).  The original keeps control states in the form records
     * (set_control_state 0x410ec0 writes record +0x10; draw_button 0x41b41c
     * stores 1 on ODS_FOCUS, 0 on an unfocused repaint), so a screen opens
     * again on the button that had focus; menu.dat restarts after each race */
    int kept_focus[SCR_COUNT];
    int mouse_x, mouse_y;
    m_slide slide[M_MAX_CONTROLS];
    int disabled[M_MAX_CONTROLS]; /* disable_control 0x411060: frame 2, no clicks */
    int header_step; /* 0..10 */
    uint32_t header_last;
    uint32_t now;
    uint32_t anim_last; /* 33 ms animation clock (turntable etc.) */
    int car_frame;
    int quit;
    int start_race;

    m_garage_data garage;
    uint8_t slot_backup[M_SLOT_SIZE]; /* the garage's Annuleren */
    uint8_t show_backup[M_SLOT_SIZE]; /* the showroom's Annuleren */
    int prev_screen;
    int tile_hover;                   /* garage tile under the mouse, -1 */
    int show_cand;                    /* showroom: index into the other six cars */
    /* showroom: the car slide (scroll_special_animation 0x40be30) -- step
     * 0..19, -1 when idle; the candidate it moves to and the direction */
    int show_slide, show_slide_to, show_slide_dir;
    uint32_t show_slide_last;
    int stand_group;                  /* standings: 0 TOTAAL, 1..7 stages */
    int bind_row;                     /* controls: action waiting for a key, -1 */
    int np_sel;                       /* player list: selected slot */
    int np_name_slot;                 /* name requester open for this slot, -1 */
    char np_name[16];
    int np_name_fresh;                /* the default name is still selected */
    int np_ask_name;                  /* open the list with the name requester */
    m_request req;
    m_options opt;
    int mouse_down;
    m_audio *audio;
    m_atext atext[2];
    int help_step;      /* -1, or the step of the HELP tour being played */
    uint32_t help_until;
};

extern const m_screen m_screens[SCR_COUNT];

/* framework (mui.c) */
void m_ui_enter(menu *m, int screen);
void m_ui_tick(menu *m);   /* advance animations to m->now */
void m_ui_finish(menu *m); /* jump every animation to its end (for --shot) */
void m_ui_draw(menu *m, m_canvas *c, int cursor);
void m_ui_mouse_move(menu *m, int x, int y);
void m_ui_mouse_button(menu *m, int down);
void m_ui_key(menu *m, int sym);
int m_anim_frame(const menu *m, int anim, int frame);
void m_draw_sprite(menu *m, m_canvas *c, int index, int x, int y);
/* get_centered_text: x1 + ((x2 - x1 + 1) - n * cell) / 2 */
void m_text_centred(m_canvas *c, const m_font *f, int x1, int x2, int y, const char *s);
/* the Windows system-font text of draw_control_text (stand-in) */
void m_gdi_text(menu *m, m_canvas *c, int x, int y, const char *s);
/* the same in a colour, with a black drop shadow (the property bars, ...) */
void m_gdi_text_ink(menu *m, m_canvas *c, int ink, int x, int y, const char *s);
void m_request_open(menu *m, const char *title, const char *text,
                    void (*done)(menu *m, int yes), int arg);
void m_garage_load(menu *m);
/* (re)start animated text k when its text changes; draw it */
void m_atext_set(menu *m, int k, const char *text, int from, int to);
void m_atext_draw(menu *m, m_canvas *c, int k, const m_font *f, int x, int y);
/* an effect at the options' volume (0x40a170), and the stage's music (0x407500) */
void m_sfx(menu *m, const char *name);
void m_music_update(menu *m);
/* shared by the garage and the showroom (m_garage.c) */
int m_car_value(const menu *m);
void m_draw_bars(menu *m, m_canvas *c, int x, int y, int car, int with_parts);
void m_draw_budget(menu *m, m_canvas *c, int with_damage);
void m_draw_own_car(menu *m, m_canvas *c);
/* showroom (m_showroom.c) */
void m_showroom_enter(menu *m);
void m_showroom_draw(menu *m, m_canvas *c);
void m_showroom_action(menu *m, int id);
/* player list (m_newplayer.c) */
void m_newplayer_enter(menu *m);
void m_newplayer_draw(menu *m, m_canvas *c);
void m_newplayer_overlay(menu *m, m_canvas *c);
void m_newplayer_action(menu *m, int id);
int m_newplayer_click(menu *m, int x, int y);
int m_newplayer_key(menu *m, int sym);
void m_newplayer_text(menu *m, const char *text);
/* controls (m_controls.c) */
void m_controls_enter(menu *m);
void m_controls_draw(menu *m, m_canvas *c);
void m_controls_action(menu *m, int id);
int m_controls_click(menu *m, int x, int y);
int m_controls_key(menu *m, int sym);
/* options (m_options.c) */
void m_options_load(menu *m);
int m_options_save(const menu *m);
void m_options_write_ini(const menu *m);
void m_options_draw(menu *m, m_canvas *c);
void m_options_action(menu *m, int id);
int m_options_click(menu *m, int x, int y);
void m_options_drag(menu *m, int x, int y);
/* draw_slider 0x40b880 / its drag 0x40b9c0 on a 151x30 hot area at x */
void m_slider_draw(menu *m, m_canvas *c, int x, int y, int vol);
int m_slider_value(menu *m, int x0, int x);
void m_controls_drag(menu *m, int x, int y);
/* standings (m_standings.c) */
void m_standings_enter(menu *m);
void m_standings_draw(menu *m, m_canvas *c);
void m_standings_action(menu *m, int id);
int m_standings_click(menu *m, int x, int y);
/* screen hooks (m_garage.c) */
void m_garage_enter(menu *m);
void m_garage_draw(menu *m, m_canvas *c);
void m_garage_action(menu *m, int id);
void m_garage_hover(menu *m, int x, int y);
int m_garage_click(menu *m, int x, int y);

#endif /* A2_MENU_H */

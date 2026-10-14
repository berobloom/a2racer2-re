/* The screens.  Slide-in delays are the add_animated_button arguments of each
 * screen's builder (main 0x4071be..0x4072d6: 0 2 2 4 6 8 10; controls
 * 0x40322e/0x403256; garage 0x403b3e..0x403b8e; standings 0x40551e; showroom
 * 0x40591e/0x405946; new player 0x407c9e/0x407cc6; options 0x408c2e/0x408c56). */
#include "menu.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

/* ---- main (m_main.cpp) ---------------------------------------------------- */

static void main_draw(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    /* panel titles (positions measured on the original) */
    m_text(c, std, 338, 74, "Spelerinformatie");
    m_text(c, std, 266, 286, "Volgende etappe");
    m_text(c, std, 506, 286, "Besturing");

    /* player panel: Windows text in the original (draw_control_text) */
    char buf[64];
    m_gdi_text(m, c, 206, 115, "Naam");
    m_gdi_text(m, c, 206, 146, "Budget");
    m_gdi_text(m, c, 206, 203, "Schade");
    m_gdi_text(m, c, 291, 115, m_slot_name(&m->career));
    snprintf(buf, sizeof buf, "FL %d", m_slot_int(&m->career, M_SLOT_MONEY));
    m_gdi_text(m, c, 291, 146, buf);
    snprintf(buf, sizeof buf, "%d %%", m_slot_int(&m->career, M_SLOT_DAMAGE));
    m_gdi_text(m, c, 295, 203, buf);

    /* the car on its turntable (anim per car, 0x42df54); box sprite 133 at
     * (432, 107), frames centred on the box's anchor */
    int car = m_slot_int(&m->career, M_SLOT_CAR);
    if (car < 0 || car >= M_CARS)
        car = 0;
    m_draw_sprite(m, c, m_anim_frame(m, 22, 0), 432, 107); /* box, anim 22 */
    m_sprite s;
    int fr = m_anim_frame(m, m->d.car[car].anim, m->car_frame);
    if (fr >= 0 && m_sprite_get(&m->a, fr, &s) == 0)
        m_blit(c, &s, 432 + 90 - s.ax, 107 + 71 - s.ay);

    /* damage icon, anim 20: ten steps green to red */
    int step = m_slot_int(&m->career, M_SLOT_DAMAGE) / 10;
    step = step < 0 ? 0 : step > 9 ? 9 : step;
    m_draw_sprite(m, c, m_anim_frame(m, 20, step), 349, 180);

    /* next stage: road or city picture (anim 19), type and name */
    int e = m_slot_int(&m->career, M_SLOT_ETAPPE);
    if (e >= 0 && e < m->track_count) {
        const a2_trackinfo *t = &m->tracks[e];
        m_draw_sprite(m, c, m_anim_frame(m, 19, t->circuit ? 1 : 0), 214, 340);
        /* centred in the picture's rectangle, 214 .. 214 + 226 */
        m_text_centred(c, std, 214, 440, 345, t->circuit ? "Stedenrace" : "Kwalificatie");
        m_text_centred(c, &m->a.font[M_FONT_MID], 214, 440, 365, t->name);
    }
    /* control device, anim 21, by the slot's device byte */
    m_draw_sprite(m, c, m_anim_frame(m, 21, m->career.slot[m->career.active][M_SLOT_DEVICE]), 506,
                  333);
}

static void new_player_yes(menu *m, int yes)
{
    if (yes) {
        m->np_ask_name = 1;
        m->next_screen = SCR_NEWPLAYER;
    }
}

static void quit_yes(menu *m, int yes)
{
    if (yes)
        m->quit = 1;
}

/* 0x407320, Restart: back to the first stage with empty standings; the car
 * and the budget stay */
static void restart_career(menu *m)
{
    m_slot_set_int(&m->career, M_SLOT_ETAPPE, 0);
    m_slot_set_int(&m->career, 0x1a, 0); /* only ever written */
    for (int g = 0; g < 8; g++)
        for (int r = 0; r < 4; r++)
            memset(m_slot_row(&m->career, g, r), 0, M_ROW_SIZE);
    m->career.finished_career = 0;
}

static void main_enter(menu *m)
{
    /* 0x4076ea: Ctrl + Space + M held as the main menu opens sets the budget
     * to 700000 */
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    if (k && (k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) && k[SDL_SCANCODE_SPACE] &&
        k[SDL_SCANCODE_M])
        m_slot_set_int(&m->career, M_SLOT_MONEY, 700000);
    m_music_update(m);
}

static void main_action(menu *m, int id)
{
    switch (id) {
    case 5: /* Start (0x40786e): Restart after a finished career, then the engine */
        if (m_slot_int(&m->career, M_SLOT_ETAPPE) >= 7)
            restart_career(m);
        m_sfx(m, "motor.wav");
        m->start_race = 1;
        break;
    case 6: m->next_screen = SCR_GARAGE; break;    /* Garage */
    case 7: m->next_screen = SCR_CONTROLS; break;  /* Besturing */
    case 8: m->next_screen = SCR_OPTIONS; break;   /* Opties */
    case 9: m->next_screen = SCR_STANDINGS; break; /* Standen */
    case 10: /* Nieuwe speler (strings at 0x42e4ec / 0x42e4d0) */
        m_request_open(m, "Nieuwe speler", "Weet je zeker dat je de race wilt onderbreken?",
                       new_player_yes, 0);
        break;
    case 11: /* Einde */
        m_request_open(m, "Einde", "Wil je het spel beeindigen?", quit_yes, 0);
        break;
    }
}

/* the HELP tours, {form control, clip}: main 0x42eeb0, garage 0x42b7e8,
 * showroom 0x42db68, controls 0x42a430, options 0x431190, standings
 * 0x42c468, player list 0x430228 */
static const m_help help_main[] = {{8, "help_09.wav"},  {9, "help_10.wav"}, {10, "help_11.wav"},
                                   {11, "help_12.wav"}, {0, "help_13.wav"}, {1, "help_14.wav"},
                                   {2, "help_15.wav"},  {3, "help_16.wav"}, {4, "help_17.wav"},
                                   {5, "help_18.wav"},  {6, "help_19.wav"}, {-1, NULL}};
static const m_help help_garage[] = {
    {10, "help_20.wav"}, {10, "help_21.wav"}, {11, "help_22.wav"}, {12, "help_23.wav"},
    {13, "help_24.wav"}, {14, "help_25a.wav"}, {4, "help_25b.wav"}, {5, "help_25c.wav"},
    {7, "help_25d.wav"}, {8, "help_25e.wav"}, {9, "help_25f.wav"}, {6, "help_26.wav"},
    {0, "help_30.wav"},  {1, "help_31.wav"},  {2, "help_32.wav"},  {-1, NULL}};
static const m_help help_showroom[] = {
    {6, "help_33.wav"},  {6, "help_34.wav"}, {7, "help_35.wav"}, {8, "help_36.wav"},
    {2, "help_37a.wav"}, {4, "help_37b.wav"}, {3, "help_38.wav"}, {0, "help_39.wav"},
    {1, "help_40.wav"},  {-1, NULL}};
static const m_help help_controls[] = {
    {9, "help_41.wav"},   {9, "help_42.wav"},   {10, "help_43a.wav"}, {11, "help_43b.wav"},
    {12, "help_43c.wav"}, {13, "help_43d.wav"}, {14, "help_44.wav"},  {10, "help_45a.wav"},
    {16, "help_45b.wav"}, {12, "help_46a.wav"}, {13, "help_46b.wav"}, {15, "help_47.wav"},
    {0, "help_48.wav"},   {1, "help_49.wav"},   {-1, NULL}};
static const m_help help_options[] = {
    {12, "help_50.wav"}, {12, "help_51.wav"}, {13, "help_52.wav"}, {13, "help_55.wav"},
    {14, "help_54.wav"}, {18, "help_59.wav"}, {18, "help_60.wav"}, {15, "help_56.wav"},
    {16, "help_57.wav"}, {17, "help_58.wav"}, {0, "help_61.wav"},  {1, "help_62.wav"},
    {-1, NULL}};
static const m_help help_standings[] = {{10, "help_63.wav"}, {10, "help_64.wav"},
                                        {2, "help_65.wav"},  {3, "help_66.wav"},
                                        {11, "help_67.wav"}, {0, "help_68.wav"},
                                        {-1, NULL}};
static const m_help help_newplayer[] = {{13, "help_01.wav"}, {13, "help_02.wav"},
                                        {4, "help_03a.wav"}, {0, "help_03b.wav"},
                                        {4, "help_04.wav"},  {3, "help_08.wav"},
                                        {-1, NULL}};

const m_screen m_screens[SCR_COUNT] = {
    [SCR_MAIN] = {"main", M_FORM_MAIN, "hoofd.PCX", 1, {0, 2, 2, 4, 6, 8, 10}, main_enter,
                  main_draw, main_action, NULL, NULL, NULL, NULL, help_main},
    [SCR_GARAGE] = {"garage", M_FORM_GARAGE, "garage.PCX", 2, {0, 2, 4}, m_garage_enter,
                    m_garage_draw, m_garage_action, m_garage_hover, m_garage_click, NULL, NULL,
                    help_garage},
    [SCR_SHOWROOM] = {"showroom", M_FORM_SHOWROOM, "showroom.pcx", 3, {0, 2}, m_showroom_enter,
                      m_showroom_draw, m_showroom_action, NULL, NULL, NULL, NULL, help_showroom},
    [SCR_CONTROLS] = {"controls", M_FORM_CONTROLS, "bedien.PCX", 4, {0, 2}, m_controls_enter,
                      m_controls_draw, m_controls_action, NULL, m_controls_click,
                      m_controls_key, NULL, help_controls},
    [SCR_OPTIONS] = {"options", M_FORM_OPTIONS, "opties.PCX", 5, {0, 2}, NULL, m_options_draw,
                     m_options_action, NULL, m_options_click, NULL, NULL, help_options},
    [SCR_STANDINGS] = {"standings", M_FORM_STANDINGS, "hiscore.pcx", 6, {0}, m_standings_enter,
                       m_standings_draw, m_standings_action, NULL, m_standings_click, NULL,
                       NULL, help_standings},
    [SCR_NEWPLAYER] = {"newplayer", M_FORM_NEWPLAYER, "Newgame.PCX", 0, {0, 2}, m_newplayer_enter,
                       m_newplayer_draw, m_newplayer_action, NULL, m_newplayer_click,
                       m_newplayer_key, m_newplayer_overlay, help_newplayer},
};

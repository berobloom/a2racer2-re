/* The controls screen (m_bediening.cpp).
 *
 * - Four device tiles (anims 12..15, frames available / selected / crossed)
 *   at 210,150 keyboard, 301,150 joypad, 210,241 joystick, 301,241 wheel; the
 *   choice is the slot's byte at +0x22, written to Game.ini [INPUT] DEVICE as
 *   1, 2, 3, 3 (0x401bee: joystick and wheel are both "Wheel/Joystick").
 *   Under Wine none is crossed, so the original does not detect devices.
 * - The sensitivity panel (sprite 17 red while the keyboard is chosen,
 *   measured at 193,342) and the key list: "Toetsen" (486,109), "Instellen"
 *   (480,148), the actions Links Rechts Gas Rem Turbo Handrem in ink 31 at
 *   (426, 177 + 31r) and the bound keys in ink 0x3a at x 528, named from the
 *   44-entry {VK code, name} table at 0x42a4b0 (A..Z, NUMPAD 0..9, the arrows
 *   as Omhoog / Omlaag / Rechts / Links, SPACE, ENTER, ALT, CTRL).
 * - Instellen (hot area 426,146 179x21) asks for the six keys in turn; only
 *   keys in the table are taken.  OK keeps, Annuleren restores the slot.
 */
#include "menu.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static const struct {
    int vk;
    const char *name;
} vk_names[] = {
    {65, "  A "},     {66, "  B "},     {67, "  C "},     {68, "  D "},     {69, "  E "},
    {70, "  F "},     {71, "  G "},     {72, "  H "},     {73, "  I "},     {74, "  J "},
    {75, "  K "},     {76, "  L "},     {77, "  M "},     {78, "  N "},     {79, "  O "},
    {80, "  P "},     {81, "  Q "},     {82, "  R "},     {83, "  S "},     {84, "  T "},
    {85, "  U "},     {86, "  V "},     {87, "  W "},     {88, "  X "},     {89, "  Y "},
    {90, "  Z "},     {96, "NUMPAD 0"}, {97, "NUMPAD 1"}, {98, "NUMPAD 2"}, {99, "NUMPAD 3"},
    {100, "NUMPAD 4"}, {101, "NUMPAD 5"}, {102, "NUMPAD 6"}, {103, "NUMPAD 7"},
    {104, "NUMPAD 8"}, {105, "NUMPAD 9"}, {38, "Omhoog"},   {40, "Omlaag"},
    {39, "Rechts"},   {37, "Links"},    {32, "SPACE"},    {13, "ENTER"},    {18, "ALT"},
    {17, "CTRL"},
};

static const char *vk_name(int vk)
{
    for (size_t i = 0; i < sizeof vk_names / sizeof vk_names[0]; i++)
        if (vk_names[i].vk == vk)
            return vk_names[i].name;
    return "?";
}

/* SDL key to Windows VK code, for the keys the table allows; 0 otherwise */
static int sdl_to_vk(int sym)
{
    if (sym >= SDLK_a && sym <= SDLK_z)
        return 'A' + (sym - SDLK_a);
    if (sym >= SDLK_KP_1 && sym <= SDLK_KP_9)
        return 97 + (sym - SDLK_KP_1);
    switch (sym) {
    case SDLK_KP_0: return 96;
    case SDLK_UP: return 38;
    case SDLK_DOWN: return 40;
    case SDLK_RIGHT: return 39;
    case SDLK_LEFT: return 37;
    case SDLK_SPACE: return 32;
    case SDLK_RETURN: return 13;
    case SDLK_LALT: case SDLK_RALT: return 18;
    case SDLK_LCTRL: case SDLK_RCTRL: return 17;
    }
    return 0;
}

static int device(const menu *m) { return m->career.slot[m->career.active][M_SLOT_DEVICE]; }
static int sens_active(const menu *m) { return device(m) == 2 || device(m) == 3; }

void m_controls_drag(menu *m, int x, int y)
{
    if (sens_active(m) && x >= 215 && x < 215 + 151 && y >= 391 && y < 391 + 30)
        m_slot_set_int(&m->career, M_SLOT_SENS, m_slider_value(m, 215, x));
}

void m_controls_enter(menu *m)
{
    memcpy(m->slot_backup, m->career.slot[m->career.active], M_SLOT_SIZE);
    m->bind_row = -1;
}

void m_controls_draw(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    static const int tx[4] = {210, 301, 210, 301}, ty[4] = {150, 150, 241, 241};
    for (int k = 0; k < 4; k++)
        m_draw_sprite(m, c, m_anim_frame(m, 12 + k, device(m) == k ? 1 : 0), tx[k], ty[k]);
    /* the sensitivity panel: red (anim 17 frame 1, sprite 17) for the keyboard */
    m_draw_sprite(m, c, m_anim_frame(m, 17, device(m) == 0 ? 1 : 0), 193, 342);
    /* grey (ink 100) and at (242, 351) on a capture with the keyboard; white
     * while the slider is live is the port's guess */
    m_text_ink(c, &m->a.font[M_FONT_STD], sens_active(m) ? 31 : 100, 242, 351, "Gevoeligheid");
    /* 0x40332c: the sensitivity slider on hot area 7 (215,391), drawn only
     * for a joystick or wheel (the capture with the keyboard shows the empty
     * panel; the drag 0x403840 is guarded by device 2 or 3) */
    if (sens_active(m))
        m_slider_draw(m, c, 215, 391, m_slot_int(&m->career, M_SLOT_SENS));

    m_text(c, std, 254, 109, "Besturing");
    m_text(c, std, 486, 109, "Toetsen");
    m_text(c, std, 480, 148, "Instellen");
    static const char *actions[6] = {"Links", "Rechts", "Gas", "Rem", "Turbo", "Handrem"};
    for (int r = 0; r < 6; r++) {
        int y = 177 + 31 * r;
        m_text_ink(c, std, 31, 426, y, actions[r]);
        if (m->bind_row == r)
            continue; /* waiting for this key */
        m_text_ink(c, std, 0x3a, 528, y, vk_name(m_slot_int(&m->career, M_SLOT_KEYS + 4 * r)));
    }
}

int m_controls_click(menu *m, int x, int y)
{
    static const int tx[4] = {210, 301, 210, 301}, ty[4] = {150, 150, 241, 241};
    for (int k = 0; k < 4; k++)
        if (x >= tx[k] && x < tx[k] + 80 && y >= ty[k] && y < ty[k] + 80) {
            m->career.slot[m->career.active][M_SLOT_DEVICE] = (uint8_t)k;
            return 1;
        }
    if (x >= 426 && x < 426 + 179 && y >= 146 && y < 146 + 21) {
        m->bind_row = 0;
        return 1;
    }
    if (sens_active(m) && x >= 215 && x < 215 + 151 && y >= 391 && y < 391 + 30) {
        m_controls_drag(m, x, y);
        return 0; /* the slider is silent while it moves */
    }
    return 0;
}

/* Instellen: the next key press binds the highlighted action */
int m_controls_key(menu *m, int sym)
{
    if (m->bind_row < 0)
        return 0;
    if (sym == SDLK_ESCAPE) {
        m->bind_row = -1;
        return 1;
    }
    int vk = sdl_to_vk(sym);
    if (!vk)
        return 1;
    m_slot_set_int(&m->career, M_SLOT_KEYS + 4 * m->bind_row, vk);
    if (++m->bind_row >= 6)
        m->bind_row = -1;
    return 1;
}

void m_controls_action(menu *m, int id)
{
    if (id == 10) {
        m->next_screen = SCR_MAIN;
    } else if (id == 11) {
        memcpy(m->career.slot[m->career.active], m->slot_backup, M_SLOT_SIZE);
        m->next_screen = SCR_MAIN;
    }
}

/* The player list (m_nspeler.cpp, req_nsp.cpp).
 *
 * - Nine rows (form 0x430080 areas, 218,104 + 36k, 393x27), one per
 *   SLOTS.RAS slot; plate anim 37 (selected / not).  A slot in use (its first
 *   int, set when a player is created) shows, in CARVED at y + 7: the name at
 *   x + 14, the car's small picture (anim 38 selected, 39 not) anchored at
 *   (x + 140, y + 13), the budget at x + 185, the next stage (etappe + 1, "-"
 *   when done) at x + 275 and the total time "%02d:%02d" (the TOTAAL sum,
 *   0x40a6e0) at x + 324.  Header "Naam       Auto     Geld    Etappe   Tijd"
 *   at (250, 74).
 * - Clicking a free row asks for a name (req_nsp: panel 150, "Nieuwe speler",
 *   "Vul je naam in en wordt lid van de A2 racing club", an edit box with
 *   "Speler", plates at 143,312 / 333,312) and makes a new player there
 *   (0x40b1f0: start budget, keys 37 39 38 40 17 18, sensitivity 50,
 *   everything else cleared).  Clicking a used row selects it; OK makes the
 *   selected player the active one.  Verwijderen (id 40, anim 40) asks "Weet
 *   je zeker dat je dit spel wilt verwijderen?" and clears the slot; it is
 *   disabled for a free slot and for the active player.
 * - From the main menu, Nieuwe speler first asks "Weet je zeker dat je de race
 *   wilt onderbreken?" and then opens this screen with the name requester on
 *   the first free slot.
 */
#include "menu.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define ROW_X 218
#define ROW_Y 104

static int in_use(const menu *m, int s) { return m_career_in_use(&m->career, s); }

static int first_free(const menu *m)
{
    for (int s = 0; s < M_SLOTS; s++)
        if (!in_use(m, s))
            return s;
    return 0;
}

static void open_name(menu *m, int slot)
{
    m->np_name_slot = slot;
    snprintf(m->np_name, sizeof m->np_name, "Speler");
    m->np_name_fresh = 1; /* the default is selected: typing replaces it */
    SDL_StartTextInput();
}

void m_newplayer_enter(menu *m)
{
    m->np_sel = m_career_most_recent(&m->career); /* focus 4 + slot, 0x4081e6 */
    m->np_name_slot = -1;
    if (m->np_ask_name) {
        m->np_ask_name = 0;
        m->np_sel = first_free(m);
        open_name(m, m->np_sel);
    }
}

static void update_buttons(menu *m)
{
    const m_form *f = &m->d.form[M_FORM_NEWPLAYER];
    for (int i = 0; i < f->count; i++)
        if (f->c[i].type == 1 && f->c[i].id == 40)
            m->disabled[i] = !in_use(m, m->np_sel) || m->np_sel == m->career.active;
}

void m_newplayer_draw(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD], *carved = &m->a.font[M_FONT_CARVED];
    update_buttons(m);
    m_text(c, std, 250, 74, "Naam       Auto     Geld    Etappe   Tijd");
    int keep = m->career.active;
    for (int s = 0; s < M_SLOTS; s++) {
        int x = ROW_X, y = ROW_Y + 36 * s;
        m_draw_sprite(m, c, m_anim_frame(m, 37, s == m->np_sel ? 1 : 0), x, y);
        if (!in_use(m, s))
            continue;
        m->career.active = s; /* read that slot's fields */
        char buf[32];
        m_text(c, carved, x + 14, y + 7, m_slot_name(&m->career));
        m_sprite sp;
        int car = m_slot_int(&m->career, M_SLOT_CAR);
        if (m_sprite_get(&m->a, m_anim_frame(m, s == m->np_sel ? 38 : 39, car), &sp) == 0)
            m_blit(c, &sp, x + 140 - sp.ax, y + 13 - sp.ay);
        snprintf(buf, sizeof buf, "%d", m_slot_int(&m->career, M_SLOT_MONEY));
        m_text(c, carved, x + 185, y + 7, buf);
        int e = m_slot_int(&m->career, M_SLOT_ETAPPE);
        if (e >= 7)
            snprintf(buf, sizeof buf, "-");
        else
            snprintf(buf, sizeof buf, "%d", e + 1);
        m_text(c, carved, x + 275, y + 7, buf);
        int t = m_career_total_seconds(&m->career, m_slot_name(&m->career));
        snprintf(buf, sizeof buf, "%02d:%02d", t / 60, t % 60);
        m_text(c, carved, x + 324, y + 7, buf);
    }
    m->career.active = keep;
}

/* the name requester (req_nsp.cpp), over everything */
void m_newplayer_overlay(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    if (m->np_name_slot >= 0) {
        for (int yy = 0; yy < M_H; yy++)
            for (int xx = (yy & 1) ^ 1; xx < M_W; xx += 2)
                c->px[yy * M_W + xx] = 0;
        m_sprite p;
        if (m_sprite_get(&m->a, m_anim_frame(m, 56, 0), &p) == 0)
            m_blit(c, &p, M_W / 2 - p.ax, M_H / 2 - p.ay);
        m_draw_sprite(m, c, 151, 143, 312);
        m_draw_sprite(m, c, 151, 333, 312);
        m_text_centred(c, std, 130, 510, 141, "Nieuwe speler");
        const m_font *mid = &m->a.font[M_FONT_MID];
        m_text(c, mid, 170, 170, "Vul je naam in en wordt lid van");
        m_text(c, mid, 170, 196, "de A2 racing club");
        /* the edit box: the name, a caret after it */
        int w = m_text_width(mid, m->np_name);
        if (m->np_name_fresh)
            m_rect(c, 280, 254, w ? w : 10, 26, 100);
        m_text(c, mid, 280, 255, m->np_name);
        if (!m->np_name_fresh && (m->now / 400) % 2 == 0)
            m_rect(c, 280 + w, 256, 2, 22, 31);
        const m_font *sb = &m->a.font[M_FONT_SMALLBT];
        m_text_centred(c, sb, 143, 143 + 160, 316, "OK");
        m_text_centred(c, sb, 333, 333 + 164, 316, "Annuleren");
    }
}

static void name_done(menu *m, int ok)
{
    SDL_StopTextInput();
    int slot = m->np_name_slot;
    m->np_name_slot = -1;
    if (!ok || !m->np_name[0])
        return;
    m_career_new_player(&m->career, slot, m->np_name);
    m_career_touch(&m->career, slot); /* 0x408401 */
    m->career.active = slot;
    m->np_sel = slot;
}

int m_newplayer_click(menu *m, int x, int y)
{
    if (m->np_name_slot >= 0) {
        if (y >= 312 && y < 335) {
            if (x >= 143 && x < 143 + 164)
                name_done(m, 1);
            else if (x >= 333 && x < 333 + 164)
                name_done(m, 0);
        }
        return 1;
    }
    for (int s = 0; s < M_SLOTS; s++)
        if (x >= ROW_X && x < ROW_X + 393 && y >= ROW_Y + 36 * s && y < ROW_Y + 36 * s + 27) {
            m->np_sel = s;
            if (!in_use(m, s))
                open_name(m, s);
            return 1;
        }
    return 0;
}

int m_newplayer_key(menu *m, int sym)
{
    if (m->np_name_slot < 0)
        return 0;
    size_t n = strlen(m->np_name);
    if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER)
        name_done(m, 1);
    else if (sym == SDLK_ESCAPE)
        name_done(m, 0);
    else if (sym == SDLK_BACKSPACE) {
        if (m->np_name_fresh)
            m->np_name[0] = '\0';
        else if (n)
            m->np_name[n - 1] = '\0';
        m->np_name_fresh = 0;
    }
    return 1;
}

void m_newplayer_text(menu *m, const char *text)
{
    if (m->np_name_slot < 0)
        return;
    if (m->np_name_fresh) {
        m->np_name[0] = '\0';
        m->np_name_fresh = 0;
    }
    for (const char *p = text; *p; p++) {
        size_t n = strlen(m->np_name);
        if (n >= 8 || (unsigned char)*p < 32 || (unsigned char)*p > 126)
            continue;
        m->np_name[n] = *p;
        m->np_name[n + 1] = '\0';
    }
}

static void deleted(menu *m, int yes)
{
    if (yes) { /* 0x40b7f0, then the most recent player is selected (0x40856f) */
        m_career_forget(&m->career, m->np_sel);
        m_career_clear_slot(&m->career, m->np_sel);
        m->np_sel = m_career_most_recent(&m->career);
    }
}

void m_newplayer_action(menu *m, int id)
{
    switch (id) {
    case 10: /* OK: the selected player becomes the active one */
        if (in_use(m, m->np_sel)) {
            m->career.active = m->np_sel;
            m_career_touch(&m->career, m->np_sel); /* 0x408699 */
        }
        m->next_screen = SCR_MAIN;
        break;
    case 11: m->next_screen = SCR_MAIN; break;
    case 40:
        m_request_open(m, "Verwijderen", "Weet je zeker dat je dit spel wilt verwijderen?", deleted,
                       0);
        break;
    }
}

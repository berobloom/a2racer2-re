/* The showroom (m_kopen.cpp): trade the car in for another.
 *
 * - The candidates are the six cars other than the player's, in car order,
 *   starting with the first (0x405cc0); the arrows (ids 23 / 24, keys 1 / 2)
 *   step through them with wrap-around.
 * - Top panel: the player's car as in the garage (bars with owned parts,
 *   frame 5, name, "Waarde").  Bottom panel at (186, 270): the candidate's
 *   bars (no parts), its turntable running, and "naam" / "Prijs : Fl %d" in
 *   ink 0x8f at (434, 278) / (434, 290) -- animated text in the original
 *   (init_anim_text 0x40c250), drawn finished here.
 * - The cost is the candidate's price less the player's car value; KOPEN
 *   (id 25) is disabled when the budget cannot cover it (0x4060c5).  The
 *   requester asks "... je auto voor een %s wilt inruilen?" with
 *   "Inruilkosten" or, when the trade brings money in, "Meerwaarde"; yes sets
 *   the car, takes the cost off the budget (capped at 999999) and clears the
 *   six part ints, damage included (0x40639a..0x406448).
 * - The slot is copied on entry; OK goes back to the garage, Annuleren puts
 *   the copy back first.
 */
#include "menu.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int own_car(const menu *m)
{
    int c = m_slot_int(&m->career, M_SLOT_CAR);
    return c < 0 || c >= M_CARS ? 0 : c;
}

/* the k-th of the six cars other than the player's */
static int cand_car(const menu *m, int k)
{
    int own = own_car(m);
    for (int c = 0; c < M_CARS; c++) {
        if (c == own)
            continue;
        if (k-- == 0)
            return c;
    }
    return own;
}

static int candidate(const menu *m) { return cand_car(m, m->show_cand); }

static int trade_cost(const menu *m)
{
    return m->garage.car_price[candidate(m)] - m_car_value(m);
}

static void update_kopen(menu *m)
{
    const m_form *f = &m->d.form[M_FORM_SHOWROOM];
    for (int i = 0; i < f->count; i++)
        if (f->c[i].type == 1 && f->c[i].id == 25)
            m->disabled[i] = m_slot_int(&m->career, M_SLOT_MONEY) < trade_cost(m);
}

void m_showroom_enter(menu *m)
{
    memcpy(m->show_backup, m->career.slot[m->career.active], M_SLOT_SIZE);
    m->show_cand = 0;
    m->show_slide = -1;
    update_kopen(m);
}

/* scroll_special_animation 0x40be30, on the turntable {frame, 432, 311,
 * w 181, h 110, frames, box anim 26}: 20 steps, one per > 33 ms tick.  Each
 * step clears a 2w x h strip, draws its first car centred at (w/2, h/2) and
 * its second at (w + w/2, h/2) in the turntable's current frame, copies the
 * strip's window [off, off + w) over the box with the colour key and puts the
 * box on screen.  off = table[i] going right (0x4064bb passes old, new, 1),
 * w - table[i] going left (0x406600 passes new, old, 0); table =
 * create_sine_table(20, w), the last entry w. */
#define SLIDE_X 432
#define SLIDE_Y 311
#define SLIDE_W 181
#define SLIDE_H 110

static void strip_blit(uint8_t *strip, const m_sprite *s, int cx, int cy)
{
    int x0 = cx - s->ax, y0 = cy - s->ay;
    for (int j = 0; j < s->h; j++) {
        int y = y0 + j;
        if (y < 0 || y >= SLIDE_H)
            continue;
        for (int i = 0; i < s->w; i++) {
            int x = x0 + i;
            uint8_t v = s->px[j * s->w + i];
            if (x >= 0 && x < 2 * SLIDE_W && v != M_KEY)
                strip[y * 2 * SLIDE_W + x] = v;
        }
    }
}

static void draw_slide(menu *m, m_canvas *c, int from)
{
    static uint8_t strip[SLIDE_H * 2 * SLIDE_W];
    int to = cand_car(m, m->show_slide_to), i = m->show_slide;
    int first = m->show_slide_dir ? from : to, second = m->show_slide_dir ? to : from;
    int t = i >= 19 ? SLIDE_W : (int)(sin(i * (1.57 / 20)) * SLIDE_W);
    int off = m->show_slide_dir ? t : SLIDE_W - t;
    memset(strip, M_KEY, sizeof strip);
    m_sprite s;
    if (m_sprite_get(&m->a, m_anim_frame(m, m->d.car[first].anim, m->car_frame), &s) == 0)
        strip_blit(strip, &s, SLIDE_W / 2, SLIDE_H / 2);
    if (m_sprite_get(&m->a, m_anim_frame(m, m->d.car[second].anim, m->car_frame), &s) == 0)
        strip_blit(strip, &s, SLIDE_W + SLIDE_W / 2, SLIDE_H / 2);
    for (int y = 0; y < SLIDE_H; y++)
        for (int x = 0; x < SLIDE_W; x++) {
            uint8_t v = strip[y * 2 * SLIDE_W + off + x];
            if (v != M_KEY)
                c->px[(SLIDE_Y + y) * M_W + SLIDE_X + x] = v;
        }
}

static void slide_tick(menu *m)
{
    if (m->show_slide < 0 || m->now - m->show_slide_last <= 33)
        return;
    m->show_slide_last = m->now;
    if (++m->show_slide >= 20) {
        m->show_slide = -1;
        m->show_cand = m->show_slide_to;
    }
}

void m_showroom_draw(menu *m, m_canvas *c)
{
    slide_tick(m);
    const m_font *std = &m->a.font[M_FONT_STD];
    char buf[96];
    update_kopen(m);
    m_draw_budget(m, c, 0);
    m_draw_bars(m, c, 186, 102, own_car(m), 1);
    m_draw_own_car(m, c);

    int cand = candidate(m);
    m_draw_bars(m, c, 186, 270, cand, 0);
    /* turntable (init_anim_data 0x405e71): box anim 26 (sprite 266, 181x110) at
     * (432, 311), frames centred on its middle (522, 366) by their anchor --
     * measured: frame 221 at (494, 324), anchor (28, 42) */
    m_draw_sprite(m, c, m_anim_frame(m, 26, 0), 432, 311);
    if (m->show_slide >= 0) {
        draw_slide(m, c, cand); /* cand is still the old car */
    } else {
        m_sprite s;
        if (m_sprite_get(&m->a, m_anim_frame(m, m->d.car[cand].anim, m->car_frame), &s) == 0)
            m_blit(c, &s, 432 + 90 - s.ax, 311 + 55 - s.ay);
    }
    /* animated text (0x4059c0: init_anim_text with inks 0x8f -> 0x85) */
    m_atext_set(m, 0, m->d.car[cand].name, 0x8f, 0x85);
    snprintf(buf, sizeof buf, "Prijs : Fl %d", m->garage.car_price[cand]);
    m_atext_set(m, 1, buf, 0x8f, 0x85);
    m_atext_draw(m, c, 0, std, 0x1b2, 0x116);
    m_atext_draw(m, c, 1, std, 0x1b2, 0x122);
}

static void traded(menu *m, int yes)
{
    if (!yes)
        return;
    int cost = trade_cost(m);
    int money = m_slot_int(&m->career, M_SLOT_MONEY) - cost;
    m_slot_set_int(&m->career, M_SLOT_CAR, candidate(m));
    m_slot_set_int(&m->career, M_SLOT_MONEY, money > 999999 ? 999999 : money);
    for (int i = 0; i < 6; i++)
        m_slot_set_int(&m->career, M_SLOT_MOTOR + 4 * i, 0);
    m->show_cand = 0;
}

void m_showroom_action(menu *m, int id)
{
    switch (id) {
    case 23: /* previous: the new car comes in from the left (0x406600) */
    case 24: /* next: from the right (0x4064bb) */
        m->show_slide_to = (m->show_cand + (id == 24 ? 1 : 5)) % 6;
        m->show_slide_dir = id == 24;
        m->show_slide = 0;
        m->show_slide_last = m->now;
        break;
    case 25: {                                             /* KOPEN */
        int cost = trade_cost(m), money = m_slot_int(&m->career, M_SLOT_MONEY);
        if (money < cost)
            break;
        char text[256];
        snprintf(text, sizeof text,
                 cost > 0 ? "Weet je zeker dat je je auto voor een %s wilt inruilen? \n"
                            "Inruilkosten: Fl %d\n(Budget: Fl %d)"
                          : "Weet je zeker dat je je auto voor een %s wilt inruilen? \n"
                            "Meerwaarde: Fl %d\n(Budget: Fl %d)",
                 m->d.car[candidate(m)].name, cost > 0 ? cost : -cost, money);
        m_request_open(m, "KOPEN", text, traded, 0);
        break;
    }
    case 10: m->next_screen = SCR_GARAGE; break; /* OK */
    case 11:                                     /* Annuleren */
        memcpy(m->career.slot[m->career.active], m->show_backup, M_SLOT_SIZE);
        m->next_screen = SCR_GARAGE;
        break;
    }
}

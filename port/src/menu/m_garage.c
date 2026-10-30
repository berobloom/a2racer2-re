/* The garage (m_garage.cpp): upgrades, repair, and the car's properties.
 *
 * - Tiles (table 0x42b8b8, 120-byte records {anim, x, y, label[...], ...}):
 *   MOTOR, BANDEN, REPAREREN, TURBO, ABS, 6 VERSNELLINGEN, anims 47..52 with
 *   frames {normal, lit, crossed, bought}.  0x403da0: an owned part shows
 *   "bought" and no price; one the budget cannot cover is crossed; the one
 *   under the mouse is lit.  Repair costs garage.ini's per-percent rate times
 *   the damage and is crossed (no price) at 0 damage.
 * - Buying (0x4046f0): repair needs damage > 0, the rest must not be owned;
 *   the budget must cover it; a yes/no requester confirms ("Weet je zeker dat
 *   je ...", with the budget), then the part's sound plays, the flag is set
 *   (or damage cleared) and the price comes off the budget.
 * - On entry the slot is copied; Annuleren puts the copy back (0x404d99).
 * - Car value (0x405ac0): price / 2 - repair rate * damage + half the price of
 *   every owned part ("Waarde : Fl %d").
 * - Properties (0x40cef0, show_car_property_bar 0x40c960): the car's four
 *   values plus each owned part's, as percents (1..100) of each bar's range;
 *   the number shown is min + (max - min) * pct / 100, speed 100..300 km/h,
 *   the rest 0..100 %.
 */
#include "menu.h"

#include <stdio.h>
#include <string.h>

static const struct {
    int anim, x, y;
    const char *label;
} tiles[6] = {
    {47, 186, 270, "MOTOR"}, {48, 332, 270, "BANDEN"}, {49, 478, 270, "REPAREREN"},
    {50, 186, 369, "TURBO"}, {51, 332, 369, "ABS"},    {52, 478, 369, "6 VERSNELLINGEN"},
};

/* the slot's six part ints (MOTOR BANDEN DAMAGE TURBO ABS VERSNELLINGSBAK) */
static int part(const menu *m, int i) { return m_slot_int(&m->career, M_SLOT_MOTOR + 4 * i); }

static int car_index(const menu *m)
{
    int c = m_slot_int(&m->career, M_SLOT_CAR);
    return c < 0 || c >= M_CARS ? 0 : c;
}

static int price(const menu *m, int i)
{
    int p = m->garage.item_price[i][car_index(m)];
    return i == 2 ? p * part(m, 2) : p;
}

/* bar ranges: speed 100..300 km/h, the others 0..100 % */
static const float bar_min[4] = {100.0f, 0.0f, 0.0f, 0.0f};
static const float bar_max[4] = {300.0f, 100.0f, 100.0f, 100.0f};

void m_garage_load(menu *m)
{
    m_garage_data *g = &m->garage;
    const char *dir = m->datadir;
    char key[32];
    for (int c = 0; c < M_CARS; c++) {
        snprintf(key, sizeof key, "car%d", c + 1);
        g->car_price[c] = a2_ini_int(dir, "garage.ini", "[prices]", key, 0);
        for (int i = 0; i < 6; i++) {
            snprintf(key, sizeof key, "item%d_car%d", i + 1, c + 1);
            g->item_price[i][c] = a2_ini_int(dir, "garage.ini", "[prices]", key, 0);
        }
        /* 0x401787: (value - min) / (max - min) as a percent */
        for (int k = 0; k < 4; k++) {
            snprintf(key, sizeof key, "car%d_perc%d", c + 1, k + 1);
            float v = (float)a2_ini_int(dir, "garage.ini", "[percents]", key, 0);
            int pct = (int)((v - bar_min[k]) * 100.0f / (bar_max[k] - bar_min[k]));
            g->car_perc[c][k] = pct < 1 ? 1 : pct > 100 ? 100 : pct;
        }
    }
    for (int i = 0; i < 6; i++)
        for (int k = 0; k < 4; k++) {
            snprintf(key, sizeof key, "item%d_perc%d", i + 1, k + 1);
            g->item_perc[i][k] = a2_ini_int(dir, "garage.ini", "[percents]", key, 0);
        }
}

static int tile_state(const menu *m, int i)
{
    int money = m_slot_int(&m->career, M_SLOT_MONEY);
    if (i == 2) {
        if (part(m, 2) == 0)
            return 3;
    } else if (part(m, i)) {
        return 3;
    }
    if (money < price(m, i))
        return 2;
    return m->tile_hover == i ? 1 : 0;
}

int m_car_value(const menu *m)
{
    int c = car_index(m);
    int v = m->garage.car_price[c] / 2;
    for (int i = 0; i < 6; i++) {
        if (i == 2)
            v -= m->garage.item_price[2][c] * part(m, 2);
        else if (part(m, i))
            v += m->garage.item_price[i][c] / 2;
    }
    return v;
}

/* The four property bars of a car at panel (x, y); with_parts adds the
 * slot's owned parts (the player's car), as 0x40cef0 does. */
void m_draw_bars(menu *m, m_canvas *c, int x, int y, int car, int with_parts)
{
    static const char *label[4] = {"Snelheid", "Acceleratie", "Remmen", "Wegligging"};
    int pct[4];
    for (int k = 0; k < 4; k++)
        pct[k] = m->garage.car_perc[car][k];
    for (int i = 0; i < 6; i++) /* 0x40cf4a: owned parts add their percents */
        if (with_parts && i != 2 && part(m, i))
            for (int k = 0; k < 4; k++)
                if (m->garage.item_perc[i][k]) {
                    pct[k] += m->garage.item_perc[i][k];
                    pct[k] = pct[k] < 1 ? 1 : pct[k] > 100 ? 100 : pct[k];
                }
    m_sprite bar, rip;
    m_sprite_get(&m->a, m_anim_frame(m, 53, 0), &bar);
    int have_rip = m_sprite_get(&m->a, m_anim_frame(m, 54, m->car_frame), &rip) == 0;
    for (int k = 0; k < 4; k++) {
        int by = y + 0x18 + 35 * k, bx = x + 0xb;
        int fill = bar.w * pct[k] / 100;
        fill = fill < 1 ? 1 : fill > bar.w ? bar.w : fill;
        m_blit_part(c, &bar, 0, 0, fill, bar.h, bx, by);
        if (have_rip)
            m_blit_part(c, &rip, 0, 0, fill, rip.h, bx, by);
        /* labels and values in STDFONT, placed on a capture of the original */
        m_text_ink(c, &m->a.font[M_FONT_STD], 47, bx + 5, by - 19, label[k]);
        char v[16];
        float shown = bar_min[k] + (bar_max[k] - bar_min[k]) * (float)pct[k] * 0.01f;
        snprintf(v, sizeof v, "%d", (int)shown);
        m_text_ink(c, &m->a.font[M_FONT_STD], 31, bx + 156 - 8 * (int)strlen(v), by - 2, v);
        m_text_ink(c, &m->a.font[M_FONT_STD], 31, bx + 168, by - 2, k == 0 ? "km/h" : "%");
    }
}

/* BUDGET (and SCHADE) row, 0x403fc0 / 0x405cc0: labels ink 0x1f, values 0x3a */
void m_draw_budget(menu *m, m_canvas *c, int with_damage)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    char buf[32];
    m_text_ink(c, std, 0x1f, 0xee, 0x4c, "BUDGET");
    snprintf(buf, sizeof buf, "Fl %d", m_slot_int(&m->career, M_SLOT_MONEY));
    m_text_ink(c, std, 0x3a, 301 + (101 - m_text_width(std, buf)) / 2, 75, buf);
    if (!with_damage)
        return;
    m_text_ink(c, std, 0x1f, 0x1ae, 0x4c, "SCHADE");
    snprintf(buf, sizeof buf, "%d %%", part(m, 2));
    m_text_ink(c, std, 0x3a, 498 + (101 - m_text_width(std, buf)) / 2, 75, buf);
}

/* The player's car panel (0x405c20): frame 5 of its turntable anim centred on
 * (522, 199), name and value in ink 0x8f. */
void m_draw_own_car(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    char buf[64];
    int car = car_index(m);
    m_sprite s;
    if (m_sprite_get(&m->a, m_anim_frame(m, m->d.car[car].anim, 5), &s) == 0)
        m_blit(c, &s, 522 - s.ax, 199 - s.ay);
    m_text_ink(c, std, 0x8f, 0x1b5, 0x6d, m->d.car[car].name);
    snprintf(buf, sizeof buf, "Waarde : Fl %d", m_car_value(m));
    m_text_ink(c, std, 0x8f, 0x1b5, 0x79, buf);
}

void m_garage_draw(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    char buf[96];
    m_draw_budget(m, c, 1);
    m_draw_bars(m, c, 186, 102, car_index(m), 1);
    m_draw_own_car(m, c);

    /* the six tiles (0x403ba0) */
    for (int i = 0; i < 6; i++) {
        int st = tile_state(m, i);
        int fr = m_anim_frame(m, tiles[i].anim, st);
        m_draw_sprite(m, c, fr, tiles[i].x, tiles[i].y);
        m_text_centred(c, std, tiles[i].x, tiles[i].x + 0x85, tiles[i].y + 3, tiles[i].label);
        if (st != 3) {
            snprintf(buf, sizeof buf, "Fl %d", price(m, i));
            int w = m_text_width(std, buf);
            m_text_ink(c, std, 0x3a, tiles[i].x + (0x86 - w) / 2, tiles[i].y + 94 - 20, buf);
        }
    }
}

static int tile_at(int x, int y)
{
    for (int i = 0; i < 6; i++)
        if (x >= tiles[i].x && x < tiles[i].x + 133 && y >= tiles[i].y && y < tiles[i].y + 109)
            return i;
    return -1;
}

void m_garage_hover(menu *m, int x, int y)
{
    int t = tile_at(x, y), was = m->tile_hover;
    m->tile_hover = t;
    if (t >= 0 && t != was && tile_state(m, t) == 1)
        m_sfx(m, "glow.wav"); /* 0x403c25: a tile lighting up */
}

static void bought(menu *m, int yes)
{
    if (!yes)
        return;
    int i = m->req.arg;
    int cost = price(m, i);
    /* 0x403a90: the part's sound; the gearbox's "versne~1.wav" is an 8.3
     * name Mrcs.img does not have, so the original plays nothing for it */
    static const char *snd[6] = {"motor.wav", "banden.wav", "repair.wav", "banden.wav",
                                 "antiblok.wav", "versne~1.wav"};
    m_sfx(m, snd[i]);
    if (i == 2)
        m_slot_set_int(&m->career, M_SLOT_DAMAGE, 0);
    else
        m_slot_set_int(&m->career, M_SLOT_MOTOR + 4 * i, 1);
    m_slot_set_int(&m->career, M_SLOT_MONEY, m_slot_int(&m->career, M_SLOT_MONEY) - cost);
}

int m_garage_click(menu *m, int x, int y)
{
    int i = tile_at(x, y);
    if (i < 0)
        return 0;
    int money = m_slot_int(&m->career, M_SLOT_MONEY), cost = price(m, i);
    char text[256];
    if (i == 2) {
        if (part(m, 2) == 0 || money < cost)
            return 1;
        snprintf(text, sizeof text,
                 "Weet je zeker dat je je auto voor Fl %d wilt laten repareren?\n\n(Budget: Fl %d)",
                 cost, money);
        m_request_open(m, "REPAREREN", text, bought, i);
    } else {
        if (part(m, i) || money < cost)
            return 1;
        snprintf(text, sizeof text,
                 "Weet je zeker dat je dit onderdeel voor Fl %d wilt kopen?\n\n(Budget: Fl %d)",
                 cost, money);
        m_request_open(m, tiles[i].label, text, bought, i);
    }
    return 1;
}

void m_garage_enter(menu *m)
{
    /* the showroom runs inside the garage's loop in the original (0x40484a),
     * so coming back from it keeps the garage's copy */
    if (m->prev_screen != SCR_SHOWROOM)
        memcpy(m->slot_backup, m->career.slot[m->career.active], M_SLOT_SIZE);
    m->tile_hover = -1;
}

void m_garage_action(menu *m, int id)
{
    switch (id) {
    case 7: m->next_screen = SCR_SHOWROOM; break; /* Showroom */
    case 8: m->next_screen = SCR_MAIN; break;     /* OK */
    case 9:                                       /* Annuleren: undo */
        memcpy(m->career.slot[m->career.active], m->slot_backup, M_SLOT_SIZE);
        m->next_screen = SCR_MAIN;
        break;
    }
}

/* The standings (m_highscore.cpp).
 *
 * - Eight plates down the left at (177, 59 + 45k), 108x50: TOTAAL and the
 *   seven stages, names baked in; the shown one from anim 59, the others
 *   from anim 58 (frame k each).  Clicking one shows that table.
 * - The table (0x404f80): drawn only once a stage has been driven and only
 *   for groups up to the stages driven; four rows at y = 108 + 36r: the name
 *   at x 325 and "%02d:%02d" at x 540, STDFONT ink 0x3a, and the car's small
 *   picture (anim 57, by car) anchored at (460, 118 + 36r).
 * - Header "    Naam         Auto        Tijd" at (306, 76); picture below the
 *   table, anim 0 frame by group (the flag for TOTAAL, the stage maps), at
 *   (304, 258).  All measured on a capture and matching the code's numbers.
 */
#include "menu.h"

#include <stdio.h>
#include <string.h>

/* the slot remembers the tab last shown (0x40559f) */
void m_standings_enter(menu *m)
{
    m->stand_group = m->career.slot[m->career.active][M_SLOT_STAND];
    if (m->stand_group > 7)
        m->stand_group = 0;
}

/* The plate labels are Windows text in the original (black, engraved with a
 * 116 highlight): "TOTAAL" and the Raser.ini stage names, "A - B" split over
 * two lines.  Stand-in: STDFONT black over a 116 copy at (+1, +1). */
static void plate_label(menu *m, m_canvas *c, int k)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    char l1[48] = "TOTAAL", l2[48] = "";
    if (k > 0 && k - 1 < m->track_count) {
        const char *n = m->tracks[k - 1].name, *dash = strstr(n, " - ");
        if (dash) {
            snprintf(l1, sizeof l1, "%.*s", (int)(dash - n), n);
            snprintf(l2, sizeof l2, "%s", dash + 1);
        } else {
            snprintf(l1, sizeof l1, "%s", n);
        }
    }
    int x = 177, y = 59 + 45 * k, lines = l2[0] ? 2 : 1;
    int ty = y + (50 - lines * 15) / 2;
    for (int i = 0; i < lines; i++) {
        const char *t = i ? l2 : l1;
        int tx = x + (108 - m_text_width(std, t)) / 2;
        m_text_ink(c, std, 116, tx + 1, ty + 15 * i + 1, t);
        m_text_ink(c, std, 0, tx, ty + 15 * i, t);
    }
}

void m_standings_draw(menu *m, m_canvas *c)
{
    const m_font *std = &m->a.font[M_FONT_STD];
    for (int k = 0; k < 8; k++) {
        m_draw_sprite(m, c, m_anim_frame(m, k == m->stand_group ? 59 : 58, k), 177, 59 + 45 * k);
        plate_label(m, c, k);
    }
    m_text(c, std, 306, 76, "    Naam         Auto        Tijd");
    m_draw_sprite(m, c, m_anim_frame(m, 0, m->stand_group), 304, 258);

    int stages = m_slot_int(&m->career, M_SLOT_ETAPPE);
    if (stages <= 0 || m->stand_group > stages)
        return;
    for (int r = 0; r < 4; r++) {
        const uint8_t *row = m_slot_row(&m->career, m->stand_group, r);
        char name[10], t[16];
        snprintf(name, sizeof name, "%.9s", (const char *)row + 1);
        int secs = (int)(row[10] | row[11] << 8 | row[12] << 16 | (uint32_t)row[13] << 24);
        snprintf(t, sizeof t, "%02d:%02d", secs / 60, secs % 60);
        int y = 108 + 36 * r;
        m_text_ink(c, std, 0x3a, 0x145, y, name);
        m_text_ink(c, std, 0x3a, 0x21c, y, t);
        m_sprite s;
        if (m_sprite_get(&m->a, m_anim_frame(m, 57, row[0]), &s) == 0)
            m_blit(c, &s, 460 - s.ax, 118 + 36 * r - s.ay);
    }
}

int m_standings_click(menu *m, int x, int y)
{
    for (int k = 0; k < 8; k++)
        if (x >= 177 && x < 177 + 108 && y >= 59 + 45 * k && y < 59 + 45 * k + 50) {
            m->stand_group = k;
            m->career.slot[m->career.active][M_SLOT_STAND] = (uint8_t)k; /* 0x4057ad */
            return 1;
        }
    return 0;
}

void m_standings_action(menu *m, int id)
{
    if (id == 11) /* OK */
        m->next_screen = SCR_MAIN;
}

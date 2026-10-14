#include "mcareer.h"

#include "../a2_io.h"
#include "../trackinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int m_slot_int(const m_career *c, int off)
{
    const uint8_t *b = c->slot[c->active] + off;
    return (int)(b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24);
}

void m_slot_set_int(m_career *c, int off, int v)
{
    uint8_t *b = c->slot[c->active] + off;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

const char *m_slot_name(const m_career *c) { return (const char *)c->slot[c->active] + M_SLOT_NAME; }

uint8_t *m_slot_row(m_career *c, int group, int row)
{
    return c->slot[c->active] + M_SLOT_TABLES + (group * 4 + row) * M_ROW_SIZE;
}

static int32_t row_time(const uint8_t *r)
{
    return (int32_t)(r[10] | r[11] << 8 | r[12] << 16 | (uint32_t)r[13] << 24);
}

static void set_row(uint8_t *r, int car, const char *name, int32_t seconds)
{
    r[0] = (uint8_t)car;
    memset(r + 1, 0, 9);
    for (int i = 0; i < 8 && name[i]; i++)
        r[1 + i] = (uint8_t)name[i];
    r[10] = (uint8_t)seconds;
    r[11] = (uint8_t)(seconds >> 8);
    r[12] = (uint8_t)(seconds >> 16);
    r[13] = (uint8_t)(seconds >> 24);
}

/* 0x40aeb0: TOTAAL (group 0) from the stage groups 1..etappe.  The drivers are
 * "racer 1".."racer 3" and the player; a driver's total is the sum of the
 * rows carrying their name (0x40a6e0), the car that of their row in the last
 * stage; the times start at 0x0FFFFFFF and each driver is inserted in order
 * of total (0x40a970). */
static void compute_totals(m_career *c)
{
    int stages = m_slot_int(c, M_SLOT_ETAPPE);
    char names[4][10];
    for (int k = 0; k < 3; k++)
        snprintf(names[k], sizeof names[k], "racer %d", k + 1);
    snprintf(names[3], sizeof names[3], "%.8s", m_slot_name(c));
    for (int r = 0; r < 4; r++)
        set_row(m_slot_row(c, 0, r), 0, "", 0x0FFFFFFF);
    for (int d = 0; d < 4; d++) {
        int32_t total = 0;
        int car = 0;
        for (int g = 1; g <= stages && g < 8; g++)
            for (int r = 0; r < 4; r++) {
                const uint8_t *row = m_slot_row(c, g, r);
                if (strncmp((const char *)row + 1, names[d], 9) == 0) {
                    total += row_time(row);
                    if (g == stages)
                        car = row[0];
                }
            }
        int at = 0;
        while (at < 4 && row_time(m_slot_row(c, 0, at)) <= total)
            at++;
        if (at == 4)
            continue;
        for (int r = 3; r > at; r--)
            memcpy(m_slot_row(c, 0, r), m_slot_row(c, 0, r - 1), M_ROW_SIZE);
        set_row(m_slot_row(c, 0, at), car, names[d], total);
    }
}

int m_career_load(m_career *c, const char *datadir)
{
    memset(c, 0, sizeof *c);
    c->active = a2_ini_int(datadir, "menu.ini", "[PLAYER]", "save_game", 0);
    if (c->active < 0 || c->active >= M_SLOTS)
        c->active = 0;
    c->start_budget = a2_ini_int(datadir, "menu.ini", "[PLAYER]", "start_budget", 35000);
    char path[1024];
    snprintf(path, sizeof path, "%s/SLOTS.RAS", datadir);
    size_t size = 0;
    uint8_t *d = a2_read_file(path, &size);
    if (d && size == sizeof c->slot) {
        memcpy(c->slot, d, size);
    } else {
        /* load_slot: "File %s does not exist" -- start every slot fresh, with
         * the start budget and the default keys (37 39 38 40 17 18, 50) */
        static const int keys[7] = {37, 39, 38, 40, 17, 18, 50};
        int keep = c->active;
        for (int s = 0; s < M_SLOTS; s++) {
            c->active = s;
            m_slot_set_int(c, M_SLOT_MONEY, c->start_budget);
            for (int k = 0; k < 7; k++)
                m_slot_set_int(c, M_SLOT_KEYS + 4 * k, keys[k]);
        }
        c->active = keep;
    }
    free(d);
    return 0;
}

int m_career_save(const m_career *c, const char *datadir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/SLOTS.RAS", datadir);
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    size_t n = fwrite(c->slot, 1, sizeof c->slot, f);
    return (fclose(f) == 0 && n == sizeof c->slot) ? 0 : -1;
}

int m_career_take_results(m_career *c, const char *datadir, int mark_taken)
{
    if (!a2_ini_int(datadir, "Game.ini", "[PLAYER]", "finished", 0))
        return 0;
    /* 0x401ff0: money (clamped 0..999999) and damage come back from the race */
    int money = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "money", 0);
    money = money < 0 ? 0 : money > 999999 ? 999999 : money;
    m_slot_set_int(c, M_SLOT_MONEY, money);
    m_slot_set_int(c, M_SLOT_DAMAGE, a2_ini_int(datadir, "Game.ini", "[PLAYER]", "damage", 0));
    /* 0x409870: any finished race advances the career; after the last stage
     * (6) the career is complete and the win/lose video decides the ending */
    int e = m_slot_int(c, M_SLOT_ETAPPE);
    /* 0x401d30 / 0x401ff0: the stage's standings, group etappe + 1, by
     * finishing position: the three racers ("racer N", their car, their time)
     * and the player; times in whole seconds */
    if (e >= 0 && e < 7) {
        char key[8], buf[64];
        for (int i = 0; i < 3; i++) {
            snprintf(key, sizeof key, "%d", i);
            int car = 0, pos = 0, ms = 0;
            if (a2_ini_string(datadir, "Game.ini", "[RACERS]", key, buf, sizeof buf) &&
                sscanf(buf, "%d,%d,%d", &car, &pos, &ms) == 3 && pos >= 0 && pos < 4) {
                char name[10];
                snprintf(name, sizeof name, "racer %d", i + 1);
                set_row(m_slot_row(c, e + 1, pos), car, name, ms / 1000);
            }
        }
        int pos = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "position", 0);
        int ms = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "tracktime", 0);
        if (pos >= 0 && pos < 4)
            set_row(m_slot_row(c, e + 1, pos), m_slot_int(c, M_SLOT_CAR), m_slot_name(c), ms / 1000);
    }
    c->finished_career = e >= 6;
    m_slot_set_int(c, M_SLOT_ETAPPE, e + 1);
    compute_totals(c);
    /* Port choice: menu.dat leaves finished=1 in place (only spel.dat resets
     * it, and the shell always runs a race in between); a2menu can be
     * started twice in a row, so it marks the result as taken. */
    if (mark_taken)
        a2_ini_write(datadir, "Game.ini", "[PLAYER]", "finished", "0");
    return 1;
}

static void put(const char *dir, const char *file, const char *sec, const char *key, int v)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", v);
    a2_ini_write(dir, file, sec, key, buf);
}

void m_career_write_race(const m_career *c, const char *datadir)
{
    static const char *keys6[6] = {"MOTOR", "BANDEN", "DAMAGE", "TURBO", "ABS", "VERSNELLINGSBAK"};
    for (int i = 0; i < 6; i++)
        put(datadir, "Game.ini", "[PLAYER]", keys6[i], m_slot_int(c, M_SLOT_MOTOR + 4 * i));
    put(datadir, "Game.ini", "[PLAYER]", "Car", m_slot_int(c, M_SLOT_CAR));
    char up[32];
    size_t n = 0;
    for (const char *p = m_slot_name(c); *p && n < sizeof up - 1; p++)
        up[n++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    up[n] = '\0';
    put(datadir, "Game.ini", "[PLAYER]", "COPLAND", strcmp(up, "COPLAND") == 0);
    static const char *keys[6] = {"LEFT", "RIGHT", "UP", "DOWN", "EXTRA1", "EXTRA2"};
    for (int i = 0; i < 6; i++)
        put(datadir, "Game.ini", "[INPUT]", keys[i], m_slot_int(c, M_SLOT_KEYS + 4 * i));
    put(datadir, "Game.ini", "[INPUT]", "SENSITIVITY", m_slot_int(c, M_SLOT_SENS));
    /* 0x401bee: device byte 0..3 -> DEVICE 1, 2, 3, 3 */
    static const int dev[4] = {1, 2, 3, 3};
    int d = c->slot[c->active][M_SLOT_DEVICE];
    if (d >= 0 && d < 4)
        put(datadir, "Game.ini", "[INPUT]", "Device", dev[d]);
    put(datadir, "Game.ini", "[PLAYER]", "money", m_slot_int(c, M_SLOT_MONEY));
    put(datadir, "Game.ini", "[etappe]", "etappe", m_slot_int(c, M_SLOT_ETAPPE));
    put(datadir, "menu.ini", "[PLAYER]", "save_game", c->active);
}

int m_career_in_use(const m_career *c, int slot)
{
    const uint8_t *b = c->slot[slot];
    return (b[0] | b[1] | b[2] | b[3]) != 0;
}

void m_career_clear_slot(m_career *c, int slot)
{
    static const int keys[7] = {37, 39, 38, 40, 17, 18, 50};
    int keep = c->active;
    memset(c->slot[slot], 0, M_SLOT_SIZE);
    c->active = slot;
    m_slot_set_int(c, M_SLOT_MONEY, c->start_budget);
    for (int k = 0; k < 7; k++)
        m_slot_set_int(c, M_SLOT_KEYS + 4 * k, keys[k]);
    c->active = keep;
}

void m_career_new_player(m_career *c, int slot, const char *name)
{
    m_career_clear_slot(c, slot);
    int keep = c->active;
    c->active = slot;
    m_slot_set_int(c, 0, 1);
    for (int i = 0; i < 8 && name[i]; i++)
        c->slot[slot][M_SLOT_NAME + i] = (uint8_t)name[i];
    c->active = keep;
}

int m_career_total_seconds(m_career *c, const char *name)
{
    int stages = m_slot_int(c, M_SLOT_ETAPPE), total = 0;
    for (int g = 1; g <= stages && g < 8; g++)
        for (int r = 0; r < 4; r++) {
            const uint8_t *row = m_slot_row(c, g, r);
            if (strncmp((const char *)row + 1, name, 9) == 0)
                total += row_time(row);
        }
    return total;
}

static int recent(const m_career *c, int slot)
{
    const uint8_t *b = c->slot[slot] + M_SLOT_RECENT;
    return (int)(b[0] | b[1] << 8 | b[2] << 16 | (uint32_t)b[3] << 24);
}

static void set_recent(m_career *c, int slot, int v)
{
    uint8_t *b = c->slot[slot] + M_SLOT_RECENT;
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

int m_career_most_recent(const m_career *c)
{
    int best = 0, max = -1;
    for (int s = 0; s < M_SLOTS; s++)
        if (recent(c, s) > max) {
            max = recent(c, s);
            best = s;
        }
    return best;
}

void m_career_touch(m_career *c, int slot) { set_recent(c, slot, recent(c, m_career_most_recent(c)) + 1); }

void m_career_forget(m_career *c, int slot)
{
    int v = recent(c, slot);
    for (int s = 0; s < M_SLOTS; s++)
        if (recent(c, s) > v)
            set_recent(c, s, recent(c, s) - 1);
}

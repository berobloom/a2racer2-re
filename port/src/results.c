#include "results.h"

#include "trackinfo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static int key_int(const char *datadir, const char *section, const char *fmt, int a, int b,
                   int fallback)
{
    char key[64];
    snprintf(key, sizeof key, fmt, a, b);
    return a2_ini_int(datadir, "Game.ini", section, key, fallback);
}

void a2_save_load(a2_save *s, const char *datadir, int etappe)
{
    memset(s, 0, sizeof *s);
    s->etappe = etappe;
    s->money = clampi(a2_ini_int(datadir, "Game.ini", "[PLAYER]", "money", 0), 0, 999999);
    s->damage = clampi(a2_ini_int(datadir, "Game.ini", "[PLAYER]", "damage", 0), 0, 100);
    s->position = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "position", 0);
    if (s->position < 0)
        s->position = 0;
    /* 0 (or absent) means no record yet (0x422266). */
    s->lap_record = key_int(datadir, "[BEST TIMES]", "LAP_RECORD_E%d", etappe, 0, 0);
    if (!s->lap_record)
        s->lap_record = A2_NO_RECORD;
    s->race_record = key_int(datadir, "[BEST TIMES]", "RACE_RECORD_E%d", etappe, 0, 0);
    if (!s->race_record)
        s->race_record = A2_NO_RECORD;
    /* The saved count starts at -1 in the binary (0x48c88c) and only a lap
     * completion sets it, so the loaded times are kept for the writer alone. */
    s->time_checks = -1;
    int n = clampi(key_int(datadir, "[BEST TIMES]", "TIME_CHECK_COUNT_E%d", etappe, 0, 0), 0, 9);
    for (int i = 0; i < n; i++)
        s->check_ms[i] = key_int(datadir, "[BEST TIMES]", "TIME_CHECK_E%d_C%d", etappe, i, 0);

    for (int i = 0; i < 8; i++) {
        s->damage_mesh[i] = -1;
        s->damage_value[i] = -1.0f;
    }
    char buf[256];
    if (a2_ini_string(datadir, "Game.ini", "[PLAYER]", "DAMAGE_MESH", buf, sizeof buf)) {
        char *p = buf;
        for (int i = 0; i < 8 && *p; i++) {
            s->damage_mesh[i] = (int)strtol(p, &p, 10);
            p += *p == ',';
        }
    }
    if (a2_ini_string(datadir, "Game.ini", "[PLAYER]", "DAMAGE_VALUE", buf, sizeof buf)) {
        char *p = buf;
        for (int i = 0; i < 8 && *p; i++) {
            s->damage_value[i] = strtof(p, &p);
            p += *p == ',';
        }
    }
}

int a2_save_prize(int position, int etappe, int difficulty)
{
    static const int prize[4] = {55000, 35000, 15000, 0};
    int p = prize[clampi(position, 0, 3)];
    if (etappe == 6)
        p *= 4; /* shl eax, 2 at 0x42811f */
    if (position == 3 && difficulty == 0)
        p = 5000; /* 0x428131: last place, EASY */
    return p;
}

void a2_save_rank_racers(a2_racer_result *r, int n, int player_position)
{
    /* 0x411593: the three slots' ties, broken in that fixed order */
    if (n >= 2 && r[0].time_ms == r[1].time_ms)
        r[0].time_ms = r[1].time_ms + 1;
    if (n >= 3 && r[0].time_ms == r[2].time_ms)
        r[0].time_ms = r[2].time_ms + 1;
    if (n >= 3 && r[1].time_ms == r[2].time_ms)
        r[1].time_ms = r[2].time_ms + 1;
    for (int i = 0; i < n; i++) {
        r[i].position = 0;
        for (int j = 0; j < n; j++)
            if (j != i && r[i].time_ms > r[j].time_ms)
                r[i].position++;
    }
    for (int i = 0; i < n; i++)
        if (r[i].position >= player_position)
            r[i].position++;
}

static void put_int(const char *datadir, const char *section, const char *key, int v)
{
    char buf[32];
    snprintf(buf, sizeof buf, "%d", v);
    a2_ini_write(datadir, "Game.ini", section, key, buf);
}

void a2_save_mark_unfinished(const char *datadir)
{
    put_int(datadir, "[PLAYER]", "finished", 0);
}

void a2_save_write(const char *datadir, const a2_save *s, int finished, int tracktime_ms,
                   const a2_racer_result *racers, int racer_count)
{
    char key[64], buf[256];
    /* write_player_info, 0x422ec0 */
    put_int(datadir, "[PLAYER]", "finished", finished);
    if (!finished)
        return;
    put_int(datadir, "[PLAYER]", "money", s->money);
    put_int(datadir, "[PLAYER]", "damage", s->damage);
    put_int(datadir, "[PLAYER]", "tracktime", tracktime_ms);
    put_int(datadir, "[PLAYER]", "position", s->position);
    const int *m = s->damage_mesh;
    snprintf(buf, sizeof buf, "%d,%d,%d,%d,%d,%d,%d,%d", m[0], m[1], m[2], m[3], m[4], m[5], m[6],
             m[7]);
    a2_ini_write(datadir, "Game.ini", "[PLAYER]", "DAMAGE_MESH", buf);
    const float *v = s->damage_value;
    snprintf(buf, sizeof buf, "%f,%f,%f,%f,%f,%f,%f,%f", v[0], v[1], v[2], v[3], v[4], v[5], v[6],
             v[7]);
    a2_ini_write(datadir, "Game.ini", "[PLAYER]", "DAMAGE_VALUE", buf);

    /* the racers, 0x422d40: always the three slots of 0x485b20; their damage
     * arrays start at zero (static data) and nothing changes them */
    for (int i = 0; i < 3; i++) {
        a2_racer_result r = {3, 0, i + 1}; /* the slots' static defaults */
        if (i < racer_count)
            r = racers[i];
        snprintf(key, sizeof key, "%d", i);
        snprintf(buf, sizeof buf, "%d,%d,%d", r.car, r.position, r.time_ms);
        a2_ini_write(datadir, "Game.ini", "[RACERS]", key, buf);
        snprintf(key, sizeof key, "DAM%d", i);
        a2_ini_write(datadir, "Game.ini", "[RACERS]", key, "0,0,0,0,0,0,0,0");
        snprintf(key, sizeof key, "VAL%d", i);
        a2_ini_write(datadir, "Game.ini", "[RACERS]", key,
                     "0.000000,0.000000,0.000000,0.000000,0.000000,0.000000,0.000000,0.000000");
    }

    /* best times, 0x422c10 */
    snprintf(key, sizeof key, "LAP_RECORD_E%d", s->etappe);
    put_int(datadir, "[BEST TIMES]", key, s->lap_record);
    snprintf(key, sizeof key, "RACE_RECORD_E%d", s->etappe);
    put_int(datadir, "[BEST TIMES]", key, s->race_record);
    int n = s->time_checks < 0 || s->time_checks >= 10 ? 0 : s->time_checks;
    snprintf(key, sizeof key, "TIME_CHECK_COUNT_E%d", s->etappe);
    put_int(datadir, "[BEST TIMES]", key, n);
    for (int i = 0; i < n; i++) {
        snprintf(key, sizeof key, "TIME_CHECK_E%d_C%d", s->etappe, i);
        put_int(datadir, "[BEST TIMES]", key, s->check_ms[i]);
    }
}

/* The tweak menu; see tweak.h.  The texts are the game's own (0x485cf4..). */
#include "tweak.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

static const char *const MAIN_ITEMS[12] = {
    "   1. Acceleratie constanten", "   2. Max. snelheid/versnelling",
    "   3. Schakelpunt vooruit",    "   4. Schakelpunt achteruit",
    "   5. Remkracht",              "   6. Afremmen op motor",
    "   7. Rolhoek factoren",       "   8. Pitch factoren",
    "   9. Hoogte factoren",        "  10. Stuursnelheid",
    "  11. Opslaan",                "  12. Stoppen",
};

/* the per-gear lists (0x412820, 0x412390): item 6 is Hoofdmenu, 7 the 6th gear */
static const char *const GEAR_ITEMS[7] = {
    "  1. 1e versnelling", "  2. 2e versnelling", "  3. 3e versnelling", "  4. 4e versnelling",
    "  5. 5e versnelling", "  6. Hoofdmenu",       "  7. 6e versnelling",
};

/* the suspension lists (0x411710, 0x411bd0, 0x411fb0); the pitch list's
 * items read "Rolhoek" in the game too */
typedef struct {
    const char *title;
    int n;
    const char *items[4];
    const char *labels[3];
} susp_list;
static const susp_list SUSP[3] = {
    {"  Rolhoek factoren", 4,
     {"  1. Rolhoek versterking", "  2. Rolhoek demping", "  3. Rolhoek factor", "  4. Hoofdmenu"},
     {"Rolhoek versterking", "Rolhoek demping", "Rolhoek Factor"}},
    {"  Pitchhoek factoren", 3, {"  1. Rolhoek demping", "  2. Rolhoek factor", "  3. Hoofdmenu"},
     {"Pitchhoek demping", "Pitchhoek Factor", NULL}},
    {"  Hoogte factoren", 3, {"  1. Hoogte demping", "  2. Hoogte factor", "  3. Hoofdmenu"},
     {"Hoogte demping", "Hoogte Factor", NULL}},
};

void a2_tweak_init(a2_tweak *t)
{
    memset(t, 0, sizeof *t);
}

/* CARS.DAT as 0x4135b3 writes it: the 10 x 124-byte table (0x4a5ee0) then
 * the 10 x 24-byte one (0x4a5df0). */
static void save_cars(const a2_cars *cars, const char *datadir)
{
    char path[512];
    snprintf(path, sizeof path, "%s/CARS.DAT", datadir);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "tweak: cannot write %s\n", path);
        return;
    }
    for (int i = 0; i < A2_CAR_SLOTS; i++) {
        const a2_car *c = &cars->cars[i];
        float a[31];
        int k = 0;
        for (int g = 0; g < A2_CAR_GEARS; g++)
            a[k++] = c->accel[g];
        for (int g = 0; g < A2_CAR_GEARS; g++)
            a[k++] = c->accel_upgraded[g];
        for (int g = 0; g < A2_CAR_GEARS; g++)
            a[k++] = c->max_speed[g];
        for (int g = 0; g < A2_CAR_GEARS; g++)
            a[k++] = c->max_speed_upgraded[g];
        a[k++] = c->upshift_point;
        a[k++] = c->downshift_point;
        a[k++] = c->unused_26;
        a[k++] = c->brake_force;
        a[k++] = c->engine_decel;
        a[k++] = c->roll_rate;
        a[k++] = c->steer_speed;
        fwrite(a, 4, 31, f);
    }
    for (int i = 0; i < A2_CAR_SLOTS; i++) {
        const a2_car *c = &cars->cars[i];
        float b[6] = {c->roll_damping,  c->roll_factor,    c->pitch_damping,
                      c->pitch_factor, c->height_damping, c->height_factor};
        fwrite(b, 4, 6, f);
    }
    fclose(f);
    fprintf(stderr, "tweak: wrote %s\n", path);
}

void a2_tweak_open(a2_tweak *t, const a2_cars *cars, const char *datadir)
{
    if (t->state == 11) { /* Opslaan */
        save_cars(cars, datadir);
        t->state = 0;
        return;
    }
    if (t->state == 12) { /* Stoppen */
        t->state = 0;
        return;
    }
    t->open = 1;
    t->cursor = 0;
    t->editing = 0;
}

static int list_len(int state)
{
    if (state == 0)
        return 12;
    if (state == 1 || state == 2)
        return 7;
    if (state >= 7 && state <= 9)
        return SUSP[state - 7].n;
    return 0;
}

/* the value an editor changes, and its step for +/- */
static float *field(a2_tweak *t, a2_car *c, int gearbox, float *step)
{
    switch (t->state) {
    case 1: *step = 0.05f; return gearbox ? &c->accel_upgraded[t->item] : &c->accel[t->item];
    case 2: *step = 1.0f; return gearbox ? &c->max_speed_upgraded[t->item] : &c->max_speed[t->item];
    case 3: *step = 0.1f; return &c->upshift_point;
    case 4: *step = 0.1f; return &c->downshift_point;
    case 5: *step = 0.1f; return &c->brake_force;
    case 6: *step = 0.5f; return &c->engine_decel;
    case 7:
        *step = t->item == 0 ? 0.1f : t->item == 1 ? 0.5f : 1.0f;
        return t->item == 0 ? &c->roll_rate : t->item == 1 ? &c->roll_damping : &c->roll_factor;
    case 8: *step = t->item == 0 ? 0.5f : 1.0f; return t->item == 0 ? &c->pitch_damping : &c->pitch_factor;
    case 9: *step = t->item == 0 ? 0.5f : 1.0f; return t->item == 0 ? &c->height_damping : &c->height_factor;
    case 10: *step = 0.1f; return &c->steer_speed;
    default: return NULL;
    }
}

static int single(int state) { return (state >= 3 && state <= 6) || state == 10; }

int a2_tweak_key(a2_tweak *t, int key, a2_cars *cars, int car_slot, int gearbox,
                 const char *datadir)
{
    (void)datadir;
    if (!t->open)
        return 0;
    a2_car *c = &cars->cars[car_slot];
    int listing = list_len(t->state) > 0 && !t->editing;
    if (listing) {
        int n = list_len(t->state);
        if (key == SDLK_UP)
            t->cursor = (t->cursor + n - 1) % n;
        else if (key == SDLK_DOWN)
            t->cursor = (t->cursor + 1) % n;
        else if (key == SDLK_SPACE) {
            if (t->state == 0) { /* the next Tab opens the chosen item */
                t->state = t->cursor + 1;
                t->open = 0;
            } else if ((t->state <= 2 && t->cursor == 5) ||
                       (t->state >= 7 && t->cursor == SUSP[t->state - 7].n - 1)) {
                t->state = 0; /* Hoofdmenu */
                t->open = 0;
            } else {
                t->item = t->state <= 2 ? (t->cursor == 6 ? 5 : t->cursor) : t->cursor;
                t->editing = 1;
            }
        } else {
            return 0;
        }
        return 1;
    }
    float step = 0.0f;
    float *v = field(t, c, gearbox, &step);
    if (key == SDLK_KP_PLUS && v)
        *v += step;
    else if (key == SDLK_KP_MINUS && v)
        *v -= step;
    else if (key == SDLK_RIGHT && v && t->state == 2)
        *v += 10.0f;
    else if (key == SDLK_LEFT && v && t->state == 2)
        *v -= 10.0f;
    else if (key == SDLK_SPACE) {
        if (single(t->state)) { /* back to the main menu, and the race */
            t->state = 0;
            t->open = 0;
        } else {
            t->editing = 0; /* back to the list (the port's reading) */
        }
    } else {
        return 0;
    }
    return 1;
}

static void add(a2_tweak *t, const char *s)
{
    if (t->line_count < 16)
        snprintf(t->lines[t->line_count++], sizeof t->lines[0], "%s", s);
}

void a2_tweak_text(a2_tweak *t, const a2_cars *cars, int car_slot, int gearbox)
{
    t->line_count = 0;
    if (!t->open)
        return;
    const a2_car *c = &cars->cars[car_slot];
    char buf[64];
    static const char *const TITLES[11] = {
        NULL, "  Acceleratie constanten", "  Top snelheden/versnelling", "Punt vooruitschakelen",
        "Punt terugschakelen", "Remkracht", "Afremmen motor", NULL, NULL, NULL, "Draaihoek"};
    static const char *const VALUE[11] = {NULL,           NULL,
                                          NULL,           "Schakelpunt: %3.3f",
                                          "Schakelpunt: %3.3f", "Remkracht: %3.3f",
                                          "Motor vertraging: %3.3f", NULL,
                                          NULL,           NULL,
                                          "Draaihoek: %3.3f"};
    if (t->state == 0 || ((t->state <= 2 || (t->state >= 7 && t->state <= 9)) && !t->editing)) {
        int n = list_len(t->state);
        if (t->state == 0)
            add(t, "   Tweak Menu");
        else if (t->state <= 2)
            add(t, TITLES[t->state]);
        else
            add(t, SUSP[t->state - 7].title);
        for (int i = 0; i < n; i++) {
            const char *it = t->state == 0 ? MAIN_ITEMS[i]
                             : t->state <= 2 ? GEAR_ITEMS[i]
                                             : SUSP[t->state - 7].items[i];
            snprintf(buf, sizeof buf, "%s%s", i == t->cursor ? ">" : " ", it + 1);
            add(t, buf);
        }
        return;
    }
    float step;
    float *v = field(t, (a2_car *)c, gearbox, &step);
    if (t->state == 1 || t->state == 2) {
        snprintf(buf, sizeof buf, "Bak: %d Acceleratie: %d", t->item + 1, gearbox);
        add(t, buf);
        snprintf(buf, sizeof buf, t->state == 1 ? "Waarde: %3.3f" : "Snelheid: %3.3f", v ? *v : 0.0f);
        add(t, buf);
    } else if (t->state >= 7 && t->state <= 9) {
        add(t, SUSP[t->state - 7].labels[t->item]);
        snprintf(buf, sizeof buf, "%3.3f", v ? *v : 0.0f);
        add(t, buf);
    } else {
        add(t, TITLES[t->state]);
        snprintf(buf, sizeof buf, VALUE[t->state], v ? *v : 0.0f);
        add(t, buf);
    }
}

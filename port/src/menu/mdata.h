/* Tables read straight out of menu.dat (v1.01, PE timestamp 0x363DD150), the
 * way the race reads its sound names out of spel.dat: the screens' form
 * layouts, the sprite animations and the car list.  Addresses are recorded
 * in reverse-engineering/menu.md.
 *
 * - A form is an array of {x, y, type, descriptor*, state, 0, 0} records
 *   ending in x = -1 (Forms.cpp).  Type 1 is an animated button whose
 *   descriptor is packed: id (i32), -1, -1, hotkey (char), label* , ...;
 *   type 4 is a hot area whose descriptor starts {w, h}.
 * - The animation table at 0x433e60 holds {frames, 1, frame list*}; a frame is
 *   {2, sprite number}, and sprite numbers are MENURC index + 1.
 * - The car table at 0x42df54 holds 48-byte records: anim, name[40], index.
 */
#ifndef A2_MDATA_H
#define A2_MDATA_H

#include <stdint.h>

#define M_MAX_CONTROLS 24
#define M_MAX_FRAMES 48
#define M_CARS 7

typedef struct {
    int x, y, type, state;
    int id;       /* type 1 */
    char hotkey;  /* type 1 */
    uint32_t style; /* type 1: descriptor +0x15, 0x4274a8 for the big
                       148x42 buttons, 0x427708 for the small ones, 0 HELP */
    char label[32];
    int w, h;     /* type 4 */
} m_control;

typedef struct {
    int count;
    m_control c[M_MAX_CONTROLS];
} m_form;

typedef struct {
    int frames;
    int sprite[M_MAX_FRAMES]; /* MENURC indices (0-based) */
} m_anim;

typedef struct {
    char name[40];
    int anim;
} m_car;

/* The screens' forms, by the address their code uses. */
enum {
    M_FORM_MAIN,      /* 0x42ed40, m_main.cpp */
    M_FORM_CONTROLS,  /* 0x42a238, m_bediening.cpp */
    M_FORM_GARAGE,    /* 0x42b628, m_garage.cpp */
    M_FORM_STANDINGS, /* 0x42c2f8, m_highscore.cpp */
    M_FORM_SHOWROOM,  /* 0x42da50, m_kopen.cpp */
    M_FORM_NEWPLAYER, /* 0x430080, m_nspeler.cpp */
    M_FORM_OPTIONS,   /* 0x430f60, m_opties.cpp */
    M_FORM_COUNT
};

typedef struct {
    m_form form[M_FORM_COUNT];
    int anim_count;
    m_anim anim[64];
    m_car car[M_CARS];
} m_data;

/* Returns 0, or -1 if menu.dat is missing or not the build these addresses
 * belong to. */
int m_data_load(m_data *d, const char *datadir);

#endif /* A2_MDATA_H */

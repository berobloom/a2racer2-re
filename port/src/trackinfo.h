/* Raser.ini's [MAPS] track table.
 *
 * This is the game's own per-track configuration, and the original parses it
 * with `load_track_info` (fcn.004225D0, Gm_ini.cpp) into a 137-byte record per
 * track.  The developers commented the row format in the ini itself:
 *
 *   ; Arg 0:  0=highway(snelweg), 1=city(stad)
 *   ; Arg 1:  Filenames for maps (without extension)
 *   ; Arg 2, 3, 4, 5:  Textures to load for map (without ext.)
 *   ; Arg 6:  Lores unaccelerated filename of the sky
 *   ; Arg 7:  Hires unaccelerated filename of the sky
 *   ; Arg 8:  Name of the track (shown in main menu)
 *   ; Arg 9:  Zero-based index in music array
 *   ; Arg 10: Timer (should be kept 10000!)
 *
 *   0=1,etappe0,e00_weg,e00_gras,e00_gra2,e00_vang,scrl_lo,scrl_hi,AMSTERDAM,0,10000
 *
 * Reading it beats guessing: args 2-5 give the four `.rmp` plane-list textures
 * **in list order** (which is not the order of E0x_ALGEMENE_TEXTURES_ANIM's
 * frames), and arg 0 states outright whether a track is a circuit.
 *
 * The table declares 9 tracks; etappe7 (MUNCHEN - BERLIN) and etappe8 (BERLIN)
 * have no data files on the Dutch CD, so a row whose map files are missing is
 * simply not selectable.
 */
#ifndef A2_TRACKINFO_H
#define A2_TRACKINFO_H

#include <stddef.h>

#define A2_MAX_TRACKS 20 /* the original's own limit, in load_track_info */
#define A2_TI_NAME 32
#define A2_TI_FILE 20

typedef struct {
    int circuit;               /* arg 0: 1 = city circuit, 0 = point-to-point */
    char stem[A2_TI_FILE];     /* arg 1: "etappe0" */
    char tex[4][A2_TI_FILE];   /* args 2-5: weg, gras, gra2, vang (no extension) */
    char sky_lo[A2_TI_FILE];   /* arg 6 */
    char sky_hi[A2_TI_FILE];   /* arg 7 */
    char name[A2_TI_NAME];     /* arg 8: "AMSTERDAM - DEN HAAG" */
    int music;                 /* arg 9 */
    int timer;                 /* arg 10 */
} a2_trackinfo;

/* Parse <datadir>/Raser.ini.  Fills up to `max` rows and returns how many were
 * read, or 0 if the file is missing or has no [MAPS] section. */
int a2_trackinfo_load(a2_trackinfo *out, int max, const char *datadir);

/* Index of the row whose stem matches `stem` (case-insensitive), or -1. */
int a2_trackinfo_find(const a2_trackinfo *rows, int count, const char *stem);

/* Read one integer key out of an ini file in `datadir` (e.g. "Game.ini",
 * "[OPTIONS]", "Nrlaps").  Section and key are matched case-insensitively;
 * `fallback` is returned when the file, section or key is missing.
 * Game.ini is the game's own settings file, so its defaults are the game's:
 * Nrlaps=2, visiblechunks=30, dif_mod=4, Difficulty=1. */
int a2_ini_int(const char *datadir, const char *file, const char *section, const char *key,
               int fallback);

/* The same lookup for a string value (up to `cap` - 1 bytes).  Returns 0 and
 * leaves `out` alone when the key is missing. */
int a2_ini_string(const char *datadir, const char *file, const char *section, const char *key,
                  char *out, size_t cap);

/* Set `key` in `section` (with brackets, as above) to `value`, the way
 * WritePrivateProfileString does it: in place if the key exists, otherwise
 * after the section's last key, otherwise in a new section at the end.
 * Comments and line ends are kept.  Returns 0 if the file cannot be written. */
int a2_ini_write(const char *datadir, const char *file, const char *section, const char *key,
                 const char *value);

#endif /* A2_TRACKINFO_H */

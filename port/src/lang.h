/* language.ini -- the game's own UI strings.
 *
 * [SETTINGS] LANGUAGE selects 0=Dutch, 1=English, 2=German, and TEXT_COUNT says
 * how many numbered strings to read from the matching section ([NEDERLANDS],
 * [ENGELS], [DUITS]).  The retail file has 56.
 *
 * Useful ids: 0 GEFLITST (speed-camera flash), 1 the fine, 6/8/10 POLITIE with
 * 7/9/11 their fines, 17 GEFINISHT, 18 POSITIE, 19 total time, 33 RACE TIJD,
 * 34 RONDE INFO, 35 TIJD, 36 RECORD, 38 POSITIE, 39 RONDE, 41 TIJD IS OM,
 * 42 GEARRESTEERD, 45 LAATSTE RONDE.
 *
 * Note the shipped [ENGELS] section is only half translated -- 34 of its 56
 * entries are byte-identical to the German ones (see research/discoveries.md).
 */
#ifndef A2_LANG_H
#define A2_LANG_H

typedef struct a2_lang a2_lang;

/* Load <datadir>/language.ini.  `language` of -1 honours the file's own
 * [SETTINGS] LANGUAGE value. */
a2_lang *a2_lang_load(const char *datadir, int language);
void a2_lang_free(a2_lang *l);

/* String `id`, or `fallback` when it is missing. */
const char *a2_lang_get(const a2_lang *l, int id, const char *fallback);
int a2_lang_language(const a2_lang *l);

#endif /* A2_LANG_H */

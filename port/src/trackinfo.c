#include "trackinfo.h"

#include "a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Copy one comma-separated field, trimmed, into a fixed buffer. */
static void field(char *dst, size_t cap, const char *src, size_t len)
{
    while (len && (*src == ' ' || *src == '\t')) {
        src++;
        len--;
    }
    while (len && (src[len - 1] == ' ' || src[len - 1] == '\t' || src[len - 1] == '\r'))
        len--;
    if (len > cap - 1)
        len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Split `line` on commas into at most `max` pointers/lengths.  Returns the
 * number of fields found. */
static int split(const char *line, size_t len, const char **p, size_t *n, int max)
{
    int c = 0;
    size_t start = 0;
    for (size_t i = 0; i <= len && c < max; i++) {
        if (i == len || line[i] == ',') {
            p[c] = line + start;
            n[c] = i - start;
            c++;
            start = i + 1;
        }
    }
    return c;
}

int a2_trackinfo_load(a2_trackinfo *out, int max, const char *datadir)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/Raser.ini", datadir);
    size_t size = 0;
    uint8_t *data = a2_read_file(path, &size);
    if (!data) {
        /* The CD ships it capitalised one way, Wine copies another. */
        snprintf(path, sizeof path, "%s/RASER.INI", datadir);
        data = a2_read_file(path, &size);
    }
    if (!data) {
        fprintf(stderr, "trackinfo: cannot read %s/Raser.ini\n", datadir);
        return 0;
    }

    int count = 0;
    int in_maps = 0;
    const char *s = (const char *)data;
    const char *end = s + size;

    while (s < end && count < max) {
        const char *eol = memchr(s, '\n', (size_t)(end - s));
        size_t len = eol ? (size_t)(eol - s) : (size_t)(end - s);
        const char *line = s;
        s = eol ? eol + 1 : end;

        while (len && (*line == ' ' || *line == '\t')) {
            line++;
            len--;
        }
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' '))
            len--;
        if (!len || line[0] == ';')
            continue;

        if (line[0] == '[') {
            in_maps = (len >= 6 && memcmp(line, "[MAPS]", 6) == 0);
            continue;
        }
        if (!in_maps)
            continue;

        /* Rows are "<n>=<arg0>,<arg1>,..."; `count=9` also lives here. */
        const char *eq = memchr(line, '=', len);
        if (!eq)
            continue;
        size_t klen = (size_t)(eq - line);
        if (klen == 0 || line[0] < '0' || line[0] > '9')
            continue; /* the "count=" key, not a track row */

        const char *rhs = eq + 1;
        size_t rlen = len - klen - 1;
        const char *p[16];
        size_t n[16];
        int got = split(rhs, rlen, p, n, 16);
        if (got < 11) {
            fprintf(stderr, "trackinfo: row with %d args, expected 11 -- skipped\n", got);
            continue;
        }

        a2_trackinfo *t = &out[count];
        memset(t, 0, sizeof(*t));
        char buf[64];
        field(buf, sizeof buf, p[0], n[0]);
        t->circuit = atoi(buf);
        field(t->stem, sizeof t->stem, p[1], n[1]);
        for (int i = 0; i < 4; i++)
            field(t->tex[i], sizeof t->tex[i], p[2 + i], n[2 + i]);
        field(t->sky_lo, sizeof t->sky_lo, p[6], n[6]);
        field(t->sky_hi, sizeof t->sky_hi, p[7], n[7]);
        field(t->name, sizeof t->name, p[8], n[8]);
        field(buf, sizeof buf, p[9], n[9]);
        t->music = atoi(buf);
        field(buf, sizeof buf, p[10], n[10]);
        t->timer = atoi(buf);
        count++;
    }

    free(data);
    return count;
}

int a2_trackinfo_find(const a2_trackinfo *rows, int count, const char *stem)
{
    for (int i = 0; i < count; i++) {
        const char *a = rows[i].stem, *b = stem;
        while (*a && *b) {
            int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
            int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
            if (ca != cb)
                break;
            a++;
            b++;
        }
        if (!*a && !*b)
            return i;
    }
    return -1;
}

static int ci_eq(const char *a, const char *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        int ca = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i];
        int cb = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
        if (!cb || ca != cb)
            return 0;
    }
    return b[n] == '\0';
}

int a2_ini_int(const char *datadir, const char *file, const char *section, const char *key,
               int fallback)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", datadir, file);
    size_t size = 0;
    uint8_t *data = a2_read_file(path, &size);
    if (!data)
        return fallback;

    int value = fallback;
    int in_section = (section == NULL);
    const char *s = (const char *)data, *end = s + size;
    while (s < end) {
        const char *eol = memchr(s, '\n', (size_t)(end - s));
        size_t len = eol ? (size_t)(eol - s) : (size_t)(end - s);
        const char *line = s;
        s = eol ? eol + 1 : end;
        while (len && (*line == ' ' || *line == '\t')) {
            line++;
            len--;
        }
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t'))
            len--;
        if (!len || line[0] == ';')
            continue;
        if (line[0] == '[') {
            if (section)
                in_section = (len == strlen(section) && ci_eq(line, section, len));
            continue;
        }
        if (!in_section)
            continue;
        const char *eq = memchr(line, '=', len);
        if (!eq)
            continue;
        size_t klen = (size_t)(eq - line);
        while (klen && (line[klen - 1] == ' ' || line[klen - 1] == '\t'))
            klen--;
        if (klen != strlen(key) || !ci_eq(line, key, klen))
            continue;
        char buf[32];
        size_t vlen = len - (size_t)(eq + 1 - line);
        if (vlen > sizeof buf - 1)
            vlen = sizeof buf - 1;
        memcpy(buf, eq + 1, vlen);
        buf[vlen] = '\0';
        value = atoi(buf);
        break;
    }
    free(data);
    return value;
}

int a2_ini_string(const char *datadir, const char *file, const char *section, const char *key,
                  char *out, size_t cap)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", datadir, file);
    size_t size = 0;
    uint8_t *data = a2_read_file(path, &size);
    if (!data || !cap)
        return 0;
    int found = 0;
    int in_section = 0;
    const char *s = (const char *)data, *end = s + size;
    while (s < end && !found) {
        const char *eol = memchr(s, '\n', (size_t)(end - s));
        size_t len = eol ? (size_t)(eol - s) : (size_t)(end - s);
        const char *line = s;
        s = eol ? eol + 1 : end;
        while (len && (*line == ' ' || *line == '\t')) {
            line++;
            len--;
        }
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t'))
            len--;
        if (!len || line[0] == ';')
            continue;
        if (line[0] == '[') {
            in_section = (len == strlen(section) && ci_eq(line, section, len));
            continue;
        }
        const char *eq = in_section ? memchr(line, '=', len) : NULL;
        if (!eq)
            continue;
        size_t klen = (size_t)(eq - line);
        while (klen && (line[klen - 1] == ' ' || line[klen - 1] == '\t'))
            klen--;
        if (klen != strlen(key) || !ci_eq(line, key, klen))
            continue;
        size_t vlen = len - (size_t)(eq + 1 - line);
        if (vlen > cap - 1)
            vlen = cap - 1;
        memcpy(out, eq + 1, vlen);
        out[vlen] = '\0';
        found = 1;
    }
    free(data);
    return found;
}

/* WritePrivateProfileString's behaviour, as far as the game relies on it: an
 * existing key keeps its line and gets the new value; a missing key goes after
 * the section's last key; a missing section is appended.  Everything else --
 * the developers' comments, blank lines, CRLF line ends -- is left alone. */
int a2_ini_write(const char *datadir, const char *file, const char *section, const char *key,
                 const char *value)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", datadir, file);
    size_t size = 0;
    uint8_t *data = a2_read_file(path, &size);
    const char *s = data ? (const char *)data : "", *end = s + (data ? size : 0);
    const char *nl = (data && memchr(s, '\r', size)) || !data ? "\r\n" : "\n";

    size_t replace_at = 0, replace_end = 0; /* the existing key line */
    size_t insert_at = 0;                   /* after the section's last key line */
    int have_section = 0, have_key = 0, in_section = 0;
    const char *p = s;
    while (p < end && !have_key) {
        const char *eol = memchr(p, '\n', (size_t)(end - p));
        const char *next = eol ? eol + 1 : end;
        const char *line = p;
        size_t len = (size_t)((eol ? eol : end) - p);
        while (len && (*line == ' ' || *line == '\t')) {
            line++;
            len--;
        }
        while (len && (line[len - 1] == '\r' || line[len - 1] == ' ' || line[len - 1] == '\t'))
            len--;
        if (len && line[0] == '[') {
            in_section = (len == strlen(section) && ci_eq(line, section, len));
            if (in_section) {
                have_section = 1;
                insert_at = (size_t)(next - s);
            }
        } else if (in_section && len && line[0] != ';') {
            const char *eq = memchr(line, '=', len);
            if (eq) {
                size_t klen = (size_t)(eq - line);
                while (klen && (line[klen - 1] == ' ' || line[klen - 1] == '\t'))
                    klen--;
                insert_at = (size_t)(next - s);
                if (klen == strlen(key) && ci_eq(line, key, klen)) {
                    have_key = 1;
                    replace_at = (size_t)(eq + 1 - s);
                    replace_end = (size_t)((eol ? eol : end) - s);
                    if (replace_end > replace_at && s[replace_end - 1] == '\r')
                        replace_end--;
                }
            }
        }
        p = next;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(data);
        return 0;
    }
    size_t total = (size_t)(end - s);
    if (have_key) {
        fwrite(s, 1, replace_at, f);
        fputs(value, f);
        fwrite(s + replace_end, 1, total - replace_end, f);
    } else if (have_section) {
        fwrite(s, 1, insert_at, f);
        if (insert_at && s[insert_at - 1] != '\n')
            fputs(nl, f);
        fprintf(f, "%s=%s%s", key, value, nl);
        fwrite(s + insert_at, 1, total - insert_at, f);
    } else {
        fwrite(s, 1, total, f);
        if (total && s[total - 1] != '\n')
            fputs(nl, f);
        fprintf(f, "%s%s%s=%s%s", section, nl, key, value, nl);
    }
    int ok = fclose(f) == 0;
    free(data);
    return ok;
}

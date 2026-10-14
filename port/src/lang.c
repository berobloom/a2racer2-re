#include "lang.h"

#include "a2_io.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_STRINGS 128

struct a2_lang {
    char *text[MAX_STRINGS];
    int language;
};

static const char *const SECTION[3] = {"[NEDERLANDS]", "[ENGELS]", "[DUITS]"};

/* Trim trailing CR/LF and spaces in place. */
static void rstrip(char *s)
{
    size_t n = strlen(s);
    while (n && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = '\0';
}

a2_lang *a2_lang_load(const char *datadir, int language)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/language.ini", datadir);
    size_t size = 0;
    uint8_t *data = a2_read_file(path, &size);
    if (!data) {
        fprintf(stderr, "lang: cannot read %s\n", path);
        return NULL;
    }

    a2_lang *l = calloc(1, sizeof(*l));
    if (!l) {
        free(data);
        return NULL;
    }

    /* Pass 1: the file's own LANGUAGE setting, if the caller did not pick. */
    if (language < 0 || language > 2) {
        language = 0;
        const char *p = strstr((char *)data, "LANGUAGE=");
        if (p)
            language = atoi(p + 9);
        if (language < 0 || language > 2)
            language = 0;
    }
    l->language = language;

    /* Pass 2: read the numbered entries of the selected section. */
    char *text = malloc(size + 1);
    if (!text) {
        free(data);
        free(l);
        return NULL;
    }
    memcpy(text, data, size);
    text[size] = '\0';
    free(data);

    int in_section = 0;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        rstrip(line);
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '[') {
            in_section = strncmp(line, SECTION[language], strlen(SECTION[language])) == 0;
            continue;
        }
        if (!in_section || !isdigit((unsigned char)*line))
            continue;
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        int id = atoi(line);
        if (id < 0 || id >= MAX_STRINGS)
            continue;
        free(l->text[id]);
        l->text[id] = strdup(eq + 1);
    }
    free(text);

    int n = 0;
    for (int i = 0; i < MAX_STRINGS; i++)
        if (l->text[i])
            n++;
    fprintf(stderr, "lang: %d strings, language %d (%s)\n", n, language,
            language == 0 ? "Dutch" : language == 1 ? "English" : "German");
    return l;
}

void a2_lang_free(a2_lang *l)
{
    if (!l)
        return;
    for (int i = 0; i < MAX_STRINGS; i++)
        free(l->text[i]);
    free(l);
}

const char *a2_lang_get(const a2_lang *l, int id, const char *fallback)
{
    if (!l || id < 0 || id >= MAX_STRINGS || !l->text[id])
        return fallback;
    return l->text[id];
}

int a2_lang_language(const a2_lang *l) { return l ? l->language : 0; }

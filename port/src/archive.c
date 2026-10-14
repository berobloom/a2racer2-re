#include "archive.h"

#include "a2_io.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NAMED_ENTRY_LEN (A2_ARCHIVE_NAME_LEN + 4)

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    return *a == *b;
}

/* The named index is an exact fit for 2 + count*24; that is what distinguishes
 * it from the binary one, which is an exact fit for 2 + count*4. */
static a2_ind_kind detect(const uint8_t *ind, size_t n)
{
    if (n < 2)
        return A2_IND_BINARY;
    size_t count = (size_t)(ind[0] | (ind[1] << 8));
    if (n == 2 + count * NAMED_ENTRY_LEN)
        return A2_IND_NAMED;
    return A2_IND_BINARY;
}

int a2_archive_open(a2_archive *ar, const char *img_path, const char *ind_path)
{
    memset(ar, 0, sizeof(*ar));

    size_t ind_size = 0;
    uint8_t *ind = a2_read_file(ind_path, &ind_size);
    if (!ind) {
        fprintf(stderr, "archive: cannot read %s\n", ind_path);
        return -1;
    }
    ar->img = a2_read_file(img_path, &ar->img_size);
    if (!ar->img) {
        fprintf(stderr, "archive: cannot read %s\n", img_path);
        free(ind);
        return -1;
    }

    a2_cur c;
    a2_cur_init(&c, ind, ind_size);
    size_t count = a2_u16(&c);
    ar->kind = detect(ind, ind_size);
    ar->entries = calloc(count ? count : 1, sizeof(a2_entry));
    if (!ar->entries) {
        free(ind);
        a2_archive_close(ar);
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        a2_entry *e = &ar->entries[i];
        if (ar->kind == A2_IND_NAMED) {
            uint8_t raw[A2_ARCHIVE_NAME_LEN];
            a2_bytes(&c, raw, sizeof raw);
            /* The name is NUL-terminated inside a fixed 20-byte field; the
             * bytes after the NUL are uninitialised writer garbage. */
            size_t k = 0;
            while (k < sizeof raw && raw[k])
                k++;
            memcpy(e->name, raw, k);
            e->name[k] = '\0';
        }
        e->offset = a2_u32(&c);
    }
    if (!a2_cur_ok(&c)) {
        fprintf(stderr, "archive: %s is truncated (%zu entries declared)\n", ind_path, count);
        free(ind);
        a2_archive_close(ar);
        return -1;
    }
    free(ind);

    ar->count = count;
    for (size_t i = 0; i < count; i++) {
        uint32_t end = (i + 1 < count) ? ar->entries[i + 1].offset : (uint32_t)ar->img_size;
        ar->entries[i].size =
            (end >= ar->entries[i].offset && end <= ar->img_size) ? end - ar->entries[i].offset : 0;
    }
    return 0;
}

void a2_archive_close(a2_archive *ar)
{
    free(ar->entries);
    free(ar->img);
    memset(ar, 0, sizeof(*ar));
}

long a2_archive_find(const a2_archive *ar, const char *name)
{
    for (size_t i = 0; i < ar->count; i++)
        if (ieq(ar->entries[i].name, name))
            return (long)i;
    return -1;
}

const uint8_t *a2_archive_data(const a2_archive *ar, size_t index, uint32_t *out_size)
{
    if (index >= ar->count)
        return NULL;
    const a2_entry *e = &ar->entries[index];
    if (e->offset > ar->img_size || e->size > ar->img_size - e->offset)
        return NULL;
    if (out_size)
        *out_size = e->size;
    return ar->img + e->offset;
}

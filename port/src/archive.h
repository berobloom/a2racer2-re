/* IND/IMG archive reader.
 *
 * The game ships two index formats (see reverse-engineering/file-formats.md):
 *
 *   named  (RCS.IND, Mrcs.ind)   u16 count, then count x { char name[20]; u32 offset; }
 *                                file length is exactly 2 + count*24
 *   binary (RC.IND, MENURC.IND)  u16 count, then count x u32 offset
 *                                the IMG repeats the count in its first u16
 *
 * Entry sizes are implicit in both: the gap to the next offset, and the IMG
 * length for the last entry.
 */
#ifndef A2_ARCHIVE_H
#define A2_ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

#define A2_ARCHIVE_NAME_LEN 20

typedef enum { A2_IND_NAMED, A2_IND_BINARY } a2_ind_kind;

typedef struct {
    char name[A2_ARCHIVE_NAME_LEN + 1]; /* empty for binary indices */
    uint32_t offset;
    uint32_t size;
} a2_entry;

typedef struct {
    a2_ind_kind kind;
    a2_entry *entries;
    size_t count;
    uint8_t *img; /* owned */
    size_t img_size;
} a2_archive;

/* Load an IMG/IND pair.  Returns 0 on success, -1 on failure (message on stderr). */
int a2_archive_open(a2_archive *ar, const char *img_path, const char *ind_path);
void a2_archive_close(a2_archive *ar);

/* Case-insensitive lookup; returns entry index or -1. */
long a2_archive_find(const a2_archive *ar, const char *name);
/* Pointer to an entry's bytes, or NULL. */
const uint8_t *a2_archive_data(const a2_archive *ar, size_t index, uint32_t *out_size);

#endif /* A2_ARCHIVE_H */

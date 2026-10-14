/* Byte-level readers for the original game's little-endian data files.
 *
 * Everything the game ships is little-endian x86 struct dumps, so the parsers
 * read through a cursor that refuses to run off the end of the buffer.  Once a
 * cursor goes bad it stays bad, so a parser can do a run of reads and check for
 * failure once at the end.
 */
#ifndef A2_IO_H
#define A2_IO_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int bad; /* set once a read overruns; never cleared */
} a2_cur;

/* Whole-file read.  Returns NULL on failure; caller frees with free(). */
uint8_t *a2_read_file(const char *path, size_t *out_size);

void a2_cur_init(a2_cur *c, const void *data, size_t size);
int a2_cur_ok(const a2_cur *c);
size_t a2_cur_left(const a2_cur *c);

uint8_t a2_u8(a2_cur *c);
uint16_t a2_u16(a2_cur *c);
uint32_t a2_u32(a2_cur *c);
int32_t a2_i32(a2_cur *c);
float a2_f32(a2_cur *c);

/* Copy n bytes out; zero-fills and marks bad on overrun. */
void a2_bytes(a2_cur *c, void *dst, size_t n);
/* Skip n bytes. */
void a2_skip(a2_cur *c, size_t n);
/* Borrow a pointer to n bytes in place (no copy); NULL on overrun. */
const uint8_t *a2_take(a2_cur *c, size_t n);

#endif /* A2_IO_H */

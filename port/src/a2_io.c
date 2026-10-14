#include "a2_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *a2_read_file(const char *path, size_t *out_size)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long n = ftell(f);
    if (n < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    uint8_t *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    if (out_size)
        *out_size = (size_t)n;
    return buf;
}

void a2_cur_init(a2_cur *c, const void *data, size_t size)
{
    c->data = (const uint8_t *)data;
    c->size = size;
    c->pos = 0;
    c->bad = 0;
}

int a2_cur_ok(const a2_cur *c) { return !c->bad; }

size_t a2_cur_left(const a2_cur *c)
{
    return c->pos <= c->size ? c->size - c->pos : 0;
}

/* Every read funnels through this so the overrun check lives in one place. */
static int take(a2_cur *c, size_t n, const uint8_t **out)
{
    if (c->bad || n > a2_cur_left(c)) {
        c->bad = 1;
        return 0;
    }
    *out = c->data + c->pos;
    c->pos += n;
    return 1;
}

uint8_t a2_u8(a2_cur *c)
{
    const uint8_t *p;
    return take(c, 1, &p) ? p[0] : 0;
}

uint16_t a2_u16(a2_cur *c)
{
    const uint8_t *p;
    if (!take(c, 2, &p))
        return 0;
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t a2_u32(a2_cur *c)
{
    const uint8_t *p;
    if (!take(c, 4, &p))
        return 0;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

int32_t a2_i32(a2_cur *c)
{
    uint32_t v = a2_u32(c);
    int32_t out;
    memcpy(&out, &v, 4);
    return out;
}

float a2_f32(a2_cur *c)
{
    uint32_t v = a2_u32(c);
    float out;
    memcpy(&out, &v, 4);
    return out;
}

void a2_bytes(a2_cur *c, void *dst, size_t n)
{
    const uint8_t *p;
    if (take(c, n, &p))
        memcpy(dst, p, n);
    else
        memset(dst, 0, n);
}

void a2_skip(a2_cur *c, size_t n)
{
    const uint8_t *p;
    take(c, n, &p);
}

const uint8_t *a2_take(a2_cur *c, size_t n)
{
    const uint8_t *p;
    return take(c, n, &p) ? p : NULL;
}

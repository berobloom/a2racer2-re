#include "msmacker.h"

#include <stdlib.h>
#include <string.h>

/* ---- bit reader: bits come from the least significant end of each byte -- */

typedef struct {
    const uint8_t *p;
    size_t size, pos; /* in bits */
} bits;

static int bit(bits *b)
{
    if (b->pos >= b->size * 8)
        return 0;
    int v = (b->p[b->pos >> 3] >> (b->pos & 7)) & 1;
    b->pos++;
    return v;
}

static unsigned getb(bits *b, int n)
{
    unsigned v = 0;
    for (int i = 0; i < n; i++)
        v |= (unsigned)bit(b) << i;
    return v;
}

/* ---- trees --------------------------------------------------------------- */

/* A node is internal (child[0] / child[1], taken on bit 0 / 1) or a leaf
 * holding a slot in `value`. */
typedef struct {
    int child[2];
    int slot; /* >= 0 for a leaf */
} node;

typedef struct {
    node *n;
    int nn, cap;
    int *value; /* slot values */
    int nv, vcap;
    int root; /* -1: no tree, every lookup is 0 */
    int last[3]; /* the cache's slots (big trees) */
} tree;

static void tree_free(tree *t)
{
    free(t->n);
    free(t->value);
    memset(t, 0, sizeof *t);
    t->root = -1;
}

static int new_node(tree *t)
{
    if (t->nn == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 64;
        t->n = realloc(t->n, (size_t)t->cap * sizeof *t->n);
    }
    t->n[t->nn] = (node){{-1, -1}, -1};
    return t->nn++;
}

static int new_slot(tree *t, int v)
{
    if (t->nv == t->vcap) {
        t->vcap = t->vcap ? t->vcap * 2 : 64;
        t->value = realloc(t->value, (size_t)t->vcap * sizeof *t->value);
    }
    t->value[t->nv] = v;
    return t->nv++;
}

static int walk(const tree *t, bits *b)
{
    if (t->root < 0)
        return -1;
    int i = t->root;
    for (int guard = 0; t->n[i].slot < 0 && guard < 64; guard++) {
        int c = t->n[i].child[bit(b)];
        if (c < 0)
            return -1;
        i = c;
    }
    return t->n[i].slot;
}

/* the small 8-bit tree: 1 = node (children 0 then 1), 0 = leaf with 8 bits */
static int small_rec(tree *t, bits *b, int depth)
{
    int i = new_node(t);
    if (depth > 32)
        return i;
    if (!bit(b)) {
        t->n[i].slot = new_slot(t, (int)getb(b, 8));
        return i;
    }
    int c0 = small_rec(t, b, depth + 1);
    int c1 = small_rec(t, b, depth + 1);
    t->n[i].child[0] = c0;
    t->n[i].child[1] = c1;
    return i;
}

/* present flag, the tree, a closing 0 bit */
static void small_tree(tree *t, bits *b)
{
    memset(t, 0, sizeof *t);
    t->root = -1;
    if (!bit(b))
        return;
    t->root = small_rec(t, b, 0);
    bit(b);
}

static int small_get(const tree *t, bits *b)
{
    int s = walk(t, b);
    return s < 0 ? 0 : t->value[s];
}

typedef struct {
    tree lo, hi;
    int esc[3];
} big_ctx;

static int big_rec(tree *t, bits *b, big_ctx *ctx, int depth)
{
    int i = new_node(t);
    if (depth > 64)
        return i;
    if (!bit(b)) {
        int v = small_get(&ctx->lo, b) | small_get(&ctx->hi, b) << 8;
        int slot = new_slot(t, v);
        for (int k = 0; k < 3; k++)
            if (v == ctx->esc[k] && t->last[k] < 0) {
                t->last[k] = slot;
                t->value[slot] = 0;
                break;
            }
        t->n[i].slot = slot;
        return i;
    }
    int c0 = big_rec(t, b, ctx, depth + 1);
    int c1 = big_rec(t, b, ctx, depth + 1);
    t->n[i].child[0] = c0;
    t->n[i].child[1] = c1;
    return i;
}

static void big_tree(tree *t, bits *b)
{
    memset(t, 0, sizeof *t);
    t->root = -1;
    t->last[0] = t->last[1] = t->last[2] = -1;
    if (!bit(b))
        return;
    big_ctx ctx;
    small_tree(&ctx.lo, b);
    small_tree(&ctx.hi, b);
    for (int k = 0; k < 3; k++)
        ctx.esc[k] = (int)getb(b, 16);
    t->root = big_rec(t, b, &ctx, 0);
    bit(b);
    for (int k = 0; k < 3; k++) /* escapes the tree never used get a slot of their own */
        if (t->last[k] < 0)
            t->last[k] = new_slot(t, 0);
    tree_free(&ctx.lo);
    tree_free(&ctx.hi);
}

static void big_reset(tree *t)
{
    if (t->root < 0)
        return;
    for (int k = 0; k < 3; k++)
        t->value[t->last[k]] = 0;
}

/* a code; the three cache slots then hold the last three distinct codes */
static int big_get(tree *t, bits *b)
{
    int s = walk(t, b);
    if (s < 0)
        return 0;
    int v = t->value[s];
    if (v != t->value[t->last[0]]) {
        t->value[t->last[2]] = t->value[t->last[1]];
        t->value[t->last[1]] = t->value[t->last[0]];
        t->value[t->last[0]] = v;
    }
    return v;
}

/* ---- the file ------------------------------------------------------------ */

struct smk {
    const uint8_t *d;
    size_t size;
    int w, h, frames, flags;
    double ms;
    uint32_t audio_rate[7];
    uint32_t *frame_size;
    uint8_t *frame_type;
    size_t pos; /* next frame's offset */
    int cur;
    tree mmap, mclr, full, type;
    uint8_t *pix;
    uint8_t pal[768];
    uint8_t *audio;
    size_t audio_bytes, audio_cap;
};

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

smk *smk_open(const uint8_t *d, size_t size)
{
    if (size < 104 || memcmp(d, "SMK2", 4) != 0)
        return NULL;
    smk *s = calloc(1, sizeof *s);
    if (!s)
        return NULL;
    s->d = d;
    s->size = size;
    s->w = (int)rd32(d + 4);
    s->h = (int)rd32(d + 8);
    s->frames = (int)rd32(d + 12);
    int32_t rate = (int32_t)rd32(d + 16);
    s->ms = rate > 0 ? rate : rate < 0 ? -rate / 100.0 : 100.0;
    s->flags = (int)rd32(d + 20);
    if (s->flags & 1) /* a ring frame at the end */
        s->frames++;
    uint32_t trees_size = rd32(d + 52);
    for (int i = 0; i < 7; i++)
        s->audio_rate[i] = rd32(d + 72 + 4 * i);
    size_t off = 104;
    if (s->w <= 0 || s->h <= 0 || s->frames <= 0 || off + (size_t)s->frames * 5 + trees_size > size) {
        free(s);
        return NULL;
    }
    s->frame_size = malloc((size_t)s->frames * 4);
    s->frame_type = malloc((size_t)s->frames);
    for (int i = 0; i < s->frames; i++)
        s->frame_size[i] = rd32(d + off + 4 * (size_t)i) & ~3u;
    off += (size_t)s->frames * 4;
    memcpy(s->frame_type, d + off, (size_t)s->frames);
    off += (size_t)s->frames;
    bits b = {d + off, trees_size, 0};
    big_tree(&s->mmap, &b);
    big_tree(&s->mclr, &b);
    big_tree(&s->full, &b);
    big_tree(&s->type, &b);
    s->pos = off + trees_size;
    s->pix = calloc((size_t)s->w * (size_t)s->h, 1);
    return s;
}

void smk_close(smk *s)
{
    if (!s)
        return;
    tree_free(&s->mmap);
    tree_free(&s->mclr);
    tree_free(&s->full);
    tree_free(&s->type);
    free(s->frame_size);
    free(s->frame_type);
    free(s->pix);
    free(s->audio);
    free(s);
}

int smk_width(const smk *s) { return s->w; }
int smk_height(const smk *s) { return s->h; }
int smk_frames(const smk *s) { return s->frames; }
double smk_frame_ms(const smk *s) { return s->ms; }
const uint8_t *smk_pixels(const smk *s) { return s->pix; }
const uint8_t *smk_palette(const smk *s) { return s->pal; }

void smk_audio_info(const smk *s, int *rate, int *channels, int *bits_out)
{
    uint32_t r = s->audio_rate[0];
    *rate = (r & 0x40000000u) ? (int)(r & 0xffffff) : 0;
    *channels = (r & 0x10000000u) ? 2 : 1;
    *bits_out = (r & 0x20000000u) ? 16 : 8;
}

const uint8_t *smk_audio(const smk *s, size_t *bytes)
{
    *bytes = s->audio_bytes;
    return s->audio;
}

static const uint8_t smk_pal6[64] = {
    0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30,
    0x34, 0x38, 0x3C, 0x41, 0x45, 0x49, 0x4D, 0x51, 0x55, 0x59, 0x5D, 0x61, 0x65,
    0x69, 0x6D, 0x71, 0x75, 0x79, 0x7D, 0x82, 0x86, 0x8A, 0x8E, 0x92, 0x96, 0x9A,
    0x9E, 0xA2, 0xA6, 0xAA, 0xAE, 0xB2, 0xB6, 0xBA, 0xBE, 0xC3, 0xC7, 0xCB, 0xCF,
    0xD3, 0xD7, 0xDB, 0xDF, 0xE3, 0xE7, 0xEB, 0xEF, 0xF3, 0xF7, 0xFB, 0xFF};

static void palette(smk *s, const uint8_t *p, size_t len)
{
    uint8_t old[768];
    memcpy(old, s->pal, sizeof old);
    size_t i = 0;
    int pos = 0;
    while (i < len && pos < 256) {
        uint8_t t = p[i++];
        if (t & 0x80) { /* keep (t & 0x7f) + 1 entries */
            pos += (t & 0x7f) + 1;
        } else if (t & 0x40) { /* copy (t & 0x3f) + 1 entries of the old palette from `off` */
            if (i >= len)
                break;
            int off = p[i++];
            int n = (t & 0x3f) + 1;
            while (n-- && pos < 256 && off < 256) {
                memcpy(s->pal + 3 * pos, old + 3 * off, 3);
                pos++;
                off++;
            }
        } else { /* one colour, 6 bits per component */
            if (i + 2 > len)
                break;
            s->pal[3 * pos] = smk_pal6[t & 0x3f];
            s->pal[3 * pos + 1] = smk_pal6[p[i++] & 0x3f];
            s->pal[3 * pos + 2] = smk_pal6[p[i++] & 0x3f];
            pos++;
        }
    }
}

static void put_audio(smk *s, const void *src, size_t n)
{
    if (s->audio_bytes + n > s->audio_cap) {
        s->audio_cap = (s->audio_bytes + n) * 2;
        s->audio = realloc(s->audio, s->audio_cap);
    }
    memcpy(s->audio + s->audio_bytes, src, n);
    s->audio_bytes += n;
}

static void audio(smk *s, const uint8_t *p, size_t len)
{
    if ((s->audio_rate[0] & 0x80000000u) == 0) { /* uncompressed */
        put_audio(s, p, len);
        return;
    }
    if (len < 4)
        return;
    uint32_t unpacked = rd32(p);
    bits b = {p + 4, len - 4, 0};
    if (!bit(&b))
        return;
    int stereo = bit(&b), sixteen = bit(&b);
    tree t[4];
    int ntrees = 1 << (sixteen + stereo);
    for (int i = 0; i < ntrees; i++)
        small_tree(&t[i], &b);
    if (sixteen) {
        int pred[2] = {0, 0};
        for (int i = stereo; i >= 0; i--) {
            unsigned v = getb(&b, 16);
            pred[i] = (int16_t)(((v & 0xff) << 8) | (v >> 8)); /* stored high byte first */
        }
        size_t n = unpacked / 2;
        int16_t *out = malloc(n * 2 + 4);
        size_t k = 0;
        for (int i = 0; i <= stereo && k < n; i++)
            out[k++] = (int16_t)pred[i];
        for (; k < n; k++) {
            int ch = (int)(k & (size_t)stereo);
            int lo = small_get(&t[2 * ch], &b), hi = small_get(&t[2 * ch + 1], &b);
            pred[ch] = (int16_t)(pred[ch] + (int16_t)(lo | hi << 8));
            out[k] = (int16_t)pred[ch];
        }
        put_audio(s, out, n * 2);
        free(out);
    } else {
        int pred[2] = {0, 0};
        for (int i = stereo; i >= 0; i--)
            pred[i] = (int)getb(&b, 8);
        size_t n = unpacked;
        uint8_t *out = malloc(n + 2);
        size_t k = 0;
        for (int i = 0; i <= stereo && k < n; i++)
            out[k++] = (uint8_t)pred[i];
        for (; k < n; k++) {
            int ch = (int)(k & (size_t)stereo);
            pred[ch] = (uint8_t)(pred[ch] + (int8_t)small_get(&t[ch], &b));
            out[k] = (uint8_t)pred[ch];
        }
        put_audio(s, out, n);
        free(out);
    }
    for (int i = 0; i < ntrees; i++)
        tree_free(&t[i]);
}

static const int block_runs[64] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,  16,  17,   18,   19,   20,  21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37,  38,  39,   40,   41,   42,  43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048};

static void video(smk *s, const uint8_t *p, size_t len)
{
    bits b = {p, len, 0};
    big_reset(&s->mmap);
    big_reset(&s->mclr);
    big_reset(&s->full);
    big_reset(&s->type);
    int bw = s->w / 4, bh = s->h / 4, blocks = bw * bh, blk = 0, stride = s->w;
    while (blk < blocks) {
        int type = big_get(&s->type, &b);
        int run = block_runs[(type >> 2) & 0x3f];
        switch (type & 3) {
        case 0: /* mono: two colours by a 16-bit map */
            while (run-- && blk < blocks) {
                int clr = big_get(&s->mclr, &b), map = big_get(&s->mmap, &b);
                uint8_t *o = s->pix + (blk / bw) * stride * 4 + (blk % bw) * 4;
                uint8_t hi = (uint8_t)(clr >> 8), lo = (uint8_t)clr;
                for (int i = 0; i < 4; i++, o += stride, map >>= 4)
                    for (int j = 0; j < 4; j++)
                        o[j] = (map >> j) & 1 ? hi : lo;
                blk++;
            }
            break;
        case 1: /* full: sixteen colours, two at a time, right pair first */
            while (run-- && blk < blocks) {
                uint8_t *o = s->pix + (blk / bw) * stride * 4 + (blk % bw) * 4;
                for (int i = 0; i < 4; i++, o += stride) {
                    int px = big_get(&s->full, &b);
                    o[2] = (uint8_t)px;
                    o[3] = (uint8_t)(px >> 8);
                    px = big_get(&s->full, &b);
                    o[0] = (uint8_t)px;
                    o[1] = (uint8_t)(px >> 8);
                }
                blk++;
            }
            break;
        case 2: /* skip: keep the previous frame */
            while (run-- && blk < blocks)
                blk++;
            break;
        case 3: /* fill with one colour */
            while (run-- && blk < blocks) {
                uint8_t *o = s->pix + (blk / bw) * stride * 4 + (blk % bw) * 4;
                for (int i = 0; i < 4; i++, o += stride)
                    memset(o, type >> 8, 4);
                blk++;
            }
            break;
        }
    }
}

int smk_next(smk *s)
{
    if (s->cur >= s->frames)
        return -1;
    size_t fsize = s->frame_size[s->cur];
    uint8_t ftype = s->frame_type[s->cur];
    if (s->pos + fsize > s->size)
        return -1;
    const uint8_t *p = s->d + s->pos, *end = p + fsize;
    s->audio_bytes = 0;
    if (ftype & 1) { /* palette: first byte is its length in 4-byte units */
        size_t len = (size_t)p[0] * 4;
        if (len == 0 || p + len > end)
            return -1;
        palette(s, p + 1, len - 1);
        p += len;
    }
    for (int t = 0; t < 7; t++) {
        if (!(ftype & (2 << t)))
            continue;
        if (p + 4 > end)
            return -1;
        size_t len = rd32(p);
        if (len < 4 || p + len > end)
            return -1;
        if (t == 0)
            audio(s, p + 4, len - 4);
        p += len;
    }
    video(s, p, (size_t)(end - p));
    s->pos += fsize;
    s->cur++;
    return 0;
}

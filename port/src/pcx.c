/* ZSoft PCX decoder, 8-bit single-plane (everything the game ships). */
#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PCX_HEADER 128
#define PCX_PALETTE_TAIL 769 /* 0x0C marker + 256*3 */

void a2_image_free(a2_image *im)
{
    free(im->rgba);
    memset(im, 0, sizeof(*im));
}

static int pcx_geometry(const uint8_t *d, size_t size, int *w, int *h, int *bpp, int *planes,
                        int *bpl)
{
    if (size < PCX_HEADER || d[0] != 0x0A)
        return 0;
    int xmin = d[4] | (d[5] << 8);
    int ymin = d[6] | (d[7] << 8);
    int xmax = d[8] | (d[9] << 8);
    int ymax = d[10] | (d[11] << 8);
    *bpp = d[3];
    *planes = d[65];
    *bpl = d[66] | (d[67] << 8);
    *w = xmax - xmin + 1;
    *h = ymax - ymin + 1;
    return (*w > 0 && *h > 0 && *bpl > 0);
}

int a2_pcx_palette(uint8_t out[768], const uint8_t *data, size_t size)
{
    if (size < PCX_PALETTE_TAIL)
        return -1;
    const uint8_t *tail = data + size - PCX_PALETTE_TAIL;
    if (tail[0] != 0x0C)
        return -1;
    memcpy(out, tail + 1, 768);
    return 0;
}

/* Decode the RLE body into `bpl`-wide rows of 8-bit indices.  Returns the
 * rows (malloc'd) or NULL, and the geometry through the pointers. */
static uint8_t *pcx_rows(const uint8_t *data, size_t size, const char *name, int *pw, int *ph,
                         int *pbpl)
{
    int w, h, bpp, planes, bpl;
    if (!pcx_geometry(data, size, &w, &h, &bpp, &planes, &bpl)) {
        fprintf(stderr, "pcx %s: bad header\n", name ? name : "?");
        return NULL;
    }
    if (bpp != 8 || planes != 1) {
        fprintf(stderr, "pcx %s: unsupported %d bpp x %d planes\n", name ? name : "?", bpp, planes);
        return NULL;
    }
    if (size < PCX_HEADER + PCX_PALETTE_TAIL) {
        fprintf(stderr, "pcx %s: missing palette\n", name ? name : "?");
        return NULL;
    }
    size_t body_end = size - PCX_PALETTE_TAIL;
    uint8_t *rows = calloc((size_t)bpl * (size_t)h, 1);
    if (!rows)
        return NULL;

    size_t pos = PCX_HEADER;
    for (int y = 0; y < h; y++) {
        uint8_t *row = rows + (size_t)y * bpl;
        int x = 0;
        while (x < bpl) {
            if (pos >= body_end)
                goto truncated;
            uint8_t c = data[pos++];
            if ((c & 0xC0) == 0xC0) {
                int n = c & 0x3F;
                if (pos >= body_end)
                    goto truncated;
                uint8_t val = data[pos++];
                while (n-- > 0 && x < bpl)
                    row[x++] = val;
            } else {
                row[x++] = c;
            }
        }
    }
    *pw = w;
    *ph = h;
    *pbpl = bpl;
    return rows;

truncated:
    fprintf(stderr, "pcx %s: truncated RLE\n", name ? name : "?");
    free(rows);
    return NULL;
}

int a2_pcx_decode(a2_image *im, const uint8_t *data, size_t size, const char *name)
{
    memset(im, 0, sizeof(*im));
    uint8_t pal[768];
    if (a2_pcx_palette(pal, data, size) != 0) {
        fprintf(stderr, "pcx %s: missing palette\n", name ? name : "?");
        return -1;
    }
    int w, h, bpl;
    uint8_t *rows = pcx_rows(data, size, name, &w, &h, &bpl);
    if (!rows)
        return -1;
    im->rgba = malloc((size_t)w * h * 4);
    if (!im->rgba) {
        free(rows);
        return -1;
    }
    for (int y = 0; y < h; y++) {
        const uint8_t *row = rows + (size_t)y * bpl;
        uint8_t *dst = im->rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            uint8_t idx = row[x];
            dst[x * 4 + 0] = pal[idx * 3 + 0];
            dst[x * 4 + 1] = pal[idx * 3 + 1];
            dst[x * 4 + 2] = pal[idx * 3 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
    free(rows);
    im->width = w;
    im->height = h;
    return 0;
}

uint8_t *a2_pcx_decode_indexed(const uint8_t *data, size_t size, const char *name, int *w,
                               int *h)
{
    int bpl;
    uint8_t *rows = pcx_rows(data, size, name, w, h, &bpl);
    if (!rows || bpl == *w)
        return rows;
    for (int y = 1; y < *h; y++) /* pack the padded rows */
        memmove(rows + (size_t)y * *w, rows + (size_t)y * bpl, (size_t)*w);
    return rows;
}

void a2_image_key_transparent(a2_image *im)
{
    size_t n = (size_t)im->width * im->height;
    for (size_t i = 0; i < n; i++) {
        uint8_t *p = im->rgba + i * 4;
        int keyed = (p[0] >= 240 && p[1] <= 16 && p[2] >= 112 && p[2] <= 144) || /* 255,0,127 */
                    (p[0] >= 240 && p[1] <= 16 && p[2] >= 240);                  /* pure magenta */
        if (keyed)
            p[3] = 0;
    }
}

void a2_image_bleed_keyed(a2_image *im, int passes)
{
    /* With bilinear filtering, texels next to a keyed pixel blend in its
     * colour and leave a pink fringe.  Push neighbouring colour into the keyed
     * texels (keeping alpha 0) so the blend has something sane to pick up. */
    int w = im->width, h = im->height;
    for (int pass = 0; pass < passes; pass++) {
        int changed = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t *p = im->rgba + ((size_t)y * w + x) * 4;
                if (p[3] != 0)
                    continue;
                int sum[3] = {0, 0, 0}, n = 0;
                static const int dx[4] = {1, -1, 0, 0};
                static const int dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                        continue;
                    const uint8_t *q = im->rgba + ((size_t)ny * w + nx) * 4;
                    if (q[3] == 0)
                        continue;
                    sum[0] += q[0];
                    sum[1] += q[1];
                    sum[2] += q[2];
                    n++;
                }
                if (n) {
                    p[0] = (uint8_t)(sum[0] / n);
                    p[1] = (uint8_t)(sum[1] / n);
                    p[2] = (uint8_t)(sum[2] / n);
                    changed = 1;
                }
            }
        }
        if (!changed)
            break;
    }
}

int a2_image_decode(a2_image *im, const uint8_t *data, size_t size, const char *name)
{
    if (size >= 1 && data[0] == 0x0A)
        return a2_pcx_decode(im, data, size, name);
    return a2_tga_decode(im, data, size, name);
}

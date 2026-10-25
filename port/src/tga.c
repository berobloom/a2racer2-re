/* Truevision TGA decoder: uncompressed (type 2) and RLE (type 10) true-colour.
 * Every TGA the game ships is type 2, 24 bpp (one is 32 bpp). */
#include "image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int a2_tga_decode(a2_image *im, const uint8_t *data, size_t size, const char *name)
{
    memset(im, 0, sizeof(*im));
    if (size < 18) {
        fprintf(stderr, "tga %s: too short\n", name ? name : "?");
        return -1;
    }
    unsigned idlen = data[0];
    unsigned cmaptype = data[1];
    unsigned imgtype = data[2];
    unsigned cmaplen = (unsigned)(data[5] | (data[6] << 8));
    unsigned cmapbits = data[7];
    int w = data[12] | (data[13] << 8);
    int h = data[14] | (data[15] << 8);
    unsigned bpp = data[16];
    unsigned desc = data[17];

    if (cmaptype != 0 || (imgtype != 2 && imgtype != 10) || (bpp != 24 && bpp != 32)) {
        fprintf(stderr, "tga %s: unsupported (type %u, cmap %u, %u bpp)\n", name ? name : "?",
                imgtype, cmaptype, bpp);
        return -1;
    }
    if (w <= 0 || h <= 0) {
        fprintf(stderr, "tga %s: bad size %dx%d\n", name ? name : "?", w, h);
        return -1;
    }

    size_t off = 18 + idlen + (size_t)cmaplen * (cmapbits / 8);
    size_t nchan = bpp / 8;
    size_t npx = (size_t)w * h;

    uint8_t *px = malloc(npx * nchan);
    if (!px)
        return -1;

    if (imgtype == 2) {
        if (size < off || size - off < npx * nchan) {
            fprintf(stderr, "tga %s: truncated\n", name ? name : "?");
            free(px);
            return -1;
        }
        memcpy(px, data + off, npx * nchan);
    } else {
        size_t got = 0;
        while (got < npx) {
            if (off >= size)
                goto truncated;
            uint8_t pkt = data[off++];
            size_t run = (size_t)(pkt & 0x7F) + 1;
            if (run > npx - got)
                run = npx - got;
            if (pkt & 0x80) {
                if (size - off < nchan)
                    goto truncated;
                for (size_t k = 0; k < run; k++)
                    memcpy(px + (got + k) * nchan, data + off, nchan);
                off += nchan;
            } else {
                if (size - off < run * nchan)
                    goto truncated;
                memcpy(px + got * nchan, data + off, run * nchan);
                off += run * nchan;
            }
            got += run;
        }
    }

    im->rgba = malloc(npx * 4);
    if (!im->rgba) {
        free(px);
        return -1;
    }
    int top_down = (desc & 0x20) != 0;
    /* The low nibble of the descriptor is the alpha bit count.  E04_vang.TGA,
     * the one 32 bpp file, declares 0 and its fourth byte is 0 everywhere;
     * taking that as alpha made Rotterdam's whole rail-plane atlas -- its
     * building facades -- invisible. */
    int has_alpha = nchan == 4 && (desc & 0x0F) != 0;
    for (int y = 0; y < h; y++) {
        int src = top_down ? y : (h - 1 - y);
        const uint8_t *s = px + (size_t)src * w * nchan;
        uint8_t *d = im->rgba + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            /* TGA stores BGR(A). */
            d[x * 4 + 0] = s[x * nchan + 2];
            d[x * 4 + 1] = s[x * nchan + 1];
            d[x * 4 + 2] = s[x * nchan + 0];
            d[x * 4 + 3] = has_alpha ? s[x * nchan + 3] : 255;
        }
    }
    free(px);
    im->width = w;
    im->height = h;
    return 0;

truncated:
    fprintf(stderr, "tga %s: truncated RLE\n", name ? name : "?");
    free(px);
    return -1;
}

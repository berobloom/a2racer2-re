/* smkdump -- decode a Smacker file with msmacker.c, for checking it against
 * another decoder (ffmpeg's): every frame as <outdir>/NNN.ppm and track 0's
 * audio, raw, to <audio.raw>.
 *
 *   smkdump <file.smk> <outdir> <audio.raw>
 */
#include "msmacker.h"

#include "../a2_io.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: smkdump <file.smk> <outdir> <audio.raw>\n");
        return 1;
    }
    size_t n = 0;
    uint8_t *d = a2_read_file(argv[1], &n);
    smk *s = d ? smk_open(d, n) : NULL;
    if (!s) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    int rate, ch, bits;
    smk_audio_info(s, &rate, &ch, &bits);
    fprintf(stderr, "%dx%d, %d frames, %.2f ms each, audio %d Hz %d ch %d bit\n", smk_width(s),
            smk_height(s), smk_frames(s), smk_frame_ms(s), rate, ch, bits);
    FILE *raw = fopen(argv[3], "wb");
    int f = 0;
    while (smk_next(s) == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/%03d.ppm", argv[2], ++f);
        FILE *o = fopen(path, "wb");
        if (!o)
            return 1;
        fprintf(o, "P6\n%d %d\n255\n", smk_width(s), smk_height(s));
        const uint8_t *px = smk_pixels(s), *pal = smk_palette(s);
        for (int i = 0; i < smk_width(s) * smk_height(s); i++)
            fwrite(pal + 3 * px[i], 1, 3, o);
        fclose(o);
        size_t ab;
        const uint8_t *a = smk_audio(s, &ab);
        if (raw)
            fwrite(a, 1, ab, raw);
    }
    if (raw)
        fclose(raw);
    fprintf(stderr, "decoded %d frames\n", f);
    smk_close(s);
    free(d);
    return 0;
}

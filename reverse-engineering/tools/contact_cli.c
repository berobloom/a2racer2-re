/* Feeds port/src/contact.c the cases fit_contact.py writes (one per line:
 * ax0 az0 ax1 az1 ax az ah  bx0 bz0 bx1 bz1 bx bz bh  mx mz) and prints
 * "hit t corner side" for each.  Build:
 *   cc -O2 -I port/src reverse-engineering/tools/contact_cli.c port/src/contact.c -lm */
#include "contact.h"
#include <stdio.h>
int main(void)
{
    a2_cbox a, b;
    float mx, mz;
    while (scanf("%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", &a.x0, &a.z0, &a.x1, &a.z1,
                 &a.x, &a.z, &a.heading, &b.x0, &b.z0, &b.x1, &b.z1, &b.x, &b.z, &b.heading, &mx,
                 &mz) == 16) {
        float t = 1.0f;
        int c = 0, s = 0;
        int h = a2_contact_sweep(&a, &b, mx, mz, &t, &c, &s);
        printf("%d %.5f %d %d\n", h, t, c, s);
    }
    return 0;
}

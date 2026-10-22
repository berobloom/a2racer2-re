/* The engine's contact test between two boxes, after spel.dat's collision
 * code (session 14; open question 53).
 *
 * A collision entry is a box: the mesh's bottom-face corners in its own
 * frame, a position and a heading (4096 a turn).  0x44a520 sweeps the four
 * corners of one box B along a move, in the frame of another box A, and
 * returns the earliest time (0..1 of the move) at which a corner enters A,
 * with that corner and the side of A it crossed.  0x44a0f0 runs it twice per
 * pair -- the moving object's corners into the other box, and the other's
 * corners into the moving box against the move -- and keeps the earlier hit;
 * 0x44b860 gives the crossed side's outward normal.  Checked against the
 * original under Unicorn (reverse-engineering/tools/fit_contact.py).
 */
#ifndef A2_CONTACT_H
#define A2_CONTACT_H

typedef struct a2_cbox {
    float x0, z0, x1, z1; /* the box in its own frame: x across, z forward */
    float x, z;           /* its position, world units */
    float heading;        /* 4096 a turn, the engine's convention */
    int fixed;            /* static (entry +0x48 = 0): only the .cmp rail slabs */
} a2_cbox;

/* 0x44a520: B's corners, moved by (mx, mz) given in A's frame, against A.
 * Returns 1 on a hit with *t in 0..1, the corner of B (0-3: x0z0, x1z0, x1z1,
 * x0z1) and the side of A (0 -z, 1 +x, 2 +z, 3 -x); 0 for none. */
int a2_contact_sweep(const a2_cbox *a, const a2_cbox *b, float mx, float mz, float *t,
                     int *corner, int *side);

/* 0x44a0f0 for one pair: `self` moved by (dx, dz) in the world this frame,
 * `other` held still.  On a hit returns 1 and gives the time, the contact
 * point in the world and the normal pointing from `self` into `other` (the
 * direction `self` is pushed back against).  Against a fixed box the
 * second pass keeps the original's slip (session 16): the point is the
 * other's raw corner taken in `self`'s frame, so the lever arm the impulse
 * gets is the slab's local corner (0x44a628, read by 0x449570). */
int a2_contact(const a2_cbox *self, float dx, float dz, const a2_cbox *other, float *t,
               float point[2], float normal[2]);

#endif

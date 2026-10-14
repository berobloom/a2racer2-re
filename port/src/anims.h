/* Anim table reader.
 *
 * The game binds a texture to a mesh through its *anim* system, and the tables
 * live as static data inside `spel.dat` rather than in any data file.  Each
 * `.omp` scenery object names an anim ("A_LNTRN0_ANIM"); the anim lists frames,
 * and each frame pairs a mesh with a texture.
 *
 *   anim3d   VA 0x492050, 312 x  9 B: i32 frame_count; i32 *frame_indices; u8 flag
 *   frame3d  VA 0x48E5E8, 386 x 16 B: i32 mesh_handle(-1); i32 texture_handle(-1);
 *                                     i32 mesh_index; i32 texture_index
 *   names    parallel arrays of 12-byte { u32; char *name; u32(-1) } records
 *
 * Evidence and the derivation of the 9-byte stride are in
 * research/discoveries.md.  The offsets are specific to the retail
 * `spel.dat` (MD5 556416bfff29667e455d8b0fc246b75c), so the reader validates
 * what it finds and simply reports failure on anything else -- the viewer then
 * falls back to untextured meshes.
 */
#ifndef A2_ANIMS_H
#define A2_ANIMS_H

#include <stddef.h>

typedef struct {
    const char *mesh;    /* e.g. "A_LNTRN0.d3d", may be NULL */
    const char *texture; /* e.g. "a_trffc1.pcx", may be NULL */
} a2_anim_frame;

typedef struct a2_anims a2_anims;

/* Load the tables out of <datadir>/spel.dat.  Returns NULL on any failure
 * (message on stderr); the caller can carry on without textures. */
a2_anims *a2_anims_load(const char *datadir);
void a2_anims_free(a2_anims *a);

/* Look up an anim by name (case-insensitive).  Returns its frame array and
 * count, or NULL when the anim is unknown. */
const a2_anim_frame *a2_anims_find(const a2_anims *a, const char *name, int *out_count);

int a2_anims_count(const a2_anims *a);

#endif /* A2_ANIMS_H */

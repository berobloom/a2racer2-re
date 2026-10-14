/* `.d3d` mesh loader (Davilex dplib mesh format).
 *
 *   u32 vertexCount;
 *   u32 triangleCount;
 *   { float x,y,z,nx,ny,nz,tu,tv; u8 pad; } vertices [vertexCount];  // 33 B
 *   { u16 a,b,c;                  u8 pad[3]; } triangles[triangleCount]; // 9 B
 *
 * The trailing pad bytes are dead: the original loader reads them and discards
 * them (it reads 33 bytes per vertex and copies 32, and 9 per triangle keeping
 * the leading 3 u16).  The 8 floats are exactly a D3DVERTEX.
 */
#ifndef A2_MESH_H
#define A2_MESH_H

#include <stddef.h>
#include <stdint.h>

#define A2_MESH_VERTEX_LEN 33
#define A2_MESH_TRI_LEN 9

typedef struct {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
} a2_vertex;

typedef struct {
    uint16_t a, b, c;
} a2_tri;

typedef struct {
    a2_vertex *vertices;
    a2_tri *triangles;
    uint32_t vertex_count;
    uint32_t triangle_count;
} a2_mesh;

/* Parse a .d3d blob.  Returns 0 on success.  `name` is used in error messages.
 *
 * One shipped entry (RCS.IMG "E04_koor.D3D") has an archive offset 24 bytes too
 * low, so the blob starts with stray leading bytes; the header is relocated by
 * searching for a shift whose counts account for the remaining bytes exactly. */
int a2_mesh_parse(a2_mesh *m, const uint8_t *data, size_t size, const char *name);
void a2_mesh_free(a2_mesh *m);

#endif /* A2_MESH_H */

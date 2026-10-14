#include "render.h"

#include "anims.h"
#include "gfx.h"
#include "contact.h"
#include "image.h"
#include "mesh.h"

#include <GL/gl.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
    float x, y, z;
    float u, v;
} vtx;

/* One plane list (road / land / list2 / list3) flattened across all chunks. */
typedef struct {
    vtx *verts;
    uint32_t *indices;
    uint32_t vcount, icount;
    uint32_t *first; /* per chunk: first index */
    uint32_t *count; /* per chunk: index count */
    unsigned tex;
    int tex_owned;
} plane_batch;

typedef struct {
    vtx *verts;
    uint8_t *colors; /* RGBA, cheap per-vertex diffuse so models read as 3D */
    uint32_t *indices;
    uint32_t vcount, icount;
    unsigned tex; /* from the anim's frame; 0 = use the placeholder */
    /* Bounding box in model space, for scenery collision.  `radius` is the
     * horizontal half-diagonal, `height` the Y extent. */
    float min_x, min_y, min_z, max_x, max_y, max_z;
    float radius, height;
} gmesh;

#define A2_MAX_ANIM_FRAMES 4

typedef struct {
    int mesh;
    int chunk;
    float x, y, z, yaw;
    /* Extra frames of the object's anim, when it has more than one.  Only five
     * props in the whole game do: the two traffic lights (3 frames each) and
     * the three roadworks arrows (2 each).  See a2_scene_set_anim_time. */
    int frames[A2_MAX_ANIM_FRAMES];
    int frame_count;
    int solid; /* participates in scenery collision; see a2_scene_collide_objects */
    /* Knock-over props (a2_scene_props_step): the .omp type, the phase the
     * original keeps at object +0x10 (0 standing, 1 flying, 2 down), the
     * pending sample at +0x14, the velocity +0x6c..+0x74 in units per frame,
     * the gravity +0x30, and the extra rotations in degrees. */
    int type, phase, pending;
    float vx, vy, vz, g;
    float rx, ry, rz;
    float y0; /* where it stood */
    int hidden; /* drawn by a module instead (the type-9 cops) */
    float spin; /* pushable scenery: yaw rate, rad/s (velocity in vx / vz, m/s) */
} instance;

struct a2_scene {
    plane_batch lists[A2_RMP_LISTS];
    uint32_t chunk_count;
    float *chunk_x, *chunk_y, *chunk_z;
    /* each chunk's ground plane, nx ny nz d (0x442cb0, read by 0x442f00) */
    float *chunk_plane;
    /* a circuit: the last chunk runs on into chunk 0 */
    int closed;

    gmesh *meshes;
    char (*mesh_names)[A2_OMP_NAME_LEN + 8];
    int mesh_count, mesh_cap;
    unsigned mesh_tex;

    unsigned shared_tex;

    /* name -> GL texture cache, so an atlas shared by many meshes is uploaded
       once (cars01.pcx alone backs every traffic car). */
    char (*tex_names)[64];
    unsigned *tex_ids;
    int tex_count, tex_cap;

    /* .cmp rails, with the per-chunk index lists from .pmp. */
    a2_rail *rails;
    uint32_t rail_count;
    a2_waypoint *waypoints;
    uint32_t waypoint_count;
    uint32_t (*chunk_wp)[A2_CHUNK_WP_MAX];
    uint32_t *chunk_wp_count;
    uint32_t **chunk_rails;
    uint32_t *chunk_rail_count;

    instance *instances;
    int instance_count;
    float anim_time;       /* seconds, for the animated props */
    unsigned shadow_tex;   /* S_SHADOW_ANIM's texture, 0 until loaded */
    int *chunk_inst_first; /* instances sorted by chunk */
    int *chunk_inst_count;
};

/* ------------------------------------------------------------- textures */

/* Which texture each .rmp plane list uses.
 *
 * CONFIRMED mechanism (session 6): textures are bound through the anim system.
 * Every track has an `E0x_ALGEMENE_TEXTURES_ANIM` in spel.dat's static anim
 * table, and each of them has exactly **four** frames -- matching the four
 * plane lists -- in the same order on all six tracks:
 *
 *     [0] e0x_gras   [1] e0x_gra2   [2] e0x_weg   [3] e0x_vang
 *
 * CONFIRMED order (session 7): `Raser.ini`'s `[ETAPPES]` table names the four
 * textures per track **in plane-list order**, which settles what was open
 * question #31:
 *
 *     3=0,etappe3,e03_weg,e03_gras,e03_gra2,e03_vang,scrl_lo,scrl_hi,...
 *
 * so list 0 -> _weg, list 1 -> _gras, list 2 -> _gra2, list 3 -> _vang.  (The
 * anim table's own frame order is gras/gra2/weg/vang, which is NOT the list
 * order -- binding by frame index would mis-assign all four.)
 *
 * What the atlases actually hold, read off the decoded images:
 *   _weg   road surface        _gras  ground/grass
 *   _gra2  building facades    _vang  guardrails, viaduct spans, treelines,
 *                                     fences  (all colour-keyed)
 * An earlier note here had _gra2 and _vang the wrong way round, which drew
 * buildings on the guardrail planes and viaducts on the building planes.
 *
 * The shipped archives hold both .pcx and .tga copies of each; the .tga is
 * true-colour so it is preferred. */
/* The game names all four lists itself, in its own overflow messages:
 * "Too many road-planes", "land-planes", "extra-planes", "rail-planes", in
 * this order -- which lines up with the Raser.ini [MAPS] suffixes weg / gras /
 * gra2 / vang. */
static const char *const list_texture_suffix[A2_RMP_LISTS][4] = {
    {"_Weg.tga", "_weg.tga", "_weg.pcx", NULL},    /* list 0: road-planes */
    {"_gras.tga", "_Gras.tga", "_gras.pcx", NULL}, /* list 1: land-planes */
    {"_gra2.tga", "_Gra2.tga", "_gra2.pcx", NULL}, /* list 2: extra-planes */
    {"_vang.tga", "_Vang.tga", "_vang.pcx", NULL}, /* list 3: rail-planes  */
};

static unsigned load_texture_named(const a2_archive *rcs, const char *name)
{
    long idx = a2_archive_find(rcs, name);
    if (idx < 0)
        return 0;
    uint32_t size = 0;
    const uint8_t *data = a2_archive_data(rcs, (size_t)idx, &size);
    a2_image im;
    if (!data || a2_image_decode(&im, data, size, name) != 0)
        return 0;
    a2_image_key_transparent(&im);
    a2_image_bleed_keyed(&im, 2);
    a2_gfx_gamma_rgba(im.rgba, (size_t)im.width * im.height);
    unsigned tex = a2_gfx_texture_from_image(&im, 0);
    fprintf(stderr, "texture: %s (%dx%d)\n", name, im.width, im.height);
    a2_image_free(&im);
    return tex;
}

/* Load a texture named by Raser.ini, which gives a stem such as "e00_gra2"
 * (the original appends ".pcx"; the archives also hold a true-colour ".TGA"
 * twin, which the game uses in 16-bit mode, so that is tried first). */
static unsigned load_texture_stem(const a2_archive *rcs, const char *stem)
{
    char base[64];
    snprintf(base, sizeof base, "%s", stem);
    char *dot = strrchr(base, '.');
    if (dot)
        *dot = '\0';
    static const char *const ext[] = {".tga", ".TGA", ".pcx", ".PCX", NULL};
    for (int i = 0; ext[i]; i++) {
        char name[80];
        snprintf(name, sizeof name, "%s%s", base, ext[i]);
        unsigned t = load_texture_named(rcs, name);
        if (t)
            return t;
    }
    return 0;
}
static unsigned load_texture(const a2_archive *rcs, const char *prefix, const char *const *sufs)
{
    for (int i = 0; sufs[i]; i++) {
        char name[64];
        snprintf(name, sizeof name, "%s%s", prefix, sufs[i]);
        long idx = a2_archive_find(rcs, name);
        if (idx < 0)
            continue;
        uint32_t size = 0;
        const uint8_t *data = a2_archive_data(rcs, (size_t)idx, &size);
        a2_image im;
        if (!data || a2_image_decode(&im, data, size, name) != 0)
            continue;
        a2_image_key_transparent(&im);
        a2_image_bleed_keyed(&im, 2);
        a2_gfx_gamma_rgba(im.rgba, (size_t)im.width * im.height);
        unsigned tex = a2_gfx_texture_from_image(&im, 0);
        a2_image_free(&im);
        fprintf(stderr, "texture: %s\n", name);
        return tex;
    }
    return 0;
}

/* ---------------------------------------------------------- plane lists */

static int build_list(plane_batch *b, const a2_track *tr, int list)
{
    uint32_t n = tr->rmp.chunk_count;
    uint32_t planes = 0;
    for (uint32_t i = 0; i < n; i++)
        planes += tr->rmp.chunks[i].counts[list];

    b->verts = calloc(planes ? planes * 4 : 1, sizeof(vtx));
    b->indices = calloc(planes ? planes * 6 : 1, sizeof(uint32_t));
    b->first = calloc(n ? n : 1, sizeof(uint32_t));
    b->count = calloc(n ? n : 1, sizeof(uint32_t));
    if (!b->verts || !b->indices || !b->first || !b->count)
        return -1;

    for (uint32_t i = 0; i < n; i++) {
        const a2_rmp_chunk *ch = &tr->rmp.chunks[i];
        b->first[i] = b->icount;
        for (uint32_t p = 0; p < ch->counts[list]; p++) {
            const a2_plane *pl = &ch->planes[list][p];
            uint32_t base = b->vcount;
            for (int k = 0; k < 4; k++) {
                vtx *v = &b->verts[b->vcount++];
                v->x = pl->v[k].x;
                v->y = pl->v[k].y;
                v->z = pl->v[k].z;
                v->u = pl->v[k].u;
                v->v = pl->v[k].v;
            }
            for (int t = 0; t < 2; t++)
                for (int k = 0; k < 3; k++) {
                    uint32_t li = pl->tri[t][k];
                    b->indices[b->icount++] = base + (li < 4 ? li : 0);
                }
        }
        b->count[i] = b->icount - b->first[i];
    }
    return 0;
}

static void free_list(plane_batch *b)
{
    free(b->verts);
    free(b->indices);
    free(b->first);
    free(b->count);
    if (b->tex_owned)
        a2_gfx_delete_texture(b->tex);
    memset(b, 0, sizeof(*b));
}

/* ---------------------------------------------------------- .omp meshes */

/* Look up (or upload) a texture by its archive name. */
static unsigned cached_texture(a2_scene *s, const a2_archive *rcs, const char *name)
{
    if (!name || !*name)
        return 0;
    for (int i = 0; i < s->tex_count; i++)
        if (strcasecmp(s->tex_names[i], name) == 0)
            return s->tex_ids[i];
    if (s->tex_count == s->tex_cap)
        return 0;

    /* The registry names .pcx, but the archives also carry a true-colour .tga
     * of the same base; prefer that. */
    char cand[64];
    snprintf(cand, sizeof cand, "%s", name);
    char *dot = strrchr(cand, '.');
    unsigned tex = 0;
    if (dot) {
        strcpy(dot, ".tga");
        tex = load_texture_named(rcs, cand);
    }
    if (!tex)
        tex = load_texture_named(rcs, name);

    snprintf(s->tex_names[s->tex_count], sizeof s->tex_names[s->tex_count], "%s", name);
    s->tex_ids[s->tex_count] = tex;
    s->tex_count++;
    return tex;
}

/* Resolve an .omp object's anim name to its frame-0 mesh and texture.
 * The anim tables live in spel.dat (see anims.h); without them we fall back to
 * stripping the "_ANIM" suffix, which happens to name the mesh for all 1860
 * objects in the shipped tracks but says nothing about the texture. */
static void resolve_anim(const a2_anims *anims, const char *anim_name, char *mesh_out,
                         size_t mesh_len, const char **tex_out)
{
    *tex_out = NULL;
    if (anims) {
        int n = 0;
        const a2_anim_frame *fr = a2_anims_find(anims, anim_name, &n);
        if (fr && n > 0 && fr[0].mesh) {
            snprintf(mesh_out, mesh_len, "%s", fr[0].mesh);
            *tex_out = fr[0].texture;
            return;
        }
    }
    size_t n = strlen(anim_name);
    if (n > 5 && strcasecmp(anim_name + n - 5, "_ANIM") == 0)
        n -= 5;
    if (n >= mesh_len - 5)
        n = mesh_len - 5;
    memcpy(mesh_out, anim_name, n);
    strcpy(mesh_out + n, ".d3d");
}

static int find_or_load_mesh(a2_scene *s, const a2_archive *rcs, const char *mesh_name,
                             const char *tex_name)
{
    /* Key on mesh+texture: the same mesh can appear under anims with different
     * textures, and the pair is what actually gets drawn. */
    for (int i = 0; i < s->mesh_count; i++)
        if (strcasecmp(s->mesh_names[i], mesh_name) == 0 &&
            s->meshes[i].tex == cached_texture(s, rcs, tex_name))
            return i;

    if (s->mesh_count >= s->mesh_cap)
        return -1;
    long idx = a2_archive_find(rcs, mesh_name);
    if (idx < 0)
        return -1;
    uint32_t size = 0;
    const uint8_t *data = a2_archive_data(rcs, (size_t)idx, &size);
    a2_mesh m;
    if (!data || a2_mesh_parse(&m, data, size, mesh_name) != 0)
        return -1;

    int slot = s->mesh_count;
    gmesh *g = &s->meshes[slot];
    memset(g, 0, sizeof(*g));
    g->tex = cached_texture(s, rcs, tex_name);
    g->verts = calloc(m.vertex_count ? m.vertex_count : 1, sizeof(vtx));
    g->colors = calloc(m.vertex_count ? m.vertex_count * 4 : 4, 1);
    g->indices = calloc(m.triangle_count ? m.triangle_count * 3 : 1, sizeof(uint32_t));
    if (!g->verts || !g->colors || !g->indices) {
        a2_mesh_free(&m);
        return -1;
    }
    /* No shading: MESH.CPP's loader writes every vertex colour as 0x00FFFFFF
     * and specular 0 (0x455cfa / 0x455d08), and the transform copies them
     * (0x454dfc), so a mesh is drawn at its texture's own colour.  (The port
     * used to shade by a key light of its own, down to 45%.) */
    for (uint32_t i = 0; i < m.vertex_count; i++) {
        const a2_vertex *sv = &m.vertices[i];
        g->verts[i].x = sv->x;
        g->verts[i].y = sv->y;
        g->verts[i].z = sv->z;
        g->verts[i].u = sv->u;
        g->verts[i].v = sv->v;
        g->colors[i * 4 + 0] = 255;
        g->colors[i * 4 + 1] = 255;
        g->colors[i * 4 + 2] = 255;
        g->colors[i * 4 + 3] = 255;
    }

    /* Bounds, for collision. */
    g->min_x = g->min_y = g->min_z = 1e30f;
    g->max_x = g->max_y = g->max_z = -1e30f;
    for (uint32_t i = 0; i < m.vertex_count; i++) {
        const a2_vertex *sv = &m.vertices[i];
        if (sv->x < g->min_x) g->min_x = sv->x;
        if (sv->y < g->min_y) g->min_y = sv->y;
        if (sv->z < g->min_z) g->min_z = sv->z;
        if (sv->x > g->max_x) g->max_x = sv->x;
        if (sv->y > g->max_y) g->max_y = sv->y;
        if (sv->z > g->max_z) g->max_z = sv->z;
    }
    if (m.vertex_count) {
        float hx = (g->max_x - g->min_x) * 0.5f, hz = (g->max_z - g->min_z) * 0.5f;
        g->radius = sqrtf(hx * hx + hz * hz);
        g->height = g->max_y - g->min_y;
    }
    g->vcount = m.vertex_count;
    for (uint32_t i = 0; i < m.triangle_count; i++) {
        g->indices[i * 3 + 0] = m.triangles[i].a;
        g->indices[i * 3 + 1] = m.triangles[i].b;
        g->indices[i * 3 + 2] = m.triangles[i].c;
    }
    g->icount = m.triangle_count * 3;
    a2_mesh_free(&m);

    snprintf(s->mesh_names[slot], sizeof s->mesh_names[slot], "%s", mesh_name);
    s->mesh_count++;
    return slot;
}

/* Which frame of an animated prop to show at time `t`.
 *
 * Only two shapes exist in the shipped data, and the mesh names say what each
 * one is.  The traffic lights have three frames whose mesh names end in the
 * Dutch colour initial -- c_stoplg / c_stoplo / c_stoplr and c_stplg2 /
 * c_stplo2 / c_stplr2, i.e. **g**roen, **o**ranje, **r**ood -- so frame order
 * is green, amber, red.  The roadworks arrows have two (A_pijl0 / A_pijl1,
 * and likewise for A_lpijl and c_pijl), which flash.
 *
 * The durations are code, not data (session 13).  The traffic-light handler
 * 0x429480 (.omp type 4) shows frame 2 / 0 / 1 for the global state 0x48c484
 * = 0 / 3 / 2, which steps on timer 1: red 5000 ms, green 5000 ms, amber
 * 500 ms (0x42951d; 0x4295a0 starts it red).  The arrow handler 0x4296b0
 * (type 8) flips 0x4dc96c on timer 7: frame 0 for 1000 ms, frame 1 for
 * 500 ms.  Every light and every arrow switches together. */
static int anim_frame_for(int frame_count, float t)
{
    if (frame_count == 3) {
        float c = fmodf(t, 10.5f);
        if (c < 5.0f)
            return 2; /* red */
        if (c < 10.0f)
            return 0; /* green */
        return 1;     /* amber */
    }
    if (frame_count == 2)
        return fmodf(t, 1.5f) < 1.0f ? 0 : 1;
    int n = frame_count > 0 ? frame_count : 1;
    return ((int)(t / 0.5f)) % n;
}

static int build_objects(a2_scene *s, const a2_track *tr, const a2_archive *rcs,
                         const a2_anims *anims)
{
    if (!tr->object_count) {
        s->mesh_cap = 80;
        s->meshes = calloc(s->mesh_cap, sizeof(gmesh));
        s->mesh_names = calloc(s->mesh_cap, sizeof(*s->mesh_names));
        return (s->meshes && s->mesh_names) ? 0 : -1;
    }
    s->mesh_cap = (int)tr->object_count + 80; /* room for player, traffic and the 3D font */
    s->meshes = calloc(s->mesh_cap, sizeof(gmesh));
    s->mesh_names = calloc(s->mesh_cap, sizeof(*s->mesh_names));
    s->instances = calloc(tr->object_count, sizeof(instance));
    if (!s->meshes || !s->mesh_names || !s->instances)
        return -1;

    /* Bucket instances by chunk so culling is a range walk. */
    int *per_chunk = calloc(s->chunk_count + 1, sizeof(int));
    if (!per_chunk)
        return -1;
    for (uint32_t i = 0; i < tr->object_count; i++) {
        const a2_object *o = &tr->objects[i];
        if (o->chunk >= 0 && (uint32_t)o->chunk < s->chunk_count)
            per_chunk[o->chunk]++;
    }
    s->chunk_inst_first = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(int));
    s->chunk_inst_count = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(int));
    if (!s->chunk_inst_first || !s->chunk_inst_count) {
        free(per_chunk);
        return -1;
    }
    int run = 0;
    for (uint32_t c = 0; c < s->chunk_count; c++) {
        s->chunk_inst_first[c] = run;
        run += per_chunk[c];
    }
    int *cursor = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(int));
    if (!cursor) {
        free(per_chunk);
        return -1;
    }

    int skipped = 0, on_lane = 0;
    for (uint32_t i = 0; i < tr->object_count; i++) {
        const a2_object *o = &tr->objects[i];
        if (o->chunk < 0 || (uint32_t)o->chunk >= s->chunk_count)
            continue;
        char mname[A2_OMP_NAME_LEN + 8];
        const char *tex = NULL;
        resolve_anim(anims, o->name, mname, sizeof mname, &tex);
        int mi = find_or_load_mesh(s, rcs, mname, tex);
        if (mi < 0) {
            skipped++;
            continue;
        }
        int slot = s->chunk_inst_first[o->chunk] + cursor[o->chunk]++;
        instance *in = &s->instances[slot];
        in->mesh = mi;
        in->chunk = o->chunk;
        in->x = o->x;
        in->y = o->y;
        in->z = o->z;
        in->yaw = o->rot_y;
        in->type = o->type;
        in->y0 = o->y;
        in->phase = in->pending = 0;
        in->rx = in->ry = in->rz = 0.0f;
        /* Load the anim's other frames, if it has any. */
        in->frames[0] = mi;
        in->frame_count = 1;
        if (anims) {
            int nf = 0;
            const a2_anim_frame *fr = a2_anims_find(anims, o->name, &nf);
            if (fr && nf > 1) {
                for (int f = 1; f < nf && in->frame_count < A2_MAX_ANIM_FRAMES; f++) {
                    if (!fr[f].mesh)
                        continue;
                    int m2 = find_or_load_mesh(s, rcs, fr[f].mesh, fr[f].texture);
                    if (m2 >= 0)
                        in->frames[in->frame_count++] = m2;
                }
            }
        }
        /* Solid as in the original: the static loader registers only types
         * 5 (columns), 8 (arrows) and 10 (police placements) as collision
         * candidates (0x4454a7 -> 0x4426c0, list 0x4e4988); type 9 becomes a
         * cop.  Lamp posts, trees and the rest of type 0 never collide, and
         * pylons, barriers and cameras are knocked over instead (see
         * a2_scene_props_step).  The original can push the arrows (mass
         * 13000) and police cars (1300, damped by 0x4293a0); the port cannot,
         * so anything on the driving line itself stays passable -- the
         * shipped tracks park police cars 1-8 units from a lane waypoint, and
         * the autopilot has no way round static objects. */
        in->solid = o->type == 5 || o->type == 8 || o->type == 10;
        if (in->solid) {
            int nl = a2_scene_lane_count(s, o->chunk);
            for (int L = 0; L < nl; L++) {
                float p[3];
                if (!a2_scene_lane_point(s, o->chunk, L, p))
                    continue;
                float dx = p[0] - o->x, dz = p[2] - o->z;
                if (dx * dx + dz * dz < 25.0f * 25.0f) {
                    in->solid = 0;
                    on_lane++;
                    break;
                }
            }
        }
        s->chunk_inst_count[o->chunk]++;
        if (slot + 1 > s->instance_count)
            s->instance_count = slot + 1;
    }
    free(cursor);
    free(per_chunk);
    if (skipped)
        fprintf(stderr, "scene: %d objects had no mesh\n", skipped);
    if (on_lane)
        fprintf(stderr, "scene: %d solid objects on the driving line left passable\n", on_lane);
    int animated = 0, frames_total = 0;
    for (int i = 0; i < s->instance_count; i++)
        if (s->instances[i].frame_count > 1) {
            animated++;
            frames_total += s->instances[i].frame_count;
        }
    if (animated)
        fprintf(stderr, "scene: %d animated props, %d frames (traffic lights, arrows)\n", animated,
                frames_total);
    return 0;
}

/* ------------------------------------------------------------------ api */

a2_scene *a2_scene_create(const a2_track *tr, const a2_archive *rcs, const char *prefix,
                          const char *const plane_tex[4], const char *atlas,
                          const a2_anims *anims)
{
    a2_scene *s = calloc(1, sizeof(*s));
    if (!s)
        return NULL;
    s->chunk_count = tr->rmp.chunk_count;
    s->chunk_x = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(float));
    s->chunk_y = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(float));
    s->chunk_z = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(float));
    if (!s->chunk_x || !s->chunk_y || !s->chunk_z) {
        a2_scene_destroy(s);
        return NULL;
    }
    for (uint32_t i = 0; i < s->chunk_count && i < tr->pmp.chunk_count; i++) {
        s->chunk_x[i] = tr->pmp.chunks[i].x;
        s->chunk_y[i] = tr->pmp.chunks[i].y;
        s->chunk_z[i] = tr->pmp.chunks[i].z;
    }
    /* 0x442cb0, called by the loader (0x444fb7, 0x44504b) for every chunk
     * toward the next one's position, the last toward the first: forward to
     * that point, right = (0,1,0) x forward (so never banked), up = forward x
     * right, and the plane through the chunk's position with normal up. */
    /* The city stages close: their last chunk lies 171-189 units from chunk 0
     * (a step is 128); the highways end 6-10 km from their start. */
    if (s->chunk_count > 2) {
        float dx = s->chunk_x[s->chunk_count - 1] - s->chunk_x[0];
        float dz = s->chunk_z[s->chunk_count - 1] - s->chunk_z[0];
        s->closed = dx * dx + dz * dz < 384.0f * 384.0f;
    }
    s->chunk_plane = calloc(s->chunk_count ? s->chunk_count * 4 : 4, sizeof(float));
    if (!s->chunk_plane) {
        a2_scene_destroy(s);
        return NULL;
    }
    for (uint32_t i = 0; i < s->chunk_count; i++) {
        uint32_t j = i + 1 < s->chunk_count ? i + 1 : 0;
        float f[3] = {s->chunk_x[j] - s->chunk_x[i], s->chunk_y[j] - s->chunk_y[i],
                      s->chunk_z[j] - s->chunk_z[i]};
        float *pl = &s->chunk_plane[i * 4];
        float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (fl < 1e-6f) {
            pl[1] = 1.0f;
            pl[3] = s->chunk_y[i];
            continue;
        }
        for (int k = 0; k < 3; k++)
            f[k] /= fl;
        float r[3] = {f[2], 0.0f, -f[0]}; /* (0,1,0) x f */
        float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
        if (rl < 1e-6f) {
            pl[1] = 1.0f;
            pl[3] = s->chunk_y[i];
            continue;
        }
        r[0] /= rl;
        r[2] /= rl;
        float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2],
                      f[0] * r[1] - f[1] * r[0]}; /* f x r */
        float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
        for (int k = 0; k < 3; k++)
            pl[k] = u[k] / ul;
        pl[3] = pl[0] * s->chunk_x[i] + pl[1] * s->chunk_y[i] + pl[2] * s->chunk_z[i];
    }

    s->tex_cap = 256;
    s->tex_names = calloc(s->tex_cap, sizeof(*s->tex_names));
    s->tex_ids = calloc(s->tex_cap, sizeof(unsigned));
    if (!s->tex_names || !s->tex_ids) {
        a2_scene_destroy(s);
        return NULL;
    }

    unsigned placeholder = a2_gfx_placeholder_texture();
    /* --atlas forces one texture for every plane list, to test whether the
       whole track samples a single per-track atlas (see open questions). */
    unsigned shared = atlas ? load_texture_named(rcs, atlas) : 0;
    for (int l = 0; l < A2_RMP_LISTS; l++) {
        if (build_list(&s->lists[l], tr, l) != 0) {
            a2_scene_destroy(s);
            return NULL;
        }
        unsigned tex = shared;
        if (!tex && plane_tex && plane_tex[l] && plane_tex[l][0])
            tex = load_texture_stem(rcs, plane_tex[l]);
        if (!tex)
            tex = load_texture(rcs, prefix, list_texture_suffix[l]);
        s->lists[l].tex = tex ? tex : placeholder;
        s->lists[l].tex_owned = tex != 0 && !shared;
    }
    s->shared_tex = shared;
    s->mesh_tex = placeholder;

    if (tr->rail_count && tr->rails) {
        s->rails = calloc(tr->rail_count, sizeof(a2_rail));
        s->chunk_rails = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(uint32_t *));
        s->chunk_rail_count = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(uint32_t));
        if (!s->rails || !s->chunk_rails || !s->chunk_rail_count) {
            a2_scene_destroy(s);
            return NULL;
        }
        memcpy(s->rails, tr->rails, tr->rail_count * sizeof(a2_rail));
        s->rail_count = tr->rail_count;
        for (uint32_t i = 0; i < s->chunk_count && i < tr->pmp.chunk_count; i++) {
            s->chunk_rails[i] = tr->pmp.chunks[i].rails;
            s->chunk_rail_count[i] = tr->pmp.chunks[i].rail_count;
        }
    }

    if (tr->waypoint_count && tr->waypoints) {
        s->waypoints = calloc(tr->waypoint_count, sizeof(a2_waypoint));
        s->chunk_wp = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(*s->chunk_wp));
        s->chunk_wp_count = calloc(s->chunk_count ? s->chunk_count : 1, sizeof(uint32_t));
        if (!s->waypoints || !s->chunk_wp || !s->chunk_wp_count) {
            a2_scene_destroy(s);
            return NULL;
        }
        memcpy(s->waypoints, tr->waypoints, tr->waypoint_count * sizeof(a2_waypoint));
        s->waypoint_count = tr->waypoint_count;
        for (uint32_t i = 0; i < s->chunk_count && i < tr->pmp.chunk_count; i++) {
            /* The right-hand carriageway is the one the player drives.
             *
             * Not every waypoint in it is a running lane: a chunk may also
             * carry the fuel-station slip road or a service path, sitting
             * hundreds of units off the centreline (etappe1 chunk 399 has
             * lanes at 40/80/120 and three more at 1094/1351/1694).  Running
             * lanes are spaced 40 units apart, so sort by lateral offset and
             * keep the contiguous run inward of the first big gap.  Across the
             * 7 tracks this drops 55 waypoints of 14548, all of them off-road.
             * See research/discoveries.md. */
            const a2_pmp_chunk *pc = &tr->pmp.chunks[i];
            uint32_t n = pc->wp_right_count;
            if (n > A2_CHUNK_WP_MAX)
                n = A2_CHUNK_WP_MAX;
            if (n == 0) {
                s->chunk_wp_count[i] = 0;
                continue;
            }
            /* Road normal, from this chunk to the next. */
            float px = 0.0f, pz = 0.0f;
            if (i + 1 < tr->pmp.chunk_count) {
                float fx = tr->pmp.chunks[i + 1].x - pc->x;
                float fz = tr->pmp.chunks[i + 1].z - pc->z;
                float fl = sqrtf(fx * fx + fz * fz);
                if (fl > 1e-3f) {
                    px = fz / fl;
                    pz = -fx / fl;
                }
            }
            typedef struct {
                float lat;
                uint32_t wi;
            } lane_sort;
            lane_sort order[A2_CHUNK_WP_MAX];
            for (uint32_t k = 0; k < n; k++) {
                uint32_t wi = pc->wp_right[k];
                float lat = 0.0f;
                if ((px != 0.0f || pz != 0.0f) && wi < tr->waypoint_count)
                    lat = (tr->waypoints[wi].x - pc->x) * px + (tr->waypoints[wi].z - pc->z) * pz;
                order[k].lat = lat;
                order[k].wi = wi;
            }
            for (uint32_t a = 1; a < n; a++) { /* insertion sort, <=12 entries */
                lane_sort v = order[a];
                uint32_t b = a;
                while (b > 0 && order[b - 1].lat > v.lat) {
                    order[b] = order[b - 1];
                    b--;
                }
                order[b] = v;
            }
            uint32_t keep = 1;
            if (px != 0.0f || pz != 0.0f)
                while (keep < n && order[keep].lat - order[keep - 1].lat <= 100.0f)
                    keep++;
            else
                keep = n;
            for (uint32_t k = 0; k < keep; k++)
                s->chunk_wp[i][k] = order[k].wi;
            s->chunk_wp_count[i] = keep;
        }
    }

    if (build_objects(s, tr, rcs, anims) != 0) {
        a2_scene_destroy(s);
        return NULL;
    }
    return s;
}

void a2_scene_destroy(a2_scene *s)
{
    if (!s)
        return;
    for (int l = 0; l < A2_RMP_LISTS; l++)
        free_list(&s->lists[l]);
    if (s->shared_tex)
        a2_gfx_delete_texture(s->shared_tex);
    for (int i = 0; i < s->mesh_count; i++) {
        free(s->meshes[i].verts);
        free(s->meshes[i].colors);
        free(s->meshes[i].indices);
    }
    for (int i = 0; i < s->tex_count; i++)
        a2_gfx_delete_texture(s->tex_ids[i]);
    free(s->tex_names);
    free(s->tex_ids);
    free(s->meshes);
    free(s->mesh_names);
    free(s->instances);
    free(s->waypoints);
    free(s->chunk_wp);
    free(s->chunk_wp_count);
    free(s->rails);
    free(s->chunk_rails);
    free(s->chunk_rail_count);
    free(s->chunk_inst_first);
    free(s->chunk_inst_count);
    free(s->chunk_x);
    free(s->chunk_plane);
    free(s->chunk_y);
    free(s->chunk_z);
    free(s);
}

int a2_scene_object_box(const a2_scene *s, int inst, struct a2_cbox *box)
{
    if (!s || inst < 0 || inst >= s->instance_count)
        return 0;
    const instance *in = &s->instances[inst];
    const gmesh *g = &s->meshes[in->mesh];
    box->x0 = g->min_x;
    box->x1 = g->max_x;
    box->z0 = g->min_z;
    box->z1 = g->max_z;
    box->x = in->x;
    box->z = in->z;
    /* drawn turned by yaw degrees; the engine's heading turns the other way */
    box->heading = -in->yaw * (4096.0f / 360.0f);
    return 1;
}

int a2_scene_rail_boxes(const a2_scene *s, int chunk, struct a2_cbox *out, int max)
{
    int n = 0;
    if (!s || !s->rails)
        return 0;
    for (int i = -1; i <= 0; i++) {
        int c = a2_scene_wrap_chunk(s, chunk + i);
        if (c < 0 || !s->chunk_rails[c])
            continue;
        for (uint32_t k = 0; k < s->chunk_rail_count[c] && n < max; k++) {
            uint32_t ri = s->chunk_rails[c][k];
            if (ri >= s->rail_count)
                continue;
            const a2_rail *r = &s->rails[ri];
            float len = sqrtf(r->dx * r->dx + r->dz * r->dz);
            if (len < 1e-3f)
                continue;
            a2_cbox *b = &out[n++];
            b->x0 = -10.0f;
            b->x1 = 0.0f;
            b->z0 = -1.0f;
            b->z1 = len + 1.0f;
            b->x = r->x;
            b->z = r->z;
            /* forward along the rail: (-sin h, cos h) */
            b->heading = atan2f(-r->dx, r->dz) * (4096.0f / 6.28318531f);
        }
    }
    return n;
}

int a2_scene_object_type(const a2_scene *s, int inst)
{
    if (!s || inst < 0 || inst >= s->instance_count)
        return -1;
    return s->instances[inst].type;
}

int a2_scene_solid_boxes(const a2_scene *s, int chunk, struct a2_cbox *out, int *inst, int max)
{
    int n = 0;
    if (!s || !s->instance_count || chunk < 0)
        return 0;
    for (int i = -1; i <= 1; i++) {
        int c = a2_scene_wrap_chunk(s, chunk + i);
        if (c < 0)
            continue;
        for (int k = 0; k < s->chunk_inst_count[c] && n < max; k++) {
            int ii = s->chunk_inst_first[c] + k;
            const instance *in = &s->instances[ii];
            if (!in->solid || !s->meshes[in->mesh].icount)
                continue;
            if (a2_scene_object_box(s, ii, &out[n])) {
                inst[n] = ii;
                n++;
            }
        }
    }
    return n;
}

int a2_scene_wrap_chunk(const a2_scene *s, int c)
{
    int n = (int)s->chunk_count;
    if (n <= 0)
        return -1;
    if (s->closed)
        return ((c % n) + n) % n;
    return c >= 0 && c < n ? c : -1;
}

int a2_scene_chunk_delta(const a2_scene *s, int from, int to)
{
    int d = to - from, n = (int)s->chunk_count;
    if (s->closed && n > 0) {
        d %= n;
        if (d > n / 2)
            d -= n;
        if (d < -n / 2)
            d += n;
    }
    return d;
}

int a2_scene_nearest_chunk(const a2_scene *s, float x, float z)
{
    int best = 0;
    float bestd = 1e30f;
    for (uint32_t i = 0; i < s->chunk_count; i++) {
        float dx = s->chunk_x[i] - x, dz = s->chunk_z[i] - z;
        float d = dx * dx + dz * dz;
        if (d < bestd) {
            bestd = d;
            best = (int)i;
        }
    }
    return best;
}

/* Centroid of a chunk's road planes.  The chunk position in .pmp is a corner,
 * not the road centre, so this is what the camera should sit above.
 *
 * `single` averages only the first plane instead of the whole chunk: a stage
 * like etappe5 is a dual carriageway, so averaging every road plane lands in
 * the median rather than on a lane. */
static int road_centroid_ex(const a2_scene *s, uint32_t chunk, float out[3], int single)
{
    const plane_batch *road = &s->lists[A2_PLANE_ROAD];
    if (chunk >= s->chunk_count || !road->count[chunk])
        return 0;
    double sx = 0.0, sy = 0.0, sz = 0.0;
    uint32_t n = 0;
    uint32_t end = road->first[chunk] + (single ? 6u : road->count[chunk]);
    if (end > road->first[chunk] + road->count[chunk])
        end = road->first[chunk] + road->count[chunk];
    for (uint32_t k = road->first[chunk]; k < end; k++) {
        const vtx *v = &road->verts[road->indices[k]];
        sx += v->x;
        sy += v->y;
        sz += v->z;
        n++;
    }
    if (!n)
        return 0;
    out[0] = (float)(sx / n);
    out[1] = (float)(sy / n);
    out[2] = (float)(sz / n);
    return 1;
}

static int road_centroid(const a2_scene *s, uint32_t chunk, float out[3])
{
    return road_centroid_ex(s, chunk, out, 1);
}

void a2_scene_start_position(const a2_scene *s, float out[3], float *out_yaw)
{
    /* A few chunks in, so the view is not clipped by the start of the stage. */
    uint32_t i = s->chunk_count > 16 ? 4 : 0;
    out[0] = s->chunk_count ? s->chunk_x[i] : 0.0f;
    out[1] = (s->chunk_count ? s->chunk_y[i] : 0.0f) + 60.0f;
    out[2] = s->chunk_count ? s->chunk_z[i] : 0.0f;

    float a[3], b[3];
    int have_a = road_centroid(s, i, a);
    if (have_a) {
        out[0] = a[0];
        out[1] = a[1] + 60.0f;
        out[2] = a[2];
    }
    if (out_yaw) {
        /* Look along the road itself, averaged over a few chunks so a local
         * bend does not throw the opening view sideways. */
        *out_yaw = 0.0f;
        uint32_t j = i + 4 < s->chunk_count ? i + 4 : s->chunk_count - 1;
        if (have_a && j != i && road_centroid(s, j, b)) {
            float dx = b[0] - a[0], dz = b[2] - a[2];
            if (dx * dx + dz * dz > 1e-6f)
                *out_yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
        }
    }
}

int a2_scene_start_pose_on_lane(const a2_scene *s, int chunk, int lane, float out[3],
                                float *out_yaw)
{
    if (!s->waypoints || chunk < 0 || chunk >= (int)s->chunk_count)
        return 0;
    uint32_t n = s->chunk_wp_count[chunk];
    if (!n)
        return 0;
    if (lane < 0)
        lane = 0;
    if (lane >= (int)n)
        lane = (int)n - 1;
    uint32_t wi = s->chunk_wp[chunk][lane];
    if (wi >= s->waypoint_count)
        return 0;
    const a2_waypoint *w = &s->waypoints[wi];
    out[0] = w->x;
    out[2] = w->z;
    out[1] = w->y; /* the waypoint's own height (.dmp), which follows bridges */

    if (out_yaw) {
        /* The two carriageways run in opposite directions, so a waypoint's
         * links are "next" for one set and "previous" for the other (wp24's
         * links[3] is the next chunk, wp25's links[3] is the previous one).
         * Rather than guess, take the heading from the same lane one chunk
         * further along, which is the direction the game counts progress in. */
        *out_yaw = 0.0f;
        int nc = chunk + 1;
        if (nc < (int)s->chunk_count && s->chunk_wp_count[nc]) {
            uint32_t nn = s->chunk_wp_count[nc];
            uint32_t nwi = s->chunk_wp[nc][nn / 2];
            if (nwi < s->waypoint_count) {
                float dx = s->waypoints[nwi].x - w->x;
                float dz = s->waypoints[nwi].z - w->z;
                if (dx * dx + dz * dz > 1e-6f)
                    *out_yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
            }
        }
    }
    return 1;
}

int a2_scene_chunk_count(const a2_scene *s)
{
    return s ? (int)s->chunk_count : 0;
}

void a2_scene_set_anim_time(a2_scene *s, float seconds)
{
    if (s)
        s->anim_time = seconds;
}

int a2_scene_lane_count(const a2_scene *s, int chunk)
{
    if (!s->waypoints || chunk < 0 || chunk >= (int)s->chunk_count)
        return 0;
    return (int)s->chunk_wp_count[chunk];
}

int a2_scene_lane_point(const a2_scene *s, int chunk, int lane, float out[3])
{
    int n = a2_scene_lane_count(s, chunk);
    if (n <= 0)
        return 0;
    if (lane < 0)
        lane = 0;
    if (lane >= n)
        lane = n - 1;
    uint32_t wi = s->chunk_wp[chunk][lane];
    if (wi >= s->waypoint_count)
        return 0;
    out[0] = s->waypoints[wi].x;
    out[2] = s->waypoints[wi].z;
    /* The waypoint's own height: the game stores one per waypoint, it follows
     * bridges, and it differs from the chunk's ground plane only on the
     * etappe1 slip road (chunks 399/400), where it is the right one. */
    out[1] = s->waypoints[wi].y;
    return 1;
}

int a2_scene_nearest_lane(const a2_scene *s, int chunk, float x, float z)
{
    int n = a2_scene_lane_count(s, chunk);
    if (n <= 0)
        return -1;
    int best = 0;
    float bestd = 1e30f;
    for (int l = 0; l < n; l++) {
        uint32_t wi = s->chunk_wp[chunk][l];
        if (wi >= s->waypoint_count)
            continue;
        float dx = s->waypoints[wi].x - x, dz = s->waypoints[wi].z - z;
        float d = dx * dx + dz * dz;
        if (d < bestd) {
            bestd = d;
            best = l;
        }
    }
    return best;
}

void a2_scene_stats(const a2_scene *s, char *buf, size_t buflen)
{
    unsigned long tris = 0;
    for (int l = 0; l < A2_RMP_LISTS; l++)
        tris += s->lists[l].icount / 3;
    int textured = 0;
    for (int i = 0; i < s->mesh_count; i++)
        if (s->meshes[i].tex)
            textured++;
    snprintf(buf, buflen, "%u chunks, %lu plane tris, %d meshes (%d textured), %d objects",
             s->chunk_count, tris, s->mesh_count, textured, s->instance_count);
}

int a2_scene_load_model(a2_scene *s, const a2_archive *rcs, const a2_anims *anims,
                        const char *anim_name)
{
    if (s->mesh_count >= s->mesh_cap)
        return -1;
    char mname[A2_OMP_NAME_LEN + 8];
    const char *tex = NULL;
    resolve_anim(anims, anim_name, mname, sizeof mname, &tex);
    return find_or_load_mesh(s, rcs, mname, tex);
}

int a2_scene_load_mesh_named(a2_scene *s, const a2_archive *rcs, const char *mesh_name,
                             const char *tex_name)
{
    if (s->mesh_count >= s->mesh_cap)
        return -1;
    return find_or_load_mesh(s, rcs, mesh_name, tex_name);
}

int a2_scene_model_bounds(const a2_scene *s, int handle, float lo[3], float hi[3])
{
    if (handle < 0 || handle >= s->mesh_count)
        return 0;
    const gmesh *g = &s->meshes[handle];
    if (!g->vcount)
        return 0;
    lo[0] = hi[0] = g->verts[0].x;
    lo[1] = hi[1] = g->verts[0].y;
    lo[2] = hi[2] = g->verts[0].z;
    for (uint32_t i = 1; i < g->vcount; i++) {
        const vtx *v = &g->verts[i];
        if (v->x < lo[0]) lo[0] = v->x;
        if (v->y < lo[1]) lo[1] = v->y;
        if (v->z < lo[2]) lo[2] = v->z;
        if (v->x > hi[0]) hi[0] = v->x;
        if (v->y > hi[1]) hi[1] = v->y;
        if (v->z > hi[2]) hi[2] = v->z;
    }
    return 1;
}

void a2_scene_draw_model_scaled(const a2_scene *s, int handle, float x, float y, float z,
                                float scale)
{
    if (handle < 0 || handle >= s->mesh_count)
        return;
    const gmesh *g = &s->meshes[handle];
    if (!g->icount)
        return;
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glBindTexture(GL_TEXTURE_2D, g->tex ? g->tex : s->mesh_tex);
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(scale, scale, scale);
    glVertexPointer(3, GL_FLOAT, sizeof(vtx), &g->verts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), &g->verts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, g->colors);
    glDrawElements(GL_TRIANGLES, (GLsizei)g->icount, GL_UNSIGNED_INT, g->indices);
    glPopMatrix();
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void a2_scene_draw_model_rpy(const a2_scene *s, int handle, float x, float y, float z,
                             float yaw_deg, float pitch_deg, float roll_deg)
{
    if (handle < 0 || handle >= s->mesh_count)
        return;
    const gmesh *g = &s->meshes[handle];
    if (!g->icount)
        return;
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glBindTexture(GL_TEXTURE_2D, g->tex ? g->tex : s->mesh_tex);
    glPushMatrix();
    glTranslatef(x, y, z);
    /* Yaw first, then pitch about the car's own lateral axis and roll about its
     * own forward axis, so the lean follows the car rather than the world. */
    glRotatef(yaw_deg, 0.0f, 1.0f, 0.0f);
    glRotatef(pitch_deg, 1.0f, 0.0f, 0.0f);
    glRotatef(roll_deg, 0.0f, 0.0f, 1.0f);
    glVertexPointer(3, GL_FLOAT, sizeof(vtx), &g->verts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), &g->verts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, g->colors);
    glDrawElements(GL_TRIANGLES, (GLsizei)g->icount, GL_UNSIGNED_INT, g->indices);
    glPopMatrix();
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void a2_scene_draw_model(const a2_scene *s, int handle, float x, float y, float z, float yaw_deg)
{
    if (handle < 0 || handle >= s->mesh_count)
        return;
    const gmesh *g = &s->meshes[handle];
    if (!g->icount)
        return;
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glBindTexture(GL_TEXTURE_2D, g->tex ? g->tex : s->mesh_tex);
    glPushMatrix();
    glTranslatef(x, y, z);
    glRotatef(yaw_deg, 0.0f, 1.0f, 0.0f);
    glVertexPointer(3, GL_FLOAT, sizeof(vtx), &g->verts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), &g->verts[0].u);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, g->colors);
    glDrawElements(GL_TRIANGLES, (GLsizei)g->icount, GL_UNSIGNED_INT, g->indices);
    glPopMatrix();
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

int a2_scene_set_glow_texture(a2_scene *s, const a2_archive *rcs, int handle, const char *tex_name)
{
    if (handle < 0 || handle >= s->mesh_count)
        return 0;
    char cand[64];
    snprintf(cand, sizeof cand, "%s", tex_name);
    char *dot = strrchr(cand, '.');
    if (dot)
        strcpy(dot, ".tga");
    long idx = a2_archive_find(rcs, cand);
    if (idx < 0)
        idx = a2_archive_find(rcs, tex_name);
    if (idx < 0)
        return 0;
    uint32_t size = 0;
    const uint8_t *data = a2_archive_data(rcs, (size_t)idx, &size);
    a2_image im;
    if (!data || a2_image_decode(&im, data, size, tex_name) != 0)
        return 0;
    a2_image_key_transparent(&im);
    a2_gfx_gamma_rgba(im.rgba, (size_t)im.width * im.height);
    for (size_t i = 0; i < (size_t)im.width * im.height; i++) {
        uint8_t *p = im.rgba + i * 4;
        unsigned lum = (p[0] + p[1] + p[2]) / 3;
        p[3] = p[3] ? (uint8_t)(lum * lum / 255) : 0; /* fitted to the capture */
        p[0] = p[1] = p[2] = 255;
    }
    s->meshes[handle].tex = a2_gfx_texture_from_image(&im, 0);
    a2_image_free(&im);
    return 1;
}

void a2_scene_draw_model_tinted(const a2_scene *s, int handle, uint8_t r, uint8_t g, uint8_t b,
                                uint8_t a)
{
    if (handle < 0 || handle >= s->mesh_count)
        return;
    const gmesh *gm = &s->meshes[handle];
    if (!gm->icount)
        return;
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glBindTexture(GL_TEXTURE_2D, gm->tex ? gm->tex : s->mesh_tex);
    glColor4ub(r, g, b, a);
    glVertexPointer(3, GL_FLOAT, sizeof(vtx), &gm->verts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), &gm->verts[0].u);
    glDrawElements(GL_TRIANGLES, (GLsizei)gm->icount, GL_UNSIGNED_INT, gm->indices);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_BLEND);
}

unsigned a2_scene_texture(a2_scene *s, const a2_archive *rcs, const char *name)
{
    return cached_texture(s, rcs, name);
}

int a2_scene_blit(const a2_scene *s, const char *tex_name, int x, int y, int w, int h,
                  const uint8_t *rgba)
{
    size_t stem = strcspn(tex_name, ".");
    for (int i = 0; i < s->tex_count; i++) {
        const char *n = s->tex_names[i];
        if (strncasecmp(n, tex_name, stem) != 0 || (n[stem] != '.' && n[stem] != 0))
            continue;
        if (!s->tex_ids[i])
            return 0;
        /* the stamped sprite went through the same gamma when it was loaded
         * (Sprite.cpp's conversion), so it matches the texture it lands in */
        uint8_t *px = malloc((size_t)w * h * 4);
        if (!px)
            return 0;
        memcpy(px, rgba, (size_t)w * h * 4);
        a2_gfx_gamma_rgba(px, (size_t)w * h);
        glBindTexture(GL_TEXTURE_2D, s->tex_ids[i]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
        free(px);
        return 1;
    }
    return 0;
}

/* Car shadows, after gfx_shdw.cpp.
 *
 * The original keeps one shared quad, S_SHADOW_ANIM (init_car_shadow_object,
 * 0x410df0), and the engine's vehicle draw loop calls the shadow hook
 * (0x41d460) for every vehicle just before drawing it -- player, racers,
 * traffic and police alike.  For each car the hook:
 *   - takes the quad's corners from the car mesh's bounding box (x and z of
 *     four box corners), so the shadow is the car's own footprint;
 *   - puts it at the car's position plus (2, 2) world units (0x47f6c4 = -2,
 *     subtracted), a fixed sun offset;
 *   - lifts it 0.5 above the road height under the car (0x47f6c0 = -0.5);
 *   - gives it the car's yaw;
 *   - draws it with SRCALPHA / INVSRCALPHA blending, only when Game.ini's
 *     alpha_blending is on.
 * The texture is a black rounded rectangle on the colour key.  It carries no
 * alpha channel, so the translucency comes from the render path. The ~0.5 used
 * here is measured off a capture of the original, where the shadow halves the
 * road's brightness. */
int a2_scene_load_shadow(a2_scene *s, const a2_archive *rcs, const a2_anims *anims)
{
    char mname[A2_OMP_NAME_LEN + 8];
    const char *tex = NULL;
    resolve_anim(anims, "S_SHADOW_ANIM", mname, sizeof mname, &tex);
    s->shadow_tex = cached_texture(s, rcs, tex ? tex : "s_shadow.pcx");
    return s->shadow_tex != 0;
}

#define SHADOW_OFFSET 2.0f
#define SHADOW_LIFT 0.5f
#define SHADOW_ALPHA 0.5f

void a2_scene_draw_shadow(const a2_scene *s, int handle, float x, float z, int chunk,
                          float yaw_deg)
{
    float lo[3], hi[3], y;
    if (!s->shadow_tex || !a2_scene_model_bounds(s, handle, lo, hi))
        return;
    if (!a2_scene_road_height(s, chunk, x, z, &y))
        return;
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -2.0f);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, s->shadow_tex);
    glColor4f(1.0f, 1.0f, 1.0f, SHADOW_ALPHA);
    glPushMatrix();
    glTranslatef(x + SHADOW_OFFSET, y + SHADOW_LIFT, z + SHADOW_OFFSET);
    glRotatef(yaw_deg, 0.0f, 1.0f, 0.0f);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(lo[0], 0.0f, lo[2]);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(lo[0], 0.0f, hi[2]);
    glTexCoord2f(1.0f, 1.0f); glVertex3f(hi[0], 0.0f, hi[2]);
    glTexCoord2f(1.0f, 0.0f); glVertex3f(hi[0], 0.0f, lo[2]);
    glEnd();
    glPopMatrix();
    glPopAttrib();
}

/* 0x442f00: the height of `chunk`'s ground plane at (x, z).  One plane a
 * chunk, through its position and the next chunk's, so it never sees an
 * overpass, a bridge deck above or the road below.  `chunk` is a hint (the
 * nearest chunk): a point not yet past that chunk's position along its
 * forward lies on the previous chunk's stretch, so that plane is used (the
 * port's choice; the original reads the object's tracked chunk, +0x18). */
int a2_scene_road_height(const a2_scene *s, int chunk, float x, float z, float *y)
{
    if (!s->chunk_plane || chunk < 0 || chunk >= (int)s->chunk_count)
        return 0;
    int n = (int)s->chunk_count;
    int next = chunk + 1 < n ? chunk + 1 : 0;
    float fx = s->chunk_x[next] - s->chunk_x[chunk], fz = s->chunk_z[next] - s->chunk_z[chunk];
    if ((x - s->chunk_x[chunk]) * fx + (z - s->chunk_z[chunk]) * fz < 0.0f)
        chunk = chunk > 0 ? chunk - 1 : n - 1;
    const float *pl = &s->chunk_plane[chunk * 4];
    if (pl[1] > -1e-6f && pl[1] < 1e-6f)
        return 0;
    *y = (pl[3] - pl[0] * x - pl[2] * z) / pl[1];
    return 1;
}

/* Closest point on segment (ax,az)-(bx,bz) to (px,pz). */
static void closest_on_segment(float ax, float az, float bx, float bz, float px, float pz,
                               float *cx, float *cz, float *t)
{
    float dx = bx - ax, dz = bz - az;
    float len2 = dx * dx + dz * dz;
    float u = len2 > 1e-6f ? ((px - ax) * dx + (pz - az) * dz) / len2 : 0.0f;
    if (u < 0.0f)
        u = 0.0f;
    if (u > 1.0f)
        u = 1.0f;
    *cx = ax + dx * u;
    *cz = az + dz * u;
    *t = u;
}

int a2_scene_collide_rails(const a2_scene *s, int chunk, float *x, float *z, float radius,
                           float slide[2])
{
    if (!s->rails || chunk < 0 || chunk >= (int)s->chunk_count)
        return 0;
    int hit = 0;
    for (int i = -1; i <= 1; i++) {
        int c = a2_scene_wrap_chunk(s, chunk + i);
        if (c < 0 || !s->chunk_rails[c])
            continue;
        for (uint32_t k = 0; k < s->chunk_rail_count[c]; k++) {
            uint32_t ri = s->chunk_rails[c][k];
            if (ri >= s->rail_count)
                continue;
            const a2_rail *r = &s->rails[ri];
            float cx, cz, t;
            closest_on_segment(r->x, r->z, r->x + r->dx, r->z + r->dz, *x, *z, &cx, &cz, &t);
            float dx = *x - cx, dz = *z - cz;
            float d2 = dx * dx + dz * dz;
            if (d2 >= radius * radius)
                continue;
            float d = sqrtf(d2);
            if (d < 1e-4f) {
                /* Dead on the line: push along its normal. */
                dx = -r->dz;
                dz = r->dx;
                d = sqrtf(dx * dx + dz * dz);
                if (d < 1e-4f)
                    continue;
            }
            *x = cx + dx / d * radius;
            *z = cz + dz / d * radius;
            if (slide) {
                float rl = sqrtf(r->dx * r->dx + r->dz * r->dz);
                if (rl > 1e-4f) {
                    slide[0] = r->dx / rl;
                    slide[1] = r->dz / rl;
                }
            }
            hit = 1;
        }
    }
    return hit;
}

void a2_scene_draw_rails(const a2_scene *s, int near_chunk, int radius)
{
    if (!s->rails)
        return;
    glDisable(GL_TEXTURE_2D);
    glColor4f(1.0f, 0.2f, 0.1f, 1.0f);
    glBegin(GL_LINES);
    for (int i = -radius; i <= radius; i++) {
        int c = a2_scene_wrap_chunk(s, near_chunk + i);
        if (c < 0 || !s->chunk_rails[c])
            continue;
        for (uint32_t k = 0; k < s->chunk_rail_count[c]; k++) {
            uint32_t ri = s->chunk_rails[c][k];
            if (ri >= s->rail_count)
                continue;
            const a2_rail *r = &s->rails[ri];
            glVertex3f(r->x, r->y + 10.0f, r->z);
            glVertex3f(r->x + r->dx, r->y + r->dy + 10.0f, r->z + r->dz);
        }
    }
    glEnd();
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
}

void a2_scene_draw(const a2_scene *s, int near_chunk, int radius, int show_objects)
{
    /* the chunks within `radius`, round the seam on a circuit */
    int win[2 * 64 + 1], nwin = 0;
    if (radius > 64)
        radius = 64;
    for (int i = -radius; i <= radius; i++) {
        int c = a2_scene_wrap_chunk(s, near_chunk + i);
        if (c >= 0)
            win[nwin++] = c;
    }

    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    for (int l = 0; l < A2_RMP_LISTS; l++) {
        const plane_batch *b = &s->lists[l];
        if (!b->icount)
            continue;
        glBindTexture(GL_TEXTURE_2D, b->tex);
        glVertexPointer(3, GL_FLOAT, sizeof(vtx), &b->verts[0].x);
        glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), &b->verts[0].u);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        for (int w = 0; w < nwin; w++) {
            int c = win[w];
            if (b->count[c])
                glDrawElements(GL_TRIANGLES, (GLsizei)b->count[c], GL_UNSIGNED_INT,
                               b->indices + b->first[c]);
        }
    }

    if (show_objects && s->instance_count) {
        glEnableClientState(GL_COLOR_ARRAY);
        unsigned bound = 0;
        for (int w = 0; w < nwin; w++) {
            int c = win[w];
            for (int k = 0; k < s->chunk_inst_count[c]; k++) {
                const instance *in = &s->instances[s->chunk_inst_first[c] + k];
                if (in->hidden)
                    continue;
                const gmesh *g = &s->meshes[in->mesh];
                if (in->frame_count > 1)
                    g = &s->meshes[in->frames[anim_frame_for(in->frame_count, s->anim_time)]];
                if (!g->icount)
                    continue;
                unsigned want = g->tex ? g->tex : s->mesh_tex;
                if (want != bound) {
                    glBindTexture(GL_TEXTURE_2D, want);
                    bound = want;
                }
                glPushMatrix();
                glTranslatef(in->x, in->y, in->z);
                glRotatef(in->yaw + in->ry, 0.0f, 1.0f, 0.0f);
                if (in->phase) { /* a knocked prop's tumble */
                    glRotatef(in->rx, 1.0f, 0.0f, 0.0f);
                    glRotatef(in->rz, 0.0f, 0.0f, 1.0f);
                }
                glVertexPointer(3, GL_FLOAT, sizeof(vtx), &g->verts[0].x);
                glTexCoordPointer(2, GL_FLOAT, sizeof(vtx), &g->verts[0].u);
                glColorPointer(4, GL_UNSIGNED_BYTE, 0, g->colors);
                glDrawElements(GL_TRIANGLES, (GLsizei)g->icount, GL_UNSIGNED_INT, g->indices);
                glPopMatrix();
            }
        }
        glDisableClientState(GL_COLOR_ARRAY);
    }

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

/* Scenery collision, for the .omp types the original registers as collision
 * candidates (instance.solid), against their turned bounding rectangle. */

int a2_scene_collide_objects(const a2_scene *s, int chunk, float *x, float *z, float radius,
                             float push[2], int *type_hit, int *inst_hit)
{
    if (!s || !s->instance_count || chunk < 0)
        return 0;
    int hit = 0;
    for (int i = -1; i <= 1; i++) {
        int c = a2_scene_wrap_chunk(s, chunk + i);
        if (c < 0)
            continue;
        for (int k = 0; k < s->chunk_inst_count[c]; k++) {
            const instance *in = &s->instances[s->chunk_inst_first[c] + k];
            if (!in->solid)
                continue;
            const gmesh *g = &s->meshes[in->mesh];
            if (!g->icount)
                continue;

            /* The original's shape (0x449a30 copies bbox corners 0, 1, 5, 4):
             * the mesh's XZ bounding rectangle, turned by the object's
             * heading.  The car is a circle here.  Work in the object's own
             * frame: glRotatef(yaw) maps local (lx, lz) to world
             * (lx cos + lz sin, -lx sin + lz cos). */
            float r = in->yaw * (float)M_PI / 180.0f;
            float sr = sinf(r), cr = cosf(r);
            float dx = *x - in->x, dz = *z - in->z;
            float lx = cr * dx - sr * dz, lz = sr * dx + cr * dz;
            float px = lx < g->min_x ? g->min_x : lx > g->max_x ? g->max_x : lx;
            float pz = lz < g->min_z ? g->min_z : lz > g->max_z ? g->max_z : lz;
            float ex = lx - px, ez = lz - pz;
            float d2 = ex * ex + ez * ez;
            float nx, nz; /* outward normal, local */
            if (d2 > 1e-8f) {
                if (d2 >= radius * radius)
                    continue;
                float d = sqrtf(d2);
                nx = ex / d;
                nz = ez / d;
                lx = px + nx * radius;
                lz = pz + nz * radius;
            } else { /* centre inside the rectangle: out by the nearest side */
                float l = lx - g->min_x, rgt = g->max_x - lx, n = lz - g->min_z, f = g->max_z - lz;
                float m = l < rgt ? l : rgt, m2 = n < f ? n : f;
                if (m < m2) {
                    nx = l < rgt ? -1.0f : 1.0f;
                    nz = 0.0f;
                    lx = (l < rgt ? g->min_x : g->max_x) + nx * radius;
                } else {
                    nx = 0.0f;
                    nz = n < f ? -1.0f : 1.0f;
                    lz = (n < f ? g->min_z : g->max_z) + nz * radius;
                }
            }
            /* back to world: the inverse of the rotation above */
            *x = in->x + cr * lx + sr * lz;
            *z = in->z - sr * lx + cr * lz;
            if (push) {
                push[0] = cr * nx + sr * nz;
                push[1] = -sr * nx + cr * nz;
            }
            if (type_hit)
                *type_hit = in->type;
            if (inst_hit)
                *inst_hit = (int)(in - s->instances);
            hit = 1;
        }
    }
    return hit;
}

/* ------------------------------------------------------------ knock-over props
 *
 * The .omp type handlers registered by 0x429310 (table 0x48c2b4) run every
 * frame for the objects in a window of chunks (0x4408fc).  Three of them knock
 * the object over:
 *
 *   type 2 pylons      0x4293f0  mass 4, hit sample 16 pionb1, bounce 17
 *                                pionb3, settles at pitch 0x4fb, lift 4
 *   type 3 barriers    0x4297d0  mass 8, samples 18 / 19 paal, 0x400, lift 2
 *   type 7 cameras     0x428d30  mass 10, samples 18 / 19, 0x400, lift 4, and
 *                                the radar bonus (0x4dc964 += 1, banner 51)
 *
 * Standing (phase 0), object_flyup_check_collision 0x428ea0 tests the player
 * (0x429350: centres closer than the two mesh radii, 0x446930) and launches
 * the object; flying (phase 1), 0x429020 moves it.  All of it per frame. */

#define TURN_UNITS 4096.0f
#define UNIT_DEG (360.0f / TURN_UNITS)

static int prop_kind(int type, float *mass, int *hit, int *bounce, int *pitch, float *lift)
{
    switch (type) {
    case 2: *mass = 4.0f; *hit = 16; *bounce = 17; *pitch = 0x4fb; *lift = 4.0f; return 1;
    case 3: *mass = 8.0f; *hit = 18; *bounce = 19; *pitch = 0x400; *lift = 2.0f; return 1;
    case 7: *mass = 10.0f; *hit = 18; *bounce = 19; *pitch = 0x400; *lift = 4.0f; return 1;
    }
    return 0;
}

/* 0x429020: one frame of flight; 1 on a bounce (the handler then sets the
 * bounce sample) */
static int prop_fly(const a2_scene *s, instance *in, float radius, int pitch, float lift)
{
    in->vx *= 0.98f;
    in->vz *= 0.98f;
    in->vy -= in->g;
    in->x += in->vx;
    in->y += in->vy;
    in->z += in->vz;
    float h;
    int ch = a2_scene_nearest_chunk(s, in->x, in->z);
    /* 0x442f00: the chunk's ground plane, defined everywhere */
    if (!a2_scene_road_height(s, ch, in->x, in->z, &h))
        h = in->y0;
    int bounced = 0;
    float d = in->y - h;
    if (d * d <= radius * radius) {
        in->vy *= -0.6f;
        in->y = h + 1.0f;
        if (in->vy < 2.0f) { /* settled */
            in->y = h + lift;
            in->phase = 2;
            in->rx = (float)pitch * UNIT_DEG;
            in->rz = 0.0f;
        }
        in->g *= 0.5f;
        bounced = 1;
    }
    in->rx += 64.0f * UNIT_DEG;
    in->ry += 32.0f * UNIT_DEG;
    in->rz += 48.0f * UNIT_DEG;
    return bounced;
}

int a2_scene_props_step(a2_scene *s, int lo, int hi, float px, float pz, float pradius,
                        float v_lat, float v_fwd, float heading, unsigned *rng)
{
    if (!s || !s->instance_count)
        return 0;
    int cameras = 0;
    for (int cc = lo; cc <= hi; cc++) {
        int c = a2_scene_wrap_chunk(s, cc); /* round the seam on a circuit */
        if (c < 0)
            continue;
        for (int k = 0; k < s->chunk_inst_count[c]; k++) {
            instance *in = &s->instances[s->chunk_inst_first[c] + k];
            float mass, lift;
            int hit, bounce, pitch;
            if (!prop_kind(in->type, &mass, &hit, &bounce, &pitch, &lift))
                continue;
            float radius = s->meshes[in->mesh].radius;
            if (in->phase == 0) {
                float dx = in->x - px, dz = in->z - pz, r = radius + pradius;
                if (dx * dx + dz * dz >= r * r)
                    continue;
                /* 0x428ea0: the player's heading +- 125 units, rand() % 250 */
                *rng = *rng * 214013u + 2531011u;
                int a = (int)heading + 125 - (int)((*rng >> 16) & 0x7fff) % 250;
                a &= 4095;
                float ang = (float)a * (6.28318548f / TURN_UNITS);
                float sn = sinf(ang), cs = cosf(ang);
                in->vx = cs * v_lat - 1.5f * sn * v_fwd;
                in->vz = sn * v_lat + 1.5f * cs * v_fwd;
                in->vy = (v_lat + v_fwd) / mass;
                in->g = 1.0f;
                in->phase = 1;
                in->pending = hit;
                if (in->type == 7)
                    cameras++;
            } else if (in->phase == 1) {
                if (prop_fly(s, in, radius, pitch, lift))
                    in->pending = bounce;
            }
        }
    }
    return cameras;
}

int a2_scene_prop_sounds(const a2_scene *s, a2_prop_sound *out, int max)
{
    int n = 0;
    if (!s)
        return 0;
    for (int i = 0; i < s->instance_count && n < max; i++) {
        const instance *in = &s->instances[i];
        if (!in->pending)
            continue;
        out[n].key = A2_PROP_KEY + i;
        out[n].x = in->x;
        out[n].z = in->z;
        out[n].sample = in->pending;
        n++;
    }
    return n;
}

void a2_scene_prop_sound_done(a2_scene *s, int key)
{
    int i = key - A2_PROP_KEY;
    if (s && i >= 0 && i < s->instance_count)
        s->instances[i].pending = 0;
}

void a2_scene_hide_type(a2_scene *s, int type)
{
    for (int i = 0; s && i < s->instance_count; i++)
        if (s->instances[i].type == type)
            s->instances[i].hidden = 1;
}

int a2_scene_chunk_point(const a2_scene *s, int chunk, float out[3])
{
    if (!s || chunk < 0 || chunk >= (int)s->chunk_count)
        return 0;
    out[0] = s->chunk_x[chunk];
    out[1] = s->chunk_y[chunk];
    out[2] = s->chunk_z[chunk];
    return 1;
}

int a2_scene_object_pos(const a2_scene *s, int inst, float *x, float *z)
{
    if (!s || inst < 0 || inst >= s->instance_count)
        return 0;
    *x = s->instances[inst].x;
    *z = s->instances[inst].z;
    return 1;
}

void a2_scene_object_push(a2_scene *s, int inst, float dvx, float dvz, float dw)
{
    if (!s || inst < 0 || inst >= s->instance_count)
        return;
    instance *in = &s->instances[inst];
    if (in->type != 8 && in->type != 10)
        return;
    in->vx += dvx;
    in->vz += dvz;
    in->spin += dw;
}

void a2_scene_objects_step(a2_scene *s, int lo, int hi, float dt)
{
    if (!s || !s->instance_count)
        return;
    if (lo < 0)
        lo = 0;
    if (hi >= (int)s->chunk_count)
        hi = (int)s->chunk_count - 1;
    for (int c = lo; c <= hi; c++)
        for (int k = 0; k < s->chunk_inst_count[c]; k++) {
            instance *in = &s->instances[s->chunk_inst_first[c] + k];
            if (in->type != 8 && in->type != 10)
                continue;
            if (in->vx == 0.0f && in->vz == 0.0f && in->spin == 0.0f)
                continue;
            /* 10 units a metre, as the integrator 0x4404a0 moves objects */
            in->x += 10.0f * dt * in->vx;
            in->z += 10.0f * dt * in->vz;
            in->yaw += in->spin * dt * 57.29578f;
            float k2 = in->type == 8 ? 0.95f : 0.9f;
            in->vx *= k2;
            in->vz *= k2;
            in->spin *= k2;
            if (fabsf(in->vx) < 0.01f && fabsf(in->vz) < 0.01f && fabsf(in->spin) < 0.001f)
                in->vx = in->vz = in->spin = 0.0f;
        }
}

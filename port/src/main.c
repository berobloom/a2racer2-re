/* a2view -- free-fly viewer for an A2 Racer II stage.
 *
 *   a2view <datadir> [etappe0] [--start-chunk N] [--atlas NAME.tga]
 *           [--autopilot] [--drive-test] [--no-traffic] [--no-objects]
 *           [--no-racers] [--no-cops] [--no-clock] [--screenshot out.ppm] [--frames N]
 *           [--sky N]  (1..7: back<N>.pcx, 0: the software renderer's scrolling sky)
 *           [--no-save] [--save]  (Game.ini results: never / even with --drive-test)
 *
 * Two modes, toggled with TAB:
 *   drive  -- a car on the road using CARS.DAT gear data, chase camera.
 *             Arrow keys steer/accelerate/brake, as the original binds them.
 *   fly    -- free camera (W/A/S/D, R/F, shift, mouse look)
 *
 * Drive-mode keys that mirror the original:
 *   1 2 3 4  camera views (near chase / cockpit / hold to look back / far chase);
 *   5 7 8 9 0  the tweak-mode views (Raser.ini TWEAK_MODE=1)
 *   M        tweak mode: the camera follows the next car (racers, then you)
 *   F12      tweak mode, held: the frame rate and the visible chunks
 *   Tab      tweak mode: the tweak menu (Up/Down, Space, numpad + and -)
 *   L        tweak mode, held: the player's chunk number
 *   D        cycle the overlay detail 0-3 (the original's 'detail' key)
 *
 * Port-only helpers: O scenery, G .cmp barriers, T traffic, H hides the HUD,
 *   P autopilot, [ ] draw distance, F2 screenshot, Esc quits.
 */
#include "anims.h"
#include "archive.h"
#include "cars.h"
#include "cops.h"
#include "gfx.h"
#include "handling.h"
#include "audio.h"
#include "drive.h"
#include "smoke.h"
#include "skid.h"
#include "paths.h"
#include "render.h"
#include "font.h"
#include "font3d.h"
#include "lang.h"
#include "racers.h"
#include "results.h"
#include "sky.h"
#include "trackinfo.h"
#include "sprites.h"
#include "traffic.h"
#include "track.h"
#include "wheels.h"
#include "contact.h"
#include "tweak.h"

#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Unit scales bridging CARS.DAT's numbers to this engine's time/world base.
 * These are ours; the per-car values they multiply are the game's. */
#define BRAKE_SCALE 40.0f
#define STEER_SCALE 12.0f
/* Maps our impact measure (m/s x how square the hit was) onto the game's own
 * 18000..35000 damage band.  The band is the game's; this scale is ours. */
#define IMPACT_SCALE 900.0f
/* How far back a square-on impact can throw the car, in m/s. */
#define REVERSE_MAX 6.0f

typedef struct {
    float pos[3];
    float yaw, pitch; /* degrees */
} camera;

/* The first contact in the player's move this frame (see the move below). */
enum { TOI_NONE, TOI_RAIL, TOI_OBJECT, TOI_TRAFFIC, TOI_RACER, TOI_COP };
typedef struct {
    int kind, idx;
    float n[2], p[2]; /* normal into the obstacle, contact point (world) */
} toi_hit;

/* That contact, if it was with this obstacle (idx -1: any of the kind). */
static int toi_take(const toi_hit *t, int kind, int idx, float n[2], float p[2])
{
    if (t->kind != kind || (idx >= 0 && t->idx != idx))
        return 0;
    n[0] = t->n[0];
    n[1] = t->n[1];
    p[0] = t->p[0];
    p[1] = t->p[1];
    return 1;
}

/* The impulse of one step's contact (0x4497f0, applied by 0x440fb0 right
 * after the step's move), with each obstacle's own parameters -- the same
 * ones the collision blocks after the move use:
 *   barriers   restitution 0.2 against the world
 *   scenery    columns immovable at 0.1, arrows 13000 kg and parked police
 *              cars 1300 kg at 0.2 (0x445441..0x445486), mean with the car's
 *   traffic    the vehicle table's mass, 0.2, the car takes its share
 *   racers     the car table's mass, 0.2; cops 0.25; they take their share
 * Returns the impulse's magnitude. */
static float step_impulse(a2_drive *drv, const toi_hit *h, float hl, float hw, float m,
                          a2_scene *scene, a2_traffic *traffic, a2_racers *racers,
                          a2_cops *cops, const a2_handling_table *handling)
{
    float dv2[3];
    switch (h->kind) {
    case TOI_RAIL:
        return a2_drive_impulse_at(drv, hl, hw, m, h->n[0], h->n[1], 0.2f, 0.0f, 0.0f, 0.0f, 1,
                                   NULL, NULL, 0.0f, 0.0f, drv->x, drv->z, h->p);
    case TOI_OBJECT: {
        int type = a2_scene_object_type(scene, h->idx);
        float om = type == 8 ? 13000.0f : type == 10 ? 1300.0f : 0.0f;
        float oe = type == 5 ? 0.1f : 0.2f;
        float opos[2] = {0.0f, 0.0f};
        a2_scene_object_pos(scene, h->idx, &opos[0], &opos[1]);
        float j = a2_drive_impulse_at(drv, hl, hw, m, h->n[0], h->n[1], 0.5f * (0.2f + oe), om,
                                      0.0f, 0.0f, om <= 0.0f, dv2, opos, 4.0f, 1.0f, drv->x,
                                      drv->z, h->p);
        if (om > 0.0f)
            a2_scene_object_push(scene, h->idx, dv2[0], dv2[1], dv2[2]);
        return j;
    }
    case TOI_TRAFFIC: {
        float ovx = 0.0f, ovz = 0.0f, om = 1000.0f;
        const char *oanim = NULL;
        if (traffic && a2_traffic_car_motion(traffic, h->idx, &ovx, &ovz, &oanim)) {
            int hi = oanim ? a2_handling_find(handling, oanim) : -1;
            if (hi >= 0 && handling->v[hi].valid && handling->v[hi].mass > 0.0f)
                om = handling->v[hi].mass;
        }
        float j = a2_drive_impulse_at(drv, hl, hw, m, h->n[0], h->n[1], 0.2f, om, ovx, ovz, 0,
                                      dv2, NULL, 0.0f, 0.0f, drv->x, drv->z, h->p);
        if (traffic)
            a2_traffic_car_push(traffic, h->idx, dv2[0], dv2[1]);
        return j;
    }
    case TOI_RACER:
    case TOI_COP: {
        int racer = h->kind == TOI_RACER;
        a2_drive *od = racer ? a2_racer_drive(racers, h->idx) : a2_cop_drive(cops, h->idx);
        if (!od)
            return 0.0f;
        int oslot = racer ? a2_racer_car(racers, h->idx) : 3;
        const char *oanim = racer ? a2_car_anim_name(oslot) : "MPOLIZEI_ANIM";
        float om = 1000.0f, ovx, ovz;
        int hi = a2_handling_find(handling, oanim);
        if (hi >= 0 && handling->v[hi].valid && handling->v[hi].mass > 0.0f)
            om = handling->v[hi].mass;
        a2_ai_world_velocity(od, &ovx, &ovz);
        float opos[2] = {od->x, od->z};
        float ol = 0.2f * a2_car_half_length(oslot), ow = 0.2f * a2_car_half_width(oslot);
        float j = a2_drive_impulse_at(drv, hl, hw, m, h->n[0], h->n[1], racer ? 0.2f : 0.25f, om,
                                      ovx, ovz, 0, dv2, opos, ol, ow, drv->x, drv->z, h->p);
        a2_ai_push(od, dv2[0], dv2[1], dv2[2]);
        return j;
    }
    }
    return 0.0f;
}

/* The engine's contact against one box (0x44a0f0, contact.h): the player's
 * box from where it started the frame, moved by this frame's step.  On a hit
 * the normal (into the other box) and the contact point replace the
 * push-out's; without one the push-out's stand. */
static int engine_contact(float pre_x, float pre_z, float pre_h, float half_wid, float half_len,
                          float dx, float dz, const a2_cbox *other, float n[2], float point[2])
{
    a2_cbox self = {-half_wid, -half_len, half_wid, half_len, pre_x, pre_z, pre_h};
    float t;
    int hit = a2_contact(&self, dx, dz, other, &t, point, n);
    if (getenv("A2_COL_LOG"))
        fprintf(stderr, hit ? "contact: engine, t %.2f, n (%.2f, %.2f)\n" : "contact: push-out\n", t,
                n[0], n[1]);
    return hit;
}

/* The view presets of fcn.00414B70 (table 0x41507C).  Each sets the engine
 * camera's offset in the car's frame (0x447770, camera +0xa4) and either a
 * look target -- the car plus (0, 10, 0), 0x4477f0 -- or none (0x4478c0: look
 * along the car).  `smooth` is what the arm does to the follow lag: set it to
 * 0.245 (0x4475a0), make the camera rigid (0x4476d0: off, factor 1), or
 * leave it as the last preset left it.  Preset 3 also hides the car (object
 * +0x2c = 0, which the vehicle draw loop 0x440aa2 tests). */
enum { CAM_KEEP, CAM_SMOOTH, CAM_RIGID };
static const struct {
    float x, y, z;
    int target, smooth, hide_car;
} cam_preset[11] = {
    {0.0f, 25.0f, -100.0f, 1, CAM_SMOOTH, 0}, /* 0: key 4, far chase */
    {0.0f, 25.0f, -60.0f, 1, CAM_SMOOTH, 0},  /* 1: key 1, near chase */
    {0.0f, 38.0f, -55.0f, 1, CAM_SMOOTH, 0},  /* 2 */
    {0.0f, 15.5f, -1.0f, 0, CAM_RIGID, 1},    /* 3: key 2, cockpit */
    {25.0f, 15.0f, -50.0f, 0, CAM_RIGID, 0},  /* 4: key 5 (tweak mode) */
    {0.0f, 23.0f, 40.0f, 1, CAM_KEEP, 0},     /* 5: key 3 held, look back */
    {-30.0f, 15.0f, 60.0f, 1, CAM_KEEP, 0},   /* 6: key 7 (tweak mode) */
    {25.0f, 15.0f, -50.0f, 1, CAM_KEEP, 0},   /* 7: key 8 (tweak mode) */
    {-20.0f, 9.0f, -30.0f, 1, CAM_KEEP, 0},   /* 8: key 9 (tweak mode) */
    {-25.0f, 55.0f, -30.0f, 1, CAM_KEEP, 0},  /* 9: key 0 (tweak mode) */
    {0.0f, 45.0f, -55.0f, 1, CAM_SMOOTH, 0},  /* 10 */
};

/* Brake lights, after txt_reml.cpp.
 *
 * The original stamps a lit or an unlit tail-light sprite into the player's
 * own texture sheet (0x42b100 -> blit_anim_frame_to_texture 0x42ae90), and
 * only for the player: the slot is the player's car type [0x4d9e2c].  Per
 * slot the 88-byte table at 0x48C968 gives the sheet and position; the frames
 * come from RC.IMG through anim 27 (hires), lit = frame 2*car, unlit =
 * 2*car+1.  Those anim entries are RC.IND indices **plus one** -- all 14 line
 * up exactly once shifted -- so the sprite numbers below are the 0-based ones.
 * Four slots match the shipped sheets pixel for pixel with their unlit sprite,
 * which is how the shift was confirmed.  Hires (anim 27) art is used because
 * the port renders the true-colour sheets. */
static const struct {
    const char *sheet;
    int x, y, lit, unlit;
} brake_light[7] = {
    {"RACER02.PCX", 246, 13, 338, 339},  /* 0 Trabant */
    {"RACER01.PCX", 109, 24, 333, 327},  /* 1 Astra */
    {"RACER02.PCX", 35, 163, 328, 329},  /* 2 Beetle */
    {"RACER01.PCX", 102, 164, 330, 331}, /* 3 BMW */
    {"RACER02.PCX", 95, 39, 336, 337},   /* 4 Porsche */
    {"RACER01.PCX", 242, 42, 332, 326},  /* 5 CLK */
    {"RACER01.PCX", 143, 132, 334, 335}, /* 6 Diablo */
};

/* The object-light brake lights, gfx_lght.cpp, which the shipped settings
 * use instead of the stamp above: in a 640x480 mode ([0x4d9aac]) with Game.ini
 * object_lights on ([0x4d9b0c]), 0x425f0b switches two light objects instead
 * of calling 0x42b100.  0x425500 makes them: OBJ_LIGHT01_ANIM (s_licht.d3d, a
 * small card facing back, on the glow in s_sfx) hung on the player's car at
 * the per-car positions of 0x485c28 / 0x485c34, pitched by a per-car angle
 * (4096 a turn) and tinted ARGB by 0x41b400, drawn alpha-blended (0x41b720). */
static const struct {
    float x, y, z;  /* the left light; the right one is at -x */
    float pitch;    /* 4096 units a turn */
    uint32_t argb;
} brake_obj[7] = {
    {-7.62f, 6.76f, -18.91f, 0.0f, 0xff960000u},   {-7.10f, 8.85f, -20.63f, 0.0f, 0xff960000u},
    {-6.25f, 7.70f, -16.70f, -500.0f, 0xffff0000u}, {-5.77f, 6.92f, -19.92f, 0.0f, 0xffff0000u},
    {-4.39f, 5.66f, -21.90f, 0.0f, 0xffff0000u},    {-6.29f, 5.00f, -22.50f, 0.0f, 0xa4ff0000u},
    {-6.47f, 8.77f, -22.90f, -180.0f, 0xa4ff0000u},
};

/* in the car's own frame */
static void draw_brake_lights(const a2_scene *scene, int model, int car_slot)
{
    if (model < 0 || car_slot < 0 || car_slot >= 7)
        return;
    uint32_t c = brake_obj[car_slot].argb;
    for (int side = 0; side < 2; side++) {
        glPushMatrix();
        glTranslatef(side ? -brake_obj[car_slot].x : brake_obj[car_slot].x, brake_obj[car_slot].y,
                     brake_obj[car_slot].z);
        glRotatef(brake_obj[car_slot].pitch * (360.0f / 4096.0f), 1.0f, 0.0f, 0.0f);
        a2_scene_draw_model_tinted(scene, model, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c,
                                   (uint8_t)(c >> 24));
        glPopMatrix();
    }
}

static void stamp_brake_light(const a2_scene *scene, const a2_sprites *rc, int car_slot, int lit)
{
    if (!rc || car_slot < 0 || car_slot >= 7)
        return;
    int w = 0, h = 0;
    uint8_t *px = a2_sprite_rgba(rc, lit ? brake_light[car_slot].lit : brake_light[car_slot].unlit,
                                 &w, &h);
    if (!px)
        return;
    a2_scene_blit(scene, brake_light[car_slot].sheet, brake_light[car_slot].x,
                  brake_light[car_slot].y, w, h, px);
    free(px);
}

/* Traffic signs, after tr_vbord.cpp and txt_main.cpp.  The gantries' faces are
 * not separate meshes: a trigger stamps a face into a_TRFFC1.PCX with
 * blit_anim_frame_to_texture (0x42ae90), when Game.ini texturecopy
 * ([0x4d9aa4], read at 0x421d53) is on.
 *  - Triggers 22+k and 31+k (handlers 0x42a640 + n*0x70, highway stages only)
 *    stamp direction board k: slots 8/9/10 (etappe 1/3/5) at (9, 122) and
 *    slots 11/12/13 at (9, 7), one per carriageway.  Their hires anims 29/31/33
 *    and 35/37/39 list RC.IMG sprites (+1, as everywhere); 0-based below.
 *  - Triggers 4/5/6/8 (0x42a340..0x42a3d0) post 50/70/100/120 and set the
 *    matrix sign through 0x42b190: slot 7 at (190, 220), anim 25.  Its jump
 *    table (0x42b200) maps them to the faces "50", "70", "100" and the X.
 *    50/70/100 only where the stage's track flag is 0 (the highways). */
static const int sign_board[3][2][4] = {
    {{368, 367, 366, 369}, {391, 390, 389, 392}}, /* etappe 1 */
    {{370, 371, 372, 373}, {393, 394, 395, 396}}, /* etappe 3 */
    {{374, 375, 376, 377}, {397, 398, 399, 400}}, /* etappe 5 */
};

static void stamp_sprite(const a2_scene *scene, const a2_sprites *rc, int sprite, const char *sheet,
                         int x, int y)
{
    int w = 0, h = 0;
    uint8_t *px = a2_sprite_rgba(rc, sprite, &w, &h);
    if (!px)
        return;
    a2_scene_blit(scene, sheet, x, y, w, h, px);
    free(px);
}

static void stamp_traffic_sign(const a2_scene *scene, const a2_sprites *rc, int etappe, int trigger,
                               int highway)
{
    if (!rc)
        return;
    switch (trigger) { /* 0x42b190 */
    case 4: if (highway) stamp_sprite(scene, rc, 416, "a_TRFFC1.PCX", 190, 220); return; /* 50 */
    case 5: if (highway) stamp_sprite(scene, rc, 413, "a_TRFFC1.PCX", 190, 220); return; /* 70 */
    case 6: if (highway) stamp_sprite(scene, rc, 418, "a_TRFFC1.PCX", 190, 220); return; /* 100 */
    case 8: stamp_sprite(scene, rc, 410, "a_TRFFC1.PCX", 190, 220); return;               /* X */
    default: break;
    }
    int set = etappe == 1 ? 0 : etappe == 3 ? 1 : etappe == 5 ? 2 : -1;
    int group = trigger >= 22 && trigger <= 25 ? 0 : trigger >= 31 && trigger <= 34 ? 1 : -1;
    if (set < 0 || group < 0)
        return; /* frames 4-8 (triggers 26-30, 35-39) are not in the anims */
    int k = trigger - (group ? 31 : 22);
    stamp_sprite(scene, rc, sign_board[set][group][k], "a_TRFFC1.PCX", 9, group ? 7 : 122);
}

/* The police voice pools of ev_cops.cpp: the warning for catch 1, 2 and 3
 * (tables 0x486648 / 0x486660 / 0x486678, picked with rand() % n), the
 * pursuit chatter played as a chase starts (0x486690, 13 lines) and the
 * give-up lines played as the cop switches its light off (0x4866c8, 5). */
static const int VOICE_CATCH[3][6] = {
    {48, 49, 50, 51, 52, 53}, {54, 55, 56, 57, 58, 0}, {59, 60, 61, 62, 63, 0}};
static const int VOICE_CATCH_N[3] = {6, 5, 5};

/* A private generator for the sound's random picks, so they never disturb
 * the simulation's own sequence. */
static unsigned snd_seed = 0x2468aceu;
static unsigned snd_rand(void)
{
    snd_seed = snd_seed * 1103515245u + 12345u;
    return snd_seed >> 16;
}

/* Road height for the driving model's ground test and ride spring. */
typedef struct {
    const a2_scene *scene;
    int chunk; /* the car's chunk this frame: the lookup also samples its
                * neighbours, so no whole-track search is needed */
} drive_ground;

static int drive_height(void *ud, float x, float z, float *y)
{
    const drive_ground *g = ud;
    return a2_scene_road_height(g->scene, g->chunk, x, z, y);
}

/* The port's heading convention (forward = (sin yaw, -cos yaw), degrees) and
 * the original's (forward = (-sin psi, cos psi), 4096 units per turn) differ
 * by half a turn. */
static float drive_yaw_deg(const a2_drive *d)
{
    return d->heading * (360.0f / 4096.0f) + 180.0f;
}

/* The radar, bottom-left.
 *
 * `hiradar.tga` and `loradar.tga` are blank 128x128 and 64x64 canvases filled
 * with the (255,0,127) key colour, so the original paints them at runtime --
 * gfx_radr.cpp has `create_radar` and `draw_road_in_radar_surface`, and the
 * result on screen is the road as a white ribbon with a black edge, the player
 * a yellow dot at the middle of it and the other cars red ones.  The port draws
 * the same picture straight into the overlay instead of into a surface.
 *
 * The road's centre line comes from the lane points of the chunks around the
 * player, turned into the player's frame so that the way ahead points up. */
static void hud_radar(const a2_scene *scene, const a2_traffic *traffic, const a2_racers *racers,
                      const a2_cops *cops, float cx, float cy, float half, float px, float pz,
                      float yaw_deg)
{
    enum { AHEAD = 10, BEHIND = 4 };
    const float range = 1600.0f; /* world units mapped onto `half` pixels */
    float sc = half / range;
    float a = yaw_deg * 3.14159265f / 180.0f;
    float ca = cosf(a), sa = sinf(a);
    /* World -> radar.  The car's heading vector is (sin yaw, -cos yaw), so its
     * right-hand side is (cos yaw, sin yaw); rotating into that frame and
     * flipping y puts the way ahead at the top of the radar. */
#define RADAR_XY(wx, wz, ox, oy)                                                                   \
    do {                                                                                           \
        float ddx = (wx)-px, ddz = (wz)-pz;                                                        \
        float fwd = ddx * sa - ddz * ca, side = ddx * ca + ddz * sa;                               \
        (ox) = cx + side * sc;                                                                     \
        (oy) = cy - fwd * sc;                                                                      \
    } while (0)

    int here = a2_scene_nearest_chunk(scene, px, pz);
    int total = a2_scene_chunk_count(scene);
    float pts[(AHEAD + BEHIND + 1) * 2];
    int n = 0;
    for (int d = -BEHIND; d <= AHEAD; d++) {
        int c = here + d;
        if (c < 0 || c >= total)
            continue;
        int lanes = a2_scene_lane_count(scene, c);
        if (lanes <= 0)
            continue;
        float sum[2] = { 0.0f, 0.0f };
        int got = 0;
        for (int l = 0; l < lanes; l++) {
            float q[3];
            if (!a2_scene_lane_point(scene, c, l, q))
                continue;
            sum[0] += q[0];
            sum[1] += q[2];
            got++;
        }
        if (!got)
            continue;
        RADAR_XY(sum[0] / got, sum[1] / got, pts[n * 2], pts[n * 2 + 1]);
        n++;
    }
    if (n < 2)
        return;

    glDisable(GL_TEXTURE_2D);
    /* The ribbon is a dark edge with a white core drawn over it. */
    for (int pass = 0; pass < 2; pass++) {
        glLineWidth(pass == 0 ? 9.0f : 5.0f);
        if (pass == 0)
            glColor4f(0.0f, 0.0f, 0.0f, 0.85f);
        else
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBegin(GL_LINE_STRIP);
        for (int i = 0; i < n; i++)
            glVertex2f(pts[i * 2], pts[i * 2 + 1]);
        glEnd();
    }
    glLineWidth(1.0f);

    /* Dots: the other cars first, then the player over them. */
    float dot = half * 0.085f;
    for (int kind = 0; kind < 4; kind++) {
        int count = kind == 0   ? a2_traffic_count(traffic)
                    : kind == 1 ? a2_racers_count(racers)
                    : kind == 2 ? a2_cops_count(cops)
                                : 1;
        for (int i = 0; i < count; i++) {
            float w[2], ox, oy;
            if (kind == 0) {
                if (!a2_traffic_car_xz(traffic, i, w))
                    continue;
            } else if (kind == 1) {
                if (!a2_racers_car_xz(racers, i, w))
                    continue;
            } else if (kind == 2) {
                if (!a2_cops_car_xz(cops, i, w))
                    continue;
            } else {
                w[0] = px;
                w[1] = pz;
            }
            RADAR_XY(w[0], w[1], ox, oy);
            if (ox < cx - half || ox > cx + half || oy < cy - half || oy > cy + half)
                continue;
            if (kind == 3)
                glColor4f(1.0f, 0.85f, 0.0f, 1.0f); /* the player */
            else if (kind == 2)
                glColor4f(0.25f, 0.45f, 1.0f, 1.0f); /* a cop */
            else
                glColor4f(0.88f, 0.09f, 0.09f, 1.0f);
            glBegin(GL_TRIANGLE_FAN);
            glVertex2f(ox, oy);
            for (int k = 0; k <= 10; k++) {
                float th = (float)k / 10.0f * 6.2831853f;
                glVertex2f(ox + cosf(th) * dot, oy + sinf(th) * dot);
            }
            glEnd();
        }
    }
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glEnable(GL_TEXTURE_2D);
#undef RADAR_XY
}

/* Did the car pass chunk `target` between last frame and this one?
 *
 * Chunk indices run forward and wrap at the end of a circuit, and a frame can
 * step over more than one, so this tests whether `target` lies in the half-open
 * arc (prev, now] rather than comparing for equality. */
static int crossed_chunk(int prev, int now, int target, int total)
{
    if (prev < 0 || target < 0 || total <= 0 || prev == now)
        return 0;
    int span = now - prev;
    if (span < 0)
        span += total;
    if (span <= 0 || span > total / 2)
        return 0; /* a jump that large is a reset, not driving */
    int off = target - prev;
    if (off < 0)
        off += total;
    return off > 0 && off <= span;
}

/* One of the HUD's white brackets.
 *
 * RC.IMG sprite 425 holds the top-right pieces and 426 the bars, the speed
 * bracket and the "km/h" mark, each sheet packing several pieces with gaps
 * between them.  Every piece is a rounded left cap with a rail that runs out to
 * the piece's own right edge, and the original simply stretches the whole piece
 * to the size the element needs -- which is why a wide bracket has a thicker
 * rail than a narrow one, exactly as the original's do. */
static void hud_bracket(const a2_sprites *s, int idx, int sx, int sy, int sw, int sh, float x,
                        float y, float w, float h, float u)
{
    (void)u;
    a2_sprite_draw_sub(s, idx, sx, sy, sw, sh, x, y, w, h);
}

static void camera_forward(const camera *c, float out[3])
{
    float cy = cosf(c->yaw * (float)M_PI / 180.0f);
    float sy = sinf(c->yaw * (float)M_PI / 180.0f);
    float cp = cosf(c->pitch * (float)M_PI / 180.0f);
    float sp = sinf(c->pitch * (float)M_PI / 180.0f);
    out[0] = sy * cp;
    out[1] = sp;
    out[2] = -cy * cp;
}

static void apply_view(const camera *c)
{
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* forward is (sin yaw, sin pitch, -cos yaw); undoing that needs a +yaw
     * rotation about Y and a -pitch rotation about X.  (The yaw sign was wrong
     * here at first and only showed up on stages whose heading is not 0.) */
    glRotatef(-c->pitch, 1.0f, 0.0f, 0.0f);
    glRotatef(c->yaw, 0.0f, 1.0f, 0.0f);
    glTranslatef(-c->pos[0], -c->pos[1], -c->pos[2]);
}

/* The vertical view angle, measured off the original (session 14): its
 * Trabant on the start grid in preset 1, at rest so the camera sits exactly
 * on the preset, is 136 px wide on a 640x480 capture; the port's draws 152 px
 * at 60 degrees and 125 at 70, i.e. width x tan(fov/2) = 87.6, so 65.6
 * degrees (a focal length of about 373 px at 480 lines).  The projection
 * code itself is not traced.  Wider windows keep it and see more sideways. */
static float view_fov = 65.6f; /* vertical, degrees */

static void set_projection(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    /* The world is huge (chunks are 128 units apart), so the far plane is
     * generous.  (0x49B014, once read as a focal constant of 1024, is the sine
     * table's size; see discoveries.md.) */
    float aspect = h ? (float)w / (float)h : 1.0f;
    float fov = view_fov, znear = 1.0f, zfar = 60000.0f;
    float top = znear * tanf(fov * (float)M_PI / 360.0f);
    float right = top * aspect;
    glFrustum(-right, right, -top, top, znear, zfar);
    /* The original is Direct3D: left-handed, its screen right the car's
     * lateral axis (cos psi, sin psi).  This camera's right, forward x up, is
     * the opposite vector, so without this flip every stage showed mirrored
     * (the START/FINISH banner read backwards against a Wine capture). */
    glScalef(-1.0f, 1.0f, 1.0f);
    glViewport(0, 0, w, h);
}

/* Is the key with Windows VK code `vk` down?  Game.ini [INPUT] stores the
 * bindings as VK codes (the menu's controls screen offers the 44 keys of its
 * table at menu.dat 0x42a4b0: A..Z, NUMPAD 0..9, the arrows, SPACE, ENTER,
 * ALT, CTRL); spel.dat reads them at 0x42393b. */
static int vk_down(const Uint8 *k, int vk)
{
    if (vk >= 'A' && vk <= 'Z')
        return k[SDL_SCANCODE_A + (vk - 'A')];
    if (vk >= 97 && vk <= 105)
        return k[SDL_SCANCODE_KP_1 + (vk - 97)];
    switch (vk) {
    case 96: return k[SDL_SCANCODE_KP_0];
    case 37: return k[SDL_SCANCODE_LEFT];
    case 38: return k[SDL_SCANCODE_UP];
    case 39: return k[SDL_SCANCODE_RIGHT];
    case 40: return k[SDL_SCANCODE_DOWN];
    case 32: return k[SDL_SCANCODE_SPACE];
    case 13: return k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_KP_ENTER];
    case 17: return k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL];
    case 18: return k[SDL_SCANCODE_LALT] || k[SDL_SCANCODE_RALT];
    }
    return 0;
}

/* One frame of player input, by Game.ini [INPUT] DEVICE.  spel.dat builds a
 * key mask with 0x4246c0 (LEFT 1, RIGHT 2, UP 4, DOWN 8, EXTRA1 0x10, EXTRA2
 * 0x20) and hands it to the handler set_device picked (dispatch 0x42614d). */
typedef struct {
    unsigned keys;     /* A2_KEY_* */
    int turbo;         /* the turbo tick 0x425cf0's argument */
    int handbrake;     /* 0x4d9df8 */
    int analog;        /* the wheel: throttle, brake and steer are set directly */
    float throttle, brake, steer;
} a2_input;

/* An axis as winmm reports it, 0..65535 with the centre at 32767. */
static int joy_raw(SDL_Joystick *j, int axis) { return SDL_JoystickGetAxis(j, axis) + 32768; }

/* get_joystick_position 0x46ed90 for a range of 100: within 10 % of the
 * centre the axis reads 50; outside it, raw / ((max - centre) / 50) on either
 * side (integer divisions), so 0..45 and 55..100.  Y is reversed (100 - ...)
 * so pushing forward reads high.  0x4247f0 clamps to 0..100. */
static int joy_pos(int raw, int reverse)
{
    const int c = 32767, div = (65535 - c) / 50;
    int v = 50;
    if ((double)raw > c + 0.1 * c || (double)raw < c - 0.1 * c)
        v = reverse ? 100 - raw / div : raw / div;
    return v < 0 ? 0 : v > 100 ? 100 : v;
}

static a2_input read_input(int device, SDL_Joystick *joy, const Uint8 *k, int key_left,
                           int key_right, int key_up, int key_down, int key_turbo,
                           int key_handbrake)
{
    a2_input in = {0};
    if (device == 2) {
        /* Joypad.  set_device(2) binds the stick and buttons to the keys
         * (0x4245d2, set_joystick_keys 0x46ec80): stick left LEFT, right
         * RIGHT, down DOWN, button 1 UP, button 2 EXTRA1; stick up and
         * buttons 3-4 get nothing and EXTRA2 no control.  A direction counts
         * past 10 % of the centre (0x46eac0).  The mask reads the stick only,
         * not the keyboard (source 2, 0x466960). */
        const double c = 32767.0;
        int x = joy_raw(joy, 0), y = joy_raw(joy, 1);
        if (x < c - 0.1 * c)
            in.keys |= A2_KEY_LEFT;
        if (x > c + 0.1 * c)
            in.keys |= A2_KEY_RIGHT;
        if (y > c + 0.1 * c)
            in.keys |= A2_KEY_DOWN;
        if (SDL_JoystickGetButton(joy, 0))
            in.keys |= A2_KEY_UP;
        in.turbo = SDL_JoystickGetButton(joy, 1);
        /* handler 0x4269c0: the handbrake is EXTRA2 or Space (0x426a02), and
         * holding the turbo holds the gas (`or ebx, 4` at 0x426a34) */
        in.handbrake = k[SDL_SCANCODE_SPACE];
        if (in.turbo)
            in.keys |= A2_KEY_UP;
        return in;
    }
    if (device == 3) {
        /* Wheel / joystick, handler 0x426e00 (stuur_model 0 arm 0x426e88):
         * steer 2x - 100; Y at or above the middle is gas 2y - 100 with no
         * brake, below it brake 2 * (50 - y) with no gas.  Button 1 is the
         * handbrake, button 2 the turbo (0x46ef63 makes them bits 0x10 and
         * 0x20).  The SENSITIVITY setting only shapes the old model's table
         * (0x425862), so it does nothing here. */
        int x = joy_pos(joy_raw(joy, 0), 0), y = joy_pos(joy_raw(joy, 1), 1);
        in.analog = 1;
        if (y >= 50) {
            in.throttle = (float)(2 * y - 100);
            in.brake = 0.0f;
        } else {
            in.throttle = 0.0f;
            in.brake = (float)(2 * (50 - y));
        }
        in.steer = (float)(2 * x - 100);
        if (x < 50)
            in.keys |= A2_KEY_LEFT;
        if (x > 50)
            in.keys |= A2_KEY_RIGHT;
        if (in.throttle > 0.0f)
            in.keys |= A2_KEY_UP;
        if (in.brake > 0.0f)
            in.keys |= A2_KEY_DOWN;
        in.handbrake = SDL_JoystickGetButton(joy, 0);
        in.turbo = SDL_JoystickGetButton(joy, 1);
        return in;
    }
    /* Keyboard, handler 0x4265b0.  Unlike the joypad handler it has no Space
     * handbrake and the turbo does not hold the gas: the handbrake is EXTRA2
     * alone (0x4265e5) and EXTRA1 only ticks the turbo.  Checked under Wine:
     * holding only Ctrl on the grid for six seconds left the car at 0 km/h. */
    in.keys = (vk_down(k, key_left) ? A2_KEY_LEFT : 0) | (vk_down(k, key_right) ? A2_KEY_RIGHT : 0) |
              (vk_down(k, key_up) ? A2_KEY_UP : 0) | (vk_down(k, key_down) ? A2_KEY_DOWN : 0);
    in.turbo = vk_down(k, key_turbo);
    in.handbrake = vk_down(k, key_handbrake);
    return in;
}

static int write_ppm(const char *path, int w, int h)
{
    uint8_t *px = malloc((size_t)w * h * 3);
    if (!px)
        return -1;
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px);
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(px);
        return -1;
    }
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int y = h - 1; y >= 0; y--) /* GL origin is bottom-left */
        fwrite(px + (size_t)y * w * 3, 1, (size_t)w * 3, f);
    fclose(f);
    free(px);
    fprintf(stderr, "wrote %s\n", path);
    return 0;
}

/* Vehicle model driven entirely by CARS.DAT.  Every coefficient below is the
 * car's own value under the name the developers gave it in the TWEAK_MODE menu
 * (see cars.h); only the unit scales are ours, because the game's time base and
 * world units are not the SI ones these numbers are expressed in. */
typedef struct {
    float x, y, z;
    float yaw;   /* degrees */
    float speed; /* m/s, matching CARS.DAT units */
    int gear;
    /* Body attitude, driven by CARS.DAT's three (damping, factor) pairs --
     * tweak menu items 7 "Rolhoek factoren", 8 "Pitch factoren" and 9 "Hoogte
     * factoren".  `factor` sets how far the body moves, `damping` how quickly
     * it gets there; both are the car's own numbers, only the scale is ours. */
    float roll, pitch_ang, ride;
    /* Degrees between where the body points and where the car is going:
     * travel is along `yaw - slip`.  Zero except in and just after a handbrake
     * slide -- the only thing in the port that breaks the rear loose. */
    float slip;
    /* Equipment from Game.ini [PLAYER]: `abs` is the flag 0x4a63c8 that the
     * upgrade applier 0x428400 sets and only the brake function reads. */
    int abs;
    /* Engine rpm, for the engine sound (the original's 0x4a63d8): pinned to
     * 800 at standstill (0x40f391). */
    float rpm;
} vehicle;


/* Steer toward this lane's waypoint a few chunks ahead.  The lane graph is the
 * game's own driving line, so following it keeps the car on the road through
 * curves; the same routine is what traffic AI would use. */
static float lane_follow_steer(const a2_scene *scene, const vehicle *v, int chunk, int lane,
                               int lookahead)
{
    /* Lane counts vary from chunk to chunk (etappe0 is mostly 2 lanes, etappe5
     * runs 3 to 6), so `lane` is clamped into whatever the lookahead chunk
     * offers.  The caller owns the lane choice: it normally tracks the lane the
     * car is actually in, but holds a different one while overtaking. */
    float target[3];
    int c = chunk + lookahead;
    int n = a2_scene_lane_count(scene, c);
    while (lookahead > 1 && n <= 0) {
        lookahead--;
        c = chunk + lookahead;
        n = a2_scene_lane_count(scene, c);
    }
    if (n <= 0)
        return 0.0f;
    int tl = lane < n ? lane : n - 1;
    if (!a2_scene_lane_point(scene, c, tl, target))
        return 0.0f;

    float dx = target[0] - v->x, dz = target[2] - v->z;
    if (dx * dx + dz * dz < 1e-6f)
        return 0.0f;
    float want = atan2f(dx, -dz) * 180.0f / (float)M_PI;
    float diff = want - v->yaw;
    while (diff > 180.0f)
        diff -= 360.0f;
    while (diff < -180.0f)
        diff += 360.0f;
    float steer = diff / 20.0f; /* full lock at 20 degrees of error */
    if (steer > 1.0f)
        steer = 1.0f;
    if (steer < -1.0f)
        steer = -1.0f;
    return steer;
}

/* Is `lane` blocked by traffic over the next few chunks?  Probing the lane's
 * own waypoints is exact: traffic drives the waypoint segments, so a car in
 * that lane is always close to one of them. */
static int lane_blocked(const a2_scene *scene, const a2_traffic *traffic, int chunk, int lane)
{
    for (int d = 2; d <= 7; d++) {
        int c = chunk + d;
        int n = a2_scene_lane_count(scene, c);
        if (n <= 0)
            continue;
        float p[3];
        if (!a2_scene_lane_point(scene, c, lane < n ? lane : n - 1, p))
            continue;
        /* Lateral tolerance only: roughly half a lane, since the vehicle's
         * length is now covered by the capsule test inside a2_traffic_occupied.
         * Lanes are 40 units apart, so this must stay well under that or every
         * neighbouring lane reads as blocked. */
        if (a2_traffic_occupied(traffic, p[0], p[2], 18.0f))
            return 1;
    }
    return 0;
}

#define collide_impulse a2_drive_impulse


int main(int argc, char **argv)
{
    const char *datadir = argc > 1 ? argv[1] : ".";
    const char *stem = "etappe0";
    const char *shot = NULL;
    const char *atlas = NULL;
    int max_frames = 0;
    float gb_gas = -1.0f, gb_brake = 0.0f; /* --gas-brake: scripted keys */
    int drive_test = 0;
    int start_chunk = -1; /* <0 = derive it the way the game does; see below */
    int autopilot = 0;
    int no_traffic = 0;
    int no_objects = 0;
    int no_cops = 0;
    int no_sound = 0;
    int no_clock = 0;
    int no_racers = 0;
    int no_save = 0;    /* --no-save: leave Game.ini alone */
    int force_save = 0; /* --save: write it even from a --drive-test run */
    int sky_back = -1; /* --sky N: back<N>.pcx, 0 the scrolling panorama, <0 random */

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc)
            shot = argv[++i];
        else if (strcmp(argv[i], "--atlas") == 0 && i + 1 < argc)
            atlas = argv[++i];
        else if (strcmp(argv[i], "--drive-test") == 0)
            drive_test = 1;
        else if (strcmp(argv[i], "--autopilot") == 0)
            autopilot = 1;
        else if (strcmp(argv[i], "--no-traffic") == 0)
            no_traffic = 1;
        else if (strcmp(argv[i], "--no-objects") == 0)
            no_objects = 1;
        else if (strcmp(argv[i], "--no-cops") == 0)
            no_cops = 1;
        else if (strcmp(argv[i], "--no-sound") == 0)
            no_sound = 1;
        else if (strcmp(argv[i], "--no-clock") == 0)
            no_clock = 1;
        else if (strcmp(argv[i], "--no-save") == 0)
            no_save = 1;
        else if (strcmp(argv[i], "--save") == 0)
            force_save = 1;
        else if (strcmp(argv[i], "--sky") == 0 && i + 1 < argc)
            sky_back = atoi(argv[++i]);
        else if (strcmp(argv[i], "--no-racers") == 0)
            no_racers = 1;
        else if (strcmp(argv[i], "--start-chunk") == 0 && i + 1 < argc)
            start_chunk = atoi(argv[++i]);
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
            max_frames = atoi(argv[++i]);
        else if (strcmp(argv[i], "--gas-brake") == 0 && i + 2 < argc) {
            /* hold the gas G s of race time, then the brake B s, the autopilot
             * steering: about the input of wine_chase_run.sh */
            gb_gas = (float)atof(argv[++i]);
            gb_brake = (float)atof(argv[++i]);
        }
        else if (argv[i][0] != '-')
            stem = argv[i];
    }

    char prefix[8];
    snprintf(prefix, sizeof prefix, "e%02d", (int)(stem[strlen(stem) - 1] - '0'));

    a2_track track;
    if (a2_track_load(&track, datadir, stem) != 0) {
        fprintf(stderr, "failed to load track %s from %s\n", stem, datadir);
        return 1;
    }

    char img[512], ind[512];
    snprintf(img, sizeof img, "%s/RCS.IMG", datadir);
    snprintf(ind, sizeof ind, "%s/RCS.IND", datadir);
    a2_archive rcs;
    if (a2_archive_open(&rcs, img, ind) != 0) {
        a2_track_free(&track);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    /* The drive test measures physics, not pixels; a small window keeps a
     * software-GL run from taking minutes. */
    int win_w = drive_test ? 320 : 1280, win_h = drive_test ? 240 : 720;
    /* Debug: A2_WIN=<w>x<h> sets the window, A2_FOV=<deg> the vertical view
     * angle (for matching captures of the original). */
    if (getenv("A2_WIN"))
        sscanf(getenv("A2_WIN"), "%dx%d", &win_w, &win_h);
    if (getenv("A2_FOV"))
        view_fov = (float)atof(getenv("A2_FOV"));
    SDL_Window *win = SDL_CreateWindow("A2 Racer II - track viewer", SDL_WINDOWPOS_CENTERED,
                                       SDL_WINDOWPOS_CENTERED, win_w, win_h,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx) {
        fprintf(stderr, "SDL_GL_CreateContext: %s\n", SDL_GetError());
        return 1;
    }
    fprintf(stderr, "GL %s / %s\n", (const char *)glGetString(GL_VERSION),
            (const char *)glGetString(GL_RENDERER));

    a2_anims *anims = a2_anims_load(datadir);
    if (anims)
        fprintf(stderr, "anims: %d anim definitions from spel.dat\n", a2_anims_count(anims));

    /* Raser.ini's [MAPS] row for this track: the game's own configuration, so
     * the plane-list textures, the circuit flag and the menu name all come
     * from data rather than from guesses here. */
    /* Game.ini [OPTIONS] Nrlaps, default 2 -- the game's own default. */
    int nr_laps = a2_ini_int(datadir, "Game.ini", "[OPTIONS]", "Nrlaps", 2);
    /* Game.ini [INPUT]: the bound keys as Windows VK codes (spel.dat 0x42393b) */
    int key_left = a2_ini_int(datadir, "Game.ini", "[INPUT]", "LEFT", 37);
    int key_right = a2_ini_int(datadir, "Game.ini", "[INPUT]", "RIGHT", 39);
    int key_up = a2_ini_int(datadir, "Game.ini", "[INPUT]", "UP", 38);
    int key_down = a2_ini_int(datadir, "Game.ini", "[INPUT]", "DOWN", 40);
    int key_turbo = a2_ini_int(datadir, "Game.ini", "[INPUT]", "EXTRA1", 17);
    int key_handbrake = a2_ini_int(datadir, "Game.ini", "[INPUT]", "EXTRA2", 18);
    /* DEVICE (set_device 0x424590): 1 keyboard, 2 joypad, 3 wheel/joystick,
     * 4 Sidewinder Force Feedback.  The menu writes 1, 2 or 3. */
    int device = a2_ini_int(datadir, "Game.ini", "[INPUT]", "DEVICE", 1);
    SDL_Joystick *joy = NULL;
    if (device == 2 || device == 3) {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) == 0 && SDL_NumJoysticks() > 0)
            joy = SDL_JoystickOpen(0);
        if (joy)
            fprintf(stderr, "input: DEVICE %d on %s\n", device, SDL_JoystickName(joy));
        else
            fprintf(stderr, "input: DEVICE %d but no joystick; using the keyboard\n", device);
    }
    if (device == 4) /* DirectInput force feedback, old steering model only */
        fprintf(stderr, "input: DEVICE 4 is not supported; using the keyboard\n");
    if (!joy)
        device = 1;
    /* 0 = EASY, 1 = HARD; picks the 6-or-12 per-impact damage maximum. */
    int difficulty = a2_ini_int(datadir, "Game.ini", "[OPTIONS]", "difficulty", 1);
    /* Where the player starts.  The original reads Game.ini [OPTIONS] STARTCHUNK
     * (a test variable, commented out in the shipped file) and, when it is
     * unset, derives it in init_game from the chunk carrying **trigger 13** --
     * starting one chunk before it (fcn.0042A460).  --start-chunk overrides
     * both, as the ini key does in the original. */
    if (start_chunk < 0)
        start_chunk = a2_ini_int(datadir, "Game.ini", "[OPTIONS]", "startchunk", -1);
    if (start_chunk < 0) {
        for (uint32_t i = 0; i < track.pmp.chunk_count; i++) {
            const a2_pmp_chunk *c = &track.pmp.chunks[i];
            if (c->trigger0 == 13 || c->trigger1 == 13) {
                start_chunk = i > 0 ? (int)i - 1 : (int)track.pmp.chunk_count - 1;
                fprintf(stderr, "race: start line (trigger 13) at chunk %u, starting at %d\n",
                        i, start_chunk);
                break;
            }
        }
    }
    if (start_chunk < 0)
        start_chunk = 4;
    if (nr_laps < 1)
        nr_laps = 1;
    a2_trackinfo ti[A2_MAX_TRACKS];
    int ti_count = a2_trackinfo_load(ti, A2_MAX_TRACKS, datadir);
    int ti_idx = ti_count ? a2_trackinfo_find(ti, ti_count, stem) : -1;
    const char *plane_tex[4] = {NULL, NULL, NULL, NULL};
    /* The state spel.dat takes from Game.ini at start-up (money, damage,
     * last finishing position, this track's records), and the finished=0 it
     * writes straight away, so that a quit or a crash reads as not finished
     * (0x421b59).  Test runs (--drive-test) leave the file alone unless
     * --save is given. */
    int saving = !no_save && (!drive_test || force_save);
    a2_save save;
    a2_save_load(&save, datadir, ti_idx >= 0 ? ti_idx : 0);
    if (saving)
        a2_save_mark_unfinished(datadir);
    /* The player is created at STARTCHUNK - position (0x4256e0): the last
     * finishing position puts them that many chunks further back. */
    int player_chunk0 = start_chunk - save.position;
    if (player_chunk0 < 0)
        player_chunk0 += (int)track.pmp.chunk_count;
    if (ti_idx >= 0) {
        for (int i = 0; i < 4; i++)
            plane_tex[i] = ti[ti_idx].tex[i];
        fprintf(stderr, "track: \"%s\" (%s), textures %s/%s/%s/%s, music %d\n",
                ti[ti_idx].name, ti[ti_idx].circuit ? "city circuit" : "point-to-point",
                ti[ti_idx].tex[0], ti[ti_idx].tex[1], ti[ti_idx].tex[2], ti[ti_idx].tex[3],
                ti[ti_idx].music);
    }

    /* Game.ini gamma (shipped 1, "dark"): squares every texture colour */
    a2_gfx_set_gamma(a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "gamma", 1));
    a2_scene *scene = a2_scene_create(&track, &rcs, prefix, ti_idx >= 0 ? plane_tex : NULL,
                                      atlas, anims);
    if (!scene) {
        fprintf(stderr, "failed to build scene\n");
        return 1;
    }
    char stats[256];
    a2_scene_stats(scene, stats, sizeof stats);
    fprintf(stderr, "scene: %s\n", stats);

    camera cam;
    memset(&cam, 0, sizeof cam);
    a2_scene_start_position(scene, cam.pos, &cam.yaw);
    cam.pitch = -15.0f;

    /* HUD art is 8-bit and needs a palette; the game takes the per-track one
     * named by Raser.ini [PALET] (e00_pal.pcx for etappe0), which is a 16x16
     * PCX acting purely as a palette carrier. */
    a2_sprites *hud = NULL;
    /* The HUD's own bitmap fonts.  The labels are drawn with one of the 17
     * fonts in the table at VA 0x486D08 (see font.h) and the big readouts with
     * the 3D mesh font, which is why the numbers are bevelled and the labels
     * are flat. */
    a2_font *fnt_label = NULL, *fnt_time = NULL, *fnt_speed = NULL, *fnt_value = NULL,
            *fnt_pct = NULL;
    {
        /* Raser.ini's [PALET] gives two palettes per track -- "first arg is the
         * version for 8-bit mode, second is the version for 16-bit mode" --
         * and every track names the same 16-bit one, e02_pl16.pcx.  The port
         * renders in true colour, so that is the one whose colours the HUD art
         * was authored against; the 8-bit per-track palette is the fallback. */
        char palname[32];
        snprintf(palname, sizeof palname, "e02_pl16.pcx");
        long pi = a2_archive_find(&rcs, palname);
        if (pi < 0) {
            snprintf(palname, sizeof palname, "%s_pal.pcx", prefix);
            pi = a2_archive_find(&rcs, palname);
        }
        if (pi < 0) {
            snprintf(palname, sizeof palname, "e00_pal.pcx");
            pi = a2_archive_find(&rcs, palname);
        }
        if (pi >= 0) {
            uint32_t psz = 0;
            const uint8_t *pd = a2_archive_data(&rcs, (size_t)pi, &psz);
            uint8_t pal[768];
            if (pd && a2_pcx_palette(pal, pd, psz) == 0) {
                hud = a2_sprites_load(datadir, pal);
                fprintf(stderr, "hud: palette %s\n", palname);
                /* The HUD's four faces, and their glyph sets say exactly what
                 * each one is for -- a font carries nothing it does not need:
                 *
                 *   olfnt01  h17  ' ' A-Z             every label
                 *   olfnt02  h25  0-9 . - / :         race time, POSITIE, RONDE
                 *   olfnt03  h47  0-9                 the speed readout
                 *   olfnt04  h15  0-9 . - :           the lap-info times
                 *
                 * olfnt01 is flat white and the other three are blue with a
                 * bevel, which is the split you see on screen.  They are
                 * entries 10, 11, 0 and 12 of the hires half of the font table
                 * at VA 0x486D08 (font.h); the lores halves are lo_fnt1..4,
                 * and loading those is what a 320x240 original would do. */
                fnt_label = a2_font_load(&rcs, "olfnt01.fnt", pal);
                if (!fnt_label)
                    fnt_label = a2_font_load(&rcs, "lo_fnt1.fnt", pal);
                fnt_time = a2_font_load(&rcs, "olfnt02.fnt", pal);
                if (!fnt_time)
                    fnt_time = a2_font_load(&rcs, "lo_fnt2.fnt", pal);
                fnt_speed = a2_font_load(&rcs, "olfnt03.fnt", pal);
                if (!fnt_speed)
                    fnt_speed = a2_font_load(&rcs, "lo_fnt3.fnt", pal);
                fnt_value = a2_font_load(&rcs, "olfnt04.fnt", pal);
                if (!fnt_value)
                    fnt_value = a2_font_load(&rcs, "lo_fnt4.fnt", pal);
                /* The damage percentage needs a '%', which none of the four
                 * carries; slider13 is the small white face that has one. */
                fnt_pct = a2_font_load(&rcs, "slider13.fnt", pal);
                if (!fnt_pct)
                    fnt_pct = a2_font_load(&rcs, "lo_fnt4.fnt", pal);
            }
        }
    }

    a2_font3d *font = a2_font3d_load(scene, &rcs);
    /* The accelerated game picks back<rand() % 7 + 1>.pcx per race (0x42357c). */
    if (sky_back < 0)
        sky_back = (int)(time(NULL) % 7) + 1;
    a2_sky *sky = a2_sky_load(&rcs, sky_back);
    a2_lang *lang = a2_lang_load(datadir, -1);

    /* Checkpoints are the .pmp trigger slot value 45.  Across all 7 tracks
     * these are 1:1 with the A_CHECK* overhead gantries (11 triggers, 11
     * gantries, each on the gantry's chunk or the next one) and absent from
     * every circuit, so the trigger is the mechanism and the gantry is only
     * its visible marker.  See research/discoveries.md. */
    /* Stages with checkpoint triggers are point-to-point; the rest are the
     * city circuits ("Stedenrace") and are scored by laps instead -- which is
     * why language.ini carries RONDE INFO / RONDE / LAATSTE RONDE and
     * NIEUW RONDE RECORD alongside the checkpoint wording. */
    int circuit = 0;
    /* Game.ini texturecopy: brake lights, sirens, traffic signs, radar */
    const int texture_copy = a2_ini_int(datadir, "Game.ini", "[OPTIONS]", "texturecopy", 1);
    const int etappe_no = (int)(stem[strlen(stem) - 1] - '0');
    int lap = 1;
    int lap_armed = 0;
    int prev_ch = -1; /* last frame's chunk, for crossing tests */
    /* Game.ini [PLAYER] damage, 0-100.  The original also has code for
     * **eight** figures, one per direction round the car: it resolves each
     * hit to l_zone = l_height_zone + 10 * l_width_zone, each in {-1,0,+1}
     * (so +/-1, +/-9, +/-10, +/-11 are the eight directions), and
     * adds the impact scaled by 0.5 to that zone, with DAMAGE_MESH[zone]
     * naming the damage frame to show, -1 for undamaged.
     * The frame has no threshold ladder: the applier sets
     *     frame = (int)(model_frame_count * DAMAGE_VALUE[zone])
     * so the per-zone value is a 0..1 fraction mapped linearly onto whatever
     * number of damage frames that car model carries.  The two arrays live at
     * 0x4D9E74 (8 floats) and 0x4D9E94 (8 ints) and are round-tripped through
     * Game.ini [PLAYER].
     * None of that runs in the shipped game: handle_mesh_damage (0x427190)
     * returns first for any model without damage frames, and no .DMG ships,
     * so only the scalar 0-100 figure (player_car_collision, 0x427de0) ever
     * changes.  The port keeps that scalar alone.  See "The regional damage
     * code never runs" in research/discoveries.md. */
    int damage = save.damage; /* Game.ini [PLAYER] damage, read at start-up (0x42296a) */
    int brake_lit = -1; /* forces the first stamp */
    float player_progress = 0.0f;
    int progress_chunk = -1; /* set to the start chunk below */
    /* The player's race position.  While racing it is ranked live; once the
     * player has finished it stays what it was on the line, so a racer
     * finishing afterwards does not move them down. */
    int final_pos = 0;
#define PLAYER_POSITION()                                                                         \
    ((finished && final_pos) ? final_pos                                                           \
                             : (final_pos = a2_racers_position(racers, player_progress)))
    float lap_start = 0.0f, last_lap = 0.0f, best_lap = 0.0f;
    /* The banner slot: 0x4882c0 takes a pending language.ini id, the HUD
     * (0x41a95b) latches it into 0x4882c4 with the GetTickCount, and for 2000
     * ms draws it while (ticks / 100) is odd -- 100 ms on, 100 ms off -- as
     * HUD element 76, centred on (160, 40) of the 320x240 layout, in the
     * label font (font 10, olfnt01 / lo_fnt1).  Nine ids ever go through it:
     * 31, 44, 45, 46, 47, 48, 49, 50, 51.  A new raise replaces the old. */
    int banner_id = 0;
    Uint32 banner_ms = 0;
#define RAISE_BANNER(id) (banner_id = (id), banner_ms = SDL_GetTicks())
    int finished = 0;
    int arrested = 0;         /* the police ended it; not the same as finishing */
    /* The game-over sequence (game mode 6, `ev_g_ovr.cpp`) has three arms,
     * picked by the sub-mode 0x4867A4 through the table at 0x4162cc: 0 shows
     * 40 TOTAL LOSS, 1 shows 41 TIJD IS OM, 2 shows 42 GEARRESTEERD.  The
     * arrest already ends the race through `arrested`; this records which of
     * the three it was, as its language.ini id, and `arrested` now means any of
     * them -- the race is over and the car is brought to a stop. */
    int game_over_msg = 0;
    /* Ctrl + Space + I in the original (0x424fbe..0x424ff2): sets the flag
     * 0x4d9eb8 that add_damage (0x428390) tests before adding anything, and
     * raises banner 46 INVINCIBLE. */
    int invincible = 0;
    float turbo_left = 1.0f;  /* the turbo reserve, full at the start */
    float finish_time = 0.0f;
    int result_booked = 0; /* the finish has been booked into `save` */
    int track_ms = 0;
    /* Radar cameras knocked over (0x4dc964, by the type-7 handler 0x428d30;
     * see a2_scene_props_step), and when banner 51 RADAR BONUS was raised. */
    int radar_knocked = 0;
    /* A warning catch pulls the player over: 0x4258f0 puts the car under an
     * AI driver (0x4d9e64, run by 0x426004 -> 0x4076b0) until the 4 s
     * warning sequence (game mode 8, 0x415420) ends and 0x425980 gives
     * control back.  The cop in front holds half the speed it caught us at
     * (0x4028e3); the port eases the car down to that (LIKELY: the player's
     * AI follows the cop ahead). */
    float pulled_until = -1.0f, pulled_speed = 0.0f;
    unsigned prop_rng = 1;

    /* Speed cameras are .omp objects of type 7 (A_FLTSRP / C_FLTSRP, a
     * "flitspaal"); every stage has five or six.  The limit in force comes from
     * the most recent limit sign, whose value is in its name:
     * A_MAX50S / A_MAX80S / A_MX120S -> 50, 80, 120 km/h. */
    /* Per-chunk race markers, read from the .pmp trigger slots.  The values
     * come from check_racer_trigger's switch (fcn.0040B030):
     *   9 = TRIGGER_ZERO_CHUNK_MARKER, 14 = TRIGGER_FINISH_POSITION,
     *   15 = TRIGGER_FINISH_VALID.
     * Circuits carry the zero-chunk marker at chunk 0; point-to-point stages
     * carry FINISH_VALID near the start and FINISH_POSITION at the end. */
    int zero_chunk = -1, finish_chunk = -1, finish_valid_chunk = -1;
    for (uint32_t i = 0; i < track.pmp.chunk_count; i++) {
        uint32_t t[2] = {track.pmp.chunks[i].trigger0, track.pmp.chunks[i].trigger1};
        for (int k = 0; k < 2; k++) {
            if (t[k] == 9 && zero_chunk < 0)
                zero_chunk = (int)i;
            else if (t[k] == 14 && finish_chunk < 0)
                finish_chunk = (int)i;
            else if (t[k] == 15 && finish_valid_chunk < 0)
                finish_valid_chunk = (int)i;
        }
    }
    fprintf(stderr, "race: zero-chunk marker %d, finish %d, finish-valid %d\n", zero_chunk,
            finish_chunk, finish_valid_chunk);

    /* Where the race ends, in the same absolute-chunk units as the racers'
     * progress: the finish chunk for a point-to-point stage, or the start line
     * plus Nrlaps whole laps for a circuit. */
    float finish_progress = 0.0f;

    int camera_chunk[16];
    int camera_count = 0;
    struct { int chunk, limit; } limit_sign[64];
    int limit_count = 0;

    int checkpoint_chunk[32];
    int checkpoint_count = 0;

    /* Trigger 2, the service station (`TRIGGER_BENZINE_STATION`).  Its handler
     * is 0x42a4d0 and it does four things: refill the turbo tank to 100 if the
     * turbo upgrade is owned (0x428060), reset the damage (0x4281c0), play
     * sample 47 `repair.wav` and raise banner 48 QUICK SERVICE.  It is a
     * service stop, not a petrol stop -- it never touches a fuel quantity, and
     * QUICK SERVICE is this banner rather than a cheat label. */
    int service_chunk[16];
    int service_count = 0;
    for (uint32_t i = 0; i < track.pmp.chunk_count && service_count < 16; i++) {
        uint32_t tg[2] = {track.pmp.chunks[i].trigger0, track.pmp.chunks[i].trigger1};
        if (tg[0] == 2 || tg[1] == 2)
            service_chunk[service_count++] = (int)i;
    }
    if (service_count)
        fprintf(stderr, "race: %d service stations\n", service_count);
    /* The cameras fire off **trigger 1**, which is what the game reads: its
     * chunks match the type-7 flitspaal objects one for one on all seven tracks
     * (37 of each), the trigger sitting on the camera's chunk or a chunk or two
     * past it.  The objects are still what gets drawn; this is only where the
     * flash is armed. */
    for (uint32_t i = 0; i < track.pmp.chunk_count && camera_count < 16; i++) {
        uint32_t tg[2] = {track.pmp.chunks[i].trigger0, track.pmp.chunks[i].trigger1};
        if (tg[0] == 1 || tg[1] == 1)
            camera_chunk[camera_count++] = (int)i;
    }
    /* The posted limit comes from the **triggers**, not from the sign objects.
     * The game keeps it in a single global (0x48C480) that triggers 4, 5, 6
     * and 8 overwrite with 50, 70, 100 and 120 km/h, and the camera compares
     * the speed -- `(int)(v * 3.6)`, so km/h -- straight against it.
     *
     * This matters because the painted signs disagree with it: every trigger-5
     * chunk sits on an `A_MAX80S` sign and every trigger-6 chunk on an
     * `A_MX120S`, one for one across etappe1, 3 and 5, so the camera flashes
     * above 70 in a zone signed 80 and above 100 in one signed 120.  Only the
     * 50 zones match their sign.  Reading the limit off the sign name, as this
     * did before, made the port 10-20 km/h more lenient than the original.
     *
     * Triggers 7 and 8 appear in no shipped track, so the 120 limit is never
     * actually posted. */
    for (uint32_t i = 0; i < track.pmp.chunk_count && limit_count < 64; i++) {
        uint32_t tg[2] = {track.pmp.chunks[i].trigger0, track.pmp.chunks[i].trigger1};
        for (int k = 0; k < 2 && limit_count < 64; k++) {
            int v = 0;
            switch (tg[k]) {
            case 4: v = 50; break;
            case 5: v = 70; break;
            case 6: v = 100; break;
            case 8: v = 120; break;
            default: continue;
            }
            limit_sign[limit_count].chunk = (int)i;
            limit_sign[limit_count].limit = v;
            limit_count++;
        }
    }
    for (int i = 1; i < camera_count; i++) {
        int v = camera_chunk[i], j = i - 1;
        while (j >= 0 && camera_chunk[j] > v) { camera_chunk[j + 1] = camera_chunk[j]; j--; }
        camera_chunk[j + 1] = v;
    }
    for (int i = 1; i < limit_count; i++) {
        int c = limit_sign[i].chunk, l = limit_sign[i].limit, j = i - 1;
        while (j >= 0 && limit_sign[j].chunk > c) { limit_sign[j + 1] = limit_sign[j]; j--; }
        limit_sign[j + 1].chunk = c;
        limit_sign[j + 1].limit = l;
    }
    if (camera_count)
        fprintf(stderr, "race: %d speed cameras, %d posted limits\n", camera_count, limit_count);

    /* Checkpoints are trigger slot value 45.  Chunks are walked in order, so
     * the list comes out sorted. */
    for (uint32_t i = 0; i < track.pmp.chunk_count && checkpoint_count < 32; i++) {
        uint32_t t[2] = {track.pmp.chunks[i].trigger0, track.pmp.chunks[i].trigger1};
        if (t[0] == 45 || t[1] == 45)
            checkpoint_chunk[checkpoint_count++] = (int)i;
    }
    if (checkpoint_count)
        fprintf(stderr, "race: %d checkpoints at chunks", checkpoint_count);
    for (int i = 0; i < checkpoint_count; i++)
        fprintf(stderr, " %d", checkpoint_chunk[i]);
    if (checkpoint_count)
        fprintf(stderr, "\n");
    /* Raser.ini [MAPS] arg 0 states this outright (1 = city/stad), so prefer it
     * and fall back to "no checkpoint triggers" only when the ini is missing.
     * The two agree on all 7 shipped tracks. */
    int circuit_by_triggers = (checkpoint_count == 0);
    circuit = ti_idx >= 0 ? ti[ti_idx].circuit : circuit_by_triggers;
    if (ti_idx >= 0 && circuit != circuit_by_triggers)
        fprintf(stderr, "race: WARNING Raser.ini says %s but the triggers say %s\n",
                circuit ? "circuit" : "point-to-point",
                circuit_by_triggers ? "circuit" : "point-to-point");
    if (circuit)
        fprintf(stderr, "race: circuit stage, scored by laps\n");

    a2_cars cars;
    int have_cars = a2_cars_load(&cars, datadir) == 0;
    /* Game.ini [PLAYER] car, the player's car type 0..6 ([0x4d9e2c], read at
     * 0x42290c; "Illegal car!" outside that), and copland ([0x4d9e0c],
     * 0x422944), which menu.dat sets when the player is named COPLAND.  With
     * copland the player init (0x4256a0) makes the type 6 and writes
     * MPOLIZEI_ANIM's anim id (0x85) into type 6's record of the 0x484B18
     * table: the Lambdaghini's car with the police body.  Its wheels are
     * type 0's (0x42571f) and the start camera's end (0x416ccd) raises
     * banner 47 COP MODE. */
    /* the police light flasher, 0x4a5d3c / 0x4a5d40 / 0x4a5d30 */
    int pol_light_frame = 0, pol_light_was_on = 0;
    Uint32 pol_light_at = 0;
    int copland = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "copland", 0) != 0;
    int car_slot = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "car", 0);
    if (car_slot < 0 || car_slot > 6)
        car_slot = 0;
    if (copland)
        car_slot = 6;
    int wheel_type = copland ? 0 : car_slot;
    int car_lane = 1;   /* middle of the three lanes */
    int overtaking = 0; /* holding a lane the car is not yet in */
    /* Stuck recovery for the autopilot.  The lane-follower has no model of a
     * static object, so once it noses into a post it holds full throttle
     * against it for ever (etappe5 chunk 97).  Count the time spent barely
     * moving and shunt the target lane sideways when it runs on, which is
     * enough to scrape the car past.  This is the port's own crutch, not
     * anything the original does. */
    float auto_stuck = 0.0f;
    int auto_stuck_dir = 1;
    int auto_stuck_tries = 0;
    int auto_stuck_at = -1;   /* where the last reposition happened */
    int auto_stuck_runs = 0;  /* repeats at that same place */
    float auto_anchor_x = 0.0f, auto_anchor_z = 0.0f, auto_anchor_t = 0.0f;
    float auto_prev_x = 0.0f, auto_prev_z = 0.0f;
    /* Upgrades, from Game.ini [PLAYER], as the shipped driving model uses them
     * (see drive.c and "Two driving models" in research/discoveries.md):
     *   MOTOR            +4 m/s on every gear's top speed (0x40e9f0), and the
     *                    toeren2 engine note
     *   BANDEN           doubles the lateral grip (0x40f5bd, 0x40f9ec)
     *   TURBO            fills the turbo tank
     *   ABS              brakes 0.5 harder (0x40f2a1) -- it prevents nothing
     *   VERSNELLINGSBAK  the upgraded CARS.DAT arrays and their 6th gear
     *   INJECTIE         nothing at all */
    int up_motor = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "MOTOR", 0);
    int up_banden = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "BANDEN", 0);
    int up_abs = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "ABS", 0);
    int up_gearbox = a2_ini_int(datadir, "Game.ini", "[PLAYER]", "VERSNELLINGSBAK", 0);
    /* The 0x484B18 physics table.  It drives only the old Autobahn Raser I
     * model (stuur_model = 1), which the shipped game never runs; the port
     * still reads it for two things the shipped game does use from it: the
     * rear axle's distance (field 6, where tyre smoke and skidmarks are laid)
     * and each vehicle's engine sample (field 55). */
    a2_handling_table handling;
    memset(&handling, 0, sizeof handling);
    const a2_handling *car_hand = NULL;
    if (have_cars && a2_handling_load(&handling, datadir) == 0) {
        int hi = a2_handling_find(&handling, a2_car_anim_name(car_slot));
        if (hi >= 0 && handling.v[hi].valid)
            car_hand = &handling.v[hi];
    }
    if (have_cars)
        fprintf(stderr, "car upgrades: motor %d, banden %d, abs %d, gearbox %d\n", up_motor,
                up_banden, up_abs, up_gearbox);

    /* The checkpoint clock.  On a point-to-point stage the original runs a
     * countdown in game timer 11: the HUD's highway panel (0x41a5f0, chosen
     * over the circuit one at 0x41a94d by Raser.ini's circuit flag) draws it
     * under the literal label CHECKPOINT as mm:ss:t; the checkpoint handler
     * (trigger 45, 0x42a530) adds a fixed bonus to it, plays 83 checkp.wav and
     * raises 44 EXTRA TIJD; and when it runs out the race ends in game-over
     * sub-mode 1, 41 TIJD IS OM (0x424b80..0x424b9a).  The bonus is computed
     * once, at 0x424344:
     *     bonus_s = (int)( 10 * dist[etappe] / (top * k) )
     * with dist a per-stage table built on the stack at 0x4242e7 -- 142, 110
     * and 142 for etappe 1, 3 and 5, 0 for the circuits -- `top` the car's
     * CARS.DAT top-gear ceiling in m/s plus 4 if the motor is owned (0x410b00),
     * and k 0.4 on EASY, 0.65 on HARD ([OPTIONS] difficulty; 0x47f76c,
     * 0x47f770).  It reads as a distance over a speed: the time to cover
     * 10 * dist metres at 40% or 65% of the car's top speed.  The clock starts
     * at one bonus (0x42439a sets timer 11 to it). */
    float cp_bonus = 0.0f, time_left = 0.0f;
    int timed = 0;
    if (have_cars && !no_clock && !circuit && ti_idx >= 0 && ti_idx < 7) {
        static const int cp_dist[7] = { 0, 142, 0, 110, 0, 142, 0 };
        float top = a2_car_top_speed(&cars.cars[car_slot], up_gearbox ? 1 : 0);
        if (up_motor)
            top += 4.0f;
        float k = difficulty == 0 ? 0.4f : 0.65f;
        if (cp_dist[ti_idx] > 0 && top > 1.0f) {
            cp_bonus = (float)(int)((float)(cp_dist[ti_idx] * 10) / (top * k));
            time_left = cp_bonus;
            timed = 1;
            fprintf(stderr, "race: checkpoint clock, %.0f s per checkpoint (%s)\n", cp_bonus,
                    difficulty == 0 ? "EASY" : "HARD");
        }
    }

    /* The car's own size, from its mesh -- CARS.DAT has performance but no
     * dimensions.  A Diablo is 18.8 units wide against the Trabant's 16.4. */
    float car_half_width = a2_car_half_width(car_slot);
    float car_half_len = a2_car_half_length(car_slot);
    if (copland) /* the collision box is the mesh's (0x449a30) */
        a2_cop_half_size(&car_half_width, &car_half_len);
    int car_model = a2_scene_load_model(scene, &rcs, anims,
                                        copland ? "MPOLIZEI_ANIM" : a2_car_anim_name(car_slot));
    /* car_whl.cpp: the racing meshes have no wheels; four WIEL_ANIM hang on the
     * car (0x4136c0, called for the player at 0x42572b) */
    int wheel_model = a2_scene_load_model(scene, &rcs, anims, "WIEL_ANIM");
    if (wheel_model < 0)
        fprintf(stderr, "wheels: WIEL_ANIM could not be loaded\n");
    a2_wheels wheels;
    a2_wheels_init(&wheels);
    /* Game.ini object_lights (shipped 1) with the port's 640x480-class
     * output: the brake lights are light objects, not a texture stamp */
    int brake_light_model = -1;
    if (a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "object_lights", 1)) {
        brake_light_model = a2_scene_load_mesh_named(scene, &rcs, "s_licht.d3d", "s_licht_glow");
        if (brake_light_model >= 0)
            a2_scene_set_glow_texture(scene, &rcs, brake_light_model, "s_sfx.tga");
    }
    /* the car mesh's radius, for the props' contact test (0x446930) */
    float car_radius = car_half_width;
    {
        float lo[3], hi[3];
        if (car_model >= 0 && a2_scene_model_bounds(scene, car_model, lo, hi)) {
            float hx = (hi[0] - lo[0]) * 0.5f, hz = (hi[2] - lo[2]) * 0.5f;
            car_radius = sqrtf(hx * hx + hz * hz);
        }
    }
    /* Car shadows, which the original draws only with Game.ini's alpha_blending
     * on ("car shadows, clouds and lights"; 0x41d79e tests the flag). */
    if (a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "alpha_blending", 1))
        a2_scene_load_shadow(scene, &rcs, anims);
    /* The shipped AI (ai_model 0) drives the racers and the chasing cops on
     * the .dmp waypoint graph; see ai_world.h. */
    a2_aiw *aiw = a2_aiw_create(&track);
    int demo_mode = a2_ini_int(datadir, "Game.ini", "[OPTIONS]", "demo", 0);
    /* Opponents.  Game.ini's [RACERS] section ships with count=3. */
    a2_racers *racers = NULL;
    if (have_cars && !no_racers && aiw) {
        a2_racers_config rc = {0};
        rc.count = a2_ini_int(datadir, "Game.ini", "[RACERS]", "count", 3);
        rc.player_slot = car_slot;
        rc.player_car = &cars.cars[car_slot];
        rc.gearbox = up_gearbox;
        rc.tyres = up_banden;
        rc.difficulty = difficulty;
        rc.etappe = ti_idx >= 0 ? ti_idx : 0;
        rc.circuit = circuit;
        rc.laps = nr_laps;
        rc.demo = demo_mode;
        rc.start_chunk = start_chunk;
        rc.player_position = save.position;
        racers = a2_racers_create(scene, &rcs, anims, &cars, aiw, &rc);
    }
    if (have_cars)
        fprintf(stderr, "car: slot %d %s, top speed %.0f km/h (%.0f upgraded)\n", car_slot,
                up_gearbox ? "upgraded gearbox" : "stock gearbox",
                a2_car_top_speed(&cars.cars[car_slot], 0) * 3.6f,
                a2_car_top_speed(&cars.cars[car_slot], 1) * 3.6f);

    a2_traffic *traffic =
        no_traffic ? NULL : a2_traffic_create(scene, &rcs, anims, 24, start_chunk);
    a2_traffic_set_limits(traffic, &track);
    /* The police.  The speed limit they enforce is the per-chunk .pmp trigger,
     * not the limit signs standing by the road -- see cops.h. */
    a2_cops *cops = NULL;
    if (!no_cops && have_cars && aiw) {
        a2_cops_config cc = {0};
        cc.player_car = &cars.cars[car_slot];
        cc.gearbox = up_gearbox;
        cc.tyres = up_banden;
        cc.difficulty = difficulty;
        cc.circuit = circuit;
        cc.demo = demo_mode;
        cops = a2_cops_create(scene, &rcs, anims, &track, aiw, &cc);
    }
    /* The scripted path objects: boats, trains, helicopter, aeroplane and
     * scripted traffic (.pth). */
    a2_paths *paths = a2_paths_create(scene, &rcs, anims, &track);
    int scene_chunks = (int)track.rmp.chunk_count;
    finish_progress = circuit ? (float)(start_chunk + nr_laps * scene_chunks)
                              : (float)finish_chunk;

    vehicle car;
    memset(&car, 0, sizeof car);
    car.abs = up_abs ? 1 : 0;
    float start[3];
    float start_yaw;
    int lanes = a2_scene_lane_count(scene, player_chunk0);
    /* The player is made by the vehicle constructor 0x410bc0 with lane 4
     * (pushed at 0x4256f0), which 0x442150 clamps to the chunk's last lane:
     * on the shipped three-lane starts, the outermost lane on the right. */
    if (lanes > 0)
        car_lane = lanes - 1 < 4 ? lanes - 1 : 4;
    if (a2_scene_start_pose_on_lane(scene, player_chunk0, car_lane, start, &start_yaw)) {
        car.x = start[0];
        car.y = start[1];
        car.z = start[2];
        car.yaw = start_yaw;
    } else {
        car.x = cam.pos[0];
        car.y = cam.pos[1] - 60.0f;
        car.z = cam.pos[2];
        car.yaw = cam.yaw;
    }
    int drive_mode = (have_cars && car_model >= 0);
    /* The driving model: the original's shipped "new (A2 Racer II)" model,
     * driven by CARS.DAT (see drive.h).  `car` stays as its mirror for the
     * collision, camera, HUD and race code. */
    a2_drive drv;
    a2_drive_init(&drv, car.x, car.y, car.z, (car.yaw - 180.0f) * (4096.0f / 360.0f));
    a2_drive_env denv;
    memset(&denv, 0, sizeof denv);
    denv.car = have_cars ? &cars.cars[car_slot] : NULL;
    denv.motor = up_motor;
    denv.gearbox = up_gearbox;
    denv.tyres = up_banden;
    denv.abs = up_abs;
    drive_ground dground = {scene, a2_scene_nearest_chunk(scene, car.x, car.z)};
    /* Tyre smoke: Game.ini's "clouds", which the original only draws with
     * alpha_blending on (init_clouds, 0x4201a0). */
    a2_smoke *smoke = NULL;
    if (a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "clouds", 1) &&
        a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "alpha_blending", 1))
        smoke = a2_smoke_create();
    float smoke_last = -1.0f;
    /* Skidmarks: Game.ini skidmarks, fading with fade_skmarks. */
    a2_skid *skid = a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "skidmarks", 1)
                        ? a2_skid_create(a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]",
                                                    "fade_skmarks", 1),
                                         a2_scene_texture(scene, &rcs, "s_sfx.pcx"))
                        : NULL;
    /* the rear axle's distance behind the centre of gravity, physics field 6,
     * which places the puffs at the rear wheels */
    float rear_axle = car_hand && car_hand->cog_to_rear > 0.0f ? car_hand->cog_to_rear : 1.2f;
    denv.height = drive_height;
    denv.height_ud = &dground;
    int was_touching = 0;
    /* Sound.  The headless test runs stay silent; otherwise the levels follow
     * Raser.ini [SOUND] (ON, SFXVOLUME) and Game.ini [OPTIONS] 3dsound. */
    a2_audio *audio = NULL;
    if (!no_sound && !drive_test && a2_ini_int(datadir, "Raser.ini", "[SOUND]", "ON", 1))
        audio = a2_audio_open(&rcs, a2_ini_int(datadir, "Raser.ini", "[SOUND]", "SFXVOLUME", 100),
                              a2_ini_int(datadir, "Game.ini", "[OPTIONS]", "3dsound", 1));
    int snd_motor = -1, snd_toeren = -1, snd_scrape = -1, snd_horn = -1, snd_voice = -1;
    int snd_slip = -1;
    int snd_rattle[2] = {-1, -1};
    int snd_turbo_was = 0, snd_tick_sec = -1, snd_caught = 0;
    float snd_last_col = -10.0f;
    /* The player's engine (init_player_car, 0x425821): sample 23 motor.wav and
     * 24 toeren1.wav, or 85 toeren2.wav with the motor upgrade. */
    if (audio) {
        snd_motor = a2_audio_play(audio, 23, 1);
        snd_toeren = a2_audio_play(audio, up_motor ? 85 : 24, 1);
    }
    /* Which collider last held the car, for the autopilot's give-up message. */
    int last_rail_hit = 0, last_obj_hit = 0;
    int was_hitting_car = 0;
    float race_time = 0.0f;
    int next_camera = 0;
    float flash_at = -100.0f;
    /* Progress starts where the racers' does: the absolute start chunk. */
    player_progress = (float)player_chunk0;
    progress_chunk = player_chunk0;
    int next_checkpoint = 0;
    while (next_checkpoint < checkpoint_count && checkpoint_chunk[next_checkpoint] < player_chunk0)
        next_checkpoint++;
    while (next_camera < camera_count && camera_chunk[next_camera] < player_chunk0)
        next_camera++;
    float split_time = 0.0f;
    int cop_banner_done = 0; /* COP MODE, raised once as the race starts */
    int cop_msg_seen = 0;
    float cop_msg_age = 0.0f;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CW); /* Direct3D's front faces, now that the view is not mirrored */
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glClearColor(0.42f, 0.55f, 0.72f, 1.0f);

    /* Game.ini [OPTIONS] visiblechunks -- "Sets the number of visible chunks
     * (Def.=30)".  Read rather than hardcoded, so the port's draw distance
     * tracks the same knob the original uses. */
    int radius = a2_ini_int(datadir, "Game.ini", "[OPTIONS]", "visiblechunks", 30);
    if (radius < 1)
        radius = 30;
    int show_objects = !no_objects;
    int show_sky = 1;
    /* Active camera view, indexed as the original does: keys 1-4 select views
     * 1, 3, 5 and 0 (see the switch in the drive block).  View 1 is the default
     * because it is the nearer of the two chase cameras. */
    int cam_view = 1;
    int cam_view_prev = 1;
    int cam_set = -1;              /* a preset to switch to this frame */
    int cam_target = 3;            /* [0x4d9b2c]: 0-2 a racer, 3 the player */
    a2_tweak tweak;
    a2_tweak_init(&tweak);
    float pre_x = 0.0f, pre_z = 0.0f, pre_h = 0.0f, step_dx = 0.0f, step_dz = 0.0f;
    toi_hit toi = {TOI_NONE, -1, {0.0f, 0.0f}, {0.0f, 0.0f}};
    /* impulses the steps applied this frame, per TOI kind: the largest
     * magnitude, and the obstacle (-2 none) */
    float step_j[6];
    int step_idx[6];
    int cam_smooth_on = 1;         /* camera +0x6c */
    float cam_smooth = 0.245f;     /* camera +0x84 */
    float cam_eye[3], cam_look[3]; /* the smoothed position and look point */
    int cam_fresh = 1;             /* no previous position yet */
    const int tweak_mode = a2_ini_int(datadir, "Raser.ini", "[HELPDESK]", "TWEAK_MODE", 0);
    int show_rails = 0;
    int show_traffic = 1;
    /* Game.ini [GFX OPTIONS] overlay_detail -- "Overlay detail level. This can
     * be adjusted by the player when pressing the 'detail' key. Default=3 (full
     * detail: radar, all overlay graphics. (min=0, max=3)".  The original
     * cycles it on **D** (fcn.00424EE0: ++detail, wrapping past 3 to 0), and
     * gates each element on it -- the checkpoint text, for one, draws only
     * above level 2 (fcn.0041A5F0).  `disable_overlay` locks it off entirely. */
    int overlay_detail = a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "overlay_detail", 3);
    if (overlay_detail < 0 || overlay_detail > 3)
        overlay_detail = 3;
    if (a2_ini_int(datadir, "Game.ini", "[GFX OPTIONS]", "disable_overlay", 0))
        overlay_detail = 0;
    int show_hud = 1;
    int running = 1, mouse_captured = 0, frames = 0, over_frames = 0;
    Uint32 last = SDL_GetTicks();

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_RESIZED) {
                win_w = ev.window.data1;
                win_h = ev.window.data2;
            } else if (ev.type == SDL_MOUSEBUTTONDOWN && !mouse_captured) {
                mouse_captured = 1;
                SDL_SetRelativeMouseMode(SDL_TRUE);
            } else if (ev.type == SDL_MOUSEMOTION && mouse_captured) {
                cam.yaw += ev.motion.xrel * 0.15f;
                cam.pitch -= ev.motion.yrel * 0.15f;
                if (cam.pitch > 89.0f)
                    cam.pitch = 89.0f;
                if (cam.pitch < -89.0f)
                    cam.pitch = -89.0f;
            } else if (ev.type == SDL_KEYDOWN) {
                /* the tweak menu (tweak.h): Tab opens a screen, which then
                 * takes the keys while the race stands still */
                if (tweak.open) {
                    if (a2_tweak_key(&tweak, ev.key.keysym.sym, &cars, car_slot, up_gearbox,
                                     datadir))
                        continue;
                } else if (ev.key.keysym.sym == SDLK_TAB && tweak_mode && drive_mode && have_cars) {
                    a2_tweak_open(&tweak, &cars, datadir);
                    continue;
                }
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (mouse_captured) {
                        mouse_captured = 0;
                        SDL_SetRelativeMouseMode(SDL_FALSE);
                    } else
                        running = 0;
                    break;
                case SDLK_TAB:
                    if (have_cars && car_model >= 0)
                        drive_mode = !drive_mode;
                    break;
                /* Camera views, the original's bindings (handle_game_cameras
                 * 0x4144b0): 1 -> preset 1, 2 -> 3, 4 -> 0, and with Raser.ini
                 * TWEAK_MODE ([0x4d9b24], tested at 0x41455f) 5 -> 4, 7 -> 6,
                 * 8 -> 7, 9 -> 8, 0 -> 9; key 6 is not polled.  Key 3 is
                 * polled each frame below: held, it shows preset 5. */
                case SDLK_1: cam_set = 1; break;
                case SDLK_2: cam_set = 3; break;
                case SDLK_4: cam_set = 0; break;
                case SDLK_5: if (tweak_mode) cam_set = 4; break;
                case SDLK_7: if (tweak_mode) cam_set = 6; break;
                case SDLK_8: if (tweak_mode) cam_set = 7; break;
                case SDLK_9: if (tweak_mode) cam_set = 8; break;
                case SDLK_0: if (tweak_mode) cam_set = 9; break;
                /* M, tweak mode (0x424ef8): the camera follows the next car,
                 * racer 0, 1, 2, then the player ([0x4d9b2c] 3), 0x4149b0 */
                case SDLK_m:
                    if (tweak_mode) {
                        cam_target = cam_target + 1 > 3 ? 0 : cam_target + 1;
                        cam_fresh = 1;
                    }
                    break;
                case SDLK_o:
                    show_objects = !show_objects;
                    break;
                case SDLK_g:
                    show_rails = !show_rails;
                    break;
                case SDLK_p:
                    autopilot = !autopilot;
                    break;
                case SDLK_t:
                    show_traffic = !show_traffic;
                    break;
                /* 'D' cycles the overlay detail, as the original does. */
                case SDLK_d:
                    if (!drive_mode)
                        break; /* fly mode uses D to strafe */
                    overlay_detail++;
                    if (overlay_detail > 3)
                        overlay_detail = 0;
                    fprintf(stderr, "overlay detail %d\n", overlay_detail);
                    break;
                case SDLK_h:
                    show_hud = !show_hud;
                    break;
                case SDLK_LEFTBRACKET:
                    if (radius > 1)
                        radius--;
                    break;
                case SDLK_RIGHTBRACKET:
                    radius++;
                    break;
                case SDLK_F2:
                    write_ppm("a2view.ppm", win_w, win_h);
                    break;
                default:
                    break;
                }
            }
        }

        Uint32 now = SDL_GetTicks();
        float dt = (now - last) / 1000.0f;
        last = now;
        if (dt > 0.1f)
            dt = 0.1f;
        if (tweak.open)
            dt = 0.0f; /* the menu is modal in the original: the race stands still */
        /* Fixed step for headless runs, so they are reproducible.  It has to be
         * set here, before anything reads `dt`: when it was applied further
         * down, the autopilot's own stuck timer had already accumulated
         * wall-clock time, so the unwedge fired at a different moment on every
         * run and two identical invocations of etappe5 finished 20 s apart. */
        if (drive_test || autopilot)
            dt = 1.0f / 30.0f;

        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if (drive_mode) {
            /* The keys Game.ini [INPUT] binds, as VK codes (defaults LEFT 37,
             * RIGHT 39, UP 38 gas, DOWN 40 brake, EXTRA1 17 Ctrl turbo, EXTRA2
             * 18 Alt handbrake); the menu's controls screen rebinds them.
             * W/A/S/D used to steer here too, but D is the original's
             * overlay-detail key, so drive mode leaves the letters alone
             * unless they are bound; fly mode still uses W/A/S/D. */
            a2_input in = read_input(device, joy, k, key_left, key_right, key_up, key_down,
                                     key_turbo, key_handbrake);
            float throttle = (in.keys & A2_KEY_UP) ? 1.0f : 0.0f;
            float brake = (in.keys & A2_KEY_DOWN) ? 1.0f : 0.0f;
            if (in.analog) {
                throttle = in.throttle / 100.0f;
                brake = in.brake / 100.0f;
            }
            /* Turbo, on EXTRA1.  Watching the original drive under Wine with the
             * key held: the TURBO bar starts **full**, drains steadily while
             * held, and does **not** refill during the race -- it was still at
             * the level it had reached six seconds after release.  The boost is
             * large: 153 km/h rose to 202 over about four seconds, well past
             * the car's rated top speed.
             * Both constants below are the port's own reading of that: the
             * drain is set so a full bar lasts about twelve seconds, which is
             * mid-range across two runs that measured between eight and
             * eighteen, and the force multiplier is what v-squared drag needs
             * to lift 153 km/h to 202 (1.32 cubed in speed is 1.75 in force). */
            int turbo_held = in.turbo;
            int handbrake = in.handbrake;
            float steer = 0.0f;
            if (in.keys & A2_KEY_LEFT)
                steer -= 1.0f;
            if (in.keys & A2_KEY_RIGHT)
                steer += 1.0f;
            if (drive_test || autopilot) {
                int ch0 = a2_scene_nearest_chunk(scene, car.x, car.z);
                /* Lane choice.  Normally sit in whatever lane the car is
                 * actually in; when traffic blocks it, hold a neighbouring
                 * lane until the car has moved across.  Without this the
                 * lane-follower just rear-ends the first slow car and grinds
                 * to a halt behind it (etappe1 chunk 198). */
                int here = a2_scene_nearest_lane(scene, ch0, car.x, car.z);
                if (here >= 0 && (!overtaking || here == car_lane))
                    car_lane = here;
                if (overtaking && here == car_lane)
                    overtaking = 0;
                if (traffic && here >= 0) {
                    int nlanes = a2_scene_lane_count(scene, ch0);
                    if (!overtaking && lane_blocked(scene, traffic, ch0, car_lane)) {
                        /* Prefer the faster (lower-index) side, then the other. */
                        int cand[2] = {car_lane - 1, car_lane + 1};
                        for (int ci = 0; ci < 2; ci++) {
                            int L = cand[ci];
                            if (L < 0 || L >= nlanes)
                                continue;
                            if (lane_blocked(scene, traffic, ch0, L))
                                continue;
                            car_lane = L;
                            overtaking = 1;
                            break;
                        }
                    }
                }
                steer = lane_follow_steer(scene, &car, ch0, car_lane, 3);
                /* Look further ahead than we steer by, and ease off when that
                 * point is well off the nose -- otherwise a Trabant at 150 km/h
                 * simply runs wide on etappe0's city corners. */
                float ahead = lane_follow_steer(scene, &car, ch0, car_lane, 8);
                float bend = ahead < 0.0f ? -ahead : ahead;
                throttle = 1.0f - 0.9f * bend;
                if (throttle < 0.1f)
                    throttle = 0.1f;
                /* Proportional, not on/off.  Until the brake term went in this
                 * was a full stamp on the pedal, which cost nothing because
                 * braking was a no-op; against real brakes (7.4 m/s^2 for a
                 * Trabant, 22 for a Diablo) a full stamp in every bend drops
                 * the car to a crawl and it spends the stage being overtaken
                 * into. Easing it in across bend 0.75..1.0 keeps the corner
                 * speed sane. */
                if (bend > 0.75f && car.speed > 8.0f) {
                    brake = (bend - 0.75f) * 4.0f;
                    if (brake > 1.0f)
                        brake = 1.0f;
                } else {
                    brake = 0.0f;
                }
                /* Below walking pace, always pull away: otherwise a car that
                 * stops while pointing off-line brakes because it is
                 * misaligned and can never turn back, which deadlocks it. */
                if (car.speed < 5.0f) {
                    throttle = 1.0f;
                    brake = 0.0f;
                }
                /* metres actually covered this frame (10 units a metre) */
                float auto_moved = hypotf(car.x - auto_prev_x, car.z - auto_prev_z) * 0.1f;
                auto_prev_x = car.x;
                auto_prev_z = car.z;
                /* Stuck against something: after a second and a half of barely
                 * moving, step the target lane sideways (reversing the search
                 * direction each time) until the car is clear.
                 * Not while the race is over, though: an arrest deliberately
                 * brings the car to a stop, and without this gate the recovery
                 * read that as a wedge and started teleporting a car that is
                 * supposed to be sitting still. */
                if (arrested || finished) {
                    auto_stuck = 0.0f;
                    auto_stuck_tries = 0;
                } else if (car.speed < 2.0f || auto_moved < 2.0f * dt) {
                    /* also when the model reports speed but the collision
                     * pushes the car back every frame (etappe2 chunk 241,
                     * wedged on a roadworks arrow at 11 km/h) */
                    auto_stuck += dt;
                } else {
                    auto_stuck = 0.0f;
                }
                /* and when it rocks to and fro against something: under 5 m
                 * of net progress in 3 s (etappe2 chunk 142, jiggling 4 m a
                 * frame against a corner barrier at 30 km/h) */
                if (hypotf(car.x - auto_anchor_x, car.z - auto_anchor_z) * 0.1f > 5.0f) {
                    auto_anchor_x = car.x;
                    auto_anchor_z = car.z;
                    auto_anchor_t = 0.0f;
                } else if (!arrested && !finished) {
                    auto_anchor_t += dt;
                    if (auto_anchor_t > 3.0f) {
                        auto_stuck = 2.0f;
                        auto_anchor_t = 0.0f;
                    }
                }
                if (auto_stuck > 1.5f) {
                    int nl = a2_scene_lane_count(scene, ch0);
                    int want = car_lane + auto_stuck_dir;
                    if (nl > 1 && (want < 0 || want >= nl)) {
                        auto_stuck_dir = -auto_stuck_dir;
                        want = car_lane + auto_stuck_dir;
                    }
                    if (nl > 1 && want >= 0 && want < nl) {
                        car_lane = want;
                        overtaking = 1;
                    }
                    auto_stuck = 0.0f;
                    auto_stuck_tries++;
                } else if (car.speed > 10.0f) {
                    auto_stuck_tries = 0;
                }
                /* A lane shunt cannot help where the obstacle spans the lane or
                 * the section has only one.  After three failed attempts, put
                 * the car back on the lane centre a few chunks behind and let
                 * it have another run at it.  This is a crutch for the headless
                 * test runs only -- it keeps a wedged autopilot from turning a
                 * whole-stage regression into a no-result -- and nothing like it
                 * exists in the original. */
                if (auto_stuck_tries >= 3) {
                    /* Escalate: the first reposition often drops the car right
                     * back into the same obstacle, so each repeat at the same
                     * place backs off further and takes the next lane.  Without
                     * the escalation etappe5 chunk 242 unwedged 158 times
                     * without ever getting past. */
                    /* "the same place" is within 5 chunks: a hard corner
                     * wedges the car on a slightly different chunk each time */
                    if (auto_stuck_at >= 0 && ch0 - auto_stuck_at <= 5 && auto_stuck_at - ch0 <= 5) {
                        auto_stuck_runs++;
                    } else {
                        auto_stuck_at = ch0;
                        auto_stuck_runs = 1;
                    }
                    if (auto_stuck_runs > 8) {
                        fprintf(stderr,
                                "autopilot: stuck at chunk %d after %d attempts, giving up "
                                "(rail=%d object=%d traffic=%d pos=(%.0f, %.0f))\n",
                                ch0, auto_stuck_runs, last_rail_hit, last_obj_hit,
                                was_hitting_car, car.x, car.z);
                        break;
                    }
                    /* the first attempt backs off; a repeat at the same
                     * place goes past it instead -- a corner the
                     * lane-follower keeps hitting (etappe2 chunk 112) */
                    int back = auto_stuck_runs > 1 ? ch0 + 3 * auto_stuck_runs : ch0 - 3;
                    /* Keep the lane on the first attempt -- a plain setback in
                     * the same lane is what clears most wedges, and rotating
                     * straight away can drop the car somewhere worse.  Only
                     * start trying other lanes once the simple version has
                     * failed. */
                    if (auto_stuck_runs > 1) {
                        int nl = a2_scene_lane_count(scene, ch0);
                        if (nl > 0)
                            car_lane = (car_lane + 1) % nl;
                    }
                    float lp[3];
                    float lq[3];
                    if (back > 0 && a2_scene_lane_point(scene, back, car_lane, lp)) {
                        car.x = lp[0];
                        car.z = lp[2];
                        car.speed = 0.0f;
                        /* face down the lane, with no slide or spin left */
                        if (a2_scene_lane_point(scene, back + 1, car_lane, lq)) {
                            float ly = atan2f(lq[0] - lp[0], -(lq[2] - lp[2])) * 180.0f / (float)M_PI;
                            drv.heading = (ly - 180.0f) * (4096.0f / 360.0f);
                            car.yaw = ly;
                        }
                        drv.v_lat = drv.v_fwd = drv.yaw_rate = 0.0f;
                        fprintf(stderr, "autopilot: unwedged at chunk %d (attempt %d, to %d)\n",
                                ch0, auto_stuck_runs, back);
                    }
                    auto_stuck_tries = 0;
                    auto_stuck = 0.0f;
                }
            }
            const float TURBO_SECONDS = 12.0f;
            int turbo_on = turbo_held && turbo_left > 0.0f && throttle > 0.0f;
            if (turbo_on) {
                turbo_left -= dt / TURBO_SECONDS;
                if (turbo_left < 0.0f)
                    turbo_left = 0.0f;
            }
            /* An arrest ends the race.  The original switches to game mode 6,
             * `ev_g_ovr.cpp`, with the sub-mode 0x4867A4 set to 2 -- the arrest
             * arm of the game-over sequence, whose callbacks are the only two
             * fetches of 42 GEARRESTEERD and 43 RACE OVER.  So the player stops
             * driving; the race clock keeps running only so the opponents'
             * finishing times still come out right. */
            /* The two cheats, both Ctrl + Space + a letter, polled as raw key
             * state with no edge latch in the code: T adds 180 000 ms to timer
             * 11 and raises 44 EXTRA TIJD (0x424fa5), on point-to-point stages
             * only; I makes the car invincible (above), on any stage.  Held, T
             * keeps adding time every frame -- read that way from the code,
             * not tried in the running game. */
            /* 47 COP MODE as the start camera hands over (0x416ccd) */
            if (copland && !cop_banner_done) {
                cop_banner_done = 1;
                RAISE_BANNER(47);
            }
            /* 49 / 50, the police warnings, at the catch (0x41535e / 0x415352) */
            if (cops) {
                float age = 0.0f;
                int msg = a2_cops_message(cops, &age);
                if ((msg == 49 || msg == 50) && (msg != cop_msg_seen || age < cop_msg_age))
                    RAISE_BANNER(msg);
                cop_msg_seen = msg;
                cop_msg_age = age;
            }
            if ((k[SDL_SCANCODE_LCTRL] || k[SDL_SCANCODE_RCTRL]) && k[SDL_SCANCODE_SPACE]) {
                if (k[SDL_SCANCODE_T] && timed && !circuit) {
                    time_left += 180.0f;
                    RAISE_BANNER(44);
                }
                if (k[SDL_SCANCODE_I] && !invincible) {
                    invincible = 1;
                    RAISE_BANNER(46);
                    fprintf(stderr, "INVINCIBLE\n");
                }
            }
            if (timed && !arrested && !finished) {
                time_left -= dt;
                if (time_left <= 0.0f) {
                    time_left = 0.0f;
                    arrested = 1;
                    game_over_msg = 41;
                    fprintf(stderr, "out of time at %.1f s -- TIJD IS OM\n", race_time);
                }
            }
            if (arrested) {
                throttle = 0.0f;
                brake = 1.0f;
                handbrake = 0;
                steer = 0.0f;
                turbo_on = 0;
            }
            int pulled = !arrested && race_time < pulled_until;
            if (pulled) {
                int pc = a2_scene_nearest_chunk(scene, car.x, car.z);
                int pl = a2_scene_nearest_lane(scene, pc, car.x, car.z);
                steer = lane_follow_steer(scene, &car, pc, pl < 0 ? 0 : pl, 3);
                throttle = car.speed < pulled_speed - 1.0f ? 0.5f : 0.0f;
                brake = car.speed > pulled_speed + 1.0f ? 0.5f : 0.0f;
                handbrake = 0;
                turbo_on = 0;
            }
            /* Collisions push the car out and scrub its speed in the mirror;
             * carry that into the model before stepping it. */
            drv.x = car.x;
            drv.z = car.z;
            drv.v_fwd = car.speed;
            if (gb_gas >= 0.0f && !arrested && !pulled) {
                /* gas, then brake, at full travel; the autopilot steers so the
                 * car stays on the road */
                drv.throttle = race_time < gb_gas ? 100.0f : 0.0f;
                drv.brake = race_time >= gb_gas && race_time < gb_gas + gb_brake ? 100 : 0;
                drv.steer = -steer * 100.0f;
            } else if (drive_test || autopilot || arrested || pulled) {
                /* the autopilot drives the model's inputs directly */
                drv.throttle = throttle * 100.0f;
                drv.brake = (int)(brake * 100.0f);
                /* The autopilot's positive steer turns toward this port's
                 * camera-right vector, which is the model's negative lateral
                 * axis (see set_projection), hence the sign. */
                drv.steer = -steer * 100.0f;
            } else if (in.analog) {
                /* 0x426e00, stuur_model 0: the wheel writes the model's
                 * inputs directly, no ramps */
                drv.throttle = in.throttle;
                drv.brake = in.brake;
                drv.steer = in.steer;
            } else {
                a2_drive_keys(&drv, in.keys, dt);
            }
            /* Debug: A2_HANDBRAKE_AT=<frame> holds the handbrake for a second
             * from that frame, to provoke a slide in a headless run. */
            if (getenv("A2_HANDBRAKE_AT")) {
                int at = atoi(getenv("A2_HANDBRAKE_AT"));
                if (frames >= at && frames < at + 30)
                    handbrake = 1;
            }
            drv.handbrake = handbrake;
            dground.chunk = a2_scene_nearest_chunk(scene, car.x, car.z);
            denv.turbo_active = turbo_on;
            /* the game-over and victory sequences force the handbrake on */
            denv.forced_handbrake = arrested || finished;
            /* where the car starts the frame, for the contact test (contact.h) */
            pre_x = drv.x;
            pre_z = drv.z;
            pre_h = drv.heading;
            /* The driving model once (0x40fc30), then the engine moves the
             * player in 8 steps of dt/8 (0x440fb0: [0x4e65d8] is the player),
             * each only as far as its earliest contact (0x44a0f0, contact.c)
             * against the barriers (0x449c10's slabs), solid scenery and the
             * other vehicles, so the car never sinks into them.  A contact
             * the car runs into is re-tested as 0x441490 does it, and its
             * impulse is applied in the step (0x4497f0, step_impulse), so the
             * next step moves with the new velocity.  The collision blocks
             * after the move keep the push-out, the damage and the sound,
             * and apply an impulse only for a contact no step saw. */
            denv.no_integrate = 1;
            a2_drive_step(&drv, &denv, dt);
            denv.no_integrate = 0;
            toi.kind = TOI_NONE;
            for (int k = 0; k < 6; k++) {
                step_j[k] = 0.0f;
                step_idx[k] = -2;
            }
            float hit_hl = car_hand ? car_hand->length * 0.5f : 2.25f;
            float hit_hw = car_hand ? car_hand->width * 0.5f : 0.85f;
            float hit_m = car_hand && car_hand->mass > 0.0f ? car_hand->mass : 1000.0f;
            {
                a2_cbox cand[320];
                int ckind[320], cidx[320], nc = 0;
                int pc = a2_scene_nearest_chunk(scene, drv.x, drv.z);
                int nr = a2_scene_rail_boxes(scene, pc, cand, 200);
                for (int i = 0; i < nr; i++) {
                    ckind[nc] = TOI_RAIL;
                    cidx[nc++] = i;
                }
                int sidx[64];
                int ns = a2_scene_solid_boxes(scene, pc, cand + nc, sidx, 64);
                for (int i = 0; i < ns; i++) {
                    ckind[nc] = TOI_OBJECT;
                    cidx[nc++] = sidx[i];
                }
                for (int i = 0; traffic && i < a2_traffic_count(traffic) && nc < 320; i++)
                    if (a2_traffic_car_box(traffic, i, &cand[nc]) &&
                        fabsf(cand[nc].x - drv.x) < 300.0f && fabsf(cand[nc].z - drv.z) < 300.0f) {
                        ckind[nc] = TOI_TRAFFIC;
                        cidx[nc++] = i;
                    }
                for (int kind = 0; kind < 2; kind++) {
                    int cnt = kind == 0 ? a2_racers_count(racers) : a2_cops_count(cops);
                    for (int i = 0; i < cnt && nc < 320; i++) {
                        a2_drive *od = kind == 0 ? a2_racer_drive(racers, i) : a2_cop_drive(cops, i);
                        if (!od || fabsf(od->x - drv.x) > 300.0f || fabsf(od->z - drv.z) > 300.0f)
                            continue;
                        a2_cbox *b = &cand[nc];
                        if (kind == 0) {
                            int sl = a2_racer_car(racers, i);
                            b->x1 = a2_car_half_width(sl);
                            b->z1 = a2_car_half_length(sl);
                        } else {
                            a2_cop_half_size(&b->x1, &b->z1);
                        }
                        b->x0 = -b->x1;
                        b->z0 = -b->z1;
                        b->x = od->x;
                        b->z = od->z;
                        b->heading = od->heading;
                        ckind[nc] = kind == 0 ? TOI_RACER : TOI_COP;
                        cidx[nc++] = i;
                    }
                }
                float hw = car_half_width, hl = car_half_len, sdt = dt / 8.0f;
                /* earliest contact of a move from `self` among the candidates;
                 * 2 when none */
#define EARLIEST(self, mx, mz, hit)                                                                \
    ({                                                                                             \
        float best_ = 2.0f;                                                                        \
        for (int i = 0; i < nc; i++) {                                                             \
            float t_, pt_[2], nn_[2];                                                              \
            if (a2_contact(&(self), (mx), (mz), &cand[i], &t_, pt_, nn_) && t_ < best_) {           \
                best_ = t_;                                                                        \
                (hit) = (toi_hit){ckind[i], cidx[i], {nn_[0], nn_[1]}, {pt_[0], pt_[1]}};          \
            }                                                                                      \
        }                                                                                          \
        best_;                                                                                     \
    })
                for (int sub = 0; sub < 8; sub++) {
                    float hh = floorf(drv.heading);
                    float ang = (float)(((int)hh) & 0xfff) * (6.28318531f / 4096.0f);
                    float sn = sinf(ang), cs = cosf(ang);
                    float vx = cs * drv.v_lat - sn * drv.v_fwd, vz = sn * drv.v_lat + cs * drv.v_fwd;
                    float mx = 10.0f * sdt * vx, mz = 10.0f * sdt * vz;
                    a2_cbox self = {-hw, -hl, hw, hl, drv.x, drv.z, drv.heading};
                    toi_hit hit = {TOI_NONE, -1, {0.0f, 0.0f}, {0.0f, 0.0f}};
                    float best = EARLIEST(self, mx, mz, hit);
                    if (best > 1.0f) {
                        a2_drive_integrate_move(&drv, sdt, mx, mz);
                        continue;
                    }
                    /* 0x441490: a contact the car's velocity runs into
                     * (v . n >= 0 here; the record's n is the other way) is
                     * re-tested with the normal part of the velocity set to
                     * 10 away from it, and the car moves by that velocity as
                     * far as its own earliest contact; otherwise, and at
                     * t = 0, the car stops at the contact (0x43f900 with
                     * the move x t). The real velocity is left alone; the
                     * impulse changes it. */
                    float vn = vx * hit.n[0] + vz * hit.n[1];
                    if (best > 0.0f && vn >= 0.0f) {
                        float k = -10.0f - vn; /* the normal part becomes -10 */
                        float wx = vx + k * hit.n[0], wz = vz + k * hit.n[1];
                        float wmx = 10.0f * sdt * wx, wmz = 10.0f * sdt * wz;
                        toi_hit h2 = hit;
                        float t2 = EARLIEST(self, wmx, wmz, h2);
                        (void)h2;
                        if (t2 > 1.0f)
                            t2 = 1.0f;
                        a2_drive_integrate_move(&drv, sdt, wmx * t2, wmz * t2);
                    } else if (best > 0.0f || vn >= 0.0f) {
                        /* stopped at the contact; at t = 0 not moved at all,
                         * the impulse below turning it away */
                        a2_drive_integrate_move(&drv, sdt, mx * best, mz * best);
                    } else {
                        /* Port deviation: a box already overlapping the car
                         * that it moves away from would hold it at t = 0 for
                         * ever (the overlap's "nearest side" is arbitrary).
                         * The original never gets into an overlap; the port
                         * can (the AI's own moves, the turn), so it lets the
                         * move go and the push-out separates them. */
                        a2_drive_integrate_move(&drv, sdt, mx, mz);
                    }
                    /* the impulse, in the step (0x4497f0 after 0x43f900),
                     * when the contact point moves into the obstacle */
                    {
                        float rx = 0.1f * (hit.p[0] - drv.x), rz = 0.1f * (hit.p[1] - drv.z);
                        float cvx = vx - drv.yaw_rate * rz, cvz = vz + drv.yaw_rate * rx;
                        if (cvx * hit.n[0] + cvz * hit.n[1] > 0.0f) {
                            if (toi.kind == TOI_NONE)
                                toi = hit; /* the frame's first, for the blocks below */
                            float j = step_impulse(&drv, &hit, hit_hl, hit_hw, hit_m, scene,
                                                   traffic, racers, cops, &handling);
                            if (step_idx[hit.kind] == -2 || j > step_j[hit.kind]) {
                                step_j[hit.kind] = fmaxf(step_j[hit.kind], j);
                                step_idx[hit.kind] = hit.idx;
                            }
                        }
                    }
                }
#undef EARLIEST
                if (getenv("A2_COL_LOG") && toi.kind != TOI_NONE)
                    fprintf(stderr, "toi: kind %d idx %d n (%.2f, %.2f)\n", toi.kind, toi.idx,
                            toi.n[0], toi.n[1]);
            }
            step_dx = drv.x - pre_x;
            step_dz = drv.z - pre_z;
            car.x = drv.x;
            car.z = drv.z;
            car.y = drv.y;
            car.speed = drv.v_fwd;
            car.yaw = drive_yaw_deg(&drv);
            car.gear = drv.gear;
            car.rpm = drv.rpm;
            /* the body's visual roll and pitch are object angles, 4096 a turn */
            car.roll = drv.roll * (360.0f / 4096.0f);
            car.pitch_ang = drv.pitch * (360.0f / 4096.0f);
            car.ride = 0.0f;
            {
                /* 0x413900 (0x426029): spin, steer and stand the wheels */
                float gy;
                float gmy = drive_height(&dground, drv.x, drv.z, &gy) ? gy - drv.y : 0.0f;
                a2_wheels_update_player(&wheels, drv.v_fwd, drv.steer, gmy, dt);
            }
            /* 0x425a90: while skidding (intensity >= 4), every 100 ms, a pair
             * of puffs at each rear wheel -- 10 * f6 behind the car, 9 units
             * either side, 3 up -- carried along at 0.6 of the car's velocity
             * (per frame, as the original moves them). */
            if (smoke && drv.skid >= 4 && race_time - smoke_last >= 0.1f) {
                smoke_last = race_time;
                float h = a2_drive_heading_rad(&drv), sn = sinf(h), cs = cosf(h);
                float back = 10.0f * rear_axle;
                float vx = 0.6f * (cs * drv.v_lat - sn * drv.v_fwd);
                float vz = 0.6f * (cs * drv.v_fwd + sn * drv.v_lat);
                for (int side = -1; side <= 1; side += 2)
                    a2_smoke_spawn(smoke, drv.x + sn * back + cs * 9.0f * side, drv.y + 3.0f,
                                   drv.z - cs * back + sn * 9.0f * side, vx, vz, dt);
            }
            a2_smoke_update(smoke);
            /* 0x424afd: the skidmarks get the skid intensity every frame */
            a2_skid_lay(skid, drv.skid, drv.x, drv.y, drv.z, a2_drive_heading_rad(&drv), drv.v_lat,
                        drv.v_y, drv.v_fwd, 10.0f * rear_axle, dt);
            a2_skid_update(skid);
            /* Knock-over props: their type handlers run each frame for the
             * objects near the player (0x4408fc; the window is the port's
             * choice).  The contact radius is the car mesh's, as 0x446930
             * adds the two objects' radii. */
            {
                int pc = a2_scene_nearest_chunk(scene, car.x, car.z);
                a2_scene_objects_step(scene, pc - 5, pc + 15, dt);
                int cams = a2_scene_props_step(scene, pc - 5, pc + 15, car.x, car.z, car_radius,
                                               drv.v_lat, drv.v_fwd, drv.heading, &prop_rng);
                if (cams) {
                    radar_knocked += cams;
                    RAISE_BANNER(51); /* 0x428da3 */
                    fprintf(stderr, "radar bonus: camera knocked over (%d) at %.1f s, frame %d\n", radar_knocked, race_time, frames);
                }
            }
            /* The lights come on when the drive function's force opposes the
             * motion (0x4a63cc, set at 0x40ddee) -- braking -- or while
             * 0x4d9df4 is set, LIKELY reversing (0x425f0b..0x425faa); they are
             * re-stamped only when the state changes, as the original does. */
            {
                int lit = drv.braking || car.speed < 0.0f;
                if (lit != brake_lit) {
                    brake_lit = lit;
                    if (brake_light_model < 0) /* the stamp is the other path */
                        stamp_brake_light(scene, hud, car_slot, lit);
                }
            }
            /* The police light (0x40c860, texture-blit slot 14): while any cop
             * has its siren on (the count [0x4a5d38] kept by 0x40c640, made a
             * flag in 0x4a5d30), every 200 ms of GetTickCount it stamps the
             * current frame of anim 41 into CARS01.PCX at (99, 62) and flips
             * it; when the count falls back to 0, 0x40c76c stamps frame 0.
             * Anim 41 is (326, 325), RC.IMG 325 (dark) and 324 (lit) once the
             * +1 is taken off; CARS01 ships with the lit one, so until the
             * first chase every police car shows it.  One sheet, so every
             * police car flashes together, the player's too in COP MODE.
             * 0x42ae90 does nothing without texturecopy. */
            if (texture_copy && hud) {
                int on = a2_cops_in_pursuit(cops);
                Uint32 now = SDL_GetTicks();
                if (on && now - pol_light_at >= 200) {
                    pol_light_at = now;
                    stamp_sprite(scene, hud, pol_light_frame ? 324 : 325, "CARS01.PCX", 99, 62);
                    pol_light_frame = !pol_light_frame;
                } else if (!on && pol_light_was_on) {
                    stamp_sprite(scene, hud, 325, "CARS01.PCX", 99, 62);
                }
                pol_light_was_on = on;
            }
            if (audio) {
                int S = a2_audio_sfx_volume(audio);
                /* 0x427070, the two engine loops on the rpm. */
                int rpm = (int)car.rpm;
                int f1 = 24000 - rpm, v1 = -rpm / 8;
                f1 = f1 < 11000 ? 11000 : f1 > 24000 ? 24000 : f1;
                v1 = v1 > -100 ? -100 : v1 < -2000 ? -2000 : v1;
                a2_audio_set(audio, snd_motor, f1, v1 - S - 200, 0);
                int f2 = 10000 + 3 * rpm, v2 = rpm / 2 - 2000;
                f2 = f2 > 40000 ? 40000 : f2;
                v2 = v2 > -100 ? -100 : v2 < -2000 ? -2000 : v2;
                a2_audio_set(audio, snd_toeren, f2, v2 - S - 200, 0);
                /* 0x4259a0: the tyre-slip loop, sample 39 sliploop.wav, while
                 * the skid intensity is 4 or more; pitch and level rise with
                 * it, and it stops below 4. */
                if (drv.skid >= 4) {
                    int i = drv.skid;
                    int f = 15000 + 700 * i, v = 100 * (i - 11);
                    f = f > 28000 ? 28000 : f;
                    v = v > -100 ? -100 : v < -1100 ? -1100 : v;
                    if (!a2_audio_playing(audio, snd_slip))
                        snd_slip = a2_audio_play(audio, 39, 1);
                    a2_audio_set(audio, snd_slip, f, v - S - 100, 0);
                } else if (snd_slip >= 0) {
                    a2_audio_stop(audio, snd_slip);
                    snd_slip = -1;
                }
                /* 0x425d47: turbo.wav once as the turbo engages. */
                if (turbo_on && !snd_turbo_was)
                    a2_audio_play(audio, 84, 0);
                snd_turbo_was = turbo_on;
                /* 0x424ee0: the horn, key T, sample 41 toeter.wav. */
                if (k[SDL_SCANCODE_T] && !a2_audio_playing(audio, snd_horn))
                    snd_horn = a2_audio_play(audio, 41, 0);
                /* 0x424bd3: the checkpoint clock ticks (sample 10) on each of
                 * its last five seconds. */
                if (timed && !finished && !arrested && time_left < 6.0f) {
                    int sec = (int)time_left;
                    if (sec != snd_tick_sec) {
                        snd_tick_sec = sec;
                        a2_audio_play(audio, 10, 0);
                    }
                } else {
                    snd_tick_sec = -1;
                }
                /* 0x425fbe: the rattles, 37 rammel from damage 65 and 38
                 * rammel2 from 75, each stopping again below its threshold. */
                for (int r = 0; r < 2; r++) {
                    int on = damage >= (r ? 75 : 65);
                    if (on && !a2_audio_playing(audio, snd_rattle[r]))
                        snd_rattle[r] = a2_audio_play(audio, 37 + r, 1);
                    else if (!on && snd_rattle[r] >= 0) {
                        a2_audio_stop(audio, snd_rattle[r]);
                        snd_rattle[r] = -1;
                    }
                }
            }
            if (!finished)
                race_time += dt;

            int ch = a2_scene_nearest_chunk(scene, car.x, car.z);
            /* Chunks travelled, counted the way the original keeps its
             * 0x4d9e70: a running sum of chunk-to-chunk steps, with the step
             * taken the short way round so a circuit's index wrap counts as
             * one chunk forward.  It used to be (lap - 1) * chunks + chunk,
             * which on a circuit drops a whole lap between the index wrap and
             * the start/finish line -- where every circuit starts the car --
             * and so ranked the player a lap down there.  The racers' rubber
             * band reads this, which is how that showed up. */
            {
                int step = ch - progress_chunk;
                if (scene_chunks > 0) {
                    if (step > scene_chunks / 2)
                        step -= scene_chunks;
                    else if (step < -scene_chunks / 2)
                        step += scene_chunks;
                }
                progress_chunk = ch;
                player_progress += (float)step;
            }

            /* Collisions, with the original's impulse (collide_impulse):
             * barriers (the .cmp rails, a world collision), the scenery the
             * original registers (types 5 / 8 / 10, objects), and traffic.
             * The push-out keeps the car clear; the impulse sets its
             * velocity, spin and hop.  The magnitude of the first frame of a
             * contact drives the damage and the crunch (0x4273b0, 0x427cc0). */
            float slide[2] = {0.0f, 0.0f};
            /* This frame's collision for the sound (0x427cc0): the impact
             * magnitude on the first frame of a contact, 0 while it lasts,
             * -1 for no contact. */
            float col_mag = -1.0f;
            float hit_mag = 0.0f; /* for the damage, first frame only */
            drv.v_fwd = car.speed;
            int touching =
                a2_scene_collide_rails(scene, ch, &car.x, &car.z, car_half_width, slide);
            last_rail_hit = touching;
            if (toi.kind == TOI_RAIL && !touching) { /* stopped at a barrier by the move */
                touching = 1;
                slide[0] = -toi.n[0]; /* the barrier's outward normal */
                slide[1] = -toi.n[1];
            }
            if (touching) {
                /* slide is the wall's outward normal; into the wall is -slide.
                 * The car's restitution, 0.2 (0x410cc0), against the world. */
                /* the barrier's slab contact from the move, else the wall's
                 * own normal (the push-out found it) */
                float n[2] = {-slide[0], -slide[1]}, cp[2];
                int at = toi_take(&toi, TOI_RAIL, -1, n, cp);
                float j = step_idx[TOI_RAIL] != -2
                              ? step_j[TOI_RAIL] /* the steps applied it */
                              : a2_drive_impulse_at(&drv, hit_hl, hit_hw, hit_m, n[0], n[1], 0.2f,
                                                    0.0f, 0.0f, 0.0f, 1, NULL, NULL, 0.0f, 0.0f,
                                                    car.x, car.z, at ? cp : NULL);
                col_mag = fmaxf(col_mag, was_touching ? 0.0f : j);
                if (!was_touching)
                    hit_mag = fmaxf(hit_mag, j);
                /* An autopilot crutch, not physics: a person steers away
                 * from a barrier after hitting it, the lane-follower keeps
                 * aiming into it (etappe2's city corner at chunk 115).  Turn
                 * an autopilot car a quarter of the way to the barrier's
                 * line. */
                if (drive_test || autopilot) {
                    /* the barrier's line is at right angles to its normal */
                    float wall_yaw = atan2f(slide[0], -slide[1]) * 180.0f / (float)M_PI + 90.0f;
                    float dd = wall_yaw - drive_yaw_deg(&drv);
                    while (dd > 180.0f)
                        dd -= 360.0f;
                    while (dd < -180.0f)
                        dd += 360.0f;
                    if (dd > 90.0f)
                        dd -= 180.0f;
                    else if (dd < -90.0f)
                        dd += 180.0f;
                    drv.heading += dd * 0.25f * (4096.0f / 360.0f);
                    car.yaw = drive_yaw_deg(&drv);
                }
                ch = a2_scene_nearest_chunk(scene, car.x, car.z);
            }
            /* Scenery: columns (immovable, restitution 0.1), arrows (13000 kg)
             * and parked police cars (1300 kg), both 0.2 (0x445441..0x445486);
             * the port does not move them. */
            float obj_push[2] = {0.0f, 0.0f};
            int obj_type = 0, obj_inst = -1;
            int objhit = a2_scene_collide_objects(scene, ch, &car.x, &car.z, car_half_width,
                                                  obj_push, &obj_type, &obj_inst);
            if (!objhit && toi.kind == TOI_OBJECT) { /* stopped at it by the move */
                obj_inst = toi.idx;
                obj_type = a2_scene_object_type(scene, obj_inst);
                obj_push[0] = -toi.n[0];
                obj_push[1] = -toi.n[1];
                objhit = 1;
            }
            if (objhit) {
                /* columns are immovable; arrows and parked police cars are
                 * 4 x 1 m boxes that the impulse pushes (a2_scene_objects_step) */
                float om = obj_type == 8 ? 13000.0f : obj_type == 10 ? 1300.0f : 0.0f;
                float oe = obj_type == 5 ? 0.1f : 0.2f;
                float opos[2] = {0.0f, 0.0f}, dv2[3];
                a2_scene_object_pos(scene, obj_inst, &opos[0], &opos[1]);
                float n[2] = {-obj_push[0], -obj_push[1]}, cp[2];
                a2_cbox ob;
                int at = toi_take(&toi, TOI_OBJECT, obj_inst, n, cp) ||
                         (a2_scene_object_box(scene, obj_inst, &ob) &&
                          engine_contact(pre_x, pre_z, pre_h, car_half_width,
                                         car_half_len, step_dx, step_dz, &ob, n,
                                         cp));
                int stepped = step_idx[TOI_OBJECT] == obj_inst;
                float j = stepped ? step_j[TOI_OBJECT]
                                  : a2_drive_impulse_at(&drv, hit_hl, hit_hw, hit_m, n[0], n[1],
                                                        0.5f * (0.2f + oe), om, 0.0f, 0.0f,
                                                        om <= 0.0f, dv2, opos, 4.0f, 1.0f, car.x,
                                                        car.z, at ? cp : NULL);
                if (om > 0.0f && !stepped) {
                    a2_scene_object_push(scene, obj_inst, dv2[0], dv2[1], dv2[2]);
                    if (getenv("A2_COL_LOG"))
                        fprintf(stderr, "push: object %d (type %d) by (%.2f, %.2f) m/s, spin %.3f, j %.0f\n",
                                obj_inst, obj_type, dv2[0], dv2[1], dv2[2], j);
                }
                col_mag = fmaxf(col_mag, was_touching ? 0.0f : j);
                if (!was_touching)
                    hit_mag = fmaxf(hit_mag, j);
                touching = 1;
                last_obj_hit = 1;
            } else {
                last_obj_hit = 0;
            }
            was_touching = touching;

            /* Traffic: the other vehicle's mass from the vehicle table (a bus
             * is 15000 kg), both restitutions 0.2; the traffic car takes its
             * share of the impulse along its heading. */
            float push[2] = {0.0f, 0.0f};
            int who = -1;
            int trhit = traffic && a2_traffic_collide(traffic, &car.x, &car.z, car_half_width, push, &who);
            if (!trhit && toi.kind == TOI_TRAFFIC) { /* stopped at it by the move */
                who = toi.idx;
                push[0] = -toi.n[0];
                push[1] = -toi.n[1];
                trhit = 1;
            }
            if (trhit) {
                float ovx = 0.0f, ovz = 0.0f, om = 1000.0f;
                const char *oanim = NULL;
                if (a2_traffic_car_motion(traffic, who, &ovx, &ovz, &oanim)) {
                    int hi = oanim ? a2_handling_find(&handling, oanim) : -1;
                    if (hi >= 0 && handling.v[hi].valid && handling.v[hi].mass > 0.0f)
                        om = handling.v[hi].mass;
                }
                float dv2[3];
                float n[2] = {-push[0], -push[1]}, cp[2];
                a2_cbox ob;
                int at = toi_take(&toi, TOI_TRAFFIC, who, n, cp) ||
                         (a2_traffic_car_box(traffic, who, &ob) &&
                          engine_contact(pre_x, pre_z, pre_h, car_half_width,
                                         car_half_len, step_dx, step_dz, &ob, n,
                                         cp));
                float j;
                if (step_idx[TOI_TRAFFIC] == who) {
                    j = step_j[TOI_TRAFFIC]; /* the steps applied it */
                } else {
                    j = a2_drive_impulse_at(&drv, hit_hl, hit_hw, hit_m, n[0], n[1], 0.2f, om,
                                            ovx, ovz, 0, dv2, NULL, 0.0f, 0.0f, car.x, car.z,
                                            at ? cp : NULL);
                    a2_traffic_car_push(traffic, who, dv2[0], dv2[1]);
                }
                col_mag = fmaxf(col_mag, was_hitting_car ? 0.0f : j);
                if (!was_hitting_car)
                    hit_mag = fmaxf(hit_mag, j);
                was_hitting_car = 1;
            } else {
                was_hitting_car = 0;
            }
            /* The racers and the cops are vehicles to the engine too: the
             * same impulse, the car table's mass, restitution 0.2 for a racer
             * and 0.3 for a cop (init_physics callers), and each takes its
             * share.  (Until session 13 the player passed through them.) */
            for (int kind = 0; kind < 2; kind++) {
                int whoa = -1;
                float pa[2] = {0.0f, 0.0f};
                int hitp = kind == 0
                               ? a2_racers_collide(racers, &car.x, &car.z, car_half_width, pa, &whoa)
                               : a2_cops_collide(cops, &car.x, &car.z, car_half_width, pa, &whoa);
                if (!hitp && toi.kind == (kind == 0 ? TOI_RACER : TOI_COP)) {
                    whoa = toi.idx; /* stopped at it by the move */
                    pa[0] = -toi.n[0];
                    pa[1] = -toi.n[1];
                    hitp = 1;
                }
                if (!hitp)
                    continue;
                a2_drive *od = kind == 0 ? a2_racer_drive(racers, whoa) : a2_cop_drive(cops, whoa);
                if (!od)
                    continue;
                const char *oanim = kind == 0 ? a2_car_anim_name(a2_racer_car(racers, whoa))
                                              : "MPOLIZEI_ANIM";
                float om = 1000.0f, ovx, ovz, dv2[3];
                int hi = a2_handling_find(&handling, oanim);
                if (hi >= 0 && handling.v[hi].valid && handling.v[hi].mass > 0.0f)
                    om = handling.v[hi].mass;
                a2_ai_world_velocity(od, &ovx, &ovz);
                float opos[2] = {od->x, od->z};
                /* the other body's length and width in metres, for its spin */
                int oslot = kind == 0 ? a2_racer_car(racers, whoa) : 3;
                float ol = 0.2f * a2_car_half_length(oslot), ow = 0.2f * a2_car_half_width(oslot);
                float n[2] = {-pa[0], -pa[1]}, cp[2];
                a2_cbox ob = {0};
                if (kind == 0) {
                    ob.x1 = a2_car_half_width(oslot);
                    ob.z1 = a2_car_half_length(oslot);
                } else {
                    a2_cop_half_size(&ob.x1, &ob.z1);
                }
                ob.x0 = -ob.x1;
                ob.z0 = -ob.z1;
                ob.x = od->x;
                ob.z = od->z;
                ob.heading = od->heading;
                int at = toi_take(&toi, kind == 0 ? TOI_RACER : TOI_COP, whoa, n, cp) ||
                         engine_contact(pre_x, pre_z, pre_h, car_half_width,
                                        car_half_len, step_dx, step_dz, &ob, n, cp);
                int tk = kind == 0 ? TOI_RACER : TOI_COP;
                float j;
                if (step_idx[tk] == whoa) {
                    j = step_j[tk]; /* the steps applied it */
                } else {
                    j = a2_drive_impulse_at(&drv, hit_hl, hit_hw, hit_m, n[0], n[1],
                                            kind == 0 ? 0.2f : 0.25f, om, ovx, ovz, 0, dv2, opos,
                                            ol, ow, car.x, car.z, at ? cp : NULL);
                    a2_ai_push(od, dv2[0], dv2[1], dv2[2]);
                }
                if (j > 0.0f) {
                    col_mag = fmaxf(col_mag, j);
                    hit_mag = fmaxf(hit_mag, j);
                }
            }
            car.speed = drv.v_fwd;
            if (car.speed < -REVERSE_MAX)
                car.speed = -REVERSE_MAX;
            if (getenv("A2_COL_LOG") && (touching || last_obj_hit || was_hitting_car))
                fprintf(stderr, "col t=%.2f chunk %d rail %d obj %d (type %d) car %d v_fwd %.1f v_lat %.1f w %.2f\n",
                        race_time, ch, last_rail_hit, last_obj_hit, obj_type, was_hitting_car,
                        drv.v_fwd, drv.v_lat, drv.yaw_rate);
            /* Damage, the original's (fcn.00427DE0 tail, read out of
             * 0x4273b0): below 18000 nothing; 18000..35000 from about half
             * to all of a per-impact maximum of 12 on HARD or 6 on EASY. */
            {
                float mag = hit_mag;
                if (mag >= 18000.0f) {
                    if (mag > 35000.0f)
                        mag = 35000.0f;
                    int a = (int)(mag / 350.0f); /* 51..100 */
                    int maxd = difficulty ? 12 : 6;
                    int dmg = (int)(maxd * 0.01f * a);
                    if (dmg > maxd)
                        dmg = maxd;
                    if (dmg > 0 && !invincible && !arrested) {
                        damage += dmg;
                        if (damage > 100)
                            damage = 100;
                        fprintf(stderr, "impact: %d damage, total %d/100\n", dmg, damage);
                        /* TOTAL LOSS.  The per-frame check at 0x424be8
                         * ends the race in game-over sub-mode 0 once the
                         * damage reaches 99 while racing (game mode 0). */
                        if (damage >= 99) {
                            arrested = 1;
                            game_over_msg = 40;
                            fprintf(stderr, "wrecked at %.1f s -- TOTAL LOSS\n", race_time);
                        }
                    }
                }
            }

            /* The collision sound, 0x427cc0: within 300 ms of the last
             * collision call a light contact keeps the scrape (82 schuur2)
             * going; otherwise an impact plays graded on the magnitude,
             * carsoft / carmid / carhard (25-30) at 18000 and 25000, one of
             * two at random.  Every call restarts the 300 ms. */
            if (audio && col_mag >= 0.0f) {
                if (race_time - snd_last_col < 0.3f) {
                    if (col_mag < 18000.0f && car.speed > 1.0f && !a2_audio_playing(audio, snd_scrape))
                        snd_scrape = a2_audio_play(audio, 82, 0);
                } else {
                    int base = col_mag < 18000.0f ? 25 : col_mag < 25000.0f ? 27 : 29;
                    a2_audio_play(audio, base + (int)(snd_rand() & 1u), 0);
                }
                snd_last_col = race_time;
            }

            /* Point-to-point stages end at TRIGGER_FINISH_POSITION. */
            if (!circuit && !finished && finish_chunk >= 0 && ch >= finish_chunk) {
                finished = 1;
                finish_time = race_time;
                fprintf(stderr,
                        "finished at chunk %d in %.1f s, position %d of %d "
                        "(traffic made %d lane changes)\n",
                        ch, finish_time, PLAYER_POSITION(),
                        a2_racers_count(racers) + 1, a2_traffic_lane_changes(traffic));
            }

            if (circuit && !finished && scene_chunks > 16 && finish_chunk >= 0) {
                /* A lap is counted exactly as the original counts it.
                 * `check_racer_trigger` gives trigger **15**
                 * (TRIGGER_FINISH_VALID) one job -- set an armed flag on the
                 * racer -- and trigger **14** (TRIGGER_FINISH_POSITION) checks
                 * that flag, and only then increments the lap counter and
                 * compares it with Nrlaps.  The chunk-0 marker (trigger 9) has
                 * nothing to do with laps: its handler sets a crossed flag and
                 * toggles three booleans, which is chunk-index wrap
                 * bookkeeping.
                 *
                 * The shipped circuits place the two one chunk apart -- 524 and
                 * 525 on etappe0/6, 16 and 17 on etappe2, 329 and 330 on
                 * etappe4 -- so crossing the line disarms and the very next
                 * chunk re-arms for the following lap.  The car starts one
                 * chunk *before* the line, so its first crossing is unarmed and
                 * scores nothing, which is what makes the first fire the real
                 * end of lap 1.  This replaces a distance window of our own. */
                if (crossed_chunk(prev_ch, ch, finish_valid_chunk, scene_chunks))
                    lap_armed = 1;
                if (lap_armed && crossed_chunk(prev_ch, ch, finish_chunk, scene_chunks)) {
                    lap_armed = 0;
                    last_lap = race_time - lap_start;
                    if (best_lap <= 0.0f || last_lap < best_lap)
                        best_lap = last_lap;
                    /* Tr_finis.cpp (0x429860): a lap under LAP_RECORD is the
                     * new record, and the lap's time checks become the saved
                     * set; the saved count is the lap's own either way.  The
                     * circuits carry no checkpoints, so that count is 0. */
                    /* and raises 31 NIEUW RONDE RECORD for a record lap,
                     * otherwise 45 LAATSTE RONDE (0x4298d5 / 0x429921) --
                     * on every lap, the code has no test for the last one */
                    if ((int)(last_lap * 1000.0f) < save.lap_record) {
                        save.lap_record = (int)(last_lap * 1000.0f);
                        RAISE_BANNER(31);
                    } else {
                        RAISE_BANNER(45);
                    }
                    save.time_checks = 0;
                    lap_start = race_time;
                    lap++;
                    fprintf(stderr, "lap %d complete in %.1f s\n", lap - 1, last_lap);
                    /* Game.ini [OPTIONS] Nrlaps: "Number of laps to drive in
                     * city tracks.  Range: 1-999 (default=2)". */
                    if (lap > nr_laps) {
                        finished = 1;
                        finish_time = race_time;
                        fprintf(stderr, "finished %d laps in %.1f s, position %d of %d\n",
                                nr_laps, finish_time,
                                PLAYER_POSITION(),
                                a2_racers_count(racers) + 1);
                    }
                }
            }
            /* fcn.00434460 runs when the player's chunk changes: it starts the
             * paths whose first_chunk this is, on their lap, and releases
             * objects whose last_chunk it is.  Laps completed is the
             * original's 0x4d9ad4, clamped to Nrlaps. */
            if (ch != prev_ch)
                a2_paths_on_chunk(paths, ch, lap - 1 < nr_laps ? lap - 1 : nr_laps);
            /* the traffic-sign triggers of every chunk crossed forwards */
            if (texture_copy && prev_ch >= 0 && ch > prev_ch && ch - prev_ch < 16)
                for (int c = prev_ch + 1; c <= ch; c++) {
                    stamp_traffic_sign(scene, hud, etappe_no, (int)track.pmp.chunks[c].trigger0, !circuit);
                    stamp_traffic_sign(scene, hud, etappe_no, (int)track.pmp.chunks[c].trigger1, !circuit);
                }
            a2_paths_update(paths, dt);
            prev_ch = ch;

            if (next_camera < camera_count && ch >= camera_chunk[next_camera]) {
                /* The game keeps the posted limit in a single global, 0x48C480,
                 * whose **static initial value is -1** and which only the
                 * trigger-4/5/6/8 handlers ever write.  The camera compares
                 * `(int)(v * 3.6) > limit` with no tolerance, so where nothing
                 * has been posted the comparison is against -1 and **every**
                 * car that passes is flashed, at any speed.  That is why this
                 * starts at -1 rather than 0: etappe0 and etappe6 carry six
                 * cameras each and post no limit at all, and the original
                 * flashes you on all of them. */
                int limit = -1;
                for (int k = 0; k < limit_count && limit_sign[k].chunk <= ch; k++)
                    limit = limit_sign[k].limit;
                int kmh = (int)(car.speed * 3.6f + 0.5f);
                if (kmh > limit) {
                    flash_at = race_time;
                    if (audio)
                        a2_audio_play(audio, 11, 0); /* flits.wav, 0x42a30f */
                    if (limit < 0)
                        fprintf(stderr, "FLITS at chunk %d: %d km/h, no limit posted\n", ch, kmh);
                    else
                        fprintf(stderr, "FLITS at chunk %d: %d km/h in a %d zone\n", ch, kmh,
                                limit);
                }
                /* The original's second gate is `prev_chunk < current_chunk` on
                 * the car's record (+0x1c and +0x18), i.e. it fires on the frame
                 * the car advances a chunk forwards -- a direction check and a
                 * once-per-crossing guard in one.  The port instead fires each
                 * camera once per race, which differs only on a circuit, where
                 * the original would flash again on the next lap. */
                next_camera++;
            }

            /* The service station.  Unlike the camera this is not a one-shot
             * per race: the original's handler runs every time the trigger is
             * crossed, so a circuit can be serviced each lap. */
            for (int si = 0; si < service_count; si++) {
                if (!crossed_chunk(prev_ch, ch, service_chunk[si], scene_chunks))
                    continue;
                int repaired = damage;
                turbo_left = 1.0f;   /* set_turbo_tank(100) */
                damage = 0;          /* reset the damage */
                RAISE_BANNER(48); /* 0x42a51d */
                if (audio)
                    a2_audio_play(audio, 47, 0); /* repair.wav, 0x42a515 */
                fprintf(stderr, "QUICK SERVICE at chunk %d: turbo refilled, %d damage repaired\n",
                        service_chunk[si], repaired);
            }

            /* The finish, as the game books it: the track time and the race
             * record at the line (0x4299b6), then -- at the end of the
             * victory sequence in the original (0x415690), straight away
             * here -- the prize for the position plus 2000 per radar knocked
             * over, and finished = 1. */
            if (finished && !result_booked) {
                result_booked = 1;
                track_ms = (int)(finish_time * 1000.0f);
                if (track_ms < save.race_record)
                    save.race_record = track_ms;
                save.position = PLAYER_POSITION() - 1;
                int prize = a2_save_prize(save.position, save.etappe, difficulty);
                save.money += prize + radar_knocked * 2000;
                if (save.money > 999999)
                    save.money = 999999;
                fprintf(stderr, "result: position %d, %d ms, prize %d, money %d\n",
                        save.position, track_ms, prize, save.money);
            }

            if (next_checkpoint < checkpoint_count && ch >= checkpoint_chunk[next_checkpoint]) {
                split_time = race_time;
                next_checkpoint++;
                if (timed && !arrested) {
                    time_left += cp_bonus;
                    RAISE_BANNER(44); /* 0x42a621 */
                }
                if (audio)
                    a2_audio_play(audio, 83, 0); /* checkp.wav, 0x42a5a8 */
                fprintf(stderr, "checkpoint %d/%d at chunk %d, %.1f s\n", next_checkpoint,
                        checkpoint_count, ch, split_time);
            }

            /* the height is the model's own ride spring now (0x40ee80) */

            /* Camera views: the original's presets (cam_preset above) and its
             * engine camera update (0x447bd0..0x447f40).  The wanted position
             * is the car plus the preset's offset turned into the car's frame
             * (0x45e4e0); the look point is the car plus (0, 10, 0) the same
             * way, or straight ahead along the car without a target.  With the
             * lag on, each frame moves the camera `factor` of the way to where
             * it wants to be (the code divides by dt and multiplies by it
             * again, so it is per frame).  The port scales that to 30 frames a
             * second, since the original's step is its frame rate -- ours.
             * Then (0x447f78..0x448111, camera +0x80, set to 1 by the
             * constructor 0x447119 and never cleared) the lagged position is
             * turned into the car's frame (0x45e970, the transpose), its
             * along-the-car part replaced by the preset's z, and turned back:
             * only the sideways and height parts lag, the distance behind the
             * car does not.  The look point is not lagged (only the position,
             * +0x88, is).  "The car's frame" is its 3D object's matrix
             * (0x4ea9e0, 153-byte c_3dobject records, matrix +0x10, position
             * +0x50): the same object that is drawn, so the body's pitch
             * (+0x74) and roll (+0x7c) turn the camera too.  The port builds
             * it as the car is drawn: yaw, then pitch, then roll. */
            {
                /* key 3 (0x414500): held, keep the view and show preset 5;
                 * released, go back */
                const Uint8 *kb = SDL_GetKeyboardState(NULL);
                if (kb[SDL_SCANCODE_3]) {
                    if (cam_view != 5) {
                        cam_view_prev = cam_view;
                        cam_set = 5;
                    }
                } else if (cam_view == 5) {
                    cam_set = cam_view_prev;
                }
            }
            if (cam_set >= 0) {
                cam_view = cam_set;
                cam_set = -1;
                if (cam_preset[cam_view].smooth == CAM_SMOOTH) {
                    cam_smooth_on = 1;
                    cam_smooth = 0.245f;
                } else if (cam_preset[cam_view].smooth == CAM_RIGID) {
                    cam_smooth_on = 0;
                    cam_smooth = 1.0f;
                }
            }
            {
                /* the car the camera follows (M, tweak mode) */
                float tx = car.x, ty = car.y + car.ride, tz = car.z, tyaw = car.yaw;
                float tpitch = car.pitch_ang, troll = car.roll;
                if (cam_target < 3 && racers && cam_target < a2_racers_count(racers)) {
                    const a2_drive *od = a2_racer_drive(racers, cam_target);
                    if (od) {
                        tx = od->x;
                        ty = od->y;
                        tz = od->z;
                        tyaw = od->heading * (360.0f / 4096.0f) + 180.0f;
                        tpitch = od->pitch * (360.0f / 4096.0f);
                        troll = od->roll * (360.0f / 4096.0f);
                    }
                }
                /* M = Ry(180 - yaw) Rx(pitch) Rz(roll), the draw's order;
                 * columns are the car's x, y and z axes in the world */
                float M[3][3];
                {
                    float a = (180.0f - tyaw) * (float)M_PI / 180.0f;
                    float p = tpitch * (float)M_PI / 180.0f, r = troll * (float)M_PI / 180.0f;
                    float ca = cosf(a), sa = sinf(a), cp = cosf(p), sp = sinf(p), cr = cosf(r),
                          sr = sinf(r);
                    /* Rx Rz */
                    float B[3][3] = {{cr, -sr, 0.0f}, {cp * sr, cp * cr, -sp}, {sp * sr, sp * cr, cp}};
                    for (int c = 0; c < 3; c++) {
                        M[0][c] = ca * B[0][c] + sa * B[2][c];
                        M[1][c] = B[1][c];
                        M[2][c] = -sa * B[0][c] + ca * B[2][c];
                    }
                }
#define CAM_TURN(o, x, y, z)                                                                         \
    ((o)[0] = M[0][0] * (x) + M[0][1] * (y) + M[0][2] * (z),                                       \
     (o)[1] = M[1][0] * (x) + M[1][1] * (y) + M[1][2] * (z),                                       \
     (o)[2] = M[2][0] * (x) + M[2][1] * (y) + M[2][2] * (z))
                float ox = cam_preset[cam_view].x, oy = cam_preset[cam_view].y,
                      oz = cam_preset[cam_view].z;
                float off[3];
                CAM_TURN(off, ox, oy, oz);
                float want[3] = {tx + off[0], ty + off[1], tz + off[2]};
                float look[3];
                if (cam_preset[cam_view].target) {
                    CAM_TURN(off, 0.0f, 10.0f, 0.0f);
                    look[0] = tx + off[0];
                    look[1] = ty + off[1];
                    look[2] = tz + off[2];
                } else {
                    CAM_TURN(off, 0.0f, 0.0f, 100.0f); /* along the car */
                    look[0] = want[0] + off[0];
                    look[1] = want[1] + off[1];
                    look[2] = want[2] + off[2];
                }
                float jump = (want[0] - cam_eye[0]) * (want[0] - cam_eye[0]) +
                             (want[2] - cam_eye[2]) * (want[2] - cam_eye[2]);
                if (cam_fresh || !cam_smooth_on || jump > 2000.0f * 2000.0f) {
                    memcpy(cam_eye, want, sizeof want);
                    memcpy(cam_look, look, sizeof look);
                    cam_fresh = 0;
                } else {
                    float f = 1.0f - powf(1.0f - cam_smooth, dt * 30.0f);
                    for (int i = 0; i < 3; i++)
                        cam_eye[i] += f * (want[i] - cam_eye[i]);
                    /* hold the distance along the car at the preset's z:
                     * into the car's frame by the transpose, z replaced,
                     * and back */
                    float e[3] = {cam_eye[0] - tx, cam_eye[1] - ty, cam_eye[2] - tz};
                    float lx = M[0][0] * e[0] + M[1][0] * e[1] + M[2][0] * e[2];
                    float ly = M[0][1] * e[0] + M[1][1] * e[1] + M[2][1] * e[2];
                    CAM_TURN(off, lx, ly, oz);
                    cam_eye[0] = tx + off[0];
                    cam_eye[1] = ty + off[1];
                    cam_eye[2] = tz + off[2];
                    memcpy(cam_look, look, sizeof look);
                }
                float dx = cam_look[0] - cam_eye[0], dy = cam_look[1] - cam_eye[1],
                      dz = cam_look[2] - cam_eye[2];
                memcpy(cam.pos, cam_eye, sizeof cam_eye);
                cam.yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
                cam.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) * 180.0f / (float)M_PI;
#undef CAM_TURN
            }
            /* The police warning's camera (0x4151f0, camera 6 for the 4 s of
             * game mode 8): at the next chunk's point, 20 units up, looking at
             * the player's car 20 units up. */
            if (!arrested && race_time < pulled_until) {
                float cp[3];
                if (a2_scene_chunk_point(scene, ch + 1, cp)) {
                    float dx = car.x - cp[0], dy = (car.y + 20.0f) - (cp[1] + 20.0f),
                          dz = car.z - cp[2];
                    cam.pos[0] = cp[0];
                    cam.pos[1] = cp[1] + 20.0f;
                    cam.pos[2] = cp[2];
                    cam.yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
                    cam.pitch = atan2f(dy, sqrtf(dx * dx + dz * dz)) * 180.0f / (float)M_PI;
                }
            }

            char title[160];
            snprintf(title, sizeof title, "A2 Racer II - drive - %3.0f km/h - gear %d - chunk %d",
                     car.speed * 3.6f, car.gear + 1, ch);
            SDL_SetWindowTitle(win, title);
        }
        float speed = (k[SDL_SCANCODE_LSHIFT] ? 6000.0f : 1200.0f) * dt;
        float fwd[3];
        camera_forward(&cam, fwd);
        float right[3] = {-fwd[2], 0.0f, fwd[0]};
        float rl = sqrtf(right[0] * right[0] + right[2] * right[2]);
        if (rl > 1e-6f) {
            right[0] /= rl;
            right[2] /= rl;
        }
        if (!drive_mode) {
        if (k[SDL_SCANCODE_W])
            for (int i = 0; i < 3; i++)
                cam.pos[i] += fwd[i] * speed;
        if (k[SDL_SCANCODE_S])
            for (int i = 0; i < 3; i++)
                cam.pos[i] -= fwd[i] * speed;
        if (k[SDL_SCANCODE_D])
            for (int i = 0; i < 3; i++)
                cam.pos[i] += right[i] * speed;
        if (k[SDL_SCANCODE_A])
            for (int i = 0; i < 3; i++)
                cam.pos[i] -= right[i] * speed;
        if (k[SDL_SCANCODE_R])
            cam.pos[1] += speed;
        if (k[SDL_SCANCODE_F])
            cam.pos[1] -= speed;
        }

        if (traffic) {
            /* what the traffic's lane scan sees besides itself */
            a2_traffic_other oth[8];
            int no = 0;
            if (drive_mode)
                oth[no++] = (a2_traffic_other){car.x, car.y, car.z, car.speed};
            for (int i = 0; racers && i < a2_racers_count(racers) && no < 8; i++) {
                float rp[2];
                if (a2_racers_car_xz(racers, i, rp))
                    oth[no++] = (a2_traffic_other){rp[0], car.y, rp[1], a2_racer_speed(racers, i)};
            }
            a2_traffic_set_others(traffic, oth, no);
        }
        if (traffic)
            a2_traffic_update(traffic, scene,
                              drive_test ? 1.0f / 30.0f : dt,
                              drive_mode ? a2_scene_nearest_chunk(scene, car.x, car.z)
                                         : a2_scene_nearest_chunk(scene, cam.pos[0], cam.pos[2]));
        /* The new AI (ai_model 0): the player as it sees it, every vehicle's
         * waypoint into the obstacle lists, then the racers and the cops. */
        a2_ai_player aip;
        memset(&aip, 0, sizeof aip);
        if (aiw && (racers || cops)) {
            aip.x = car.x;
            aip.y = car.y;
            aip.z = car.z;
            aip.d = &drv;
            aip.chunk = a2_scene_nearest_chunk(scene, car.x, car.z);
            aip.wp = a2_aiw_nearest(aiw, aip.chunk, car.x, car.y, car.z, 1);
            aip.progress = player_progress - (float)player_chunk0;
            aip.progress_abs = player_progress;
            aip.rank = racers ? a2_racers_position(racers, player_progress) - 1 : 0;
            aip.skid = drv.skid;
            aip.stopped = drv.stopped;
            /* the game mode 0x4d9a7c: 8 the police sequence, 6 game over,
             * 3 the finish */
            aip.mode = arrested ? 6 : finished ? 3 : race_time < pulled_until ? 8 : 0;
            a2_aiw_occ_clear(aiw);
            if (drive_mode)
                a2_aiw_occ_add(aiw, aip.wp, A2_AI_ID_PLAYER, car.x, car.z, drv.v_fwd);
            for (int i = 0; traffic && i < a2_traffic_count(traffic); i++) {
                float tp[2];
                if (!a2_traffic_car_xz(traffic, i, tp))
                    continue;
                int tch = a2_scene_nearest_chunk(scene, tp[0], tp[1]);
                a2_aiw_occ_add(aiw, a2_aiw_nearest(aiw, tch, tp[0], car.y, tp[1], -1),
                               A2_AI_ID_TRAFFIC + i, tp[0], tp[1], a2_traffic_car_speed(traffic, i));
            }
            a2_racers_register(racers, aiw);
            a2_cops_register(cops, aiw);
        }
        /* The cops first: the original runs every record's transition
         * (0x403340) before any state (0x408ed0), so a cop judges a racer by
         * its speed after last frame's collisions, before the racer's own
         * state sets it again (state 11 pins it to 60 km/h). */
        if (racers && drive_mode)
            a2_racers_set_player(racers, &aip);
        if (cops && drive_mode && !finished && !arrested) {
            float slow = 1.0f;
            if (a2_cops_update(cops, &aip, racers, drive_test ? 1.0f / 30.0f : dt)) {
                car.speed *= slow;
                int n = a2_cops_times_caught(cops);
                /* Three strikes ends it, but this is not `finished`: that flag
                 * means the player crossed the line and it stops the race
                 * clock, which would leave every other car's time wrong. */
                if (n >= 3) {
                    arrested = 1;
                    game_over_msg = 42;
                } else {
                    pulled_until = race_time + 4.0f;
                    pulled_speed = car.speed * 0.5f;
                }
                fprintf(stderr, "caught by the police (%d) at %.1f s%s\n", n, race_time,
                        arrested ? " -- arrested" : "");
                /* 0x4153f1: the warning voice for this catch. */
                if (audio && n >= 1 && n <= 3) {
                    a2_audio_stop(audio, snd_voice);
                    snd_voice = a2_audio_play(
                        audio, VOICE_CATCH[n - 1][snd_rand() % (unsigned)VOICE_CATCH_N[n - 1]], 0);
                }
                snd_caught = 1;
            }
        }
        if (racers && drive_mode)
            a2_racers_update(racers, drive_test ? 1.0f / 30.0f : dt, race_time);
        /* The AI cars collide as every vehicle does in the original's engine:
         * with the barriers (a world collision, restitution 0.2 for a racer,
         * 0.3 for a cop), with traffic and with each other, by the same
         * impulse as the player (a2_drive_impulse). */
        if (aiw && drive_mode && (racers || cops)) {
            typedef struct {
                a2_drive *d;
                float hlu, hwu; /* half sizes, world units */
                float hlm, hwm, m, e;
            } ai_body;
            ai_body B[64];
            int nb = 0;
            for (int i = 0; racers && i < a2_racers_count(racers) && nb < 64; i++) {
                a2_drive *d = a2_racer_drive(racers, i);
                if (!d)
                    continue;
                int slot = a2_racer_car(racers, i);
                int hi = a2_handling_find(&handling, a2_car_anim_name(slot));
                const a2_handling *he = hi >= 0 && handling.v[hi].valid ? &handling.v[hi] : NULL;
                B[nb++] = (ai_body){d, a2_car_half_length(slot), a2_car_half_width(slot),
                                    he ? he->length * 0.5f : 2.0f, he ? he->width * 0.5f : 0.85f,
                                    he && he->mass > 0.0f ? he->mass : 1000.0f, 0.2f};
            }
            {
                int hi = a2_handling_find(&handling, "MPOLIZEI_ANIM");
                const a2_handling *he = hi >= 0 && handling.v[hi].valid ? &handling.v[hi] : NULL;
                float chw, chl;
                a2_cop_half_size(&chw, &chl);
                for (int i = 0; cops && i < a2_cops_count(cops) && nb < 64; i++) {
                    a2_drive *d = a2_cop_drive(cops, i);
                    if (d)
                        B[nb++] = (ai_body){d, chl, chw, he ? he->length * 0.5f : 2.0f,
                                            he ? he->width * 0.5f : 0.85f,
                                            he && he->mass > 0.0f ? he->mass : 1300.0f, 0.3f};
                }
            }
            /* the AI car's impulse at a contact from its step */
#define collide_impulse_at(A, H, E, M2, OVX, OVZ, WORLD, DV2, OPOS, OL, OW)                         \
    a2_drive_impulse_at((A)->d, (A)->hlm, (A)->hwm, (A)->m, (H).n[0], (H).n[1], (E), (M2), (OVX),    \
                        (OVZ), (WORLD), (DV2), (OPOS), (OL), (OW), (A)->d->x, (A)->d->z, (H).p)
            /* the engine's contact (0x44a0f0) moves a body by its velocity x
             * dt x 10 for the frame; from where it started */
            float adt = drive_test ? 1.0f / 30.0f : dt;
            for (int i = 0; i < nb; i++) {
                ai_body *a = &B[i];
                float avx, avz;
                a2_ai_world_velocity(a->d, &avx, &avz);
                float amx = avx * adt * 10.0f, amz = avz * adt * 10.0f;
                /* only near the player: the original collides everything, but
                 * far off nothing is drawn and the barriers are only loaded
                 * per chunk here as well */
                int ach = a2_scene_nearest_chunk(scene, a->d->x, a->d->z);
                /* The engine moves every object but the player in one step,
                 * only as far as its earliest contact (0x440fb0, contact.c).
                 * The AI cars have already moved; put the car back where it
                 * started, find that contact over the move, and slide along
                 * it for the rest (as the player's steps do).  The impulse
                 * for it follows here; the push-out below stays as the
                 * safety net. */
                toi_hit ah = {TOI_NONE, -1, {0.0f, 0.0f}, {0.0f, 0.0f}};
                {
                    float px = a->d->x - amx, pz = a->d->z - amz;
                    a2_cbox cand[300];
                    int ckind[300], cidx[300], nc = 0;
                    int nr = a2_scene_rail_boxes(scene, ach, cand, 200);
                    for (int k = 0; k < nr; k++) {
                        ckind[nc] = TOI_RAIL;
                        cidx[nc++] = k;
                    }
                    int sidx[48];
                    int ns = a2_scene_solid_boxes(scene, ach, cand + nc, sidx, 48);
                    for (int k = 0; k < ns; k++) {
                        ckind[nc] = TOI_OBJECT;
                        cidx[nc++] = sidx[k];
                    }
                    for (int k = 0; traffic && k < a2_traffic_count(traffic) && nc < 300; k++)
                        if (a2_traffic_car_box(traffic, k, &cand[nc]) &&
                            fabsf(cand[nc].x - px) < 300.0f && fabsf(cand[nc].z - pz) < 300.0f) {
                            ckind[nc] = TOI_TRAFFIC;
                            cidx[nc++] = k;
                        }
                    for (int k = 0; k < nb && nc < 300; k++) {
                        ai_body *b = &B[k];
                        if (k == i || fabsf(b->d->x - px) > 300.0f || fabsf(b->d->z - pz) > 300.0f)
                            continue;
                        cand[nc] = (a2_cbox){-b->hwu, -b->hlu, b->hwu, b->hlu, b->d->x, b->d->z,
                                             b->d->heading};
                        ckind[nc] = TOI_RACER; /* another AI body, by its index in B */
                        cidx[nc++] = k;
                    }
                    a2_cbox self = {-a->hwu, -a->hlu, a->hwu, a->hlu, px, pz, a->d->heading};
                    float best = 2.0f;
                    toi_hit first = {TOI_NONE, -1, {0.0f, 0.0f}, {0.0f, 0.0f}};
                    for (int k = 0; k < nc; k++) {
                        float t, pt[2], nn[2];
                        if (a2_contact(&self, amx, amz, &cand[k], &t, pt, nn) && t < best) {
                            best = t;
                            first = (toi_hit){ckind[k], cidx[k], {nn[0], nn[1]}, {pt[0], pt[1]}};
                        }
                    }
                    if (first.kind != TOI_NONE) {
                        /* the move, as the player's steps make it: 0x441490's
                         * re-test with the normal part 10 away for a contact
                         * the velocity runs into, else stopped at the contact
                         * (the overlap deviation as for the player) */
                        float vn = avx * first.n[0] + avz * first.n[1];
                        if (best > 0.0f && vn >= 0.0f) {
                            float kk = -10.0f - vn;
                            float wmx = (avx + kk * first.n[0]) * adt * 10.0f;
                            float wmz = (avz + kk * first.n[1]) * adt * 10.0f;
                            float t2 = 1.0f;
                            for (int k = 0; k < nc; k++) {
                                float t, pt[2], nn[2];
                                if (a2_contact(&self, wmx, wmz, &cand[k], &t, pt, nn) && t < t2)
                                    t2 = t;
                            }
                            a->d->x = px + wmx * t2;
                            a->d->z = pz + wmz * t2;
                        } else if (best > 0.0f || vn >= 0.0f) {
                            a->d->x = px + amx * best;
                            a->d->z = pz + amz * best;
                        }
                        /* the impulse when the contact point moves into it */
                        float rx = 0.1f * (first.p[0] - a->d->x), rz = 0.1f * (first.p[1] - a->d->z);
                        float cvx = avx - a->d->yaw_rate * rz, cvz = avz + a->d->yaw_rate * rx;
                        if (cvx * first.n[0] + cvz * first.n[1] > 0.0f)
                            ah = first;
                    }
                    if (ah.kind != TOI_NONE) {
                        if (ah.kind == TOI_RAIL) {
                            collide_impulse_at(a, ah, a->e, 0.0f, 0.0f, 0.0f, 1, NULL,
                                               NULL, 0.0f, 0.0f);
                        } else if (ah.kind == TOI_OBJECT) {
                            int ot = a2_scene_object_type(scene, ah.idx);
                            float om = ot == 8 ? 13000.0f : ot == 10 ? 1300.0f : 0.0f;
                            float oe = ot == 5 ? 0.1f : 0.2f, opos[2] = {0.0f, 0.0f}, dv2[3];
                            a2_scene_object_pos(scene, ah.idx, &opos[0], &opos[1]);
                            collide_impulse_at(a, ah, 0.5f * (a->e + oe), om, 0.0f, 0.0f, om <= 0.0f,
                                               dv2, opos, 4.0f, 1.0f);
                            if (om > 0.0f)
                                a2_scene_object_push(scene, ah.idx, dv2[0], dv2[1], dv2[2]);
                        } else if (ah.kind == TOI_TRAFFIC) {
                            float ovx = 0.0f, ovz = 0.0f, om = 1000.0f, dv2[3];
                            const char *oanim = NULL;
                            if (a2_traffic_car_motion(traffic, ah.idx, &ovx, &ovz, &oanim)) {
                                int hi = oanim ? a2_handling_find(&handling, oanim) : -1;
                                if (hi >= 0 && handling.v[hi].valid && handling.v[hi].mass > 0.0f)
                                    om = handling.v[hi].mass;
                            }
                            collide_impulse_at(a, ah, 0.5f * (a->e + 0.2f), om, ovx, ovz, 0, dv2,
                                               NULL, 0.0f, 0.0f);
                            a2_traffic_car_push(traffic, ah.idx, dv2[0], dv2[1]);
                        } else {
                            ai_body *b = &B[ah.idx];
                            float ovx, ovz, dv2[3], opos[2] = {b->d->x, b->d->z};
                            a2_ai_world_velocity(b->d, &ovx, &ovz);
                            collide_impulse_at(a, ah, 0.5f * (a->e + b->e), b->m, ovx, ovz, 0, dv2,
                                               opos, 2.0f * b->hlm, 2.0f * b->hwm);
                            a2_ai_push(b->d, dv2[0], dv2[1], dv2[2]);
                        }
                    }
                }
                float sl[2];
                if (a2_scene_collide_rails(scene, ach, &a->d->x, &a->d->z, a->hwu, sl) &&
                    ah.kind != TOI_RAIL)
                    collide_impulse(a->d, a->hlm, a->hwm, a->m, -sl[0], -sl[1], a->e, 0.0f, 0.0f,
                                    0.0f, 1, NULL, NULL, 0.0f, 0.0f, a->d->x, a->d->z);
                float pu[2];
                int tw = -1;
                if (traffic && a2_traffic_collide(traffic, &a->d->x, &a->d->z, a->hwu, pu, &tw) &&
                    !(ah.kind == TOI_TRAFFIC && ah.idx == tw)) {
                    float ovx = 0.0f, ovz = 0.0f, om = 1000.0f, dv2[3];
                    const char *oanim = NULL;
                    if (a2_traffic_car_motion(traffic, tw, &ovx, &ovz, &oanim)) {
                        int hi = oanim ? a2_handling_find(&handling, oanim) : -1;
                        if (hi >= 0 && handling.v[hi].valid && handling.v[hi].mass > 0.0f)
                            om = handling.v[hi].mass;
                    }
                    float n[2] = {-pu[0], -pu[1]}, cp[2];
                    a2_cbox ob;
                    int at = a2_traffic_car_box(traffic, tw, &ob) &&
                             engine_contact(a->d->x - amx, a->d->z - amz, a->d->heading, a->hwu,
                                            a->hlu, amx, amz, &ob, n, cp);
                    a2_drive_impulse_at(a->d, a->hlm, a->hwm, a->m, n[0], n[1], 0.5f * (a->e + 0.2f),
                                        om, ovx, ovz, 0, dv2, NULL, 0.0f, 0.0f, a->d->x, a->d->z,
                                        at ? cp : NULL);
                    a2_traffic_car_push(traffic, tw, dv2[0], dv2[1]);
                }
                for (int j = i + 1; j < nb; j++) {
                    ai_body *b = &B[j];
                    if (!a2_ai_capsule_push(b->d, b->hlu, b->hwu, &a->d->x, &a->d->z, a->hwu, pu))
                        continue;
                    if (ah.kind == TOI_RACER && ah.idx == j)
                        continue; /* its impulse came with the contact above */
                    float ovx, ovz, dv2[3];
                    a2_ai_world_velocity(b->d, &ovx, &ovz);
                    float opos[2] = {b->d->x, b->d->z};
                    float n[2] = {-pu[0], -pu[1]}, cp[2];
                    a2_cbox ob = {-b->hwu, -b->hlu, b->hwu, b->hlu, b->d->x, b->d->z, b->d->heading};
                    int at = engine_contact(a->d->x - amx, a->d->z - amz, a->d->heading, a->hwu,
                                            a->hlu, amx, amz, &ob, n, cp);
                    a2_drive_impulse_at(a->d, a->hlm, a->hwm, a->m, n[0], n[1], 0.5f * (a->e + b->e),
                                        b->m, ovx, ovz, 0, dv2, opos, 2.0f * b->hlm, 2.0f * b->hwm,
                                        a->d->x, a->d->z, at ? cp : NULL);
                    a2_ai_push(b->d, dv2[0], dv2[1], dv2[2]);
                }
            }
        }
        if (audio && cops) {
            /* Pursuit chatter as a chase starts (0x403584), a give-up line as
             * one ends without a catch (0x401e88); both only when no police
             * line is already playing, as the shared channel 0x4866DC is
             * checked first. */
            int voice = a2_cops_take_voice(cops);
            if (voice && !a2_audio_playing(audio, snd_voice)) {
                if (voice == 1)
                    snd_voice = a2_audio_play(audio, 64 + (int)(snd_rand() % 13u), 0);
                else if (!snd_caught)
                    snd_voice = a2_audio_play(audio, 77 + (int)(snd_rand() % 5u), 0);
            }
            if (voice == 1)
                snd_caught = 0;
        }
        if (audio) {
            /* The other vehicles, on the 20 positional slots (0x428bc0). */
            a2_sound_src src[96] = {0};
            int ns = 0;
            if (racers)
                ns += a2_racers_sound_sources(racers, src + ns, 96 - ns);
            if (traffic && show_traffic)
                ns += a2_traffic_sound_sources(traffic, src + ns, 96 - ns);
            if (cops)
                ns += a2_cops_sound_sources(cops, src + ns, 96 - ns);
            ns += a2_paths_sound_sources(paths, src + ns, 96 - ns);
            for (int i = 0; i < ns; i++)
                if (src[i].sample < 0) {
                    int hi = a2_handling_find(&handling, src[i].anim);
                    src[i].sample = hi >= 0 ? handling.v[hi].engine_sound : 31;
                }
            /* knocked props' pending samples, once each (0x428bc0) */
            a2_prop_sound ps[32];
            int np = a2_scene_prop_sounds(scene, ps, 32);
            for (int i = 0; i < np && ns < 96; i++, ns++) {
                src[ns].key = ps[i].key;
                src[ns].x = ps[i].x;
                src[ns].z = ps[i].z;
                src[ns].sample = ps[i].sample;
                src[ns].once = 1;
            }
            float lx = drive_mode ? car.x : cam.pos[0], lz = drive_mode ? car.z : cam.pos[2];
            float lyaw = (drive_mode ? car.yaw : cam.yaw) * (float)M_PI / 180.0f;
            a2_audio_objects(audio, src, ns, lx, lz, sinf(lyaw), -cosf(lyaw));
            for (int i = 0; i < np; i++)
                if (!a2_audio_object_playing(audio, ps[i].key))
                    a2_scene_prop_sound_done(scene, ps[i].key);
        } else {
            a2_prop_sound ps[32];
            int np = a2_scene_prop_sounds(scene, ps, 32);
            for (int i = 0; i < np; i++)
                a2_scene_prop_sound_done(scene, ps[i].key);
        }

        set_projection(win_w, win_h);
        /* Debug: A2_PATH_CAM=1 watches the first active path object from
         * the side and above, to check the boats, trains and the rest. */
        if (getenv("A2_PATH_CAM")) {
            float pp[3], pyaw;
            if (a2_paths_first(paths, pp, &pyaw)) {
                float r = (pyaw + 90.0f) * (float)M_PI / 180.0f;
                float dist = getenv("A2_PATH_CAM_DIST") ? (float)atof(getenv("A2_PATH_CAM_DIST")) : 900.0f;
                cam.pos[0] = pp[0] + sinf(r) * dist;
                cam.pos[1] = pp[1] + dist * 0.28f;
                cam.pos[2] = pp[2] + cosf(r) * dist;
                float dx = pp[0] - cam.pos[0], dz = pp[2] - cam.pos[2];
                cam.yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
                cam.pitch = -15.0f;
            }
        }
        /* Debug: A2_CAR_CAM=<deg> watches the player's car from that angle off
         * its nose (90 = its side), A2_CAR_CAM_DIST units away, low down, to
         * check the wheels. */
        if (getenv("A2_CAR_CAM") && drive_mode) {
            float r = (car.yaw + 180.0f + (float)atof(getenv("A2_CAR_CAM"))) * (float)M_PI / 180.0f;
            float dist = getenv("A2_CAR_CAM_DIST") ? (float)atof(getenv("A2_CAR_CAM_DIST")) : 45.0f;
            cam.pos[0] = car.x + sinf(r) * dist;
            cam.pos[1] = car.y + 6.0f;
            cam.pos[2] = car.z - cosf(r) * dist;
            float dx = car.x - cam.pos[0], dz = car.z - cam.pos[2];
            cam.yaw = atan2f(dx, -dz) * 180.0f / (float)M_PI;
            cam.pitch = -5.0f;
        }
        apply_view(&cam);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        /* The backdrop goes down first and writes no depth, so the scene draws
         * straight over it -- the original blits it the same way. */
        /* The backdrop is a 2D band across the top of the screen and knows
         * nothing about pitch, so it is wrong once the camera looks steeply
         * down -- as the topview does.  Skip it there. */
        if (sky && show_sky && cam.pitch > -45.0f)
            a2_sky_draw(sky, cam.yaw, win_w, win_h);
        /* Drives the traffic lights and roadworks arrows. */
        /* Animation time has to come off the same fixed step as the physics in
         * a headless run, or the animated props -- the traffic lights among
         * them -- run on wall-clock phase and the whole run stops being
         * reproducible.  This was gated on `drive_test` alone, which is why two
         * `--autopilot` runs of etappe5 that differed in nothing but a log
         * string came in 47 s apart with 9 fewer traffic lane changes. */
        a2_scene_set_anim_time(scene, (drive_test || autopilot) ? frames / 30.0f
                                                                : SDL_GetTicks() / 1000.0f);
        int near_chunk = drive_mode ? a2_scene_nearest_chunk(scene, car.x, car.z)
                                    : a2_scene_nearest_chunk(scene, cam.pos[0], cam.pos[2]);
        a2_scene_draw(scene, near_chunk, radius, show_objects);
        if (show_rails)
            a2_scene_draw_rails(scene, near_chunk, radius);
        a2_skid_draw(skid);
        if (traffic && show_traffic)
            a2_traffic_draw(traffic, scene, near_chunk, radius);
        if (racers)
            a2_racers_draw(racers, scene, near_chunk, radius);
        if (cops)
            a2_cops_draw(cops, scene, near_chunk, radius);
        a2_paths_draw(paths, scene);
        a2_smoke_draw(smoke);
        if (drive_mode) {
            /* Car meshes are authored nose-toward +Z, and the model rotation
             * takes the opposite sign from the view matrix, so heading `yaw`
             * becomes a rotation of 180 - yaw. */
            if (!cam_preset[cam_view].hide_car || cam_target < 3) {
                a2_scene_draw_shadow(scene, car_model, car.x, car.z, near_chunk, 180.0f - car.yaw);
                /* the body, then its wheels in the body's frame (sub-objects) */
                glPushMatrix();
                glTranslatef(car.x, car.y + car.ride, car.z);
                glRotatef(180.0f - car.yaw, 0.0f, 1.0f, 0.0f);
                glRotatef(car.pitch_ang, 1.0f, 0.0f, 0.0f);
                glRotatef(car.roll, 0.0f, 0.0f, 1.0f);
                a2_scene_draw_model_rpy(scene, car_model, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
                a2_wheels_draw(&wheels, scene, wheel_model, wheel_type);
                if (brake_lit == 1 && brake_light_model >= 0)
                    draw_brake_lights(scene, brake_light_model, car_slot);
                glPopMatrix();
            } /* not in the cockpit view, where the car is hidden (+0x2c = 0) */
        }
        if (hud && show_hud && overlay_detail > 0 && drive_mode) {
            /* RC.IMG also holds a speedometer dial (sprite 1, marked 20..250)
             * with 19 baked needle frames at 32..50, and this HUD used to draw
             * it bottom-right.  It is genuine art but it does not appear in the
             * game's own HUD, so it is left unused rather than invented into
             * the layout. */
            a2_sprites_begin(win_w, win_h);

            if (font || fnt_label) {
                /* The layout follows the original's (hud.png), and every number
                 * below is a measurement off it divided by that capture's 1.21
                 * scale, so they are the HUD art's own pixels:
                 *
                 *     [bracket] RACE TIJD           [bracket] SCHADE  <bar> n%
                 *               02:06:0             [bracket] TURBO   <bar>
                 *     [bracket] RONDE INFO          [bracket] SNELHEID
                 *       TIJD    02:06:0                       233 km/h
                 *       RECORD  02:52:8
                 *       LAATSTE --:--:-
                 *     [bracket] POSITIE   2/4
                 *     [bracket] RONDE     1/2
                 *
                 * and the radar bottom-left, which the original builds procedurally from the
                 * road rather than from a sprite (`create_radar`,
                 * `draw_road_in_radar_surface`, gfx_radr.cpp) -- see hud_radar.
                 *
                 * The hires art is authored for 800x600 and drawn 1:1 there, so
                 * `s` is one of its pixels.  That it really is 1:1 shows in the
                 * labels: at the capture's scale olfnt01 puts SNELHEID at 90.1
                 * pixels against the font's own 90, POSITIE at 74.4 against 76
                 * and RONDE at 58.7 against 60 -- no tracking, no scaling.
                 *
                 * Labels are language.ini strings 33..39 and 52..55 in olfnt01;
                 * the numbers are olfnt02/03/04, whose glyph sets say what each
                 * is for.  The white brackets and the two bars are RC.IMG art:
                 * sprite 425 holds the top-right pieces and 426 the bar frames,
                 * the speed bracket and the "km/h" mark. */
                char line[64], val[64];
                float s = win_h / 600.0f;
                float right = win_w - 16.0f * s;
                float rval = right - 8.0f * s; /* the numbers stop short of the labels */
                /* The viewport the measurements come from is 607 of the art's
                 * pixels tall, so the bottom group hangs off that. */
                float bot = win_h;
                float lsc = s;
                float lh = fnt_label ? a2_font_height(fnt_label, lsc) : 17.0f * s;

/* Labels and values are both right-aligned to the same edge; the label macro
 * yields its width so the bracket beside it knows where to stop. */
#define HUD_LABEL(txt, yy)                                                                         \
    (fnt_label ? (a2_font_draw(fnt_label, (txt),                                                   \
                               right - a2_font_text_width(fnt_label, (txt), lsc), (yy), lsc),      \
                  a2_font_text_width(fnt_label, (txt), lsc))                                       \
               : (a2_font3d_draw(font, scene, (txt),                                               \
                                 right - a2_font3d_width(font, (txt)) * lh, (yy), lh),             \
                  a2_font3d_width(font, (txt)) * lh))
#define HUD_VALUE(fnt, txt, yy)                                                                    \
    do {                                                                                           \
        if (fnt)                                                                                   \
            a2_font_draw((fnt), (txt), rval - a2_font_text_width((fnt), (txt), lsc), (yy), lsc);   \
        else                                                                                       \
            a2_font3d_draw(font, scene, (txt),                                                     \
                           rval - a2_font3d_width(font, (txt)) * lh, (yy), lh);                    \
    } while (0)
/* A bracket `w` wide and `h` tall whose right edge is at `xr`. */
#define HUD_BRACKET(idx, sx, sy, sw, sh, xr, yy, w, h)                                             \
    hud_bracket(hud, (idx), (sx), (sy), (sw), (sh), (xr) - (w) * s, (yy), (w) * s, (h) * s, s)

                /* --- top right: race time, lap info, position, lap --- */
                HUD_LABEL(a2_lang_get(lang, 33, "RACE TIJD"), 6.0f * s);
                HUD_BRACKET(425, 0, 22, 27, 7, right - 116.0f * s, 7.0f * s, 68.0f, 23.0f);
                int rm = (int)(race_time / 60.0f);
                snprintf(line, sizeof line, "%02d:%02d:%d", rm, ((int)race_time) % 60,
                         (int)((race_time - (int)race_time) * 10.0f));
                HUD_VALUE(fnt_time, line, 31.0f * s);

                if (overlay_detail >= 3 && circuit) {
                    HUD_LABEL(a2_lang_get(lang, 34, "RONDE INFO"), 73.0f * s);
                    HUD_BRACKET(425, 0, 22, 27, 7, right - 131.0f * s, 73.0f * s, 79.0f, 25.0f);
                    /* TIJD / RECORD / LAATSTE: the label sits left of the
                     * value, which stays flush with the right-hand edge. */
                    static const int row_id[3] = { 35, 36, 37 };
                    const char *row_fb[3] = { "TIJD", "RECORD", "LAATSTE" };
                    float row_t[3] = { race_time - lap_start, best_lap, last_lap };
                    float rowy[3] = { 97.0f * s, 120.0f * s, 142.0f * s };
                    for (int r = 0; r < 3; r++) {
                        if (row_t[r] > 0.0f)
                            snprintf(val, sizeof val, "%02d:%02d:%d", (int)(row_t[r] / 60.0f),
                                     ((int)row_t[r]) % 60,
                                     (int)((row_t[r] - (int)row_t[r]) * 10.0f));
                        else
                            snprintf(val, sizeof val, "--:--:-");
                        HUD_VALUE(fnt_value, val, rowy[r]);
                        if (fnt_label)
                            a2_font_draw(fnt_label, a2_lang_get(lang, row_id[r], row_fb[r]),
                                         right - 182.0f * s, rowy[r], lsc);
                    }
                    /* The box closes with a long rule under the last row, and a
                     * plain vertical joins it to the bracket above -- the stub
                     * at the foot of the one and the one at the head of the
                     * other both point into it. */
                    hud_bracket(hud, 425, 0, 51, 78, 7, right - 197.0f * s, 147.0f * s, 190.0f * s,
                                20.0f * s, s);
                    a2_sprite_draw_sub(hud, 425, 0, 26, 4, 1, right - 197.0f * s, 95.0f * s,
                                       3.0f * s, 56.0f * s);
                }

                /* The highway panel (0x41a5f0): the literal word CHECKPOINT --
                 * not a language.ini string -- over timer 11 as mm:ss:t, at the
                 * top centre of the screen, built like RACE TIJD (screenshot of
                 * the original on etappe5: "CHECKPOINT 00:42:4" at 0.5 s into
                 * the race).  Not gated on overlay_detail.  Drawn here as the
                 * RACE TIJD block moved so that its middle sits mid-screen. */
                if (timed) {
                    float save_right = right, save_rval = rval;
                    float shift = (float)win_w * 0.5f - (right - 92.0f * s);
                    right += shift;
                    rval += shift;
                    HUD_LABEL("CHECKPOINT", 6.0f * s);
                    HUD_BRACKET(425, 0, 22, 27, 7, right - 116.0f * s, 7.0f * s, 68.0f, 23.0f);
                    snprintf(line, sizeof line, "%02d:%02d:%d", (int)(time_left / 60.0f),
                             ((int)time_left) % 60,
                             (int)((time_left - (int)time_left) * 10.0f));
                    HUD_VALUE(fnt_time, line, 31.0f * s);
                    right = save_right;
                    rval = save_rval;
                }

                if (racers) {
                    HUD_LABEL(a2_lang_get(lang, 38, "POSITIE"), 178.0f * s);
                    HUD_BRACKET(425, 37, 91, 17, 11, right - 87.0f * s, 193.0f * s, 26.0f, 28.0f);
                    snprintf(line, sizeof line, "%d/%d", PLAYER_POSITION(),
                             a2_racers_count(racers) + 1);
                    HUD_VALUE(fnt_time, line, 204.0f * s);
                }
                if (circuit) {
                    HUD_LABEL(a2_lang_get(lang, 39, "RONDE"), 238.0f * s);
                    HUD_BRACKET(425, 37, 91, 17, 11, right - 71.0f * s, 252.0f * s, 29.0f, 30.0f);
                    snprintf(line, sizeof line, "%d/%d", lap, nr_laps);
                    HUD_VALUE(fnt_time, line, 264.0f * s);
                }

                /* --- bottom right: damage, turbo, speed.  Every offset is
                 * measured up from the bottom of the original's screen. --- */
                float kmh = car.speed < 0.0f ? -car.speed * 3.6f : car.speed * 3.6f;
                snprintf(line, sizeof line, "%d", (int)(kmh + 0.5f));
                /* The digits stop 44 pixels short of the edge: that is the room
                 * the "km/h" mark beside them needs. */
                if (fnt_speed)
                    a2_font_draw(fnt_speed, line,
                                 right - 44.0f * s - a2_font_text_width(fnt_speed, line, lsc),
                                 bot - 52.0f * s, lsc);
                else
                    HUD_VALUE(fnt_speed, line, bot - 52.0f * s);
                a2_sprite_draw_sub(hud, 426, 63, 53, 16, 6, right - 37.0f * s, bot - 28.0f * s,
                                   37.0f * s, 18.0f * s);
                HUD_BRACKET(426, 0, 37, 34, 12, right - 96.0f * s, bot - 92.0f * s, 88.0f, 45.0f);

                if (overlay_detail >= 3) {
                    /* Both bars are an 11-frame 91x8 ladder in RC.IMG, in 10%
                     * steps, and in neither case is the step order the index
                     * order -- these were read off by measuring how far the lit
                     * colour reaches in each frame.  The empty frames sit well
                     * away from their runs: 431 is the all-dark damage bar and
                     * 14 the all-dark chevron strip.  (122/125 and 134..144 are
                     * the same two ladders at 49x3, for 320x240.) */
                    static const int solid[11] = { 431, 129, 124, 126, 127, 128,
                                                   123, 130, 131, 132, 133 };
                    /* The turbo bar is sprite anim 21 (0x4961E8; HUD element 4's
                     * hires art), the plain orange chevrons, not the gradient
                     * set 11..21: matched against Wine captures with gamma
                     * applied (session 15).  Its full frame is the gradient
                     * strip 13, shown only at exactly 100%. */
                    static const int chevron[11] = { 449, 433, 439, 445, 446, 447,
                                                     448, 432, 450, 451, 13 };
                    /* The frame stretches to the element's size but the fill
                     * strip keeps its own 91x8 proportions, which is why the
                     * fill band looks taller than the frame's placeholder. */
                    float bar_w = 123.0f * s, bar_h = 23.0f * s;
                    float fill_w = bar_w * (49.0f / 55.0f);
                    float fill_h = fill_w * (8.0f / 91.0f);
                    static const float bar_y[2] = { 119.0f, 168.0f };  /* turbo, damage */
                    static const float lab_y[2] = { 142.0f, 190.0f };
                    for (int b = 0; b < 2; b++) {
                        /* TURBO sits above SNELHEID and SCHADE above that. */
                        int is_turbo = (b == 0);
                        const char *label = is_turbo ? a2_lang_get(lang, 53, "TURBO")
                                                     : a2_lang_get(lang, 55, "SCHADE");
                        float frac = is_turbo ? turbo_left : damage / 100.0f;
                        if (frac < 0.0f)
                            frac = 0.0f;
                        if (frac > 1.0f)
                            frac = 1.0f;
                        /* frame = level * (frames - 1) / 100, truncated
                         * (0x41aad1, _ftol 0x4700e0) */
                        int step = (int)(frac * 10.0f);
                        float bx = right - 2.0f * s - bar_w, by = bot - bar_y[b] * s;
                        hud_bracket(hud, 426, 25, is_turbo ? 19 : 0, 55, 13, bx, by, bar_w, bar_h,
                                    s);
                        /* 431, the all-dark bar, backs both: the frame carries a
                         * placeholder fill of its own and the chevrons are
                         * transparent between the arrows. */
                        float fx = bx + bar_w * (6.0f / 55.0f), fy = by + (bar_h - fill_h) * 0.5f;
                        a2_sprite_draw_sub(hud, 431, 0, 0, 91, 8, fx, fy, fill_w, fill_h);
                        a2_sprite_draw_sub(hud, is_turbo ? chevron[step] : solid[step], 0, 0, 91, 8,
                                           fx, fy, fill_w, fill_h);
                        if (!is_turbo && fnt_pct) {
                            /* The damage percentage is centred in its bar. */
                            snprintf(val, sizeof val, "%d%%", (int)(frac * 100.0f + 0.5f));
                            a2_font_draw(fnt_pct, val,
                                         bx + (bar_w - a2_font_text_width(fnt_pct, val, s)) * 0.5f,
                                         by + bar_h * 0.5f - a2_font_height(fnt_pct, s) * 0.5f, s);
                        }
                        HUD_LABEL(label, bot - lab_y[b] * s);
                    }
                }
                /* Last, so the turbo bar's bottom rail passes behind it. */
                HUD_LABEL(a2_lang_get(lang, 54, "SNELHEID"), bot - 93.0f * s);

                /* --- bottom left: the radar --- */
                hud_radar(scene, traffic, racers, cops, 72.0f * s, bot - 72.0f * s, 56.0f * s,
                          car.x, car.z, car.yaw);

                float small = 14.0f * s; /* 3D font, the centre-screen banners */
#undef HUD_BRACKET
#undef HUD_VALUE
#undef HUD_LABEL

                /* Speed-camera flash, then the fine notice.
                 *
                 * Two things here are **not** the original's, now that its own
                 * camera path has been read (discoveries.md, "Speed cameras,
                 * posted signs and the radar bonus"):
                 *  - the original compares against a **posted** limit of
                 *    50/70/100/120 held in a global by `txt_vbrd.cpp`, with no
                 *    tolerance -- a different set from the 60/80/120 and the
                 *    10% the police use, and from the limit signs this reads;
                 *  - it shows **no text at all**: it plays sound sample 11 and
                 *    runs a palette fade to white, nothing more.
                 * There is in fact no fine system in the shipped game. The one
                 * text getter is reached either with a constant index or via
                 * the banner slot, and GEFLITST, POLITIE and all three BOETE
                 * strings appear in neither set -- they are vestigial, as are
                 * the fuel, breakdown and budget lines, and menu.dat has no
                 * language subsystem to use them either. An earlier version of
                 * this drew GEFLITST plus a fine of its own invention; that is
                 * gone, and only the flash remains.
                 * The earlier note here that the original "awards a radar bonus
                 * when under the limit" was also wrong: the bonus is a
                 * **collision** reward for knocking a camera over. */
                if (race_time - flash_at < 2.5f) {
                    float age = race_time - flash_at;
                    if (age < 0.15f) {
                        glDisable(GL_TEXTURE_2D);
                        glColor4f(1.0f, 1.0f, 1.0f, 1.0f - age / 0.15f);
                        glBegin(GL_QUADS);
                        glVertex2f(0.0f, 0.0f);
                        glVertex2f((float)win_w, 0.0f);
                        glVertex2f((float)win_w, (float)win_h);
                        glVertex2f(0.0f, (float)win_h);
                        glEnd();
                        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
                        glEnable(GL_TEXTURE_2D);
                    }
                }

                /* the banner slot (see banner_id) */
                if (banner_id && fnt_label) {
                    Uint32 now = SDL_GetTicks();
                    if (now - banner_ms <= 2000 && ((now / 100) & 1)) {
                        static const char *const fb[52] = {
                            [31] = "NIEUW RONDE RECORD", [44] = "EXTRA TIJD",
                            [45] = "LAATSTE RONDE",      [46] = "INVINCIBLE",
                            [47] = "COP MODE",           [48] = "QUICK SERVICE",
                            [49] = "EERSTE WAARSCHUWING", [50] = "LAATSTE WAARSCHUWING",
                            [51] = "RADAR BONUS"};
                        const char *txt = a2_lang_get(lang, banner_id, fb[banner_id]);
                        float sc = win_h / 480.0f;
                        a2_font_draw(fnt_label, txt,
                                     win_w * 0.5f - a2_font_text_width(fnt_label, txt, sc) * 0.5f,
                                     80.0f * sc - a2_font_height(fnt_label, sc) * 0.5f, sc);
                    }
                }

                /* the two game-over arms the police code does not already
                 * announce: 40 TOTAL LOSS and 41 TIJD IS OM */
                if (game_over_msg == 40 || game_over_msg == 41) {
                    int id = game_over_msg;
                    const char *txt = a2_lang_get(lang, id, id == 40 ? "TOTAL LOSS" : "TIJD IS OM");
                    float bs = small * 1.8f;
                    a2_font3d_draw(font, scene, txt,
                                   (win_w - a2_font3d_width(font, txt) * bs) * 0.5f,
                                   win_h * 0.40f, bs);
                }

                /* The police: the two warnings go through the banner slot
                 * (above); the arrest's 42 GEARRESTEERD and 43 RACE OVER are
                 * the game-over sequence's own (see cops.h). */
                if (cops) {
                    float age = 0.0f;
                    int msg = a2_cops_message(cops, &age);
                    if ((msg == 42 || msg == 43) && age < 4.0f) {
                        const char *fb = msg == 49   ? "EERSTE WAARSCHUWING"
                                         : msg == 50 ? "LAATSTE WAARSCHUWING"
                                         : msg == 43 ? "RACE OVER"
                                                     : "GEARRESTEERD";
                        const char *txt = a2_lang_get(lang, msg, fb);
                        float bs = small * 1.6f;
                        a2_font3d_draw(font, scene, txt,
                                       (win_w - a2_font3d_width(font, txt) * bs) * 0.5f,
                                       win_h * 0.22f, bs);
                    }
                }

                /* Finish banner: the game's own GEFINISHT and total time. */
                if (finished) {
                    int fm = (int)(finish_time / 60.0f);
                    snprintf(line, sizeof line, "%s   %02d:%02d",
                             a2_lang_get(lang, 17, "GEFINISHT"), fm, ((int)finish_time) % 60);
                    float bs = small * 1.8f;
                    a2_font3d_draw(font, scene, line,
                                   (win_w - a2_font3d_width(font, line) * bs) * 0.5f,
                                   win_h * 0.40f, bs);
                }

            }
            /* the tweak menu's text, as 0x466420 prints it: x = 10, a row
             * every 20 from y = 10, on the 640x480 screen */
            /* (in slider13, the HUD's one full-ASCII font; which set
             * 0x466420 prints with is not traced) */
            if (tweak.open && fnt_pct && have_cars) {
                float sx = win_w / 640.0f, sy = win_h / 480.0f;
                a2_tweak_text(&tweak, &cars, car_slot, up_gearbox);
                for (int r = 0; r < tweak.line_count; r++)
                    a2_font_draw(fnt_pct, tweak.lines[r], 10.0f * sx, (10.0f + 20.0f * r) * sy, sy);
            }
            /* L, tweak mode (0x41aa15, in draw_dashboard): the player's
             * chunk ([0x4d9e44]) at (0, 100), in font 11 (olfnt02) */
            if (tweak_mode && fnt_time && drive_mode && SDL_GetKeyboardState(NULL)[SDL_SCANCODE_L]) {
                char line[16];
                snprintf(line, sizeof line, "%d", a2_scene_nearest_chunk(scene, car.x, car.z));
                a2_font_draw(fnt_time, line, 0.0f, 100.0f * (win_h / 480.0f), win_h / 480.0f);
            }
            /* F12, tweak mode (0x42524a): the frame rate and the visible
             * chunks, at (10, 80) and (10, 100) of the 640x480 screen */
            if (tweak_mode && fnt_pct && SDL_GetKeyboardState(NULL)[SDL_SCANCODE_F12]) {
                char line[64];
                float sx = win_w / 640.0f, sy = win_h / 480.0f;
                snprintf(line, sizeof line, "frameRate: %f", dt > 0.0f ? 1.0f / dt : 0.0f);
                a2_font_draw(fnt_pct, line, 10.0f * sx, 80.0f * sy, sy);
                snprintf(line, sizeof line, "vchunks: %d", radius);
                a2_font_draw(fnt_pct, line, 10.0f * sx, 100.0f * sy, sy);
            }
            a2_sprites_end();
        }
        SDL_GL_SwapWindow(win);

        frames++;
        /* A headless test run ends itself a few seconds after the race does --
         * by finishing or by any of the game-over arms -- instead of sitting
         * on the result until the window is closed or the frame cap hits. */
        if (drive_test && (finished || arrested) && ++over_frames > 150) {
            fprintf(stderr, "drive-test: race over, exiting\n");
            running = 0;
        }
        float dbg_road = 0.0f;
        if ((drive_test || autopilot) && (frames % 40) == 0)
            fprintf(stderr,
                    "t=%4.1fs speed=%5.1f km/h gear=%d chunk=%4d pos=(%.0f, %.0f) "
                    "roll=%5.2f pitch=%5.2f skid=%d y=%.2f road=%.2f\n",
                    frames / 30.0f, car.speed * 3.6f, car.gear + 1,
                    a2_scene_nearest_chunk(scene, car.x, car.z), car.x, car.z, car.roll,
                    car.pitch_ang, drv.skid, drv.y,
                    (a2_scene_road_height(scene, a2_scene_nearest_chunk(scene, car.x, car.z), car.x,
                                          car.z, &dbg_road) ? dbg_road : -999.0f));
        if (max_frames && frames >= max_frames) {
            if (shot)
                write_ppm(shot, win_w, win_h);
            running = 0;
        }
    }

    /* deinit_game (0x421af0) saves before tearing anything down. */
    if (saving) {
        a2_racer_result rr[3];
        int nr = racers ? a2_racers_count(racers) : 0;
        if (nr > 3)
            nr = 3;
        int fin = finished && !arrested;
        if (fin) {
            /* 0x4114a0: a racer still running gets its time so far stretched
             * by the distance it has left, at its own average pace. */
            float total = finish_progress - (float)start_chunk;
            int now_ms = (int)(race_time * 1000.0f);
            for (int i = 0; i < nr; i++) {
                rr[i].car = a2_racer_car(racers, i);
                if (a2_racer_finished(racers, i)) {
                    rr[i].time_ms = (int)(a2_racer_finish_time(racers, i) * 1000.0f);
                } else {
                    int gap = (int)(player_progress - a2_racer_progress(racers, i));
                    int covered = (int)total - gap;
                    rr[i].time_ms = covered > 0 ? now_ms + now_ms / covered * gap : now_ms;
                }
            }
            a2_save_rank_racers(rr, nr, save.position);
        }
        save.damage = damage;
        a2_save_write(datadir, &save, fin, track_ms, rr, nr);
        fprintf(stderr, "Game.ini: %s\n", fin ? "results saved" : "finished=0");
    }

    a2_font_free(fnt_value);
    a2_font_free(fnt_time);
    a2_font_free(fnt_speed);
    a2_font_free(fnt_pct);
    a2_font_free(fnt_label);
    a2_lang_free(lang);
    a2_font3d_free(font);
    a2_sprites_free(hud);
    a2_cops_free(cops);
    a2_racers_free(racers);
    a2_aiw_free(aiw);
    a2_paths_free(paths);
    a2_traffic_free(traffic);
    a2_scene_destroy(scene);
    a2_anims_free(anims);
    a2_archive_close(&rcs);
    a2_track_free(&track);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    a2_audio_close(audio);
    SDL_Quit();
    return 0;
}

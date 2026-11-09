// SSX 3 GizWipe - DS port (stage 4: rider physics from the Gizmondo executable)
//
// Top screen: streamed SSX 3 track + rider driven by the ported physics (physics.c).
// Bottom screen: rider state and render stats. Controls: see README.md / the bottom screen.

#include <math.h>
#include "xtext.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <nds.h>
#include <filesystem.h>
#include "track.h"
#include "rider.h"
#include "music.h"
#include "physics.h"
#include "bot.h"
#include "anim_ids.h"
#include "sfx.h"
#include "submap.h"
#include "ghost.h"
#include "hud.h"
#include "frontend.h"
#include "rumble.h"
#include "net.h"
#include "lang.h"
#include "sky.h"
#include <fat.h>
#include <stdlib.h>
#include <unistd.h>
#include "fireworks.h"
#include "voice.h"
#include "board.h"
#include "career.h"

// exe sound ids used by the race flow (table at 0xcc338)
enum { SND_COUNT = 7, SND_START = 12, SND_FLARE = 14, SND_FIREWORK = 15, SND_COLLIDE = 19, SND_KNOCKDOWN = 13, SND_CROWD = 18, SND_PUNCH = 28 };
enum { R_COUNT, R_RACE, R_DONE, R_INTRO };

static void fmt_time(char *o, int ms)
{
    if (ms <= 0) { strcpy(o, "-:--.--"); return; }
    sprintf(o, "%d:%02d.%02d", ms / 60000, ms / 1000 % 60, ms % 1000 / 10);
}

static u32 hash32(u32 x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }

// trees (0x62ca8: xz scale 1 - rand(0x3ff)/0x4000, height 0.5 + rand(0xfff)/0x4000, random yaw) and pickups,
// nearest first within the polygon budget left after the track
static Mesh m_pick[6], m_tree;
static int tex_imp, imp_w, imp_h; static s16 imp_ext[4];        // far trees: x0, x1, y0, y1 of the picture, mesh units
#define IMP_D 3000                                               // trees beyond this are their impostor
static int tree_reach = 6000, fog_end_units = 5750;                                    // game units: the fog's end, a little past
static int tex_glare, tex_spark, tex_fog, tex_shadow, tex_trail;
static Fireworks fw;

// A3I5 white texture, alpha from the source's brightness (tools/ds_menu.py fx)
static int load_a3(const char *path, bool ramp, bool wrap_t)
{
    int tex = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char m[4]; u16 wh[2]; fread(m, 4, 1, f); fread(wh, 2, 2, f);
    u8 *tx = malloc(wh[0] * wh[1]); fread(tx, 1, wh[0] * wh[1], f); fclose(f);
    static u16 pal[32]; pal[0] = RGB15(31, 31, 31);
    for (int i = ramp ? 0 : 1; i < 32; i++) pal[i] = ramp ? RGB15(i, i, i) : 0;
    glGenTextures(1, &tex); glBindTexture(0, tex);
    // (wrapping in T: the trail runs one strip with its V counting on, 16 texels a section - the same texels as
    // the separate 0..16 quads it was drawn with)
    glTexImage2D(0, 0, GL_RGB32_A3, wh[0], wh[1], 0, TEXGEN_TEXCOORD | (wrap_t ? GL_TEXTURE_WRAP_T : 0), tx);
    glColorTableEXT(0, 0, 32, 0, 0, pal);
    free(tx);
    return tex;
}
// glare_* markers: translucent light flares (glare.bmp), fireworks sparks (fireworks.png)
static void load_glare(void)
{
    tex_glare = load_a3("nitro:/fx/glare.ssxa", false, false);
    tex_spark = load_a3("nitro:/fx/firework.ssxa", false, false);
    tex_fog = load_a3("nitro:/fx/volfog.ssxa", true, false);
    tex_shadow = load_a3("nitro:/fx/boardshadow.ssxa", false, false);
    tex_trail = load_a3("nitro:/fx/boardtrail.ssxa", false, true);
    FILE *f = fopen("nitro:/fx/tree_imp.ssxi", "rb");          // the far trees' impostor (tools/ds_impostor.py)
    if (f) {
        char m[4]; u16 wh[2]; u16 *pal = malloc(512);
        fread(m, 4, 1, f); fread(wh, 2, 2, f); fread(imp_ext, 2, 4, f); fread(pal, 2, 256, f);
        u8 *tx = malloc(wh[0] * wh[1]); fread(tx, 1, wh[0] * wh[1], f); fclose(f);
        glGenTextures(1, &tex_imp); glBindTexture(0, tex_imp);
        glTexImage2D(0, 0, GL_RGB256, wh[0], wh[1], 0, TEXGEN_TEXCOORD | GL_TEXTURE_COLOR0_TRANSPARENT, tx);
        glColorTableEXT(0, 0, 256, 0, 0, pal);
        imp_w = wh[0]; imp_h = wh[1];
        free(tx); free(pal);
    }
}

// port: a camera-facing quad (centre c, corners c +- X +- Y, game units; the matrix translated to c) clipped on the
// CPU to 24 units in front of the camera. One the hardware clips at the near plane gets a depth just under zero at
// the cut, which the DS wraps to the far end of the depth range: the polygon came out fully fogged (and fog makes it
// opaque) - the white wedges over the slope near the rider on hardware, from R&B's volume fog patches pulled in front
// of the camera and the light flares. (melonDS clamps that depth to zero, so they never showed there.)
static float view_f[3] = { 0, 0, 1 };
ITCM_CODE static void quad_front(const float c[3], const float cam[3], const float X[3], const float Y[3], int tw, int th)
{
    static const s8 S[4][2] = { { -1, 1 }, { -1, -1 }, { 1, -1 }, { 1, 1 } };
    float in[4][6]; int nin = 0, front = 0;                     // x y z (relative to c), u, v, depth margin
    float cd = (c[0] - cam[0]) * view_f[0] + (c[1] - cam[1]) * view_f[1] + (c[2] - cam[2]) * view_f[2];
    for (int k = 0; k < 4; k++) {
        float *q = in[nin++];
        for (int j = 0; j < 3; j++) q[j] = X[j] * S[k][0] + Y[j] * S[k][1];
        q[3] = S[k][0] < 0 ? 0 : tw; q[4] = S[k][1] > 0 ? 0 : th;
        q[5] = cd + q[0] * view_f[0] + q[1] * view_f[1] + q[2] * view_f[2] - 24;
        if (q[5] >= 0) front++;
    }
    if (!front) return;
    float out[8][6]; int n = 0;
    if (front == 4) { for (int k = 0; k < 4; k++) memcpy(out[k], in[k], sizeof(in[k])); n = 4; }
    else for (int k = 0; k < 4; k++) {                          // Sutherland-Hodgman against depth margin >= 0
        const float *a = in[k], *b = in[(k + 1) & 3];
        if (a[5] >= 0) memcpy(out[n++], a, sizeof(in[k]));
        if ((a[5] >= 0) != (b[5] >= 0)) {
            float t = a[5] / (a[5] - b[5]);
            for (int j = 0; j < 6; j++) out[n][j] = a[j] + (b[j] - a[j]) * t;
            out[n][5] = 0; n++;
        }
    }
    glBegin(GL_TRIANGLES);
    for (int k = 1; k + 1 < n; k++) {
        const int id[3] = { 0, k, k + 1 };
        for (int e = 0; e < 3; e++) {
            const float *q = out[id[e]];
            glTexCoord2t16(floattot16(q[3]), floattot16(q[4]));
            glVertex3v16(floattov16(q[0] / 256), floattov16(q[1] / 256), floattov16(q[2] / 256));
        }
    }
    glEnd();
}

// volume fog patches (0x74e0c, 'fog' markers on R&B and Gravitude): camera-facing 1536-unit squares of volfog.bmp,
// 384 units toward the camera, alpha blended with an alpha that grows with the xz distance (sqrt / 4 - 100, 0..255):
// clear when you ride into one, a white haze from ~1400 units on
ITCM_CODE static int draw_fog(const Track *t, const float cam[3], float fx, float fz)
{
    if (!tex_fog || !t->nfogs) return 0;
    glBindTexture(0, tex_fog);
    glColor(RGB15(31, 31, 31));
    float rx = fz, rz = -fx;
    int n = 0;
    for (int i = 0; i < t->nfogs; i++) {
        const s32 *p = t->fogs[i];
        float dx = p[0] - cam[0], dy = p[1] - cam[1], dz = p[2] - cam[2];
        float dh = sqrtf(dx * dx + dz * dz);
        if (dx * fx + dz * fz < -800 || dh > 14000) continue;
        int a = (int)(dh / 4) - 100; if (a <= 0) continue; if (a > 255) a = 255;
        a >>= 3; if (a < 1) continue;
        float tl = sqrtf(dx * dx + dy * dy + dz * dz); if (tl < 1) tl = 1;
        float pull = -384 / tl;
        glPolyFmt(POLY_ALPHA(a) | POLY_CULL_NONE | POLY_FOG | POLY_ID(12 + (i & 3)));
        glPushMatrix();
        glTranslatef32((int)((p[0] + dx * pull) * 16), (int)((p[1] + dy * pull) * 16), (int)((p[2] + dz * pull) * 16));
        const float s = 768;
        float c[3] = { p[0] + dx * pull, p[1] + dy * pull, p[2] + dz * pull }, X[3] = { rx * s, 0, rz * s }, Y[3] = { 0, s, 0 };
        quad_front(c, cam, X, Y, 64, 64);
        glPopMatrix(1);
        n++;
    }
    return n;
}

// the board's shadow (0x644d4 'shadowtex', placed by 0x4cfe0): a black 24 x 90 quad of boardshadow.bmp on the ground
// under the board, laid on the ground's normal and turned with the board. It fades as the board rises (alpha
// 0x60 - (height - 24) / 6 of 255) and shrinks ((0x818 - height) x 8, at least 0x800, of 0x4000).
ITCM_CODE static int draw_shadow(Track *t, const Phys *p, const float pos[3])     // fixed point (no FPU on the ARM9)
{
    if (!tex_shadow || p->rail_state) return 0;          // grinding: the ray finds the rail, and the shadow lay on it
    s32 P[3] = { (s32)pos[0], (s32)pos[1], (s32)pos[2] };
    s32 h[3], n[3];
    // the ray down costs ~6 scanlines a rider: done every other frame per rider, the frame between keeps the last
    // hit's height offset under the rider and its normal (a frame's travel along the ground changes neither much)
    static struct { const Phys *p; s32 y, n[3]; u8 ok, age; } cache[4];
    int ci = 0; while (ci < 3 && cache[ci].p && cache[ci].p != p) ci++;
    if (cache[ci].p != p) { cache[ci].p = p; cache[ci].ok = 0; cache[ci].age = 9; }
    if (cache[ci].ok && cache[ci].age < 1) {
        cache[ci].age++;
        h[0] = P[0]; h[1] = cache[ci].y; h[2] = P[2]; memcpy(n, cache[ci].n, 12);
    } else {
        s32 a[3] = { P[0], P[1] + 40, P[2] }, b[3] = { P[0], P[1] - 1500, P[2] }; int sf;
        cache[ci].age = 0;
        if (!track_ray(t, a, b, h, n, &sf)) { cache[ci].ok = 0; return 0; }
        cache[ci].ok = 1; cache[ci].y = h[1]; memcpy(cache[ci].n, n, 12);
    }
    s32 ht = P[1] - h[1];
    int al = 0x60 - (ht - 24) / 6; if (al > 0x60) al = 0x60; if (al <= 0) return 0;
    s32 S = (0x818 - ht) * 8; if (S < 0x800) S = 0x800;            // size, 16384 = 1
    s32 f[3] = { p->mat[0][2], p->mat[1][2], p->mat[2][2] };       // the board's line laid on the ground (16384 = 1)
    s32 d = (f[0] * n[0] + f[1] * n[1] + f[2] * n[2]) >> 14;
    for (int k = 0; k < 3; k++) f[k] -= (d * n[k]) >> 14;
    s32 fl = (s32)sqrt32((u32)(f[0] * f[0] + f[1] * f[1] + f[2] * f[2])); if (fl < 164) return 0;
    for (int k = 0; k < 3; k++) f[k] = f[k] * 16384 / fl;
    s32 r[3] = { (f[1] * n[2] - f[2] * n[1]) >> 14, (f[2] * n[0] - f[0] * n[2]) >> 14, (f[0] * n[1] - f[1] * n[0]) >> 14 };
    s32 L = 45 * S, Wd = 12 * S;                                   // 16384 = 1 game unit
    v16 q[4][3];                                                   // corners from the centre, v16 (game units x 16)
    for (int k = 0; k < 3; k++) {
        s32 rw = (s32)(((s64)r[k] * Wd) >> 24), fL = (s32)(((s64)f[k] * L) >> 24);
        q[0][k] = -rw + fL; q[1][k] = -rw - fL; q[2][k] = rw - fL; q[3][k] = rw + fL;
    }
    glPushMatrix();
    // (from the float position: the translation from the truncated one put the shadow visibly off the board)
    // 0.75 units off the ground (was 2): the board's underside sits ~3.5 units into the snow, as in the exe, and the
    // shadow 2 units up covered a band of the board
    glTranslatef32((int)((pos[0] + n[0] * 0.75f / 16384) * 16), (int)((h[1] + n[1] * 0.75f / 16384) * 16), (int)((pos[2] + n[2] * 0.75f / 16384) * 16));
    glBindTexture(0, tex_shadow);
    glColor(RGB15(0, 0, 0));
    glPolyFmt(POLY_ALPHA(al * 31 / 255 > 0 ? al * 31 / 255 : 1) | POLY_CULL_NONE | POLY_ID(20));
    glBegin(GL_QUADS);
    static const int UV[4][2] = { { 0, 0 }, { 0, 32 }, { 32, 32 }, { 32, 0 } };
    for (int i = 0; i < 4; i++) {
        glTexCoord2t16(inttot16(UV[i][0]), inttot16(UV[i][1]));
        glVertex3v16(q[i][0], q[i][1], q[i][2]);
    }
    glEnd();
    glPopMatrix(1);
    return 1;
}

// the board's snow trail (0x644d4 'trail_texture' = trailtest.bmp, laid by 0x785b4): a ring of 32 segments, each a
// pair of points either side of the board (the board's right axis x the width, a little behind it). The newest
// pair follows the board; a new one starts every ~96 units. Only on packed snow (width 0x12) and powder (0x15) -
// nothing on ice, in the air or on a rail, where the ribbon breaks. Drawn white at alpha 0x30.
#define TRAIL_N 32
typedef struct { s32 l[3], r[3]; u8 on; } TrailSeg;
typedef struct { TrailSeg seg[TRAIL_N]; int head; s32 last[3]; u8 head_on; } Trail;
static Trail trails[4];                                            // the player, then the ghosts
static void trail_reset(Trail *t) { memset(t, 0, sizeof(*t)); }
static void trail_step(Trail *t, const Phys *p, int width)
{
    s32 pos[3] = { p->pos[0] >> 8, p->pos[1] >> 8, p->pos[2] >> 8 };
    TrailSeg *h = &t->seg[t->head];
    for (int k = 0; k < 3; k++) {
        // (the exe's board matrix is 1.12, the port's 2.14: >> 13 there is >> 15 here - an 18 / 21 unit wide band)
        s32 side = p->mat[k][0] * width >> 15, back = p->mat[k][2] * width >> 17;
        h->l[k] = pos[k] - back - side; h->r[k] = pos[k] - back + side;
    }
    h->on = width > 0;
    s32 dx = (pos[0] - t->last[0]) >> 3, dy = (pos[1] - t->last[1]) >> 3, dz = (pos[2] - t->last[2]) >> 3;
    if ((width && !t->head_on) || dx * dx + dy * dy + dz * dz > 0x8f) {   // start a new segment
        t->head = (t->head + 1) % TRAIL_N; t->seg[t->head] = *h;
        memcpy(t->last, pos, sizeof(pos)); t->head_on = width > 0;
    }
}
static int trail_width(const Phys *p, bool known_surface)
{
    if (p->air || p->rail_state || p->jstate == 7) return 0;
    if (!known_surface) return 0x12;                               // ghosts: no surface from the recording
#ifdef TEST_TRAIL
    { static int cnt; if (++cnt % 25 == 0) printf("surf %d\n", p->surf); }
    return 0x12;
#endif
    return p->surf == 4 ? 0x15 : p->surf == 1 ? 0x12 : 0;
}
ITCM_CODE static int draw_trail(const Trail *t, const float cam[3])
{
    if (!tex_trail) return 0;
    const TrailSeg *c = &t->seg[t->head];                          // origin: the newest pair (v16 reaches +-2048 units)
    s32 o[3] = { (c->l[0] + c->r[0]) / 2, (c->l[1] + c->r[1]) / 2, (c->l[2] + c->r[2]) / 2 };
    const TrailSeg *hd = &t->seg[t->head];
    float dx = hd->l[0] - cam[0], dz = hd->l[2] - cam[2];
    if (dx * dx + dz * dz > 4000.0f * 4000.0f) return 0;
    glPushMatrix();
    glTranslatef32(o[0] * 16, o[1] * 16 + 6 * 16, o[2] * 16);   // a little up: the DS's simplified course rides above the collision ground
    glBindTexture(0, tex_trail);
#ifdef TEST_TRAIL
    glColor(RGB15(31, 0, 0)); glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(21));
#else
    glColor(RGB15(31, 31, 31));
    glPolyFmt(POLY_ALPHA(10) | POLY_CULL_NONE | POLY_ID(21));
#endif
    // port: one quad strip per unbroken run (each section's far edge is the next one's near edge: sent once, not
    // twice - half the vertices)
    int n = 0; bool open = false; int vi = 0;
    for (int i = 1; i < TRAIL_N; i++) {                            // from the oldest to the newest
        const TrailSeg *a = &t->seg[(t->head + i) % TRAIL_N], *b = &t->seg[(t->head + i + 1) % TRAIL_N];
        bool ok = a->on && b->on;
        if (ok)
            for (int k = 0; k < 3 && ok; k++) {
                s32 v0 = a->l[k] - o[k], v1 = a->r[k] - o[k], v2 = b->r[k] - o[k], v3 = b->l[k] - o[k];
                if (v0 > 2000 || v0 < -2000 || v1 > 2000 || v1 < -2000 || v2 > 2000 || v2 < -2000 || v3 > 2000 || v3 < -2000) ok = false;
            }
        if (!ok) { if (open) { glEnd(); open = false; } continue; }
        if (!open) {                                               // a run starts: its near edge
            glBegin(GL_QUAD_STRIP); open = true; vi = 0;
            glTexCoord2t16(0, 0); glVertex3v16((a->l[0] - o[0]) * 16, (a->l[1] - o[1]) * 16, (a->l[2] - o[2]) * 16);
            glTexCoord2t16(inttot16(16), 0); glVertex3v16((a->r[0] - o[0]) * 16, (a->r[1] - o[1]) * 16, (a->r[2] - o[2]) * 16);
        }
        vi++;
        glTexCoord2t16(0, inttot16(16 * vi)); glVertex3v16((b->l[0] - o[0]) * 16, (b->l[1] - o[1]) * 16, (b->l[2] - o[2]) * 16);
        glTexCoord2t16(inttot16(16), inttot16(16 * vi)); glVertex3v16((b->r[0] - o[0]) * 16, (b->r[1] - o[1]) * 16, (b->r[2] - o[2]) * 16);
        n++;
    }
    if (open) glEnd();
    glPopMatrix(1);
#ifdef TEST_TRAIL
    if (t == &trails[0]) { static int c2; if (++c2 % 30 == 0) { int on = 0; for (int i = 0; i < TRAIL_N; i++) on += t->seg[i].on;
        const TrailSeg *p0 = &t->seg[(t->head + TRAIL_N - 1) % TRAIL_N];
        printf("trail q%d on%d h%d d%ld,%ld\n", n, on, t->head, (long)(hd->l[0] - p0->l[0]), (long)(hd->l[2] - p0->l[2])); } }
#endif
    return n;
}

// firework sparks (0x39f48): camera-facing quads, 2 x 16 game units in the exe, drawn a little larger here so they
// read at 256 x 192; they shrink over the last 64 of their life
ITCM_CODE static int draw_fireworks(const Fireworks *f, const float cam[3], float fx, float fz, int budget)
{
    if (!tex_spark || !f->alive || budget <= 0) return 0;
    glBindTexture(0, tex_spark);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(11));
    float rx = fz, rz = -fx;
    int n = 0;
    for (int i = 0; i < FW_SPARKS && n < budget; i++) {
        const Spark *s = &f->p[i];
        if (s->life <= 0 || s->delay) continue;
        float px = s->pos[0] / 256.0f, py = s->pos[1] / 256.0f, pz = s->pos[2] / 256.0f;
        float dx = px - cam[0], dy = py - cam[1], dz = pz - cam[2];
        if (dx * fx + dz * fz < 0 || dx * dx + dy * dy + dz * dz > 10000.0f * 10000.0f) continue;
        float sz = 28.0f * (s->life < 64 ? s->life / 64.0f : 1.0f);
        glColor(s->color);
        glPushMatrix();
        glTranslatef32(s->pos[0] / 16, s->pos[1] / 16, s->pos[2] / 16);
        v16 ax = floattov16(rx * sz / 256), az = floattov16(rz * sz / 256), uy = floattov16(sz / 256);
        glBegin(GL_QUADS);
        glTexCoord2t16(0, 0);                         glVertex3v16(-ax, uy, -az);
        glTexCoord2t16(0, inttot16(32));              glVertex3v16(-ax, -uy, -az);
        glTexCoord2t16(inttot16(32), inttot16(32));   glVertex3v16(ax, -uy, az);
        glTexCoord2t16(inttot16(32), 0);              glVertex3v16(ax, uy, az);
        glEnd();
        glPopMatrix(1);
        n++;
    }
    return n;
}

// Culling here runs over every glare / tree / pickup of the course each frame: in integers (the ARM9 has no FPU;
// the float version of these loops cost ~90 scanlines a frame), floats only for the few that are drawn.
static inline bool near_ahead(s32 dx, s32 dy, s32 dz, s32 r, s32 ifx, s32 ifz, s32 back, u32 *d2)
{
    if (dx > r || dx < -r || dz > r || dz < -r || dy > r || dy < -r) return false;
    if (dx * ifx + dz * ifz < -back * 1024) return false;          // behind the camera (fx, fz x 1024)
    *d2 = (u32)(dx * dx) + (u32)(dz * dz) + (u32)(dy * dy);
    return *d2 <= (u32)r * (u32)r;
}
ITCM_CODE static int draw_glares(const Track *t, const float cam[3], float fx, float fz, int budget)
{
    if (!tex_glare || !t->nglares || budget <= 0) return 0;
    enum { K = 24 };
    int idx[K]; u32 dist[K]; int n = 0;
    s32 cx = (s32)cam[0], cy = (s32)cam[1], cz = (s32)cam[2], ifx = (s32)(fx * 1024), ifz = (s32)(fz * 1024);
    for (int i = 0; i < t->nglares; i++) {
        const s32 *p = t->glares[i].pos; u32 d;
        if (!near_ahead(p[0] - cx, p[1] - cy, p[2] - cz, 12000, ifx, ifz, 0, &d)) continue;
        if (n == K && d >= dist[K - 1]) continue;
        int j = n < K ? n++ : K - 1;
        while (j > 0 && dist[j - 1] > d) { dist[j] = dist[j - 1]; idx[j] = idx[j - 1]; j--; }
        dist[j] = d; idx[j] = i;
    }
    if (n > budget) n = budget;
    glBindTexture(0, tex_glare);
    // the exe adds them (blend 0x100c) at half size 128 units, 64 units toward the camera (0x750d0); the DS can only
    // alpha blend, so they are drawn faint: on bright snow an additive flare barely shows, on dark ground it glows
    glPolyFmt(POLY_ALPHA(20) | POLY_CULL_NONE | POLY_ID(10));
    // camera right (horizontal) x half size 128, in v16 (DS units 4096 = 256 game units -> 128 units = 2048)
    v16 ax = (v16)(ifz * 2048 / 1024), az = (v16)(-ifx * 2048 / 1024), uy = 2048;
    for (int k = 0; k < n; k++) {
        const struct Glare *g = &t->glares[idx[k]];
        s32 tx = cx - g->pos[0], ty = cy - g->pos[1], tz = cz - g->pos[2];
        s32 tl = (s32)sqrt32(dist[k]); if (tl < 1) tl = 1;          // hardware square root
        glColor(g->color);
        glPushMatrix();
        // 64 units toward the camera; f32 translation = game units x 16
        glTranslatef32((g->pos[0] + tx * 64 / tl) * 16, (g->pos[1] + ty * 64 / tl) * 16, (g->pos[2] + tz * 64 / tl) * 16);
        if (tl < 400) {                                            // near the camera: clipped short of the near plane
            float c[3] = { g->pos[0] + tx * 64.0f / tl, g->pos[1] + ty * 64.0f / tl, g->pos[2] + tz * 64.0f / tl };
            float X[3] = { ifz / 8.0f, 0, -ifx / 8.0f }, Y[3] = { 0, 128, 0 };   // (ifx, ifz x 1024; half size 128)
            quad_front(c, cam, X, Y, 64, 64);
            glPopMatrix(1);
            continue;
        }
        glBegin(GL_QUADS);
        glTexCoord2t16(0, 0);                         glVertex3v16(-ax, uy, -az);
        glTexCoord2t16(0, inttot16(64));              glVertex3v16(-ax, -uy, -az);
        glTexCoord2t16(inttot16(64), inttot16(64));   glVertex3v16(ax, -uy, az);
        glTexCoord2t16(inttot16(64), 0);              glVertex3v16(ax, uy, az);
        glEnd();
        glPopMatrix(1);
    }
    return n;
}
ITCM_CODE static int draw_props(const Track *t, const float cam[3], float fx, float fz, int budget, int spin)
{
    int polys = 0;
    s32 cx = (s32)cam[0], cy = (s32)cam[1], cz = (s32)cam[2], ifx = (s32)(fx * 1024), ifz = (s32)(fz * 1024);
    // pickups glow: no lights, full emission (a NORMAL command sets the vertex colour = emission when unlit)
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FOG | POLY_ID(2));
    glMaterialf(GL_EMISSION, RGB15(31, 31, 31));
    for (int i = 0; i < t->npicks; i++) {                          // pickups, spinning (+0x16 per step)
        const Pickup *q = &t->picks[i]; u32 d;
        if (q->taken) continue;
        if (!near_ahead((s32)q->pos[0] - cx, (s32)q->pos[1] - cy, (s32)q->pos[2] - cz, 6000, ifx, ifz, 300, &d)) continue;
        const Mesh *m = &m_pick[q->type];
        if (polys + m->npolys > budget) break;
        int a = ((spin + i * 300) & 0x7ff) * 16;                   // 2048 a turn -> libnds' 32768
        s32 cs = cosLerp(a), sn = sinLerp(a);                      // 4096 = 1
        MatX W = { { { cs, 0, sn, (s32)q->pos[0] * 16 }, { 0, 4096, 0, (s32)q->pos[1] * 16 }, { -sn, 0, cs, (s32)q->pos[2] * 16 } } };
        rider_draw_static_x(m, &W);
        polys += m->npolys;
    }
    glMaterialf(GL_EMISSION, RGB15(0, 0, 0));
    if (!m_tree.dl) return polys;
    // trees are crossed cards: lit by one-sided normals their back halves came out black, so they are drawn unlit
    // at a fixed brightness (the exe gives static objects a plain white vertex colour, 0x5c9ac)
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FOG | POLY_ID(3));
    glMaterialf(GL_EMISSION, RGB15(27, 27, 27));
    // every tree out to the end of the fog (5750 x 12/10 of the camera's reach): with only the nearest 14 drawn, trees
    // switched on and off in plain view as the nearest set changed - now they come out of the fog like the course
    enum { K = 40 };
    int idx[K]; u32 dist[K]; int n = 0;
    for (int i = 0; i < t->ntrees; i++) {
        const TreePos *tp = &t->trees[i]; u32 d;
        s32 dx = tp->x - cx, dz = tp->z - cz;
        if (!near_ahead(dx, 0, dz, tree_reach, ifx, ifz, 400, &d)) continue;
        if (n == K && d >= dist[K - 1]) continue;
        int j = n < K ? n++ : K - 1;
        while (j > 0 && dist[j - 1] > d) { dist[j] = dist[j - 1]; idx[j] = idx[j - 1]; j--; }
        dist[j] = d; idx[j] = i;
    }
    // port: far trees as one camera-facing quad of the tree rendered from the side (tools/ds_impostor.py: its
    // mesh, unlit, as drawn here) - 1 polygon instead of 72, 4 vertices instead of 188. Trees nearer than IMP_D stay
    // meshes; the change happens a little over half way into the fog.
    int nmesh = n;
    if (tex_imp) { nmesh = 0; while (nmesh < n && dist[nmesh] <= (u32)IMP_D * IMP_D) nmesh++; }
    if (nmesh < n) {
        glPushMatrix();
        glTranslatef32(cx * 16, cy * 16, cz * 16);                // v16 = game units relative to the camera
        glScalef32(inttof32(16), inttof32(16), inttof32(16));
        glBindTexture(0, tex_imp);
        glColor(RGB15(27, 27, 27));                                // = the meshes' emission (unlit)
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FOG | POLY_ID(3));
        glBegin(GL_QUADS);
        for (int k = nmesh; k < n; k++) {
            const TreePos *tp = &t->trees[idx[k]];
            u32 h = hash32(idx[k] * 2654435761u + tp->x);
            s32 sxz = (0x4000 - (h & 0x3ff)) >> 2, sy = (0x2000 + ((h >> 10) & 0xfff)) >> 2;   // as the mesh's
            s32 x0 = imp_ext[0] * sxz >> 12, x1 = imp_ext[1] * sxz >> 12, y0 = imp_ext[2] * sy >> 12, y1 = imp_ext[3] * sy >> 12;
            s32 bx = tp->x - cx, by = tp->y - cy, bz = tp->z - cz;
            s32 rx = ifz, rz = -ifx;                               // the view's right (x 1024)
            s32 lx0 = bx + (x0 * rx >> 10), lz0 = bz + (x0 * rz >> 10), lx1 = bx + (x1 * rx >> 10), lz1 = bz + (x1 * rz >> 10);
            glTexCoord2t16(0, inttot16(imp_h));        glVertex3v16(lx0, by + y0, lz0);
            glTexCoord2t16(inttot16(imp_w), inttot16(imp_h)); glVertex3v16(lx1, by + y0, lz1);
            glTexCoord2t16(inttot16(imp_w), 0);        glVertex3v16(lx1, by + y1, lz1);
            glTexCoord2t16(0, 0);                      glVertex3v16(lx0, by + y1, lz0);
            polys++;
        }
        glEnd();
        glPopMatrix(1);
        glColor(RGB15(31, 31, 31));
        glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FOG | POLY_ID(3));
    }
    for (int k = 0; k < nmesh && polys + m_tree.npolys <= budget; k++) {
        const TreePos *tp = &t->trees[idx[k]];
        u32 h = hash32(idx[k] * 2654435761u + tp->x);
        s32 sxz = (0x4000 - (h & 0x3ff)) >> 2, sy = (0x2000 + ((h >> 10) & 0xfff)) >> 2;   // 4096 = 1
        {   // port: the camera inside the tree's crown - its branches would cross the near plane, which the hardware
            // draws as fully fogged white wedges (see quad_front): the tree is left out until the camera is clear
            s32 rr = (tex_imp ? (imp_ext[1] > -imp_ext[0] ? imp_ext[1] : -imp_ext[0]) : 200) * sxz >> 12;
            s32 y0 = tex_imp ? imp_ext[2] * sy >> 12 : 0, y1 = tex_imp ? imp_ext[3] * sy >> 12 : 960 * sy >> 12;
            s32 dx = tp->x - cx, dz = tp->z - cz, dy = cy - tp->y;
            if ((s64)dx * dx + (s64)dz * dz < (s64)(rr + 24) * (rr + 24) && dy > y0 - 24 && dy < y1 + 24) continue;
        }
        int a = ((h >> 22) & 0x7ff) * 16;
        s32 cs = cosLerp(a) * sxz >> 12, sn = sinLerp(a) * sxz >> 12;
        MatX W = { { { cs, 0, sn, tp->x * 16 }, { 0, sy, 0, tp->y * 16 }, { -sn, 0, cs, tp->z * 16 } } };
        rider_draw_static_x(&m_tree, &W);
        polys += m_tree.npolys;
    }
    glMaterialf(GL_EMISSION, RGB15(0, 0, 0));
    return polys;
}

static const char *RIDERS[] = { "elise", "mac", "kaori", "moby", "psymon", "zoe", "griff", "nate", "viggo", "alegra" };
#define NRIDERS (int)(sizeof(RIDERS) / sizeof(RIDERS[0]))

static const char *TRACKS[] = { "ABC1", "ASS1", "BHP1", "CRA3", "DBC2", "DRA4",
                                "DSS2", "EBC3", "ERA5", "ESS3", "ara1", "bra2" };
#define NTRACKS (int)(sizeof(TRACKS) / sizeof(TRACKS[0]))

static volatile u32 vblanks;
void vfx_idle(void) { music_update(vblanks, false); }   // (voicefx.c: the music through a clip's processing)
static int track_cull = POLY_CULL_FRONT;   // consistent winding; FRONT because of the X-mirrored projection
static volatile bool rumble_live;                 // the Rumble Pak may run (not paused / in a replay)
static void on_vblank(void) { vblanks++; submap_vblank(); rumble_frame(1, rumble_live); }
// a scanline clock across frames (263 lines a frame; the vblank interrupt counts at line 192)
static int NOW_LINE(void) { int ime = REG_IME; REG_IME = 0; int v = REG_VCOUNT; u32 n = vblanks; REG_IME = ime; return (int)n * 263 + (v >= 192 ? v - 192 : v + 71); }

#define NEAR_DS 0.025f
static void setup_fog(void)
{
    glFogShift(3);
    glFogOffset(0x7F00);
    for (int i = 0; i < 32; i++) {
        int d = i * 4;
        glFogDensity(i, d > 127 ? 127 : d);
    }
    glFogColor(19, 22, 27, 31);
}

// 0x644d4 / 0x7695c: the exe fogs linearly from half the fog distance to the whole of it, the distance being 6900
// game units (13800 on the superpipe; it then shrinks while the frame is busy, never below 3450) x 10 / 12. That
// hides how far the course is drawn, so new sections come out of the fog instead of popping in. The DS fog works on
// the depth buffer: map those distances through the projection (near 0.1, far 160 DS units) to depth values.
static int fog_depth(float game_units)
{
    float z = game_units / 256.0f, n = NEAR_DS, f = 160.0f;
    float d = (f / (f - n)) * (1.0f - n / z);
    int v = (int)(d * 0x7fff); return v < 0 ? 0 : v > 0x7fff ? 0x7fff : v;
}
// 0x644d4: each event's fog colour (the event table's word at 0xdddf0 + event x 0x54, 0xAARRGGBB). The port fogged
// every course to one light grey-blue: on Metro City's night sky (fog 8, 49, 100) the buildings past the fog's
// end stood out as white blocks. The clear colour (where nothing is drawn) follows it.
static const u32 EVENT_FOG[15] = { 0xf7f7f7, 0x083164, 0xf7f7f7, 0x000000, 0xf7f7f7, 0xf7f7f7, 0xf7d6ce, 0xbdbdbd,
                                   0xbdbdbd, 0xc1bdb8, 0xc1bdb8, 0xffffff, 0xffffff, 0x7983c7, 0x7983c7 };
static void set_event_fog(int ev)
{
    u32 c = EVENT_FOG[ev % 15];
    int r = (c >> 19) & 31, g = (c >> 11) & 31, b = (c >> 3) & 31;
    glFogColor(r, g, b, 31);
    glClearColor(r, g, b, 31);
}
static void set_fog_range(int fog_dist)
{
    int end = fog_dist * 10 / 12, start = end / 2;
    tree_reach = end + 250; fog_end_units = end;
    int d0 = fog_depth(start), d1 = fog_depth(end), shift = 10;
    while (shift > 0 && (32 * (0x400 >> shift)) < d1 - d0) shift--;   // 32 table steps of 0x400 >> shift depth units
    glFogShift(shift);
    glFogOffset(d0);
    int step = 0x400 >> shift;
    for (int i = 0; i < 32; i++) {
        int dep = d0 + i * step;
        int k = d1 > d0 ? (dep - d0) * 127 / (d1 - d0) : 127;
        glFogDensity(i, k > 127 ? 127 : k);
    }
}

static void setup_3d(void)
{
    videoSetMode(MODE_0_3D);
    vramSetBankA(VRAM_A_TEXTURE);
    vramSetBankB(VRAM_B_TEXTURE);
    // banks in slot order: the texture allocator hands out addresses bank by bank (A, B, C, D = slots 0-3). With C
    // on slot 3 and D on slot 2 (as before stage 47) whatever was put in bank C was fetched from bank D: those
    // course textures drew black - the holes in the course.
    vramSetBankC(VRAM_C_TEXTURE_SLOT2);
    vramSetBankD(VRAM_D_TEXTURE_SLOT3);
    vramSetBankE(VRAM_E_TEX_PALETTE);
    glInit();
    // no antialiasing (the Gizmondo had none): the DS blends every polygon edge with what is behind it, inner ones
    // too, and the board's seams showed as bright dots along its edges ("visible vertices")
    glEnable(GL_TEXTURE_2D | GL_FOG | GL_BLEND);                   // blending: without it translucent polygons draw opaque
    glClearColor(19, 22, 27, 31);
    glClearPolyID(63);
    glClearDepth(0x7FFF);
    glViewport(0, 0, 255, 191);
    setup_fog();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // 0x583f0: glFrustum(w, -w, -0.75w, 0.75w, 1.55w, far): a 4:3 view 51.6 degrees tall (the X mirror is below)
    // near plane 0.05 DS units = 12.8 game units (was 25.6): the camera is kept ~25 units off walls and ground,
    // so the near plane cut into them - holes right beside the rider (a wall or the slope seen through)
    gluPerspective(51.6f, 256.0f / 192.0f, NEAR_DS, 160.0f);    // DS units (1 = 256 game units)
    // SSX 3 data is authored in a left-handed world (verified: FINISH banner text and finish_left/right markers).
    // Mirror X in projection so the screen image matches the original; this flips screen winding too.
    glScalef32(inttof32(-1), inttof32(1), inttof32(1));
    glMatrixMode(GL_MODELVIEW);
}

static Rider *player_rider;
static Rider *fe_rider;                  // (the menu's rider: defined with the frontend below)
static void load_rider_meshes_b(Rider *r, int ri, bool hi, int board);
static void load_rider_meshes(Rider *r, int ri, bool hi) { load_rider_meshes_b(r, ri, hi, 0); }
static void load_rider_meshes_b(Rider *r, int ri, bool hi, int board)
{
    char nm[24]; snprintf(nm, sizeof(nm), "%s_%s", RIDERS[ri], hi ? "high" : "low");
    rider_free_mesh(&r->body); rider_free_mesh(&r->board);
    rider_load_mesh(r, &r->body, nm);
    rider_load_mesh(r, &r->board, hi ? XT_67 : "def_board");
    // 0x3af34 / 0x4cfe0: def_board at object scale 0x7080 against the rider's 0x550 (hi_res_board is 6.5x larger)
    // (Griff is drawn at 0x4a0, his board still at 0x7080)
    r->board_scale = (hi ? 1.0f / 6.5f : 1.0f) * (float)0x7080 / (strcmp(RIDERS[ri], "griff") ? 0x550 : 0x4a0);
    r->board_lift = 0.0f;
    // the rider's own board (0x3af34: "<char>_board_<n>[_low].xgt"; the player's choice, opponents a random one)
    snprintf(nm, sizeof(nm), "%s_board_%d%s.xgt", RIDERS[ri], board & 3, hi ? "" : "_low");
    rider_set_tex(r, &r->board, nm);
    bool own = g_opt.board && (r == player_rider || r == fe_rider);    // port: the board designer's board
    // (both boards take the 128 x 128 design: def_board's texture coordinates run 0..128 texels across too)
    if (own) rider_use_tex(&r->board, board_texture(true));
    // the player in a race also keeps the high-detail model (the menu's): drawn while the frame has the polygons
    rider_free_mesh(&r->body_hi); rider_free_mesh(&r->board_hi); r->use_hi = false;
    if (!hi && r == player_rider) {
        snprintf(nm, sizeof(nm), "%s_high", RIDERS[ri]);
        if (rider_load_mesh(r, &r->body_hi, nm) && rider_load_mesh(r, &r->board_hi, XT_67)) {
            snprintf(nm, sizeof(nm), "%s_board_%d.xgt", RIDERS[ri], board & 3);
            rider_set_tex(r, &r->board_hi, nm);
            if (own) rider_use_tex(&r->board_hi, board_texture(true));
            r->board_scale_hi = (1.0f / 6.5f) * (float)0x7080 / (strcmp(RIDERS[ri], "griff") ? 0x550 : 0x4a0);
            r->board_lift_hi = 0;
        } else { rider_free_mesh(&r->body_hi); rider_free_mesh(&r->board_hi); }
    }
}


// sky panorama per course (the event table's sky<n>.bmp)
static const u8 SKY_OF[NTRACKS] = { 1, 1, 2, 4, 5, 5, 5, 6, 7, 8, 1, 3 };   // order of TRACKS[]

static void load_track_n(Track *t, int ti)
{
    sky_load(SKY_OF[ti]);
    set_fog_range(strcmp(TRACKS[ti], "BHP1") ? 6900 : 13800);
    char path[64];
    snprintf(path, sizeof(path), "nitro:/tracks/%s.ssxt", TRACKS[ti]);
    if (!track_load(t, path)) { while (1) swiWaitForVBlank(); }
}


// exe character index for each entry of RIDERS[] (mesh table at file 0x9f048: zoe, moby, psymon, griff, viggo,
// elise, nate, mac, alegra, kaori) - selects the grab set; Griff is drawn smaller (0x4a0 instead of 0x550)
static const u8 CHAR_OF[NRIDERS] = { 5, 7, 9, 1, 2, 0, 3, 6, 4, 8 };

static u32 read_buttons(u32 held)
{
    u32 b = 0;
    if (held & KEY_SELECT) return 0;
    // the world is drawn X-mirrored, so the original's "+turn" (heading increasing) is a right turn on screen
    if (held & KEY_RIGHT) b |= PB_RIGHT;
    if (held & KEY_LEFT) b |= PB_LEFT;
    if (held & KEY_UP) b |= PB_TUCK;
    if (held & KEY_DOWN) b |= PB_BRAKE;
    if (held & KEY_A) b |= PB_JUMP;
    if (held & KEY_B) b |= PB_BOOST;
    if (held & KEY_L) b |= PB_GRAB1;
    if (held & KEY_R) b |= PB_GRAB2;
    if (held & KEY_X) b |= PB_GRAB3;
    if (held & KEY_Y) b |= PB_GRAB4;
    return b;
}

static void print_help(void)
{
    printf("\033[13;0H" XT_68 "-"
           "D-pad carve/tuck/brake; air:\n spin L/R, flip UP/DOWN\n"
           "A jump  B boost  L R X Y grabs\n"
           "B+grab tweak/uber; ground X/Y\n punch (SELECT+RIGHT: rival in)\n"
           "START pause  SELECT+: L/R course\n A rider  START detail  B top\n"
           " X autopilot  Y camera\n UP respawn  DOWN auto-LOD");
}

// a trick's rating (0x45d80): 1-5 stars from HUD.bmp (11 x 12, u 0x40, one row per count) for 1000 / 2500 /
// 4000 / 7000 / 12500 points, centred over the points (Gizmondo 0xa3, 0x50 -> DS -32 / -24, stars a row above)
static void hud_stars(int pts, int dx, int dy)
{
    static const u8 X[5][5] = { {0x9f}, {0x97, 0xa8}, {0x9f, 0x90, 0xae}, {0x8a, 0x97, 0xa8, 0xb5}, {0x9f, 0x90, 0xae, 0x82, 0xbc} };
    int n = pts >= 12500 ? 5 : pts >= 7000 ? 4 : pts >= 4000 ? 3 : pts >= 2500 ? 2 : pts >= 1000 ? 1 : 0;
    for (int i = 0; i < n; i++)
        hud_sprite(X[n - 1][i] - 32 + dx - 2, 0x50 - 24 - 13 + dy, 0xb, 0xc, 0x40, (n - 1) * 0xc, 0xb, 0xc, RGB15(31, 31, 31));
}

// rider at interpolated position rp: physics orientation, flip pitch (0x4fba4) and lean; the board nose = heading
// (switch: backwards). Pose (when cl != NULL) at clip key `key`.
static int sx_sin_q(int a) { return sinLerp((a & 0x7ff) * 16) * 4; }   // 2048 a turn -> 16384 = 1
// centred, word-wrapped HUD text (current font); returns the number of lines
static int wrap_hud_c(int cx, int y, int w, const char *s, u16 color, int lh)
{
    s = L(s);
    char line[64]; int n = 0, lines = 0;
    line[0] = 0;
    while (*s) {
        const char *e = s; while (*e && *e != ' ') e++;
        char t[64]; int k = snprintf(t, sizeof(t), "%s%s%.*s", line, n ? " " : "", (int)(e - s), s); (void)k;
        if (n && hud_text_width(t) > w) { hud_text(cx, y + lines * lh, 2, line, color); lines++; snprintf(line, sizeof(line), "%.*s", (int)(e - s), s); }
        else snprintf(line, sizeof(line), "%s", t);
        n = 1; s = *e ? e + 1 : e;
    }
    if (line[0]) { hud_text(cx, y + lines * lh, 2, line, color); lines++; }
    return lines;
}

#ifndef RIDE_LIFT
#define RIDE_LIFT 4.0f
#endif
// sine / cosine of a 2048-a-turn angle from libnds' table (newlib's soft-float sinf / cosf cost ~0.5 lines each:
// four per rider a frame)
static inline float tsin(int a) { return sinLerp((s16)((a & 0x7ff) * 16)) * (1.0f / 4096.0f); }
static inline float tcos(int a) { return cosLerp((s16)((a & 0x7ff) * 16)) * (1.0f / 4096.0f); }
static int rprof[5];                                               // TEST_RIDERPROF: pose / draw / fade lines
#ifdef TEST_RIDERPROF
#define RLINE() NOW_LINE()
#else
#define RLINE() 0
#endif
ITCM_CODE static int draw_rider(Rider *r, const Phys *p, const float rp[3], const Clip *cl, float key, bool loop)
{
    int rp0 = RLINE();
    float up[3], f[3];
    for (int k = 0; k < 3; k++) { up[k] = p->mat[k][1] / 16384.0f; f[k] = p->mat[k][2] / 16384.0f; }
    if (p->pitch) {                                                // flip: rotate forward/up about the right axis
        float c = tcos(p->pitch), s = tsin(p->pitch), f2[3], u2[3];
        for (int k = 0; k < 3; k++) { f2[k] = c * f[k] - s * up[k]; u2[k] = s * f[k] + c * up[k]; }
        memcpy(f, f2, sizeof(f)); memcpy(up, u2, sizeof(up));
    }
    // 0x4cfe0: the rider object takes the physics matrix (forward = travel) and a non-zero stance (switch, or a goofy
    // rider's natural stance) negates its X scale: the rider is mirrored, not turned round. The skeleton looks along
    // +Y (rendered offline: the face, the gate pose's chest and the board's nose all point +Y), so +Y = travel.
    float sgn = p->stance ? -1.0f : 1.0f;
    float side[3] = { f[1] * up[2] - f[2] * up[1], f[2] * up[0] - f[0] * up[2], f[0] * up[1] - f[1] * up[0] };
    float c = tcos(p->lean), sl = tsin(p->lean);                    // the lean is a world tilt: the same either way
    float s2[3], u2[3];
    for (int k = 0; k < 3; k++) { s2[k] = c * side[k] + sl * up[k]; u2[k] = -sl * side[k] + c * up[k]; }
    for (int k = 0; k < 3; k++) s2[k] *= sgn;
    Mat34 W;
    const float RS = (p->character == 3 ? 0x4a0 : 0x550) / 16384.0f;   // rider object scale (0x3af34)
    for (int k = 0; k < 3; k++) { W.m[k][0] = s2[k] * RS; W.m[k][1] = f[k] * RS; W.m[k][2] = u2[k] * RS; }
    int rp1 = RLINE();
    if (cl) rider_pose_clip(r, cl, key, loop);
    int rp2 = RLINE();
    float o[3]; rider_bone_pos(r, 4, o); o[2] -= 12;
    // grinding: the course's rail splines run ~3 units above the rail's top faces (checked on R&B's rails), and the
    // board rode that high - drawn 3 units down onto the rail
    // on the snow the board sat a board's thickness into the ground (only the shadow showed under the boots on
    // a carve): lifted that much along the rider's up (u2: the board's own normal), in the air too (no step on
    // take-off). Grinding, 3 down instead. The offset eases between the two (a 7 unit jump landing on a rail).
    static struct { const Phys *p; float off; } eo[4];
    int ei = 0; while (ei < 3 && eo[ei].p && eo[ei].p != p) ei++;
    if (eo[ei].p != p) { eo[ei].p = p; eo[ei].off = RIDE_LIFT; }
    float want = p->rail_state == 2 ? -3.0f : p->rail_state ? 0.0f : RIDE_LIFT;
    eo[ei].off += (want - eo[ei].off) * 0.35f;
    float lift = eo[ei].off;
    for (int k = 0; k < 3; k++) W.m[k][3] = rp[k] - (W.m[k][0] * o[0] + W.m[k][1] * o[1] + W.m[k][2] * o[2]) + (p->rail_state == 2 ? (k == 1 ? lift : 0) : u2[k] * lift);
    int rp3 = RLINE();
    rider_draw(r, &W, 4);
    int rp4 = RLINE();
    rprof[0] += rp1 - rp0; rprof[1] += rp2 - rp1; rprof[2] += rp3 - rp2; rprof[3] += rp4 - rp3;
    if (r->use_hi && r->body_hi.dl) return r->body_hi.npolys + (r->board_hi.dl ? r->board_hi.npolys : r->board.npolys);
    return r->body.npolys + r->board.npolys;
}

// race opponents: the exe's recorded ghosts for this course (ghost.c), ridden by other characters
extern int rider_tex_fail;
static Ghost ghosts[3]; static Rider grd[3]; static int grd_char[3] = { -1, -1, -1 };
static int nghost, race_kind;                                     // race_kind: 1 time race, 2 trick event, 0 none
static float gprev[3][3], gcur[3][3];
static struct { s32 mat[3][3]; int pitch, lean, anim; } prev_ori;
static struct { s32 mat[3][3]; int pitch, lean, anim, phase, ok; } gori[3];    // the same for the opponents
// a rider's turn drawn between its last two steps: up / forward nlerped, flip angle and lean interpolated
ITCM_CODE static void interp_ori(Phys *d, const s32 pm[3][3], int ppitch, int plean, const Phys *c, float al)
{
    float u[3], f[3], a = 1 - al;
    for (int k = 0; k < 3; k++) { u[k] = pm[k][1] * a + c->mat[k][1] * al; f[k] = pm[k][2] * a + c->mat[k][2] * al; }
    float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
    if (ul > 1) {
        for (int k = 0; k < 3; k++) u[k] /= ul;
        float dd = f[0] * u[0] + f[1] * u[1] + f[2] * u[2];
        for (int k = 0; k < 3; k++) f[k] -= dd * u[k];
        float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (fl > 1) for (int k = 0; k < 3; k++) { d->mat[k][1] = (s32)(u[k] * 16384); d->mat[k][2] = (s32)(f[k] / fl * 16384); }
    }
    int dp = ((c->pitch - ppitch + 0x400) & 0x7ff) - 0x400;
    d->pitch = (ppitch + (int)(dp * al)) & 0x7ff;
    d->lean = plean + (int)((c->lean - plean) * al);
}   // the player's turn at the step before (drawing interpolates)
// port: the cross-fade into a new clip (as the player's) for the replayed rider (slot 0) and the near ghosts (1..3);
// returns NULL when it posed the bones itself
static struct { int from, to, ms; float key, last_key; } fade[4] = { { -1, -1, 999 }, { -1, -1, 999 }, { -1, -1, 999 }, { -1, -1, 999 } };
static const Clip *pose_fade(Rider *r, const Rider *src, int slot, int id, const Clip *cl, float key, int dms)
{
    if (id != fade[slot].to) { fade[slot].from = fade[slot].to; fade[slot].key = fade[slot].last_key; fade[slot].to = id; fade[slot].ms = 0; }
    else fade[slot].ms += dms;
    fade[slot].last_key = key;
    const Clip *from = fade[slot].from >= 0 && fade[slot].ms < 100 ? rider_clip((Rider *)src, fade[slot].from) : NULL;
    if (!from || !cl) return cl;
    rider_pose_blend(r, from, fade[slot].key, false, cl, key, false, fade[slot].ms * 256 / 100);
    return NULL;
}
static struct { s32 mat[3][3]; int pitch, lean, anim, phase, ok; } rpori;   // the replayed rider's last step
// port: when each rider first reached each stretch of the race line (in 40 ms steps from GO), for the bottom
// screen's time gaps: rider A's lead over B = now - when A was where B is now
static u16 *gap_t; static int gap_nb, gap_bs = 64, gap_hi[4];
static void gap_reset(const Track *t)
{
    gap_bs = t->length / 2048 > 64 ? (int)(t->length / 2048) + 1 : 64;
    int nb = (int)(t->length / gap_bs) + 2;
    if (nb != gap_nb) { free(gap_t); gap_t = malloc(4 * nb * sizeof(u16)); gap_nb = gap_t ? nb : 0; }
    for (int r = 0; r < 4; r++) gap_hi[r] = -1;
}
static int gap_b(float s) { int b = s > 0 ? (int)(s / gap_bs) : 0; return b < gap_nb ? b : gap_nb - 1; }
static void gap_mark(int r, float s, int step) { if (!gap_nb) return; int b = gap_b(s); while (gap_hi[r] < b) gap_t[r * gap_nb + ++gap_hi[r]] = step; }
static int gap_at(int r, float s) { if (!gap_nb) return -1; int b = gap_b(s); return b <= gap_hi[r] ? gap_t[r * gap_nb + b] : -1; }
static s32 field[8]; static int nfield;                           // trick events: the recorded scores to place against
static int pay_cash, pay_unl[15], pay_nunl;                       // the last finish's reward (0x711c4)

// ---- shadows (0x53d2c recorder, Shadow Racing). The player's run is recorded as the ghosts are: a 12-byte frame
// every 80 ms from the start of the countdown (position change clamped to +-127, up / 256, flags, heading >> 3,
// clip, phase, clock / 16) under a .ght header (1001, rider, -, frames, result, -, start x y z). The best run of each
// event (lowest time / highest score) is kept as its shadow, and next to the ROM when there is a FAT card.
static bool have_fat_g;                                          // a FAT card is there for the save and the shadows
#define REC_MAX 3750
static u8 *shadow[15]; static int shadow_len[15];
static u8 *rec; static int rec_n, rec_step_n; static s32 rec_at[3], rec_start[3]; static bool rec_on;
static bool shadow_new;                                          // the last run became the event's shadow
bool fe_has_shadow(int ev) { return ev >= 0 && ev < 15 && shadow[ev]; }
bool fe_shadow_info(int ev, char *buf, int n)
{
    if (!fe_has_shadow(ev)) return false;
    const s32 *h = (const s32 *)shadow[ev];
    char r[12] = "?"; if (h[1] >= 0 && h[1] < NRIDERS) { snprintf(r, sizeof(r), "%s", RIDERS[h[1]]); r[0] = toupper((u8)r[0]); }
    char v[16];
    if (EVENTS[ev].timed) fmt_time(v, h[4]); else snprintf(v, sizeof(v), "%ld", (long)h[4]);
    snprintf(buf, n, "%s  %s  %s", EVENTS[ev].name, r, v);
    return true;
}
void fe_delete_shadow(int ev)
{
    if (!fe_has_shadow(ev)) return;
    free(shadow[ev]); shadow[ev] = NULL; shadow_len[ev] = 0;
    if (have_fat_g) { char nm[40]; snprintf(nm, sizeof(nm), "ssx3gizwipe_shadow%02d.ght", ev); remove(nm); }
}
static void rec_begin(const Phys *p)
{
    if (!rec) rec = malloc(48 + REC_MAX * 12);
    rec_on = rec != NULL; rec_n = 0; rec_step_n = 0;
    for (int k = 0; k < 3; k++) rec_start[k] = rec_at[k] = p->pos[k] >> 8;
}
static void rec_frame(const Phys *p)                             // every 40 ms step; one frame per two
{
    if (!rec_on || (rec_step_n++ & 1) || rec_n >= REC_MAX) return;
    s8 *f = (s8 *)(rec + 48 + rec_n * 12);
    for (int k = 0; k < 3; k++) {
        s32 d = (p->pos[k] >> 8) - rec_at[k];
        if (d > 127) d = 127; if (d < -127) d = -127;
        f[k] = d; rec_at[k] += d;
        f[3 + k] = p->up[k] >> 8;
    }
    u8 fl = (p->stance ? 1 : 0) | (p->air ? 2 : 0) | (p->rail_state ? 4 : 0) | (p->surf == 5 ? 0x40 : p->surf == 4 ? 0x80 : 0);
    ((u8 *)f)[6] = fl;
    ((u8 *)f)[7] = (p->heading & 0x7ff) >> 3;
    ((u8 *)f)[8] = p->anim < 0xbf ? p->anim : 8;
    ((u8 *)f)[9] = p->anim_len ? (p->anim_ms * 256 / p->anim_len > 255 ? 255 : p->anim_ms * 256 / p->anim_len) : 0;
    ((u8 *)f)[10] = 80 / 16; ((u8 *)f)[11] = 0;
    rec_n++;
}
static void shadow_save(int ev)
{
    if (!have_fat_g || !shadow[ev]) return;
    char nm[40]; snprintf(nm, sizeof(nm), "ssx3gizwipe_shadow%02d.ght", ev);
    FILE *f = fopen(nm, "wb"); if (!f) return;
    fwrite(shadow[ev], 1, shadow_len[ev], f); fclose(f);
}
static void shadow_load_all(void)
{
    if (!have_fat_g) return;
    for (int ev = 0; ev < 15; ev++) {
        char nm[40]; snprintf(nm, sizeof(nm), "ssx3gizwipe_shadow%02d.ght", ev);
        FILE *f = fopen(nm, "rb"); if (!f) continue;
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        u8 *b = n >= 48 && n < 48 + REC_MAX * 12 + 1 ? malloc(n) : NULL;
        if (b && fread(b, 1, n, f) == (size_t)n && ((s32 *)b)[0] == 1001) { shadow[ev] = b; shadow_len[ev] = n; } else free(b);
        fclose(f);
    }
}
// the run is over: keep it as the shadow if there is none or it beat the old one
static void rec_finish(int ev, int rider, s32 result, bool timed)
{
    shadow_new = false;
    if (!rec_on || rec_n < 40) { rec_on = false; return; }
    rec_on = false;
    s32 *h = (s32 *)rec; memset(h, 0, 48);                       // (the header also serves the replay)
    h[0] = 1001; h[1] = rider; h[3] = rec_n; h[4] = result; h[6] = rec_start[0]; h[7] = rec_start[1]; h[8] = rec_start[2];
    if (shadow[ev]) {
        s32 old = ((s32 *)shadow[ev])[4];
        if (timed ? result >= old : result <= old) return;
    }
    int n = 48 + rec_n * 12;
    u8 *b = malloc(n); if (!b) return;
    memcpy(b, rec, n);
    free(shadow[ev]); shadow[ev] = b; shadow_len[ev] = n; shadow_new = true;
    shadow_save(ev);
}

// ---- wireless sessions (net.c): every rider is posed on every console from what its own console sends
static void net_pose(Ghost *g)                                    // a remote player, from their last packet
{
    const NetRider *r = &net_s.r[g->pid];
    if (!r->pos[0] && !r->pos[1] && !r->pos[2]) return;            // nothing heard yet: stay at the gate
    for (int k = 0; k < 3; k++) {
        g->ph.pos[k] = r->pos[k] << 8;
        g->ph.mat[k][1] = r->up[k]; g->ph.mat[k][2] = r->fwd[k];
    }
    g->ph.pitch = r->pitch; g->ph.lean = r->lean; g->ph.stance = (r->flags & 1) ? 0x400 : 0;
    g->ph.air = r->flags & 2; g->ph.rail_state = (r->flags & 4) ? 2 : 0;
    g->ph.anim = r->anim; g->phase = r->phase; g->ph.character = r->character; g->ph.kmh = r->kmh;
    g->s = r->s; g->net_flags = r->flags; g->result = r->result;
    if ((r->flags & 8) && !g->finished) { g->finished = true; g->fin_steps = r->result / PHYS_TICK_MS + 74; }
}

static void setup_ghosts(Track *t, int ev, int ri, const Rider *proto)
{
    int used[3];
    for (int k = 0; k < 3; k++) ghost_free(&ghosts[k]);
    // port: a restart / the next heat keeps no turn or clip from the run before (the ghosts stand still through
    // the intro and countdown, so the last run's final orientation was blended into their gate pose every frame)
    for (int k = 0; k < 3; k++) { gori[k].ok = 0; fade[1 + k].from = fade[1 + k].to = -1; fade[1 + k].ms = 999; }
    fade[0].from = fade[0].to = -1; rpori.ok = 0;
    nghost = 0;
    const Event *E = &EVENTS[ev];
    race_kind = E->timed ? 1 : 2;
    nfield = race_kind == 2 ? ghost_scores(E->ghost_event, field, 8) : 0;
    pay_cash = 0; pay_nunl = 0;
    if (fe_mp()) {                                                 // a wireless session: the other players (net.c)
        nfield = 0;
        for (int p = 0; p < NET_MAX && nghost < 3; p++) {
            if (p == net_me() || !((net_s.mask >> p) & 1)) continue;
            int k = nghost, rk = net_s.ctl[p].rider < NRIDERS ? net_s.ctl[p].rider : 0;
            if (grd_char[k] != rk) {
                if (grd_char[k] < 0) { grd[k] = *proto; memset(&grd[k].body, 0, sizeof(Mesh)); memset(&grd[k].board, 0, sizeof(Mesh)); memset(&grd[k].body_hi, 0, sizeof(Mesh)); memset(&grd[k].board_hi, 0, sizeof(Mesh)); grd[k].use_hi = false; }
                load_rider_meshes_b(&grd[k], rk, false, rand() & 3);
                grd_char[k] = rk;
            }
            Ghost *g = &ghosts[k];
            g->remote = true; g->pid = p; g->kseen = net_s.ctl[p].knock_seq;
            g->ph.character = CHAR_OF[rk];
            ghost_start(g, t, p + 1 > 3 ? 3 : p + 1);
            g->tough = 0x32 + career.attr[CHAR_OF[rk]][5] * 50 / 100 - 25;
            for (int j = 0; j < 3; j++) gprev[k][j] = gcur[k][j] = g->ph.pos[j] / 256.0f; gori[k].ok = 0;
            nghost++;
        }
        return;
    }
    if (fe_shadow() && shadow[ev] && ghost_from_mem(&ghosts[0], shadow[ev], shadow_len[ev])) {   // Shadow Racing: you vs your best
        int rk = ((s32 *)shadow[ev])[1]; if (rk < 0 || rk >= NRIDERS) rk = ri;
        if (grd_char[0] != rk) {
            if (grd_char[0] < 0) { grd[0] = *proto; memset(&grd[0].body, 0, sizeof(Mesh)); memset(&grd[0].board, 0, sizeof(Mesh)); }
            load_rider_meshes_b(&grd[0], rk, false, rand() & 3);
            grd_char[0] = rk;
        }
        ghosts[0].ph.character = CHAR_OF[rk];
        ghost_start(&ghosts[0], t, 1);
        ghosts[0].tough = 0x32 + career.attr[CHAR_OF[rk]][5] * 50 / 100 - 25;
        for (int j = 0; j < 3; j++) gprev[0][j] = gcur[0][j] = ghosts[0].ph.pos[j] / 256.0f;
        nghost = 1; nfield = 0;
        return;
    }
    for (int k = 0; k < E->riders - 1 && k < 3; k++) {
        if (!ghost_pick(&ghosts[nghost], E->ghost_event, used, nghost)) break;
        used[nghost] = ghosts[nghost].id;
        int rk = (ri + 1 + k * 3) % NRIDERS;                      // three other riders
        if (grd_char[k] != rk) {
            if (grd_char[k] < 0) { grd[k] = *proto; memset(&grd[k].body, 0, sizeof(Mesh)); memset(&grd[k].board, 0, sizeof(Mesh)); memset(&grd[k].body_hi, 0, sizeof(Mesh)); memset(&grd[k].board_hi, 0, sizeof(Mesh)); grd[k].use_hi = false; }
            load_rider_meshes_b(&grd[k], rk, false, rand() & 3);
            grd_char[k] = rk;
        }
        ghosts[nghost].ph.character = CHAR_OF[rk];
        ghost_start(&ghosts[nghost], t, nghost + 1);
        ghosts[nghost].tough = 0x32 + career.attr[CHAR_OF[rk]][5] * 50 / 100 - 25;   // 0x64414 Toughness (K 50)
        for (int j = 0; j < 3; j++) gprev[nghost][j] = gcur[nghost][j] = ghosts[nghost].ph.pos[j] / 256.0f;
        nghost++;
    }
}

// ---- career rounds (0x6a008 / 0x6b4a0 panels, 0x70db0 / 0x7171c / 0x723f4 rounds and standings)
// [...] runs each non-rival event as three rounds. Races: Qualifier, Semi Final, Final Round - a top
// three place moves you on. Slopestyle / Super Pipe: Heat 1 and Heat 2, whose[...]must be in the top
// three of six, then[...], a solo heat. The cash and unlocks come only from the final.
// AI trick riders: heat 1 = their recorded score, heat 2 = that +- rand(10000) (0x70db0); in the final the
// player's single run is ranked against their two-heat totals (0x66268 zeroes only the player's entry).
static int cur_event;                   // EVENTS[] index being played
static int fights[4];                   // debug: punches landed - [1] you knocked down, [2] a ghost, [3] stumbles
static int ev_round;                    // 0 qualifier / heat 1, 1 semi final / heat 2, 2 final
static bool round_ok;                   // the last run advanced
typedef struct { int rk; s32 h1, h2; } Stand;
static Stand stand[5];                  // the five AI riders of a trick event
static s32 pscore[3];                   // the player's score in each round
static int res_rk[4], res_ms[4], nres;  // race results: rider (-1 player), time (0 = DNF), in finishing order
static bool heats(void) { return fe_career() && !strstr(EVENTS[cur_event].kind, "rival"); }
static void stand_init(int ri)
{
    int perm[8]; for (int k = 0; k < 8; k++) perm[k] = k;
    for (int k = nfield - 1; k > 0; k--) { int j = rand() % (k + 1), x = perm[k]; perm[k] = perm[j]; perm[j] = x; }
    for (int k = 0; k < 5; k++) {
        stand[k].rk = (ri + 1 + k * 2) % NRIDERS;
        s32 b = !nfield ? 20000 + rand() % 30000 : k < nfield ? field[perm[k]]          // the recorded scores, then
              : field[perm[k % nfield]] / 100 * (80 + rand() % 40);                     // variations of them
        stand[k].h1 = b;
        s32 d = rand() % 10000;
        stand[k].h2 = (rand() & 1) ? b + d : (b - d > 0 ? b - d : 0);
    }
}
static s32 ai_total(int k) { return ev_round == 0 ? stand[k].h1 : stand[k].h1 + stand[k].h2; }
static s32 player_total(void) { return ev_round == 0 ? pscore[0] : ev_round == 1 ? pscore[0] + pscore[1] : pscore[2]; }
static int trick_rank(s32 v) { int r = 1; for (int k = 0; k < 5; k++) if (ai_total(k) > v) r++; return r; }
static s32 third_total2(bool both)      // the AI total the player has to beat for third place
{
    s32 t[5]; for (int k = 0; k < 5; k++) t[k] = both ? stand[k].h1 + stand[k].h2 : ai_total(k);
    for (int i = 0; i < 5; i++) for (int j = i + 1; j < 5; j++) if (t[j] > t[i]) { s32 x = t[i]; t[i] = t[j]; t[j] = x; }
    return t[2];
}
static s32 third_total(void) { return third_total2(false); }
static const char *round_name(bool results)
{
    static const char *const R[2][3] = { { "Qualifier", "Semi Final", "Final Round" }, { "Heat 1", "Heat 2", "Final Round" } };
    static const char *const RR[2][3] = { { XT_69, XT_70, XT_71 },
                                          { XT_72, XT_73, XT_71 } };
    if (fe_shadow()) return results ? (race_kind == 1 ? XT_74 : strstr(EVENTS[cur_event].kind, "PIPE") ? XT_75 : XT_76) : XT_32;
    if (!heats()) return results ? (fe_career() ? "Rewards" : XT_77) : (fe_career() ? L_kind(EVENTS[cur_event].kind) : XT_31);
    return (results ? RR : R)[race_kind == 2][ev_round];
}
static void cap_name(char *o, int rk) { snprintf(o, 12, "%s", RIDERS[rk]); o[0] = toupper((u8)o[0]); }
static Ghost rpg;
// port: the bottom screen's standings beside the map (submap_list): leader on top - in a race by finishing time,
// then by progress down the race line, with the player's time gap to the nearest rider in rank; in a trick event
// by score (the live score against the recorded / AI scores to beat)
static void standings(const Phys *pph, bool rp, int race, int race_steps, int fin_time, int ri)
{
    // port: at the gate (intro, countdown) the list is greyed and held in its first order until the riders leave it
    static SubRow held[9]; static int nheld = -1;
    bool gate = !rp && (race == R_INTRO || race == R_COUNT);
    if (gate && nheld >= 0) { submap_list(held, nheld, NULL); return; }
    if (!gate) nheld = -1;
    SubRow rows[9]; int n = 0;
    memset(rows, 0, sizeof(rows));
    if (race_kind == 1) {
        struct { float s; int fin, who; } e[4]; int ne = 0;     // who: -1 player, else ghost
        int pfin = rp ? (rpg.finished ? rpg.fin_steps : 0) : (race == R_DONE && fin_time > 0 ? fin_time / PHYS_TICK_MS : 0);
        e[ne].s = rp ? rpg.s : pph->s; e[ne].fin = pfin; e[ne++].who = -1;
        for (int k = 0; k < nghost && ne < 4; k++) { e[ne].s = ghosts[k].s; e[ne].fin = ghosts[k].finished ? ghosts[k].fin_steps : 0; e[ne++].who = k; }
        for (int i = 0; i < ne; i++) for (int j = i + 1; j < ne; j++) {
            bool sw = e[j].fin && (!e[i].fin || e[j].fin < e[i].fin);
            if (!e[i].fin && !e[j].fin && e[j].s > e[i].s) sw = true;
            if (e[i].fin && !e[j].fin) sw = false;
            if (sw) { typeof(e[0]) x = e[i]; e[i] = e[j]; e[j] = x; }
        }
        int me = 0; for (int i = 0; i < ne; i++) if (e[i].who < 0) me = i;
        for (int i = 0; i < ne; i++) {
            SubRow *r = &rows[n++]; r->rank = i + 1; r->me = e[i].who < 0;
            int rk = e[i].who < 0 ? ri : grd_char[e[i].who];
            if (e[i].who >= 0 && fe_mp()) snprintf(r->name, sizeof(r->name), "%.8s", net_s.ctl[ghosts[e[i].who].pid].name);
            else { char nm[12]; cap_name(nm, rk >= 0 && rk < NRIDERS ? rk : 0); snprintf(r->name, sizeof(r->name), "%s", nm); }
            if (e[i].fin && !r->me) { char tb[16]; fmt_time(tb, e[i].fin * PHYS_TICK_MS); snprintf(r->val, sizeof(r->val), "%s", tb); }
        }
        // the player's gap to the nearest rider in rank: behind the one ahead (+), or ahead of the one behind (-)
        if (ne > 1 && !rp && race == R_RACE) {
            int o = me > 0 ? me - 1 : 1;
            int ra = e[o].who < 0 ? 0 : 1 + e[o].who;
            int ms = -1;
            if (me > 0) {                                        // how long ago the rider ahead was where you are
                int t = gap_at(ra, e[me].s);
                if (t >= 0) ms = (race_steps - t) * PHYS_TICK_MS;
            } else {                                             // how long ago you were where the next rider is
                int t = gap_at(0, e[o].s);
                if (t >= 0) ms = (race_steps - t) * PHYS_TICK_MS;
            }
            if (ms >= 0) {
                if (ms > 599990) ms = 599990;
                char *v = rows[me].val; char sg = me > 0 ? '+' : '-';
                if (ms >= 60000) snprintf(v, 12, "%c%d:%02d.%d", sg, ms / 60000, ms / 1000 % 60, ms % 1000 / 100);
                else snprintf(v, 12, "%c%d.%02d", sg, ms / 1000, ms % 1000 / 10);
                rows[me].vcol = me > 0 ? 2 : 1;
            }
        }
    } else {
        s32 v[9]; int who[9], m = 0;                                // who: -1 player, else a name index
        s32 live = pph->score;
        if (heats()) {
            v[m] = ev_round == 1 ? pscore[0] + live : live; who[m++] = -1;
            for (int k = 0; k < 5; k++) { v[m] = ai_total(k); who[m++] = stand[k].rk; }
        } else if (nghost && (!nfield || fe_mp() || fe_shadow() || strstr(EVENTS[cur_event].kind, "rival"))) {
            v[m] = live; who[m++] = -1;
            for (int k = 0; k < nghost && m < 4; k++) { v[m] = ghosts[k].result; who[m++] = 100 + k; }
        } else {                                                    // the event's recorded scores (the "top 3" field)
            v[m] = live; who[m++] = -1;
            bool used[16] = { 0 }; used[ri % 16] = true;
            int gk[8]; for (int k = 0; k < nfield; k++) {              // the riders on the course: their own scores
                gk[k] = -1;
                for (int g = 0; g < nghost; g++) if (ghosts[g].result == field[k] && grd_char[g] >= 0 && !used[grd_char[g]]) { gk[k] = grd_char[g]; used[gk[k]] = true; break; }
            }
            int nx = ri;
            for (int k = 0; k < nfield && m < 9; k++) {
                if (gk[k] < 0) { do nx = (nx + 1) % NRIDERS; while (used[nx] && nx != ri); gk[k] = nx; used[nx] = true; }
                v[m] = field[k]; who[m++] = gk[k];
            }
        }
        for (int i = 0; i < m; i++) for (int j = i + 1; j < m; j++)
            if (v[j] > v[i] || (v[j] == v[i] && who[j] < 0)) { s32 x = v[i]; v[i] = v[j]; v[j] = x; int w = who[i]; who[i] = who[j]; who[j] = w; }
        for (int i = 0; i < m; i++) {
            SubRow *r = &rows[n++]; r->rank = i + 1; r->me = who[i] < 0;
            if (who[i] >= 100 && fe_mp()) snprintf(r->name, sizeof(r->name), "%.8s", net_s.ctl[ghosts[who[i] - 100].pid].name);
            else {
                int rk = who[i] < 0 ? ri : who[i] >= 100 ? grd_char[who[i] - 100] : who[i];
                char nm[12]; cap_name(nm, rk >= 0 && rk < NRIDERS ? rk : 0); snprintf(r->name, sizeof(r->name), "%s", nm);
            }
            snprintf(r->val, sizeof(r->val), "%ld", (long)v[i]);
        }
    }
    if (gate) {
        for (int i = 0; i < n; i++) { rows[i].dim = 1; rows[i].val[0] = 0; }
        memcpy(held, rows, sizeof(SubRow) * n); nheld = n;
    }
    submap_list(rows, n, NULL);
}

// rider select screen (frontend.c): the player's rider on a turntable
static Rider *fe_rider; static const s16 *fe_clip; static Phys fe_ph;
static void save_best(const int *best);
int fe_char_of(int r) { return CHAR_OF[r]; }
static int *fe_best;
void fe_save(void) { save_best(fe_best); }
void fe_apply_options(void) { sfx_volume(g_opt.master * g_opt.fx); music_volume(g_opt.master * 10); }
void fe_show_rider(int r)
{
    load_rider_meshes(fe_rider, r, true); fe_ph.character = CHAR_OF[r];
    int ch = CHAR_OF[r]; fe_ph.stance = (ch == 1 || ch == 2 || ch == 5 || ch == 8) ? 0x400 : 0;   // goofy: mirrored
}
// drawn centred on screen point (cx, cy), the rider about `h` pixels tall, playing animation id `anim` from its start
// when `restart` (the menu idle loops; a celebration plays through and loops as at the finish, 0x494a0)
void fe_draw_rider(int yaw, int cx, int cy, int h, int anim, bool restart)
{
    static int t;
    if (restart) t = 0;
    // the rider (~0.27 DS units tall, centre ~0.14 up) at a distance that makes it h pixels: the view is
    // 2 d tan(25.8) = 0.967 d units over 192 pixels
    float d = 0.27f * 192.0f / (0.967f * h);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    // move the picture's centre to (cx, cy): a clip-space translation (x' = x + tx w) shifts it after the divide
    glTranslatef32(floattof32((cx - 128) / 128.0f), floattof32(-(cy - 96) / 96.0f), 0);
    gluPerspective(51.6f, 256.0f / 192.0f, 0.05f, 40.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(0, 0.15f, d, 0, 0.14f, 0, 0, 1, 0);
    s32 up[3] = { 0, 0x4000, 0 };
    build_matrix(&fe_ph, up, yaw & 0x7ff);
    const Clip *cl = anim >= 0 && fe_clip[anim] >= 0 ? rider_clip(fe_rider, fe_clip[anim]) : NULL;
    if (!cl) cl = rider_clip(fe_rider, fe_clip[8]);            // "frontend": the menu idle
    float key = 0;
    if (cl) { t += 17; key = (t * 30.0f / 1000.0f) / cl->interval; }
    float rp[3] = { 0, 0, 0 };
    rider_fmt = POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_ID(1); glPolyFmt(rider_fmt);
    rider_board_cull = POLY_CULL_FRONT;    // with the board bone's own mirror (a regular stance), BACK: the menus' projection is not mirrored
    draw_rider(fe_rider, &fe_ph, rp, cl, key, true);
    glMatrixMode(GL_PROJECTION); glPopMatrix(1); glMatrixMode(GL_MODELVIEW);
}

// best times per event, kept next to the ROM when a FAT card (flashcart / DSi SD / emulator SD image) is there
#define have_fat have_fat_g
#define SAVE_NAME "ssx3gizwipe.sav"
static void save_best(const int *best)
{
    if (!have_fat) return;
    FILE *f = fopen(SAVE_NAME, "wb");
    if (!f) return;
    u32 magic = 0x34585353;                                       // "SSX4": best times + career + attributes + options
    fwrite(&magic, 4, 1, f); fwrite(best, 4, 15, f); fwrite(&career, sizeof(career), 1, f); fwrite(&g_opt, sizeof(g_opt), 1, f); fclose(f);
}
static void load_best(int *best)
{
    have_fat = fatInitDefault();
    if (!have_fat) return;
    // the saves sit beside the ROM. nitroFSInit (earlier) may already have run fatInitDefault - on a flashcart it reads
    // the ROM through FAT - and then made nitro:/ the current drive, so this call returned at once and every relative
    // path pointed into the read-only NitroFS: nothing (progress, options, language, shadows, voice clips) was ever
    // saved or loaded on hardware. Go to the ROM's folder (argv[0]) or the card's root.
    {
        char *cwd = fatGetDefaultCwd();
        if (!cwd || chdir(cwd) != 0) { const char *dr = fatGetDefaultDrive(); if (!dr || chdir(dr) != 0) chdir("fat:/"); }
        free(cwd);
    }
    FILE *f = fopen(SAVE_NAME, "rb");
    if (!f) return;
    u32 magic = 0;
    if (fread(&magic, 4, 1, f) == 1 && (magic == 0x31585353 || magic == 0x32585353 || magic == 0x33585353 || magic == 0x34585353)) fread(best, 4, 15, f);
    if (magic == 0x32585353 && fread(&career, CAREER_V1_SIZE, 1, f) != 1) career_defaults();
    if ((magic == 0x33585353 || magic == 0x34585353) && fread(&career, sizeof(career), 1, f) != 1) career_defaults();
    int on;
    if (magic == 0x34585353 && (on = fread(&g_opt, 1, sizeof(g_opt), f)) >= 3) {      // (stage 26-31 saves: 3 bytes, no fps30)
        if (on < 5 || g_opt.lang > 4) g_opt.lang = 0xff;                            // (before stage 42: no language yet)
        if (on < 6 || g_opt.rumble > 1) g_opt.rumble = 1;                           // (before stage 64: no rumble option)
        if (on < 8 || g_opt.voice > 1 || g_opt.vduck > 1) { g_opt.voice = 0; g_opt.vduck = 1; }   // (before stage 76: Voice Clips off)
        if (on < 9 || g_opt.board > 1) g_opt.board = 0;                              // (before stage 79: no custom board)
        if (g_opt.master > 10) g_opt.master = 10; if (g_opt.fx > 10) g_opt.fx = 10; if (g_opt.cam > 2) g_opt.cam = 1; if (g_opt.fps30 > 1) g_opt.fps30 = 0;
    }
    fclose(f);
}

// ---- chase camera (0x75808), run once per 40 ms step like the exe's, drawn interpolated
// target = the rider + 32 up, eased 1/4; distance = the camera setting's 288 / 352 / 416 over 3, times the momentum
// (0x18c0) on the ground, x 320/256 in the air, x 1.5 boosting, x 5/3 on the superpipe; eased 1/16 closer, 1/8
// farther. The camera sits that far back along the velocity (pitch held within ~41 degrees; below a walking pace,
// behind the rider's facing), 96 higher in the air, eased 1/8 across and 1/12 vertically, then set back to exactly
// that distance from the target.
static float camT[3], camC[3], camD, camT0[3], camC0[3]; static bool cam_ok, cam_occ_reset;
static float camV[3], camV0[3];
#ifdef TEST_CAMLOG
float camdbg[4][3];
#define CAMDBG(i, v) memcpy(camdbg[i], v, 12)
#else
#define CAMDBG(i, v)
#endif   // port: the camera as drawn - camC kept clear of the course (the state camC is not)
static void cam_step(const Phys *p, int setting, bool pipe)
{
    static const int SET[4] = { 288, 352, 416, 416 };             // 0xb0180: Options camera Near / Mid / Far
    float pos[3] = { p->pos[0] / 256.0f, p->pos[1] / 256.0f, p->pos[2] / 256.0f };
    float T[3] = { pos[0], pos[1] + 32, pos[2] };
    if (pipe) T[1] += p->up[1] / 16384.0f * 24;
    float base = SET[setting & 3];
    float d = p->air ? base * 320 / 256 / 3 : (base / 3) * p->momentum / 256;
    if (pipe) d = d * 5 / 3;
    if (p->boosting) d = d * 3 / 2;
    float dir[3];
    float vx = p->vel[0] / 16.0f, vy = p->vel[1] / 16.0f, vz = p->vel[2] / 16.0f, h2 = vx * vx + vz * vz;
    if (h2 <= 0x10000) { float a = p->facing * (6.2831853f / 2048.0f); dir[0] = sinf(a); dir[1] = 0; dir[2] = cosf(a); }
    else {
        if (vy * vy > h2 * 0.75f) vy = (vy < 0 ? -1 : 1) * sqrtf(h2 * 0.75f);
        float l = sqrtf(h2 + vy * vy); dir[0] = vx / l; dir[1] = vy / l; dir[2] = vz / l;
    }
    memcpy(camT0, camT, sizeof(camT)); memcpy(camC0, camC, sizeof(camC));
    if (!cam_ok) {
        memcpy(camT, T, sizeof(T)); camD = d;
        for (int k = 0; k < 3; k++) camC[k] = T[k] - dir[k] * d;
        memcpy(camT0, camT, sizeof(camT)); memcpy(camC0, camC, sizeof(camC)); cam_ok = true;
        memcpy(camV, camC, sizeof(camC)); memcpy(camV0, camC, sizeof(camC));
        cam_occ_reset = true;
        return;
    }
    for (int k = 0; k < 3; k++) camT[k] = (camT[k] * 3 + T[k]) / 4;
    camD = d < camD ? (camD * 15 + d) / 16 : (camD * 7 + d) / 8;
    float C[3]; for (int k = 0; k < 3; k++) C[k] = camT[k] - dir[k] * camD;
    if (p->air) C[1] += 96;
    camC[0] = (camC[0] * 7 + C[0]) / 8; camC[1] = (camC[1] * 11 + C[1]) / 12; camC[2] = (camC[2] * 7 + C[2]) / 8;
    float e[3] = { camT[0] - camC[0], camT[1] - camC[1], camT[2] - camC[2] }, l = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
    if (l > 1) for (int k = 0; k < 3; k++) camC[k] = camT[k] - e[k] / l * camD;
    // 0x75808 (0x6829c against the course): a ray from the target out to the camera; if the course is in the way
    // (surface types 3 / 7 / 11 / 15 excepted, table 0xb0140) the camera comes in to just in front of it. Without
    // this the camera went through tunnel roofs and walls, and the course behind them showed as holes.
    // port: the camera drawn (camV) comes in at once when the course is in the way and goes back out over a few
    // steps; the chase state (camC) is left alone, so the ray's answer does not depend on where it put the camera
    // the step before. Pulling camC itself in made the next ray miss, the camera eased out, the ray hit again: along
    // rails, fences and the city's walls the camera jumped 80-180 units back and forth (judder).
    static float occ_d;
    if (cam_occ_reset) { occ_d = 0; cam_occ_reset = false; }
    memcpy(camV0, camV, sizeof(camV)); memcpy(camV, camC, sizeof(camC));
    if (p->trk && l > 1) {
        s32 a[3], b[3], h[3], n[3]; int sf = 0;
        float want = 1e9f;
        for (int k = 0; k < 3; k++) { a[k] = (s32)camT[k]; b[k] = (s32)(camC[k] - e[k] / l * 24); }
        if (track_ray((Track *)p->trk, a, b, h, n, &sf) && (sf & 3) != 3) {
            float hd = 0; for (int k = 0; k < 3; k++) hd += (h[k] - camT[k]) * (h[k] - camT[k]);
            hd = sqrtf(hd) - 24; if (hd < 16) hd = 16;
            want = hd;
        }
        if (occ_d <= 0) occ_d = want < l ? want : l;
        if (want < occ_d) occ_d = want;
        else { float to = want < l ? want : l; occ_d += (to - occ_d) / 5; }
        if (occ_d < l) for (int k = 0; k < 3; k++) camV[k] = camT[k] - e[k] / l * occ_d;
    }
}

// 0x75808 while the race is in state 0 (0x72758: waiting at the gate for the start button): the camera swings
// +-45 degrees about the rider, 512 units behind it (-Z of its matrix) and 150 up, looking at it. The angle runs
// 0..0x3ff (+4 per update) as a triangle wave. The chase camera then eases from there (param_2 set: no snap).
static void cam_intro(const Phys *p, int t)
{
    t &= 0x3ff; int a = t < 0x200 ? t - 0x100 : 0x300 - t;
    float bx = -p->mat[0][2] * 512.0f / 16384, by = -p->mat[1][2] * 512.0f / 16384, bz = -p->mat[2][2] * 512.0f / 16384;
    float sn = sinf(a * (6.2831853f / 2048)), cs = cosf(a * (6.2831853f / 2048));
    float ox = bx * cs - bz * sn, oz = bx * sn + bz * cs;
    float pos[3] = { p->pos[0] / 256.0f, p->pos[1] / 256.0f, p->pos[2] / 256.0f };
    memcpy(camT0, camT, sizeof(camT)); memcpy(camC0, camC, sizeof(camC));
    memcpy(camT, pos, sizeof(pos));
    camC[0] = pos[0] + ox; camC[1] = pos[1] + by + 150; camC[2] = pos[2] + oz;
    camD = 117;
    if (!cam_ok) { memcpy(camT0, camT, sizeof(camT)); memcpy(camC0, camC, sizeof(camC)); cam_ok = true; }
    memcpy(camV0, camC0, sizeof(camC)); memcpy(camV, camC, sizeof(camC)); cam_occ_reset = true;
}

// ---- replay (results "Replay": 0x778a4 starts it, 0x77fd8 steps it - A pauses, R fast-forwards x4). The player's
// run plays back from the shadow recording,[...]from theirs. Camera (0x76514, 0x2cef0): 0 = trackside,
// from the nearest 'camera' marker (xz): 1000 units from the rider toward it and 150 above the higher of the two,
// eased 1/4; 1 = the chase camera. Y switches.
static bool show_rec;                   // results: the Records panel (Y)
static Ghost rpg; static bool replay, rp_pause, rp_cam_ok, watch, rp_slow, rp_close; static int rp_tick, rp_end, rp_fwd1, rp_back1; static int rp_cam, rp_steps;
static void cam_replay(const Track *t, const Phys *p)
{
    float T[3] = { p->pos[0] / 256.0f, p->pos[1] / 256.0f, p->pos[2] / 256.0f };
    int best = -1; float bd = 1e30f;
    for (u32 i = 0; i < t->h.nmarkers; i++) {
        if (strcmp(t->markers[i].name, "camera")) continue;
        float dx = t->markers[i].pos[0] - T[0], dz = t->markers[i].pos[2] - T[2], d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    float C[3];
    if (best < 0) { float a = p->facing * (6.2831853f / 2048.0f); C[0] = T[0] - sinf(a) * 300; C[1] = T[1] + 150; C[2] = T[2] - cosf(a) * 300; }
    else {
        const s32 *m = t->markers[best].pos;
        float d[3] = { m[0] - T[0], m[1] - T[1], m[2] - T[2] }, l = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (l < 1) l = 1;
        for (int k = 0; k < 3; k++) C[k] = T[k] + d[k] / l * 1000;
        C[1] = (C[1] > T[1] ? C[1] : T[1]) + 150;
    }
    memcpy(camT0, camT, sizeof(camT)); memcpy(camC0, camC, sizeof(camC));
    if (!rp_cam_ok) { memcpy(camT, T, sizeof(T)); memcpy(camC, C, sizeof(C)); memcpy(camT0, T, sizeof(T)); memcpy(camC0, C, sizeof(C)); memcpy(camV, C, sizeof(C)); memcpy(camV0, C, sizeof(C)); rp_cam_ok = true; return; }
    for (int k = 0; k < 3; k++) { camT[k] = (camT[k] * 3 + T[k]) / 4; camC[k] = (camC[k] * 3 + C[k]) / 4; }
    memcpy(camV0, camC0, sizeof(camC)); memcpy(camV, camC, sizeof(camC));
}

// ----[...]splash (eaanim.bmp, 75 frames of 256 x 240, with EASplash.wav: 2.5 s, so 30 fps), shown
// on the top screen as an 8-bit bitmap before anything is uploaded to bank A; A / START skip it
static void ea_splash(void)
{
    FILE *f = fopen("nitro:/menu/eaanim.ssxe", "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    u8 *d = malloc(n), *pix = malloc(256 * 192 + 4);
    if (!d || !pix || fread(d, 1, n, f) != (size_t)n) { fclose(f); free(d); free(pix); return; }
    fclose(f);
    int nf = d[4] | (d[5] << 8);
    setBrightness(2, -16);                                          // the bottom screen dark meanwhile
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    REG_BG3CNT = BG_BMP8_256x256 | BG_BMP_BASE(0) | BG_PRIORITY(0);
    REG_BG3PA = 256; REG_BG3PB = 0; REG_BG3PC = 0; REG_BG3PD = 256; REG_BG3X = 0; REG_BG3Y = 0;
    REG_DISPCNT = MODE_5_2D | DISPLAY_BG3_ACTIVE;
    dmaCopy(d + 12, BG_PALETTE, 512);
    const u8 *p = d + 12 + 512;                                     // every frame 4-aligned (swiDecompress wants it)
    sfx_play_name("EASplash");
    u32 t0 = vblanks;
    for (int i = 0; i < nf; i++) {
        u32 sz = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
        {   // DS LZ77 (type 0x10), unpacked here (the BIOS call hung on melonDS's HLE BIOS)
            const u8 *q = p + 8; u32 len = (p[5] | (p[6] << 8) | (p[7] << 16)), o = 0;
            while (o < len) {
                u8 fl = *q++;
                for (int bit = 0; bit < 8 && o < len; bit++) {
                    if (fl & (0x80 >> bit)) {
                        int l = (q[0] >> 4) + 3, dd = (((q[0] & 15) << 8) | q[1]) + 1; q += 2;
                        while (l-- && o < len) { pix[o] = pix[o - dd]; o++; }
                    } else pix[o++] = *q++;
                }
            }
        }
        p += 4 + sz;
        while (vblanks - t0 < (u32)i * 2) swiWaitForVBlank();      // 30 fps against the sound
        DC_FlushRange(pix, 256 * 192);
        dmaCopy(pix, BG_BMP_RAM(0), 256 * 192);
        scanKeys();
        if (keysDown() & (KEY_A | KEY_START)) break;
    }
    for (int k = 0; k < 20; k++) swiWaitForVBlank();                // hold the last frame a moment
    free(d); free(pix);
    vramSetBankA(VRAM_A_TEXTURE);                                   // back to the 3D set-up
    videoSetMode(MODE_0_3D);
    setBrightness(2, 0);
}

// the boot stills after the EA animation (0x1f960): the developer's logo, then the PEGI 3+ rating, 4 s each with a
// fade (A / START skips one)
static void boot_still(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char m[4]; u16 wh[2]; u16 *pal = malloc(512); u8 *px = malloc(256 * 192);
    if (!pal || !px) { fclose(f); free(pal); free(px); return; }
    fread(m, 4, 1, f); fread(wh, 2, 2, f); fread(pal, 2, 256, f); fread(px, 1, 256 * 192, f); fclose(f);
    setBrightness(3, -16);
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    REG_BG3CNT = BG_BMP8_256x256 | BG_BMP_BASE(0) | BG_PRIORITY(0);
    REG_BG3PA = 256; REG_BG3PB = 0; REG_BG3PC = 0; REG_BG3PD = 256; REG_BG3X = 0; REG_BG3Y = 0;
    REG_DISPCNT = MODE_5_2D | DISPLAY_BG3_ACTIVE;
    DC_FlushRange(pal, 512); DC_FlushRange(px, 256 * 192);
    dmaCopy(pal, BG_PALETTE, 512); dmaCopy(px, BG_BMP_RAM(0), 256 * 192);
    free(pal); free(px);
    const int T = 240, FD = 24;                                     // 4 s, fades of 0.4 s
    for (int t = 0; t < T; t++) {
        int b = t < FD ? -16 + t * 16 / FD : t > T - FD ? -16 + (T - t) * 16 / FD : 0;
        setBrightness(1, b);
        swiWaitForVBlank();
        scanKeys();
        if ((keysDown() & (KEY_A | KEY_START)) && t < T - FD) t = T - FD;
    }
    setBrightness(3, -16);
    vramSetBankA(VRAM_A_TEXTURE);
    videoSetMode(MODE_0_3D);
}

static const char *ordinal(int n) { static const char *O[] = { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th" }; return O[n < 1 ? 0 : n > 9 ? 8 : n - 1]; }

int main(int argc, char **argv)
{
    // bottom-screen console in bank H (32 KB: font tiles at 0, map at 16 KB), leaving bank C to the textures
    videoSetModeSub(MODE_0_2D);
    vramSetBankH(VRAM_H_SUB_BG);
    PrintConsole *con = consoleInit(NULL, 0, BgType_Text4bpp, BgSize_T_256x256, 8, 0, false, true);
    REG_DISPCNT_SUB &= ~DISPLAY_BG0_ACTIVE;              // port: the debug text stays hidden (SELECT + DOWN in a race)
    soundEnable();
    setup_3d();
    rumble_init();
    irqSet(IRQ_VBLANK, on_vblank);
    irqEnable(IRQ_VBLANK);

    printf("SSX 3 GizWipe - DS\n");
    if (!nitroFSInit(NULL)) { printf("nitroFSInit failed\n"); while (1) swiWaitForVBlank(); }

    glLight(0, RGB15(31, 31, 29), floattov10(-0.40f), floattov10(-0.85f), floattov10(-0.35f));
    glMaterialf(GL_AMBIENT, RGB15(19, 19, 21));
    glMaterialf(GL_DIFFUSE, RGB15(26, 26, 26));
    glMaterialf(GL_SPECULAR, RGB15(0, 0, 0));
    glMaterialf(GL_EMISSION, RGB15(0, 0, 0));

    if (!sfx_open("nitro:/sfx.ssfx")) printf("sfx.ssfx missing\n");
    ea_splash();
    // (port: no PEGI rating still - boot_still is kept for any other boot picture)
    (void)boot_still;
    setBrightness(3, 0);
    lang_open();
    if (!hud_open("nitro:/hud.ssxh")) printf("hud.ssxh missing\n");
    static Rider rider; int ri = 0; bool hi = false; player_rider = &rider;
    if (!rider_open(&rider)) { while (1) swiWaitForVBlank(); }
    submap_open(con);                                              // the bottom-screen trail map's backdrop
    load_rider_meshes(&rider, ri, hi);
    // animation id -> clip, and the clip lengths for the physics' animation timing
    static s16 id_clip[NANIM_IDS]; static u16 id_ms[NANIM_IDS];
    for (int i = 0; i < NANIM_IDS; i++) {
        id_clip[i] = ANIM_NAME[i][0] ? rider_clip_index(&rider, ANIM_NAME[i]) : -1;
        id_ms[i] = id_clip[i] >= 0 ? rider.recs[id_clip[i]].length * 1000 / 30 : 0;
    }
    phys_set_anim_lengths(id_ms, NANIM_IDS);
    {
        static const char *PN[6] = { "speed_boost", "trick_boost", "x2", "x3", "x5", "x10" };
        for (int i = 0; i < 6; i++) rider_load_mesh(&rider, &m_pick[i], PN[i]);
        rider_load_mesh(&rider, &m_tree, "tree_low");
        load_glare();
    }
    static int best_ms[15];                                       // per event
    bool in_menu = true;
    career_defaults();
    load_best(best_ms); shadow_load_all(); fe_apply_options(); voice_init(have_fat_g); board_init(have_fat_g);
    fe_rider = &rider; fe_clip = id_clip; fe_best = best_ms;
    bool uber_tip_done = false;
    int race = R_INTRO, intro_ms = 0, count_ms = 0, race_steps = 0, fin_time = 0, spin = 0, place = 1, fin_place = 0; bool new_best = false;

    int ti = 0; Track trk; static Phys ph;
    bool paused = false, p_opt = false; int p_sel = 0, p_osel = 0; bool adaptive = true, autopilot = false;
#ifdef TEST_LOG
    autopilot = true;
#endif
    int bias = 0;
    int last_polys = 0, last_verts = 0, near_polys = 0, near_verts = 0; bool near_read = false; u32 last_vb = 0; float fps = 60;
    float prev_pos[3], cur_pos[3], cam_pos[3];
    int acc_us = 0, steps_frame = 0, phys_us = 0, frame_us = 0;
    int star_pts = 0, star_dx = 0, star_dy = 0;          // landed trick's rating stars flying off (0x46b5c 0x418 / 0x43c / 0x440)
    int combo_drain = 0, drain_step = 10, drain_lvl = 1, seen_bonus = 0, combo_pts = 0, prev_lvl = 1;   // port: a combo's bonus counting into the score
    int trick_msg_t = 0, trick_msg_pts = 0, trick_msg_id = -1, last_score = 0; char trick_msg_text[48] = "";

    // race start: spawn marker 0, countdown 3-2-1-GO (0x46b5c), timer until[...]
    #define START_REPLAY(buf, len) do { if (ghost_from_mem(&rpg, (buf), (len))) { \
        rpg.ph.character = CHAR_OF[ri]; ghost_start(&rpg, &trk, 0); \
        for (int k = 0; k < nghost; k++) { ghost_start(&ghosts[k], &trk, ghosts[k].shadow ? 1 : k + 1); \
            for (int j = 0; j < 3; j++) gprev[k][j] = gcur[k][j] = ghosts[k].ph.pos[j] / 256.0f; gori[k].ok = 0; } \
        for (int k = 0; k < 3; k++) prev_pos[k] = cur_pos[k] = rpg.ph.pos[k] / 256.0f; \
        replay = true; rp_pause = false; rp_cam_ok = false; cam_ok = false; rp_steps = 0; rp_slow = false; rp_close = false; rp_end = 0; } } while (0)
    #define RESET_RIDER() do { music_pause(vblanks); replay = false; show_rec = false; track_update_stream(&trk, 0, 99); ph.character = CHAR_OF[ri]; memcpy(ph.attr, career.attr[CHAR_OF[ri]], 7); \
        setup_ghosts(&trk, cur_event, ri, &rider); fin_place = 0; place = 1; \
        for (int k = 0; k < 4; k++) trail_reset(&trails[k]); \
        phys_place(&ph, &trk, 0); combo_drain = 0; combo_pts = 0; seen_bonus = ph.bonus_n; uber_tip_done = false; race = R_INTRO; intro_ms = 0; count_ms = 0; race_steps = 0; fin_time = 0; \
        for (int k = 0; k < 3; k++) prev_pos[k] = cur_pos[k] = ph.pos[k] / 256.0f; cam_ok = false; fw_init(&fw, &trk); music_resume(vblanks); } while (0)
    memset(&trk, 0, sizeof(trk));
    // the exe's play list (music.c), from Fatboy Slim's "Don't L[...]" (song 11, Music_16): the
    // exe's first pick at boot comes from its fixed random table, then the songs shuffle as each one ends
    music_playlist(11, vblanks);
    g_lang = g_opt.lang <= 4 ? g_opt.lang : 0;
    fe_enter(g_opt.lang <= 4 ? FE_TITLE : FE_LANG);              // first boot: the language select (0x1f960 state 2)
    #define MAP_ON() submap_begin(&trk, EVENTS[cur_event].name, fe_mp() ? "Multiplayer" : fe_shadow() ? XT_32 : fe_career() ? XT_30 : XT_31)
    #define START_EVENT(ev) do { music_pause(vblanks); cur_event = (ev); for (int k = 0; k < NTRACKS; k++) if (!strcmp(TRACKS[k], EVENTS[cur_event].track)) ti = k; \
        load_track_n(&trk, ti); set_event_fog(cur_event); bias = 0; ph.trk = NULL; ev_round = 0; memset(pscore, 0, sizeof(pscore)); RESET_RIDER(); stand_init(ri); paused = false; consoleClear(); print_help(); music_next(vblanks); } while (0)   /* a new course: a random different song */
    #define QUIT_TO_MENU() do { submap_release(); replay = false; fw_free(&fw); track_free(&trk); sky_free(); setup_fog(); glClearColor(19, 22, 27, 31); memset(&trk, 0, sizeof(trk)); sfx_loop(-1); in_menu = true; \
        load_rider_meshes(&rider, ri, hi); fe_enter(FE_EVENT); } while (0)

    while (1) {
        scanKeys();
        u32 down = keysDown(), held = keysHeld();
        rumble_live = in_menu || (!paused && !replay);
        {   // the Radio BIG strip on the bottom screen (menus and races)
            static u32 trax_vb; u32 v = vblanks; int d = (int)(v - trax_vb); trax_vb = v;
            submap_trax(music_song_name(), d > 0 && d < 10 ? d : 1);
        }
        net_update();
        if (in_menu) {                                             // front end (frontend.c)
            music_update(vblanks, false);
            glLoadIdentity();
            int ev = cur_event, r = ri;
            bool go = fe_frame(down, &ev, &r, NRIDERS, RIDERS, best_ms);
            cur_event = ev;
            if (go || !fe_hold()) {
                while (GFX_STATUS & BIT(27));
                glFlush(0); fe_flushed();
            }
            swiWaitForVBlank();
            if (go && fe_watch() && shadow[cur_event]) {          // Watch Shadow: the event with nobody else, replaying it
                int rk = ((s32 *)shadow[cur_event])[1]; ri = rk >= 0 && rk < NRIDERS ? rk : 0;
                music_pause(vblanks); load_rider_meshes(&rider, ri, hi); in_menu = false; START_EVENT(cur_event); MAP_ON(); music_resume(vblanks);
                for (int k = 0; k < nghost; k++) ghost_free(&ghosts[k]);
                nghost = 0; race = R_DONE; ph.race_over = true; watch = true;
                START_REPLAY(shadow[cur_event], shadow_len[cur_event]);
            } else if (go) { ri = r; music_pause(vblanks); load_rider_meshes(&rider, ri, hi); in_menu = false; START_EVENT(cur_event); MAP_ON(); music_resume(vblanks); }
            continue;
        }
        if (paused && !(held & KEY_SELECT)) {                       // pause menu (0x31254): Return / Restart / Options / Give Up
            if (!p_opt) {
                if (down & KEY_UP) p_sel = (p_sel + 3) % 4;
                if (down & KEY_DOWN) p_sel = (p_sel + 1) % 4;
                if (down & KEY_B) { paused = false; continue; }
                if (down & KEY_A) {
                    if (p_sel == 0) { paused = false; continue; }
                    if (p_sel == 1 && fe_mp()) { down = 0; }                     // a session can't restart alone
                    else if (p_sel == 1) { RESET_RIDER(); paused = false; continue; }
                    if (p_sel == 2) { p_opt = true; p_osel = 0; down = 0; }
                    else if (p_sel == 3 && fe_mp()) {                    // Give Up: the host ends the session for all
                        if (net_is_host()) { net_s.phase = NP_END; for (int i = 0; i < 20; i++) { net_update(); swiWaitForVBlank(); } }
                        paused = false; net_leave(); QUIT_TO_MENU(); fe_enter(FE_MP); continue;
                    }
                    else {
                    QUIT_TO_MENU(); continue; }
                }
                down &= KEY_START;                                  // the rest were the menu's
            } else {                                                // Options, as on the menus' Options screen
                u8 *v[5] = { &g_opt.master, &g_opt.fx, &g_opt.cam, &g_opt.fps30, &g_opt.rumble };
                static const u8 mx[5] = { 10, 10, 2, 1, 1 };
                if (down & KEY_UP) p_osel = (p_osel + 4) % 5;
                if (down & KEY_DOWN) p_osel = (p_osel + 1) % 5;
                if ((down & KEY_LEFT) && *v[p_osel] > 0) { (*v[p_osel])--; fe_apply_options(); }
                if ((down & KEY_RIGHT) && *v[p_osel] < mx[p_osel]) { (*v[p_osel])++; fe_apply_options(); }
                if (down & (KEY_A | KEY_B | KEY_START)) { fe_save(); p_opt = false; }
                down = 0;                                           // the keys were the menu's
            }
        }
        if (held & KEY_SELECT) {
            if (down & (KEY_R | KEY_L)) {                          // next / previous event (debug)
                track_free(&trk);
                START_EVENT((down & KEY_R) ? (cur_event + 1) % 15 : (cur_event + 14) % 15);
                if (submap_active()) MAP_ON();
                continue;
            }
            if (down & KEY_A) { ri = (ri + 1) % NRIDERS; load_rider_meshes(&rider, ri, hi); ph.character = CHAR_OF[ri]; memcpy(ph.attr, career.attr[CHAR_OF[ri]], 7); }
            if (down & KEY_START) { hi = !hi; load_rider_meshes(&rider, ri, hi); }
            if (down & KEY_B) RESET_RIDER();
            if (down & KEY_X) autopilot = !autopilot;
            if (down & KEY_Y) g_opt.cam = (g_opt.cam + 1) % 3;
            if (down & KEY_UP) { phys_spawn(&ph, &trk, ph.s); for (int k = 0; k < 3; k++) prev_pos[k] = cur_pos[k] = ph.pos[k] / 256.0f; }
            if (down & KEY_DOWN) { if (submap_active()) submap_end(); else MAP_ON(); }   // trail map / debug text
            if ((down & KEY_RIGHT) && nghost) {                    // debug: bring the first ghost alongside (punch tests)
                for (int j = 0; j < 3; j += 2) ghosts[0].off[j] += ((ph.pos[j] - ghosts[0].ph.pos[j]) >> 8) + (j == 0 ? 60 : 0);
                ghosts[0].cur256 = ghosts[0].cur << 8;
            }
            if (down & KEY_LEFT) track_cull = track_cull == POLY_CULL_NONE ? POLY_CULL_BACK : track_cull == POLY_CULL_BACK ? POLY_CULL_FRONT : POLY_CULL_NONE;
        } else if (race == R_INTRO && !paused && fe_mp()) {       // a session: the host's go starts every console together
            net_ctl.ready = 1;                                     // loaded and at the gate
            if (net_is_host() && net_s.phase == NP_LOAD) {
                bool all = true;
                for (int p = 1; p < NET_MAX; p++) if (((net_s.mask >> p) & 1) && net_s.ctl[p].ready != 1) all = false;
                if (all && intro_ms > 1500) { net_s.phase = NP_RACE; net_s.go = 1; }
            }
            if (net_s.go) { race = R_COUNT; count_ms = 0; rec_begin(&ph); }
        } else if (race == R_INTRO && !paused) {                  // 0x72758 state 0: A / START after 500 ms starts the countdown
            if ((down & (KEY_A | KEY_START)) && intro_ms > 500) { race = R_COUNT; count_ms = 0; rec_begin(&ph); }
        } else if (down & KEY_START) { paused = !paused; p_sel = 0; p_opt = false; if (paused) sfx_loop(-1); }
        else if (replay) {                                          // replay controls (0x77fd8)
            if (down & KEY_A) rp_pause = !rp_pause;                // 0x10
            if (down & KEY_L) rp_slow = !rp_slow;                  // 0x100: slow motion (the legend's L)
            if (down & KEY_R) { rp_cam ^= 1; rp_cam_ok = false; cam_ok = false; }   // 0x200: camera (R)
            if (rp_pause && (down & KEY_RIGHT)) rp_fwd1 = 1;
            if (rp_pause && (down & KEY_LEFT)) rp_back1 = 1;
            if (((down & KEY_B) || rp_close) && watch) { watch = false; rp_close = false; QUIT_TO_MENU(); fe_enter(FE_SHADOWS); continue; }
            if ((down & KEY_B) || rp_close) { replay = false; rp_close = false; cam_ok = false; for (int k = 0; k < 3; k++) prev_pos[k] = cur_pos[k] = ph.pos[k] / 256.0f; }
        }
        else if (fe_mp() && !in_menu && (net_s.phase == NP_END || (!net_is_host() && net_age(0) > 180))) {   // the session is over
            fe_mp_message(net_s.phase == NP_END ? XT_40 : "Connection lost");
            net_leave(); QUIT_TO_MENU(); fe_enter(FE_MP); continue;
        }
        else if (fe_mp() && !net_is_host() && (net_s.phase == NP_EVENT || net_s.phase == NP_LOBBY)) {   // the host chose a new event
            QUIT_TO_MENU(); fe_mp_rematch(); continue;
        }
        else if (fe_mp() && race == R_DONE && ph.race_over) {     // [...]: the host goes on or ends it
            bool all = true;
            for (int k = 0; k < nghost; k++) if (!ghosts[k].finished && net_age(ghosts[k].pid) < 180) all = false;
            if (net_is_host() && all && (down & KEY_A)) { QUIT_TO_MENU(); fe_mp_rematch(); continue; }
            if (down & KEY_B) {
                if (net_is_host()) { net_s.phase = NP_END; for (int i = 0; i < 20; i++) { net_update(); swiWaitForVBlank(); } }
                net_leave(); QUIT_TO_MENU(); fe_enter(FE_MP); continue;
            }
        }
        else if (race == R_DONE && ph.race_over && (down & KEY_Y)) show_rec = !show_rec;   // Records
        else if (race == R_DONE && ph.race_over && (down & KEY_X) && rec && rec_n > 40) {   // Replay (0x778a4)
            START_REPLAY(rec, 48 + rec_n * 12);
            continue;
        }
        // port: once the event is over, A goes back to the menu and B runs it again (A restarted it); between
        // heats A still goes on to the next heat /[...] (0x7171c)
        else if (race == R_DONE && ph.race_over && (down & KEY_A) && heats() && round_ok && ev_round < 2) {
            ev_round++;
            RESET_RIDER(); continue;
        }
        else if (race == R_DONE && ph.race_over && (down & KEY_A)) { QUIT_TO_MENU(); continue; }
        else if (race == R_DONE && ph.race_over && (down & KEY_B) && heats() && round_ok && ev_round < 2) { QUIT_TO_MENU(); continue; }
        else if (race == R_DONE && ph.race_over && (down & KEY_B)) {
            if (heats() && round_ok && ev_round == 2) { ev_round = 0; memset(pscore, 0, sizeof(pscore)); stand_init(ri); }
            RESET_RIDER(); continue;
        }

        int line0 = NOW_LINE();
        // fixed 40 ms physics steps (as the original), rendered with interpolation
        u32 vb = vblanks; u32 dvb = vb - last_vb; last_vb = vb;
        if (dvb > 6) dvb = 6;
        if (dvb) fps = fps * 0.9f + (59.8261f / dvb) * 0.1f;
        steps_frame = 0;
        static int php[5]; memset(php, 0, sizeof(php));
        if (fe_mp()) {                                             // the session's pause (Gizmondo: "[...]")
            net_ctl.pause = paused;
            if (net_is_host()) {
                int pb = net_s.paused_by;
                if (pb != 0xff && !(pb == 0 ? net_ctl.pause : net_s.ctl[pb].pause)) pb = 0xff;
                if (pb == 0xff) for (int p = 0; p < NET_MAX; p++) if (((net_s.mask >> p) & 1) && (p == 0 ? net_ctl.pause : net_s.ctl[p].pause)) { pb = p; break; }
                net_s.paused_by = pb;
            }
        }
        bool mp_hold = fe_mp() && net_s.paused_by != 0xff && race != R_INTRO;
        if (mp_hold && !paused) sfx_loop(-1);
        if (!paused && !mp_hold) {
            acc_us += dvb * 16715;                                 // 1 / 59.8261 Hz
            int pp0 = NOW_LINE();
            u32 b = read_buttons(held);
            if (autopilot && race == R_RACE) b |= bot_buttons(&ph, 700);
            php[0] += NOW_LINE() - pp0;
            while (acc_us >= PHYS_TICK_MS * 1000) {
                acc_us -= PHYS_TICK_MS * 1000;
                cpuStartTiming(0);
                for (int k = 0; k < 3; k++) prev_pos[k] = cur_pos[k];
                memcpy(prev_ori.mat, ph.mat, sizeof(ph.mat)); prev_ori.pitch = ph.pitch; prev_ori.lean = ph.lean; prev_ori.anim = ph.anim;
                trk.rays = trk.tri_tests = 0;
                spin += 0x16;
                if (replay) {                                      // replay: everyone from their recordings (0x77fd8)
                    // forward 1 a step (slow motion: every other step), R held x4; L held rewinds x4; paused, a tap of
                    // R / L steps one frame
                    rp_tick++;
                    int n = rp_pause ? rp_fwd1 : (held & KEY_LEFT) ? 0 : (held & KEY_RIGHT) ? 4 : (rp_slow && (rp_tick & 1)) ? 0 : 1;
                    int m = rp_pause ? rp_back1 : (held & KEY_LEFT) ? 4 : 0;
                    rp_fwd1 = rp_back1 = 0;
                    if (m && rp_steps > m) {
                        rp_steps -= m;
                        ghost_rewind(&rpg, &trk, m);
                        for (int k = 0; k < nghost; k++) {
                            for (int j = 0; j < 3; j++) gprev[k][j] = gcur[k][j];
                            ghost_rewind(&ghosts[k], &trk, m);
                            for (int j = 0; j < 3; j++) gcur[k][j] = ghosts[k].ph.pos[j] / 256.0f;
                        }
                        n = 0;
                    }
                    if (!n && !m) for (int k = 0; k < nghost; k++) for (int j = 0; j < 3; j++) gprev[k][j] = gcur[k][j];
                    if (!n) {                                      // held still (slow motion's off step, paused, rewound): no turn
                        rpori.ok = 0; for (int k = 0; k < nghost; k++) gori[k].ok = 0;
                    }
                    for (int it = 0; it < n; it++) {
                        rp_steps++;
                        if (it == n - 1) {
                            memcpy(rpori.mat, rpg.ph.mat, sizeof(rpori.mat)); rpori.pitch = rpg.ph.pitch; rpori.lean = rpg.ph.lean;
                            rpori.anim = rpg.ph.anim; rpori.phase = rpg.phase; rpori.ok = n == 1;
                            for (int k = 0; k < nghost; k++) {
                                memcpy(gori[k].mat, ghosts[k].ph.mat, sizeof(gori[k].mat)); gori[k].pitch = ghosts[k].ph.pitch;
                                gori[k].lean = ghosts[k].ph.lean; gori[k].anim = ghosts[k].ph.anim; gori[k].phase = ghosts[k].phase; gori[k].ok = n == 1;
                            }
                        }
                        ghost_step(&rpg, &trk);
                        for (int k = 0; k < nghost; k++) {
                            if (it == n - 1) for (int j = 0; j < 3; j++) gprev[k][j] = gcur[k][j];
                            ghost_step(&ghosts[k], &trk);
                            for (int j = 0; j < 3; j++) gcur[k][j] = ghosts[k].ph.pos[j] / 256.0f;
                        }
                    }
                    // at the end it waits 40 updates without input, then closes (0x4b4)
                    if (rpg.finished && !rp_pause && !(held & (KEY_LEFT | KEY_RIGHT))) { if (++rp_end >= 40) rp_close = true; } else rp_end = 0;
                    if (n > 1 || m) for (int k = 0; k < 3; k++) prev_pos[k] = rpg.ph.pos[k] / 256.0f;   // no smear when skipping
                    for (int k = 0; k < 3; k++) cur_pos[k] = rpg.ph.pos[k] / 256.0f;
                    track_update_stream(&trk, rpg.s, 2);
                    rpg.ph.momentum = 256;
                    if (rp_cam == 0) cam_replay(&trk, &rpg.ph); else cam_step(&rpg.ph, g_opt.cam, rpg.ph.pipe);
                    steps_frame++;
                    phys_us = timerTicks2usec(cpuEndTiming());
                    continue;
                }
                if (race == R_INTRO) {
                    intro_ms += PHYS_TICK_MS; phys_hold(&ph, 19);
                    // port:[...]in the gate as the player (0x72758 puts every rider in g_gaterock from
                    // half way, held at its end) - they stood frozen at the clip's first pose until the countdown
                    int ph8 = ph.anim == 19 && ph.anim_len ? ph.anim_ms * 255 / ph.anim_len : 128;
                    for (int k = 0; k < nghost; k++) if (!ghosts[k].remote && ghosts[k].ph.anim == 19) ghosts[k].phase = ph8;
                }
                if (race == R_COUNT) {                             // countdown: Count at 0/1/2 s, Start + GO at 3 s
                    if (count_ms >= 0 && count_ms < 3000 && count_ms % 1000 == 0) sfx_play(SND_COUNT);
                    count_ms += PHYS_TICK_MS;
                    if (count_ms >= 3000) { race = R_RACE; sfx_play(SND_START); }
                    else phys_hold(&ph, 19);                       // g_gaterock
                }
                int pp1 = NOW_LINE();
                if (race == R_RACE || race == R_DONE || (race == R_COUNT && count_ms > 0))   // the recordings start with the countdown
                    for (int k = 0; k < nghost; k++) {
                        for (int j = 0; j < 3; j++) gprev[k][j] = gcur[k][j];
                        memcpy(gori[k].mat, ghosts[k].ph.mat, sizeof(gori[k].mat)); gori[k].pitch = ghosts[k].ph.pitch;
                        gori[k].lean = ghosts[k].ph.lean; gori[k].anim = ghosts[k].ph.anim; gori[k].phase = ghosts[k].phase; gori[k].ok = 1;
                        if (ghosts[k].remote) net_pose(&ghosts[k]); else ghost_step(&ghosts[k], &trk);
                        for (int j = 0; j < 3; j++) gcur[k][j] = ghosts[k].ph.pos[j] / 256.0f;
                    }
                int pp2 = NOW_LINE(); php[1] += pp2 - pp1;
                if (race == R_RACE || race == R_DONE) phys_step(&ph, b);
                int pp3 = NOW_LINE(); php[2] += pp3 - pp2;
                if (race == R_RACE || race == R_DONE) {            // the boards' snow trails (0x785b4)
                    trail_step(&trails[0], &ph, trail_width(&ph, true));
                    for (int k = 0; k < nghost; k++) trail_step(&trails[1 + k], &ghosts[k].ph, ghosts[k].finished ? 0 : trail_width(&ghosts[k].ph, false));
                }
                int pp4 = NOW_LINE(); php[3] += pp4 - pp3;
                if (race == R_COUNT || race == R_RACE) rec_frame(&ph);   // the shadow recording (0x53d2c)
                {   // fireworks set off by passing an fwtrigger (0x752b8 / 0x74c40)
                    s32 pp[3] = { ph.pos[0] >> 8, ph.pos[1] >> 8, ph.pos[2] >> 8 };
                    if (fw_step(&fw, pp)) sfx_play(SND_FLARE);
                }
                if (race == R_RACE && fe_mp())                     // the other players: bumps, your punches, theirs
                    for (int k = 0; k < nghost; k++) {
                        Ghost *g = &ghosts[k];
                        if (ghost_bump_remote(g, &ph)) { sfx_play(SND_COLLIDE); rumble_sfx(SND_COLLIDE); }
                        int f = ghost_punch_remote(g, &ph);
                        if (f) { sfx_play(SND_PUNCH); fights[f]++; }
                        if (f == 2) {                              // they go down on their console
                            net_ctl.knock_to = g->pid; net_ctl.knock_seq++; sfx_play(SND_KNOCKDOWN); if (g_opt.voice) voice_play(VC_KNOCK);
                            trick_msg_pts = 0; trick_msg_id = -1; snprintf(trick_msg_text, sizeof(trick_msg_text), "%s", LX("Knockdown!", "KNOCKDOWN")); trick_msg_t = 90;
                        }
                        const NetCtl *c = &net_s.ctl[g->pid];
                        if (c->knock_seq != g->kseen) {            // their punch landed on someone
                            g->kseen = c->knock_seq;
                            if (c->knock_to == net_me() && !ph.finished && ph.jstate != 7) { phys_knockdown(&ph); sfx_play(SND_KNOCKDOWN); fights[1]++; }
                        }
                    }
                else if (race == R_RACE)
                    for (int k = 0; k < nghost; k++) {
                        if (ghost_bump(&ghosts[k], &ph)) { sfx_play(SND_COLLIDE); rumble_sfx(SND_COLLIDE); }
                        int f = ghost_fight(&ghosts[k], &ph);              // punches (0x73898)
                        if (f) fights[f]++;                                // 0x52c9c / 0x52cec / 0x52a68 / 0x52b0c
                        if (f == 1 || f == 2) sfx_play(SND_KNOCKDOWN);    // (a knocked-down player also gets Wipeout1)
                        if (f == 2 || f == 3) sfx_play(SND_PUNCH);
                        if (f == 2) rumble_sfx(SND_KNOCKDOWN); else if (f == 1 || f == 3) rumble_sfx(SND_PUNCH);
                        if (f == 2 && g_opt.voice) voice_play(VC_KNOCK);   // (Voice Clips)
                        if (f == 2) { trick_msg_pts = 0; trick_msg_id = -1; snprintf(trick_msg_text, sizeof(trick_msg_text), "%s", LX("Knockdown!", "KNOCKDOWN")); trick_msg_t = 90; }
                    }
                php[4] -= pp4; php[4] += NOW_LINE();
                if (race == R_RACE) {
                    race_steps++;
                    if (race_steps == 1) gap_reset(&trk);
                    gap_mark(0, ph.s, race_steps);
                    for (int k = 0; k < nghost; k++) gap_mark(1 + k, ghosts[k].s, race_steps);
                    int was_place = place;
                    place = 1;                                     // places by race-line progress (the exe: every 750 ms)
                    for (int k = 0; k < nghost; k++) if (ghosts[k].finished || ghosts[k].s > ph.s) place++;
#ifdef TEST_VOICE
                    if (race_steps == 150 || race_steps == 400) voice_play(VC_PASS);   // test builds: the clip path in a race
#endif
                    {   // port (Voice Clips): now and then on passing a rider - every other pass at most, 8 s apart,
                        // not in the first 5 s (the start's shuffle)
                        static int pass_at = -1000;
                        if (race_steps == 1) pass_at = -1000;
                        if (!replay && place < was_place && race_steps > 125 && race_steps - pass_at > 200 && g_opt.voice && voice_has(VC_PASS)
                            && rand() % 2 == 0) {                  // (every other pass at most, 8 s apart)
                            pass_at = race_steps; voice_play(VC_PASS);
                        }
                    }
#ifdef TEST_FINISH
                    if (race_steps == TEST_FINISH) ph.finished = true;   // test builds: finish early
#endif
#ifdef TEST_GRIND
                    if (race_steps == 40 && trk.nrails) {                   // test: dropped onto the first rail, moving along it
                        const TrackRail *r = &trk.rails[0]; int sg = r->n > 2 ? 1 : 0;
                        for (int j = 0; j < 3; j++) ph.pos[j] = (r->pts[sg][j] + (j == 1 ? 40 : 0)) << 8;
                        for (int j = 0; j < 3; j++) ph.vel[j] = r->dir[sg][j] * 3 / 2;
                        ph.vel[1] = -200; ph.air = true; ph.jstate = 4;
                    }
#endif
#ifdef TEST_CRASH
                    if (race_steps == 40) phys_knockdown(&ph);                                            // test: a crash on the ground
                    if (race_steps == 300) { ph.pos[1] += 1500 << 8; ph.air = true; }
                    if (race_steps == 305) { extern void wipeout(Phys *); wipeout(&ph); }           // and one in the air
#endif
#ifdef TEST_DROP
                    if (race_steps == 40) { ph.pos[1] += 2500 << 8; ph.air = true; }   // test: a long drop (the falling pose)
#endif
#ifdef TEST_MPKNOCK
                    if (fe_mp() && net_is_host() && nghost && race_steps >= 40 && race_steps < 70) {   // test: ride alongside player 2
                        for (int j = 0; j < 3; j++) ph.pos[j] = ghosts[0].ph.pos[j] + (j == 0 ? 50 << 8 : 0);
                    }
#endif
                    if (ph.finished) {                             // 0x494a0: time, best, celebration
                        race = R_DONE; fin_time = race_steps * PHYS_TICK_MS;
                        int *best = &best_ms[cur_event];
                        new_best = *best == 0 || fin_time < *best;
                        if (new_best) { *best = fin_time; sfx_play(SND_FIREWORK); sfx_play(SND_FLARE); if (race_kind == 1) save_best(best_ms); }
                        bool close = !new_best && fin_time * 10 < *best * 11;
                        ph.react = new_best ? 23 + rand() % 5 : close ? 20 + rand() % 3 : 28 + rand() % 8;
                        if (race_kind == 2 && nghost && strstr(EVENTS[cur_event].kind, "rival")) {   // beat the rival's score
                            fin_place = ph.score > ghosts[0].result ? 1 : 2;
                            ph.react = fin_place == 1 ? 23 + rand() % 5 : 28 + rand() % 8;
                        } else if (race_kind == 2) {               // ranked among the event's recorded scores
                            fin_place = 1;
                            for (int k = 0; k < nfield; k++) if (field[k] > ph.score) fin_place++;
                            ph.react = fin_place == 1 ? 23 + rand() % 5 : fin_place <= 3 ? 20 + rand() % 3 : 28 + rand() % 8;
                        }
                        if (race_kind == 1 && nghost) {            // 0x4883c / 0x48890: by the finishing place
                            fin_place = 1;
                            for (int k = 0; k < nghost; k++) if (ghosts[k].finished) fin_place++;
                            if (fin_place == 1 && !fe_shadow() && g_opt.voice) voice_play(VC_WIN);   // (Voice Clips)
                            ph.react = fin_place == 1 ? 23 + rand() % 5 : fin_place <= 3 && nghost == 3 ? 20 + rand() % 3 : 28 + rand() % 8;
                        }
                        if (race_kind == 1 && nghost) {            // results table: the riders by time (0x72200)
                            nres = 0; res_rk[nres] = -1; res_ms[nres++] = fin_time;
                            for (int k = 0; k < nghost; k++) {
                                // (the recordings start with the 3 s countdown: 74 steps before GO here)
                                int t = (ghosts[k].fin_steps - 74) * PHYS_TICK_MS;   // still riding: the rest of its recording at full speed
                                if (!ghosts[k].finished) t = (ghosts[k].steps - 74 + (ghosts[k].n - ghosts[k].cur) * 2) * PHYS_TICK_MS;
                                if (!ghosts[k].finished && t <= fin_time) t = fin_time + PHYS_TICK_MS * (k + 1);
                                if (ghosts[k].finished && t > fin_time) t = fin_time - PHYS_TICK_MS * (k + 1);
                                res_rk[nres] = grd_char[k]; res_ms[nres++] = t;
                            }
                            for (int i = 0; i < nres; i++) for (int j = i + 1; j < nres; j++)
                                if (res_ms[j] < res_ms[i]) { int x = res_ms[i]; res_ms[i] = res_ms[j]; res_ms[j] = x; x = res_rk[i]; res_rk[i] = res_rk[j]; res_rk[j] = x; }
                        }
                        if (fe_shadow() && nghost && race_kind == 2) {   // Shadow Racing, trick events: beat your best score
                            fin_place = ph.score > ghosts[0].result ? 1 : 2;
                            ph.react = fin_place == 1 ? 23 + rand() % 5 : 28 + rand() % 8;
                        }
                        rec_finish(cur_event, ri, race_kind == 1 ? fin_time : ph.score, race_kind == 1);
                        if (heats()) {                             // career rounds: who goes through
                            if (race_kind == 2) {
                                pscore[ev_round] = ph.score;
                                fin_place = trick_rank(player_total());
                                ph.react = fin_place == 1 ? 23 + rand() % 5 : fin_place <= 3 ? 20 + rand() % 3 : 28 + rand() % 8;
                            }
                            round_ok = ev_round == 0 && race_kind == 2 ? true : fin_place <= 3;   // heat 1 always goes on to heat 2
                        }
                        if (fin_place && fe_career() && (!heats() || ev_round == 2)) {   // cash for 1st-3rd, unlocks (0x711c4, 0x3353c) - career finals only
                            pay_cash = career_award(cur_event, fin_place, pay_unl, &pay_nunl);
                            save_best(best_ms);
                        }
                    }
                }
                for (int k = 0; k < ph.sfx_n; k++) {
                    // port: the player's own SUPER UBER clip in place of the crowd (Voice Clips)
                    if (ph.sfx_q[k] == SND_CROWD && g_opt.voice && voice_has(VC_UBER) && !replay) { voice_play(VC_UBER); rumble_sfx(ph.sfx_q[k]); continue; }
                    sfx_play(ph.sfx_q[k]); rumble_sfx(ph.sfx_q[k]);
                }
                if (ph.rail_state == 2) rumble_hold(1);           // a light buzz along a rail
                ph.sfx_n = 0;
                sfx_loop(ph.loop_snd);
                for (int k = 0; k < 3; k++) cur_pos[k] = ph.pos[k] / 256.0f;
                if (fabsf(cur_pos[0] - prev_pos[0]) + fabsf(cur_pos[2] - prev_pos[2]) > 2000) {  // respawned
                    for (int k = 0; k < 3; k++) prev_pos[k] = cur_pos[k];
                    memcpy(prev_ori.mat, ph.mat, sizeof(ph.mat)); prev_ori.pitch = ph.pitch; prev_ori.lean = ph.lean;
                }
                if (race == R_INTRO) cam_intro(&ph, intro_ms / PHYS_TICK_MS * 4); else cam_step(&ph, g_opt.cam, ph.pipe);
                steps_frame++;
                phys_us = timerTicks2usec(cpuEndTiming());
                // port: the HUD's score counts a combo in when it ends. Its tricks' points (and a bonus, x3 and up) wait
                // under "combo xN" while it runs; when it is over that total counts down there while the score at
                // the top counts up by the same amount (~2/3 s). Only the display: ph.score is banked as before.
                bool paid = false;
                if (ph.bonus_n != seen_bonus) {                    // a combo bonus paid (no trick message for it)
                    seen_bonus = ph.bonus_n; paid = true;
                    combo_drain += ph.last_bonus;
                    last_score += ph.last_bonus;
                }
                if (ph.score < last_score) last_score = ph.score;  // (a restart)
                if (ph.score != last_score) {                      // a trick was banked: show it
                    trick_msg_pts = ph.score - last_score; trick_msg_id = ph.last_trick; trick_msg_t = 120;
                    if (trick_msg_pts > 0) combo_pts += trick_msg_pts;
                    snprintf(trick_msg_text, sizeof(trick_msg_text), "%s", ph.trick_text); ph.trick_text[0] = 0;
                    last_score = ph.score;
                    if (race_kind == 2 && trick_msg_pts > 0) { star_pts = trick_msg_pts; star_dx = star_dy = 0; }   // 0x45128: stars fly off
                }
                if (ph.combo_landed == 0 && (combo_pts || paid)) {   // the combo is over (or broken)
                    combo_drain += combo_pts; combo_pts = 0;
                    drain_lvl = prev_lvl;
                    drain_step = combo_drain / 40 > 10 ? combo_drain / 40 : 10;
                }
                prev_lvl = ph.combo_lvl;
            }
        }
        float al = paused ? 1.0f : acc_us / (PHYS_TICK_MS * 1000.0f);
        float rp[3]; for (int k = 0; k < 3; k++) rp[k] = prev_pos[k] + (cur_pos[k] - prev_pos[k]) * al;

        int l_phys = NOW_LINE();
        track_update_stream(&trk, replay ? rpg.s : ph.s, 2);
        music_update(vblanks, false);
        music_duck(voice_tick() && g_opt.vduck ? 30 : 100); music_duck_step();     // (Voice Clips: the music dips under a clip)
        int l_stream = NOW_LINE();

        // chase camera (cam_step, 0x75808): interpolate the last two steps, then keep it out of the ground / walls
        if (!cam_ok && !replay) { if (race == R_INTRO) cam_intro(&ph, 0); else cam_step(&ph, g_opt.cam, ph.pipe); }
        if (replay && !rp_cam_ok) { if (rp_cam == 0) cam_replay(&trk, &rpg.ph); else { cam_ok = false; cam_step(&rpg.ph, g_opt.cam, rpg.ph.pipe); } }
        float ce[3], tg[3];
        for (int k = 0; k < 3; k++) { ce[k] = camV0[k] + (camV[k] - camV0[k]) * al; tg[k] = camT0[k] + (camT[k] - camT0[k]) * al; }
        CAMDBG(0, ce);
        float fx, fz;
        { float dx = tg[0] - ce[0], dz = tg[2] - ce[2], l = sqrtf(dx * dx + dz * dz); if (l < 1) l = 1; fx = dx / l; fz = dz / l; }
        {   // keep the camera above the ground under it. The ray starts near the rider's height, not high above the
            // camera: from 2000 units up it found the top of a tunnel roof and lifted the camera over the tunnel.
            float top = (ce[1] > rp[1] ? ce[1] : rp[1]) + 160;
            s32 a[3] = { (s32)ce[0], (s32)top, (s32)ce[2] }, b[3] = { (s32)ce[0], (s32)ce[1] - 3000, (s32)ce[2] }, h[3], n[3]; int sf;
            // port: up quickly, back down over a few frames - over the edge of a ledge or a wall's top the ray found
            // it on one frame and missed it on the next, and the camera jumped ~90 units up and down every frame
            static float glift;
            if (!cam_ok) glift = 0;
            float want = 0;
            if (track_ray(&trk, a, b, h, n, &sf) && ce[1] < h[1] + 30) want = h[1] + 30 - ce[1];
            if (want > 150) want = 0;                  // that far up it is an overhang or a ledge above, not the ground
            glift += (want - glift) * (want > glift ? 0.5f : 0.12f);
            ce[1] += glift;
        }
        memcpy(cam_pos, ce, sizeof(ce)); CAMDBG(1, ce);
        if (race == R_RACE || race == R_DONE) {   // keep the camera in sight of the rider once racing (the exe only checks
            // for a roof over the camera, and not at the gate, where this pulled the camera into the rider)
            // port: in quickly, back out over a few frames (as the chase camera's own ray): a ray grazing an edge hit
            // on one frame and missed on the next, and the camera jumped ~80 units in and out every frame (judder)
            static float sight = 1;
            if (!cam_ok) sight = 1;
            s32 e[3] = { (s32)rp[0], (s32)rp[1] + 50, (s32)rp[2] }, q[3] = { (s32)cam_pos[0], (s32)cam_pos[1], (s32)cam_pos[2] }, h[3], n[3]; int sf;
            float want = 1;
            float qx = cam_pos[0] - e[0], qy = cam_pos[1] - e[1], qz = cam_pos[2] - e[2], ql = sqrtf(qx * qx + qy * qy + qz * qz);
            if (ql > 1 && track_ray(&trk, e, q, h, n, &sf)) {
                float dx = h[0] - e[0], dy = h[1] - e[1], dz = h[2] - e[2], l = sqrtf(dx * dx + dy * dy + dz * dz);
                want = l > 30 ? (l - 25) / ql : 0;
                if (want > 1) want = 1;
            }
            sight += (want - sight) * (want < sight ? 0.6f : 0.12f);   // (in within ~3 frames rather than a 150-200 unit cut)
            if (sight < 0.999f) { cam_pos[0] = e[0] + qx * sight; cam_pos[1] = e[1] + qy * sight; cam_pos[2] = e[2] + qz * sight; }
        }

        CAMDBG(2, cam_pos);
        {   // 0x5acb4: the eye and the target both move param_4 (40; 32 on the superpipe) along the view's up vector
            float f[3] = { tg[0] - cam_pos[0], tg[1] - cam_pos[1], tg[2] - cam_pos[2] }, l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            if (l > 1) {
                for (int k = 0; k < 3; k++) f[k] /= l;
                float u[3] = { -f[0] * f[1], 1 - f[1] * f[1], -f[2] * f[1] }, ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
                float lift = ph.pipe ? 32 : 40;
                if (ul > 0.01f) for (int k = 0; k < 3; k++) { cam_pos[k] += u[k] / ul * lift; tg[k] += u[k] / ul * lift; }
            }
        }
        CAMDBG(3, cam_pos);
#ifndef TEST_NORAYS
        {   // port: keep the course off the near plane's corners (6.4 units ahead, ~8.2 to its corners).
            // A polygon the hardware cuts at the near plane gets a depth just under zero there, which the DS wraps to
            // the far end: it came out fully fogged - white wedges on hardware (melonDS clamps instead). Rock walls and
            // gullies beside the line (R&B at ~70 %) came that close: the camera moves off any surface within 25 units,
            // found with short rays down, to the sides, ahead and ahead-down.
            float f[3] = { tg[0] - cam_pos[0], tg[1] - cam_pos[1], tg[2] - cam_pos[2] }, l = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
            if (l > 1) {
                for (int k = 0; k < 3; k++) f[k] /= l;
                float r[3] = { -f[2], 0, f[0] }, rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
                if (rl > 0.01f) { r[0] /= rl; r[2] /= rl; } else { r[0] = 1; r[2] = 0; }
                float u[3] = { r[1] * f[2] - r[2] * f[1], r[2] * f[0] - r[0] * f[2], r[0] * f[1] - r[1] * f[0] };
                static const s8 DIR[9][3] = { { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, 1, -1 }, { 1, 1, 0 }, { -1, 1, 0 },
                                              { 1, 0, -1 }, { -1, 0, -1 } };
                // (three rays a frame in turn, ~3 lines; each ray's last push is kept and all nine are applied)
                static float pv[9][3]; static u8 grp;
                grp = (grp + 1) % 3;
                if (!cam_ok) memset(pv, 0, sizeof(pv));
                for (int j = grp; j < 9; j += 3) {
                    pv[j][0] = pv[j][1] = pv[j][2] = 0;
                    float d[3]; for (int k = 0; k < 3; k++) d[k] = r[k] * DIR[j][0] + f[k] * DIR[j][1] + u[k] * DIR[j][2];
                    float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
                    s32 a[3] = { (s32)cam_pos[0], (s32)cam_pos[1], (s32)cam_pos[2] }, b[3], h[3], n[3]; int sf;
                    for (int k = 0; k < 3; k++) b[k] = a[k] + (s32)(d[k] / dl * 30);
                    if (!track_ray(&trk, a, b, h, n, &sf)) continue;
                    float nl = sqrtf((float)n[0] * n[0] + (float)n[1] * n[1] + (float)n[2] * n[2]);
                    if (nl < 1) continue;
                    float nn[3] = { n[0] / nl, n[1] / nl, n[2] / nl };
                    float perp = (cam_pos[0] - h[0]) * nn[0] + (cam_pos[1] - h[1]) * nn[1] + (cam_pos[2] - h[2]) * nn[2];
                    if (perp < 0) { perp = -perp; for (int k = 0; k < 3; k++) nn[k] = -nn[k]; }
                    if (perp < 25) for (int k = 0; k < 3; k++) pv[j][k] = nn[k] * (25 - perp);
                }
                // the rays often see the same surface: each push counts only what the others have not already
                // covered along its normal (summed, nine rays on one slope threw the camera 150 units for a frame)
                float P[3] = { 0, 0, 0 };
                for (int j = 0; j < 9; j++) {
                    float a = sqrtf(pv[j][0] * pv[j][0] + pv[j][1] * pv[j][1] + pv[j][2] * pv[j][2]);
                    if (a < 0.01f) continue;
                    float nn[3] = { pv[j][0] / a, pv[j][1] / a, pv[j][2] / a };
                    float more = a - (P[0] * nn[0] + P[1] * nn[1] + P[2] * nn[2]);
                    if (more > 0) for (int k = 0; k < 3; k++) P[k] += nn[k] * more;
                }
                // out quickly, back in gently: a ray finding the wall a frame late or losing it shows as no jump
                static float Pa[3];
                if (!cam_ok) memset(Pa, 0, sizeof(Pa));
                float pl = P[0] * P[0] + P[1] * P[1] + P[2] * P[2], al2 = Pa[0] * Pa[0] + Pa[1] * Pa[1] + Pa[2] * Pa[2];
                for (int k = 0; k < 3; k++) Pa[k] = Pa[k] + (P[k] - Pa[k]) * (pl >= al2 ? 0.6f : 0.15f);
                for (int k = 0; k < 3; k++) cam_pos[k] += Pa[k];
            }
        }
#endif
        glLoadIdentity();
        gluLookAt(cam_pos[0] / 256, cam_pos[1] / 256, cam_pos[2] / 256, tg[0] / 256, tg[1] / 256, tg[2] / 256, 0, 1, 0);
#ifdef TEST_CAMLOG
        {   // test builds: the camera, its target and the drawn rider each frame (dsrun --ram, magic SSXCAM1)
            typedef struct { s32 cam[3], tg[3], rp[3], py, vy; u8 rail, air, pipe, race; s32 st[4][3]; } CamE;
            static struct { char magic[8]; u32 n; CamE e[7000]; } cl = { "SSXCAM1", 0, {{{0}}} };
            if (race == R_RACE && !paused && cl.n < 7000) {
                CamE *e = &cl.e[cl.n++];
                for (int k = 0; k < 3; k++) { e->cam[k] = (s32)(cam_pos[k] * 16); e->tg[k] = (s32)(tg[k] * 16); e->rp[k] = (s32)(rp[k] * 16); }
                e->py = ph.pos[1]; e->vy = ph.vel[1]; e->rail = ph.rail_state; e->air = ph.air; e->pipe = ph.pipe; e->race = race;
                extern float camdbg[4][3]; for (int j = 0; j < 4; j++) for (int k = 0; k < 3; k++) e->st[j][k] = (s32)(camdbg[j][k] * 16);
            }
        }
#endif
        {   float v[3] = { tg[0] - cam_pos[0], tg[1] - cam_pos[1], tg[2] - cam_pos[2] }, l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (l > 1) for (int k = 0; k < 3; k++) view_f[k] = v[k] / l; }
        glPolyFmt(POLY_ALPHA(31) | track_cull | POLY_FOG);
        int l_cam = NOW_LINE();
        sky_draw(cam_pos);
        glPolyFmt(POLY_ALPHA(31) | track_cull | POLY_FOG);
        trk.cam_y_next = (s32)cam_pos[1];
        {   // the view's downward tilt (tan, x 1024) for the culling's margin
            float vx = tg[0] - cam_pos[0], vy = tg[1] - cam_pos[1], vz = tg[2] - cam_pos[2], hl = sqrtf(vx * vx + vz * vz);
            float tp = vy < 0 ? -vy / (hl > 1 ? hl : 1) : 0; if (tp > 4) tp = 4;
            trk.cam_tanp = (s32)(tp * 1024) + 64;               // (+ a little: the 25.8 degree half-height of the view)
        }
        track_set_cam(&trk, cam_pos[0], cam_pos[2], fx, fz, fog_end_units + 800);   // (+800: the fog is by depth, terrain high above the camera reads nearer)
        // the far tiles to the end of the frame only when the last one came near the hardware's limits (sent at the
        // end the geometry engine's backlog holds the CPU there: ~9 lines a frame)
        trk.defer_far = last_polys > 1700 || last_verts > 5200;
        int submitted = track_draw(&trk, replay ? rpg.s : ph.s, bias);
        int l_trk0 = NOW_LINE();
        {
            float cp[3] = { cam_pos[0], cam_pos[1], cam_pos[2] };
            // props budget from last frame's polygon RAM use (track + rider), not from the submitted count
            static int last_props;
            int budget = 1800 - (last_polys - last_props);
            if (budget > 1100) budget = 1100;
            last_props = draw_props(&trk, cp, fx, fz, budget < 0 ? 0 : budget, spin);
            submitted += last_props;
        }
        int l_track = NOW_LINE();

        rider_fmt = POLY_ALPHA(31) | POLY_CULL_NONE | POLY_FORMAT_LIGHT0 | POLY_FOG | POLY_ID(1); glPolyFmt(rider_fmt);
        rider_board_cull = POLY_CULL_BACK;    // with the board bone's own mirror, FRONT: the race view is X-mirrored (as track_cull)
        rider_async = true;                                        // each rider's body list runs on while the next is posed
        {
            const Clip *cl = NULL; float key = 0;
            if (replay) {                                          // the replayed run: posed like a ghost
                // port: turned and posed between its last two steps like the player (it moved in 40 ms jerks)
                int a = rpg.ph.anim < NANIM_IDS ? rpg.ph.anim : 8, id = id_clip[a];
                cl = rider_clip(&rider, id);
                if (!cl) { id = id_clip[8]; cl = rider_clip(&rider, id); }
                static Phys rrp; rrp = rpg.ph;
                bool ri = rpori.ok && !rp_pause;
                if (ri) interp_ori(&rrp, (const s32 (*)[3])rpori.mat, rpori.pitch, rpori.lean, &rpg.ph, al);
                float ph8 = rpg.phase;
                if (ri && rpori.anim == rpg.ph.anim) {
                    int dph = (rpg.phase - rpori.phase) & 0xff;
                    if (dph < 128) { ph8 = rpori.phase + dph * al; if (ph8 >= 256) ph8 -= 256; }
                }
                if (cl) key = ph8 * (float)cl->length / (256.0f * cl->interval);
                rider.use_hi = false;
                cl = pose_fade(&rider, &rider, 0, id, cl, key, rp_pause ? 0 : dvb * 17);
                submitted += draw_rider(&rider, &rrp, rp, cl, key, false);
            } else {
            // posed and turned every frame, between the last two physics steps like the position: posed at the 25 Hz
            // physics rate and turned by the step's matrix, spins, flips and grabs moved in 40 ms jerks at 60 fps
            // (two frames, then three, ...) - the board looked to stutter through tricks
            static Phys rph; rph = ph;
            {
                float u[3], f[3], b = al, a = 1 - al;
                for (int k = 0; k < 3; k++) { u[k] = prev_ori.mat[k][1] * a + ph.mat[k][1] * b; f[k] = prev_ori.mat[k][2] * a + ph.mat[k][2] * b; }
                float ul = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
                if (ul > 1) {
                    for (int k = 0; k < 3; k++) u[k] /= ul;
                    float d = f[0] * u[0] + f[1] * u[1] + f[2] * u[2];
                    for (int k = 0; k < 3; k++) f[k] -= d * u[k];
                    float fl = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
                    if (fl > 1) for (int k = 0; k < 3; k++) { rph.mat[k][1] = (s32)(u[k] * 16384); rph.mat[k][2] = (s32)(f[k] / fl * 16384); }
                }
                int dp = ((ph.pitch - prev_ori.pitch + 0x400) & 0x7ff) - 0x400;
                rph.pitch = (prev_ori.pitch + (int)(dp * al)) & 0x7ff;
                rph.lean = prev_ori.lean + (int)((ph.lean - prev_ori.lean) * al);
            }
            int cur_id = ph.anim >= 0 && ph.anim < NANIM_IDS ? id_clip[ph.anim] : -1;
            cl = cur_id >= 0 ? rider_clip(&rider, cur_id) : NULL;
            if (!cl) { cur_id = id_clip[8]; cl = rider_clip(&rider, cur_id); }
            if (cl) {
                // the clip time a fraction of a step back from the step's (anim_tick advances DT x rate / 256)
                float step = PHYS_TICK_MS * (ph.anim_rate > 0 ? ph.anim_rate : 256) / 256.0f, ms = ph.anim_ms;
                if (!paused && !(ph.anim_once && ph.anim_done)) ms -= (1 - al) * (ph.anim_rev ? -step : step);
                if (ms < 0) ms = ph.anim_once || prev_ori.anim != ph.anim ? 0 : ms + ph.anim_len;   // (a clip started this step: from 0)
                if (ph.anim_hold && ms > ph.anim_hold) ms = ph.anim_hold;
                key = ms * 30.0f / (1000.0f * cl->interval);
            }
            {   // port: the player always in the high-detail model (it swapped with the low one by the frame's polygon
                // count, which showed); the course's LOD makes the room
                static int drawn_n;
                rider.use_hi = rider.body_hi.dl && !replay;
                // port: a short cross-fade into each new clip (they cut in one frame: a grab, a landing or a turn
                // snapped the body into its first pose). The clip left keeps its last time.
                static int bl_from = -1, bl_to = -1, bl_ms = 999; static float bl_key, last_key; static bool bl_loop, last_loop;
                if (cur_id != bl_to) { bl_from = bl_to; bl_key = last_key; bl_loop = last_loop; bl_to = cur_id; bl_ms = 0; }
                else if (!paused) bl_ms += dvb * 17;
                last_key = key; last_loop = !ph.anim_once;
                const Clip *from = bl_from >= 0 && bl_ms < 100 ? rider_clip(&rider, bl_from) : NULL;
                if (from && cl) { rider_pose_blend(&rider, from, bl_key, bl_loop, cl, key, !ph.anim_once, bl_ms * 256 / 100); cl = NULL; }
                drawn_n = draw_rider(&rider, &rph, rp, cl, key, !ph.anim_once);
                submitted += drawn_n;
            }
            }
        }
        int l_r1 = NOW_LINE();
        for (int k = 0; k < nghost; k++) {                         // the ghosts, near and in front of the camera
            const Ghost *g = &ghosts[k];
            float gp[3]; for (int j = 0; j < 3; j++) gp[j] = gprev[k][j] + (gcur[k][j] - gprev[k][j]) * al;
            float dx = gp[0] - cam_pos[0], dy2 = gp[1] - cam_pos[1], dz = gp[2] - cam_pos[2], d2 = dx * dx + dy2 * dy2 + dz * dz;
            if (d2 > 9000.0f * 9000.0f || dx * fx + dz * fz < -200) continue;
            const Clip *cl = NULL; float key = 0;
            // posed every frame within 2500 units, between their last two steps like the player (clip phase,
            // turn); further off one ghost a frame, round robin
            static int rr; if (k == 0) rr++;
            bool nearg = d2 < 2500.0f * 2500.0f, interp = nearg && gori[k].ok && !(replay && rp_pause);
            static Phys gph[3]; gph[k] = g->ph;
            if (interp) interp_ori(&gph[k], (const s32 (*)[3])gori[k].mat, gori[k].pitch, gori[k].lean, &g->ph, al);
            if ((nearg || rr % nghost == k || paused) && d2 < 6000.0f * 6000.0f) {
                int a = g->ph.anim < NANIM_IDS ? g->ph.anim : 8, id = id_clip[a];
                cl = rider_clip(&rider, id);
                if (!cl) { id = id_clip[8]; cl = rider_clip(&rider, id); }
                float ph8 = g->phase;
                if (interp && gori[k].anim == g->ph.anim) {
                    int dph = (g->phase - gori[k].phase) & 0xff;
                    if (dph < 128) { ph8 = gori[k].phase + dph * al; if (ph8 >= 256) ph8 -= 256; }   // forward (wrapping) only
                }
                if (cl) key = ph8 * (float)cl->length / (256.0f * cl->interval);
                // the clip cross-fade near (further off posed one frame in three: the cut doesn't show)
                int q0 = RLINE();
                if (nearg) cl = pose_fade(&grd[k], &rider, 1 + k, id, cl, key, paused || (replay && rp_pause) ? 0 : dvb * 17);
                else fade[1 + k].to = id;
                rprof[4] += RLINE() - q0;
            }
            submitted += draw_rider(&grd[k], &gph[k], gp, cl, key, false);
        }

        rider_gx_wait(); rider_async = false;
        int l_r2 = NOW_LINE(); static int fxp[5];
        {   // light flares after the opaque scene (translucent)
            float cp[3] = { cam_pos[0], cam_pos[1], cam_pos[2] };
            int f0 = NOW_LINE();
#ifndef TEST_NOGLARE
            submitted += draw_glares(&trk, cp, fx, fz, (1900 - last_polys) / 1);
#endif
            int f1 = NOW_LINE();
            submitted += draw_fog(&trk, cp, fx, fz);
            int f2 = NOW_LINE();
            submitted += draw_fireworks(&fw, cp, fx, fz, 1950 - last_polys - trk.nglares);
            int f3 = NOW_LINE();
            if (!replay) { submitted += draw_trail(&trails[0], cp); for (int k = 0; k < nghost; k++) submitted += draw_trail(&trails[1 + k], cp); }
            int f4 = NOW_LINE();
            fxp[0] = f1 - f0; fxp[1] = f2 - f1; fxp[2] = f3 - f2; fxp[3] = f4 - f3; fxp[4] = -f4;
            submitted += draw_shadow(&trk, replay ? &rpg.ph : &ph, rp);          // the boards' shadows
            for (int k = 0; k < nghost; k++) {
                float gp[3]; for (int j = 0; j < 3; j++) gp[j] = gprev[k][j] + (gcur[k][j] - gprev[k][j]) * al;
                float dx = gp[0] - cp[0], dz = gp[2] - cp[2];
                if (dx * dx + dz * dz < 2500.0f * 2500.0f) submitted += draw_shadow(&trk, &ghosts[k].ph, gp);
            }
        }
        int l_fx = NOW_LINE(); fxp[4] += l_fx;
        // ---- top-screen HUD (0x46b5c), Gizmondo 320x240 layout moved onto 256x192: top-left as is, right side
        // -32 / -64 in x, the left column and bottom row -48 in y, the centre -32 / -24
        if (fe_mp()) {                                             // my rider as the others will draw it
            NetRider *r = &net_rider;
            for (int k = 0; k < 3; k++) { r->pos[k] = ph.pos[k] >> 8; r->up[k] = ph.mat[k][1]; r->fwd[k] = ph.mat[k][2]; }
            r->s = (s32)ph.s; r->pitch = ph.pitch; r->lean = ph.lean; r->character = ph.character; r->kmh = ph.kmh;
            r->flags = (ph.stance ? 1 : 0) | (ph.air ? 2 : 0) | (ph.rail_state ? 4 : 0) | (race == R_DONE ? 8 : 0) | (ph.jstate == 7 ? 16 : 0);
            r->anim = ph.anim >= 0 && ph.anim < 256 ? ph.anim : 8;
            const Clip *cl = ph.anim >= 0 && ph.anim < NANIM_IDS ? rider_clip(&rider, id_clip[ph.anim]) : NULL;
            int ph8 = cl && cl->length ? (int)((s64)ph.anim_ms * 30 * 256 / (1000 * cl->length)) : 0;
            r->phase = ph.anim_once ? (ph8 > 255 ? 255 : ph8) : (ph8 & 255);
            r->result = race_kind == 1 ? (race == R_DONE ? fin_time : 0) : ph.score;
        }
        hud_begin();
        {
            const u16 WHITE = hud_rgb565(0xffdf), GREEN = hud_rgb565(0x540), YELLOW = RGB15(31, 27, 6);
            char s[64];
#ifdef TEST_GRIND
            { char st[48]; snprintf(st, sizeof(st), "rail%d bal %d/%d lean %d", ph.rail_state, ph.balance, ph.bal_limit, ph.lean); hud_font(3); hud_text(2, 180, 0, st, RGB15(31, 31, 0)); }
#endif
#ifdef TEST_BACKDROP
            glClearColor(31, 0, 31, 31);                            // test: where nothing is drawn (magenta)
#endif
#ifdef TEST_STATS
            { static int tc; if (++tc % 300 == 10) track_tex_check(&trk); }
            {   // render statistics: polygons / vertices reaching the hardware, LOD bias, missing segments (and maxima)
                static int mp, mv, mm, bmax; if (last_polys > mp) mp = last_polys; if (last_verts > mv) mv = last_verts;
                if (trk.missing > mm) mm = trk.missing; if (bias > bmax) bmax = bias;
                char st[80]; snprintf(st, sizeof(st), "p%d/%d v%d/%d b%d/%d m%d/%d f%d t%d/%x", last_polys, mp, last_verts, mv, bias, bmax, trk.missing, mm, (int)fps, trk.tex_bad, trk.pal_bad);
                hud_font(3); hud_text(2, 180, 0, st, RGB15(31, 31, 0));
            }
#endif
            if (race == R_INTRO || (race == R_DONE && ph.race_over) || paused) {   // the exe hides the HUD at the gate, on the results (0x52d00) and paused (0x31254)
            } else {
            if (race_kind == 1 && nghost && race != R_COUNT && race != R_INTRO) {               // place: big digit + "/n"
                int pl = race == R_DONE ? fin_place : place;
                hud_sprite(2, 2, 0x13, 0x16, 0x8a, pl * 0x16 - 0x16, 0x13, 0x16, WHITE);
                hud_sprite(0x12, 0x10, 0x1a, 0xc, 0, (nghost + 1) * 0xc + 0x38, 0x1a, 0xc, WHITE);
            }
            if (race_kind == 2 && (nghost || heats())) {                                   // trick event: score vs the rival
                snprintf(s, sizeof(s), "%d", ph.score - combo_drain - combo_pts); hud_text(2, 2, 0, s, WHITE);
                if (heats()) {                                                // career heats: the score that takes 3rd
                    s32 t = third_total() - (ev_round == 1 ? pscore[0] : 0);
                    snprintf(s, sizeof(s), "top 3 %ld", (long)(t > 0 ? t : 0));
                } else if (strstr(EVENTS[cur_event].kind, "rival") || nfield < 3)
                    snprintf(s, sizeof(s), "rival %d", ghosts[0].result);
                else {                                                        // the score that takes 3rd place
                    int c = 0; s32 v = 0;
                    for (int k = 0; k < nfield; k++) { int above = 0; for (int j = 0; j < nfield; j++) if (field[j] > field[k]) above++; if (above == 2) { v = field[k]; c = 1; } }
                    snprintf(s, sizeof(s), "top 3 %d", c ? (int)v : 0);
                }
                hud_text(2, 12, 0, s, hud_rgb565(0xfc00));
            }
            if (!(race_kind == 2 && (nghost || heats()))) {                  // port: the run's score everywhere else
                // (the exe shows it only against a rival / heats): under the place in a race, top left otherwise
                bool pl = race_kind == 1 && nghost && race != R_COUNT && race != R_INTRO;
                snprintf(s, sizeof(s), "%d", ph.score - combo_drain - combo_pts); hud_text(2, pl ? 0x1f : 2, 0, s, WHITE);
            }
            { int ms = race == R_DONE ? fin_time : race_steps * PHYS_TICK_MS;    // race time
              snprintf(s, sizeof(s), "%d:%02d.%02d", ms / 60000, ms / 1000 % 60, ms % 1000 / 10); }
            hud_text(252, 2, 1, s, WHITE);
            if (race == R_COUNT && count_ms >= 0) {                          // 3 2 1: 12x12 digits shrinking in
                int t = count_ms % 1000, sc = (1000 - t) * 0x28000 / 500;
                hud_sprite(0x99 - 32 - (sc >> 15), 0x56 - 24, 0xc, 0xc, 0x1b, 0x50 + (count_ms / 1000) * 0xc,
                           0xc + (sc >> 14), 0xc + (sc >> 14), WHITE);
            } else if ((race == R_RACE || race == R_DONE) && race_steps * PHYS_TICK_MS < 700) {   // GO! growing (table 0xa9b18)
                int sc = race_steps * PHYS_TICK_MS * 0x30000 / 700;
                hud_sprite(0xa0 - 32 - (sc >> 14) - 10, 0x56 - 24, 28, 14, 40, 80, 28 + ((sc * 2) >> 14), 14 + (sc >> 14), WHITE);
            }
            // course progress, left column (0x46b5c reads 0x182c, set by 0x52400: % of the race line covered): the
            // white track between chequered flags fills red from the bottom; the number under it is the %
            int pv = trk.length > 1 ? (int)(ph.s * 100 / trk.length) : 0; if (pv < 0) pv = 0; if (pv > 100) pv = 100;
            if (race == R_DONE) pv = 100;
            if (pv < 1) hud_sprite(4, 0x5a - 48, 0x10, 0x27, 0, 0x27, 0x10, 0x27, WHITE);
            else if (pv < 0x62) {
                int k = pv * 0x4e / 100;
                hud_sprite(8, 0x5a - 48, 8, 0x4e - k, 0x10, 0, 8, 0x4e - k, WHITE);
                hud_sprite(8, 0xa8 - 48 - k, 8, k, 0x18, 0x4e - k, 8, k, WHITE);
            } else hud_sprite(4, 0x5a - 48 + 0x27 - 0x27, 0x10, 0x4e, 0x20, 0, 0x10, 0x4e, WHITE);
            hud_sprite(4, 0x76 - 48, 0x10, 0x10, 0x30, 0x10, 0x10, 0x10, WHITE);
            snprintf(s, sizeof(s), "%d%%", pv); hud_text(4, 0xb8 - 48, 0, s, WHITE);   // (port: with a % sign)
            {   // the boost meter, right column (0x3dc7c; Gizmondo x 0x129 -> 233, y - 8): a 17 x 90 coil, grey where
                // empty and coloured from the bottom as it fills; once full its colours and the orb above it show the
                // boost level (1, 2, 3 = unlimited), the grey SUPER UBER label on top lights a letter per uber trick
                // (0x3e088), the ball below
                int bx = 0x129 - 64, by = 0x3e - 8;
                int pct = ph.boost >> 8; if (pct > 100) pct = 100; if (pct < 0) pct = 0;
                int top = (100 - pct) * 0x5a / 100, fill = pct * 0x5a / 100;
                static const u8 LV[4][3][4] = {                        // empty part, filled part, orb (u, v, w, h)
                    { { 0, 166, 17, 90 }, { 51, 166, 17, 90 }, { 0, 0, 0, 0 } },
                    { { 102, 166, 17, 90 }, { 85, 166, 17, 90 }, { 116, 0, 11, 12 } },
                    { { 136, 166, 17, 90 }, { 119, 166, 17, 90 }, { 127, 0, 11, 12 } },
                    { { 68, 166, 17, 90 }, { 170, 166, 17, 90 }, { 105, 0, 11, 12 } } };
                int lv = ph.boost_full ? (ph.boost_level < 1 ? 1 : ph.boost_level > 3 ? 3 : ph.boost_level) : 0;
                const u8 *e = LV[lv][0], *f = LV[lv][1];
                if (top) hud_sprite(bx, by, e[2], top, e[0], e[1], e[2], top, WHITE);
                if (fill) hud_sprite(bx, by + top, f[2], fill, f[0], f[1] + top, f[2], fill, WHITE);
                if (lv == 3 && fill) {                                 // port: SUPER UBER - the red coil fades to the
                    static int ph_t; ph_t = (ph_t + dvb * 20) & 0x7ff;   // level 2 orange one and back (~1.7 s)
                    int a = (sx_sin_q(ph_t) + 16384) * 31 / 32768;
                    hud_sprite_a(bx, by + top, f[2], fill, 119, f[1] + top, f[2], fill, WHITE, a);
                }
                if (lv) {                                              // the orb cycles 0..5..0 (faster at level 3)
                    static int ot, of; ot += dvb * 17;
                    int per = ph.full_ms >= 0x3a99 && ph.full_ms <= 24999 ? 17 : ph.full_ms > 14999 || ph.full_ms < 0x2329 ? 680 : 340;
                    if (ot > per) { ot = 0; of = (of + (per == 17 ? 2 : 1)) % 12; }
                    int fr = of > 5 ? 11 - of : of;
                    const u8 *o = LV[lv][2];
                    hud_sprite(299 - 64, 0x31 - 8, o[2], o[3], o[0], o[1] + o[3] * fr, o[2], o[3], WHITE);
                }
                hud_sprite(0x120 - 64, 0x20 - 8, 0x1f, 0xf, 0x2b, 0x3c, 0x1f, 0xf, WHITE);   // SUPER UBER (grey)
                if (!lv) hud_sprite(299 - 64, 0x32 - 8, 0xc, 10, 0x30, 0x21, 0xc, 10, WHITE);
                hud_sprite(299 - 64, 0x9b - 8, 0xb, 0xb, 0x30, 0x2c, 0xb, 0xb, WHITE);
                static const u8 LX[9] = { 0x1b, 0x16, 0x11, 0xc, 0x1e, 0x19, 0x14, 0xf, 10 }, LI[9] = { 5, 6, 7, 8, 0, 1, 2, 3, 4 };
                // the lit letters, blue glyphs only (ds_hud.py), each one exactly over its grey letter: the exe's
                // positions sit one pixel left of the label's glyphs (but for the two R's)
#ifdef TEST_LETTERS
                if (ph.letters < TEST_LETTERS) { ph.letters = TEST_LETTERS; ph.boost = 0x6400; }
#endif
                // port: a letter just lit swells to ~1.6x and settles back over a third of a second
                static int pop[9], shown;
                if (ph.letters < shown) { shown = ph.letters; memset(pop, 0, sizeof(pop)); }
                while (shown < ph.letters && shown < 9) pop[shown++] = 20;
                hud_sprite_over(true);
                for (int i = 0; i < ph.letters && i < 9; i++) {
                    int x = 0x141 - 64 - LX[i] + (i == 3 || i == 8 ? 0 : 1), y = (i < 4 ? 0x28 : 0x20) - 8, w = 6, h = 7;
                    if (pop[i] > 0) {
                        int t = 20 - pop[i], a = t * 1024 / 20;                 // half a sine over the pop
                        int g = sx_sin_q(a) * 4 / 16384;                      // 0..4 pixels bigger
                        w += g; h += g; x -= g / 2; y -= g / 2;
                        if (!paused) pop[i] -= dvb; if (pop[i] < 0) pop[i] = 0;
                    }
                    hud_sprite(x, y, 6, 7, 0x4d, LI[i] * 7, w, h, WHITE);
                }
                hud_sprite_over(false);
            }
            snprintf(s, sizeof(s), "%d", ph.kmh); hud_text(10, 0xd4 - 48, 2, s, WHITE);          // speed
            hud_text(0x12, 0xd4 - 48, 0, "km/h", WHITE);
            if (combo_drain > 0 && ph.combo_landed > 0) combo_drain = 0;   // a new combo going: the last one's in
            if (combo_drain > 0) {                                               // port: the finished combo counts down
                int cx = race_kind == 1 ? 0x76 - 32 : 0xba - 32;                 // as the score counts it up
                snprintf(s, sizeof(s), "combo x%d", drain_lvl); hud_text(cx + 24, 0xd, 0, s, drain_lvl > 2 ? hud_rgb565(0x7c0) : WHITE);
                snprintf(s, sizeof(s), "%d", combo_drain); hud_text(cx + 24, 0x18, 0, s, WHITE);
                if (!paused) { combo_drain -= drain_step * (int)dvb; if (combo_drain < 0) combo_drain = 0; }
            } else
            if (ph.combo_landed > 0 && ph.combo_ms > 0) {                       // combo: timer pie + level (0x46b5c)
                int cx = race_kind == 1 ? 0x76 - 32 : 0xba - 32;
                hud_sprite(cx, 0xe, 0x15, 0x15, 0x54, (5 - ph.combo_meter) * 0x15, 0x15, 0x15, WHITE);
                snprintf(s, sizeof(s), "combo x%d", ph.combo_lvl); hud_text(cx + 24, 0xd, 0, s, ph.combo_lvl > 2 ? hud_rgb565(0x7c0) : WHITE);
                // what the combo adds if it ends now: its tricks, and its bonus once it is x3 or more
                snprintf(s, sizeof(s), "%d", combo_pts + (ph.combo_lvl > 2 ? ph.combo_bank : 0)); hud_text(cx + 24, 0x18, 0, s, WHITE);
            }
            if (ph.mult > 1) { snprintf(s, sizeof(s), "x%d", ph.mult); hud_text(0xa0 - 32, 0x21 - 8, 2, s, GREEN); }
            if (race_kind == 2 && ph.points && (ph.air || ph.rail_state)) {   // trick events: rating stars over the points (0x45d80)
                hud_stars(ph.points, 0, 0); snprintf(s, sizeof(s), "%d", ph.points); hud_text(0xa3 - 32, 0x50 - 24, 2, s, WHITE);
            } else if (ph.points && (ph.air || ph.rail_state)) { snprintf(s, sizeof(s), "%d", ph.points); hud_outline(true); hud_text(128, 40, 2, s, WHITE); hud_outline(false); }
            else if (trick_msg_t > 0) {
                // the trick's name (0x45128) over its points: in the menus' smaller font with a dark outline (the HUD's
                // big font ran across most of the screen and was hard to read over snow and fog); long names wrap
                hud_font(2); hud_outline(true);
                const char *nm = trick_msg_text[0] ? trick_msg_text : trick_msg_id >= 0 ? phys_trick_name(trick_msg_id) : "trick";
                if (!trick_msg_pts) wrap_hud_c(128, 36, 200, nm, WHITE, 11);
                else {
                    int y = 36 + (hud_text_width(nm) > 200 ? 0 : 0);
                    int lines = wrap_hud_c(128, y, 200, nm, WHITE, 11);
                    snprintf(s, sizeof(s), "+%d", trick_msg_pts); hud_text(128, y + lines * 11, 2, s, YELLOW);
                }
                hud_outline(false); hud_font(0);
            }
            {   // port addition: the landing that lights the ninth letter (unlimited boost, the level 3 ubers)
                static int su_t;
                if (ph.super_uber_now) { ph.super_uber_now = false; su_t = 150; }
                if (su_t > 0) {
                    if ((su_t / 8) & 1 || su_t < 60) hud_text(128, 58, 2, "SUPER UBER!", YELLOW);
                    if (!paused) su_t -= dvb;
                }
            }
            {   // the first time the meter fills in a race: the exe's control hint "Press @ to tweak/UBER." (@ = the button)
                static bool was_full; static int tip_ms;
                if (ph.boost_full && !was_full && !uber_tip_done) { tip_ms = 3000; uber_tip_done = true; }
                was_full = ph.boost_full;
                if (tip_ms > 0 && !(ph.points && (ph.air || ph.rail_state)) && trick_msg_t <= 0) {
                    char t[96]; const char *m = L(XT_78); int k = 0;
                    for (const char *c = m; *c && k < 90; c++) { if (*c == '@') { t[k++] = 'B'; while (c[1] == ' ' && c[2] == ' ') c++; } else t[k++] = *c; }
                    t[k] = 0; hud_text(128, 40, 2, t, hud_rgb565(0x7c0));
                }
                if (!paused && tip_ms > 0) tip_ms -= dvb * 17;
            }
            if (star_pts) {                                                  // the landed trick's stars drift up and right
                hud_stars(star_pts, star_dx, star_dy);
                if (!paused) { star_dx += 3; star_dy -= 2; if (star_dx > 0x46) star_pts = 0; }
            }
            if (ph.jstate == 7) {                                            // wipeout: mash B to fill the recovery bar (0x46b5c state 7)
                hud_text(128, 0xc2 - 48, 2, ph.can_rise ? "Recovered!" : "Recover! (B)", WHITE);
                int w = ph.recover * 0x30 / 100, bx = 0x88 - 32, by = 0xd0 - 48;
                const u16 RIM = hud_rgb565(0xb596);
                hud_rect(bx, by, w, 6, RGB15(31, 0, 0), 3);
                hud_rect(bx + w, by, 0x30 - w, 6, RGB15(31, 31, 31), 3);
                hud_rect(bx - 1, by - 1, 0x32, 1, RIM, 4); hud_rect(bx - 1, by + 6, 0x32, 1, RIM, 4);
                hud_rect(bx - 1, by, 1, 6, RIM, 4); hud_rect(bx + 0x30, by, 1, 6, RIM, 4);
            }
            }
            // pause and results in the menus' style (0x7098c "Rewards"): a dark panel, an orange title strip,
            // midfont lines, the button hints in the small font
            #define PANEL(x, y, w, h, title) do { hud_rect_a(x, y, w, h, RGB15(0, 0, 0), 18, 1); \
                hud_rect(x, y, w, 16, RGB15(22, 10, 2), 2); hud_font(1); hud_text(x + 6, y + 1, 0, title, WHITE); } while (0)
            if (fe_mp() && net_s.paused_by != 0xff && !paused && race != R_INTRO) {   // 0x31610: another player paused
                char pm[64]; snprintf(pm, sizeof(pm), XT_79, net_s.ctl[net_s.paused_by].name);
                hud_rect_a(0, 70, 256, 44, hud_rgb565(0x1a4f), 24, 3);
                hud_font(2); wrap_hud(16, 78, 224, pm, WHITE, 13);
            }
            if (paused) {                                                   // 0x31254: the scene dimmed, pauseicons.bmp rows
                static const char *const PI[4] = { "Return", "Restart", "Options", "Give Up" };
                static const char *const PH[4] = { XT_80, XT_81,
                                                   XT_82, XT_83 };
                static const u8 PIC[4] = { 0, 1, 4, 5 };                  // icons: play, back arrow, turn arrow, stop
                const u16 SEL = hud_rgb565(0xfd41), BG = hud_rgb565(0x1a4f);
                hud_rect_a(0, 0, 256, 192, BG, 22, 1);
                hud_rect(0, 156, 256, 36, BG, 1);
                hud_rect(0, 156, 256, 1, SEL, 2);
                int bt = fe_image("dsbuttons", NULL, NULL);
                hud_font(1); hud_text(128, 22, 2, p_opt ? "Options" : LX("Paused", "Pause Menu"), WHITE);
                const char *help;
                if (!p_opt) {
                    for (int i = 0; i < 4; i++) {
                        int y = 52 + i * 20, on = i == p_sel;
                        hud_sprite(72, y, 16, 16, on ? 192 : 176, PIC[i] * 16, 16, 16, RGB15(31, 31, 31));   // pauseicons.bmp, packed in the HUD sheet
                        hud_font(2); hud_text(96, y + 2, 0, PI[i], i == 1 && fe_mp() ? RGB15(14, 14, 16) : on ? SEL : WHITE);
                    }
                    help = PH[p_sel];
                } else {
                    const char *const OI[5] = { XT_62, LX("Sound FX Volume", "Sound FX"), "Camera", "Frame Rate", "Rumble" };
                    static const char *const OH[5] = { XT_63, XT_64,
                                                       XT_65, "Cap the race at 30 frames a second.",
                                                       "Shake the Rumble Pak on crashes, landings and bumps." };
                    static const char *const CAM[3] = { "Near", "Mid", "Far" };
                    char vb[24];
                    for (int i = 0; i < 5; i++) {
                        int y = 48 + i * 19, on = i == p_osel;
                        if (i < 2) snprintf(vb, sizeof(vb), "%d", i ? g_opt.fx : g_opt.master);
                        else if (i == 4) snprintf(vb, sizeof(vb), "%s", !rumble_present() ? L("No Pak") : g_opt.rumble ? L("On") : L("Off"));
                        else snprintf(vb, sizeof(vb), "%s", i == 2 ? CAM[g_opt.cam] : g_opt.fps30 ? "30" : "60");
                        hud_font(2); hud_text(40, y + 2, 0, OI[i], on ? SEL : WHITE);
                        hud_text(216, y + 2, 1, vb, on ? SEL : WHITE);
                        if (on) { hud_text(150, y + 2, 2, "<", SEL); hud_text(224, y + 2, 0, ">", SEL); }
                    }
                    help = OH[p_osel];
                }
                hud_font(3); hud_text(8, 162, 0, help, WHITE);
                {   // A and B hints right-aligned, spaced by their (translated) widths
                    const char *ta = p_opt ? LX("Done", "Accept") : "Select", *tb = "Back";
                    int wb = p_opt ? 0 : 14 + hud_text_width(tb) + 8, wa = 14 + hud_text_width(ta);
                    int xa = 250 - wb - wa;
                    hud_image(bt, xa, 174, 11, 11, 12, 0, 11, 11, WHITE, 3); hud_text(xa + 14, 175, 0, ta, WHITE);
                    if (!p_opt) { hud_image(bt, xa + wa + 8, 174, 11, 11, 0, 0, 11, 11, WHITE, 3); hud_text(xa + wa + 22, 175, 0, tb, WHITE); }
                }
            }
            const u16 ORANGE = RGB15(31, 19, 3);
            if (race == R_INTRO && !paused) {                               // the event panel at the gate (0x6a008, bit 1)
                snprintf(s, sizeof(s), "%s - %s", EVENTS[cur_event].name, L(race_kind == 1 ? "Race" : strstr(EVENTS[cur_event].kind, "PIPE") ? "Super Pipe" : "Slopestyle"));
                PANEL(12, 18, 232, 150, s);
                hud_font(2); hud_text(20, 37, 0, round_name(false), ORANGE);
                char d[128];
                if (heats() && race_kind == 1)
                    snprintf(d, sizeof(d), "%s", ev_round == 0 ? XT_84
                                              : ev_round == 1 ? XT_85
                                                              : XT_86);
                else if (heats() && ev_round == 0) snprintf(d, sizeof(d), XT_87 " " XT_88);
                else if (heats() && ev_round == 1) {
                    s32 need = third_total() - pscore[0] + 1;
                    if (need > 0) snprintf(d, sizeof(d), "Earn %ld" XT_89, (long)need);
                    else snprintf(d, sizeof(d), XT_88);
                } else if (heats()) snprintf(d, sizeof(d), XT_90 " " XT_91);
                else if (fe_shadow()) snprintf(d, sizeof(d), "%s", race_kind == 1 ? XT_92
                                               : strstr(EVENTS[cur_event].kind, "PIPE") ? XT_93
                                                                                        : XT_94);
                else if (strstr(EVENTS[cur_event].kind, "rival")) snprintf(d, sizeof(d), "%s", EVENTS[cur_event].desc);
                else snprintf(d, sizeof(d), "%s", race_kind == 1 ? XT_95 : XT_91);
                hud_font(3); int y = 50 + wrap_hud(20, 50, 216, d, WHITE, 9) + 4;
                char nm[12];
                if (race_kind == 2 && heats() && ev_round > 0) {             // [...]: the combined totals
                    hud_font(2); hud_text(20, y, 0, XT_96, ORANGE); y += 12; hud_font(3);
                    int ord[6]; s32 v[6]; int n = 0;
                    v[n] = ev_round == 1 ? pscore[0] : 0; ord[n++] = -1;
                    for (int k = 0; k < 5; k++) { v[n] = ev_round == 1 ? stand[k].h1 : stand[k].h1 + stand[k].h2; ord[n++] = k; }
                    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (v[j] > v[i]) { s32 x = v[i]; v[i] = v[j]; v[j] = x; int o = ord[i]; ord[i] = ord[j]; ord[j] = o; }
                    for (int i = 0; i < n; i++) {
                        if (ord[i] < 0) cap_name(nm, ri); else cap_name(nm, stand[ord[i]].rk);
                        u16 c = ord[i] < 0 ? ORANGE : WHITE;
                        snprintf(s, sizeof(s), "%d", i + 1); hud_text(30, y, 1, s, c);
                        hud_text(40, y, 0, nm, c);
                        snprintf(s, sizeof(s), "%ld", (long)v[i]); hud_text(220, y, 1, s, c); y += 9;
                    }
                } else {                                                    // Riders
                    hud_font(2); hud_text(20, y, 0, "Riders", ORANGE); y += 12; hud_font(3);
                    cap_name(nm, ri); hud_text(30, y, 0, nm, ORANGE); y += 9;
                    for (int k = 0; k < nghost; k++) {
                        cap_name(nm, grd_char[k]);
                        if (fe_shadow()) { snprintf(s, sizeof(s), "%s (Shadow)", nm); hud_text(30, y, 0, s, WHITE); }
                        else hud_text(30, y, 0, nm, WHITE);
                        y += 9;
                    }
                    if (race_kind == 1 && best_ms[cur_event]) { char tb3[16]; fmt_time(tb3, best_ms[cur_event]); snprintf(s, sizeof(s), "Record  %s", tb3); hud_text(220, 37 + 2, 1, s, WHITE); }
                }
                if ((intro_ms / 400) & 1) { hud_font(2); hud_text(128, 154, 2, fe_mp() ? XT_97 "other riders" : "Press A to start", WHITE); }
            }
            if (replay) {                                                   // replay overlay
                hud_font(1);
                {   // rewinding / fast-forwarding: replay.bmp's arrows (in the HUD sheet, u 208) instead of the blinking label
                    u32 hk = keysHeld(); int dir = rp_pause ? 0 : (hk & KEY_LEFT) ? 1 : (hk & KEY_RIGHT) ? 2 : 0;
                    if (dir) { hud_sprite(6, 6, 16, 8, 208, dir == 1 ? 0 : 8, 32, 16, RGB15(31, 31, 31));
                               hud_text(42, 4, 0, dir == 1 ? "Rewind" : "Fast", ORANGE); }
                    else if (rp_pause || ((rp_tick / 12) & 1)) hud_text(6, 4, 0, rp_pause ? "Paused" : rp_slow ? "Slow motion" : "Replay", ORANGE);
                }
                char tb4[16]; int ms = (rp_steps - 74) * PHYS_TICK_MS; fmt_time(tb4, ms > 0 ? ms : 0);
                hud_font(0); hud_text(252, 2, 1, tb4, WHITE);
                {   // the replay legend (the exe's 0x20610 bit 0x40: Exit / Play/Pause / Slow / Camera beside
                    // [...]), with the DS's own buttons (menu/dsbuttons: B, A, D-pad, L, R)
                    int iw, ih, it = fe_image("dsbuttons", &iw, &ih);
                    static const struct { int u; const char *t; } LG[5] = {
                        { 0, "Exit" }, { 12, "Play/Pause" }, { 24, "Rewind / Fast" }, { 36, "Slow" }, { 48, "Camera" } };
                    hud_rect_a(2, 128, 84, 62, RGB15(0, 0, 0), 14, 1);
                    hud_font(3);
                    for (int i = 0; i < 5; i++) {
                        int y = 130 + i * 12;
                        hud_image(it, 5, y, 11, 11, LG[i].u, 0, 11, 11, WHITE, 3);
                        hud_text(19, y + 1, 0, i == 3 && rp_slow ? "Slow (on)" : LG[i].t, WHITE);
                    }
                }
            }
            if (race == R_DONE && ph.race_over && !replay && show_rec) {                // Records: every event's best
                PANEL(12, 18, 232, 156, "Records");
                hud_font(3); int y = 37;
                hud_text(20, y, 0, "Event", ORANGE); hud_text(236, y, 1, "Record", ORANGE); y += 10;
                for (int e = 0; e < 15; e++) {
                    u16 c = e == cur_event ? ORANGE : WHITE;
                    hud_text(20, y, 0, EVENTS[e].name, c);
                    char rb[20] = "--";
                    if (EVENTS[e].timed && best_ms[e]) fmt_time(rb, best_ms[e]);
                    else if (!EVENTS[e].timed && shadow[e]) snprintf(rb, sizeof(rb), "%ld pts", (long)((s32 *)shadow[e])[4]);
                    hud_text(236, y, 1, rb, c);
                    if (shadow[e]) { int rk = ((s32 *)shadow[e])[1]; if (rk >= 0 && rk < NRIDERS) { char nm[12]; cap_name(nm, rk); hud_text(150, y, 0, nm, c); } }
                    y += 9;
                }
                hud_text(128, 182, 2, "Y  Back", WHITE);
            } else
            if (race == R_DONE && ph.race_over && fe_mp()) {                             // [...] (0x6a008)
                PANEL(12, 18, 232, 156, XT_98);
                int y = 38;
                hud_font(3);
                hud_text(20, y, 0, "Rank", ORANGE); hud_text(60, y, 0, "Riders", ORANGE); hud_text(236, y, 1, race_kind == 1 ? "Time" : "Score", ORANGE); y += 11;
                int ord[NET_MAX], n = 0; s32 v[NET_MAX]; bool done[NET_MAX];
                v[n] = race_kind == 1 ? fin_time : ph.score; done[n] = true; ord[n++] = -1;
                for (int k = 0; k < nghost; k++) { v[n] = ghosts[k].result; done[n] = ghosts[k].finished; ord[n++] = k; }
                for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) {
                    int pi_ = ord[i] < 0 ? net_me() : ghosts[ord[i]].pid, pj = ord[j] < 0 ? net_me() : ghosts[ord[j]].pid;   // ties: lower player number
                    bool sw = done[j] && (!done[i] || (race_kind == 1 ? v[j] < v[i] : v[j] > v[i]) || (v[j] == v[i] && pj < pi_));
                    if (sw) { s32 x = v[i]; v[i] = v[j]; v[j] = x; int o = ord[i]; ord[i] = ord[j]; ord[j] = o; bool d = done[i]; done[i] = done[j]; done[j] = d; }
                }
                bool all = true;
                for (int i = 0; i < n; i++) {
                    const char *nm = ord[i] < 0 ? net_ctl.name : net_s.ctl[ghosts[ord[i]].pid].name;
                    char rn[12]; cap_name(rn, ord[i] < 0 ? ri : grd_char[ord[i]]);
                    u16 c = ord[i] < 0 ? ORANGE : WHITE;
                    snprintf(s, sizeof(s), "%d", i + 1); hud_text(30, y, 1, s, c);
                    snprintf(s, sizeof(s), "%s (%s)", nm, rn); hud_text(60, y, 0, s, c);
                    bool gone = ord[i] >= 0 && net_age(ghosts[ord[i]].pid) >= 180;
                    if (!done[i]) { hud_text(236, y, 1, gone ? "Left" : "Riding...", c); if (!gone) all = false; }
                    else if (race_kind == 1) { char tb[16]; fmt_time(tb, v[i]); hud_text(236, y, 1, tb, c); }
                    else { snprintf(s, sizeof(s), "%ld", (long)v[i]); hud_text(236, y, 1, s, c); }
                    y += 11;
                }
                int my = 1; for (int i = 0; i < n; i++) if (ord[i] < 0) my = i + 1;
                y += 6; hud_font(2);
                const char *m = !all ? XT_97 "other riders" : my == 1 ? XT_99 : XT_100;
                wrap_hud(20, y, 216, m, all && my == 1 ? GREEN : WHITE, 12);
                hud_font(3);
                if (net_is_host()) hud_text(128, 162, 2, all ? "A  Next event   B  End session" : "B  End session", WHITE);
                else hud_text(128, 162, 2, XT_97 "host   B  Leave", WHITE);
            }
            else if (race == R_DONE && ph.race_over && !replay) {                             // results (0x6a008 bit 8; 0x7098c rewards)
                PANEL(12, 18, 232, 156, round_name(true));
                int y = 38; char nm[12], tb2[16];
                hud_font(3);
                if (race_kind == 1 && nghost) {                             // Rank  Riders  Time
                    hud_text(20, y, 0, "Rank", ORANGE); hud_text(60, y, 0, "Riders", ORANGE); hud_text(236, y, 1, "Time", ORANGE); y += 11;
                    for (int i = 0; i < nres; i++) {
                        u16 c = res_rk[i] < 0 ? ORANGE : WHITE;
                        cap_name(nm, res_rk[i] < 0 ? ri : res_rk[i]);
                        snprintf(s, sizeof(s), "%d", i + 1); hud_text(30, y, 1, s, c);
                        if (fe_shadow() && res_rk[i] >= 0) { char sn[24]; snprintf(sn, sizeof(sn), "%s (Shadow)", nm); hud_text(60, y, 0, sn, c); }
                        else hud_text(60, y, 0, nm, c);
                        fmt_time(tb2, res_ms[i]); hud_text(236, y, 1, tb2, c); y += 10;
                    }
                    if (new_best) { hud_text(128, y, 2, LX("New best time!", "New Record!"), GREEN); y += 10; }
                } else if (race_kind == 2 && heats()) {                      // the six standings
                    hud_text(20, y, 0, "Rank", ORANGE); hud_text(60, y, 0, "Riders", ORANGE); hud_text(236, y, 1, ev_round == 2 ? "Score" : "Total", ORANGE); y += 11;
                    int ord[6]; s32 v[6]; int n = 0;
                    v[n] = player_total(); ord[n++] = -1;
                    for (int k = 0; k < 5; k++) { v[n] = ai_total(k); ord[n++] = k; }
                    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (v[j] > v[i] || (v[j] == v[i] && ord[j] < 0)) { s32 x = v[i]; v[i] = v[j]; v[j] = x; int o = ord[i]; ord[i] = ord[j]; ord[j] = o; }
                    for (int i = 0; i < n; i++) {
                        if (ord[i] < 0) cap_name(nm, ri); else cap_name(nm, stand[ord[i]].rk);
                        u16 c = ord[i] < 0 ? ORANGE : WHITE;
                        snprintf(s, sizeof(s), "%d", i + 1); hud_text(30, y, 1, s, c); hud_text(60, y, 0, nm, c);
                        snprintf(s, sizeof(s), "%ld", (long)v[i]); hud_text(236, y, 1, s, c); y += 9;
                    }
                } else {
                    hud_font(2);
                    fmt_time(tb2, fin_time);
                    if (race_kind == 2 && nghost && (strstr(EVENTS[cur_event].kind, "rival") || fe_shadow())) snprintf(s, sizeof(s), "%d pts  vs  %d", ph.score, ghosts[0].result);
                    else if (race_kind == 2) snprintf(s, sizeof(s), "%s place   %d pts", ordinal(fin_place), ph.score);
                    else snprintf(s, sizeof(s), "%s", tb2);
                    hud_text(128, y + 4, 2, s, WHITE); y += 20;
                    hud_font(3);
                }
                {   // the verdict (0x6a008): qualified / finished in a place / sorry
                    const char *m = NULL; char mb[80];
                    if (heats() && ev_round < 2 && !(race_kind == 2 && ev_round == 0))
                        m = fin_place <= 3 ? (ev_round == 0 ? XT_101 : XT_102)
                                           : (ev_round == 0 ? XT_103 "final."
                                                            : XT_104);
                    else if (heats() && ev_round == 0) {
                        s32 need = third_total2(true) - pscore[0] + 1;   // 0x70db0: against the AI's two-heat totals
                        if (need > 0) { snprintf(mb, sizeof(mb), "Earn %ld" XT_89, (long)need); m = mb; }
                        else m = XT_88;
                    } else if (strstr(EVENTS[cur_event].kind, "rival") || fe_shadow()) m = fin_place == 1 ? XT_99 : XT_100;
                    else m = fin_place == 1 ? XT_105 : fin_place == 2 ? XT_106
                           : fin_place == 3 ? XT_107 : XT_108;
                    y += 3 + wrap_hud(20, y + 3, 216, m, WHITE, 9);
                }
                {   // rewards (0x7098c): cash, newly unlocked events
                    char c[20], c2[20]; y += 3;
                    hud_font(2);
                    if (shadow_new) { hud_text(128, y, 2, "New shadow saved", GREEN); y += 12; }
                    if (pay_cash > 0) { career_fmt(c, pay_cash); snprintf(s, sizeof(s), "Cash: %s", c); hud_text(128, y, 2, s, YELLOW); y += 12; }
                    for (int k = 0; k < pay_nunl && k < 2; k++) { snprintf(s, sizeof(s), "Unlocked: %s", EVENTS[pay_unl[k]].name); hud_text(128, y, 2, s, GREEN); y += 12; }
                    if (fe_career()) { career_fmt(c2, career.cash); snprintf(s, sizeof(s), "Available Cash: %s", c2); hud_font(3); hud_text(128, 150, 2, s, WHITE); }
                }
                hud_font(3);
                if (ph.race_over) {
                    // (A: the next heat while the event goes on, else back to the menu; B: run it again)
                    bool more = heats() && round_ok && ev_round < 2;
                    const char *ta = more ? L(ev_round == 0 ? (race_kind == 1 ? "Next heat" : "Heat 2") : "Final Round") : L("Quit");
                    const char *tb = more ? L("Quit") : heats() && round_ok ? L("Start over") : LX("Race again", "Restart");
                    snprintf(s, sizeof(s), "A  %s   X  %s   Y  %s   B  %s", ta, L("Replay"), L("Records"), tb);
                    if (hud_text_width(s) > 248) snprintf(s, sizeof(s), "A %s  X %s  Y %s  B %s", ta, L("Replay"), L("Records"), tb);
                    if (hud_text_width(s) <= 248) hud_text(128, 162, 2, s, WHITE);
                    else {                                     // still too wide (long translations): two lines
                        snprintf(s, sizeof(s), "A %s   X %s", ta, L("Replay")); hud_text(128, 156, 2, s, WHITE);
                        snprintf(s, sizeof(s), "Y %s   B %s", L("Records"), tb); hud_text(128, 165, 2, s, WHITE);
                    }
                }
            }
            hud_font(0);
        }
        hud_end();
        {   // port: the course's far tiles last (track_draw_far): what fills polygon / vertex RAM before them is what
            // the LOD bias has to keep under the limits; an overflow only drops the farthest, fogged tiles
            // (reading the counts here waits for the geometry engine to catch up: only when the last frame's whole
            // was near the limits - otherwise its total bounds the near part)
            near_read = last_polys > 1800 || last_verts > 5400;
            if (near_read) {
                while (GFX_STATUS & BIT(27));
                glGetInt(GL_GET_POLYGON_RAM_COUNT, &near_polys);
                glGetInt(GL_GET_VERTEX_RAM_COUNT, &near_verts);
            }
            glPolyFmt(POLY_ALPHA(31) | track_cull | POLY_FOG); glColor(RGB15(31, 31, 31));
            submitted += track_draw_far(&trk);
        }

        int l_rider = NOW_LINE();
        frame_us = (NOW_LINE() - line0) * 6355 / 100;   // scanlines -> us
        #define LN(a, b) ((b) - (a))
        int prof[5] = { LN(line0, l_phys), LN(l_phys, l_stream), LN(l_stream, l_cam), LN(l_cam, l_track), LN(l_track, l_rider) };
#ifdef TEST_PHYSPROF
        int prof2[5] = { php[0], php[1], php[2], php[3], php[4] };
#elif defined(TEST_RIDERPROF)
        int prof2[5] = { rprof[0], rprof[1], rprof[2], rprof[3], rprof[4] }; memset(rprof, 0, sizeof(rprof));
#elif defined(TEST_FXPROF)
        int prof2[5] = { fxp[0], fxp[1], fxp[2], fxp[3], fxp[4] };
#else
        int prof2[5] = { LN(l_cam, l_trk0), LN(l_trk0, l_track), LN(l_track, l_r1), LN(l_r1, l_r2), LN(l_r2, l_fx) };
#endif
        (void)prof2;
        (void)frame_us;
        while (GFX_STATUS & BIT(27));
        glGetInt(GL_GET_POLYGON_RAM_COUNT, &last_polys);
        glGetInt(GL_GET_VERTEX_RAM_COUNT, &last_verts);
        if (!near_read) { near_polys = last_polys; near_verts = last_verts; }
        glFlush(0);
        swiWaitForVBlank();
        {   // Options "Frame Rate: 30": hold each frame for two vblanks (the physics runs on the vblank clock either way)
            static u32 last_flip;
            if (g_opt.fps30) while (vblanks - last_flip < 2) swiWaitForVBlank();
            last_flip = vblanks;
        }
#ifdef TEST_LOG
        {   // per-frame log in RAM for the emulator harness (dsrun --ram): race-line position, polygons and vertices
            // reaching the hardware, submitted, vblanks, bias, segments due but missing, scanlines per frame part
            typedef struct { s32 s; u16 polys, verts, sub; u8 dvb, bias, miss, p[5], q[5], pad[1]; } LogE;
            static struct { char magic[8]; u32 n; LogE e[6000]; } lg = { "SSXLOG3", 0, {{0}} };
            if (race == R_RACE && !paused && lg.n < 6000) {
                LogE *e = &lg.e[lg.n++];
                e->s = (s32)ph.s; e->polys = last_polys; e->verts = last_verts; e->sub = submitted;
                e->dvb = dvb; e->bias = bias; e->miss = trk.missing;
#ifdef TEST_TEXFAIL
                e->miss = trk.tex_fail + trk.pal_fail * 16 + rider_tex_fail * 64;
#endif 
                for (int k = 0; k < 5; k++) { e->p[k] = prof[k] > 255 ? 255 : prof[k]; e->q[k] = prof2[k] > 255 ? 255 : prof2[k]; }
                e->pad[0] = trk.loads_this_frame | (trk.sync_loads << 4);
            }
        }
#endif
        if (adaptive) {
            // drive the LOD bias by what reaches the hardware (polygon RAM 2048, vertex RAM 6144) and the frame
            // rate, not by what is submitted: most submitted polygons are culled, and the coarsest LODs open holes
            static int up_n, down_n;
            float fmax = g_opt.fps30 ? 30 : 60;                    // judge the frame rate against the cap
            // (no longer by the polygons submitted: with the frame's float maths gone and the course in strips, the
            // wide slopestyle courses held the frame rate at full detail but were pinned at the coarsest bias by it)
            // (by what came before the far tiles: those may overflow - the farthest are dropped, in the fog)
            bool over = near_polys > 1850 || near_verts > 5600 || fps < fmax * 0.8f;
            bool under = near_polys < 1350 && near_verts < 4200 && fps > fmax * 0.93f;
            bool hw_over = near_polys > 1900 || near_verts > 5800;
            // every change of bias shows as the whole course's detail switching: the hardware limits (overflowing
            // polygon / vertex RAM drops polygons - holes) step it at once, a low frame rate only after a second of
            // it, and it comes back down only after 8 s of room
            up_n = over ? up_n + 1 : 0; down_n = under ? down_n + 1 : 0;
            // slow to change (a step up after 6 bad frames, back after 2 s of headroom): every change shows as pop-in
            if (up_n >= (hw_over ? 2 : 60) && (bias < 2 || (bias < 4 && hw_over))) { bias++; up_n = 0; down_n = 0; }
            else if (down_n >= 480 && bias > 0) { bias--; down_n = 0; }
        }
        if (trick_msg_t > 0) trick_msg_t -= dvb;
        static int frame_n = 0, print_lines = 0;
        if (submap_active() && (frame_n & 1)) {                    // the trail map's dots (30 Hz)
            s32 me[3], oth[3][3]; const Phys *vp = replay ? &rpg.ph : &ph;
            for (int j = 0; j < 3; j++) me[j] = vp->pos[j] >> 8;
            int no = 0;
            for (int k = 0; k < nghost; k++) if (ghosts[k].n) { for (int j = 0; j < 3; j++) oth[no][j] = ghosts[k].ph.pos[j] >> 8; no++; }
            submap_dim(!replay && (race == R_INTRO || race == R_COUNT));   // greyed at the gate
            submap_update(me, (const s32 (*)[3])oth, no);
            if (frame_n % 6 == 1) standings(&ph, replay, race, race_steps, fin_time, ri);
        }
        if (++frame_n % 6 == 0 && !submap_active()) {              // the text console is slow: refresh at 10 Hz
            int l_print0 = NOW_LINE();
            static const char *JS[] = { "ride", "?", "crouch", "takeoff", "air", "land", "?", "WIPEOUT" };
            int ifps = (int)(fps * 10);
            char tb[16], bb[16];
            fmt_time(tb, race == R_DONE ? fin_time : race_steps * PHYS_TICK_MS); fmt_time(bb, best_ms[cur_event]);
            char pl[8] = "     ";
            if (race_kind == 1 && nghost) snprintf(pl, sizeof(pl), "%s/%d", ordinal(race == R_DONE ? fin_place : place), nghost + 1);
            printf("\x1b[0;0H%-4s %s %s best %s%s\n", TRACKS[ti], tb, pl, bb, autopilot ? "*" : " ");
            printf("%3d km/h %-7s %s %s\n", ph.kmh, ph.rail_state ? "GRIND" : JS[ph.jstate & 7], ph.stance != ph.natural ? "switch" : "      ",
                   ph.boosting ? "BOOST" : "     ");
            // boost meter bar (0x6400 = full); level 1..3 = uber tricks available
            char bar[17]; int n = ph.boost * 16 / 0x6400;
            for (int i = 0; i < 16; i++) bar[i] = i < n ? '#' : '.';
            bar[16] = 0;
            printf("boost [%s] %s\n", bar, ph.boost_level == 3 ? "MAX" : ph.boost_level ? (ph.boost_level == 2 ? "LV2" : "LV1") : "   ");
            if (ph.rail_state == 2) {                              // rail balance
                char bb[23]; int pos = 11 + ph.balance * 11 / (ph.bal_limit ? ph.bal_limit : 1);
                for (int i = 0; i < 22; i++) bb[i] = i == pos ? '|' : (i == 11 ? '+' : '-');
                bb[22] = 0;
                printf("bal <%s>  \n", bb);
            } else if (ph.points) printf("trick %5d pts %s       \n", ph.points, ph.land_ok ? "        " : "(unsafe)");
            else if (trick_msg_t > 0) printf("%-22.22s +%5d\n", trick_msg_text[0] ? trick_msg_text : trick_msg_id >= 0 ? phys_trick_name(trick_msg_id) : "trick", trick_msg_pts);
            else printf("                              \n");
            printf("score %6d  wipeouts %d   \n", ph.score, ph.crashes);
            printf("rails %d resp %d kd %d/%d st %d   \n", ph.rails_ridden, ph.respawns, fights[2], fights[1], fights[3]);
            printf("fps %2d.%d polys %4d vtx %4d  \n", ifps / 10, ifps % 10, last_polys, last_verts);
            printf("submit %4d bias %d %s seg %2d\n", submitted, bias, adaptive ? "auto" : "off ", trk.resident);
            printf("phys %4dus rays %d/%3d       \n", phys_us, trk.rays, trk.tri_tests);
            printf("ln ph%3d st%2d cm%2d tr%3d rd%3d\n", prof[0], prof[1], prof[2], prof[3], prof[4]);
            printf("rider %-6s %s anim %3d tf %d\n", RIDERS[ri], hi ? "high" : "low ", ph.anim, rider_tex_fail + trk.pal_fail * 1000);
            if (race == R_INTRO) printf("%-32s", "   press A to start");
            else if (race == R_COUNT) printf("          ---  %d  ---         ", 3 - count_ms / 1000);
            else if (race == R_DONE) printf("FINISH %s %s %s %s", tb, race_kind == 1 && nghost ? ordinal(fin_place) : "   ",
                                            new_best ? "BEST" : "    ", ph.race_over ? "A:again" : "       ");
            else if (race_steps < 40) printf("             GO!                ");
            else printf("%s%s x%-2d pick %2d/%-2d           ", ph.speed_ms ? "SPEED " : "      ", ph.trick_ms ? "TRICK " : "      ",
                        ph.mult, ph.picks_taken, trk.npicks);
            print_lines = LN(l_print0, NOW_LINE());
        }
    }
    return 0;
}

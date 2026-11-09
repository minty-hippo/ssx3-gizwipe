// SSX 3 GizWipe - skinned rider runtime
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "rider.h"
#include "hud.h"

typedef struct { char magic[4]; u32 ver, nmesh, ntex, off_meshes, off_textures; } RHdr;
typedef struct { char name[16]; s16 tex; u8 skinned, nslots; u8 slot_bone[24]; u16 npolys, pad; u32 off_dl; } RMesh;  // 52
typedef struct { char name[24]; u16 w, h; u8 fmt, flags; u16 pad; u32 off_texels, off_pal; } RTex;                    // 40

static RHdr rh;

bool rider_open(Rider *r)
{
    memset(r, 0, sizeof(*r));
    r->rf = fopen("nitro:/riders.ssxr", "rb");
    r->af = fopen("nitro:/anims.ssxa", "rb");
    if (!r->rf || !r->af) { printf("rider files missing\n"); return false; }
    fread(&rh, sizeof(rh), 1, r->rf);
    char magic[4]; u32 v[3];
    fread(magic, 4, 1, r->af); fread(v, 4, 3, r->af);
    r->nanims = v[2];
    fread(r->parent, 1, 32, r->af);
    r->recs = malloc(r->nanims * sizeof(AClip));
    r->cache = calloc(r->nanims, sizeof(Clip));
    fseek(r->af, 48, SEEK_SET); fread(r->recs, sizeof(AClip), r->nanims, r->af);
    return memcmp(rh.magic, "SSXR", 4) == 0 && memcmp(magic, "SSXA", 4) == 0;
}

#define NTEXC 128
static int tex_id[NTEXC], tex_ref[NTEXC];                // GL texture per riders.ssxr texture, shared between meshes

int rider_tex_fail;
static int upload_tex(FILE *f, int ti)
{
    if (ti < NTEXC && tex_ref[ti]) { tex_ref[ti]++; return tex_id[ti]; }
    RTex t;
    fseek(f, rh.off_textures + ti * sizeof(RTex), SEEK_SET); fread(&t, sizeof(t), 1, f);
    void *tx = malloc(t.w * t.h), *pal = malloc(512);
    fseek(f, t.off_texels, SEEK_SET); fread(tx, 1, t.w * t.h, f);
    fseek(f, t.off_pal, SEEK_SET); fread(pal, 1, 512, f);
    int id; glGenTextures(1, &id);
    if (!tex_upload(id, GL_RGB256, t.w, t.h, TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T |   // rider UVs run negative
                    ((t.flags & 1) ? GL_TEXTURE_COLOR0_TRANSPARENT : 0), tx, pal, 256)) rider_tex_fail++;   // colour-keyed sprites (trees)
    free(tx); free(pal);
    if (ti < NTEXC) { tex_id[ti] = id; tex_ref[ti] = 1; }
    return id;
}

bool rider_load_mesh(Rider *r, Mesh *m, const char *name)
{
    memset(m, 0, sizeof(*m));
    for (u32 i = 0; i < rh.nmesh; i++) {
        RMesh rm;
        fseek(r->rf, rh.off_meshes + i * sizeof(RMesh), SEEK_SET); fread(&rm, sizeof(rm), 1, r->rf);
        if (strncmp(rm.name, name, 16)) continue;
        u32 nw; fseek(r->rf, rm.off_dl, SEEK_SET); fread(&nw, 4, 1, r->rf);
        m->dl = malloc((nw + 1) * 4); m->dl[0] = nw; fread(m->dl + 1, 4, nw, r->rf);
        DC_FlushRange(m->dl, (nw + 1) * 4);                // once: gx_call sends it without flushing each time
        m->nslots = rm.nslots; memcpy(m->slot_bone, rm.slot_bone, 24); m->npolys = rm.npolys;
        m->tex = rm.tex >= 0 ? upload_tex(r->rf, rm.tex) : 0;
        m->tex_idx = rm.tex;
        return true;
    }
    printf("mesh %s not found\n", name);
    return false;
}

// dress a loaded mesh in another riders.ssxr texture, by file name (the riders' own boards)
bool rider_set_tex(Rider *r, Mesh *m, const char *texname)
{
    for (u32 i = 0; i < rh.ntex; i++) {
        RTex t;
        fseek(r->rf, rh.off_textures + i * sizeof(RTex), SEEK_SET); fread(&t, sizeof(t), 1, r->rf);
        if (strncmp(t.name, texname, 24)) continue;
        if ((int)i == m->tex_idx) return true;
        int id = upload_tex(r->rf, i);
        if (m->tex && m->tex_idx != -2 && (m->tex_idx < 0 || m->tex_idx >= NTEXC || --tex_ref[m->tex_idx] == 0)) glDeleteTextures(1, &m->tex);
        m->tex = id; m->tex_idx = i;
        return true;
    }
    return false;
}

// port (board designer): a mesh drawn with a texture someone else owns (tex_idx -2: never deleted here)
void rider_use_tex(Mesh *m, int tex)
{
    if (m->tex && m->tex_idx != -2 && (m->tex_idx < 0 || m->tex_idx >= NTEXC || --tex_ref[m->tex_idx] == 0)) glDeleteTextures(1, &m->tex);
    m->tex = tex; m->tex_idx = -2;
}

void rider_free_mesh(Mesh *m)
{
    free(m->dl);
    if (m->tex && m->tex_idx != -2 && (m->tex_idx < 0 || m->tex_idx >= NTEXC || --tex_ref[m->tex_idx] == 0)) glDeleteTextures(1, &m->tex);
    memset(m, 0, sizeof(*m));
}

bool rider_read_clip(Rider *r, const char *name, Clip *out)
{
    for (u32 i = 0; i < r->nanims; i++) {
        AClip c;
        fseek(r->af, 48 + i * sizeof(AClip), SEEK_SET); fread(&c, sizeof(c), 1, r->af);
        if (strncmp(c.name, name, 24)) continue;
        u32 bytes = c.nframes * 32 * 7 * 2;
        out->frames = malloc(bytes); out->nframes = c.nframes;
        if (!out->frames) return false;
        fseek(r->af, c.off, SEEK_SET); fread(out->frames, 1, bytes, r->af);
        return true;
    }
    printf("clip %s not found\n", name);
    return false;
}

bool rider_load_clip(Rider *r, const char *name)
{
    for (u32 i = 0; i < r->nanims; i++) {
        AClip c;
        fseek(r->af, 48 + i * sizeof(AClip), SEEK_SET); fread(&c, sizeof(c), 1, r->af);
        if (strncmp(c.name, name, 24)) continue;
        free(r->clip.frames);
        u32 bytes = c.nframes * 32 * 7 * 2;
        r->clip.frames = malloc(bytes); r->clip.nframes = c.nframes;
        fseek(r->af, c.off, SEEK_SET); fread(r->clip.frames, 1, bytes, r->af);
        return true;
    }
    printf("clip %s not found\n", name);
    return false;
}

// Fixed-point bone math (the DS has no FPU: the float version cost ~3 ms per rider).
ITCM_CODE static void mat_mul(MatX *o, const MatX *a, const MatX *b)   // o = a * b
{
    MatX t;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++)
            t.m[i][j] = (a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j]) >> 12;
        t.m[i][3] = (s32)(((s64)a->m[i][0] * b->m[0][3] + (s64)a->m[i][1] * b->m[1][3] + (s64)a->m[i][2] * b->m[2][3]) >> 12) + a->m[i][3];
    }
    *o = t;
}

// q (2.14-ish, |q|^2 = n2) to unit length 4096: one hardware divide for the reciprocal (was four software divides)
static inline void norm_q(s32 q[4], u32 n2)
{
    u32 n = sqrt32(n2); if (!n) n = 1;
#ifdef ARM9
    s32 inv = div32(1 << 28, (s32)n);                     // 2^28 / |q|
    for (int i = 0; i < 4; i++) q[i] = (s32)(((s64)q[i] * inv) >> 16);
#else
    for (int i = 0; i < 4; i++) q[i] = (q[i] << 12) / (s32)n;
#endif
}
// a key pair lerped (f 0..256): rotation quaternion nlerped to 4096, translation x16.
// port: the short way round, with nlerp's speed evened out. The clips store some neighbouring keys in opposite
// hemispheres (q and -q are the same turn): lerped as stored, a bone turned 271 degrees backwards inside one key
// instead of 89 forwards (the uber tricks' boards, ~1 in 4 of their keys - the board seemed to jump between
// poses). Nlerp across the ubers' 100-170 degree keys also bunched the turn into the middle of each key; the time
// is reshaped as zeux's slerp approximation (A, B fitted to the keys' dot product) so the spin runs evenly.
ITCM_CODE static void key_qt(const s16 *k, const s16 *k2, int f, s32 q[4], s32 t[3])
{
    s32 d = k[0] * k2[0] + k[1] * k2[1] + k[2] * k2[2] + k[3] * k2[3];                  // 4.28
    s32 b[4]; for (int i = 0; i < 4; i++) b[i] = d < 0 ? -k2[i] : k2[i];
    int fq = f;
    s32 dn = (d < 0 ? -d : d) >> 16;                                                     // |dot|, 4096 = 1
    if (dn > 4096) dn = 4096;
    if (dn < 4000 && f > 0 && f < 256) {
        s32 A = 4466 + ((dn * (-13292 + ((dn * (14567 - ((dn * 5879) >> 12))) >> 12))) >> 12);   // Q12
        s32 B = 3473 + ((dn * (-4343 + ((dn * 883) >> 12))) >> 12);
        s32 h = f - 128;                                                                 // Q8
        s32 kk = ((A * h * h) >> 16) + B;                                                // Q12
        s64 c = (s64)f * (f - 128) * (f - 256) * kk;                                     // Q36
        fq = f + (int)(c >> 28);
        if (fq < 0) fq = 0; if (fq > 256) fq = 256;
    }
    u32 n2 = 0;
    for (int i = 0; i < 4; i++) { q[i] = k[i] + (((b[i] - k[i]) * fq) >> 8); n2 += q[i] * q[i]; }   // 2.14
    norm_q(q, n2);                                                                                   // -> 4096
    for (int i = 0; i < 3; i++) t[i] = (k[4 + i] << 4) + ((((k2[4 + i] - k[4 + i]) << 4) * f) >> 8);
}
ITCM_CODE static void qt_mat(MatX *o, const s32 q[4], const s32 t[3])
{
    s32 w = q[0], x = q[1], y = q[2], z = q[3];
    o->m[0][0] = 4096 - ((y * y + z * z) >> 11); o->m[0][1] = (x * y - z * w) >> 11; o->m[0][2] = (x * z + y * w) >> 11;
    o->m[1][0] = (x * y + z * w) >> 11; o->m[1][1] = 4096 - ((x * x + z * z) >> 11); o->m[1][2] = (y * z - x * w) >> 11;
    o->m[2][0] = (x * z - y * w) >> 11; o->m[2][1] = (y * z + x * w) >> 11; o->m[2][2] = 4096 - ((x * x + y * y) >> 11);
    for (int i = 0; i < 3; i++) o->m[i][3] = t[i];
}
ITCM_CODE static void local_from(MatX *o, const s16 *k, const s16 *k2, int f)  // lerp two keys (f 0..256), quat nlerp
{
    s32 q[4], t[3]; key_qt(k, k2, f, q, t); qt_mat(o, q, t);
}

void rider_bone_pos(const Rider *r, int bone, float out[3])
{
    for (int i = 0; i < 3; i++) out[i] = r->bone[bone].m[i][3] / 16.0f;
}

int rider_clip_index(Rider *r, const char *name)
{
    for (u32 i = 0; i < r->nanims; i++) if (!strncmp(r->recs[i].name, name, 24)) return i;
    return -1;
}

const Clip *rider_clip(Rider *r, int i)
{
    if (i < 0 || (u32)i >= r->nanims) return NULL;
    Clip *c = &r->cache[i];
    if (!c->frames) {
        rider_gx_wait();                                  // (a file read: no DMA to the geometry engine meanwhile)
        const AClip *a = &r->recs[i];
        u32 bytes = a->nframes * 32 * 7 * 2;
        c->frames = malloc(bytes);
        if (!c->frames) return NULL;
        fseek(r->af, a->off, SEEK_SET); fread(c->frames, 1, bytes, r->af);
        c->nframes = a->nframes; c->interval = a->interval ? a->interval : 4; c->length = a->length;
    }
    return c;
}

ITCM_CODE void rider_pose_clip(Rider *r, const Clip *c, float key, bool loop)
{
    if (!c || !c->nframes) return;
    int n = c->nframes;
    if (key < 0) key = 0;
    if (!loop && key > n - 1) key = n - 1;
    int f0 = (int)key, f1;
    float fr = key - f0;
    if (loop) { f0 %= n; f1 = (f0 + 1) % n; } else { if (f0 > n - 1) f0 = n - 1; f1 = f0 + 1 < n ? f0 + 1 : f0; }
    Clip save = r->clip; r->clip = *c; r->clip.nframes = n;
    // rider_pose wraps modulo n; pass an index that lands on f0 with the fraction, and clamp f1 for one-shots
    if (!loop && f1 == f0) fr = 0;
    rider_pose(r, f0 + fr);
    r->clip = save;
}

// port: the bones to pose, parents first - only those the meshes are skinned to (and their parents) and the
// board's bone 4: ~20 of the 32 (the rest moved nothing drawn). Was every bone, sorted out by repeated passes.
static int pose_list_build(const Rider *r, u8 out[32]);
ITCM_CODE static int pose_list(const Rider *r, u8 out[32])           // (cached per rider: it was rebuilt every pose)
{
    Rider *w = (Rider *)r;
    if (w->plist_n <= 0 || w->plist_key[0] != r->body.dl || w->plist_key[1] != r->body_hi.dl) {
        w->plist_n = pose_list_build(r, w->plist); w->plist_key[0] = r->body.dl; w->plist_key[1] = r->body_hi.dl;
    }
    memcpy(out, w->plist, w->plist_n);
    return w->plist_n;
}
static int pose_list_build(const Rider *r, u8 out[32])
{
    u32 need = 1u << 4;
    const Mesh *ms[2] = { &r->body, &r->body_hi };
    for (int k = 0; k < 2; k++) if (ms[k]->dl) for (int s = 0; s < ms[k]->nslots; s++) need |= 1u << (ms[k]->slot_bone[s] & 31);
    if (!r->body.dl && !r->body_hi.dl) need = 0xffffffffu;
    for (int b = 0; b < 32; b++) if (need & (1u << b)) for (int p = r->parent[b]; p >= 0 && !(need & (1u << p)); p = r->parent[p]) need |= 1u << p;
    for (int pass = 0; pass < 32; pass++) {                  // ancestors added above may themselves need ancestors
        u32 add = 0;
        for (int b = 0; b < 32; b++) if ((need & (1u << b)) && r->parent[b] >= 0 && !(need & (1u << r->parent[b]))) add |= 1u << r->parent[b];
        if (!add) break; need |= add;
    }
    int n = 0; u32 done = 0;
    for (int pass = 0; pass < 32 && done != need; pass++)
        for (int b = 0; b < 32; b++) {
            if (!(need & (1u << b)) || (done & (1u << b))) continue;
            int p = r->parent[b];
            if (p >= 0 && !(done & (1u << p))) continue;
            out[n++] = b; done |= 1u << b;
        }
    return n;
}

ITCM_CODE void rider_pose(Rider *r, float frame)
{
    int n = r->clip.nframes; if (!n) return;
    int f0 = (int)frame % n, f1 = (f0 + 1) % n; int fr = (int)((frame - floorf(frame)) * 256);
    u8 list[32]; int nl = pose_list(r, list);
    for (int i = 0; i < nl; i++) {
        int b = list[i], p = r->parent[b];
        MatX L; local_from(&L, &r->clip.frames[(f0 * 32 + b) * 7], &r->clip.frames[(f1 * 32 + b) * 7], fr);
        if (p >= 0) mat_mul(&r->bone[b], &r->bone[p], &L); else r->bone[b] = L;
    }
}

// the frames either side of a clip time, and the fraction (0..256)
ITCM_CODE static void clip_at(const Clip *c, float key, bool loop, int *f0, int *f1, int *fr)
{
    int n = c->nframes;
    if (key < 0) key = 0;
    if (!loop && key > n - 1) key = n - 1;
    int a = (int)key; int f = (int)((key - a) * 256);
    if (loop) { a %= n; *f1 = (a + 1) % n; } else { if (a > n - 1) a = n - 1; *f1 = a + 1 < n ? a + 1 : a; if (*f1 == a) f = 0; }
    *f0 = a; *fr = f;
}
// port: a cross-fade from clip a (as it stood) into clip b, w 0..256 of b - clips used to switch in one frame
ITCM_CODE void rider_pose_blend(Rider *r, const Clip *a, float ka, bool la, const Clip *b, float kb, bool lb, int w)
{
    if (!b || !b->nframes) return;
    if (!a || !a->nframes || w >= 256) { rider_pose_clip(r, b, kb, lb); return; }
    int a0, a1, af, b0, b1, bf;
    clip_at(a, ka, la, &a0, &a1, &af); clip_at(b, kb, lb, &b0, &b1, &bf);
    u8 list[32]; int nl = pose_list(r, list);
    {
        for (int li = 0; li < nl; li++) {
            int bn = list[li], p = r->parent[bn];
            s32 qa[4], ta[3], qb[4], tb[3], q[4], t[3];
            key_qt(&a->frames[(a0 * 32 + bn) * 7], &a->frames[(a1 * 32 + bn) * 7], af, qa, ta);
            key_qt(&b->frames[(b0 * 32 + bn) * 7], &b->frames[(b1 * 32 + bn) * 7], bf, qb, tb);
            s32 d = (qa[0] * qb[0] + qa[1] * qb[1] + qa[2] * qb[2] + qa[3] * qb[3]) >> 12;
            if (d < 0) for (int i = 0; i < 4; i++) qa[i] = -qa[i];          // the short way round
            u32 n2 = 0;
            for (int i = 0; i < 4; i++) { q[i] = qa[i] + (((qb[i] - qa[i]) * w) >> 8); n2 += q[i] * q[i]; }
            norm_q(q, n2);
            for (int i = 0; i < 3; i++) t[i] = ta[i] + (((tb[i] - ta[i]) * w) >> 8);
            MatX L; qt_mat(&L, q, t);
            if (p >= 0) mat_mul(&r->bone[bn], &r->bone[p], &L); else r->bone[bn] = L;
        }
    }
}

static void to_fixed(MatX *o, const Mat34 *M)
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) o->m[i][j] = (s32)(M->m[i][j] * 4096.0f);
        o->m[i][3] = (s32)(M->m[i][3] * 16.0f);
    }
}

// multiply current GL matrix by M (rotation 4096 = f32 1.0; translation game*16 = f32 DS units, 1 DS unit = 256 game units)
ITCM_CODE static void gl_mult(const MatX *M)
{
    m4x3 g;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) g.m[i * 3 + j] = M->m[j][i];   // DS row-vector layout
    for (int j = 0; j < 3; j++) g.m[9 + j] = M->m[j][3];
    glMultMatrix4x3(&g);
}

// glCallList without its per-call cache flush (the lists are flushed once when loaded, like the course's)
static inline void gx_call(const u32 *dl)
{
#ifdef TEST_NODL
    return;
#endif
#ifdef ARM9
    while (dmaBusy(0) || dmaBusy(1) || dmaBusy(2) || dmaBusy(3));
    dmaSetParams(0, dl + 1, (void *)&GFX_FIFO, DMA_FIFO | dl[0]);
    while (dmaBusy(0));
#else
    glCallList(dl);
#endif
}
// port: with rider_async set (the race's riders), the body's list goes to the geometry engine by DMA while the
// CPU goes on (posing the next rider); rider_gx_wait() finishes it (and its matrix pop) before any other GL command.
bool rider_async; static bool gx_pending;
ITCM_CODE void rider_gx_wait(void)
{
    if (!gx_pending) return;
#ifdef ARM9
    while (dmaBusy(0));
#endif
    glPopMatrix(1);
    gx_pending = false;
}
// port: the riders' polygon format (set with it by the callers) and the culling the board alone is drawn with -
// with none, the deck and the base a few units under it fought in the depth buffer at race distances and the base
// showed through on top (the board designer's design came out its underside)
u32 rider_fmt, rider_board_cull = POLY_CULL_NONE;
ITCM_CODE void rider_draw(Rider *r, const Mat34 *world, int board_bone)
{
    rider_gx_wait();
    bool hi = r->use_hi && r->body_hi.dl;
    Mesh *m = hi ? &r->body_hi : &r->body, *bm = hi && r->board_hi.dl ? &r->board_hi : &r->board;
    float bscale = bm == &r->board_hi ? r->board_scale_hi : r->board_scale;
    MatX Wx; to_fixed(&Wx, world);
    if (bm->dl) {
        glPushMatrix();
        // 0x4cfe0: the board is its own object whose matrix is the rider's times the board bone's (bone 4, the
        // anim buffer's 0x60 entry via 0x5c3a0), so it follows the bone through grabs, tweaks and spins; mesh X =
        // length, Y = up, Z = width line up with the bone's axes. Its scale is its own (0x7080 against the rider's
        // 0x550): board_scale is that ratio for def_board.
        s32 k = (s32)(bscale * 4096), lift = (s32)((bm == &r->board_hi ? r->board_lift_hi : r->board_lift) * 16);
        const MatX *bb = &r->bone[board_bone];
        MatX B;
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) B.m[i][j] = (s32)(((s64)bb->m[i][j] * k) >> 12);
            B.m[i][3] = bb->m[i][3] + (i == 2 ? lift : 0);
        }
        MatX W; mat_mul(&W, &Wx, &B);
        gl_mult(&W);
        glBindTexture(0, bm->tex);
        if (rider_fmt) {
            // a mirrored matrix (the goofy stance's negated X scale) turns the winding round: the other face culled
            s64 d = ((s64)W.m[0][0] * (((s64)W.m[1][1] * W.m[2][2] - (s64)W.m[1][2] * W.m[2][1]) >> 12)
                   - (s64)W.m[0][1] * (((s64)W.m[1][0] * W.m[2][2] - (s64)W.m[1][2] * W.m[2][0]) >> 12)
                   + (s64)W.m[0][2] * (((s64)W.m[1][0] * W.m[2][1] - (s64)W.m[1][1] * W.m[2][0]) >> 12));
            u32 c = rider_board_cull;
            if (d < 0 && c != POLY_CULL_NONE) c ^= 3u << 6;
            glPolyFmt((rider_fmt & ~(3u << 6)) | c);
        }
        gx_call(bm->dl);
        if (rider_fmt) glPolyFmt(rider_fmt);
        glPopMatrix(1);
    }
    // the body last (the board first: the body's list is the long one to leave running)
    glPushMatrix();                                       // stack[0] = camera, SP -> 1
    for (int s = 0; s < m->nslots; s++) {
        MatX W; mat_mul(&W, &Wx, &r->bone[m->slot_bone[s]]);
        glRestoreMatrix(0); gl_mult(&W); glStoreMatrix(s + 1);   // slots 1..n = camera * world * bone
    }
    glBindTexture(0, m->tex);
#if defined(ARM9) && !defined(TEST_NODL)
    if (rider_async) {
        while (dmaBusy(0) || dmaBusy(1) || dmaBusy(2) || dmaBusy(3));
        dmaSetParams(0, m->dl + 1, (void *)&GFX_FIFO, DMA_FIFO | m->dl[0]);
        gx_pending = true;
        return;
    }
#endif
    gx_call(m->dl);
    glPopMatrix(1);
}

void rider_draw_static_x(const Mesh *m, const MatX *W)    // the same with a fixed-point matrix (no float on the way)
{
    rider_gx_wait();
    if (!m->dl) return;
    glPushMatrix();
    gl_mult(W);
    glBindTexture(0, m->tex);
    gx_call(m->dl);
    glPopMatrix(1);
}
void rider_draw_static(const Mesh *m, const Mat34 *world)
{
    rider_gx_wait();
    if (!m->dl) return;
    MatX W; to_fixed(&W, world);
    glPushMatrix();
    gl_mult(&W);
    glBindTexture(0, m->tex);
    gx_call(m->dl);
    glPopMatrix(1);
}

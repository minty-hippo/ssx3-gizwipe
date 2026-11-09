// SSX 3 GizWipe - DS track runtime
#include <stdlib.h>
#include "xtext.h"
#include <string.h>
#include <math.h>
#include "track.h"

static void *read_at(FILE *f, u32 off, u32 size)
{
    void *p = malloc(size);
    if (!p) return NULL;
    fseek(f, off, SEEK_SET);
    if (fread(p, 1, size, f) != size) { free(p); return NULL; }
    return p;
}


bool track_load(Track *t, const char *path)
{
    memset(t, 0, sizeof(*t));
    t->f = fopen(path, "rb");
    if (!t->f) { printf("open %s failed\n", path); return false; }
    if (fread(&t->h, sizeof(SsxtHeader), 1, t->f) != 1 || memcmp(t->h.magic, "SSXT", 4) || t->h.ver != 6) {
        printf("bad ssxt header\n"); return false;
    }
    if (t->h.nseg > MAX_SEGS) { printf("too many segs %lu\n", t->h.nseg); return false; }
    t->race = read_at(t->f, t->h.off_raceline, t->h.nraceline * sizeof(RacePoint));
    t->markers = read_at(t->f, t->h.off_markers, t->h.nmarkers * sizeof(Marker));
    t->mats = read_at(t->f, t->h.off_materials, t->h.nmat * sizeof(MatRec));
    t->segs = read_at(t->f, t->h.off_segments, t->h.nseg * sizeof(SegRec));
    t->length = (float)t->race[t->h.nraceline - 1].arclen;
    // grind rails
    t->nrails = t->h.nrails;
    t->rails = calloc(t->nrails + 1, sizeof(TrackRail));
    if (t->nrails) {
        u32 *tab = read_at(t->f, t->h.off_rails, t->nrails * 8);
        u32 npts = 0;
        for (int i = 0; i < t->nrails; i++) npts += tab[i * 2];
        s32 (*P)[3] = read_at(t->f, t->h.off_rails + t->nrails * 8, npts * 12);
        s32 (*D)[3] = calloc(npts, 12);
        for (int i = 0; i < t->nrails; i++) {
            TrackRail *r = &t->rails[i];
            r->n = tab[i * 2]; r->pts = P + tab[i * 2 + 1]; r->dir = D + tab[i * 2 + 1];
            for (int k = 0; k < 3; k++) r->bbmin[k] = r->bbmax[k] = r->pts[0][k];
            for (int j = 1; j < r->n; j++) {
                float d[3], l = 0;
                for (int k = 0; k < 3; k++) {
                    if (r->pts[j][k] < r->bbmin[k]) r->bbmin[k] = r->pts[j][k];
                    if (r->pts[j][k] > r->bbmax[k]) r->bbmax[k] = r->pts[j][k];
                    d[k] = r->pts[j][k] - r->pts[j - 1][k]; l += d[k] * d[k];
                }
                l = sqrtf(l); if (l < 1e-3f) l = 1;
                for (int k = 0; k < 3; k++) r->dir[j - 1][k] = (s32)(d[k] * 16384 / l);
            }
            s32 a = r->bbmax[0] - r->bbmin[0], b = r->bbmax[1] - r->bbmin[1], cc = r->bbmax[2] - r->bbmin[2];
            r->r2 = a * a + b * b + cc * cc;
        }
        free(tab);
    }
    t->ntrees = 0;
    t->trees = malloc(sizeof(TreePos) * (t->h.nmarkers + 1));
    for (u32 i = 0; i < t->h.nmarkers; i++)
        if (!strncmp(t->markers[i].name, "tree", 4)) {
            TreePos *q = &t->trees[t->ntrees++];
            q->x = t->markers[i].pos[0]; q->y = t->markers[i].pos[1]; q->z = t->markers[i].pos[2];
        }
    // race markers: pickups (0x6268c), spawns (0x66268), finish line (0x4bc4c)
    static const char *PICK[6] = { "c_speed", "c_trick", "c_x2", "c_x3", "c_x5", "c_x10" };
    t->npicks = 0; t->nspawn = 0; t->has_finish = false;
    t->picks = malloc(sizeof(Pickup) * (t->h.nmarkers + 1));
    bool fl = false, fr = false;
    for (u32 i = 0; i < t->h.nmarkers; i++) {
        const Marker *m = &t->markers[i];
        for (int k = 0; k < 6; k++)
            if (!strncmp(m->name, PICK[k], 24)) {
                Pickup *q = &t->picks[t->npicks++];
                memcpy(q->pos, m->pos, 12); q->type = k; q->taken = 0;
            }
        if (!strncmp(m->name, "spawn", 5) && t->nspawn < 4) memcpy(t->spawn[t->nspawn++], m->pos, 12);
        if (!strncmp(m->name, "finish_left", 24)) { memcpy(t->fin_l, m->pos, 12); fl = true; }
        if (!strncmp(m->name, XT_294, 24)) { memcpy(t->fin_r, m->pos, 12); fr = true; }
    }
    t->has_finish = fl && fr;
    // light flares: glare_blue / orange / yellow / purple / green (+ numbers)
    t->nglares = 0;
    t->glares = malloc(sizeof(*t->glares) * (t->h.nmarkers + 1));
    for (u32 i = 0; i < t->h.nmarkers; i++) {
        const char *n = t->markers[i].name;
        if (strncmp(n, "glare_", 6)) continue;
        u16 col = RGB15(31, 31, 31);
        // the exe's colours (0x61b54, ABGR): blue 0xffff8c00, orange 0xff0087ff, purple 0xffff00d8, yellow 0xffa4f1ff
        if (!strncmp(n + 6, "blue", 4)) col = RGB15(0, 17, 31);
        else if (!strncmp(n + 6, "orange", 6)) col = RGB15(31, 16, 0);
        else if (!strncmp(n + 6, "yellow", 6)) col = RGB15(31, 30, 20);
        else if (!strncmp(n + 6, "purple", 6)) col = RGB15(27, 0, 31);
        else if (!strncmp(n + 6, "green", 5)) col = RGB15(10, 31, 12);
        memcpy(t->glares[t->nglares].pos, t->markers[i].pos, 12); t->glares[t->nglares].color = col; t->nglares++;
    }
    t->nfogs = 0;
    for (u32 i = 0; i < t->h.nmarkers && t->nfogs < 16; i++)
        if (!strcmp(t->markers[i].name, "fog")) memcpy(t->fogs[t->nfogs++], t->markers[i].pos, 12);
    t->npipe = 0;
    for (int k = 0; k < 4; k++) {                           // lip lines of the superpipe (BHP1)
        char nl[16], nr[16]; int got = 0;
        snprintf(nl, sizeof(nl), "pipe%d_l", k); snprintf(nr, sizeof(nr), "pipe%d_r", k);
        for (u32 i = 0; i < t->h.nmarkers; i++) {
            if (!strncmp(t->markers[i].name, nl, 24)) { memcpy(t->pipe[k][0], t->markers[i].pos, 12); got |= 1; }
            if (!strncmp(t->markers[i].name, nr, 24)) { memcpy(t->pipe[k][1], t->markers[i].pos, 12); got |= 2; }
        }
        if (got != 3) break;
        t->npipe = k + 1;
    }

    // textures -> VRAM (one at a time through a scratch buffer)
    TexRec *tr = read_at(t->f, t->h.off_textures, t->h.ntex * sizeof(TexRec));
    t->texnames = calloc(t->h.ntex, sizeof(int));
    glGenTextures(t->h.ntex, t->texnames);
    for (u32 i = 0; i < t->h.ntex; i++) {
        u32 bytes = tr[i].w * tr[i].h;
        void *tex = read_at(t->f, tr[i].off_texels, bytes);
        void *pal = read_at(t->f, tr[i].off_pal, 512);
        glBindTexture(0, t->texnames[i]);
        int param = TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T;
        if (tr[i].flags & 1) param |= GL_TEXTURE_COLOR0_TRANSPARENT;
        if (!glTexImage2D(0, 0, GL_RGB256, tr[i].w, tr[i].h, 0, param, tex)) { t->tex_fail++; printf("tex %lu upload failed\n", i); }
        if (i < 32) { u32 sm = 0; const u8 *q = tex; for (u32 k = 0; k < bytes; k++) sm = sm * 31 + q[k]; t->tex_sum[i] = sm; }
        if (!glColorTableEXT(0, 0, 256, 0, 0, pal)) { t->pal_fail++; fprintf(stderr, "track pal %lu failed\n", i); }
        if (i < 32) { u32 sm = 0; const u16 *q = pal; for (int k = 0; k < 256; k++) sm = sm * 31 + q[k]; t->pal_sum[i] = sm; }
        free(tex); free(pal);
    }
    free(tr);
    return true;
}

static void col_free(ColGrid *g)
{
    if (!g) return;
    free(g->start); free(g->items); free(g);
}

// The collision grid of a segment (x/z cells of 1 << sh units, each listing its triangles), built in two passes:
// count, then fill. Built a slice of triangles at a time (col_work), so the frame a background read finishes doesn't
// carry the whole build (~60 scanlines); col_build does it in one go for a segment needed now.
typedef struct { ColGrid *g; int seg, pass, k; u32 *fill; } ColJob;
static bool col_begin(const Track *t, int i, ColJob *j)
{
    const SegRec *s = &t->segs[i];
    const u8 *base = t->chunk[i] - s->chunk_off + s->off_collision;
    ColGrid *g = calloc(1, sizeof(ColGrid));
    if (!g) return false;
    g->ntri = ((const u16 *)base)[0];
    u16 nvert = ((const u16 *)base)[1];
    g->V = (const s16 *)(base + 4);
    g->T = (const u16 *)(base + 4 + nvert * 6);
    g->x0 = s->bbmin[0]; g->z0 = s->bbmin[2];
    int w = s->bbmax[0] - s->bbmin[0] + 1, d = s->bbmax[2] - s->bbmin[2] + 1;
    int span = w > d ? w : d;
    // a power of two (shifts, not divides: the ARM9 has no divide instruction)
    g->sh = 8; while ((1 << g->sh) < span / 16) g->sh++;
    g->cell = 1 << g->sh;
    g->gx = (w + g->cell - 1) >> g->sh; g->gz = (d + g->cell - 1) >> g->sh;
    g->start = calloc(g->gx * g->gz + 1, 4);
    if (!g->start) { col_free(g); return false; }
    j->g = g; j->seg = i; j->pass = 0; j->k = 0; j->fill = NULL;
    return true;
}
static void col_abort(ColJob *j) { if (j->g) { free(j->fill); col_free(j->g); } j->g = NULL; j->fill = NULL; j->seg = -1; }
// up to `budget` triangles of work; true when the grid is complete (or failed: j->g NULL)
static bool col_work(ColJob *j, int budget)
{
    ColGrid *g = j->g;
    int nc = g->gx * g->gz;
    while (budget > 0) {
        if (j->k >= g->ntri) {
            if (j->pass == 1) { free(j->fill); j->fill = NULL; return true; }
            u32 tot = 0;                                   // counts -> starts
            for (int c = 0; c < nc; c++) { u32 n = g->start[c]; g->start[c] = tot; tot += n; }
            g->start[nc] = tot;
            g->items = malloc(tot * 2 + 2);
            j->fill = malloc(nc * 4);
            if (!g->items || !j->fill) { col_abort(j); return true; }
            memcpy(j->fill, g->start, nc * 4);
            j->pass = 1; j->k = 0;
            continue;
        }
        int k = j->k++; budget--;
        const u16 *T = g->T + k * TRI_U16;
        if ((T[3] >> 8) == 0) continue;                       // surface 0 = not collidable (0x67a38)
        const s16 *a = &g->V[T[0] * 3], *b = &g->V[T[1] * 3], *cc = &g->V[T[2] * 3];
        int mnx = a[0], mxx = a[0], mnz = a[2], mxz = a[2];
        if (b[0] < mnx) mnx = b[0]; if (b[0] > mxx) mxx = b[0]; if (cc[0] < mnx) mnx = cc[0]; if (cc[0] > mxx) mxx = cc[0];
        if (b[2] < mnz) mnz = b[2]; if (b[2] > mxz) mxz = b[2]; if (cc[2] < mnz) mnz = cc[2]; if (cc[2] > mxz) mxz = cc[2];
        int cx0 = (mnx - g->x0) >> g->sh, cx1 = (mxx - g->x0) >> g->sh;
        int cz0 = (mnz - g->z0) >> g->sh, cz1 = (mxz - g->z0) >> g->sh;
        if (cx0 < 0) cx0 = 0; if (cz0 < 0) cz0 = 0; if (cx1 >= g->gx) cx1 = g->gx - 1; if (cz1 >= g->gz) cz1 = g->gz - 1;
        for (int z = cz0; z <= cz1; z++)
            for (int x = cx0; x <= cx1; x++) {
                int c = z * g->gx + x;
                if (j->pass == 0) g->start[c]++; else g->items[j->fill[c]++] = k;
            }
    }
    return false;
}
static ColGrid *col_build(const Track *t, int i)
{
    ColJob j;
    if (!col_begin(t, i, &j)) return NULL;
    col_work(&j, 1 << 30);
    return j.g;
}

// one-sided segment / triangle test (0x67164): ray must enter from the front (dir . n < 0)
static bool ray_tri(const s16 *v0, const s16 *v1, const s16 *v2, const s16 *n, const s32 a[3], const s32 d[3], s32 out[3])
{
    s32 dn = (n[0] * d[0] + n[1] * d[1] + n[2] * d[2]) >> 14;
    if (dn >= 0) return false;
    s32 pa = (s32)(((s64)n[0] * (v0[0] - a[0]) + (s64)n[1] * (v0[1] - a[1]) + (s64)n[2] * (v0[2] - a[2])) >> 14);
    if (pa > 0 || pa < dn) return false;                          // plane not crossed between a and b
    s32 tt = pa * 0x4000 / dn;
    s32 p[3] = { a[0] + ((d[0] * tt) >> 14), a[1] + ((d[1] * tt) >> 14), a[2] + ((d[2] * tt) >> 14) };
    // the original tests a point pulled 3/32 of the way to the centroid (lenient on edges)
    s32 q[3];
    for (int k = 0; k < 3; k++) q[k] = (p[k] * 29 + v0[k] + v1[k] + v2[k]) / 32;
    const s16 *V[3] = { v0, v1, v2 };
    for (int e = 0; e < 3; e++) {
        const s16 *s = V[e], *f = V[(e + 1) % 3];
        s32 ex = f[0] - s[0], ey = f[1] - s[1], ez = f[2] - s[2];
        s32 wx = q[0] - s[0], wy = q[1] - s[1], wz = q[2] - s[2];
        s64 cx = (s64)ey * wz - (s64)ez * wy, cy = (s64)ez * wx - (s64)ex * wz, cz = (s64)ex * wy - (s64)ey * wx;
        if (cx * n[0] + cy * n[1] + cz * n[2] < 0) return false;
    }
    out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    return true;
}

ITCM_CODE bool track_ray(Track *t, const s32 a[3], const s32 b[3], s32 hit[3], s32 n[3], int *surf)
{
    bool found = false; s64 best = 0x7fffffffffffLL;
    t->rays++;
    int bx0 = a[0] < b[0] ? a[0] : b[0], bx1 = a[0] < b[0] ? b[0] : a[0];
    int bz0 = a[2] < b[2] ? a[2] : b[2], bz1 = a[2] < b[2] ? b[2] : a[2];
    s32 d[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
    for (u32 i = 0; i < t->h.nseg; i++) {
        ColGrid *g = t->col[i];
        if (!g) continue;
        const SegRec *s = &t->segs[i];
        int lx0 = bx0 - s->origin[0], lx1 = bx1 - s->origin[0], lz0 = bz0 - s->origin[2], lz1 = bz1 - s->origin[2];
        if (lx1 < s->bbmin[0] || lx0 > s->bbmax[0] || lz1 < s->bbmin[2] || lz0 > s->bbmax[2]) continue;
        s32 la[3] = { a[0] - s->origin[0], a[1] - s->origin[1], a[2] - s->origin[2] };
        int cx0 = (lx0 - g->x0) >> g->sh, cx1 = (lx1 - g->x0) >> g->sh;
        int cz0 = (lz0 - g->z0) >> g->sh, cz1 = (lz1 - g->z0) >> g->sh;
        if (lx0 < g->x0) cx0 = 0; if (lz0 < g->z0) cz0 = 0;
        if (cx1 >= g->gx) cx1 = g->gx - 1; if (cz1 >= g->gz) cz1 = g->gz - 1;
        for (int z = cz0; z <= cz1; z++)
            for (int x = cx0; x <= cx1; x++) {
                int c = z * g->gx + x;
                for (int j = g->start[c]; j < g->start[c + 1]; j++) {
                    int k = g->items[j];
                    const u16 *T = g->T + k * TRI_U16;
                    s32 p[3];
                    t->tri_tests++;
                    if (!ray_tri(&g->V[T[0] * 3], &g->V[T[1] * 3], &g->V[T[2] * 3], (const s16 *)(T + 4), la, d, p)) continue;
                    s32 dy = p[1] - la[1]; if (dy < 0) dy = -dy;
                    if (dy > 40000) continue;
                    s64 dist = (s64)(p[0] - la[0]) * (p[0] - la[0]) + (s64)(p[1] - la[1]) * (p[1] - la[1]) + (s64)(p[2] - la[2]) * (p[2] - la[2]);
                    if (dist < best) {
                        best = dist; found = true;
                        hit[0] = p[0] + s->origin[0]; hit[1] = p[1] + s->origin[1]; hit[2] = p[2] + s->origin[2];
                        n[0] = (s16)T[4]; n[1] = (s16)T[5]; n[2] = (s16)T[6];
                        *surf = T[3] >> 8;
                    }
                }
            }
    }
    return found;
}

static void col_abort_all(void);
void track_free(Track *t)
{
    col_abort_all();
    for (int i = 0; i < MAX_SEGS; i++) { free(t->chunk[i]); col_free(t->col[i]); }
    free(t->pend_buf); t->pend_buf = NULL; t->pend_seg = -1;
    free(t->trees); free(t->picks); free(t->glares);
    if (t->rails && t->nrails) { free(t->rails[0].pts); free(t->rails[0].dir); }
    free(t->rails);
    if (t->texnames) { glDeleteTextures(t->h.ntex, t->texnames); free(t->texnames); }
    free(t->race); free(t->markers); free(t->mats); free(t->segs);
    if (t->f) fclose(t->f);
    memset(t, 0, sizeof(*t));
}

void track_sample(const Track *t, float s, float out[3])
{
    const RacePoint *r = t->race; int n = t->h.nraceline;
    if (s <= 0) { out[0] = r[0].x; out[1] = r[0].y; out[2] = r[0].z; return; }
    if (s >= r[n - 1].arclen) { out[0] = r[n-1].x; out[1] = r[n-1].y; out[2] = r[n-1].z; return; }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) { int m = (lo + hi) / 2; if (r[m].arclen <= s) lo = m; else hi = m; }
    float span = (float)(r[hi].arclen - r[lo].arclen);
    float k = span > 0 ? (s - r[lo].arclen) / span : 0;
    out[0] = r[lo].x + (r[hi].x - r[lo].x) * k;
    out[1] = r[lo].y + (r[hi].y - r[lo].y) * k;
    out[2] = r[lo].z + (r[hi].z - r[lo].z) * k;
}

void track_set_view(Track *t, const s32 pos[3], int heading)
{
    memcpy(t->view, pos, 12); t->has_view = true;
    float a = heading * (6.2831853f / 2048.0f);
    t->view_dir[0] = (s32)(sinf(a) * 1024); t->view_dir[1] = (s32)(cosf(a) * 1024);
}

// LOD choice by race-line distance (mirrors tools/budget.py). bias raises the LOD of non-current segments.
// The race line alone misses geometry that is close in space but far along the line (wide areas, courses that
// double back): that ground stayed resident (collision) but was not drawn - holes you could ride over. So a
// segment is also drawn by its horizontal distance from the rider.
ITCM_CODE static int box_dist(const Track *t, int seg)              // the rider's distance from a segment's box (x/z, y halved)
{
    if (!t->has_view) return 1 << 30;
    const SegRec *g = &t->segs[seg];
    int x = t->view[0] - g->origin[0], z = t->view[2] - g->origin[2], y = t->view[1] - g->origin[1];
    int dx = x < g->bbmin[0] ? g->bbmin[0] - x : x > g->bbmax[0] ? x - g->bbmax[0] : 0;
    int dz = z < g->bbmin[2] ? g->bbmin[2] - z : z > g->bbmax[2] ? z - g->bbmax[2] : 0;
    int dy = y < g->bbmin[1] ? g->bbmin[1] - y : y > g->bbmax[1] ? y - g->bbmax[1] : 0;
    int d = dx > dz ? dx : dz; if (dy / 2 > d) d = dy / 2;
    return d;
}
ITCM_CODE static int lod_space(const Track *t, int seg)
{
#ifdef NOSPACE
    return -1;
#endif
    if (!t->has_view) return -1;
    const SegRec *g = &t->segs[seg];
    int x = t->view[0] - g->origin[0], z = t->view[2] - g->origin[2], y = t->view[1] - g->origin[1];
    int dx = x < g->bbmin[0] ? g->bbmin[0] - x : x > g->bbmax[0] ? x - g->bbmax[0] : 0;
    int dz = z < g->bbmin[2] ? g->bbmin[2] - z : z > g->bbmax[2] ? z - g->bbmax[2] : 0;
    int dy = y < g->bbmin[1] ? g->bbmin[1] - y : y > g->bbmax[1] ? y - g->bbmax[1] : 0;
    int d = dx > dz ? dx : dz; if (dy / 2 > d) d = dy / 2;
    if (d >= 7000) return -1;
    if (d > 600) {                                   // beyond the rider's surroundings: only what lies ahead
        int fx = x < g->bbmin[0] ? g->bbmin[0] : x > g->bbmax[0] ? g->bbmax[0] : x;   // nearest box point
        int fz = z < g->bbmin[2] ? g->bbmin[2] : z > g->bbmax[2] ? g->bbmax[2] : z;
        int cx = (g->bbmin[0] + g->bbmax[0]) / 2 - x, cz = (g->bbmin[2] + g->bbmax[2]) / 2 - z;
        bool ahead = (fx - x) * t->view_dir[0] + (fz - z) * t->view_dir[1] > 0 ||
                     cx * t->view_dir[0] + cz * t->view_dir[1] > 0;
        // port: or ahead of where the camera looks. The rider's travel alone left out ground on screen whenever the
        // camera looked elsewhere: off R&B's billboard rail at ~36 % the rider ran down to the right while the
        // camera faced the left mountainside, and its upper slopes (segments 10000+ along the line, 4000 in
        // space) were missing - holes against the sky. (What is off screen is still culled by the view test.)
        if (!ahead && t->cam_ok)
            ahead = (fx - x) * t->cam_f[0] + (fz - z) * t->cam_f[1] > 0 || cx * t->cam_f[0] + cz * t->cam_f[1] > 0;
        if (!ahead) return -1;
    }
    // full detail for boxes the rider is in or right beside (the simplified ground stands proud of the collision
    // surface the rider runs on: the board looked to sink into the snow), LOD2 drops pieces under 250 units: not
    // beside the rider
    return d < 600 ? 0 : d < 1500 ? 1 : 2;
}

// full detail out to 2400 (was 1200: its switch to LOD1 popped in plain view), the middle LOD to 4600 (was 3500),
// so the coarser steps happen where the fog (2875..5750) already covers most of them. Cheap now: the course
// is drawn in strips and the per-frame float maths is gone (stage 52), the polygon count barely moves (+2%).
#ifndef LOD0_D
#define LOD0_D 2400
#endif
#ifndef LOD1_D
#define LOD1_D 4600
#endif
// LOD by race-line distance ahead (shift moves the thresholds, for the hysteresis); -1 = not drawn
ITCM_CODE static int lod_base(const Track *t, int seg, int s, int shift)
{
    const SegRec *g = &t->segs[seg];
    int last = t->h.nlod - 1, l = -1;
    if (g->chunk_size == 0) return -1;
    int d = g->s0 - s; if (d < 0) d = 0;
    if (g->s1 < s - 2500) l = -1;
    else if (g->s1 < s) {                                // behind the rider: only its back is ever seen - but the
        // coarsest LOD drops small pieces and is not sealed to its neighbours (holes), so it is kept for what is
        // well clear of the rider: within 1500 units of its box (the camera trails it, courses bend) the middle LOD
        int bd = box_dist(t, seg);
        l = bd < 600 ? 0 : g->s1 < s - 400 && bd >= 1500 ? last : 1;   // (under the rider: full detail, as above)
    }
    else if (d < LOD0_D + shift) l = 0;                  // full detail a little before it is reached (it popped in
                                                         // right in front of the rider when it waited for d = 0)
    else if (d < LOD1_D + shift) l = 1;
    else if (d < 9000) l = 2;                            // the fog is opaque from ~5750 units (0x7695c)
    int ls = lod_space(t, seg);
    if (ls >= 0 && (l < 0 || ls < l)) l = ls;
    return l;
}

int track_lod_for(const Track *t, int seg, float sf, int bias)
{
    int last = t->h.nlod - 1, l = lod_base(t, seg, (int)sf, 0);
    if (l < 0) return -1;
    if (l == 0) return 0;                                // the rider's surroundings keep their detail under load
    int cap = l == last ? last : last > 2 ? last - 1 : last;   // the coarsest LOD has holes: never ahead
    l += bias;
    return l > cap ? cap : l;
}

// for drawing: hysteresis (a segment changes LOD only 400 units past a threshold, so it doesn't flicker back and
// forth), and under load the far end is cut inside the fog rather than coarsened into the holed LOD
ITCM_CODE static int lod_draw(Track *t, int seg, float sf, int bias)
{
    int s = (int)sf, prev = (int)t->lod_cur[seg] - 1;
    int lf = lod_base(t, seg, s, 400), lc = lod_base(t, seg, s, -400), l = lod_base(t, seg, s, 0);
    if (l >= 0 && prev >= 0 && lf >= 0 && lc >= 0 && prev >= lf && prev <= lc) l = prev;
    if (l < 0) return -1;
#ifdef TEST_LOD0
    return 0;
#endif
    const SegRec *g = &t->segs[seg];
    int d = g->s0 - s;
    if (bias >= 2 && d > 7000 && lod_space(t, seg) < 0) return -1;   // heavy load: what is past the fog's end (5750)
    int last = t->h.nlod - 1;
    if (bias >= 3) {                                      // still over the hardware's 2048 polygons (wide slopestyle
        if (d > 5750 && lod_space(t, seg) < 0) return -1; // courses): cut at the fog's end, and full detail only for
        if (l == 0 && g->s0 > s) l = 1;                   // the segment the rider is in
    }
    if (bias >= 4) {                                      // Snow Jam's widest stretch still overflowed (holes in the
        int bd = box_dist(t, seg);                        // course for ~1 s): the far reach comes in to 4600, boxes
        if (d > 4600 && bd > 2500) return -1;             // off to the side beyond 4000 go, all but the rider's own
        if (d <= 4600 && lod_space(t, seg) >= 0 && bd > 4000 && g->s1 < s) return -1;   // segment one step coarser
        if (bd > 0 && g->s0 > s && l < last - 1) l = l < 1 ? 1 : l;
    }
    if (l == 0 || l == last) return l;
    int cap = last > 2 ? last - 1 : last;
    l += bias > 1 ? 1 : 0;                                // only one step coarser for what's ahead
    return l > cap ? cap : l;
}

// Segments stream in a slice at a time (STREAM_SLICE bytes a frame, ~0.7 MB/s at 60 fps: several times what a
// full-speed descent needs), so a card read never stalls a frame for long. A segment the rider is in or about to
// reach is read at once (respawns, start, falling behind); max_loads >= 99 loads everything needed now.
#define STREAM_SLICE 12288
// the chunk's display lists go to the GPU by DMA every frame, so it must be out of the data cache: a read in the
// background is flushed a piece at a time as it arrives (all of it at commit was a ~100 line stall); `flush` for
// the ones read at once
static void seg_commit(Track *t, int i, u8 *buf, bool flush)
{
#ifdef ARM9
    if (flush) DC_FlushRange(buf, t->segs[i].chunk_size);
#else
    (void)flush;
#endif
    t->chunk[i] = buf; t->resident++; t->resident_bytes += t->segs[i].chunk_size; t->loads_this_frame++;
    if (flush) t->col[i] = col_build(t, i);              // read at once: needed now
    else t->col_queued[i] = 1;                            // in the background: built a slice a frame (col_tick)
}
static ColJob cjob = { NULL, -1, 0, 0, NULL };
static void col_abort_all(void) { col_abort(&cjob); }
// a slice of the queued collision builds; one the rider is near is finished at once
static void col_tick(Track *t)
{
    for (u32 i = 0; i < t->h.nseg; i++)                   // queued but already under the rider: now
        if (t->col_queued[i] && t->chunk[i] && (int)i != cjob.seg && box_dist(t, i) < 1500) { t->col_queued[i] = 0; t->col[i] = col_build(t, i); }
    if (!cjob.g) {
        int best = -1, bd = 1 << 30;
        for (u32 i = 0; i < t->h.nseg; i++)
            if (t->col_queued[i] && t->chunk[i]) { int d = box_dist(t, i); if (d < bd) { bd = d; best = i; } }
        if (best < 0) return;
        t->col_queued[best] = 0;
        if (!col_begin(t, best, &cjob)) return;
    }
    bool near = box_dist(t, cjob.seg) < 1500;
    if (col_work(&cjob, near ? 1 << 30 : 160)) {
        if (cjob.g) t->col[cjob.seg] = cjob.g;
        cjob.g = NULL; cjob.seg = -1;
    }
}
static void pend_cancel(Track *t) { if (t->pend_buf) free(t->pend_buf); t->pend_buf = NULL; t->pend_seg = -1; t->pend_done = 0; }

void track_update_stream(Track *t, float s, int max_loads)
{
    t->loads_this_frame = 0; t->sync_loads = 0;
    if (!t->pend_buf) t->pend_seg = -1;
    int want = -1; float want_d = 1e30f;
    for (u32 i = 0; i < t->h.nseg; i++) {
        // resident a good way before it is drawn, with a little hysteresis behind
        const SegRec *g = &t->segs[i];
        // (and anything within 9000 units in space: lod_space draws boxes from 7000, which a background read
        // must have fetched by then - otherwise it was read at once, a ~12 ms stall, with a frame of hole)
        bool need = track_lod_for(t, i, s, 0) >= 0 || (g->s0 - (int)s < 14000 && g->s1 >= (int)s - 2500) ||
                    box_dist(t, i) < 9000 ||
                    (i + 1 < t->h.nseg && track_lod_for(t, i + 1, s, 0) >= 0 && t->segs[i].s1 >= s - 4000);
        if (need && !t->chunk[i] && t->segs[i].chunk_size) {
            // urgent: where the rider is, or a segment due on screen inside the fog's reach (a missing one would
            // be a hole in the course until its background read finished)
            // ... or close to the rider in space whatever the race line says (courses that bend back on themselves:
            // ground beside the rider belonging to a segment far along the line waited at the back of the queue)
            int bd = box_dist(t, i);
            bool urgent = max_loads >= 99 || (g->s0 <= (int)s + 2500 && g->s1 >= (int)s - 500) ||
                          (lod_base(t, i, (int)s, 0) >= 0 && (g->s0 <= (int)s + 6000 || bd < 2500));
            if (urgent && t->loads_this_frame < (max_loads >= 99 ? 999 : 2)) {
                if ((int)i == t->pend_seg) pend_cancel(t);
                u8 *b = read_at(t->f, g->chunk_off, g->chunk_size);
                if (b) { seg_commit(t, i, b, true); t->sync_loads++; }
            } else {
                float d = g->s0 - s; if (d < 0) d = -d * 4;     // ahead first, nearest first (along the line or in space)
                if (bd < d) d = bd;
                if (d < want_d) { want_d = d; want = i; }
            }
        } else if (!need && t->chunk[i]) {
            col_free(t->col[i]); t->col[i] = NULL; t->col_queued[i] = 0;
            if (cjob.seg == (int)i) col_abort(&cjob);
            free(t->chunk[i]); t->chunk[i] = NULL; t->resident--; t->resident_bytes -= t->segs[i].chunk_size;
        } else if (!need && (int)i == t->pend_seg) pend_cancel(t);
    }
    if (t->pend_seg < 0 && want >= 0) {                     // start the next background read
        t->pend_buf = malloc(t->segs[want].chunk_size);
        if (t->pend_buf) { t->pend_seg = want; t->pend_done = 0; }
    }
    if (t->pend_seg >= 0) {
        // 4 KB pieces for as long as the frame's read budget lasts. The card reads at ~3 MB/s: the old fixed slices
        // (12 KB, 48 KB once the segment was due on screen) took 4-16 ms, and every 48 KB one was a dropped frame
        // (~1 a second). Prefetching by space as well (above) lets the reads start early enough at this rate.
        const SegRec *g = &t->segs[t->pend_seg];
        bool due = lod_base(t, t->pend_seg, (int)s, 0) >= 0;
#ifdef ARM9
        int v0 = REG_VCOUNT, budget = due ? 32 : 16;           // scanlines (63.5 us each)
#endif
        fseek(t->f, g->chunk_off + t->pend_done, SEEK_SET);
        for (int piece = 0; t->pend_done < g->chunk_size; piece++) {
#ifdef ARM9
            if (piece && (REG_VCOUNT - v0 + 263) % 263 >= budget) break;
#else
            if (piece >= (due ? 12 : 3)) break;
#endif
            u32 n = g->chunk_size - t->pend_done; if (n > 4096) n = 4096;
            if (fread(t->pend_buf + t->pend_done, 1, n, t->f) != n) { pend_cancel(t); return; }
#ifdef ARM9
            DC_FlushRange(t->pend_buf + t->pend_done, n);
#endif
            t->pend_done += n;
        }
        if (t->pend_done >= g->chunk_size) { int i = t->pend_seg; u8 *b = t->pend_buf; t->pend_buf = NULL; t->pend_seg = -1; seg_commit(t, i, b, false); }
    }
    col_tick(t);
}

void track_set_cam(Track *t, float x, float z, float fx, float fz, int reach)
{
    t->cam_y = t->cam_y_next;
    t->cam[0] = (s32)x; t->cam[1] = (s32)z; t->cam_f[0] = (s32)(fx * 1024); t->cam_f[1] = (s32)(fz * 1024); t->cam_ok = true;
    t->reach = reach;
}
// Segment culling against the view (x / z only, the screen's +-33 degrees widened to +-45 and 200 units of margin):
// the geometry engine transforms every vertex sent, culled by the hardware or not, and on the DS that - not the
// CPU - is what the frame waits for. Segments wholly behind the camera or off to one side are no longer sent.
#ifndef NOCULL
#define NOCULL 0
#endif
ITCM_CODE static bool box_out_of_view(const Track *t, const s32 org[3], int x0, int z0, int x1, int z1, int ybot, int ytop, bool far)
{
    if (!t->cam_ok || NOCULL) return false;
    int fx = t->cam_f[0], fz = t->cam_f[1];
    // port: a camera looking down onto a box below it sees it behind and wider to the sides than the flat x / z
    // test allows (from high in the air, ground beside / under the camera was culled: holes near the rider). A point
    // h below the eye is in front of it while fw > -h tan(pitch), and inside the side planes only while its side
    // distance < tan(32.8) (fw cos + h sin) <= fw + h tan(pitch): the margin grows by h tan(pitch).
    // The box's lowest point counts (its top did: a tall tile of mountainside reaching above the camera got no
    // margin for its lower part, and was culled at the screen's edge - R&B ~38 %, holes in the slopes beside the
    // rider), and the same above the camera when it looks up (tan of the upward tilt)
    s32 above = t->cam_y - (org[1] + ybot); if (above < 0) above = 0;
    s32 high = org[1] + ytop - t->cam_y; if (high < 0) high = 0;
    s32 m = 200 + ((above * t->cam_tanp) >> 10) + ((high * t->cam_tanu) >> 10);
    int behind = 0, left = 0, right = 0, past = 0;
    for (int c = 0; c < 4; c++) {
        s32 x = org[0] + ((c & 1) ? x1 : x0) - t->cam[0];
        s32 z = org[2] + ((c & 2) ? z1 : z0) - t->cam[1];
        s32 fw = (x * fx + z * fz) >> 10, sd = (x * fz - z * fx) >> 10;    // forward / right of the camera
        if (fw < -m) behind++;
        if (far && t->reach && fw > t->reach) past++;   // wholly past the fog's end (the fog is by depth, ~ forward
                                                       // distance): drawn, it is just fog colour
        if (sd > fw + m) right++;                        // tan 45 = 1
        if (-sd > fw + m) left++;
    }
    return behind == 4 || right == 4 || left == 4 || past == 4;
}
// the whole box inside the view's x / z wedge and short of the fog's end: none of its tiles needs testing
ITCM_CODE static bool box_inside(const Track *t, const s32 org[3], int x0, int z0, int x1, int z1, int ybot, int ytop)
{
    if (!t->cam_ok || NOCULL) return true;
    int fx = t->cam_f[0], fz = t->cam_f[1];
    s32 above = t->cam_y - (org[1] + ybot); if (above < 0) above = 0;
    s32 high = org[1] + ytop - t->cam_y; if (high < 0) high = 0;
    s32 m = 200 + ((above * t->cam_tanp) >> 10) + ((high * t->cam_tanu) >> 10);
    for (int c = 0; c < 4; c++) {
        s32 x = org[0] + ((c & 1) ? x1 : x0) - t->cam[0];
        s32 z = org[2] + ((c & 2) ? z1 : z0) - t->cam[1];
        s32 fw = (x * fx + z * fz) >> 10, sd = (x * fz - z * fx) >> 10;
        if (fw < -m || sd > fw + m || -sd > fw + m || (t->reach && fw > t->reach)) return false;
    }
    return true;
}
ITCM_CODE static bool seg_out_of_view(const Track *t, int i)
{
    const SegRec *g = &t->segs[i];
    return box_out_of_view(t, g->origin, g->bbmin[0], g->bbmin[2], g->bbmax[0], g->bbmax[2], g->bbmin[1], g->bbmax[1], false);
}

// port: the far tiles go last. Polygon / vertex RAM (2048 / 6144) keeps what arrives first and drops the rest, so
// the tiles nearer than FAR_PART of the fog's reach are sent with the scene, the farther ones after everything
// else of the frame (riders, effects, HUD), nearest first: when a frame overflows, what is lost is the farthest,
// most fogged course - not holes near the rider and not the whole course's detail switching to make room (the LOD
// bias now only reacts to what the near part needs).
#define FAR_PART 6 / 10
#define NFAR 320
static struct { u16 seg, lod, list; s16 fw; } far_q[NFAR]; static int nfar;
ITCM_CODE static s32 tile_fw(const Track *t, const s32 org[3], int x0, int z0, int x1, int z1)   // nearest corner, forward
{
    s32 m = 1 << 30;
    for (int c = 0; c < 4; c++) {
        s32 x = org[0] + ((c & 1) ? x1 : x0) - t->cam[0], z = org[2] + ((c & 2) ? z1 : z0) - t->cam[1];
        s32 fw = (x * t->cam_f[0] + z * t->cam_f[1]) >> 10;
        if (fw < m) m = fw;
    }
    return m;
}
// the course's polygon format as main sets it, and the same culled: lists of back-to-back faces (material | 0x4000)
// are always drawn culled - unculled, a board's two sides would fight in the depth buffer
u32 course_fmt, course_fmt_cull;
ITCM_CODE static int draw_list(Track *t, int i, const ListRec *lr, u8 *base)
{
    int m = lr->material;
    bool cull_own = m != 0xFFFF && (m & 0x4000) && course_fmt != course_fmt_cull;
    if (m != 0xFFFF) m &= 0x3FFF;
#ifdef TEST_SKIPMAT
    if (((u32)(TEST_SKIPMAT) >> (m == 0xFFFF ? 16 : m & 15)) & 1) return 0;
#endif
    if (cull_own) glPolyFmt(course_fmt_cull);
#ifdef TEST_NOTEX
    if (1) glBindTexture(0, 0); else
#endif
    if (m == 0xFFFF || t->mats[m].tex < 0) glBindTexture(0, 0);
    else glBindTexture(0, t->texnames[t->mats[m].tex]);
#ifdef ARM9
    {   // glCallList without its per-call cache flush (done once at load): a list per tile is ~50 a segment
        const u32 *dl = (const u32 *)(base + lr->off_dl);
        while (dmaBusy(0) || dmaBusy(1) || dmaBusy(2) || dmaBusy(3));
        dmaSetParams(0, dl + 1, (void *)&GFX_FIFO, DMA_FIFO | dl[0]);
        while (dmaBusy(0));
    }
#else
    glCallList(base + lr->off_dl);
#endif
    if (cull_own) glPolyFmt(course_fmt);
    (void)i;
    return lr->npolys;
}
ITCM_CODE static void seg_matrix(const Track *t, const SegRec *g)
{
    glPushMatrix();
    // 1 DS unit = 256 game units. v6: every segment's lists are relative to the course's one display origin, in
    // half units (translate origin/256 units = origin*16 in f32, scale VTX_16 by 32): one matrix for all of them, so
    // a corner two segments share lands on the same point (each through its own origin, the matrices' rounding
    // differed - hairline cracks along the segment borders)
    (void)g;
    s32 o[3]; for (int k = 0; k < 3; k++) o[k] = (t->h.bbmin[k] + t->h.bbmax[k]) >> 1;
    glTranslatef32(o[0] * 16, o[1] * 16, o[2] * 16);
    glScalef32(inttof32(32), inttof32(32), inttof32(32));
}
ITCM_CODE int track_draw_far(Track *t)
{
    int polys = 0;
    for (int a = 1; a < nfar; a++) {                         // nearest first (insertion sort: a few dozen)
        typeof(far_q[0]) x = far_q[a]; int b = a - 1;
        while (b >= 0 && far_q[b].fw > x.fw) { far_q[b + 1] = far_q[b]; b--; }
        far_q[b + 1] = x;
    }
    int cur = -1;
    for (int k = 0; k < nfar; k++) {
        int i = far_q[k].seg;
        if (!t->chunk[i]) continue;
        const SegRec *g = &t->segs[i];
        u8 *base = t->chunk[i] - g->chunk_off;
        const ListRec *lists = (const ListRec *)(base + g->lod[far_q[k].lod].off_lists);
        if (i != cur) { if (cur >= 0) glPopMatrix(1); seg_matrix(t, g); cur = i; }
        polys += draw_list(t, i, &lists[far_q[k].list], base);
    }
    if (cur >= 0) glPopMatrix(1);
    nfar = 0;
    return polys;
}

ITCM_CODE int track_draw(Track *t, float s, int bias)
{
    int polys = 0;
    nfar = 0;
    s32 far_d = t->reach && t->defer_far ? t->reach * FAR_PART : 1 << 30;
    for (u32 i = 0; i < t->h.nseg; i++) {
        int lod = lod_draw(t, i, s, bias);
        if (i == 0) t->missing = 0;
        if (lod >= 0 && !t->chunk[i]) t->missing++;
        t->lod_cur[i] = lod >= 0 && t->chunk[i] ? lod + 1 : 0;
        if (lod < 0 || !t->chunk[i]) continue;
        if (seg_out_of_view(t, i)) continue;
        const SegRec *g = &t->segs[i];
        const LodRec *L = &g->lod[lod];
        u8 *base = t->chunk[i] - g->chunk_off;          // file offset -> pointer
        const ListRec *lists = (const ListRec *)(base + L->off_lists);
        bool pushed = false;
        // (a segment wholly in view: its tiles are too - no test each; the far pass still sorts them by distance)
        bool all_in = box_inside(t, g->origin, g->bbmin[0], g->bbmin[2], g->bbmax[0], g->bbmax[2], g->bbmin[1], g->bbmax[1]);
        if (all_in && t->defer_far) {
            s32 sfw = -(1 << 30);                          // the box's farthest corner
            for (int c = 0; c < 4; c++) {
                s32 x = g->origin[0] + ((c & 1) ? g->bbmax[0] : g->bbmin[0]) - t->cam[0], z = g->origin[2] + ((c & 2) ? g->bbmax[2] : g->bbmin[2]) - t->cam[1];
                s32 fw = (x * t->cam_f[0] + z * t->cam_f[1]) >> 10; if (fw > sfw) sfw = fw;
            }
            if (sfw <= far_d) {                            // and none of it beyond the far pass's line: tiles as they come
                seg_matrix(t, g);
                for (int j = 0; j < L->nlists; j++) polys += draw_list(t, i, &lists[j], base);
                glPopMatrix(1);
                continue;
            }
            all_in = false;                                // (the far split needs each tile's distance)
        }
        for (int j = 0; j < L->nlists; j++) {
            const ListRec *lr = &lists[j];                 // v5: a tile of one material - left out when not in view
            if (!all_in && box_out_of_view(t, g->origin, lr->bbmin[0], lr->bbmin[2], lr->bbmax[0], lr->bbmax[2], lr->bbmin[1], lr->bbmax[1], true)) continue;
            if (t->defer_far && t->cam_ok && nfar < NFAR) {
                s32 fw = tile_fw(t, g->origin, lr->bbmin[0], lr->bbmin[2], lr->bbmax[0], lr->bbmax[2]);
                if (fw > far_d) {                          // for the far pass
                    far_q[nfar].seg = i; far_q[nfar].lod = lod; far_q[nfar].list = j; far_q[nfar].fw = fw > 32767 ? 32767 : fw;
                    nfar++; continue;
                }
            }
            if (!pushed) { seg_matrix(t, g); pushed = true; }
            polys += draw_list(t, i, lr, base);
        }
        if (pushed) glPopMatrix(1);
    }
    return polys;
}

// Highest collision surface under (x,z) that lies below y_max. Returns false if nothing found.
bool track_ground(const Track *t, float x, float z, float y_max, float *y_out, float n_out[3])
{
    bool found = false; float best = -1e30f;
    int ix = (int)x, iz = (int)z;
    for (u32 i = 0; i < t->h.nseg; i++) {
        if (!t->chunk[i]) continue;
        const SegRec *g = &t->segs[i];
        int lx = ix - g->origin[0], lz = iz - g->origin[2];
        if (lx < g->bbmin[0] - 8 || lx > g->bbmax[0] + 8 || lz < g->bbmin[2] - 8 || lz > g->bbmax[2] + 8) continue;
        const u8 *base = t->chunk[i] - g->chunk_off + g->off_collision;
        u16 ntri = ((const u16 *)base)[0], nvert = ((const u16 *)base)[1];
        const s16 *V = (const s16 *)(base + 4);
        const u16 *T = (const u16 *)(base + 4 + nvert * 6);
        float ly_max = y_max - g->origin[1];
        for (int k = 0; k < ntri; k++, T += TRI_U16) {
            const s16 *a = &V[T[0] * 3], *b = &V[T[1] * 3], *c = &V[T[2] * 3];
            int mnx = a[0] < b[0] ? a[0] : b[0]; if (c[0] < mnx) mnx = c[0];
            int mxx = a[0] > b[0] ? a[0] : b[0]; if (c[0] > mxx) mxx = c[0];
            if (lx < mnx || lx > mxx) continue;
            int mnz = a[2] < b[2] ? a[2] : b[2]; if (c[2] < mnz) mnz = c[2];
            int mxz = a[2] > b[2] ? a[2] : b[2]; if (c[2] > mxz) mxz = c[2];
            if (lz < mnz || lz > mxz) continue;
            // barycentric in XZ (integer area terms, 64-bit)
            long long d = (long long)(b[0] - a[0]) * (c[2] - a[2]) - (long long)(c[0] - a[0]) * (b[2] - a[2]);
            if (d == 0) continue;
            long long u = (long long)(lx - a[0]) * (c[2] - a[2]) - (long long)(c[0] - a[0]) * (lz - a[2]);
            long long v = (long long)(b[0] - a[0]) * (lz - a[2]) - (long long)(lx - a[0]) * (b[2] - a[2]);
            if (d < 0) { d = -d; u = -u; v = -v; }
            if (u < 0 || v < 0 || u + v > d) continue;
            float fu = (float)u / d, fv = (float)v / d;
            float y = a[1] + (b[1] - a[1]) * fu + (c[1] - a[1]) * fv;
            if (y > ly_max || y < best - g->origin[1] + 0) {
                if (y > ly_max) continue;
            }
            float wy = y + g->origin[1];
            if (wy > best) {
                best = wy; found = true;
                float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
                float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
                if (n[1] < 0) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
                float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]); if (l < 1e-6f) l = 1;
                n_out[0] = n[0] / l; n_out[1] = n[1] / l; n_out[2] = n[2] / l;
            }
        }
    }
    if (found) *y_out = best;
    return found;
}

#ifdef ARM9
// test builds: read every course texture back out of VRAM (banks mapped to the CPU for a moment) and compare
void track_tex_check(Track *t)
{
    if (!t->texnames) return;
    TexRec *tr = read_at(t->f, t->h.off_textures, t->h.ntex * sizeof(TexRec));
    if (!tr) return;
    while (GFX_STATUS & BIT(27));
    u32 saved = vramSetPrimaryBanks(VRAM_A_LCD, VRAM_B_LCD, VRAM_C_LCD, VRAM_D_LCD);
    int bad = 0;
    for (u32 i = 0; i < t->h.ntex && i < 32; i++) {
        const u8 *q = glGetTexturePointer(t->texnames[i]);
        if (!q) { bad++; continue; }
        u32 sm = 0, bytes = tr[i].w * tr[i].h;
        for (u32 k = 0; k < bytes; k++) sm = sm * 31 + q[k];
        if (sm != t->tex_sum[i]) bad |= 1 << (8 + i), bad++;
    }
    vramRestorePrimaryBanks(saved);
    vramSetBankE(VRAM_E_LCD);                            // the palettes too
    int pb = 0;
    for (u32 i = 0; i < t->h.ntex && i < 32; i++) {
        const u16 *q = glGetColorTablePointer(t->texnames[i]);
        if (!q) { pb++; continue; }
        u32 sm = 0; for (int k = 0; k < 256; k++) sm = sm * 31 + q[k];
        if (sm != t->pal_sum[i]) pb++;
    }
    vramSetBankE(VRAM_E_TEX_PALETTE);
    free(tr);
    t->tex_bad = bad; t->pal_bad = pb;
}
#endif

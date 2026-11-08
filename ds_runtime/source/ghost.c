// SSX 3 GizWipe - race opponents. The Gizmondo SSX 3 has no rider AI: the other racers are recordings ("ghosts")
// shipped in models/gst.pak as <event>_<n>.ght. Recorder 0x53d2c, loader 0x53574 / 0x53410, playback 0x541dc.
//
// .ght: header 0x30 bytes { i32 1001; i32 ?, ?; i32 nframes; i32 result (time or score, recorded clock);
//        i32 ?; i32 start x, y, z (game units); ... }, then nframes x 12 bytes:
//   i8 dx, dy, dz      position change (units, clamped to +-127)
//   i8 ux, uy, uz      rider up vector / 256
//   u8 flags           bit 0 switch stance, 1 in the air, 2 on a rail, 3/4 jump state, 6-7 surface (0x53b30)
//   u8 heading >> 3
//   u8 anim            animation id
//   u8 phase           position in the clip, 0..255
//   u8 dt / 16         recorded clock step (not used: the clock of the recording machine)
// One frame per 80 ms from the start of the countdown (frames 0-36 = 3-2-1 in the gate, header +0x14 = frames x 80 ms
// - 3 s = the race time); the last frame is[...]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ghost.h"
#include "phys_int.h"
#define GHOST_SPEED 0x80                 // 0x23d4: half a recorded frame per 40 ms step
#ifndef GHOST_DIR
#define GHOST_DIR "nitro:/ghosts"
#endif

// event table (0xdde00 + event * 0x54) as matched by the ghosts' start positions against the spawn markers
static const struct { const char *trk; s8 race, score; u8 riders; } EVENTS[] = {
    { "ara1", 0, -1, 4 }, { "bra2", 1, -1, 4 }, { "ASS1", -1, 2, 2 }, { "BHP1", -1, 3, 2 },
    { "ABC1", 4, 5, 2 }, { "CRA3", 6, -1, 4 }, { "DRA4", 7, -1, 4 }, { "DSS2", -1, 8, 2 },
    { "DBC2", 9, 10, 2 }, { "ERA5", 11, -1, 4 }, { "ESS3", -1, 12, 2 }, { "EBC3", 13, 14, 2 },
};

// out[0] = event, out[1] = riders in it (1 player + ghosts); race events first, else the trick event
int ghost_events_for(const char *track, int out[2])
{
    for (unsigned i = 0; i < sizeof(EVENTS) / sizeof(EVENTS[0]); i++)
        if (!strcmp(EVENTS[i].trk, track)) {
            out[0] = EVENTS[i].race >= 0 ? EVENTS[i].race : EVENTS[i].score;
            out[1] = EVENTS[i].riders;
            return EVENTS[i].race >= 0 ? 1 : 2;               // 1 = time race, 2 = trick event
        }
    return 0;
}

void ghost_free(Ghost *g) { free(g->frames); memset(g, 0, sizeof(*g)); }

static bool load(Ghost *g, int event, int id)
{
    char path[96]; snprintf(path, sizeof(path), GHOST_DIR "/%d_%d.ght", event, id);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    s32 h[12];
    if (fread(h, 4, 12, f) != 12 || h[0] != 1001 || h[3] <= 0 || h[3] > 8000) { fclose(f); return false; }
    ghost_free(g);
    g->frames = malloc(h[3] * 12);
    if (!g->frames || fread(g->frames, 12, h[3], f) != (size_t)h[3]) { fclose(f); ghost_free(g); return false; }
    fclose(f);
    g->n = h[3]; g->event = event; g->id = id; g->result = h[4];
    g->start[0] = h[6]; g->start[1] = h[7]; g->start[2] = h[8];
    return true;
}

// a recording held in memory (the player's own shadow, same layout as a .ght file)
bool ghost_from_mem(Ghost *g, const u8 *file, int bytes)
{
    const s32 *h = (const s32 *)file;
    if (bytes < 48 || h[0] != 1001 || h[3] <= 0 || 48 + h[3] * 12 > bytes) return false;
    ghost_free(g);
    g->frames = malloc(h[3] * 12);
    if (!g->frames) return false;
    memcpy(g->frames, file + 48, h[3] * 12);
    g->n = h[3]; g->event = -1; g->id = -1; g->result = h[4]; g->shadow = true;
    g->start[0] = h[6]; g->start[1] = h[7]; g->start[2] = h[8];
    return true;
}

// 0x53574: start at a random id and walk up to the next file that exists and no other rider uses
bool ghost_pick(Ghost *g, int event, int *used, int nused)
{
    int id = rand() & 0xff;
    for (int k = 0; k < 256; k++, id = (id + 1) & 0xff) {
        bool taken = false;
        for (int j = 0; j < nused; j++) if (used[j] == id) taken = true;
        if (!taken && load(g, event, id)) return true;
    }
    return false;
}

// pose between frame f and the next one nx at fraction fr/256 (0x541dc: heading 0x589ac, up vector lerped)
static void frame_pose(Ghost *g, const u8 *f, const u8 *nx, int fr)
{
    Phys *p = &g->ph;
    int flags = f[6];
    p->stance = (flags & 1) ? 0x400 : 0;
    p->air = (flags & 2) != 0;
    p->rail_state = (flags & 4) ? 2 : 0;
    int h0 = f[7] << 3, h1 = nx[7] << 3;
    p->heading = (h0 + sx_angdiff(h0, h1) * fr / 256) & 0x7ff;
    p->anim = f[8]; g->phase = f[9];
    if (nx[8] == f[8]) { int d = (nx[9] - f[9]) & 0xff; if (d < 128) g->phase = (f[9] + d * fr / 256) & 0xff; }
    s32 up[3];
    for (int k = 0; k < 3; k++) up[k] = (s8)f[3 + k] * (256 - fr) + (s8)nx[3 + k] * fr;
    if (!up[0] && !up[1] && !up[2]) up[1] = 0x4000;
    vnorm(up, 0x4000);
    memcpy(p->up, up, 12);
    build_matrix(p, up, (p->stance + p->heading) & 0x7ff);
    p->facing = (p->stance + p->heading) & 0x7ff;
}

static void progress(Ghost *g, const Track *t)
{
    const RacePoint *r = t->race; int n = t->h.nraceline;
    s32 x = g->ph.pos[0] >> 8, y = g->ph.pos[1] >> 8, z = g->ph.pos[2] >> 8;
    int i0 = g->seg_hint - 40, i1 = g->seg_hint + 40;
    if (i0 < 0) i0 = 0; if (i1 > n - 1) i1 = n - 1;
    s64 best = -1; int bi = g->seg_hint;
    for (int i = i0; i <= i1; i++) {
        s64 dx = r[i].x - x, dy = r[i].y - y, dz = r[i].z - z, d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < best) { best = d; bi = i; }
    }
    g->seg_hint = bi;
    int j = bi + 1 < n ? bi : bi - 1;
    float ax = r[j].x, ay = r[j].y, az = r[j].z, bx = r[j + 1].x - ax, by = r[j + 1].y - ay, bz = r[j + 1].z - az;
    float l2 = bx * bx + by * by + bz * bz, k = l2 > 0 ? ((x - ax) * bx + (y - ay) * by + (z - az) * bz) / l2 : 0;
    if (k < -1) k = -1; if (k > 2) k = 2;
    g->s = r[j].arclen + k * (r[j + 1].arclen - r[j].arclen);
    if (g->s < 0) g->s = 0;
}

void ghost_start(Ghost *g, const Track *t, int spawn)
{
    int ch = g->ph.character;
    phys_place(&g->ph, t, spawn);                                // spawn position on the ground (as for the player)
    g->ph.character = ch;
    for (int k = 0; k < 3; k++) {
        g->acc[k] = 0;
        g->off[k] = (g->ph.pos[k] >> 8) - g->start[k];
    }
    g->cur = 0; g->cur256 = 0; g->speed = GHOST_SPEED; g->steps = 0; g->finished = false; g->fin_steps = 0; g->seg_hint = 0;
    memset(g->push, 0, sizeof(g->push));
    g->aggr = 20 + rand() % 30; g->think_ms = 1000; g->delay_ms = g->want_ms = 0;
    g->punch_id = g->punch_ms = g->react_id = g->react_ms = g->knock_ms = 0;
    for (int k = 0; k < 3; k++) { g->last[k] = g->ph.pos[k] >> 8; g->kvel[k] = 0; }
    if (g->n) frame_pose(g, g->frames, g->frames, 0);
    g->ph.anim = A_GATE; g->phase = 128;                  // (the gate clip from half way, as 0x72758)
    progress(g, t);
}

// 0x541dc. The recordings hold one frame per 80 ms (the clip phases advance at that rate and the header's race time
// +0x14 = frames x 80 ms); the exe plays them at 0x80/256 frame per 40 ms update and interpolates in between.
ITCM_CODE void ghost_step(Ghost *g, Track *t)
{
    if (!g->n) return;
    g->steps++;
    if (g->cur >= g->n) {                                        // end of the recording = crossed the line
        if (!g->finished) { g->finished = true; g->fin_steps = g->steps; }
        return;
    }
    if (g->knock_ms > 0) {                                       // knocked down (0x54864): slide, the clip runs
        g->knock_ms -= DT;                                       // on at half speed underneath (0x54998)
        g->cur256 += g->speed / 2;
        while (g->cur < (g->cur256 >> 8) && g->cur < g->n) {
            const u8 *f = g->frames + g->cur * 12;
            for (int k = 0; k < 3; k++) g->acc[k] += (s8)f[k];
            g->cur++;
        }
        s32 pos[3];
        for (int k = 0; k < 3; k++) { g->kvel[k] = g->kvel[k] * 7 / 8; pos[k] = (g->ph.pos[k] >> 8) + g->kvel[k] / 256; }   // (0x54864 keeps x63/64: a slide of ~1500 units here)
        s32 a[3] = { pos[0], pos[1] + 160, pos[2] }, b[3] = { pos[0], pos[1] - 480, pos[2] }, h[3], n[3]; int sf;
        if (track_ray(t, a, b, h, n, &sf)) pos[1] = h[1];
        for (int k = 0; k < 3; k++) {
            g->ph.pos[k] = pos[k] << 8;
            g->off[k] = pos[k] - g->start[k] - g->acc[k];      // rejoin the recording from here (0x54998)
            g->last[k] = pos[k];
        }
        g->react_ms += DT;
        int lie = g->react_ms > 600, up = g->knock_ms < 1100;
        g->ph.anim = up ? A_WIPE_GETUP : lie ? A_WIPE_LIE : A_WIPE_HIT_GROUND;
        g->react_id = g->ph.anim;
        g->phase = up ? (1100 - g->knock_ms) * 256 / 1100 : lie ? (g->react_ms / 4) & 0xff : g->react_ms * 256 / 600;
        if (g->phase > 255) g->phase = 255; if (g->phase < 0) g->phase = 0;
        if (g->knock_ms <= 0) { g->knock_ms = 0; g->react_id = 0; g->speed = GHOST_SPEED / 2; }
        progress(g, t);
        return;
    }
    // speed: a bump (ghost_bump) slows it to 3/4 or 1/2; it recovers over ~1.5 s (port addition)
    if (g->speed < GHOST_SPEED) { g->speed += 3; if (g->speed > GHOST_SPEED) g->speed = GHOST_SPEED; }
    g->cur256 += g->speed;
    while (g->cur < (g->cur256 >> 8) && g->cur < g->n) {
        const u8 *f = g->frames + g->cur * 12;
        for (int k = 0; k < 3; k++) g->acc[k] += (s8)f[k];
        g->cur++;
    }
    int fr = g->cur256 & 0xff;
    const u8 *f = g->frames + (g->cur > 0 ? g->cur - 1 : 0) * 12;
    const u8 *nx = g->cur < g->n ? g->frames + g->cur * 12 : f;
    frame_pose(g, f, nx, nx == f ? 0 : fr);
    s32 pos[3];
    for (int k = 0; k < 3; k++) {
        g->off[k] += g->push[k] * DT8 / 16384;                   // collision push (0x23e8, decays x15/16)
        g->push[k] = g->push[k] * 15 / 16;
        pos[k] = g->start[k] + g->acc[k] + g->off[k] + (nx != f ? (s8)nx[k] * fr / 256 : 0);
    }
    // on the ground, after the first 50 frames, sit on the ground under the (offset) position
    if (!g->ph.air && !g->ph.rail_state && g->cur > 49) {
        s32 a[3] = { pos[0], pos[1] + 160, pos[2] }, b[3] = { pos[0], pos[1] - 480, pos[2] }, h[3], n[3]; int sf;
        if (track_ray(t, a, b, h, n, &sf)) pos[1] = h[1];
    }
    for (int k = 0; k < 3; k++) {
        g->ph.pos[k] = pos[k] << 8;
        g->off[k] = g->off[k] * 31 / 32;                         // converge on the recorded line
    }
    // a punch being thrown or taken shows over the recorded clip
    if (g->punch_id) {
        g->punch_ms += DT;
        int len = 520, hold = 180;                               // out to the hold point, held, then back
        if (g->punch_ms >= len) g->punch_id = 0;
        else {
            int t2 = g->punch_ms < hold ? g->punch_ms : g->punch_ms < len - hold ? hold : len - g->punch_ms;
            g->ph.anim = g->punch_id; g->phase = t2 * 256 / 600;
        }
    } else if (g->react_id) {
        g->react_ms += DT;
        if (g->react_ms >= 700) g->react_id = 0;
        else { g->ph.anim = g->react_id; g->phase = g->react_ms * 256 / 700; }
    }
    for (int k = 0; k < 3; k++) {                                // slide velocity if knocked down: the last step's,
        s32 v = pos[k] - g->last[k];                             // along the ground, at most 40 units a step
        if (v > 40) v = 40; if (v < -40) v = -40;
        g->kvel[k] = k == 1 ? 0 : v * 256; g->last[k] = pos[k];
    }
    progress(g, t);
}

// rider bumps (0x73898 -> 0x553bc): within 40 units, neither grinding. The player is pushed apart (velocity x15/16 +
// the position difference), the ghost gets the opposite push and its playback slows to 3/4 or 1/2.
bool ghost_bump(Ghost *g, Phys *p)
{
    if (!g->n || g->finished || g->shadow || g->knock_ms || p->rail_state || g->ph.rail_state) return false;
    s32 d[3]; s64 d2 = 0;
    for (int k = 0; k < 3; k++) { d[k] = p->pos[k] - g->ph.pos[k]; s32 u = d[k] >> 8; d2 += (s64)u * u; }
    if (d2 >= 0x640) return false;
    p->vel[0] = p->vel[0] * 15 / 16 + d[0]; p->vel[2] = p->vel[2] * 15 / 16 + d[2];
    g->push[0] = g->push[0] * 7 / 8 - d[0]; g->push[2] = g->push[2] * 7 / 8 - d[2];
    g->speed = (rand() & 1) ? g->speed * 3 / 4 : g->speed / 2;
    return true;
}

// the recorded results of every ghost file of an event (the trick events' field of scores)
int ghost_scores(int event, s32 *out, int max)
{
    int n = 0;
    for (int id = 0; id < 256 && n < max; id++) {
        char path[96]; snprintf(path, sizeof(path), GHOST_DIR "/%d_%d.ght", event, id);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        s32 h[5];
        if (fread(h, 4, 5, f) == 5 && h[0] == 1001) out[n++] = h[4];
        fclose(f);
    }
    return n;
}

//[...][...]------------ punches (0x73898 for the player and one ghost)
static int rnd(int n) { return n > 0 ? rand() % n : 0; }
static int side_of(s32 facing, const s32 from[3], const s32 to[3])   // 0x4cf88: which side `to` is on
{
    s32 fx = sx_sin[facing & 0x7ff], fz = sx_sin[(facing + 0x200) & 0x7ff];
    s32 dx = (to[0] - from[0]) >> 8, dz = (to[2] - from[2]) >> 8;
    return (s64)fx * dz - (s64)fz * dx < 0 ? 1 : 2;
}
// 0x74090: a punch that is out lands within 97 units. Closer than (attacker's toughness - victim's) / 2 + 90 knocks
// the victim down, else they stumble
static int land(s32 d, int t_att, int t_vic) { return d < (t_att - t_vic) / 2 + 90 ? 2 : 1; }

int ghost_fight(Ghost *g, Phys *p)
{
    if (!g->n || g->finished || g->shadow || g->knock_ms) return 0;
    s64 d2 = 0; s32 dd[3];
    for (int k = 0; k < 3; k++) { dd[k] = (p->pos[k] - g->ph.pos[k]) >> 8; d2 += (s64)dd[k] * dd[k]; }
    if (d2 >= 160000) { g->want_ms = 0; g->delay_ms = 0; return 0; }
    bool g_ok = !g->ph.air && !g->ph.rail_state, p_ok = !p->air && !p->rail_state && p->jstate != 7 && !p->finished;
    // 0x73ecc: the ghost decides to go for a punch (once a second: half the time; tougher riders always think about
    // it, weaker ones a third of the time; then aggression %), after 0 / 1 / 2 / 3 s
    if (g_ok && p_ok) {
        if (g->delay_ms > 0 && (g->delay_ms -= DT) <= 0) { g->delay_ms = 0; g->want_ms = 1500; }
        if (g->want_ms > 0) g->want_ms -= DT;
        if ((g->think_ms -= DT) <= 0) {
            g->think_ms = 1000;
            if (!g->delay_ms && g->want_ms <= 0 && !rnd(2) && (g->tough - p->st_tough < 0x33 || rnd(3)) && rnd(100) < g->aggr) {
                int r = rnd(100);
                if (r < 10) g->want_ms = 1500; else g->delay_ms = r < 30 ? 1000 : r < 70 ? 2000 : 3000;
            }
        }
    }
    s32 dxz = 0; { s64 h = (s64)dd[0] * dd[0] + (s64)dd[2] * dd[2]; while ((s64)(dxz + 1) * (dxz + 1) <= h) dxz += dxz > 64 ? 8 : 1; }
    bool near = d2 < 0x2341;
    // 0x550a4: close enough, it throws the punch toward the player's side
    if (near && g_ok && g->want_ms > 0 && !g->punch_id) {
        int side = side_of(g->ph.facing, g->ph.pos, p->pos);
        bool mir = g->ph.stance != 0;
        g->punch_id = (side == 1) != mir ? 0x57 : 0x58; g->punch_ms = 0; g->want_ms = 0;
    }
    int res = 0;
    if (near && p_ok && g_ok) {
        if (g->punch_id && g->punch_ms >= 180 && g->punch_ms < 340) {            // the ghost's punch is out
            int side = side_of(g->ph.facing, g->ph.pos, p->pos);
            g->punch_ms = 340;                                                     // one hit per punch
            if (land(dxz, g->tough, p->st_tough) == 2) { phys_knockdown(p); res = 1; }
            else { phys_stumble(p, side); res = 3; }
        } else if (p->punch && g_ok) {                                             // the player's
            int side = side_of(p->facing, p->pos, g->ph.pos);
            if (land(dxz, p->st_tough, g->tough) == 2) {
                g->knock_ms = 5000; g->react_ms = 0; g->punch_id = 0;          // 0x54094
                g->aggr += 25;                                                    // 0x53cdc(0x19)
                p->boost = 0x6400;                                                // 0x52c9c: the attacker's meter fills
                res = 2;
            } else {
                bool mir = g->ph.stance != 0;
                g->react_id = (side == 2) != mir ? 0x31 : 0x32; g->react_ms = 0;
                g->aggr += 10; res = 3;
            }
            if (g->aggr > 100) g->aggr = 100;
            p->punch = 0; p->punch_hit = 1;
        }
    }
    return res;
}

// replay rewind: step the playback back (the frames are deltas, so the accumulated position is walked back too).
// The ground snap and the offset are kept; good enough for watching (the exe keeps a full state buffer instead).
void ghost_rewind(Ghost *g, Track *t, int steps)
{
    if (!g->n || g->steps <= 0) return;
    g->knock_ms = 0; g->punch_id = 0; g->react_id = 0;
    for (int i = 0; i < steps && g->steps > 0; i++) {
        g->steps--;
        g->cur256 -= GHOST_SPEED; if (g->cur256 < 0) g->cur256 = 0;
    }
    if (g->cur256 >> 8 < g->n) { g->finished = false; g->fin_steps = 0; }
    while (g->cur > (g->cur256 >> 8) && g->cur > 0) {
        g->cur--;
        const u8 *f = g->frames + g->cur * 12;
        for (int k = 0; k < 3; k++) g->acc[k] -= (s8)f[k];
    }
    int fr = g->cur256 & 0xff;
    const u8 *f = g->frames + (g->cur > 0 ? g->cur - 1 : 0) * 12;
    const u8 *nx = g->cur < g->n ? g->frames + g->cur * 12 : f;
    frame_pose(g, f, nx, nx == f ? 0 : fr);
    s32 pos[3];
    for (int k = 0; k < 3; k++) pos[k] = g->start[k] + g->acc[k] + g->off[k] + (nx != f ? (s8)nx[k] * fr / 256 : 0);
    if (!g->ph.air && !g->ph.rail_state) {
        s32 a[3] = { pos[0], pos[1] + 160, pos[2] }, b[3] = { pos[0], pos[1] - 480, pos[2] }, h[3], n[3]; int sf;
        if (track_ray(t, a, b, h, n, &sf)) pos[1] = h[1];
    }
    for (int k = 0; k < 3; k++) { g->ph.pos[k] = pos[k] << 8; g->last[k] = pos[k]; }
    progress(g, t);
}

// ---- wireless sessions: the other players
int ghost_punch_remote(Ghost *g, Phys *p)
{
    if (!p->punch || p->finished || g->finished || (g->net_flags & 16)) return 0;
    s64 d2 = 0; s32 dd[3];
    for (int k = 0; k < 3; k++) { dd[k] = (p->pos[k] - g->ph.pos[k]) >> 8; d2 += (s64)dd[k] * dd[k]; }
    if (d2 >= 0x2341 || g->ph.air || g->ph.rail_state || p->air || p->rail_state) return 0;
    s32 dxz = 0; { s64 h = (s64)dd[0] * dd[0] + (s64)dd[2] * dd[2]; while ((s64)(dxz + 1) * (dxz + 1) <= h) dxz += dxz > 64 ? 8 : 1; }
    p->punch = 0; p->punch_hit = 1;
    if (land(dxz, p->st_tough, g->tough) == 2) { p->boost = 0x6400; return 2; }
    return 3;
}

bool ghost_bump_remote(Ghost *g, Phys *p)
{
    if (g->finished || p->rail_state || g->ph.rail_state || p->finished) return false;
    s32 d[3]; s64 d2 = 0;
    for (int k = 0; k < 3; k++) { d[k] = p->pos[k] - g->ph.pos[k]; s32 u = d[k] >> 8; d2 += (s64)u * u; }
    if (d2 >= 0x640) return false;
    p->vel[0] = p->vel[0] * 15 / 16 + d[0]; p->vel[2] = p->vel[2] * 15 / 16 + d[2];
    return true;
}

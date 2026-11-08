// SSX 3 GizWipe - rider physics ported from the Gizmondo SSX 3 executable (ssx_3.exe, WinCE ARM).
//
// Integer maths and constants follow the original code; the function each block comes from is given as
// its address in the exe (Ghidra decompiles in re/gh/). Units: positions 24.8 fixed-point game units
// (1 unit = 1 inch), vectors 2.14 (0x4000 = 1.0), angles 11 bit (0x800 = 360 deg), velocity such that
// pos(24.8) += vel * dt8 / 64 with dt8 = dt_ms * 256 / 1000 (vel / 64 = inches per second).
//
// Tricks, grabs, scoring and wipeouts are in tricks.c, grind rails in rails.c.
// Not ported yet: invisible wall lines (0x4a3cc), pickups (0x4ef68), finish line (0x4bc4c), AI riders.
#include <string.h>
#include "phys_int.h"

// per-surface tables (index = face flags bits 16..19)
static const s16 turn_acc_grip[12] = { 256, 256, 256, 256, 208, 288, 256, 256, 256, 256, 256, 256 }; // 0xa9524
static const s16 turn_max_grip[12] = { 256, 256, 256, 256, 224, 288, 256, 256, 256, 256, 256, 256 }; // 0xa94fc
static const s16 speed_grip[12]    = { 256, 256, 256, 256, 232, 320, 256, 256, 256, 256, 256, 256 }; // 0xa954c
static const s16 edge_grip[12]     = { 256, 256, 256, 256, 224, 224, 256, 256, 256, 256,   0, 180 }; // 0xa9574

//[...][...]------------ maths (0x850ec, 0x8538c, 0x5872c, 0x58a50)
#ifdef ARM9
// the ARM9 has no divide instruction and no FPU, but the DS has a square-root and a divide unit (math.h): exact
// integer floor results, so the same numbers as the bit loops below (a replay / shadow recorded either way agrees)
#include <nds/arm9/math.h>
static inline u32 hw_sqrt64(u64 v)
{
    REG_SQRTCNT = SQRT_64; REG_SQRT_PARAM = v;
    while (REG_SQRTCNT & SQRT_BUSY);
    return REG_SQRT_RESULT;
}
u32 isqrt(u32 v) { return hw_sqrt64(v); }
u32 isqrt64(u64 v) { return hw_sqrt64(v); }
#else
u32 isqrt(u32 v)
{
    u32 r = 0, b = 1u << 30;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return r;
}
u32 isqrt64(u64 v)
{
    if (v < 0x100000000ULL) return isqrt((u32)v);
    u64 r = 0, b = 1ULL << 62;
    while (b > v) b >>= 2;
    while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
    return (u32)r;
}
#endif
int sx_atan2(int x, int z)
{
    if (x == 0 && z == 0) return 0;
    bool nx = x < 0, nz = z < 0;
    if (nx) x = -x;
    if (nz) z = -z;
    u32 u;
    if (z < x) { int d = x >> 4; int i = d ? (z << 6) / d : 1024; if (i > 1024) i = 1024; u = sx_atan[i]; }
    else       { int d = z >> 4; int i = d ? (x << 6) / d : 1024; if (i > 1024) i = 1024; u = (0x400 - sx_atan[i]) & 0xffff; }
    if (nx) u = nz ? u + 0x800 : 0x800 - u;
    else if (nz) u = -u;
    u &= 0xffff;
    return (0x200 - (int)(u >> 1)) & 0x7ff;
}
int sx_angdiff(int a, int b) { int d = (b - a) & 0x7ff; return d > 0x400 ? d - 0x800 : d; }
int sx_ease(int a, int b)
{
    u32 p = a & 0x7ff, q = b & 0x7ff;
    if (p == q) return p;
    int d = (int)(q - p); if (d < 0) d = -d;
    if (d > 0x400) return p > 0x3ff ? (int)((p * 7 + q + 0x807) >> 3) & 0x7ff : (int)(((p + 0x800) * 7 + q) >> 3) & 0x7ff;
    if (p <= q) return (int)(((p + 1) * 7 + q) >> 3) & 0x7ff;
    return (int)((p * 7 + q) >> 3) & 0x7ff;
}
void vnorm(s32 v[3], s32 len)                               // 0x594fc (float in the original)
{
    u32 l = isqrt64((s64)v[0] * v[0] + (s64)v[1] * v[1] + (s64)v[2] * v[2]);
    if (!l) { v[0] = 0; v[1] = len; v[2] = 0; return; }
#ifdef ARM9
    for (int k = 0; k < 3; k++) v[k] = div64((s64)v[k] * len, (s32)l);   // the divide unit (64 / 32, truncating as C)
#else
    for (int k = 0; k < 3; k++) v[k] = (s32)((s64)v[k] * len / (s32)l);
#endif
}
s32 vlen_v(const s32 v[3])                                  // |v| in units/s, as the original computes it
{
    s32 x = v[0] * 256 / 16384, y = v[1] * 256 / 16384, z = v[2] * 256 / 16384;
    return isqrt(x * x + y * y + z * z);
}
static void reflect(s32 out[3], const s32 v[3], const s32 n[3])    // 0x67524
{
    s32 d = (n[0] * v[0] + n[1] * v[1] + n[2] * v[2]) / 16384;
    for (int k = 0; k < 3; k++) out[k] = v[k] - d * n[k] * 2 / 16384;
}

//[...][...]------------ collision probes
bool ray24(Phys *p, const s32 a[3], const s32 b[3], s32 hit[3], s32 n[3], int *surf)   // 0x68180
{
    s32 ua[3] = { a[0] / 256, a[1] / 256, a[2] / 256 }, ub[3] = { b[0] / 256, b[1] / 256, b[2] / 256 }, h[3];
    if (!track_ray((Track *)p->trk, ua, ub, h, n, surf)) return false;
    hit[0] = h[0] << 8; hit[1] = h[1] << 8; hit[2] = h[2] << 8;
    return true;
}

static void probe_air(Phys *p)                                     // 0x5174c
{
    s32 s[3], e[3];
    p->surf = 0;
    for (int k = 0; k < 3; k++) { s[k] = p->pos[k] + p->up[k]; e[k] = p->pos[k] - p->up[k] * 4; }
    if (ray24(p, s, e, p->contact, p->nrm, &p->surf)) { p->miss = 0; return; }
    for (int k = 0; k < 3; k++) { s[k] += p->up[k] * 2; e[k] -= p->up[k] * 4; }
    if (ray24(p, s, e, p->contact, p->nrm, &p->surf)) { p->miss = 0; return; }
    for (int k = 0; k < 3; k++) e[k] += p->vel[k] * 2;
    if (ray24(p, s, e, p->contact, p->nrm, &p->surf)) { p->miss = 0; return; }
    memcpy(p->contact, e, 12);
    // 0x67f48: anything at all below? (otherwise count a miss -> respawn after 15)
    s32 a[3] = { p->pos[0], p->pos[1] + 0x4000, p->pos[2] }, b[3] = { p->pos[0], p->pos[1] - (8000 << 8), p->pos[2] }, h[3], n[3];
    int sf;
    if (!ray24(p, a, b, h, n, &sf)) p->miss++;
}

static void probe_ground(Phys *p)                                  // 0x519d0
{
    s32 s[3], e[3];
    p->surf = 0;
    for (int k = 0; k < 3; k++) { s[k] = p->pos[k] + p->up[k] / 2; e[k] = p->pos[k] - p->up[k] * 2; }
    if (ray24(p, s, e, p->contact, p->nrm, &p->surf)) { p->miss = 0; return; }
    for (int k = 0; k < 3; k++) s[k] += p->up[k] * 2;
    for (int i = 0; i < 4; i++) {                                  // 4 rays on a 20 unit circle
        int a = i * 0x200;
        s32 ox = SIN(a) * 0x13fc / 16384, oz = COS(a) * 0x13fc / 16384;
        s32 rs[3] = { s[0] + ox, s[1], s[2] + oz }, re[3] = { e[0] + ox, e[1], e[2] + oz };
        if (ray24(p, rs, re, p->contact, p->nrm, &p->surf)) { p->miss = 0; return; }
    }
    probe_air(p);
}

//[...][...]------------ orientation
void build_matrix(Phys *p, const s32 up[3], int yaw)        // 0x50000 (columns right, up, forward)
{
    s32 r[3], f[3];
    s32 ay = up[1] < 0 ? -up[1] : up[1];
    if (ay < 1) {                                                   // up horizontal: degenerate branch
        f[0] = 0; f[1] = -up[2] * 0x4000; f[2] = up[1] * 0x4000;
        vnorm(f, 0x4000);
    } else {
        s32 sv[3] = { COS(yaw) * 0x4000, 0, -SIN(yaw) * 0x4000 };   // side vector for this yaw
        vnorm(sv, 0x4000);
        f[0] = (s32)(((s64)up[2] * sv[1] - (s64)up[1] * sv[2]) >> 14);
        f[1] = (s32)(((s64)up[0] * sv[2] - (s64)up[2] * sv[0]) >> 14);
        f[2] = (s32)(((s64)up[1] * sv[0] - (s64)up[0] * sv[1]) >> 14);
        vnorm(f, 0x4000);
    }
    r[0] = (s32)(((s64)up[1] * f[2] - (s64)up[2] * f[1]) >> 14);
    r[1] = (s32)(((s64)up[2] * f[0] - (s64)up[0] * f[2]) >> 14);
    r[2] = (s32)(((s64)up[0] * f[1] - (s64)up[1] * f[0]) >> 14);
    vnorm(r, 0x4000);
    for (int k = 0; k < 3; k++) { p->mat[k][0] = r[k]; p->mat[k][1] = up[k]; p->mat[k][2] = f[k]; }
}

static void orient_ground(Phys *p)                                 // 0x4ff24: up = (3 up + normal) / 4
{
    for (int k = 0; k < 3; k++) p->up[k] = (p->up[k] * 3 + p->nrm[k]) / 4;
    vnorm(p->up, 0x4000);
    build_matrix(p, p->up, (p->stance + p->heading) & 0x7ff);
}

static void orient_air(Phys *p)                                    // 0x4fba4
{
    if (p->air_ticks > 14 && p->vel[1] < 1 && (p->nrm[1] >= -0x1000 || p->up[1] <= -0x1000)) {
        if (p->height < 0x10001) {
            if (!SURF_WALL(p->surf))
                for (int k = 0; k < 3; k++) p->up[k] = (p->up[k] * 7 + p->nrm[k]) / 8;
        } else {
            p->up[0] = p->up[0] * 7 / 8; p->up[1] = (p->up[1] * 7 + 0x4000) / 8; p->up[2] = p->up[2] * 7 / 8;
        }
    }
    vnorm(p->up, 0x4000);
    build_matrix(p, p->up, (p->stance + p->heading) & 0x7ff);
    p->lean /= 2;
}

void travel_update(Phys *p)                                 // 0x5263c
{
    if (p->vel[0] || p->vel[2]) p->travel = sx_ease(p->travel, sx_atan2(p->vel[0] << 5, p->vel[2] << 5));
    else p->travel = -1;
}

//[...][...]------------ input handling (ground)
static void turn_acc(Phys *p, int stat, int dir)                   // 0x3c8e0
{
    if (p->buttons & PB_JUMP) return;
    s32 d = stat * 10 / 256 * turn_acc_grip[p->surf] * dir / 256;  // stat * dt8 >> 8 (dt8 = 10)
    if (p->turn * dir + 0x40 < 0) { p->turn += d; return; }
    int a = p->turn < 0 ? -p->turn : p->turn; if (a > 0x100) a = 0x100;
    p->turn += (a + 0x100) * d / 512;
}

static void steer(Phys *p)                                         // 0x3c9b0 (animation choices omitted)
{
    u32 b = p->buttons;
    if (b & PB_TUCK) {
        p->momentum += p->st_tuck;
        if (b & PB_RIGHT) turn_acc(p, p->st_turn_tuck, 1);
        else if (b & PB_LEFT) turn_acc(p, p->st_turn_tuck, -1);
    }
    if (!((b & (PB_TUCK | PB_BRAKE)) && (p->kmh > 0x44 || p->surf == 4))) {
        if (b & PB_RIGHT) turn_acc(p, p->st_turn, 1);
        if (b & PB_LEFT) turn_acc(p, p->st_turn, -1);
    }
    if (b & PB_BRAKE) {                                            // (the original also waits 750 ms after the start)
        if (b & PB_LEFT) turn_acc(p, p->st_turn, -1);
        else if (b & PB_RIGHT) turn_acc(p, p->st_turn, 1);
        p->sw_ctr = 0;
        if (!p->braking) { p->braking = true; snd(p, S_SLIDE); }   // 0x3c9b0: power slide
        if (p->brake_k < 16) p->brake_k++;
        p->momentum = (16 - p->brake_k) * p->momentum / 16;
        if (p->tol < 0x3000) p->tol += 0x2a0;
        if (p->kmh < 4) p->turn /= 2;
        return;
    }
    p->brake_k = 0; p->braking = false;
}

void boost_input(Phys *p)                                          // 0x41234
{
    int before = p->boost;
    bool was = p->boosting;
    p->boosting = false;
    if (p->buttons & PB_BOOST) {
        if (!p->boost) { if (p->pressed & PB_BOOST) snd(p, S_BOOST_OUT); }
        else {
            if (!was) snd(p, S_BOOST);
            p->boosting = true;
            if (p->boost > 0x4c && p->boost_level != 3) p->boost -= 0x4c;   // level 3: unlimited boost
            p->momentum += p->rail_state ? p->st_boost * 5 / 2 : p->st_boost;
        }
    }
    if (before > 0x4b && p->boost < 0x4c) {
        p->boost = 0; p->boosting = false;
        if (p->boost_full) { p->boost_full = false; p->boost_level = 0; }
    }
}

static void ground_input(Phys *p)                                  // 0x40c04 + jump state handlers
{
    if (p->jstate == 4 && p->t_state > 500) { p->jstate = 5; p->t_state = 0; }
    switch (p->jstate) {
    case 0: steer(p); if (!p->rail_state) punch_input(p); else p->punch = 0; break;
    case 2:                                                        // 0x3e1d4 crouch: charge, no turning
        if (p->charge < 1000) p->charge += DT;
        p->turn = 0;
        if (!(p->buttons & PB_JUMP)) p->jstate = 3;
        break;
    case 3:                                                        // 0x3c514 / 0x3c56c take-off
        tricks_takeoff(p);
        break;
    case 5:                                                        // 0x3f6f8 landed: bank the trick, 0x3f444
        p->charge = 0;
        if (p->land_ok) tricks_land(p);
        if (p->buttons & PB_TUCK) {
            if (p->buttons & PB_RIGHT) turn_acc(p, p->st_turn_tuck, 1);
            if (p->buttons & PB_LEFT) turn_acc(p, p->st_turn_tuck, -1);
        } else {
            if (p->buttons & PB_RIGHT) turn_acc(p, p->st_turn, 1);
            if (p->buttons & PB_LEFT) turn_acc(p, p->st_turn, -1);
        }
        if (p->anim_done || (p->anim != A_LAND && p->anim != A_LAND_HARD)) p->jstate = 0;
        break;
    case 7:                                                        // 0x3fedc wipeout
        wipeout_step(p);
        return;
    }
    if (p->jstate == 7) return;
    if (p->buttons & PB_JUMP) {
        p->momentum += p->st_tuck;
        if (p->jstate == 0) { p->jstate = 2; anim_play(p, A_CROUCH, false, false); }
    }
    boost_input(p);
}

static void switch_check(Phys *p);

//[...][...]------------ forces
static void ground_forces(Phys *p)                                 // 0x508d8
{
    if (p->momentum < 0) p->momentum = 0;
    s32 g = 10 * 0x5f8c / 256;                                     // gravity per 40 ms step
    p->vel[1] += g;                                                // (undo the gravity applied by the step)
    if ((p->vel[1] / 2) * (p->nrm[1] / 2) + (p->vel[2] / 2) * (p->nrm[2] / 2) + (p->vel[0] / 2) * (p->nrm[0] / 2) < 0) {
        s32 sp = vlen_v(p->vel), rf[3];                            // into the surface: slide along it, keep speed
        reflect(rf, p->vel, p->nrm);
        for (int k = 0; k < 3; k++) p->vel[k] += rf[k];
        s32 l = vlen_v(p->vel);
        if (l) for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * sp / l;
    }
    p->vel[1] -= g;
    // slope relative to the facing direction (0x5b7f8 yaw matrix * normal, z): < 0 = uphill
    int a = (p->stance - p->heading) & 0x7ff;
    s32 slope_z = (-SIN(a) * p->nrm[0] + COS(a) * p->nrm[2]) / 16384;
    s32 cur = vlen_v(p->vel), sp = cur;
    int at = p->turn < 0 ? -p->turn : p->turn;
    p->momentum -= at * 20 / 256;
    s32 m = p->momentum, mg = m * speed_grip[p->surf] / 256;
    s32 t = (p->st_friction - 1) * sp / p->st_friction;
    t = t * (mg + 0xf00) / 4096;
    if (t < cur) t = (cur * 3 + t) / 4;
    bool skip_min = false;
    if (slope_z < 0 || p->uphill_ctr < 0) { if (++p->uphill_ctr > 0) skip_min = true; }
    else p->uphill_ctr = 0;
    if (!skip_min && p->brake_k == 0) {
        s32 vmin = mg * 0x2d6 / 256 * m / 256;
        if (t < vmin) t = (t * 7 + vmin) / 8;
    }
    s32 vmax = mg * 0x528 / 256;
    if (vmax < t) t = (vmax + t) / 2;
    if (sp) {
        p->vel[0] = p->vel[0] * t / sp;
        p->vel[1] = p->vel[1] * t / sp;
        p->vel[2] = p->vel[2] * t / sp;
        s32 cap = ((p->vel[2] < 0 ? -p->vel[2] : p->vel[2]) + (p->vel[0] < 0 ? -p->vel[0] : p->vel[0])) / 2;
        if (cap < p->vel[1]) p->vel[1] = cap;
    }
    // carve: blend velocity toward the board's forward direction at the target speed
    s32 fs = t * 0x4000 / 256;
    s32 fw[3] = { p->mat[0][2] * fs / 16384, p->mat[1][2] * fs / 16384, p->mat[2][2] * fs / 16384 };
    s32 bl = edge_grip[p->surf] * p->st_edge / 256, ib = 256 - bl;
    for (int k = 0; k < 3; k++) p->vel[k] = (bl * fw[k] + p->vel[k] * ib) / 256;
}

static void pipe_forces(Phys *p)                                   // 0x50f90 (superpipe 0x508d8)
{
    if (p->momentum < 0) p->momentum = 0;
    s32 g = 10 * 0x5f8c / 256;
    if (!p->lip) {
        p->vel[1] += g;
        if ((p->vel[1] / 2) * (p->nrm[1] / 2) + (p->vel[2] / 2) * (p->nrm[2] / 2) + (p->vel[0] / 2) * (p->nrm[0] / 2) < 0) {
            s32 sp = vlen_v(p->vel), rf[3];
            reflect(rf, p->vel, p->nrm);
            for (int k = 0; k < 3; k++) p->vel[k] += rf[k];
            s32 l = vlen_v(p->vel);
            if (l) for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * sp / l;
        }
        p->vel[1] -= g;
    }
    int a = (p->stance - p->heading) & 0x7ff;
    s32 slope_z = (-SIN(a) * p->nrm[0] + COS(a) * p->nrm[2]) / 16384;
    s32 cur = vlen_v(p->vel), sp = cur;
    p->momentum -= ABS(p->turn) * 20 / 256;
    s32 m = p->momentum, mg = m * speed_grip[p->surf] / 256;
    s32 t = (p->st_friction - 1) * sp / p->st_friction;
    t = t * (mg + 0xf00) / 4096;
    if (t < cur) t = (cur * 3 + t) / 4;
    if (slope_z < 0) p->uphill_ctr++; else p->uphill_ctr = -25;   // ~1 s of climbing keeps the minimum speed
    if (p->lip) { s32 v0 = mg * 0x2d6 / 256; if (t < v0) t = (t * 3 + v0) / 4; }
    if (p->uphill_ctr < 1 && p->brake_k == 0) {
        s32 vmin = mg * 0x2d6 / 256 * m / 256;
        if (t < vmin) t = (t * 7 + vmin) / 8;
    } else t = t * 31 / 32;
    s32 vmax = mg * 0x528 / 256;
    if (vmax < t) t = (vmax + t) / 2;
    if (sp) for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * t / sp;   // no cap on climbing speed here
    if (p->jstate != 7) {
        s32 fs = t * 0x4000 / 256;
        s32 fw[3] = { p->mat[0][2] * fs / 16384, p->mat[1][2] * fs / 16384, p->mat[2][2] * fs / 16384 };
        s32 bl = edge_grip[p->surf] * p->st_edge / 256, ib = 256 - bl;
        for (int k = 0; k < 3; k++) p->vel[k] = (bl * fw[k] + p->vel[k] * ib) / 256;
    }
}

static void wall_bounce(Phys *p, s32 n[3])                         // 0x4f2c8
{
    if (p->wall_ms < 1000) {
        if (++p->wall_hits > 20) { p->wall_ms = 1000; p->wall_hits = 0; p->lost = 99; return; }   // stuck: respawn next step
    } else p->wall_hits = 0;
    p->wall_ms = 0;
    s32 d = (n[2] / 2) * (p->vel[2] / 2) + (n[1] / 2) * (p->vel[1] / 2) + (n[0] / 2) * (p->vel[0] / 2);
    if (d >= 0) return;
    s32 rf[3]; reflect(rf, p->vel, n);
    for (int k = 0; k < 3; k++) p->vel[k] = rf[k] * 5 / 8;
    u32 l = vlen_v(p->vel) | 1;
    if ((int)l > 400) for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * 400 / (s32)l;
    p->vel[0] += n[0] / 2;
    p->vel[1] = n[1] / 2 - 0x47a9;
    p->vel[2] += n[2] / 2;
    int dh = sx_angdiff(p->stance + p->heading, sx_atan2(p->vel[0] << 5, p->vel[2] << 5));
    if (ABS(dh) >= 0x240 && p->kmh > 50) wipeout(p);              // head-on at speed
    else if (p->jstate != 7) anim_play(p, A_BUMP, true, false);
    snd(p, S_COLLIDE);
    p->turn = dh * 2;
    p->heading = (p->heading + p->turn / 4) & 0x7ff;
    p->wall_bounces++;
}

static void deflect(Phys *p, s32 n[3])                             // 0x4f62c (glancing hit on non-wall geometry)
{
    s32 d = ((p->vel[2] / 2) * (n[2] / 2) + (p->vel[1] / 2) * (n[1] / 2) + (p->vel[0] / 2) * (n[0] / 2));
    s32 q = p->speed ? d / (p->speed * p->speed) : 0;
    if (!p->air && (q < 0 ? -q : q) <= 0x3f) return;
    s32 sp = vlen_v(p->vel);
    if (d < 0) {
        s32 rf[3]; reflect(rf, p->vel, n);
        for (int k = 0; k < 3; k++) p->vel[k] += n[k] * sp * 10 / 16384 + rf[k];
        int dh = sx_angdiff(p->stance + p->heading, sx_atan2(p->vel[0] << 5, p->vel[2] << 5));
        p->heading = (p->heading + dh) & 0x7ff;
        p->lean = p->lean * 3 / 4;
    } else {
        for (int k = 0; k < 3; k++) p->vel[k] += n[k] * sp / 16384;
    }
    u32 l = vlen_v(p->vel) | 1;
    for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * sp / (s32)l;
    if (p->up[1] < -6000 && n[1] > 6000) wipeout(p);   // upside down into the ground
}

static void obstacles(Phys *p)                                     // 0x4e8f4: trees + forward wall ray
{
    if (p->wall_ms < 2000) p->wall_ms += PHYS_TICK_MS;
    s32 step[3], nxt[3], prv[3];
    for (int k = 0; k < 3; k++) {
        step[k] = 10 * p->vel[k] * 256 / 16384;
        nxt[k] = p->pos[k] + step[k] * 3 / 2;
        prv[k] = p->pos[k] - step[k];
    }
    if (p->tree_ms > 0) p->tree_ms -= PHYS_TICK_MS; else p->tree_ms = 0;
    const Track *t = p->trk;
    if (t->ntrees && p->tree_ms == 0) {
        s32 x = nxt[0] / 256, y = nxt[1] / 256, z = nxt[2] / 256;
        for (int i = 0; i < t->ntrees; i++) {
            const TreePos *q = &t->trees[i];
            if (y < q->y + 0x180 && q->y < y) {
                s32 dz = q->z - z, dx = q->x - x;
                if (dz > -128 && dz < 128 && dx > -128 && dx < 128 && dz * dz + dx * dx < 0x4000) {
                    // push sideways (cross of up and velocity) away from the trunk, -1/64 speed
                    s32 sx = 0x4000 * p->vel[2] / 16384, sz = -0x4000 * p->vel[0] / 16384;
                    s32 tx = q->x - p->pos[0] / 256, tz = q->z - p->pos[2] / 256;
                    bool left = tx * sx + tz * sz > 0;
                    if (left) { p->vel[0] -= sx; p->vel[2] -= sz; } else { p->vel[0] += sx; p->vel[2] += sz; }
                    p->vel[0] = p->vel[0] * 63 / 64; p->vel[2] = p->vel[2] * 63 / 64;
                    p->heading = (sx_atan2(p->vel[0] << 5, p->vel[2] << 5) - p->stance) & 0x7ff;
                    p->land_heading = p->heading;
                    p->tree_ms = 2000;
                    p->tree_hits++;
                    snd(p, S_COLLIDE);
                    break;
                }
            }
        }
    }
    prv[1] += 0x2800; nxt[1] += 0x2800;
    s32 hit[3], n[3]; int sf;
    if (ray24(p, prv, nxt, hit, n, &sf) && p->rail_cd / 2 < 2) {
        if (SURF_WALL(sf)) wall_bounce(p, n);
        else if (!p->pipe) deflect(p, n);                          // not in the superpipe (0x4e8f4)
    }
}

static void switch_check(Phys *p)                                  // 0x51ce4 / 0x51698: riding backwards -> switch stance
{
    int c = p->sw_ctr++;
    int at = p->turn < 0 ? -p->turn : p->turn;
    if (c > 14 && at < 0x81) {
        if (p->kmh < 5) {
            if (p->uphill_ctr != 0) { p->sw_ctr = 0; p->stance = 0x400 - p->stance; if (!p->air) p->tol = 0x3000; p->vel[1] = -0x1000; }
        } else if (p->travel >= 0) {
            int d = sx_angdiff(p->travel, p->stance + p->heading);
            if ((d < 0 ? -d : d) > 0x200) {
                p->sw_ctr = 0; p->stance = 0x400 - p->stance;
                if (!p->air) { p->tol = 0x3000; if (p->jstate != 7) anim_play(p, A_SWITCH, true, false); }
            }
        }
    }
}

static void speed_update(Phys *p)                                  // 0x3c7f0
{
    int prev = p->speed;
    p->speed = vlen_v(p->vel);
    p->kmh = (p->speed * 0xb8920 / 8192) / 1000;
    if (p->kmh > 0x78) p->kmh = 0x78;
    p->speed_avg = (p->speed_avg * 3 - prev + p->speed) / 4;
}

static void track_progress(Phys *p)                                // nearest race-line point (for streaming / respawn)
{
    const Track *t = p->trk; const RacePoint *r = t->race; int n = t->h.nraceline;
    s32 x = p->pos[0] / 256, y = p->pos[1] / 256, z = p->pos[2] / 256;
    int i0 = p->seg_hint - 40, i1 = p->seg_hint + 40;
    if (i0 < 0) i0 = 0; if (i1 > n - 1) i1 = n - 1;
    s64 best = -1; int bi = p->seg_hint;
    for (int i = i0; i <= i1; i++) {
        s64 dx = r[i].x - x, dy = r[i].y - y, dz = r[i].z - z, d = dx * dx + dy * dy + dz * dz;
        if (best < 0 || d < best) { best = d; bi = i; }
    }
    p->seg_hint = bi;
    { s32 v[3] = { p->pos[0] >> 8, p->pos[1] >> 8, p->pos[2] >> 8 }; track_set_view((Track *)t, v, p->travel >= 0 ? p->travel : p->heading); }
    // project onto the neighbouring race-line segment for a continuous arclen
    int j = bi + 1 < n ? bi : bi - 1;
    float ax = r[j].x, ay = r[j].y, az = r[j].z, bx = r[j + 1].x - ax, by = r[j + 1].y - ay, bz = r[j + 1].z - az;
    float l2 = bx * bx + by * by + bz * bz, k = l2 > 0 ? ((x - ax) * bx + (y - ay) * by + (z - az) * bz) / l2 : 0;
    if (k < -1) k = -1; if (k > 2) k = 2;
    p->s = r[j].arclen + k * (r[j + 1].arclen - r[j].arclen);
    if (p->s < 0) p->s = 0;
}


//[...][...]------------ spawn (0x51f48, simplified search)
void phys_spawn(Phys *p, const Track *t, float s)
{
    bool same = p->trk == t;
    int keep_resp = same ? p->respawns + 1 : 0, keep_wb = same ? p->wall_bounces : 0, keep_th = same ? p->tree_hits : 0;
    int boost_m = same ? p->boost : 0, level = same ? p->boost_level : 0, score = same ? p->score : 0;
    int crashes = same ? p->crashes : 0, ch = p->character, rails = same ? p->rails_ridden : 0, letters = same ? p->letters : 0;
    u8 attr[7]; memcpy(attr, p->attr, 7);
    int picks = same ? p->picks_taken : 0, fin_ms = same ? p->fin_ms : 0, react = same ? p->react : -1;
    bool fin = same && p->finished, over = same && p->race_over;
    memset(p, 0, sizeof(*p));
    p->picks_taken = picks; p->finished = fin; p->fin_ms = fin_ms; p->react = react; p->race_over = over;
    p->trick_k = 0x100; p->mult = 1; p->combo_lvl = 1; p->pipe = t->npipe > 0; p->lipk = 0x200;
    p->loop_snd = -1;
    if (same) snd(p, S_RESPAWN);
    p->trk = t; p->respawns = keep_resp; p->wall_bounces = keep_wb; p->tree_hits = keep_th; p->boost = boost_m;
    p->boost_level = level; p->boost_full = level > 0; p->score = score; p->crashes = crashes; p->character = ch;
    p->rails_ridden = rails; p->letters = letters;
    // natural stance (0x3af34: 0x66f = 0x400 for Moby, Psymon, Elise and Alegra, the goofy riders; 0x51f48 starts
    // every run in it): heading + stance = the direction of travel, and a non-zero stance draws the rider mirrored
    p->natural = (ch == 1 || ch == 2 || ch == 5 || ch == 8) ? 0x400 : 0;
    p->stance = p->natural;
    p->st_glide = 0; p->st_tuck = 0x1e; p->st_turn_tuck = 0x5dc; p->st_turn = 3000;
    p->st_spin_fast = 0xbe8; p->st_settle = 0x800; p->st_spin = 0x6d6; p->st_settle_slow = 0x100;
    p->st_edge = 0x80; p->st_boost = 0x1e; p->st_friction = 32;
    p->st_grab = 0x32 + 10; p->st_tough = 0x32; p->anim_rate = 256;
    // the rider's attributes (0x644d4 via 0x64414: value * K / 100 - K / 2, so 50 = no change). Speed -> glide
    // (K 32), Spin -> the three spin rates (K 512, less 256: only a maxed Spin reaches the base rates), Tricks ->
    // grab rate (K 45), Toughness (K 50). Edging -> 0x198c (K 1024) is never read; Acceleration and Stability are
    // bought and shown but this build reads neither. attr[] all 0 (host tests) = the base values.
    memcpy(p->attr, attr, 7);
    if (attr[2]) {
        #define MOD(a, k) ((int)(a) * (k) / 100 - (k) / 2)
        p->st_glide += MOD(attr[2], 32);
        int sp = MOD(attr[3], 512) - 256;
        p->st_spin_fast += sp; p->st_settle += sp; p->st_spin += sp;
        p->st_grab += MOD(attr[6], 45);
        p->st_tough += MOD(attr[5], 50);
        #undef MOD
    }
    p->momentum = 0x100; p->land_ok = true; p->last_trick = -1; p->bal_limit = 0x2000;
    p->held_up = true;                                             // 0x400 = 1 at a respawn
    const RacePoint *r = t->race; int n = t->h.nraceline;
    int i = 0;
    while (i < n - 2 && r[i].arclen < s) i++;
    // walk forward until the ground under the race line is a normal surface
    for (int tries = 0; tries < 64 && i < n - 2; tries++, i++) {
        s32 a[3] = { r[i].x, r[i].y + 2000, r[i].z }, b[3] = { r[i].x, r[i].y - 4000, r[i].z }, h[3], nn[3]; int sf;
        if (track_ray((Track *)t, a, b, h, nn, &sf) && !SURF_WALL(sf) && !SURF_LOST(sf)) {
            p->pos[0] = r[i].x << 8; p->pos[1] = (h[1] << 8) + 0x6400; p->pos[2] = r[i].z << 8;
            break;
        }
        p->pos[0] = r[i].x << 8; p->pos[1] = (r[i].y << 8) + 0x6400; p->pos[2] = r[i].z << 8;
    }
    p->seg_hint = i;
    int j = i + 4 < n ? i + 4 : n - 1;
    p->heading = sx_atan2((r[j].x - r[i].x) << 5, (r[j].z - r[i].z) << 5);
    p->facing = p->heading; p->travel = p->heading; p->land_heading = p->heading;
    p->heading = (p->heading - p->stance) & 0x7ff;
    p->air = true; p->jstate = 4; p->uphill_ctr = -75;
    p->vel[0] = SIN(p->travel); p->vel[1] = 0; p->vel[2] = COS(p->travel);
    p->up[0] = 0; p->up[1] = 0x4000; p->up[2] = 0;
    p->nrm[1] = 0x4000;
    p->air_anim = A_AIR1;
    anim_play(p, A_AIR1, false, false);
    build_matrix(p, p->up, (p->heading + p->stance) & 0x7ff);
    track_progress(p);
}

// race start (0x66268): rider i at spawn marker i, on the ground, at rest, facing down the race line
void phys_place(Phys *p, const Track *t, int spawn)
{
    p->trk = NULL;
    phys_spawn(p, t, 0);
    for (int i = 0; i < t->npicks; i++) ((Track *)t)->picks[i].taken = 0;
    if (t->nspawn == 0) return;
    const s32 *sp = t->spawn[spawn % t->nspawn];
    s32 a[3] = { sp[0], sp[1] + 400, sp[2] }, b[3] = { sp[0], sp[1] - 2000, sp[2] }, h[3], n[3]; int sf;
    p->pos[0] = sp[0] << 8; p->pos[1] = sp[1] << 8; p->pos[2] = sp[2] << 8;
    if (track_ray((Track *)t, a, b, h, n, &sf)) {
        memcpy(p->contact, h, 12); for (int k = 0; k < 3; k++) p->contact[k] <<= 8;
        memcpy(p->nrm, n, 12); p->surf = sf;
        p->pos[1] = (h[1] << 8) + 0x600;
        p->air = false; p->jstate = 0; p->tol = 0x600;
        memcpy(p->up, n, 12);
    }
    p->vel[0] = p->vel[1] = p->vel[2] = 0;
    p->seg_hint = 0; p->s = 0;
    track_progress(p);
    // face along the race line from the nearest point
    const RacePoint *r = t->race; int i = p->seg_hint, nn = t->h.nraceline, j = i + 4 < nn ? i + 4 : nn - 1;
    p->heading = sx_atan2((r[j].x - r[i].x) << 5, (r[j].z - r[i].z) << 5);
    p->facing = p->heading; p->travel = -1; p->land_heading = 0;
    p->heading = (p->heading - p->stance) & 0x7ff;
    build_matrix(p, p->up, (p->heading + p->stance) & 0x7ff);
    p->hold = true;
    anim_play(p, A_GATE, true, false); p->anim_ms = p->anim_len / 2;   // 0x72758: from half way, held at the end
}

void phys_hold(Phys *p, int anim)
{
    // 0x72758: the gate clip starts half way through and holds at its end (0x181a) instead of looping
    if (p->anim != anim) { anim_play(p, anim, true, false); p->anim_ms = p->anim_len / 2; }
    anim_tick(p);
    p->loop_snd = -1;
}

// pickups (0x4ef68 -> 0x4f20c): within 160 units vertically and horizontally of the marker
static void pickups(Phys *p)
{
    Track *t = (Track *)p->trk;
    s32 x = p->pos[0] >> 8, y = p->pos[1] >> 8, z = p->pos[2] >> 8;
    for (int i = 0; i < t->npicks; i++) {
        Pickup *q = &t->picks[i];
        if (q->taken) continue;
        if (y <= q->pos[1] - 0xa0 || y >= q->pos[1] + 0xa0) continue;
        s32 dx = x - q->pos[0], dz = z - q->pos[2];
        if (dx * dx + dz * dz >= 0x6400) continue;
        q->taken = 1; p->picks_taken++;
        snd(p, S_PICKUP);
        static const u8 MULT[6] = { 0, 0, 2, 3, 5, 10 };
        if (q->type == 0) { p->speed_ms = 5000; p->uphill_ctr = -75; }
        else if (q->type == 1) p->trick_ms = 5000;
        else if (p->air || p->rail_state) {                        // 0x4f120: multipliers add up
            if (p->mult > 1) p->mult += MULT[q->type]; else p->mult = MULT[q->type];
        }
    }
}

static void pickup_timers(Phys *p)                                 // 0x4f170
{
    if (p->speed_ms) {
        p->speed_ms -= DT;
        if (p->speed_ms < 0) p->speed_ms = 0;
        else p->momentum = p->momentum * 0x13b / 256;
    }
    if (p->trick_ms == 0) { p->trick_k = 0x100; return; }
    p->trick_k = 0x180;
    p->trick_ms -= DT;
    if (p->trick_ms < 1) p->trick_ms = 0;
}

// finish line (0x4bc4c): the rider crossed to the far side of the line finish_left -> finish_right
static bool finish_check(Phys *p)
{
    const Track *t = p->trk;
    if (!t->has_finish || p->s < t->length * 0.6f) return false;   // the line is infinite: only near the end
    s32 ax = (p->pos[0] >> 8) - t->fin_l[0], az = (p->pos[2] >> 8) - t->fin_l[2];
    s32 bx = t->fin_r[0] - t->fin_l[0], bz = t->fin_r[2] - t->fin_l[2];
    s32 a[3] = { ax, 0, az }, b[3] = { bx, t->fin_r[1] - t->fin_l[1], bz };
    vnorm(a, 0x100); vnorm(b, 0x100);
    s32 mx = (t->fin_l[0] + t->fin_r[0]) / 2 - (p->pos[0] >> 8), mz = (t->fin_l[2] + t->fin_r[2]) / 2 - (p->pos[2] >> 8);
    s64 half = (s64)isqrt64((s64)bx * bx + (s64)bz * bz) / 2 + 3000;
    if ((s64)mx * mx + (s64)mz * mz > half * half) return false;
    return (b[2] * a[0] - a[2] * b[0]) >> 8 < 0;
}

//[...][...]------------ one step (0x48c34 -> 0x489a8 -> 0x4a60c)
static void ground_branch(Phys *p, bool was_air)
{
    if (was_air) {                                                 // landing
        p->air = false;
        if (p->jstate != 7) p->jstate = 5;
        if (!p->land_ok) {                                         // mid-trick: wipeout (0x3f748)
            wipeout(p);
            p->sw_ctr = 999;
            switch_check(p);
            p->spin_fix = 0;
            return;
        }
        p->bad_landing = p->miss > 14 || SURF_LOST(p->surf);
        for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * 15 / 16;
        if (p->jstate == 5) anim_play(p, p->vel[1] < -0xa735 ? A_LAND_HARD : A_LAND, true, false);
        snd(p, p->surf == 5 ? S_ICE_LAND : p->surf == 4 ? S_POWDER_LAND : S_PACKED_LAND);
        p->sw_ctr = 999;
        if (p->uphill_ctr > 0) p->uphill_ctr = 0;
    }
    if (p->tol > 0x600) p->tol -= 0x150;
    switch_check(p);
    for (int k = 0; k < 3; k++) p->pos[k] = (p->pos[k] * 3 + p->contact[k]) / 4;
    if (p->jstate == 7) {                                          // sliding after a wipeout
        if (!p->slow) for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * 15 / 16;
        else for (int k = 0; k < 3; k++) p->vel[k] = (p->vel[k] << 3) / 9;
        s32 x = p->vel[0] * 256 / 16384, y = p->vel[1] * 256 / 16384, z = p->vel[2] * 256 / 16384;
        s32 v2 = x * x + y * y + z * z;
        if (v2 < 0x3a000) p->slow = true;
        if (v2 < 0x19258) { p->sw_ctr = 0; p->can_rise = true; }
    }
    if (p->land_heading) {                                         // after landing, swing the heading back to travel
        int d = sx_angdiff(p->heading, p->land_heading), ad = d < 0 ? -d : d;
        if (ad > 0x200) { ad = 0x400 - ad; p->land_heading += 0x400; }
        p->heading = sx_ease(p->heading, p->land_heading);
        if (ad < 0x40) p->land_heading = 0;
    }
    if (!p->pipe && p->kmh > 1 && p->kmh < 0x50) {                 // turn toward the fall line at low speed
        int fall = sx_atan2(p->nrm[0] << 5, p->nrm[2] << 5);
        int d = sx_angdiff(p->stance + p->heading, fall);
        if ((d < 0 ? -d : d) < 0x200) p->turn += d / 16;
    }
    s32 mx = (0x380 - p->speed / 4) * turn_max_grip[p->surf] / 256;
    if (p->turn < -mx) p->turn = -mx; else if (p->turn > mx) p->turn = mx;
    s32 sc = p->speed < 0x80 ? 0x80 : p->speed > 0x100 ? 0x100 : p->speed;
    s32 dh = p->turn * (DT8 * sc / 256) / 256;
    p->heading += dh;
    if (p->land_heading) p->land_heading += dh;
    p->heading &= 0x7ff;
    if (!(p->buttons & (PB_LEFT | PB_RIGHT))) p->turn = (32 - DT8) * p->turn / 32;
    s32 ln = (p->kmh + 0x18) * p->turn * -3 / 1024;
    if (ln < -0x1c2) ln = -0x1c2; else if (ln > 0x1c2) ln = 0x1c2;
    p->lean = (ln + p->lean) / 2;
    orient_ground(p);
    if (p->pipe) {                                                 // 0x4bd84: pipe forces, walls push off by n/4
        pipe_forces(p);
        if (SURF_WALL(p->surf)) for (int k = 0; k < 3; k++) p->vel[k] += p->nrm[k] / 4;
        if (!p->wall_ride) obstacles(p);
    } else {
        ground_forces(p);
        obstacles(p);
        if (p->nrm[1] < 0x1000 || SURF_WALL(p->surf))              // too steep / wall: push off it
            for (int k = 0; k < 3; k++) p->vel[k] += p->nrm[k] / 2;
    }
    p->facing = (p->stance + p->heading) & 0x7ff;
    p->pitch = 0;
}

static void air_branch(Phys *p, bool was_air)
{
    if (!was_air) {                                                // left the ground
        p->air_ticks = 0; p->air = true; p->air_ms = 0;
        p->land_heading = p->travel | 1;
        p->sw_ctr = 999;
        p->pitch = 0;
    }
    p->air_ticks++; p->air_ms += DT;
    if (p->tol < 0x3000) p->tol += 0x150;
    p->facing = p->travel;
    obstacles(p);
    for (int k = 0; k < 3; k++) p->vel[k] = p->vel[k] * 0x3f3 / 1024;
    if (p->spin_fix) {                                             // heading correction after a rail
        int st = sx_ease(0, p->spin_fix);
        if (st > 0x400) st -= 0x800;
        p->heading = (p->heading + st) & 0x7ff;
        p->spin_fix -= st;
        if (ABS(p->spin_fix) < 100) p->spin_fix = 0;
    }
    if (p->flip_step == 0) p->pitch = sx_ease(p->pitch, 0);       // flip rotation
    else p->pitch += p->flip_step;
    p->pitch &= 0x7ff;
    p->heading = (p->heading + p->spin_step) & 0x7ff;              // spin rotation
    if (!p->pipe && p->spin_step == 0 && p->flip_step == 0 && p->kmh > 0x20 && p->air_ms > 300 && p->travel >= 0) {   // not in the pipe (0x4bd84 air)
        int d = sx_angdiff(p->travel, p->heading), ad = d < 0 ? -d : d;   // line the board up with the flight path
        p->heading = ad < 0x200 ? sx_ease(p->heading, p->travel) : sx_ease(p->heading, p->travel + 0x400);
    }
    orient_air(p);
    p->spin_step = 0; p->flip_step = 0;
    p->turn = (64 - DT8) * p->turn / 64;
}

// superpipe lip lines (0x4a3cc): rider beyond a pipe%i_l -> pipe%i_r line (outside, between its ends, within 256
// units of it). n = line normal into the pipe (0x4000), c = nearest point on the line (24.8, y interpolated).
static bool pipe_line(const Phys *p, s32 n[3], s32 c[3])
{
    const Track *t = p->trk;
    s32 x = p->pos[0] >> 8, z = p->pos[2] >> 8;
    for (int i = 0; i < t->npipe; i++) {
        const s32 *a = t->pipe[i][0], *b = t->pipe[i][1];
        s32 bx = b[0] - a[0], bz = b[2] - a[2];
        s64 cr = (s64)(x - a[0]) * bz - (s64)(z - a[2]) * bx;
        if (cr >= 0) continue;
        s64 da = (s64)(x - a[0]) * bx + (s64)(z - a[2]) * bz;
        if (da <= 0 || (s64)(x - b[0]) * bx + (s64)(z - b[2]) * bz >= 0) continue;
        s64 l2 = (s64)bx * bx + (s64)bz * bz;
        s32 cp[3];
        for (int k = 0; k < 3; k++) cp[k] = (s32)((((s64)a[k] << 8) * l2 + (((s64)(b[k] - a[k]) << 8) * da)) / l2);
        s32 ex = (p->pos[0] - cp[0]) >> 8, ez = (p->pos[2] - cp[2]) >> 8;
        if (ex * ex + ez * ez > 0x10000) continue;
        n[0] = bz; n[1] = 0; n[2] = -bx;
        vnorm(n, 0x4000);
        memcpy(c, cp, 12);
        return true;
    }
    return false;
}

static int ease_k(int a, int b, int k) { return (a + sx_angdiff(a, b) / k) & 0x7ff; }   // 0x588bc

// the lip block of 0x4bd84: past the lip line the rider is held on the line's vertical plane - riding up the
// vertical wall below the lip, in the air above it, turning back into the pipe
static void pipe_lip(Phys *p)
{
    p->wall_ride = false;
    p->lipk -= 4; if (p->lipk < 0x200) p->lipk = 0x200;
    if (p->jstate == 7) { p->lipk = 0x200; p->lip = 0; return; }
    s32 n[3], cp[3];
    if (!pipe_line(p, n, cp)) {
        if (p->lip) {
            p->lip--;
            if (p->lip_y - 0x6000 < p->pos[1]) {
                memcpy(p->nrm, p->lip_n, 12);
                for (int k = 0; k < 3; k++) p->up[k] = (p->up[k] * 7 + p->lip_n[k]) / 8;
            }
        }
        p->momentum += p->lipk >> 11;
        return;
    }
    p->pos[0] = cp[0]; p->pos[2] = cp[2];
    memcpy(p->lip_n, n, 12); p->lip_n[1] = 0x800;
    if (p->lipk < 0x1000) p->lipk += 9;
    p->lip_y = cp[1];
    memcpy(p->nrm, p->lip_n, 12);
    p->tol = 0;
    if (p->pos[1] < cp[1]) {                                       // below the lip: on the vertical wall
        if (p->vel[1] < 1) { p->vel[0] += n[0] / 8; p->vel[2] += n[2] / 8; }
        else {
            p->pos[0] = p->contact[0]; p->pos[2] = p->contact[2];
            p->vel[0] = p->vel[0] * 31 / 32; p->vel[2] = p->vel[2] * 31 / 32;
        }
        p->wall_ride = p->vel[1] > 0;
        for (int k = 0; k < 3; k++) p->up[k] = (p->up[k] * 7 + p->lip_n[k]) / 8;
    } else {                                                       // above it: air, swing round to face back in
        p->t_state = 99;
        p->land_heading = 0;                                       // land facing where the lip turn left the board
        if (!p->air) p->lip_head = p->heading;
        if (p->spin_step == 0) {
            int t = (0x400 - p->lip_head) & 0x7ff;
            int d1 = ABS(sx_angdiff(p->heading, -p->lip_head & 0x7ff)), d2 = ABS(sx_angdiff(p->heading + 0x400, -p->lip_head & 0x7ff));
            if (d1 < d2) p->heading = (ease_k(p->heading + 0x400, t, 16) - 0x400) & 0x7ff;
            else p->heading = ease_k(p->heading, t, 16);
        }
    }
    p->lip = 4;
}

static void step_body(Phys *p, u32 buttons)
{
    p->pressed = (p->buttons ^ buttons) & buttons;                 // 0x48bec
    p->buttons = buttons;
    if (p->t_state < 500) p->t_state += DT;
    boost_meter_step(p);                                           // 0x4099c
    trick_points_step(p);                                          // 0x484ac
    combo_step(p);                                                 // 0x4341c
    if (p->rail_state) rail_input(p);                              // 0x489a8 dispatch
    else if (!p->air) ground_input(p);
    else air_tricks(p);
    if (!p->air && !p->rail_state) ground_anim(p);
    anim_tick(p);                                                  // 0x3bcf4

    // ---- 0x4a60c
    s32 old[3]; memcpy(old, p->pos, 12);
    travel_update(p);
    p->pos[0] += DT8 * p->vel[0] * 256 / 16384;
    if (!p->rail_state) p->pos[1] += DT8 * p->vel[1] * 256 / 16384;
    p->pos[2] += DT8 * p->vel[2] * 256 / 16384;
    bool lost;
    if (!p->air) { probe_ground(p); lost = !p->rail_state && (p->miss >= 15 || SURF_LOST(p->surf)); }
    else { probe_air(p); lost = p->miss >= 15; }
    {
        s32 dx = (p->pos[0] - p->contact[0]) / 16, dz = (p->pos[2] - p->contact[2]) / 16, dy = (p->pos[1] - p->contact[1]) / 16;
        p->height = isqrt64((s64)dx * dx + (s64)dz * dz + (s64)dy * dy) << 4;
    }
    if (lost || p->lost >= 99) { if (++p->lost > 3) { phys_spawn(p, p->trk, p->s); return; } }
    else p->lost = 0;
    if (p->pipe) pipe_lip(p);
    p->vel[1] -= GRAVITY;
    if (p->jump && !p->air) {                                      // jump impulse
        s32 imp = p->charge * 4000 / 1000 + 20000;
        if (!p->rail_state || p->vel[1] < 0xbb9) {
            for (int k = 0; k < 3; k++) p->vel[k] += p->up[k] * imp / 16384;
        } else {                                                   // off a rail: along the rail's normal
            s32 n[3]; rail_normal(p, n);
            p->vel[0] = n[0] * imp / 16384 - p->vel[0] / 2;
            p->vel[1] = n[1] * imp / 16384;
            p->vel[2] = n[2] * imp / 16384 - p->vel[2] / 2;
        }
        if (p->rail_state) rail_leave(p);
        p->charge = 0; p->tol = 0; p->jump = false;
    }
    if (p->rail_cd) p->rail_cd--;
    if (!p->rail_state && p->land_ok && p->jstate != 7 && !p->jump) rail_acquire(p);
    if (!p->finished) {
        if (finish_check(p)) { p->finished = true; p->fin_ms = 0; }
        pickups(p);
        pickup_timers(p);
    }
    if (p->momentum) p->momentum += p->st_glide;

    if (p->rail_state) {
        rail_step(p, old);
    } else {
        bool was_air = p->air;
        s32 tol;
        if (!was_air) {
            if (p->tol == 0) tol = 0;
            else {
                tol = p->tol + ABS(p->turn) * 0x18;
                if (p->vel[1] < 0) tol -= p->vel[1] / 4;
            }
        } else tol = 0x600;
        bool ground = false;
        if (tol) {
            s32 q[3];
            for (int k = 0; k < 3; k++) q[k] = p->pos[k] - p->up[k] * tol / 16384 - p->contact[k];
            s32 dn = p->nrm[2] * (q[2] / 256) + p->nrm[1] * (q[1] / 256) + p->nrm[0] * (q[0] / 256);
            s32 nu = p->nrm[0] * p->up[0] + p->nrm[1] * p->up[1] + p->nrm[2] * p->up[2];
            static const u8 land_after_rail[12] = { 0, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0 };   // 0xa6a30
            if ((dn < 1 || p->height <= tol) && p->height < 0xa801 && nu >= 0 &&
                (p->rail_cd == 0 || land_after_rail[p->surf])) ground = true;
        }
        if (p->pipe) ground = p->wall_ride || (ground && !p->lip);    // 0x4bd84: at the lip = in the air
        if (ground) ground_branch(p, was_air);
        else air_branch(p, was_air);
    }
    p->momentum = 0x100;
    speed_update(p);
    track_progress(p);
    // rider loop sound (0x52d00): wipeout slide, carve by surface, grind, wind
    if (p->jstate == 7 && !p->air) p->loop_snd = S_WIPEOUT2;
    else if (p->rail_state) p->loop_snd = S_RAIL_LOOP;
    else if (p->air) p->loop_snd = S_WIND;
    else if (p->surf == 4) p->loop_snd = S_POWDER_CARVE;
    else if (p->surf == 5) p->loop_snd = S_ICE_CARVE;
    else if (p->surf == 1 || p->loop_snd < 0 || p->loop_snd == S_WIND || p->loop_snd == S_RAIL_LOOP || p->loop_snd == S_WIPEOUT2)
        p->loop_snd = S_PACKED_CARVE;
}

// after the finish (0x494a0): no input, brake to a stop, then the celebration clip; over after 5 s
static void finish_step(Phys *p)
{
    p->fin_ms += DT;
    if (!p->air && p->fin_ms > 500 && (p->fin_ms > 2000 || p->kmh < 20)) {
        // port: the celebration (or the disappointment) plays once, then the rider stands in the default pose (the
        // exe loops it: 0x494a0, 0x181a = 0)
        if (p->anim == A_BRAKE_TS) anim_play(p, p->react >= 0 ? p->react : A_REACT_POS, true, false);
        else if (p->anim >= A_REACT_POS && p->anim < A_REACT_NEG + 8 && p->anim_once && p->anim_done) anim_play(p, A_IDLE_STAND, false, false);
        anim_tick(p);
        if (p->fin_ms > 5000) p->race_over = true;
    } else step_body(p, 0);
    if (!p->air) {
        p->vel[0] = p->vel[0] * 5 / 6; p->vel[2] = p->vel[2] * 5 / 6;
        bool react = p->anim >= A_REACT_POS && p->anim < A_REACT_NEG + 8;   // 0x3bf6c keeps a reaction playing
        if (p->anim != A_BRAKE_TS && !react && p->anim != A_IDLE_STAND) anim_play(p, A_BRAKE_TS, true, false);
    }
    p->loop_snd = p->air ? S_WIND : -1;
}

void phys_step(Phys *p, u32 buttons)
{
    if (p->hold) { p->hold = false; anim_play(p, A_GATE_INTO, true, false); }   // out of the gate
    if (p->finished) { finish_step(p); return; }
    step_body(p, buttons);
}

// SSX 3 GizWipe - grind rails, ported from the Gizmondo ssx_3.exe
//
// Rails are the track's splines other than the race line (built by 0x82854: points in game units, unit
// direction per segment, bounding box). The rider locks on when it passes within 32 units sideways and
// between 9 (air) / 54 (ground) units below and 27 above a segment (0x4e278 / 0x4ddd0), then slides along
// it (rail branch of 0x4a60c, 0x506f4) keeping balance with LEFT/RIGHT (0x40450 / 0x506f4).
#include <string.h>
#include "phys_int.h"

static int rail_angle(const Phys *p)                               // 0x4da58: heading of the current segment
{
    const TrackRail *r = &p->trk->rails[p->rail];
    const s32 *a = r->pts[p->rail_seg], *b = r->pts[p->rail_seg + 1];
    return sx_atan2((b[0] - a[0]) * 32, (b[2] - a[2]) * 32);
}

static int side_of(const s32 a[3], const s32 b[3])                 // 0x4cf88
{
    s32 c = b[2] * a[0] - a[2] * b[0];
    return c / 256 >= 0 ? 1 : -1;
}

static s32 hdist(const s32 a[3], const s32 b[3])                   // 0x4e6c0 (24.8 points, xz only)
{
    s32 x = b[0] - a[0], z = b[2] - a[2];
    if (ABS(x) > 0x4000 || ABS(z) > 0x4000) return 0x7fffffff;
    return isqrt(x * x + z * z);
}

static s32 hdist_u(const s32 a[3], const s32 b[3])                 // 0x4e6c0 on unit coordinates
{
    s32 x = b[0] - a[0], z = b[2] - a[2];
    if (ABS(x) > 0x4000 || ABS(z) > 0x4000) return 0x7fffffff;
    return isqrt(x * x + z * z);
}

static s32 dist3_u(const s32 a[3], const s32 b[3])                 // 0x4e640 on unit coordinates
{
    s32 x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
    if (ABS(x) > 0x8000 || ABS(y) > 0x8000 || ABS(z) > 0x8000) return 0x7fffffff;
    return isqrt(x * x + y * y + z * z);
}

static s32 dist3(const s32 a[3], const s32 b[3])                   // 0x4e640 (24.8)
{
    s32 x = b[0] - a[0], y = b[1] - a[1], z = b[2] - a[2];
    if (ABS(x) > 0x8000 || ABS(y) > 0x8000 || ABS(z) > 0x8000) return 0x7fffffff;
    return isqrt((u32)(x * x) + (u32)(y * y) + (u32)(z * z));
}

// 0x4e830: project the rider (units, xz) on segment a-b; out = closest point in 24.8 with y = 0
static bool proj_xz(const s32 a[3], const s32 b[3], const s32 pu[3], s32 out[3])
{
    s32 l = hdist_u(a, b);                                         // segment length in units
    if (l == 0 || l == 0x7fffffff) return false;
    s32 t = ((pu[0] - a[0]) * (b[0] - a[0]) + (b[2] - a[2]) * (pu[2] - a[2])) * 256 / (l * l);
    out[0] = (b[0] - a[0]) * t + a[0] * 256; out[1] = 0; out[2] = (b[2] - a[2]) * t + a[2] * 256;
    return t >= 0 && t <= 256;
}

// 0x4e734: 3d projection, out in 24.8, sets the projection parameter
static bool proj_3d(Phys *p, const s32 a[3], const s32 b[3], const s32 pu[3], s32 out[3])
{
    s32 l = dist3_u(a, b);
    if (l == 0 || l == 0x7fffffff) l = 1;
    s32 t = ((pu[0] - a[0]) * (b[0] - a[0]) + (b[2] - a[2]) * (pu[2] - a[2]) + (b[1] - a[1]) * (pu[1] - a[1])) * 256 / (l * l);
    out[0] = (b[0] - a[0]) * t + a[0] * 256;
    out[1] = (b[1] - a[1]) * t + a[1] * 256;
    out[2] = (b[2] - a[2]) * t + a[2] * 256;
    p->rail_t = t;
    return t >= 0 && t <= 256;
}

// 0x4ddd0: find the segment of rail ri the rider is on / near; returns closest point (24.8) and segment
static bool rail_find(Phys *p, int ri, s32 hit[3], int *seg)
{
    const TrackRail *r = &p->trk->rails[ri];
    s32 pu[3] = { p->pos[0] / 256, p->pos[1] / 256, p->pos[2] / 256 };
    s32 pos8[3] = { p->pos[0], 0, p->pos[2] };
    int lo = 0, hi = r->n - 1, below, above;
    if (!p->rail_state) {
        below = p->air ? 0x900 : 0x3600; above = 0x1b00;
    } else {
        int s = p->rail_seg;
        if (s > 0) lo = s - 1;
        if ((u32)s < (u32)(r->n - 1)) hi = s + 1;
        if (s > 1) lo = s - 2;
        if ((u32)s < (u32)(r->n - 2)) hi = s + 2;
        below = above = 0x2d00;
    }
    for (int k = lo; k < hi; k++) {
        s32 a[3] = { r->pts[k][0], r->pts[k][1], r->pts[k][2] }, b[3] = { r->pts[k + 1][0], r->pts[k + 1][1], r->pts[k + 1][2] }, q[3];
        if (k == 0 || k == r->n - 2) {                             // moving away from an end: no end extension
            int nb = (p->rail_dir + k == 0) ? 1 : -1;
            int ni = k + nb; if (ni < 0) ni = 0; if (ni > r->n - 1) ni = r->n - 1;
            s32 dx = r->pts[ni][0] - a[0], dz = r->pts[ni][2] - a[2];
            if (dz * p->vel[2] + dx * p->vel[0] > 0) p->rail_ends = false;
        }
        if (!p->rail_ends || p->air) {                             // lengthen the segment at its start by 1/8
            s32 ex = (a[0] - b[0]) * 9 / 8, ez = (a[2] - b[2]) * 9 / 8;
            a[0] = ex + b[0]; a[2] = ez + b[2];
        } else {                                                   // on the ground: end segments reach further
            s32 ex = (a[0] - b[0]) * 3 / 2, ez = (a[2] - b[2]) * 3 / 2;
            if (k == 0) { a[0] = ex + b[0]; a[2] = ez + b[2]; }
            else if (k == r->n - 2) { b[0] = a[0] - ex; b[2] = a[2] - ez; }
        }
        if (!proj_xz(a, b, pu, q)) continue;
        s32 c[3];
        proj_3d(p, a, b, pu, c);
        if (((c[1] - below < p->pos[1] && p->pos[1] < c[1] + above) || p->rail_state) &&
            hdist(pos8, q) < 0x2000 + 0) {
            if (r->n > 1 && k > 0 && k < r->n - 2) p->rail_ends = false;
            p->bal_limit = 0x2000;
            *seg = k;
            memcpy(hit, c, 12);
            return true;
        }
    }
    return false;
}

void rail_normal(Phys *p, s32 out[3])                              // 0x4dc64: up across the rail
{
    s32 *d = p->rail_vec;
    s32 s[3] = { -0x4000 * d[2], 0, 0x4000 * d[0] };              // horizontal side vector
    s[0] /= 16384; s[2] /= 16384;
    vnorm(s, 0x4000);
    out[0] = d[2] * s[1] - d[1] * s[2];
    out[1] = d[0] * s[2] - d[2] * s[0];
    out[2] = d[1] * s[0] - d[0] * s[1];
    for (int k = 0; k < 3; k++) out[k] /= 16384;
    vnorm(out, 0x4000);
    if (out[1] < 0) for (int k = 0; k < 3; k++) out[k] = -out[k];
}

static void rail_anim_set(Phys *p, int set)                        // 0x408e0
{
    p->rail_set = set;
    anim_play(p, set == 2 ? 181 : 186, false, false);             // rsbs_fwd_cyc / rsfs_fwd_cyc
}

static void rail_dir_check(Phys *p)                                // 0x51c48: ride the rail in the travel direction
{
    int d = sx_angdiff(p->travel, p->rail_dir + rail_angle(p));
    if (ABS(d) > 0x200) {
        p->momentum += 0x40;
        p->rail_dir ^= 0x400;
        p->balance = -p->balance;
        rail_anim_set(p, p->rail_set != 2 ? 2 : 3);
    }
}

void rail_acquire(Phys *p)                                         // 0x4e278
{
    const Track *t = p->trk;
    if (!(p->rail_cd == 0 || ABS(p->balance) <= p->bal_limit)) return;
    p->rail_ends = true;
    s32 pu[3] = { p->pos[0] / 256, p->pos[1] / 256, p->pos[2] / 256 };
    for (int i = 0; i < t->nrails; i++) {
        const TrackRail *r = &t->rails[i];
        s32 dx = pu[0] - (r->bbmin[0] + (r->bbmax[0] - r->bbmin[0]) / 2);
        s32 dy = pu[1] - (r->bbmin[1] + (r->bbmax[1] - r->bbmin[1]) / 2);
        s32 dz = pu[2] - (r->bbmin[2] + (r->bbmax[2] - r->bbmin[2]) / 2);
        if (dx > 0x2000 || dy > 0x2000 || dz > 0x2000) continue;
        if (dx < -0x2000 || dy < -0x2000 || dz < -0x2000) continue;
        if (dx * dx + dy * dy + dz * dz > r->r2) continue;
        if (p->rail_cd && i == p->rail) continue;
        s32 hit[3]; int seg;
        if (!rail_find(p, i, hit, &seg)) continue;
        // lock on
        p->rail = i; p->rail_seg = seg; p->rail_state = 1; p->stance = 0;
        p->rails_ridden++;
        snd(p, S_RAIL_LAND);
        memcpy(p->rail_vec, r->dir[seg], 12);
        rail_dir_check(p);
        s32 a[3], b[3];
        const s32 *p0 = r->pts[seg], *p1 = r->pts[seg + 1];
        if (p->rail_dir == 0) {
            for (int k = 0; k < 3; k++) { a[k] = p0[k] - pu[k]; b[k] = p1[k] - p0[k]; }
        } else {
            for (int k = 0; k < 3; k++) { a[k] = p1[k] - pu[k]; b[k] = p0[k] - p1[k]; }
        }
        a[1] = 0; b[1] = 0;
        p->balance = side_of(a, b);
        int d = sx_angdiff(p->heading, rail_angle(p) + p->rail_dir);
        p->slide = d < 1 ? 0x600 : 0x200;
        rail_anim_set(p, (ABS(p->slide) < 0x400 || ABS(p->slide) > 0x800) ? 3 : 2);
        p->trick_kind = 0;
        if (p->air) { p->air = false; }
        return;
    }
}

// 0x4daa8: re-find the segment; rail_vec <- its direction; c = closest point (24.8), hz = horizontal one
static bool rail_follow(Phys *p, s32 c[3], s32 hz[3])
{
    const TrackRail *r = &p->trk->rails[p->rail];
    int seg;
    if (!rail_find(p, p->rail, c, &seg)) { p->rail_state = 0; return false; }
    p->rail_seg = seg;
    memcpy(p->rail_vec, r->dir[seg], 12);
    s32 a[3] = { r->pts[seg][0], 0, r->pts[seg][2] }, b[3] = { r->pts[seg + 1][0], 0, r->pts[seg + 1][2] };
    s32 pu[3] = { p->pos[0] / 256, 0, p->pos[2] / 256 };
    proj_3d(p, a, b, pu, hz);
    s32 n[3]; rail_normal(p, n); memcpy(p->nrm, n, 12);
    return true;
}

static void rail_push(Phys *p)                                     // 0x50668: slope pull along the normal
{
    p->vel[0] += p->nrm[0] * DT8 * 5 / 256;
    p->vel[2] += p->nrm[2] * DT8 * 5 / 256;
    p->travel = sx_atan2(p->vel[0] * 32, p->vel[2] * 32);
}

static void rail_speed(Phys *p)                                    // 0x506f4: speed along the rail + balance
{
    s32 vx = p->vel[0] * 256 / 16384, vz = p->vel[2] * 256 / 16384;
    s32 s = isqrt(vx * vx + vz * vz);
    if (s > 0x300) s = 0x300;
    if (p->momentum > 0x100) { if (s < 0x80) s += 0x40; s = p->momentum * s / 256; }
    int ra = rail_angle(p);
    if (p->rail_state == 2) {
        int d = sx_angdiff(p->rail_dir + ra, p->travel);
        s32 bal = p->balance - ((d / 4) * s) / 0x300;
        s32 k = ((0x300 - s) * 400) / 0x300; if (k < 0xb4) k = 0xb4;
        s32 j = ((0x300 - s) * 0x400) / 0x300; if (j < 0x200) j = 0x200;
        if (bal < 0) k = -k;
        p->balance = p->dpad * j + k + bal;                       // drifts away from the middle; LEFT/RIGHT correct it
    }
    int a = (p->rail_dir + ra) & 0x7ff;
    p->vel[0] = (SIN(a) * s / 16384) * 0x4000 / 256;
    p->vel[2] = (COS(a) * s / 16384) * 0x4000 / 256;
}

static void rail_orient(Phys *p)                                   // 0x4fdf4
{
    for (int k = 0; k < 3; k++) p->up[k] = (p->up[k] * 7 + p->nrm[k]) / 8;
    vnorm(p->up, 0x4000);
    build_matrix(p, p->up, (p->stance + p->heading) & 0x7ff);
    int lean = p->balance / 32;
    if (((p->rail_dir + p->slide) & 0x7ff) > 0x400) lean = -lean;
    // 0x4fdf4 -> 0x5b9a8: the balance is a rotation about the rider's own X (side) axis - the same one the drawing
    // uses for flips (pitch). As a roll about the facing it barely showed: on a boardslide the rider faces across
    // the rail, so it read as leaning toward or away from the camera instead of tipping off the rail.
    p->pitch = lean;
    p->lean = 0;
}

void rail_step(Phys *p, const s32 old[3])                          // rail branch of 0x4a60c
{
    int ra = rail_angle(p);
    p->air = false;
    s32 c[3], hz[3];
    if (!rail_follow(p, c, hz)) { rail_leave(p); return; }         // ran off the end
    ra = rail_angle(p);
    p->facing = (p->facing * 3 + p->rail_dir + ra) / 4;
    if (ABS(p->balance) > p->bal_limit) {                          // lost balance: fall off sideways
        p->rail_state = 0; p->rail_fell = true;
        rail_leave(p);
        return;
    }
    if (p->rail_state == 1) {                                      // locking on
        s32 y0 = p->pos[1];
        p->pos[0] = (p->pos[0] * 3 + hz[0]) / 4;
        s32 ny = (c[1] * 3 + y0) / 4;
        p->pos[1] = ny;
        p->pos[2] = (p->pos[2] * 3 + hz[2]) / 4;
        s32 st = DT8 * p->vel[1] * 256 / 16384;
        p->vel[1] = ((ny - (y0 - st)) + p->vel[1]) / 2;
        s32 d = dist3(p->pos, c);
        if (d < p->bal_limit / 3) { p->rail_state = 2; p->balance *= d; }
    } else {                                                       // grinding: sit on the rail, offset by balance
        s32 y0 = p->pos[1];
        p->pos[1] = c[1];
        p->vel[1] = c[1] - y0;
        s32 off = COS(-ra) * p->balance / 16384, offz = SIN(-ra - 0x400) * p->balance / 16384;
        p->pos[0] = p->rail_dir == 0 ? hz[0] - off : hz[0] + off;
        p->pos[2] = p->rail_dir == 0 ? hz[2] + offz : hz[2] - offz;
    }
    rail_dir_check(p);
    p->stance = 0;
    p->slide = sx_angdiff(p->heading, ra) < 1 ? 0x600 : 0x200;
    p->heading = sx_ease(p->heading, ra - p->slide);
    rail_push(p);
    rail_dir_check(p);
    rail_speed(p);
    rail_orient(p);
    (void)old;
}

void rail_leave(Phys *p)                                           // 0x4d7a0
{
    snd(p, S_RAIL_LEAVE);
    p->rail_state = 0;
    if (p->grab_state == 5) { tricks_land(p); p->grab_state = 0; }
    p->land_heading = p->stance + p->travel;
    if (p->rail_fell) {                                            // push sideways off the rail
        p->rail_fell = false;
        s32 s[3] = { -p->vel[2], 0, p->vel[0] };                  // up x velocity
        vnorm(s, 0x4000);
        if (p->balance < 0) { p->vel[0] -= s[0]; p->vel[2] -= s[2]; }
        else { p->vel[0] += s[0]; p->vel[2] += s[2]; }
    }
    if (!p->jump) p->vel[1] -= GRAVITY;
    p->air = true; p->air_ticks = 0; p->air_ms = 0;
    p->rail_cd = 6; p->tol = 0; p->pitch = 0;
    p->spin_fix = sx_angdiff(p->stance + p->heading, p->travel | 1);
    p->lean = 0;
    anim_play(p, p->air_anim ? p->air_anim : A_AIR1, false, false);
}

void rail_input(Phys *p)                                           // 0x403ac + 0x40450
{
    p->grab_state = 5;
    if (p->jstate == 4 || p->jstate == 5) {                        // landed on the rail: bank the air trick
        tricks_land(p);
        p->grab_state = 5; p->grab_sub = 0; p->jstate = 0;
    }
    if (!p->nm_rail) {                                             // for the trick name (0x4494c: 50-50 / BS / FS Rail)
        p->nm_rail = p->rail_set == 2 ? 2 : p->rail_set == 3 ? 3 : 1;
        if (!p->nm_halves && !p->nm_flips && !p->nm_ngrab) p->nm_switch = p->stance != p->natural;
    }
    if ((p->buttons & PB_LEFT) && p->jstate == 0) p->dpad = 1;
    else if ((p->buttons & PB_RIGHT) && p->jstate == 0) p->dpad = -1;
    else p->dpad = 0;
    boost_input(p);
    if ((p->buttons & PB_JUMP) && p->jstate == 0) { p->jstate = 2; p->momentum += p->st_tuck; }
    if (!(p->buttons & PB_JUMP) && p->jstate == 2) {               // jump off the rail
        tricks_takeoff(p);
        p->grab_state = 0;
        p->trick_done = true;
        travel_update(p);
        p->land_heading = p->travel | 1;
    }
    // balance animation (0x40580): lean clip when past 3/4 of the limit
    int set = p->rail_set == 2 ? 0 : 5;
    int want = 181 + set;
    if (p->balance > p->bal_limit * 3 / 4) want = 183 + set;
    else if (p->balance < -p->bal_limit * 3 / 4) want = 185 + set;
    if (p->anim != want) anim_play(p, want, false, false);
}

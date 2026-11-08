// host simulation: load a .ssxt, spawn, run scripted input, print trajectory
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sxtab.h"
#define SINF(a, d) ((s32)((s64)sx_sin[(a) & 0x7ff] * (d) / 16384))
#define COSF(a, d) ((s32)((s64)sx_sin[((a) + 0x200) & 0x7ff] * (d) / 16384))
#include "physics.h"
#include "bot.h"
#include "ghost.h"
#include "anim_ids.h"
#include <string.h>

static void load_anim_lengths(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) { printf("no %s\n", path); return; }
    u32 h[4]; fread(h, 4, 4, f);
    static u16 ms[NANIM_IDS];
    for (u32 i = 0; i < h[3]; i++) {
        struct { char name[24]; u16 nf; u8 ki, z; u16 nk, len; u32 off; } __attribute__((packed)) c;
        fseek(f, 48 + i * 36, SEEK_SET); fread(&c, 36, 1, f);
        for (int id = 0; id < NANIM_IDS; id++) if (ANIM_NAME[id][0] && !strncmp(ANIM_NAME[id], c.name, 24)) ms[id] = c.len * 1000 / 30;
    }
    fclose(f); phys_set_anim_lengths(ms, NANIM_IDS);
}
int main(int argc, char **argv)
{
    Track t; if (!track_load(&t, argv[1])) return 1;
    float s0 = argc > 2 ? atof(argv[2]) : 600;
    const char *script = argc > 3 ? argv[3] : "";      // e.g. "0-100:,100-150:R,150-160:J"
    int secs = argc > 4 ? atoi(argv[4]) : 30;
    static Phys p; memset(&p, 0, sizeof p);
    load_anim_lengths(getenv("SSXA") ? getenv("SSXA") : "../nitrofs/anims.ssxa");   // (run from ds_runtime/hosttest)
    if (getenv("CHAR")) p.character = atoi(getenv("CHAR"));
    if (getenv("ATTR")) { int a[7] = {0}; sscanf(getenv("ATTR"), "%d,%d,%d,%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6]); for (int k = 0; k < 7; k++) p.attr[k] = a[k]; }
    track_update_stream(&t, s0, 99);
    if (getenv("RACE")) { phys_place(&p, &t, 0); printf("spawn (%d %d %d) s=%.0f picks=%d finish=%d\n", p.pos[0] >> 8, p.pos[1] >> 8, p.pos[2] >> 8, p.s, t.npicks, t.has_finish); }
    else phys_spawn(&p, &t, s0);
    int fin_at = -1;
    if (getenv("NOPIPE")) p.pipe = false;
    static Ghost gh[3]; int ngh = 0, ev[2] = { 0, 0 };
    if (getenv("GHOSTS")) {
        const char *nm = strrchr(argv[1], '/'); nm = nm ? nm + 1 : argv[1];
        char tn[8]; memcpy(tn, nm, 4); tn[4] = 0;
        int kind = ghost_events_for(tn, ev), used[3];
        for (int k = 0; k < ev[1] - 1; k++) {
            if (!ghost_pick(&gh[k], ev[0], used, k)) break;
            used[k] = gh[k].id; gh[k].ph.character = k + 1; ghost_start(&gh[k], &t, k + 1); ngh++; if (getenv("GT")) gh[k].tough = atoi(getenv("GT")); if (getenv("AGGR")) gh[k].aggr = atoi(getenv("AGGR"));
            printf("ghost %d: %d_%d.ght %d frames (%.1f s race) kind %d\n", k, ev[0], gh[k].id, gh[k].n, gh[k].n * 0.08 - 3, kind);
        }
        phys_place(&p, &t, 0);
    }
    if (getenv("RAILPLACE")) {                                    // drop onto rail i near its first point
        const TrackRail *r = &t.rails[atoi(getenv("RAILPLACE"))];
        int k = 0; s32 *a = r->pts[k];
        p.pos[0] = a[0] << 8; p.pos[1] = (a[1] + 20) << 8; p.pos[2] = a[2] << 8;
        for (int j = 0; j < 3; j++) p.vel[j] = r->dir[k][j] * 500 * 64 / 16384;
        p.heading = sx_atan2(r->dir[k][0] << 2, r->dir[k][2] << 2); p.travel = p.heading;
        track_update_stream(&t, p.s, 99);
    }
    if (getenv("LETTERS")) p.letters = atoi(getenv("LETTERS"));
    for (int i = 0; i < secs * 25; i++) {
        track_update_stream(&t, p.s, 99);
        if (getenv("BOOSTFULL") && (i == 0 || getenv("BOOSTFULL")[0] == '2')) p.boost = 0x6400;
        u32 b = 0;
        const char *q = script;
        while (*q) {                              // ranges in ticks: a-b:LETTERS
            int a, e; char keys[16] = ""; int n = 0;
            if (sscanf(q, "%d-%d:%15[A-Z]%n", &a, &e, keys, &n) < 2) break;
            if (n == 0) { sscanf(q, "%d-%d:%n", &a, &e, &n); keys[0] = 0; }
            if (i >= a && i < e) for (char *k = keys; *k; k++)
                b |= *k == 'R' ? PB_RIGHT : *k == 'L' ? PB_LEFT : *k == 'T' ? PB_TUCK : *k == 'B' ? PB_BRAKE : *k == 'J' ? PB_JUMP : *k == 'X' ? PB_BOOST : *k == 'U' ? PB_TUCK : *k == 'D' ? PB_BRAKE : *k == 'G' ? PB_GRAB1 : *k == 'H' ? PB_GRAB2 : *k == 'I' ? PB_GRAB3 : *k == 'K' ? PB_GRAB4 : 0;
            q += n; if (*q == ',') q++; else break;
        }
        if (getenv("RAIL") && !p.rail_state && !p.air) {          // steer at a rail's first point
            const TrackRail *r = &t.rails[atoi(getenv("RAIL"))];
            static int tgt = 0;
            s32 dx0 = r->pts[tgt][0] - p.pos[0] / 256, dz0 = r->pts[tgt][2] - p.pos[2] / 256;
            if (tgt == 0 && dx0 * dx0 + dz0 * dz0 < 150 * 150) tgt = r->n - 1;
            int want = sx_atan2((r->pts[tgt][0] - p.pos[0] / 256) << 2, (r->pts[tgt][2] - p.pos[2] / 256) << 2);
            int d = sx_angdiff(p.facing, want);
            if (d > 0x18) b |= PB_RIGHT; else if (d < -0x18) b |= PB_LEFT;
        } else if (getenv("BOT")) b |= bot_buttons(&p, atof(getenv("BOT")));
        t.rays = t.tri_tests = 0;
        if (!getenv("WAIT") || i >= 75) phys_step(&p, b);
        for (int k = 0; k < ngh; k++) {
            if (ghost_bump(&gh[k], &p)) printf("  bump g%d t=%.2f kmh=%d gspeed=%d\n", k, i * 0.04, p.kmh, gh[k].speed);
            if (getenv("TELE") && k == 0 && i >= 100 && i < 160) { for (int j = 0; j < 3; j += 2) gh[0].off[j] += ((p.pos[j] - gh[0].ph.pos[j]) >> 8) + (j == 0 ? atoi(getenv("TELE")) : 0); }
            if (getenv("DIST") && i % 25 == 0) { s64 d2 = 0; for (int j = 0; j < 3; j++) { s64 u = (p.pos[j] - gh[k].ph.pos[j]) >> 8; d2 += u * u; } printf("  t=%.0f g%d dist %lld s %.0f vs %.0f want %d delay %d\n", i * 0.04, k, (long long)__builtin_sqrt((double)d2), gh[k].s, p.s, gh[k].want_ms, gh[k].delay_ms); }
            { int f = ghost_fight(&gh[k], &p); if (f) printf("  FIGHT g%d t=%.2f result %d (ghost punch %d, player punch %d, aggr %d)\n", k, i * 0.04, f, gh[k].punch_id, p.punch, gh[k].aggr); }
            int was = gh[k].finished;
            ghost_step(&gh[k], &t);
            if (!was && gh[k].finished) printf("ghost %d finished %.2f s at s=%.0f (len %.0f)\n", k, gh[k].fin_steps * 0.04 - 3, gh[k].s, t.length);
            if (getenv("GHOSTS")[0] == '2' && i % 50 == 0) {
                s32 a[3] = { gh[k].ph.pos[0] >> 8, (gh[k].ph.pos[1] >> 8) + 2000, gh[k].ph.pos[2] >> 8 }, bb[3] = { a[0], a[1] - 8000, a[2] }, hh[3], nn[3]; int sf;
                bool hit = track_ray(&t, a, bb, hh, nn, &sf);
                printf("  g%d t=%.1f s=%.0f y-ground=%d air=%d anim=%d off=%d,%d\n", k, i * 0.04, gh[k].s, hit ? (gh[k].ph.pos[1] >> 8) - hh[1] : 99999, gh[k].ph.air, gh[k].ph.anim, gh[k].off[0], gh[k].off[2]);
            }
        }
        if (getenv("POLYS")) {
            static long sum, n, mx, rmx; int tot = 0;
            for (u32 k = 0; k < t.h.nseg; k++) { int l = track_lod_for(&t, k, p.s, 0); if (l >= 0 && t.chunk[k]) tot += t.segs[k].lod[l].npolys; }
            sum += tot; n++; if (tot > mx) mx = tot; if ((long)t.resident_bytes > rmx) rmx = t.resident_bytes;
            if (p.finished || i == secs * 25 - 1) { printf("POLYS avg %ld max %ld maxres %ldKB\n", sum / n, mx, rmx / 1024); sum = n = mx = rmx = 0; }
        }
        if (getenv("HOLES")) {                                     // visible ground that belongs to an undrawn segment
            int bias = atoi(getenv("HOLES")), holes = 0, probes = 0; int seen[64] = {0};
            for (int dist = 0; dist <= 3000; dist += 250)
                for (int side = -2; side <= 2; side++) {
                    int hd = p.travel >= 0 ? p.travel : p.heading;
                    s32 cx = (p.pos[0] >> 8) + SINF(hd, dist) + SINF(hd + 0x200, side * 400);
                    s32 cz = (p.pos[2] >> 8) + COSF(hd, dist) + COSF(hd + 0x200, side * 400);
                    s32 a[3] = { cx, (p.pos[1] >> 8) + 3000, cz }, bb[3] = { cx, (p.pos[1] >> 8) - 6000, cz }, h[3], n[3], h2[3]; int sf;
                    if (!track_ray(&t, a, bb, h, n, &sf)) continue;
                    probes++;
                    ColGrid *save[64];
                    for (u32 k = 0; k < t.h.nseg; k++) { save[k] = t.col[k]; if (track_lod_for(&t, k, p.s, bias) >= 0) t.col[k] = NULL; }
                    bool hit = track_ray(&t, a, bb, h2, n, &sf);
                    for (u32 k = 0; k < t.h.nseg; k++) t.col[k] = save[k];
                    if (hit && h2[1] == h[1]) {
                        holes++;
                        for (u32 k = 0; k < t.h.nseg; k++) if (t.col[k] && track_lod_for(&t, k, p.s, bias) < 0) {
                            const SegRec *g = &t.segs[k]; int x = cx - g->origin[0], z = cz - g->origin[2];
                            if (x >= g->bbmin[0] && x <= g->bbmax[0] && z >= g->bbmin[2] && z <= g->bbmax[2]) seen[k] = dist + 1;
                        }
                    }
                }
            if (holes) { printf("HOLE t=%.2f s=%.0f %d/%d segs:", i * 0.04, p.s, holes, probes);
                         for (u32 k = 0; k < t.h.nseg; k++) if (seen[k]) printf(" %d[%d..%d]d%d", k, t.segs[k].s0, t.segs[k].s1, seen[k] - 1); printf("\n"); }
        }
        if (p.finished && fin_at < 0) { fin_at = i; printf("FINISH %d:%05.2f at s=%.0f picks=%d score=%d\n", (i + 1) * 40 / 60000, ((i + 1) * 40 % 60000) / 1000.0, p.s, p.picks_taken, p.score); }
        if (p.race_over) { printf("race over at %.2f, anim %d\n", i * 0.04, p.anim); break; }
        if (getenv("MOMTRACE") && i % 25 == 0) printf("M %.1f mom=%d vel=(%d %d %d) air=%d kmh=%d\n", i * 0.04, p.momentum, p.vel[0], p.vel[1], p.vel[2], p.air, p.kmh);
        if (getenv("ANIMTRACE") && (p.air || p.anim == 19 || i < 80)) printf("A %.2f anim=%d ms=%d/%d once=%d done=%d gs=%d sub=%d js=%d air=%d\n", i * 0.04, p.anim, p.anim_ms, p.anim_len, p.anim_once, p.anim_done, p.grab_state, p.grab_sub, p.jstate, p.air);
        if (p.trick_text[0]) { printf("TRICK %.2f \"%s\" score=%d tweak=%d letters=%d pend=%d lv=%d\n", i * 0.04, p.trick_text, p.score, p.tweak, p.letters, p.letters_pend, p.boost_level); p.trick_text[0] = 0; }
        if (getenv("SND")) for (int k = 0; k < p.sfx_n; k++) printf("  snd %d @%.2f\n", p.sfx_q[k], i * 0.04);
        p.sfx_n = 0;
        if (i % 5 == 0 || getenv("ALL"))
            printf("%5.2f s=%7.0f pos=(%7d %7d %7d) kmh=%3d spd=%4d air=%d js=%d surf=%d hd=%4d st=%d turn=%5d pitch=%4d anim=%3d tk=%d gr=%d rail=%d bal=%5d pts=%5d score=%6d boost=%5d lv=%d crash=%d resp=%d wall=%d tree=%d rails=%d\n",
                   i * 0.04, p.s, p.pos[0] >> 8, p.pos[1] >> 8, p.pos[2] >> 8, p.kmh, p.speed, p.air, p.jstate, p.surf, p.heading, p.stance,
                   p.turn, p.pitch, p.anim, p.trick_kind, p.grab_state, p.rail_state, p.balance, p.points, p.score, p.boost, p.boost_level, p.crashes, p.respawns, p.wall_bounces, p.tree_hits, p.rails_ridden);
    }
    printf("final fin=%d s=%.0f of %.0f respawns=%d score=%d crashes=%d rails=%d\n", fin_at, p.s, t.length, p.respawns, p.score, p.crashes, p.rails_ridden);
    return 0;
}

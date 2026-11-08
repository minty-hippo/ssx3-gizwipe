// superpipe test: ride wall to wall on BHP1 (steer toward one side until in the air off a lip, then the other),
// optionally spinning while airborne; print each air: launch, time, heading turned, spin input, landing, trick
// cc -O2 -w -I. -I../source -DITCM_CODE= pipetest.c ../source/{physics,tricks,rails,track,bot,ghost}.c -lm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sxtab.h"
#include "physics.h"
#include "anim_ids.h"
static void load_anim_lengths(const char *path)
{
    FILE *f = fopen(path, "rb"); if (!f) return;
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
    int spin = argc > 2 ? atoi(argv[2]) : 0;     // 0 none, 1 hold left in the air, 2 hold right
    int secs = argc > 3 ? atoi(argv[3]) : 60;
    static Phys p; memset(&p, 0, sizeof p);
    load_anim_lengths(getenv("SSXA") ? getenv("SSXA") : "../nitrofs/anims.ssxa");   // (run from ds_runtime/hosttest)
    track_update_stream(&t, 0, 99);
    phys_place(&p, &t, 0);
    int side = 1, air0 = 0, h0 = 0, hsum = 0, prevh = 0, nair = 0; s32 y0 = 0, ymax = 0;
    for (int i = 0; i < secs * 25; i++) {
        track_update_stream(&t, p.s, 99);
        u32 b = p.air ? 0 : PB_TUCK;
        if (!p.air) {                         // carve toward one wall: 50 degrees off the pipe's axis (its lip line)
            static int axis = -1;
            if (axis < 0 && t.npipe) {
                const s32 *a = t.pipe[0][0], *c = t.pipe[0][1];
                axis = sx_atan2((c[0] - a[0]) << 2, (c[2] - a[2]) << 2);
                if (p.travel >= 0 && abs(sx_angdiff(axis, p.travel)) > 0x200) axis = (axis + 0x400) & 0x7ff;
            }
            int tgt = ((axis < 0 ? p.heading : axis) + side * 0x120) & 0x7ff;
            int d = sx_angdiff(p.facing, tgt);
            if (d > 0x20) b |= PB_RIGHT; else if (d < -0x20) b |= PB_LEFT;
        } else if (spin) b |= spin == 1 ? PB_LEFT : PB_RIGHT;
        phys_step(&p, b);
        if (p.air && !air0) { air0 = i; h0 = p.facing; prevh = p.facing; hsum = 0; y0 = p.pos[1] >> 8; ymax = y0; }
        if (p.air) { hsum += sx_angdiff(prevh, p.facing); prevh = p.facing; if ((p.pos[1] >> 8) > ymax) ymax = p.pos[1] >> 8; }
        if (!p.air && air0) {
            int dt = i - air0;
            if (dt > 10) {
                nair++;
                { Phys q = p; int km = 0; for (int j = 0; j < 12; j++) { phys_step(&q, PB_TUCK); } km = q.kmh; printf("   (0.5 s after landing: %d km/h)\n", km); }
                printf("air %2d t=%5.1f dur %.2fs rise %4d turned %5.0f deg land facing %+5.0f off launch, crash=%d kmh=%d lip=%d\n",
                       nair, air0 * 0.04, dt * 0.04, ymax - y0, hsum * 360.0 / 2048, sx_angdiff(h0, p.facing) * 360.0 / 2048, p.crashes, p.kmh, p.lip);
                side = -side;
            }
            air0 = 0;
        }
        if (getenv("V") && p.air && (i % 3 == 0)) printf("   t=%.2f h=%d lip=%d heldup=%d tk=%d hd=%d y=%d\n", i * 0.04, p.height / 256, p.lip, p.held_up, p.trick_kind, p.heading, p.pos[1] >> 8);
        if (p.trick_text[0]) { printf("   TRICK \"%s\"\n", p.trick_text); p.trick_text[0] = 0; }
        p.sfx_n = 0;
        if (p.finished) { printf("finished\n"); break; }
    }
    return 0;
}

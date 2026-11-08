#include <stdio.h>
#include <string.h>
#include "phys_int.h"
#include "grabs.h"
#include "anim_ids.h"
int main(void)
{
    static u16 len[NANIM_IDS]; for (int i = 0; i < NANIM_IDS; i++) len[i] = 1000;
    len[0x91] = 1833; len[0xa3] = 433; len[0x61] = 1200;
    phys_set_anim_lengths(len, NANIM_IDS);
    static Phys p; memset(&p, 0, sizeof p); p.grab_idx = -1; p.character = 5; p.mult = 1; p.combo_lvl = 1;
    int gi = -1; for (int i = 0; i < 15; i++) if (GRABS[5][i].uber[2] == 0x91) gi = i;
    printf("grab %d clip %#x combo %d\n", gi, GRABS[5][gi].grab, GRABS[5][gi].combo);
    const u16 *c = GRAB_COMBO[GRABS[5][gi].combo]; u32 cb = c[0] | c[1] | c[2] | c[3];
    tricks_takeoff(&p); p.height = 1000 * 256; p.air = true;
    for (int t = 0; t < 200; t++) {
        p.boost_level = 3;
        p.buttons = (t < 170 ? cb : 0) | (t >= 30 && t < 130 ? PB_BOOST : 0);
        p.pressed = t == 30 ? PB_BOOST : 0;
        air_tricks(&p); anim_tick(&p);
        printf("%3d st %d sub %d anim %#x ms %4d hold %4d once %d rev %d done %d land_ok %d\n", t, p.grab_state, p.grab_sub, p.anim, p.anim_ms, p.anim_hold, p.anim_once, p.anim_rev, p.anim_done, p.land_ok);
    }
}
int sx_angdiff(int a, int b) { int d = (b - a) & 0x7ff; return d > 0x400 ? d - 0x800 : d; }
int sx_ease(int a, int b) { return a + (b - a) / 8; }

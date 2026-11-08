#include <stdio.h>
#include <string.h>
#include "phys_int.h"
int main(void)
{
    static Phys p; memset(&p, 0, sizeof p); p.combo_lvl = 1; p.mult = 1; p.grab_idx = -1;
    for (int t = 0; t < 4; t++) {                       // four 1000-point spins, 1 s apart
        p.points = 1000; p.trick_done = true; p.trick_kind = 1 + (t & 1);
        tricks_land(&p);
        printf("land %d: score %d bank %d lvl %d\n", t, p.score, p.combo_bank, p.combo_lvl);
        for (int i = 0; i < 25; i++) combo_step(&p);
    }
    for (int i = 0; i < 80; i++) combo_step(&p);
    printf("after combo: score %d bonus %d\n", p.score, p.last_bonus);
}

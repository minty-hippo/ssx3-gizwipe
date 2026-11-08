#include <stdio.h>
#include <string.h>
#include "phys_int.h"
int sx_angdiff(int a, int b) { int d = (b - a) & 0x7ff; return d > 0x400 ? d - 0x800 : d; }
int sx_ease(int a, int b) { return a + (b - a) / 8; }
static void land(Phys *p, int halves)
{
    p->points = 1000; p->trick_done = true; p->trick_kind = 1; p->nm_halves = halves; p->grab_idx = -1;
    tricks_land(p);
    printf("  %-14s score %6d (+%d) combo x%d bank %d\n", p->trick_text, p->score, p->last_points, p->combo_lvl, p->combo_bank);
    for (int i = 0; i < 10; i++) combo_step(p);
}
int main(void)
{
    static Phys p; memset(&p, 0, sizeof p); p.combo_lvl = 1; p.mult = 1; p.grab_idx = -1;
    land(&p, 2); land(&p, 2); land(&p, 2); land(&p, 2); land(&p, 3); land(&p, 2);
    int b = p.bonus_n;
    for (int i = 0; i < 200; i++) combo_step(&p);
    printf("combo over: score %d bonus %d paid %d lvl %d\n", p.score, p.last_bonus, p.bonus_n - b, p.bonus_lvl);
}

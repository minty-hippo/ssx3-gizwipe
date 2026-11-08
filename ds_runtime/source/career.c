// SSX 3 GizWipe - career cash and unlocks (see career.h)
#include <stdio.h>
#include "xtext.h"
#include <string.h>
#include "career.h"

Career career;

// event table rows (0xdde00 + event * 0x54): +0x24 / +0x28 / +0x2c cash for 1st / 2nd / 3rd (the rival events pay
// only a win), +0x30 career earnings that unlock the event
const s32 EV_REWARD[15][3] = {
    { 10000, 3000, 2000 }, { 10000, 3000, 2000 }, { 12000, 10000, 5000 }, { 15000, 10000, 5000 },
    { 25000, 0, 0 }, { 25000, 0, 0 }, { 25000, 15000, 10000 }, { 25000, 15000, 10000 },
    { 35000, 25000, 15000 }, { 50000, 0, 0 }, { 50000, 0, 0 }, { 50000, 35000, 20000 },
    { 70000, 50000, 25000 }, { 100000, 0, 0 }, { 100000, 0, 0 },
};
const s32 EV_UNLOCK[15] = { 0, 0, 0, 20000, 30000, 30000, 50000, 70000, 80000, 100000, 100000, 150000, 200000, 250000, 250000 };

bool career_unlocked(int ev) { return career.earned >= EV_UNLOCK[ev]; }

// 0x711c4: the first finish of an event pays the full reward; later ones pay the difference to a lower place
// already rewarded, or a tenth when that place (or a better one) was already reached
int career_award(int ev, int place, int *unl, int *nunl)
{
    *nunl = 0;
    if (place < 1 || place > 3 || EV_REWARD[ev][place - 1] <= 0) { career.placed[ev] |= 8; return 0; }
    u8 had = career.placed[ev];
    bool first = !(had & 8);
    const s32 *r = EV_REWARD[ev];
    s32 pay = r[place - 1];
    if (!first) {
        if (place == 1) {
            // the exe tests 1st first but then lets an earlier 2nd / 3rd override it, paying the difference again
            // on every win; here a repeated win pays the tenth
            if (had & 1) pay = r[0] / 10;
            else if (had & 2) pay = r[0] - r[1];
            else if (had & 4) pay = r[0] - r[2];
        } else if (place == 2) {
            if (had & 3) pay = r[1] / 10;
            else if (had & 4) pay = r[1] - r[2];
        } else if (had & 7) pay = r[2] / 10;
    }
    bool before[15];
    for (int i = 0; i < 15; i++) before[i] = career_unlocked(i);
    career.placed[ev] |= 8 | (1 << (place - 1));
    career.cash += pay; career.earned += pay;
    for (int i = 0; i < 15; i++) if (!before[i] && career_unlocked(i)) unl[(*nunl)++] = i;
    return pay;
}

void career_fmt(char *o, s32 v)
{
    if (v >= 1000000) sprintf(o, "$%ld,%03ld,%03ld", (long)(v / 1000000), (long)(v / 1000 % 1000), (long)(v % 1000));
    else if (v >= 1000) sprintf(o, "$%ld,%03ld", (long)(v / 1000), (long)(v % 1000));
    else sprintf(o, "$%ld", (long)v);
}

// attribute names as the store lists them (0xaf410: Acceleration, Edging, Speed, Spin, Stability, Toughness, Tricks)
const char *const ATTR_NAME[ATTR_N] = { XT_1, "Edging", "Speed", "Spin", "Stability", "Toughness", "Tricks" };

// 0x1e8dc: each character's starting attributes
static const u8 ATTR0[10][7] = {
    { 10, 20, 10, 10, 30, 40, 10 },   // zoe
    { 10, 30, 10, 10, 30, 30, 10 },   // moby
    { 20, 30, 10, 10, 10, 40, 10 },   // psymon
    { 10, 30, 10, 30, 10, 10, 30 },   // griff
    { 30, 40, 10, 10, 20, 10, 10 },   // viggo
    { 30, 10, 30, 10, 10, 30, 10 },   // elise
    { 10, 10, 10, 10, 30, 40, 10 },   // nate
    { 10, 10, 10, 30, 10, 20, 40 },   // mac
    { 30, 20, 10, 10, 20, 30, 10 },   // alegra
    { 20, 20, 10, 30, 10, 10, 30 },   // kaori
};

void career_attr_defaults(void) { memcpy(career.attr, ATTR0, sizeof(ATTR0)); }
void career_defaults(void) { memset(&career, 0, sizeof(career)); career_attr_defaults(); }

// 0xa5cd8: $250 per +2 in the 10s, $500 in the 20s ... $3,500 in the 90s; 100 is the top
int attr_cost(int v)
{
    static const int C[10] = { 250, 250, 500, 750, 1000, 1500, 2000, 2500, 3000, 3500 };
    return v >= 100 ? 0 : C[v / 10];
}

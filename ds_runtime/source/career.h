// SSX 3 GizWipe - the Gizmondo career: cash rewards for 1st / 2nd / 3rd (event table 0xdde00 + 0x24..0x2c, paid by
// 0x711c4) and events unlocked by career earnings (+0x30, checked by 0x3353c)
#pragma once
#include <nds.h>

typedef struct {
    u8 placed[15];          // bit 0: 1st, 1: 2nd, 2: 3rd (0x8ec + event * 0xc, +5..+7), bit 3: played (+8)
    u8 pad;
    s32 cash;               // available cash (profile +0x9a0)
    s32 earned;             // career earnings (+0x9a4): unlocks events
    u8 attr[10][7];         // per character (exe index): acceleration, edging, speed, spin, stability, toughness, tricks
    u8 pad2[2];
} Career;
#define CAREER_V1_SIZE 24   // the "SSX2" save: placed, cash, earned

enum { ATTR_N = 7 };
extern const char *const ATTR_NAME[ATTR_N];

extern Career career;
extern const s32 EV_REWARD[15][3];
extern const s32 EV_UNLOCK[15];

bool career_unlocked(int ev);
// pays the reward for finishing `ev` in `place` (1..); returns the cash, fills the events it newly unlocked
int  career_award(int ev, int place, int *unlocked, int *nunlocked);
void career_fmt(char *o, s32 v);              // "$12,000"
void career_defaults(void);                   // starting attributes (0x1e8dc), nothing won
void career_attr_defaults(void);              // only the attributes
int  attr_cost(int value);                    // price of the next +2 at that value (0xa5cd8 by value / 10, 0x2718c)

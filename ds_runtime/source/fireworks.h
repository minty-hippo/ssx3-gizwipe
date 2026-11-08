// SSX 3 GizWipe - fireworks: fwtrigger / fw<A>[-<B>] course markers (0x61b54, 0x752b8, 0x74c40) and the
// particle bursts they launch (0x39cec spawn, 0x39358 update, 0x39f48 draw)
#pragma once
#include "track.h"

typedef struct {
    s32 pos[3];            // 24.8 game units
    s32 vel[3];            // 24.8 per 32 steps (pos += vel * 8 / 256 per 40 ms step)
    s16 life;              // 200..327, -8 per step
    s16 delay;             // steps before it starts (fw<A>: A)
    u16 color;             // RGB15
} Spark;

enum { FW_SPARKS = 512 };
typedef struct {
    struct FwMark { s32 pos[3]; u16 type; u16 delay; bool live; } *m; int nm;   // triggers and launchers
    Spark p[FW_SPARKS]; int head; int alive;
    u32 seed; int ncol;
} Fireworks;

void fw_init(Fireworks *f, const Track *t);
void fw_free(Fireworks *f);
// one 40 ms step; player position in game units. Returns true when a set was launched (play sound 14)
bool fw_step(Fireworks *f, const s32 player[3]);

// SSX 3 GizWipe - fireworks (see fireworks.h)
//  markers (0x61b54): "fwtrigger" -> type 1 (its "%d" never parses, so 0 + 1); "fw<A>" / "fw<A>-<B>" -> launcher of
//      group 0x10 + B with a delay of A steps.
//  0x752b8: an armed trigger within xz distance^2 < 0x3e800 of the player sets the active group (type + 0xf) and
//      disarms itself.  0x74c40: every armed launcher of that group (group 0x10: only those within 3000 units)
//      disarms and bursts (0x39cec), then sound 14 plays.
//  0x39cec: 20 sparks with velocity x, z in [-0x6400, 0x6400), y in [0x12c00, 0x1c200) and 5 narrower, 3x higher
//      ones; life 200 + rand(128); colour 0xf000 / 0xf000 / 0xf780 / 0xf780 (RGB565) by spawn count.
//  0x39358 per step: a waiting spark counts its delay down, else life -= 8, pos += vel * 8 / 256, vy -= 93.75.
#include <stdlib.h>
#include <string.h>
#include "fireworks.h"

static u32 rnd(Fireworks *f) { f->seed = f->seed * 1664525u + 1013904223u; return f->seed >> 16; }
static s32 rnd_range(Fireworks *f, int r) { return (s32)((rnd(f) * (u32)r) >> 16); }

void fw_init(Fireworks *f, const Track *t)
{
    free(f->m);
    memset(f, 0, sizeof(*f));
    f->seed = 0x5eed1234;
    f->m = malloc(sizeof(*f->m) * (t->h.nmarkers + 1));
    for (u32 i = 0; i < t->h.nmarkers; i++) {
        const char *n = t->markers[i].name;
        if (n[0] != 'f' || n[1] != 'w') continue;
        struct FwMark *k = &f->m[f->nm];
        if (!strncmp(n, "fwtrigger", 9)) {
            int v = n[9] >= '0' && n[9] <= '9' ? atoi(n + 9) : 0;   // the exe's sscanf never matches: always 1
            k->type = v + 1; k->delay = 0;
            if (k->type > 9) continue;
        } else if (n[2] >= '0' && n[2] <= '9') {
            char *e; int a = strtol(n + 2, &e, 10), b = 0;
            if (*e == '-') b = atoi(e + 1);
            if (b > 8) b = 0;
            k->type = 0x10 + b; k->delay = a;
        } else continue;
        for (int j = 0; j < 3; j++) k->pos[j] = t->markers[i].pos[j];
        k->live = true;
        f->nm++;
    }
}

void fw_free(Fireworks *f) { free(f->m); f->m = NULL; f->nm = 0; f->alive = 0; }

static void spark(Fireworks *f, const s32 pos[3], s32 vx, s32 vy, s32 vz, int delay)
{
    static const u16 COL[4] = { 0xf000, 0xf000, 0xf780, 0xf780 };
    Spark *s = &f->p[f->head];
    f->head = (f->head + 1) & (FW_SPARKS - 1);
    for (int j = 0; j < 3; j++) s->pos[j] = pos[j] << 8;
    s->vel[0] = vx; s->vel[1] = vy; s->vel[2] = vz;
    s->life = 200 + (rnd(f) & 0x7f);
    s->delay = delay;
    u16 c = COL[f->ncol++ & 3];
    s->color = (c >> 11) | (((c >> 6) & 31) << 5) | ((c & 31) << 10);
}

static void burst(Fireworks *f, const s32 pos[3], int delay)
{
    for (int i = 0; i < 20; i++) {
        s32 x = rnd_range(f, 0xc800) - 0x6400, y = rnd_range(f, 0x9600) + 0x12c00, z = rnd_range(f, 0xc800) - 0x6400;
        spark(f, pos, x, y, z, delay);
    }
    for (int i = 0; i < 5; i++) {
        s32 x = rnd_range(f, 0xc800) - 0x6400, y = rnd_range(f, 0x9600) + 0x12c00, z = rnd_range(f, 0xc800) - 0x6400;
        spark(f, pos, x >> 1, y * 3, z >> 1, delay);
    }
}

bool fw_step(Fireworks *f, const s32 pl[3])
{
    int group = 0;
    for (int i = 0; i < f->nm && !group; i++) {
        struct FwMark *k = &f->m[i];
        if (!k->live || k->type > 9) continue;
        s32 dx = pl[0] - k->pos[0], dz = pl[2] - k->pos[2];
        if (abs(dx) < 1024 && abs(dz) < 1024 && dx * dx + dz * dz < 0x3e800) { group = k->type + 0xf; k->live = false; }
    }
    bool launched = false;
    if (group)
        for (int i = 0; i < f->nm; i++) {
            struct FwMark *k = &f->m[i];
            if (!k->live || k->type != group) continue;
            if (group == 0x10) {
                s32 dx = pl[0] - k->pos[0], dz = pl[2] - k->pos[2];
                if (abs(dx) >= 3000 || abs(dz) >= 3000 || dx * dx + dz * dz >= 9000000) continue;
            }
            k->live = false;
            burst(f, k->pos, k->delay);
            launched = true;
        }
    int alive = 0;
    for (int i = 0; i < FW_SPARKS; i++) {
        Spark *s = &f->p[i];
        if (s->life <= 0) continue;
        alive++;
        if (s->delay) { s->delay--; continue; }
        s->life -= 8;
        if (s->life < 0) s->life = 0;
        for (int j = 0; j < 3; j++) s->pos[j] += (s->vel[j] * 8) >> 8;
        s->vel[1] -= 93;
    }
    f->alive = alive;
    return launched;
}

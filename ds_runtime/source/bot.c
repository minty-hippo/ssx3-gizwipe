#include "bot.h"
u32 bot_buttons(const Phys *p, float lookahead)
{
    if (p->air || p->rail_state) return 0;                      // no air or rail input
    const Track *t = p->trk; const RacePoint *r = t->race; int n = t->h.nraceline;
    float s = p->s + lookahead;
    int i = p->seg_hint;
    while (i < n - 1 && r[i].arclen < s) i++;
    s32 tx = r[i].x, tz = r[i].z;
    if (s >= t->length && t->has_finish) {                      // past the end of the race line: through the finish
        s32 bx = t->fin_r[0] - t->fin_l[0], bz = t->fin_r[2] - t->fin_l[2];
        tx = (t->fin_l[0] + t->fin_r[0]) / 2 - bz; tz = (t->fin_l[2] + t->fin_r[2]) / 2 + bx;   // the far side
    }
    s32 dx = tx - p->pos[0] / 256, dz = tz - p->pos[2] / 256;
    int want = sx_atan2(dx << 2, dz << 2);
    int face = p->air ? p->travel : p->facing;
    int d = sx_angdiff(face, want);
    u32 b = 0;
    if (d > 0x18) b |= PB_RIGHT; else if (d < -0x18) b |= PB_LEFT;
    if ((d < 0 ? -d : d) < 0x60 && !p->air) { b |= PB_TUCK; if (p->kmh < 75 && p->boost > 0x800) b |= PB_BOOST; }
    return b;
}

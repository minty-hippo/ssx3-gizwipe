#include <stdio.h>
#include "track.h"
int main(int argc, char **argv)
{
    for (int a = 1; a < argc; a++) {
        Track t; if (!track_load(&t, argv[a])) continue;
        int n = t.h.nraceline, in = 0; float s0 = 0;
        for (float s = 0; s < t.length; s += 100) {
            track_update_stream(&t, s, 99);
            float p[3]; track_sample(&t, s, p);
            s32 g0[3] = { p[0], p[1] + 300, p[2] }, g1[3] = { p[0], p[1] - 600, p[2] }, h[3], nn[3]; int sf;
            if (!track_ray(&t, g0, g1, h, nn, &sf)) continue;
            s32 a0[3] = { p[0], h[1] + 2500, p[2] }, a1[3] = { p[0], h[1] - 10, p[2] }, h2[3];
            bool cov = track_ray(&t, a0, a1, h2, nn, &sf) && h2[1] > h[1] + 120;
            if (cov && !in) { in = 1; s0 = s; }
            if (!cov && in) { in = 0; if (s - s0 >= 300) printf("%s covered s=%.0f..%.0f\n", argv[a], s0, s); }
        }
        track_free(&t);
    }
}

#include <stdio.h>
#include <stdlib.h>
#include "track.h"
int main(int argc, char **argv)
{
    Track t; track_load(&t, argv[1]); int x = atoi(argv[2]);
    track_update_stream(&t, atof(argv[3]), 99);
    for (int z = -1200; z <= 1600; z += 100) {
        s32 a[3] = { x, 3000, z }, b[3] = { x, -6000, z }, h[3], n[3]; int sf;
        if (track_ray(&t, a, b, h, n, &sf)) printf("z=%5d y=%6d n=(%5d %5d %5d) surf %d\n", z, h[1], n[0], n[1], n[2], sf);
        else printf("z=%5d none\n", z);
    }
}

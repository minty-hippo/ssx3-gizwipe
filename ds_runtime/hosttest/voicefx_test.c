// host test for source/voicefx.c: a gliding buzz through each effect; prints the output's pitch per frame and
// writes WAVs to listen to (cc -DHOSTTEST -O2 -I../source voicefx_test.c ../source/voicefx.c -lm)
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "voicefx.h"
static void wav(const char *nm, const s8 *b, int n, int fs)
{
    FILE *f = fopen(nm, "wb"); int t = 36 + n;
    fwrite("RIFF", 1, 4, f); fwrite(&t, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    int v16 = 16; short pcm = 1, ch = 1, ba = 1, bps = 8; fwrite(&v16, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&fs, 4, 1, f); fwrite(&fs, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
    for (int i = 0; i < n; i++) fputc((unsigned char)(b[i] + 128), f);
    fclose(f);
}
static float pitch(const s8 *x, int n, int at, int fs)
{
    int W = 1024; if (at + W + fs / 60 > n) return 0;
    float best = 0; int bl = 0;
    for (int L = fs / 600; L < fs / 60; L++) {
        double c = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < W; i++) { c += x[at + i] * x[at + i + L]; e0 += x[at + i] * x[at + i]; e1 += x[at + i + L] * x[at + i + L]; }
        float q = c / sqrt(e0 * e1 + 1); if (q > best) { best = q; bl = L; }
    }
    // refine with the first lag within 90 % of the best
    for (int L = fs / 600; L < bl; L++) {
        double c = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < W; i++) { c += x[at + i] * x[at + i + L]; e0 += x[at + i] * x[at + i]; e1 += x[at + i + L] * x[at + i + L]; }
        if (c / sqrt(e0 * e1 + 1) > best * 0.9) { bl = L; break; }
    }
    if (!bl) return 0;
    double q[3];
    for (int d = -1; d <= 1; d++) { int L = bl + d; double c = 0, e0 = 0, e1 = 0;
        for (int i = 0; i < W; i++) { c += x[at + i] * x[at + i + L]; e0 += x[at + i] * x[at + i]; e1 += x[at + i + L] * x[at + i + L]; }
        q[d + 1] = c / sqrt(e0 * e1 + 1); }
    double den = q[0] - 2 * q[1] + q[2], off = den != 0 ? 0.5 * (q[0] - q[2]) / den : 0;
    float hz = (float)fs / (bl + off);
    return 12 * log2f(hz / 440) + 69;            // as a MIDI note
}
int main(void)
{
    int fs = 11025, n = fs * 2;
    s16 *in = malloc(n * 2); s8 *raw = malloc(n), *out = malloc(n * 2);
    double ph = 0;
    for (int i = 0; i < n; i++) {
        double f0 = getenv("F0") ? atof(getenv("F0")) : 180 + 60.0 * i / n;  ph += f0 / fs;
        double v = 0; for (int h = 1; h < 8; h++) v += sin(2 * M_PI * h * ph) / h;
        double env = i < 2000 || i > n - 2000 ? 0.02 : 1;
        in[i] = (s16)(v * 6000 * env + (rand() % 200 - 100) + 300);
    }
    int m = vfx_clean(in, n, fs, raw);
    printf("clean %d -> %d\n", n, m);
    wav("vfx_dry.wav", raw, m, fs);
    struct { const char *nm; VoiceFx fx; } T[] = {
        { "plain", { 0, 0, 0, 0, 5, 10 } }, { "up5", { 5, 0, 0, 0, 5, 10 } }, { "tune", { 0, 10, 0, 0, 5, 10 } },
        { "chorus", { 0, 0, 10, 0, 5, 10 } }, { "echo", { 0, 0, 0, 8, 5, 10 } }, { "all", { -4, 7, 6, 5, 3, 10 } },
        { "tune10", { 0, 10, 0, 0, 5, 10 } }, { "tune3", { 0, 3, 0, 0, 5, 10 } }, { "bright", { 0, 0, 0, 0, 5, 10, 5, 0 } },
        { "bass", { 0, 0, 0, 0, 5, 10, 0, 5 } }, { "lp6", { 0, 0, 0, 0, 5, 10, 0, 0, 6, 0 } }, { "hp6", { 0, 0, 0, 0, 5, 10, 0, 0, 0, 6 } } };
    for (int t = 0; t < 12; t++) {
        int k = vfx_render(raw, m, fs, &T[t].fx, out, n * 2);
        char nm[40]; snprintf(nm, 40, "vfx_%s.wav", T[t].nm); wav(nm, out, k, fs);
        printf("%-6s len %5d  pitch:", T[t].nm, k);
        for (int at = 1000; at < m - 1500; at += 1500) printf(" %5.2f", pitch(out, k, at, fs));
        printf("\n");
    }
    return 0;
}

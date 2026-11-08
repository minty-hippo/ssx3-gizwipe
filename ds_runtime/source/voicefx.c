// SSX 3 GizWipe - voice clip effects (port addition): pitch, autotune, chorus and echo for the clips recorded
// with the DS microphone (voice.c). Integer DSP (the ARM9 has no FPU; a few float calls per 23 ms frame only), run
// once when a clip or its sliders change, never while racing. Portable C: hosttest/voicefx_test.c runs it on a PC.
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "voicefx.h"
#ifdef HOSTTEST
static void vfx_idle(void) {}
#else
void vfx_idle(void);          // main.c: keeps the music streaming through the ~0.5 s a clip takes
#endif
#if defined(TEST_VOICE) && !defined(HOSTTEST)
int vfx_t[5];
#define TMARK(k) (vfx_t[k] = (int)(cpuGetTiming() / 33514))
#else
#define TMARK(k)
#endif

// --- pitch detection (autotune): normalised autocorrelation over a 512-sample window every 256 samples, lags for
// 70..600 Hz, every other sample (enough for a voice's fundamental)
enum { HOP = 256, WIN = 512 };
// x here is the clip at 1/16 level (+-2048): 256 products then fit 32 bits (64-bit sums are slow on the ARM9)
static s64 corr_q(const s16 *x, int at, int L, s32 e0, int step)   // c^2 / (e0 e1) x 4096 (0 if c <= 0)
{
    s32 c = 0, e1 = 0;
    const s16 *p = x + at, *q = x + at + L;
    for (int i = 0; i < WIN; i += step) { s32 b = q[i]; c += p[i] * b; e1 += b * b; }
    if (step == 4) { c *= 2; e1 *= 2; }                     // (e0 is summed every other sample)
    s32 cs = c >> 8, a = e0 >> 8, bb = e1 >> 8;
    return c > 0 && a > 0 && bb > 0 ? (s64)cs * cs * 4096 / ((s64)a * bb) : 0;
}
// the period in 1/256 samples (0: unvoiced)
static int detect_lag(const s16 *x, int n, int at, int fs, int *clarity)
{
    if (at < 0) at = 0;
    int lag0 = fs / 600, lag1 = fs / 70;
    if (at + WIN + lag1 > n) { *clarity = 0; return 0; }
    s32 e0 = 0;
    for (int i = 0; i < WIN; i += 2) e0 += x[at + i] * x[at + i];
    if (e0 < WIN / 2 * 19 * 19) { *clarity = 0; return 0; }   // too quiet: unvoiced
    int best = 0; s64 bestv = 0; int bestq = 0;
    // a coarse pass (every other lag, every 4th sample: the ARM9 spent ~0.8 s a clip on the full search), then
    // the best lag and its neighbours in full
    for (int L = lag0; L <= lag1; L += 2) {
        s64 q = corr_q(x, at, L, e0, 4);
        if (q > bestv) { bestv = q; best = L; }
    }
    if (!best) { *clarity = 0; return 0; }
    { int c0 = best; bestv = 0;
      for (int L = c0 - 1; L <= c0 + 1; L++) { if (L < lag0 || L > lag1) continue; s64 q = corr_q(x, at, L, e0, 2); if (q > bestv) { bestv = q; best = L; bestq = (int)q; } } }
    // prefer a sub-multiple (L/2, L/3) that is nearly as strong: the true period
    for (int div = 3; div >= 2; div--) {
        int L = (best + div / 2) / div; if (L - 1 < lag0) continue;
        s64 q = 0; int bl = L;
        for (int d = -1; d <= 1; d++) { s64 v = corr_q(x, at, L + d, e0, 2); if (v > q) { q = v; bl = L + d; } }
        if (q * 10 > bestv * 8) { best = bl; bestq = (int)q; break; }
    }
    *clarity = bestq;                                       // r^2 x 4096
    // a parabola through the peak and its neighbours: the period between samples (a semitone near 200 Hz is
    // only ~3 samples of lag at 11 kHz)
    s64 qa = best > lag0 ? corr_q(x, at, best - 1, e0, 2) : bestq, qb = corr_q(x, at, best + 1, e0, 2), q0 = bestq;
    s64 den = qa - 2 * q0 + qb;
    s32 off = den < 0 ? (s32)((qa - qb) * 128 / den) : 0;   // (qa - qb) / (2 den), x 256
    if (off > 128) off = 128;
    if (off < -128) off = -128;
    return (best << 8) + off;
}

// --- a two-tap delay-line pitch shifter with a time-varying ratio (Q16): the taps sweep a 30 ms window at
// (1 - ratio) samples per sample, half a window apart, cross-faded so each is silent as it wraps
static void shift(const s16 *x, int n, s16 *y, const s32 *ratio_q16, int fs)
{
    int L = fs * 30 / 1000; if (L < 64) L = 64;
    s32 Lq = L << 16;
    s32 d = 0;                                             // tap 0's delay, Q16 (tap 1 = d + L/2)
    s64 inv = ((s64)32768 << 32) / (Lq / 2);               // (no 64-bit division per sample: slow on the ARM9)
    for (int i = 0; i < n; i++) {
        if (!(i & 2047)) vfx_idle();
        s32 r = ratio_q16[i / HOP];
        s32 rn = ratio_q16[i / HOP + 1];
        r += (rn - r) / HOP * (i % HOP);                   // smooth across the frame (HOP a power of two)
        s32 acc = 0;
        for (int t = 0; t < 2; t++) {
            s32 dt = d + (t ? Lq / 2 : 0); if (dt >= Lq) dt -= Lq;
            s32 pos = (i << 16) - dt - (1 << 16);            // read point
            s32 s;
            if (pos < 0) s = 0;
            else { int p = pos >> 16; s32 f = pos & 0xffff; s32 a = x[p], b = p + 1 < n ? x[p + 1] : 0; s = a + (s32)(((s64)(b - a) * f) >> 16); }
            // triangular window over the sweep: 0 at the wrap, 1 half way
            s32 w = (s32)(((s64)(dt < Lq / 2 ? dt : Lq - dt) * inv) >> 32);
            acc += (s32)(((s64)s * w) >> 15);
        }
        y[i] = acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc;
        d -= r - (1 << 16);                                // pitch up: the delay shrinks
        while (d < 0) d += Lq;
        while (d >= Lq) d -= Lq;
    }
}

static s32 lfo(s32 ph)                                      // ph: 0..65535 a cycle -> -4096..4096 (a parabola)
{
    s32 t = ph & 0xffff, h = t & 0x7fff;                    // half cycle
    s32 v = (s32)((s64)h * (0x8000 - h) >> 16);             // 0..0x1000 peak at h = 0x4000
    return t < 0x8000 ? v : -v;
}

int vfx_render(const s8 *raw, int n, int fs, const VoiceFx *fx, s8 *out, int out_max)
{
    if (n <= 0) return 0;
    s16 *x = malloc(n * 2), *y = malloc(n * 2);
    int nf = n / HOP + 2;
    s32 *ratio = malloc(nf * 4);
    if (!x || !y || !ratio) { free(x); free(y); free(ratio); return 0; }
    for (int i = 0; i < n; i++) { x[i] = raw[i] << 8; y[i] = raw[i] << 4; }   // (y: the 1/16 copy pitch detection reads)

    // 1. pitch and autotune: one ratio a frame
    float base = powf(2.0f, fx->pitch / 12.0f);
    float hold = 0;                                      // the last voiced frame's correction (held through gaps)
    for (int f = 0; f < nf; f++) {
        if (!(f & 3)) vfx_idle();
        float r = base;
        if (fx->tune > 0) {
            int cl, lag = detect_lag(y, n, f * HOP + HOP / 2 - WIN / 2, fs, &cl);   // (centred on the frame)
            if (lag > 0 && cl > 4096 * 4 / 10) {           // voiced (r > ~0.63)
                float hz = (float)fs * 256 / lag * base;    // its pitch after the shift
                float m = 12.0f * log2f(hz / 440.0f) + 69.0f, t = floorf(m + 0.5f);
                hold = (t - m) * fx->tune / 10.0f;          // 10: snapped hard to the semitone
            }
            r *= powf(2.0f, hold / 12.0f);
        }
        ratio[f] = (s32)(r * 65536.0f);
    }
    TMARK(0);
    bool shifted = fx->pitch != 0 || fx->tune > 0;
    if (shifted) shift(x, n, y, ratio, fs); else memcpy(y, x, n * 2);

    TMARK(1);
    // 2. chorus ("choral"): three voices 14 / 21 / 28 ms back, each swept +-3 ms at its own slow rate
    int len = n;
    int echo_d = fx->echo > 0 ? fs * (40 + fx->echo_ms * 45) / 1000 : 0;   // 85..490 ms
    int tail = fx->echo > 0 ? echo_d * (2 + fx->echo / 3) : 0;
    if (tail > fs) tail = fs;
    len = n + tail; if (len > out_max) len = out_max;
    s32 *z = malloc(len * 4);
    if (!z) { free(x); free(y); free(ratio); return 0; }
    for (int i = 0; i < len; i++) z[i] = i < n ? y[i] : 0;
    if (fx->chorus > 0) {
        static const int base_ms[3] = { 14, 21, 28 }; static const int rate_mhz[3] = { 700, 1100, 1500 };
        s32 wet = fx->chorus * 4096 / 10, dry = 4096 - fx->chorus * 4096 / 25;
        u32 ph[3], inc[3]; s32 dbase[3], depth = (s32)(((s64)fs * 3 << 16) / 1000);
        for (int v = 0; v < 3; v++) {
            ph[v] = (u32)v * 0x55555555u; inc[v] = (u32)((((u64)rate_mhz[v]) << 32) / 1000 / fs);
            dbase[v] = (s32)(((s64)fs * base_ms[v] << 16) / 1000);
        }
        for (int i = 0; i < n; i++) {
            if (!(i & 2047)) vfx_idle();
            s32 acc = 0;
            for (int v = 0; v < 3; v++) {
                ph[v] += inc[v];
                s32 dq = dbase[v] + (s32)(((s64)depth * lfo((s32)(ph[v] >> 16))) >> 12);
                s32 pos = (i << 16) - dq;
                if (pos < 0) continue;
                int p = pos >> 16; s32 fr = pos & 0xffff; s32 a = y[p], b = p + 1 < n ? y[p + 1] : 0;
                acc += a + (s32)(((s64)(b - a) * fr) >> 16);
            }
            z[i] = (y[i] * dry >> 12) + (acc / 3 * wet >> 12);
        }
    }
    TMARK(2);
    // 3. echo: feedback delay, the repeats fading by the slider (0.15 .. 0.6)
    if (fx->echo > 0) {
        s32 fb = 600 + fx->echo * 180;                      // Q12
        for (int i = echo_d; i < len; i++) z[i] += (s32)(((s64)z[i - echo_d] * fb) >> 12);
    }
    TMARK(3);
    // 4. level: the peak to ~90 %, then 8-bit
    s32 peak = 1;
    for (int i = 0; i < len; i++) { s32 a = z[i] < 0 ? -z[i] : z[i]; if (a > peak) peak = a; }
    s32 g = (s32)((s64)29491 * 4096 / peak);               // Q12 gain to 0.9 x full scale
    if (g > 4096 * 4) g = 4096 * 4;
    int last = 0;
    for (int i = 0; i < len; i++) {
        s32 v = (s32)(((s64)z[i] * g) >> 12) >> 8;
        if (v > 127) v = 127; if (v < -128) v = -128;
        out[i] = (s8)v; if (v > 2 || v < -2) last = i;
    }
    free(x); free(y); free(ratio); free(z);
    return last + 1 < len ? last + 1 : len;                 // the silent end of the tail trimmed
}

int vfx_clean(const s16 *in, int n, int fs, s8 *raw)
{
    // the take: DC removed, the silence before and after trimmed (a 40 ms margin), the peak to ~90 %
    s32 dc = 0; for (int i = 0; i < n; i++) dc += in[i]; dc = n ? dc / n : 0;
    int thr = 0;
    s32 peak = 1; for (int i = 0; i < n; i++) { s32 a = in[i] - dc; if (a < 0) a = -a; if (a > peak) peak = a; }
    thr = peak / 8;
    int a = 0, b = n - 1, m = fs * 40 / 1000;
    while (a < n && abs(in[a] - dc) < thr) a++;
    while (b > a && abs(in[b] - dc) < thr) b--;
    a -= m; if (a < 0) a = 0; b += m; if (b > n - 1) b = n - 1;
    if (b <= a || peak < 400) return 0;                     // nothing heard
    s32 g = (s32)((s64)115 * 65536 / peak);                 // to +-115
    int k = 0;
    for (int i = a; i <= b; i++) {
        s32 v = (s32)(((s64)(in[i] - dc) * g) >> 16);
        // a short fade in and out (no click at the cut)
        int e = i - a < b - i ? i - a : b - i; if (e < 64) v = v * e / 64;
        raw[k++] = (s8)(v > 127 ? 127 : v < -128 ? -128 : v);
    }
    return k;
}

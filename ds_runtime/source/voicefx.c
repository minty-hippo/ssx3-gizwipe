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
static void shift(const s16 *x, int n, s16 *y, const s32 *ratio_q16, int fs, bool hard)
{
    int L = fs * 30 / 1000; if (L < 64) L = 64;
    s32 Lq = L << 16;
    s32 d = 0;                                             // tap 0's delay, Q16 (tap 1 = d + L/2)
    s64 inv = ((s64)32768 << 32) / (Lq / 2);               // (no 64-bit division per sample: slow on the ARM9)
    for (int i = 0; i < n; i++) {
        if (!(i & 2047)) vfx_idle();
        s32 r = ratio_q16[i / HOP];
        s32 rn = ratio_q16[i / HOP + 1];
        if (!hard) r += (rn - r) / HOP * (i % HOP);        // smooth across the frame (HOP a power of two)
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

// --- the equaliser: tone (a high shelf at 2.5 kHz, -5..+5 x 3 dB), bass (a low shelf at 200 Hz, -5..+5 x 3 dB),
// low-pass (1..10: 4000 Hz down to 300 Hz) and high-pass (1..10: 80 Hz up to 2 kHz), 12 dB/octave each
typedef struct { s32 b0, b1, b2, a1, a2; } Bq;              // Q14
static Bq bq_make(int type, float f0, float gain_db, int fs)
{
    float A = powf(10.0f, gain_db / 40.0f), w = 6.2831853f * f0 / fs, c = cosf(w), sn = sinf(w);
    float b0, b1, b2, a0, a1, a2;
    if (type == 0) {        // low-pass (Q 0.707)
        float al = sn / (2 * 0.7071f);
        b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
    } else if (type == 1) { // high-pass
        float al = sn / (2 * 0.7071f);
        b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al;
    } else {                // 2 low shelf, 3 high shelf (slope 1)
        float al = sn / 2 * sqrtf(2.0f), sa = 2 * sqrtf(A) * al;
        if (type == 2) {
            b0 = A * ((A + 1) - (A - 1) * c + sa); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sa);
            a0 = (A + 1) + (A - 1) * c + sa; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sa;
        } else {
            b0 = A * ((A + 1) + (A - 1) * c + sa); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sa);
            a0 = (A + 1) - (A - 1) * c + sa; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sa;
        }
    }
    Bq q = { (s32)(b0 / a0 * 16384), (s32)(b1 / a0 * 16384), (s32)(b2 / a0 * 16384), (s32)(a1 / a0 * 16384), (s32)(a2 / a0 * 16384) };
    return q;
}
static void bq_run(const Bq *q, s32 *z, int n)
{
    s32 x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (int i = 0; i < n; i++) {
        if (!(i & 4095)) vfx_idle();
        s32 x0 = z[i];
        s64 acc = (s64)q->b0 * x0 + (s64)q->b1 * x1 + (s64)q->b2 * x2 - (s64)q->a1 * y1 - (s64)q->a2 * y2;
        s32 y0 = (s32)(acc >> 14);
        x2 = x1; x1 = x0; y2 = y1; y1 = y0; z[i] = y0;
    }
}
static void eq(s32 *z, int n, int fs, const VoiceFx *fx)
{
    Bq q;
    if (fx->hp > 0) { q = bq_make(1, 80.0f * powf(1.43f, fx->hp - 1), 0, fs); bq_run(&q, z, n); }
    if (fx->lp > 0) { q = bq_make(0, 4000.0f * powf(0.75f, fx->lp - 1), 0, fs); bq_run(&q, z, n); }
    if (fx->bass) { q = bq_make(2, 200.0f, fx->bass * 3.0f, fs); bq_run(&q, z, n); }
    if (fx->tone) { q = bq_make(3, 2500.0f, fx->tone * 3.0f, fs); bq_run(&q, z, n); }
}

int vfx_render(const s8 *raw, int n, int fs, const VoiceFx *fx, s8 *out, int out_max)
{
    if (n <= 0) return 0;
    s16 *x = malloc(n * 2), *y = malloc(n * 2);
    int nf = n / HOP + 2;
    s32 *ratio = malloc(nf * 4);
    if (!x || !y || !ratio) { free(x); free(y); free(ratio); return 0; }
    for (int i = 0; i < n; i++) { x[i] = raw[i] << 8; y[i] = raw[i] << 4; }   // (y: the 1/16 copy pitch detection reads)

    // 1. pitch and autotune: one ratio a frame. Autotune 1..5 pulls each voiced frame toward the nearest semitone
    // (5: all the way); 6..10 also holds a note until the voice moves 0.7 of a semitone off it and snaps to the notes
    // of a major pentatonic scale, retuning at once - the stepped, robotic sound (10: every note held hardest)
    float base = powf(2.0f, fx->pitch / 12.0f);
    float corr = 0; int note = -1000;                    // the correction (semitones) and the held note
    static const u8 PENTA[12] = { 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0 };   // C D E G A
    for (int f = 0; f < nf; f++) {
        if (!(f & 3)) vfx_idle();
        float r = base;
        if (fx->tune > 0) {
            int cl, lag = detect_lag(y, n, f * HOP + HOP / 2 - WIN / 2, fs, &cl);   // (centred on the frame)
            if (lag > 0 && cl > 4096 / 4) {                // voiced (r > 0.5)
                float hz = (float)fs * 256 / lag * base;    // its pitch after the shift
                float m = 12.0f * log2f(hz / 440.0f) + 69.0f;
                if (fx->tune <= 5) {
                    float t = floorf(m + 0.5f);
                    corr = (t - m) * fx->tune / 5.0f;
                } else {
                    float hyst = 0.5f + 0.04f * (fx->tune - 5);       // 0.54 .. 0.7 semitone
                    if (note < -999 || fabsf(m - note) > hyst + 1.0f) {
                        int t = (int)floorf(m + 0.5f), best = t;
                        for (int d = 0; d < 3; d++) {                // the nearest pentatonic note
                            if (PENTA[((t - d) % 12 + 12) % 12]) { best = t - d; break; }
                            if (PENTA[((t + d) % 12 + 12) % 12]) { best = t + d; break; }
                        }
                        note = best;
                    }
                    corr = note - m;
                }
            } else note = -1000;
            r *= powf(2.0f, corr / 12.0f);
        }
        ratio[f] = (s32)(r * 65536.0f);
    }
    if (fx->tune > 5) ratio[nf - 1] = ratio[nf - 2];
    TMARK(0);
    bool shifted = fx->pitch != 0 || fx->tune > 0;
    if (shifted) shift(x, n, y, ratio, fs, fx->tune > 5); else memcpy(y, x, n * 2);

    TMARK(1);
    // 2. choral: two more voices a little sharp and flat (+-14 cents x the slider / 5, at most +-28), 25 and 38 ms
    // behind, each slowly swept, mixed in up to as loud as the voice itself - an ensemble rather than a faint comb
    int len = n;
    int echo_d = fx->echo > 0 ? fs * (40 + fx->echo_ms * 45) / 1000 : 0;   // 85..490 ms
    int tail = fx->echo > 0 ? echo_d * (2 + fx->echo / 3) : 0;
    if (tail > fs) tail = fs;
    len = n + tail; if (len > out_max) len = out_max;
    s32 *z = malloc(len * 4);
    if (!z) { free(x); free(y); free(ratio); return 0; }
    for (int i = 0; i < len; i++) z[i] = i < n ? y[i] : 0;
    if (fx->chorus > 0) {
        s16 *v = malloc(n * 2);
        if (v) {
            static const int dly_ms[2] = { 25, 38 }, rate_mhz[2] = { 900, 1300 };
            s32 wet = fx->chorus * 4096 / 10, dry = 4096 - fx->chorus * 4096 / 30;
            for (int k = 0; k < 2; k++) {
                float cents = (k ? -14.0f : 14.0f) * (fx->chorus < 5 ? 1.0f : fx->chorus / 5.0f);
                s32 rq = (s32)(powf(2.0f, cents / 1200.0f) * 65536.0f);
                for (int f = 0; f < nf; f++) ratio[f] = rq;
                shift(y, n, v, ratio, fs, false);
                u32 ph = (u32)k * 0x80000000u, inc = (u32)((((u64)rate_mhz[k]) << 32) / 1000 / fs);
                s32 dbase = (s32)(((s64)fs * dly_ms[k] << 16) / 1000), depth = (s32)(((s64)fs * 2 << 16) / 1000);
                for (int i = 0; i < n; i++) {
                    if (!(i & 2047)) vfx_idle();
                    ph += inc;
                    s32 pos = (i << 16) - dbase - (s32)(((s64)depth * lfo((s32)(ph >> 16))) >> 12);
                    if (pos < 0) continue;
                    int p = pos >> 16; s32 fr = pos & 0xffff; s32 a0 = v[p], b0 = p + 1 < n ? v[p + 1] : 0;
                    z[i] += (s32)((((s64)(a0 + (s32)(((s64)(b0 - a0) * fr) >> 16))) * wet) >> 12) / 2;
                }
            }
            for (int i = 0; i < n; i++) z[i] = (s32)(((s64)z[i] * dry) >> 12);
            free(v);
        }
    }
    // 2b. tone, bass and the filters: biquads (RBJ cookbook, Q14 coefficients) over the voice
    eq(z, len, fs, fx);
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

// SSX 3 GizWipe - voice clips (port addition). The microphone records 16-bit at 11025 Hz into a double buffer on
// the ARM7; each finished half is copied into the take here (from the FIFO interrupt). A take is trimmed and
// levelled to 8-bit (vfx_clean), kept as the slot's raw clip, and rendered through its effects (vfx_render) into
// the clip that plays. Saved as ssx3gizwipe_voice<n>.vc: "SVC2", u16 rate, u16 length, VoiceFx (12 bytes), raw samples (SVC1: an 8-byte VoiceFx, without tone, bass and
// the filters).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "voice.h"
#include "frontend.h"

const char *const VC_NAME[VC_N] = { "SuperUber", "Race Win", "Passing", "Knockdown" };
static struct { s8 *raw; int n; s8 *out; int nout; VoiceFx fx; } C[VC_N];
static bool fat;
static int play_ch = -1;
static const VoiceFx FX0 = { 0, 0, 0, 0, 5, 10, 0, 0, 0, 0, { 0, 0 } };
static int play_left;                                       // frames the clip still plays (voice_tick)

int voice_dbg[2];
bool voice_fat(void) { return fat; }
bool voice_has(int s) { return s >= 0 && s < VC_N && C[s].nout > 0; }
int voice_ms(int s) { return s >= 0 && s < VC_N ? C[s].n * 1000 / VC_RATE : 0; }
VoiceFx *voice_fx(int s) { return &C[s].fx; }

void voice_apply(int s)
{
    free(C[s].out); C[s].out = NULL; C[s].nout = 0;
    if (!C[s].raw || !C[s].n) return;
    int cap = C[s].n + VC_RATE;                             // room for the echo's tail
    s8 *o = malloc(cap);
    if (!o) return;
#ifdef TEST_VOICE
    cpuStartTiming(2);
#endif
    int k = vfx_render(C[s].raw, C[s].n, VC_RATE, &C[s].fx, o, cap);
#ifdef TEST_VOICE
    voice_dbg[1] = (int)(cpuEndTiming() / 33514);   // ms
#endif
    s8 *t = realloc(o, k > 0 ? k : 1); if (t) o = t;
    C[s].out = o; C[s].nout = k;
    DC_FlushRange(o, k);
}

void voice_stop(void) { if (play_ch >= 0) soundKill(play_ch); play_ch = -1; }
void voice_play(int s)
{
    if (!voice_has(s)) return;
    voice_stop();
    int vol = 127 * C[s].fx.vol / 10 * g_opt.master / 10;
    if (vol <= 0) return;
    play_ch = soundPlaySample(C[s].out, SoundFormat_8Bit, C[s].nout & ~3, VC_RATE, vol, 64, false, 0);
    play_left = C[s].nout * 60 / VC_RATE + 6;
}
bool voice_tick(void) { if (play_left > 0) play_left--; return play_left > 0; }

static void path(char *b, int n, int s) { snprintf(b, n, "ssx3gizwipe_voice%d.vc", s); }
void voice_save(int s)
{
    if (!fat) return;
    char p[40]; path(p, sizeof(p), s);
    if (!C[s].n) { remove(p); return; }
    FILE *f = fopen(p, "wb");
    if (!f) return;
    u16 h[2] = { VC_RATE, (u16)C[s].n };
    fwrite("SVC2", 4, 1, f); fwrite(h, 2, 2, f); fwrite(&C[s].fx, sizeof(VoiceFx), 1, f); fwrite(C[s].raw, 1, C[s].n, f);
    fclose(f);
}
void voice_clear(int s)
{
    free(C[s].raw); free(C[s].out); C[s].raw = C[s].out = NULL; C[s].n = C[s].nout = 0;
    voice_save(s);
}

void voice_init(bool have_fat)
{
    fat = have_fat;
    for (int s = 0; s < VC_N; s++) {
        C[s].fx = FX0;
        if (!fat) continue;
        char p[40]; path(p, sizeof(p), s);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        char m[4]; u16 h[2]; VoiceFx fx = FX0;
        int v = fread(m, 4, 1, f) == 1 && !memcmp(m, "SVC", 3) ? m[3] - '0' : 0;
        if ((v == 1 || v == 2) && fread(h, 2, 2, f) == 2 && fread(&fx, v == 1 ? 8 : sizeof(fx), 1, f) == 1
            && h[1] > 0 && h[1] <= VC_MAX && (C[s].raw = malloc(h[1])) && fread(C[s].raw, 1, h[1], f) == h[1]) {
            C[s].n = h[1]; C[s].fx = fx;
            if (v == 1) { C[s].fx.tone = C[s].fx.bass = 0; C[s].fx.lp = C[s].fx.hp = 0; }
            if (C[s].fx.vol > 10) C[s].fx.vol = 10;
            voice_apply(s);
        } else { free(C[s].raw); C[s].raw = NULL; }
        fclose(f);
    }
#ifdef TEST_VOICE
    for (int s = 0; s < VC_N; s++) if (!C[s].n && (C[s].raw = malloc(VC_RATE))) {   // test builds: a buzz in each slot
        int ph = 0; C[s].n = VC_RATE * 2 / 3;
        for (int i = 0; i < C[s].n; i++) { ph += (200 + 60 * s) * 65536 / VC_RATE; C[s].raw[i] = (s8)(((ph & 0xffff) - 0x8000) >> 9); }
        voice_apply(s);
    }
#endif
}

// ---- recording
enum { HALF = 512 };                                       // samples a half buffer (46 ms)
static s16 mic_buf[HALF * 2] __attribute__((aligned(32)));
static s16 *take; static volatile int take_n, take_lvl; static volatile bool rec_on;
static int rec_slot = -1;
static void mic_cb(void *data, int bytes)
{
    if (!rec_on || !take) return;
    DC_InvalidateRange(data, bytes);
    const s16 *d = data; int n = bytes / 2, pk = 0;
    for (int i = 0; i < n && take_n < VC_MAX; i++) {
        s16 v = d[i]; take[take_n++] = v;
        int a = v < 0 ? -v : v; if (a > pk) pk = a;
    }
    take_lvl = pk * 100 / 20000 > 100 ? 100 : pk * 100 / 20000;
    if (take_n >= VC_MAX) rec_on = false;
}
bool voice_record(int s)
{
    if (rec_on) return false;
    voice_stop();
    free(take); take = malloc(VC_MAX * 2);
    if (!take) return false;
    take_n = 0; take_lvl = 0; rec_slot = s;
    rec_on = true;
    if (!soundMicRecord(mic_buf, sizeof(mic_buf), MicFormat_12Bit, VC_RATE, mic_cb)) { rec_on = false; free(take); take = NULL; return false; }
    return true;
}
bool voice_recording(void) { return take != NULL; }
int voice_rec_ms(void) { return take_n * 1000 / VC_RATE; }
int voice_rec_level(void) { return take_lvl; }
bool voice_record_end(void)
{
    if (!take) return false;
    rec_on = false;
    soundMicOff();
#ifdef TEST_VOICE
    {   // test builds: a 1.5 s gliding buzz in place of the microphone (the emulator's is silent)
        int ph = 0; take_n = VC_RATE * 3 / 2;
        for (int i = 0; i < take_n; i++) { ph += (180 + 80 * i / take_n) * 65536 / VC_RATE; take[i] = (s16)(((ph & 0xffff) - 0x8000) / 4); }
    }
#endif
    int s = rec_slot; bool ok = false;
    s8 *raw = malloc(take_n > 0 ? take_n : 1);
    int n = raw ? vfx_clean(take, take_n, VC_RATE, raw) : 0;
#ifdef TEST_VOICE
    voice_dbg[0] = take_n; voice_dbg[1] = raw ? n : -1;
#endif
    free(take); take = NULL;
    if (n > VC_RATE / 10) {                                 // at least 0.1 s heard
        s8 *t = realloc(raw, n); if (t) raw = t;
        free(C[s].raw); C[s].raw = raw; C[s].n = n;
        voice_apply(s); voice_save(s); ok = true;
    } else free(raw);
    return ok;
}

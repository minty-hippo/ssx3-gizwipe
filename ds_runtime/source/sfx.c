// SSX 3 GizWipe - sound effects. Ids index the exe's sound table at VA 0xcc338 (names below); the bank was
// convert[...]'s snd/*.wav to DS IMA-ADPCM by tools/ds_audio.py.
#include <stdio.h>
#include "xtext.h"
#include <stdlib.h>
#include <string.h>
#include "sfx.h"

static const char *const SND_NAME[36] = {
    "CursorMove", XT_289, XT_290, "Scroll", "PauseMenu", "Boost", "BoostRunOut", "Count",
    XT_291, "Collectable", "Respawn", XT_292, "Start", "Knockdown", "Flare", "Firework",
    "Tweak", "Ping", "Crowd", "Collide", "IceCarve", "IceLand", "Jump", "PackedLand", "PackedCarve",
    XT_293, "PowderLand", "PowderCarve", "Punch", "RailLand", "RailLeave", "RailLoop", "Wipeout1",
    "Wipeout2", "Wind", "Swing" };

typedef struct { char name[20]; u16 timer, rate; u32 off, bytes; u16 loop_start; u8 flags, pad; } SfxRec;   // 36
static u8 *bank; static SfxRec *rec; static int nrec;
static s16 by_id[36];
static int loop_ch = -1, loop_id = -1;

bool sfx_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    bank = malloc(n);
    if (!bank || fread(bank, 1, n, f) != (size_t)n) { fclose(f); return false; }
    fclose(f);
    if (memcmp(bank, "SSFX", 4)) return false;
    nrec = *(u32 *)(bank + 8); rec = (SfxRec *)(bank + 12);
    for (int i = 0; i < 36; i++) {
        by_id[i] = -1;
        for (int k = 0; k < nrec; k++) if (!strcmp(rec[k].name, SND_NAME[i])) by_id[i] = k;
    }
    DC_FlushRange(bank, n);
    return true;
}

static int play(int id, bool loop, int vol)
{
    if (!bank || id < 0 || id >= 36 || by_id[id] < 0) return -1;
    const SfxRec *r = &rec[by_id[id]];
    // ADPCM loops restart after the 4-byte header (loop point in words)
    return soundPlaySample(bank + r->off, SoundFormat_ADPCM, r->bytes, r->rate, vol, 64, loop, loop ? 1 : 0);
}

static int vol_pct = 100;                // Options: master x fx, 0..100
void sfx_volume(int pct) { vol_pct = pct; if (loop_ch >= 0) soundSetVolume(loop_ch, 70 * vol_pct / 100); }
void sfx_play(int id) { if (vol_pct) play(id, false, 110 * vol_pct / 100); }

void sfx_loop(int id)
{
    if (id == loop_id) return;
    if (loop_ch >= 0) soundKill(loop_ch);
    loop_ch = -1; loop_id = id;
    if (id >= 0 && vol_pct) loop_ch = play(id, true, 70 * vol_pct / 100);
}

void sfx_play_name(const char *name)
{
    for (int k = 0; k < nrec; k++) if (!strcmp(rec[k].name, name)) {
        const SfxRec *r = &rec[k];
        soundPlaySample(bank + r->off, SoundFormat_ADPCM, r->bytes, r->rate, 110 * vol_pct / 100, 64, false, 0);
        return;
    }
}

void sfx_stop_all(void)
{
    sfx_loop(-1);
}

// port (Voice Clips): a sound decoded to 16-bit PCM at `rate` (IMA-ADPCM as the DS plays it, resampled linearly)
// so it can be mixed into a clip; returns the samples written (at most max)
int sfx_pcm(int id, int rate, s16 *out, int max)
{
    static const s16 STEP[89] = { 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55,
        60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494,
        544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
        3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289,
        16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767 };
    static const s8 IDX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
    if (!bank || id < 0 || id >= 36 || by_id[id] < 0 || max <= 0) return 0;
    const SfxRec *r = &rec[by_id[id]];
    const u8 *d = bank + r->off;
    int pred = (s16)(d[0] | d[1] << 8), ix = d[2] > 88 ? 88 : d[2];
    int nsrc = (r->bytes - 4) * 2, src_rate = r->rate ? r->rate : 22050;
    s32 pos = 0, inc = (s32)(((s64)src_rate << 16) / rate);         // source samples a target sample, Q16
    int have = -1, k = 0; s32 prev = pred, cur = pred;
    for (int i = 0; i < max; i++, pos += inc) {
        int want = pos >> 16;
        if (want >= nsrc) break;
        while (have < want) {                                       // decode up to the source sample wanted
            have++;
            int nib = (d[4 + (have >> 1)] >> ((have & 1) * 4)) & 15, st = STEP[ix], df = st >> 3;
            if (nib & 1) df += st >> 2;
            if (nib & 2) df += st >> 1;
            if (nib & 4) df += st;
            pred += (nib & 8) ? -df : df;
            if (pred > 32767) pred = 32767;
            if (pred < -32768) pred = -32768;
            ix += IDX[nib & 7];
            if (ix < 0) ix = 0;
            if (ix > 88) ix = 88;
            prev = cur; cur = pred;
        }
        s32 f = pos & 0xffff;
        out[k++] = (s16)(prev + (((cur - prev) * (f >> 4)) >> 12));
    }
    return k;
}

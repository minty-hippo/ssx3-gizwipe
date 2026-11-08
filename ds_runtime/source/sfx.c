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

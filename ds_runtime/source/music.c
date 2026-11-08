// SSX 3 GizWipe - streamed music (.ssmu: independent IMA-ADPCM blocks) -> looping PCM16 ring buffer
#include <nds.h>
#include "xtext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "music.h"
static int duck_cur = 100, duck_to = 100; static bool vol_mute;
static int eff_vol(void);

#define RING 32768                       // samples (2 s at 16 kHz)
#define LOOKAHEAD 12288                  // keep this many samples decoded ahead of the play head

static const s16 step_tab[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767 };
static const s8 idx_tab[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct { char magic[4]; u32 ver, rate, block_bytes, spb, nblocks, total; } MuHdr;

static struct {
    FILE *f; MuHdr h; u8 *blk; s16 *ring; int ch;
    u32 block;                           // next block to read
    u32 pcm_n, k;                        // samples in the current block, samples decoded from it so far
    int pred, idx; u32 bi; int half;     // the decoder's state inside the block (byte, nibble)
    u64 written, start_vb; int vol;
    int pause_depth; u64 played;          // music_pause / music_resume
    int song;                             // playlist index (-1: a single file, looped)
} M = { .song = -1 };

// port: the exe's play list (0x606f4's table at 0xdd9c8; titles / artists at 0xa3a40, 0x80 bytes each). The exe
// picks the next song at random among the enabled ones (0x63604, play mode 24 = shuffle, all enabled by default)
// when the front end starts and whenever one ends; the music runs on through menus and races.
static const u8 SONG_FILE[24] = { 1, 5, 6, 7, 8, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 22, 23, 24, 25, 26, 27, 28, 29, 30 };
static const char *const SONG_NAME[24] = {               // "artist - title", as the exe's play list shows them
    "Andy Hunter - Go", XT_109 " - " XT_110, "Deep Sky - Ride",
    XT_111 " - No One Knows (Unkle Remix)", "Yellowcard - Way Away", "Overseer - Screw Up",
    "Black Eyed Peas - " XT_112, XT_113 " - Hypersonic", "MXPX - Play It Loud",
    "Dilated Peoples - Who's Who", XT_114 " - Leave Home", "Fatboy Slim - Don't L" XT_115,
    "Thrice - " XT_116, "Autopilot Off - Clockworks", "Placebo - The " XT_117,
    "Finger Eleven - Good Times", "Royksopp - Poor Leno", "Swollen Members - All Night", "Swollen Members - Deep End",
    "The Faint - Glass Dance", "X-ecutioners - Like This", "Alpine Stars - Snow Patrol", "Caesars - Jerk It Out",
    "Fischerspooner - Emerge" XT_118 };
const char *music_song_name(void) { return M.song >= 0 && M.song < 24 ? SONG_NAME[M.song] : ""; }
static bool open_song(int i)
{
    char path[48]; snprintf(path, sizeof(path), "nitro:/music/Music_%02d.ssmu", SONG_FILE[i]);
    FILE *f = fopen(path, "rb"); MuHdr h;
    if (!f || fread(&h, sizeof(h), 1, f) != 1 || memcmp(h.magic, "SSMU", 4) || (M.f && h.block_bytes != M.h.block_bytes)) { if (f) fclose(f); return false; }
    if (M.f) fclose(M.f);
    M.f = f; M.h = h; M.block = 0; M.song = i;
    return true;
}
static int pick_next(void)
{
    int n = rand() % 24;
    if (n == M.song) n = (n + 1 + rand() % 23) % 24;    // (the exe redraws until it differs)
    return n;
}

// IMA-ADPCM, decoded straight into the ring a little at a time (it was a whole block at once: ~16K samples in one
// frame, a dropped frame about once a second while racing)
ITCM_CODE static void decode_some(s16 *out, u32 n)
{
    const u8 *b = M.blk; int pred = M.pred, idx = M.idx; u32 i = M.bi; int h = M.half;
    for (u32 j = 0; j < n; j++) {
        if (M.k == 0) { out[j] = pred; M.k = 1; continue; }
        int code = h ? (b[i] >> 4) : (b[i] & 15), step = step_tab[idx], d = step >> 3;
        if (h) i++;
        h ^= 1;
        if (code & 4) d += step;
        if (code & 2) d += step >> 1;
        if (code & 1) d += step >> 2;
        pred += (code & 8) ? -d : d;
        if (pred > 32767) pred = 32767; else if (pred < -32768) pred = -32768;
        idx += idx_tab[code & 7]; if (idx < 0) idx = 0; else if (idx > 88) idx = 88;
        out[j] = pred; M.k++;
    }
    M.pred = pred; M.idx = idx; M.bi = i; M.half = h;
}

static bool next_block(void)
{
    if (M.block >= M.h.nblocks) {                                 // the song is over
        if (M.song < 0 || !open_song(pick_next())) M.block = 0;  // the next from the play list (a lone file loops)
    }
    fseek(M.f, sizeof(MuHdr) + (u32)M.block * M.h.block_bytes, SEEK_SET);
    if (fread(M.blk, 1, M.h.block_bytes, M.f) != M.h.block_bytes) return false;
    u32 n = M.h.spb;
    if (M.block == M.h.nblocks - 1) { u32 rem = M.h.total - M.block * M.h.spb; if (rem < n) n = rem; }
    M.pcm_n = n; M.k = 0; M.block++;
    M.pred = (s16)(M.blk[0] | (M.blk[1] << 8)); M.idx = M.blk[2] > 88 ? 88 : M.blk[2]; M.bi = 4; M.half = 0;
    return true;
}

static u64 play_pos(u32 vblanks)                               // samples played since start (estimate)
{
    // DS sound timer runs at 33513982/2 / round(16756991/rate); vblank = 59.8261 Hz
    return (u64)(vblanks - M.start_vb) * M.h.rate * 10000 / 598261;
}

bool music_play(const char *path, u32 vblanks)
{
    music_stop();
    M.f = fopen(path, "rb");
    if (!M.f || fread(&M.h, sizeof(MuHdr), 1, M.f) != 1 || memcmp(M.h.magic, "SSMU", 4)) return false;
    M.blk = malloc(M.h.block_bytes); M.ring = malloc(RING * 2);
    if (!M.blk || !M.ring) return false;
    M.block = 0; M.written = 0; M.pcm_n = 0; M.k = 0;
    music_update(vblanks, true);
    DC_FlushRange(M.ring, RING * 2);
    M.ch = soundPlaySample(M.ring, SoundFormat_16Bit, RING * 2, M.h.rate, eff_vol(), 64, true, 0);
    M.start_vb = vblanks;
    return true;
}

ITCM_CODE void music_update(u32 vblanks, bool prime)
{
    if (!M.f || M.pause_depth) return;
    u64 target = (prime ? 0 : play_pos(vblanks)) + LOOKAHEAD;
    while (M.written < target) {
        if (M.k >= M.pcm_n && !next_block()) return;
        u32 n = M.pcm_n - M.k, pos = M.written % RING;
        if (n > RING - pos) n = RING - pos;
        if (n > target - M.written) n = target - M.written;
        decode_some(M.ring + pos, n);
        DC_FlushRange(M.ring + pos, n * 2);
        M.written += n;
    }
}

void music_stop(void)
{
    if (M.f) { if (!M.pause_depth) soundKill(M.ch); fclose(M.f); }
    free(M.blk); free(M.ring);
    int v = M.vol; memset(&M, 0, sizeof(M)); M.vol = v; M.song = -1;
}

// the play list from song i (0..23), then shuffled
bool music_playlist(int i, u32 vblanks)
{
    char path[48]; snprintf(path, sizeof(path), "nitro:/music/Music_%02d.ssmu", SONG_FILE[i % 24]);
    if (!music_play(path, vblanks)) return false;
    M.song = i % 24;
    return true;
}
int music_song(void) { return M.song; }
bool music_next(u32 vblanks) { return music_playlist(pick_next(), vblanks); }   // a random different song, now

// port: held still around a load (the ring kept looping its last 2 s while nothing refilled it - the skip on
// entering a course): the channel stops where it is, and resumes from the same sample
void music_pause(u32 vblanks)
{
    if (!M.f || M.pause_depth++) return;
    u64 p = play_pos(vblanks);
    M.played = p < M.written ? p : M.written;
    soundKill(M.ch);
}
void music_resume(u32 vblanks)
{
    if (!M.f || !M.pause_depth || --M.pause_depth) return;
    u32 n = (u32)(M.written - M.played); if (n > RING) n = RING;
    s16 *t = malloc(n * 2 + 2);
    if (t) {                                                       // the samples not yet heard, to the ring's start
        for (u32 j = 0; j < n; j++) t[j] = M.ring[(M.played + j) % RING];
        memcpy(M.ring, t, n * 2); free(t);
    } else n = 0;
    M.written = n;
    music_update(vblanks, true);
    DC_FlushRange(M.ring, RING * 2);
    M.ch = soundPlaySample(M.ring, SoundFormat_16Bit, RING * 2, M.h.rate, eff_vol(), 64, true, 0);
    M.start_vb = vblanks;
}

// Options: the master volume (0..100 % of the normal 90)
void music_volume(int pct)
{
    M.vol = 90 * pct / 100; if (!M.vol) M.vol = 1;          // (0 means "default" to music_play)
    vol_mute = !pct;
    if (M.f && M.ring && !M.pause_depth) soundSetVolume(M.ch, eff_vol());
}

// port: fade the music down (the Voice Clips screen, so the takes are heard) and back up: 0..100 % of the volume,
// eased ~3 % a frame by music_duck_step
void music_duck(int pct) { duck_to = pct < 0 ? 0 : pct > 100 ? 100 : pct; }
void music_duck_step(void)
{
    if (duck_cur == duck_to) return;
    duck_cur += duck_cur < duck_to ? 3 : -3;
    if (duck_cur < 0) duck_cur = 0;
    if (duck_cur > 100) duck_cur = 100;
    if (abs(duck_cur - duck_to) < 3) duck_cur = duck_to;
    if (M.f && M.ring && !M.pause_depth) soundSetVolume(M.ch, eff_vol());
}

static int eff_vol(void)                                    // the volume a channel plays at (the duck applied)
{
    if (vol_mute) return 0;
    int v = (M.vol ? M.vol : 90) * duck_cur / 100;
    return v;
}

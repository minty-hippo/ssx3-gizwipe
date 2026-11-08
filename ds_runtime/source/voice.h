// SSX 3 GizWipe - voice clips (port addition): short takes recorded with the DS microphone that play on race
// moments, each with its own effects (voicefx.c). Kept next to the ROM when there is a FAT card.
#pragma once
#include <nds.h>
#include "voicefx.h"
enum { VC_UBER, VC_WIN, VC_PASS, VC_KNOCK, VC_N };
enum { VC_RATE = 11025, VC_MAX = VC_RATE * 5 / 2 };       // 2.5 s a take
extern const char *const VC_NAME[VC_N];
void voice_init(bool fat);                 // load the saved clips (after fatInitDefault)
bool voice_has(int slot);
int  voice_ms(int slot);                   // the take's length
VoiceFx *voice_fx(int slot);
void voice_apply(int slot);                // re-render after the sliders change
void voice_play(int slot);                 // the clip with its effects (fx volume)
void voice_stop(void);
bool voice_tick(void);                     // once a frame: true while a clip plays (the music ducks under it)
bool voice_record(int slot);               // start a take (stops by itself at VC_MAX); false: no microphone
bool voice_recording(void);
int  voice_rec_ms(void);                   // how long the take has run
int  voice_rec_level(void);                // the input's level 0..100 (a meter while recording)
bool voice_record_end(void);               // stop: the take replaces the slot's clip if anything was heard
void voice_clear(int slot);
void voice_save(int slot);
bool voice_fat(void);

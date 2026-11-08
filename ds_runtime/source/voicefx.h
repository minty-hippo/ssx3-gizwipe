// SSX 3 GizWipe - voice clip effects (voicefx.c)
#pragma once
#ifdef HOSTTEST
#include <stdint.h>
#include <stdbool.h>
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64; typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
#include <stdbool.h>
#else
#include <nds.h>
#endif
// pitch -12..12 semitones; tone, bass -5..5; lp, hp 0 (off)..10; the rest 0..10
typedef struct { s8 pitch; u8 tune, chorus, echo, echo_ms, vol; s8 tone, bass; u8 lp, hp, bank[2]; } VoiceFx;   // bank: game sounds (voice.c)
// the take (16-bit mic samples) cleaned to 8-bit: DC, silence trimmed, level; returns its length (0: nothing heard)
int vfx_clean(const s16 *in, int n, int fs, s8 *raw);
// the clip with its effects into out (at most out_max samples: room for the echo's tail); returns the length
int vfx_render(const s8 *raw, int n, int fs, const VoiceFx *fx, s8 *out, int out_max);

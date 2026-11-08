// SSX 3 GizWipe - voice clip effects (voicefx.c)
#pragma once
#ifdef HOSTTEST
#include <stdint.h>
#include <stdbool.h>
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64; typedef uint8_t u8; typedef uint32_t u32; typedef uint64_t u64;
#else
#include <nds.h>
#endif
typedef struct { s8 pitch; u8 tune, chorus, echo, echo_ms, vol, pad[2]; } VoiceFx;   // pitch -12..12 semitones, the rest 0..10
// the take (16-bit mic samples) cleaned to 8-bit: DC, silence trimmed, level; returns its length (0: nothing heard)
int vfx_clean(const s16 *in, int n, int fs, s8 *raw);
// the clip with its effects into out (at most out_max samples: room for the echo's tail); returns the length
int vfx_render(const s8 *raw, int n, int fs, const VoiceFx *fx, s8 *out, int out_max);

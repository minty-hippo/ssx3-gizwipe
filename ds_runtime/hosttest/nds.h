// host stub of the bits of libnds used by track.c / physics.c, for desktop testing of the physics
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef int8_t s8; typedef uint8_t u8; typedef int16_t s16; typedef uint16_t u16;
typedef int32_t s32; typedef uint32_t u32; typedef int64_t s64; typedef uint64_t u64;
#define TEXGEN_TEXCOORD 0
#define GL_TEXTURE_WRAP_S 0
#define GL_TEXTURE_WRAP_T 0
#define GL_TEXTURE_COLOR0_TRANSPARENT 0
#define GL_RGB256 0
static inline void glGenTextures(int n, int *t) { for (int i = 0; i < n; i++) t[i] = i + 1; }
static inline void glDeleteTextures(int n, int *t) { (void)n; (void)t; }
static inline void glBindTexture(int a, int b) { (void)a; (void)b; }
static inline int glTexImage2D(int a, int b, int c, int d, int e, int f, int g, void *h) { return 1; }
static inline int glColorTableEXT(int a, int b, int c, int d, int e, void *f) { return 1; }
static inline void glPushMatrix(void) {}
static inline void glPopMatrix(int n) {}
static inline void glTranslatef32(int a, int b, int c) {}
static inline void glScalef32(int a, int b, int c) {}
static inline void glCallList(const void *p) {}
#define inttof32(x) ((x) << 12)
#define RGB15(r,g,b) ((r)|((g)<<5)|((b)<<10))

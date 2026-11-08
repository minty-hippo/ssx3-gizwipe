// SSX 3 GizWipe - top-screen HUD: the Gizmondo HUD sprite sheet and font (hud.ssxh, tools/ds_hud.py), drawn as
// textured quads in an orthographic pass after the 3D scene. Coordinates are DS pixels (256 x 192).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hud.h"
#include "lang.h"
#include <math.h>

typedef struct { u8 ch, w, h, pad, x, y; u16 pad2; } Glyph;
typedef struct { int tex; Glyph glyphs[256]; u8 gmap[256]; int n; u8 *bits; } Font;   // bits: a RAM copy (2 bpp) for software text
enum { NFONT = 4 };
static Font fonts[NFONT]; static Font *cur;
static int tex_sheet;
static bool ok;

// fonts: 0 fontssxbig (race HUD), 1 mainfont (menu titles), 2 midfont (menu items), 3 smallestfont (help text)
static void read_font(FILE *f, Font *ft)
{
    u8 *font = malloc(64 * 128); u16 fpal[4], n = 0;
    fread(font, 1, 256 * 128 / 4, f); fread(fpal, 2, 4, f);
    fread(&n, 2, 1, f);
    ft->n = n > 256 ? 256 : n;
    fread(ft->glyphs, sizeof(Glyph), ft->n, f);
    if (n > 256) fseek(f, (n - 256) * sizeof(Glyph), SEEK_CUR);
    memset(ft->gmap, 0xff, sizeof(ft->gmap));
    for (int i = 0; i < ft->n; i++) ft->gmap[ft->glyphs[i].ch] = i;
    glGenTextures(1, &ft->tex); glBindTexture(0, ft->tex);
    glTexImage2D(0, 0, GL_RGB4, 256, 128, 0, TEXGEN_TEXCOORD | GL_TEXTURE_COLOR0_TRANSPARENT, font);
    glColorTableEXT(0, 0, 4, 0, 0, fpal);
    ft->bits = font;                                     // kept for hud_soft_text
}
void hud_font(int i) { if (i >= 0 && i < NFONT && fonts[i].tex) cur = &fonts[i]; }

bool hud_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char magic[4]; u32 ver;
    fread(magic, 4, 1, f); fread(&ver, 4, 1, f);
    int rows = ver >= 3 ? 256 : 128;                      // version 3 keeps the boost meter coils (rows 166..255)
    u8 *sheet = malloc(256 * rows);
    u16 *spal = malloc(512);
    fread(sheet, 1, 256 * rows, f); fread(spal, 2, 256, f);
    glGenTextures(1, &tex_sheet); glBindTexture(0, tex_sheet);
    glTexImage2D(0, 0, GL_RGB256, 256, rows, 0, TEXGEN_TEXCOORD | GL_TEXTURE_COLOR0_TRANSPARENT, sheet);
    glColorTableEXT(0, 0, 256, 0, 0, spal);
    free(sheet); free(spal);
    read_font(f, &fonts[0]);
    u16 extra = 0;
    if (ver >= 2 && fread(&extra, 2, 1, f) == 1)
        for (int i = 0; i < extra && i + 1 < NFONT; i++) read_font(f, &fonts[i + 1]);
    fclose(f);
    cur = &fonts[0];
    ok = memcmp(magic, "SSXH", 4) == 0;
    return ok;
}

// eye z: larger = nearer (ortho near plane at +4). Shadows sit behind the text: with equal depth the DS depth test
// (strictly less) keeps whichever was drawn first, so the white text could come out black on some renderers.
static v16 Z = floattov16(3.5f);
#define Z_SPRITE floattov16(3.5f)
#define Z_SHADOW floattov16(3.6f)
#define Z_TEXT   floattov16(3.7f)
static void quad(int x, int y, int w, int h, int u, int v, int tw, int th)
{
    glTexCoord2t16(inttot16(u), inttot16(v));             glVertex3v16(x * 64, y * 64, Z);
    glTexCoord2t16(inttot16(u), inttot16(v + th));        glVertex3v16(x * 64, (y + h) * 64, Z);
    glTexCoord2t16(inttot16(u + tw), inttot16(v + th));   glVertex3v16((x + w) * 64, (y + h) * 64, Z);
    glTexCoord2t16(inttot16(u + tw), inttot16(v));        glVertex3v16((x + w) * 64, y * 64, Z);
}

void hud_begin(void)
{
    if (!ok) return;
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrthof32(0, inttof32(4), inttof32(3), 0, inttof32(-4), inttof32(4));   // 1 unit = 64 pixels
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(5));
}

void hud_end(void)
{
    if (!ok) return;
    glMatrixMode(GL_PROJECTION); glPopMatrix(1);
    glMatrixMode(GL_MODELVIEW); glPopMatrix(1);
    glColor(RGB15(31, 31, 31));
}

static bool sprite_top;
void hud_sprite_over(bool on) { sprite_top = on; }      // sprites laid over other sprites (the lit SUPER UBER letters)
ITCM_CODE void hud_sprite(int x, int y, int w, int h, int u, int v, int dw, int dh, u16 color)
{
    if (!ok) return;
    glBindTexture(0, tex_sheet);
    glColor(color);
    Z = sprite_top ? floattov16(3.6f) : Z_SPRITE;
    glBegin(GL_QUADS);
    quad(x, y, dw, dh, u, v, w, h);
    glEnd();
}

int hud_text_width(const char *s)
{
    s = L(s);
    int w = 0;
    for (; *s; s++) { u8 g = cur->gmap[(u8)*s]; w += g == 0xff ? 4 : cur->glyphs[g].w + 1; }
    return w;
}

// align: 0 left, 1 right, 2 centre (the exe's 0x79d2c)
static bool shadow = true, outline;
static void text_pass(int x, int y, const char *s, u16 color);
void hud_outline(bool on) { outline = on; }       // a dark outline all round (text over white snow / fog)
ITCM_CODE void hud_text(int x, int y, int align, const char *s, u16 color)
{
    if (!ok) return;
    s = L(s);                                    // the chosen language (lang.c)
    if (align == 1) x -= hud_text_width(s);
    else if (align == 2) x -= hud_text_width(s) / 2;
    glBindTexture(0, cur->tex);
    if (outline) {
        static const s8 O[5][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { 1, 1 } };
        Z = Z_SHADOW; for (int k = 0; k < 5; k++) text_pass(x + O[k][0], y + O[k][1], s, RGB15(3, 3, 6));
    } else if (shadow) { Z = Z_SHADOW; text_pass(x + 1, y + 1, s, RGB15(2, 2, 4)); }   // drop shadow: the font's black background is keyed out
    Z = Z_TEXT;   text_pass(x, y, s, color);
}

static void text_pass(int x, int y, const char *s, u16 color)
{
    glColor(color);
    glBegin(GL_QUADS);
    for (; *s; s++) {
        u8 gi = cur->gmap[(u8)*s];
        if (gi == 0xff) { x += 4; continue; }
        const Glyph *g = &cur->glyphs[gi];
        if (*s != ' ') quad(x, y, g->w, g->h, g->x, g->y, g->w, g->h);
        x += g->w + 1;
    }
    glEnd();
}

u16 hud_rgb565(u16 c) { return RGB15(c >> 11, (c >> 6) & 31, c & 31); }

// any texture (menu images): src rect (u, v, uw, vh) in texels -> dst (x, y, w, h) pixels; layer 0 = back .. 3 = front
void hud_image(int tex, int x, int y, int w, int h, int u, int v, int uw, int vh, u16 color, int layer)
{
    if (!ok || !tex) return;
    // layer 0 sits at the far end of the depth range so 3D (the rider on the select screen) draws in front of it
    static const v16 ZL[4] = { floattov16(-3.9f), floattov16(3.15f), floattov16(3.3f), floattov16(3.45f) };
    glBindTexture(0, tex);
    glColor(color);
    Z = ZL[layer & 3];
    glBegin(GL_QUADS);
    quad(x, y, w, h, u, v, uw, vh);
    glEnd();
}

void hud_rect(int x, int y, int w, int h, u16 color, int layer)
{
    if (!ok) return;
    static const v16 ZL[4] = { floattov16(-3.9f), floattov16(3.15f), floattov16(3.3f), floattov16(3.45f) };
    glBindTexture(0, 0);
    glColor(color);
    Z = ZL[layer & 3];
    glBegin(GL_QUADS);
    glVertex3v16(x * 64, y * 64, Z); glVertex3v16(x * 64, (y + h) * 64, Z);
    glVertex3v16((x + w) * 64, (y + h) * 64, Z); glVertex3v16((x + w) * 64, y * 64, Z);
    glEnd();
}

// translucent pieces of the menu frame: alpha 0..31 (each call its own polygon id so overlapping ones blend)
static int alpha_id;
static void alpha_fmt(int alpha)
{
    alpha_id = (alpha_id + 1) & 31;
    glPolyFmt(POLY_ALPHA(alpha < 1 ? 1 : alpha > 31 ? 31 : alpha) | POLY_CULL_NONE | POLY_ID(32 + alpha_id));
}
ITCM_CODE void hud_sprite_a(int x, int y, int w, int h, int u, int v, int dw, int dh, u16 color, int alpha)   // translucent, on top
{
    if (!ok || alpha <= 0) return;
    alpha_fmt(alpha); sprite_top = true;
    hud_sprite(x, y, w, h, u, v, dw, dh, color);
    sprite_top = false;
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(5));
}
void hud_rect_a(int x, int y, int w, int h, u16 color, int alpha, int layer)
{
    if (!ok) return;
    alpha_fmt(alpha);
    hud_rect(x, y, w, h, color, layer);
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(5));
}
// a texture quad centred on (cx, cy), size w x h pixels, turned by angle (radians), alpha 0..31
void hud_image_rot(int tex, float cx, float cy, float w, float h, float ang, int uw, int vh, u16 color, int alpha, int layer)
{
    if (!ok || !tex) return;
    static const v16 ZL[4] = { floattov16(-3.9f), floattov16(3.15f), floattov16(3.3f), floattov16(3.45f) };
    alpha_fmt(alpha);
    glBindTexture(0, tex);
    glColor(color);
    float c = cosf(ang), sn = sinf(ang), hx = w / 2, hy = h / 2;
    float px[4] = { -hx, -hx, hx, hx }, py[4] = { -hy, hy, hy, -hy };
    int tu[4] = { 0, 0, uw, uw }, tv[4] = { 0, vh, vh, 0 };
    v16 z = ZL[layer & 3];
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) {
        float X = cx + px[i] * c - py[i] * sn, Y = cy + px[i] * sn + py[i] * c;
        glTexCoord2t16(inttot16(tu[i]), inttot16(tv[i]));
        glVertex3v16((v16)(X * 64), (v16)(Y * 64), z);
    }
    glEnd();
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(5));
}
void hud_shadow(bool on) { shadow = on; }

// the same fonts drawn in software into an 8-bit 256 x 192 bitmap (the bottom-screen trail map): texel values 1..3
// (the font's shades) become palette indices col[1..3]; returns the width
int hud_soft_width(int font, const char *s)
{
    if (font < 0 || font >= NFONT || !fonts[font].bits) return 0;
    Font *ft = &fonts[font]; int w = 0;
    for (const char *p = s; *p; p++) { u8 g = ft->gmap[(u8)*p]; w += g == 0xff ? 4 : ft->glyphs[g].w + 1; }
    return w;
}

int hud_soft_text(u8 *dst, int x, int y, int font, int align, const char *s, const u8 col[4])
{
    if (font < 0 || font >= NFONT || !fonts[font].bits) return 0;
    Font *ft = &fonts[font];
    int w = 0;
    for (const char *p = s; *p; p++) { u8 g = ft->gmap[(u8)*p]; w += g == 0xff ? 4 : ft->glyphs[g].w + 1; }
    if (align == 1) x -= w; else if (align == 2) x -= w / 2;
    for (; *s; s++) {
        u8 gi = ft->gmap[(u8)*s];
        if (gi == 0xff) { x += 4; continue; }
        const Glyph *g = &ft->glyphs[gi];
        if (*s != ' ')
            for (int j = 0; j < g->h; j++) for (int i = 0; i < g->w; i++) {
                int u = g->x + i, v = g->y + j, px = x + i, py = y + j;
                if (px < 0 || px > 255 || py < 0 || py > 191) continue;
                int t = (ft->bits[(v * 256 + u) >> 2] >> (((v * 256 + u) & 3) * 2)) & 3;
                if (t) dst[py * 256 + px] = col[t];
            }
        x += g->w + 1;
    }
    return w;
}

// Texture uploads. glTexImage2D / glColorTableEXT map the texture banks to the CPU while they copy; while a bank is
// away the 3D engine reads nothing from it and the picture tears. The engine starts on the next picture's first
// lines about 48 lines before line 0 (VCOUNT ~214), so a copy is only safe between 192 and ~210 - and the blank can
// begin with the bottom panel's DMA (submap_vblank, ~35 lines). A 16 KB copy at once did not always fit (the line
// across the top of the track select). So: allocate with no data, then copy 1 KB at a time, each piece only while
// VCOUNT is 192..205, mapping just that piece's bank to the CPU around it (interrupts off for the piece).
void tex_sync(void)
{
    for (int k = 0; k < 4; k++) {
        int v = REG_VCOUNT;
        if (v >= 192 && v < 200) return;
        swiWaitForVBlank();
    }
}

static void vram_put(void *dst, const void *src, u32 n)
{
    u8 *d = dst; const u8 *s = src; int waits = 0;
    while (n) {
        int v = REG_VCOUNT;
        // (after a few blanks without a clear start - the bottom screen's DMA can take every blank's first lines,
        // or the blank interrupt isn't running - copy anyway: a torn frame, not a hang)
        if ((v < 192 || v >= 205) && waits < 4) { if (REG_IME && (REG_IE & IRQ_VBLANK)) swiWaitForVBlank(); waits++; continue; }
        waits = 0;
        u32 a = (u32)d, bank, end;
        if (a >= 0x06880000) { bank = 4; end = 0x06890000; }          // E: the texture palettes
        else { bank = (a - 0x06800000) >> 17; end = 0x06800000 + ((bank + 1) << 17); }
        u32 k = n < 1024 ? n : 1024;
        if (a + k > end) k = end - a;
        vu8 *cr = (vu8 *)0x04000240 + bank;
        int ime = REG_IME; REG_IME = 0;
        u8 old = *cr; *cr = VRAM_ENABLE;                                // LCD (the CPU's)
        memcpy(d, s, k);
        *cr = old;
        REG_IME = ime;
        d += k; s += k; n -= k;
    }
}

bool tex_upload(int name, int type, int w, int h, int param, const void *texels, const void *pal, int ncol)
{
    glBindTexture(0, name);
    if (!glTexImage2D(0, 0, type, w, h, 0, param, NULL)) return false;
    int bpp = type == GL_RGB4 ? 2 : type == GL_RGB16 ? 4 : type == GL_RGB || type == GL_RGBA ? 16 : 8;
    void *dst = glGetTexturePointer(name);
    if (dst) vram_put(dst, texels, (u32)w * h * bpp / 8);
    if (pal) {
        if (!glColorTableEXT(0, 0, ncol, 0, 0, NULL)) return false;
        void *pd = glGetColorTablePointer(name);
        if (pd) vram_put(pd, pal, ncol * 2);
    }
    return true;
}

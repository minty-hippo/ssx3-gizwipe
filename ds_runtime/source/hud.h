// SSX 3 GizWipe - top-screen HUD (Gizmondo HUD sheet + fontssxbig)
#pragma once
#include <nds.h>

bool hud_open(const char *path);
void hud_begin(void);                    // orthographic pass over the 3D scene
void hud_end(void);
// sheet sprite (u, v, w, h) drawn at x, y with size dw x dh (the exe's 0x7b1e8 / scaled 0x7b32c)
void hud_sprite(int x, int y, int w, int h, int u, int v, int dw, int dh, u16 color);
void hud_sprite_over(bool on);
void hud_outline(bool on);
void hud_sprite_a(int x, int y, int w, int h, int u, int v, int dw, int dh, u16 color, int alpha);
void hud_text(int x, int y, int align, const char *s, u16 color);   // align 0 left, 1 right, 2 centre
int  hud_text_width(const char *s);
u16  hud_rgb565(u16 c);                  // the exe's colours are RGB565
void hud_image(int tex, int x, int y, int w, int h, int u, int v, int uw, int vh, u16 color, int layer);
void hud_rect(int x, int y, int w, int h, u16 color, int layer);   // solid rectangle (untextured)
void hud_font(int i);                      // 0 race HUD font (fontssxbig), 1 mainfont, 2 midfont, 3 smallestfont
void hud_rect_a(int x, int y, int w, int h, u16 color, int alpha, int layer);   // translucent rectangle, alpha 0..31
void hud_image_rot(int tex, float cx, float cy, float w, float h, float ang, int uw, int vh, u16 color, int alpha, int layer);
void hud_shadow(bool on);
int  hud_soft_text(u8 *dst, int x, int y, int font, int align, const char *s, const u8 col[4]);   // into an 8-bit 256 x 192 bitmap                  // text drop shadow (on by default)
int  hud_soft_width(int font, const char *s);                    // its width in pixels
void tex_sync(void);
bool tex_upload(int name, int type, int w, int h, int param, const void *texels, const void *pal, int ncol);   // tear-free upload (vblank pieces)
   // before a texture upload: wait for the vertical blank (uploads map the banks away from the 3D engine)

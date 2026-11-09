// SSX 3 GizWipe - bottom-screen trail map (see submap.c)
#pragma once
#include <nds.h>
#include "track.h"
bool submap_open(PrintConsole *console);                          // load the backdrop (once)
void submap_begin(const Track *t, const char *title, const char *sub);   // switch the bottom screen to the map
void submap_update(const s32 player[3], const s32 (*others)[3], int nothers);   // positions in game units
void submap_end(void);                                           // back to the text console
bool submap_active(void);
void submap_release(void);
void submap_dim(bool on);                                        // the map's dots greyed (at the gate)                                       // the map stops; the bitmap stays (no debug text)
void submap_trax(const char *song, int dvb);                     // the Radio BIG strip (current song): call every frame
// the standings beside the map: rank, name, value (vcol 0 grey, 1 green - ahead, 2 red - behind); me = the player
typedef struct { char name[16]; char val[12]; u8 rank, me, vcol, dim; } SubRow;   // dim: greyed (at the gate)
void submap_list(const SubRow *rows, int n, const char *head);

// the menus' bottom screen (same backdrop and frame as the map): a title bar, then text, wrapped text and bars drawn
// with the game's fonts into the 8-bit bitmap; subui_show() puts it on screen. Colours: SU_WHITE, SU_ORANGE, ...
enum { SU_WHITE, SU_ORANGE, SU_BLUE, SU_GREY, SU_GREEN, SU_RED };
void subui_begin(const char *title, const char *sub);
int  subui_text(int x, int y, int font, int align, const char *s, int colour);     // returns the width
int  subui_wrap(int x, int y, int w, int font, const char *s, int colour, int lh);  // returns the height used
void subui_bar(int x, int y, int w, int h, int value, int max);
void subui_rect(int x, int y, int w, int h, int colour);
void subui_show(void);
bool subui_image(int x, int y, int w, int h, const char *path);
u8 *subui_pixels(void);                       // the panel's 256 x 192 pixels (palette indices), for subui_show
void subui_own_palette(const u16 *p, int n);  // palette slots 202.. (at most 54) for those pixels   // a .subi picture into the panel (shown with subui_show)
void submap_vblank(void);                  // (from the vertical blank interrupt)

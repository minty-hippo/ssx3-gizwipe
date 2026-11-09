// SSX 3 GizWipe - the board designer (port addition): the player's own board graphic, both faces, drawn with the
// stylus and stickers from the game's menu art, used on the player's board when it is on (Options.board)
#pragma once
#include <nds.h>
enum { BOARD_TEX = 128,            // the board textures are 128 x 128: the right half the top deck, the left the base
       BOARD_NDRAW = 16,           // palette 0..15: the drawing colours
       BOARD_NCOL = 54 };          // 16..53: the stickers' colours
extern u8 board_px[BOARD_TEX * BOARD_TEX];          // palette indices
extern u16 board_pal[BOARD_NCOL];
void board_init(bool fat);                          // the stickers, and the saved design
bool board_fat(void);
void board_save(void);
int  board_nstickers(void);
const u8 *board_sticker(int i, int *w, int *h);     // 255: clear, else a palette index
void board_changed(void);                           // board_px changed: the textures follow (board_textures)
int  board_texture(bool hi);                        // GL texture of the design: 128 x 128 (hi) or 64 x 64
void board_textures_update(void);                   // re-upload when changed (call between frames)

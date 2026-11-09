// SSX 3 GizWipe - the board designer's data (port addition). The design is a 128 x 128 paletted texture laid out
// as the riders' own boards are (x 0..63 one face, 64..127 the other, the board's length down the texture); the
// races use it at 128 x 128 for the high-detail board and halved for the low one. Saved as ssx3gizwipe_board.bin
// ("SBD1", then the 16 KB of texels).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "board.h"
#include "hud.h"

u8 board_px[BOARD_TEX * BOARD_TEX];
u16 board_pal[BOARD_NCOL] = {
    RGB15(31, 31, 31), RGB15(2, 2, 3), RGB15(29, 4, 4), RGB15(31, 16, 2), RGB15(31, 28, 4), RGB15(6, 24, 6),
    RGB15(4, 26, 28), RGB15(4, 10, 28), RGB15(18, 6, 26), RGB15(31, 12, 22), RGB15(16, 9, 4), RGB15(15, 15, 16),
    RGB15(16, 24, 31), RGB15(18, 30, 6), RGB15(14, 2, 4), RGB15(28, 22, 8) };
static u8 *stk; static int nstk; static const u8 *stk_at[32];
static bool fat, dirty = true;
static int tex_hi, tex_lo;
static u8 lo_px[64 * 64];

bool board_fat(void) { return fat; }
int board_nstickers(void) { return nstk; }
const u8 *board_sticker(int i, int *w, int *h)
{
    if (i < 0 || i >= nstk) return NULL;
    *w = stk_at[i][0]; *h = stk_at[i][1];
    return stk_at[i] + 2;
}

void board_init(bool have_fat)
{
    fat = have_fat;
    memset(board_px, 0, sizeof(board_px));                      // white both sides
    FILE *f = fopen("nitro:/menu/stickers.bin", "rb");
    if (f) {
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        stk = malloc(n);
        if (stk && fread(stk, 1, n, f) == (size_t)n && !memcmp(stk, "STKR", 4)) {
            int cnt = stk[4] | stk[5] << 8, nc = stk[6] | stk[7] << 8;
            if (nc > BOARD_NCOL - BOARD_NDRAW) nc = BOARD_NCOL - BOARD_NDRAW;
            memcpy(board_pal + BOARD_NDRAW, stk + 8, nc * 2);
            const u8 *p = stk + 8 + (stk[6] | stk[7] << 8) * 2;
            for (int i = 0; i < cnt && i < 32 && p + 2 <= stk + n; i++) { stk_at[nstk++] = p; p += 2 + p[0] * p[1]; }
        }
        fclose(f);
    }
    if (fat) {
        f = fopen("ssx3gizwipe_board.bin", "rb");
        if (f) {
            char m[4];
            if (fread(m, 4, 1, f) == 1 && !memcmp(m, "SBD1", 4)) fread(board_px, 1, sizeof(board_px), f);
            fclose(f);
            for (u32 i = 0; i < sizeof(board_px); i++) if (board_px[i] >= BOARD_NCOL) board_px[i] = 0;
        }
    }
    dirty = true;
    // the textures now, before the menus' pictures: made later, the design's texture name came back from a freed
    // menu picture, and the frontend's retiring of that picture took the design's VRAM with it (the board showed
    // the menu's snowflakes)
    board_texture(true);
}

void board_save(void)
{
    if (!fat) return;
    FILE *f = fopen("ssx3gizwipe_board.bin", "wb");
    if (!f) return;
    fwrite("SBD1", 4, 1, f); fwrite(board_px, 1, sizeof(board_px), f);
    fclose(f);
}

void board_changed(void) { dirty = true; }

static void make_lo(void)
{
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) lo_px[y * 64 + x] = board_px[(y * 2) * BOARD_TEX + x * 2];
}

int board_texture(bool hi)
{
    if (!tex_hi) {
        u16 pal[256]; memset(pal, 0, sizeof(pal)); memcpy(pal, board_pal, sizeof(board_pal));
        make_lo();
        glGenTextures(1, &tex_hi); glGenTextures(1, &tex_lo);
        const int prm = TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S | GL_TEXTURE_WRAP_T;
        tex_upload(tex_hi, GL_RGB256, 128, 128, prm, board_px, pal, 256);
        tex_upload(tex_lo, GL_RGB256, 64, 64, prm, lo_px, pal, 256);
        dirty = false;
    }
    return hi ? tex_hi : tex_lo;
}

void board_textures_update(void)
{
    if (!dirty || !tex_hi) return;
    make_lo();
    tex_update(tex_hi, board_px, sizeof(board_px), board_pal, BOARD_NCOL);
    tex_update(tex_lo, lo_px, sizeof(lo_px), NULL, 0);
    dirty = false;
}

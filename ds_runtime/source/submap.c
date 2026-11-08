// SSX 3 GizWipe - bottom-screen trail map during an event. The sub engine shows an 8-bit 256 x 192 bitmap (banks H
// + I, 48 KB at 0x06200000) holding the game's in-game backdrop igpbg.bmp, a darkened panel framed like the menus
// (orange title bar and rule), the course's race line seen from above (start at the top), and the riders as dots -
// orange for you, blue for the others. Dots move by restoring their old squares from the clean picture and drawing
// the new ones, so only a few small rectangles reach VRAM a frame.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "submap.h"
#include "hud.h"
#include "lang.h"

enum { W = 256, H = 192, PAL_DARK = 96,
       C_BLACK = 192, C_WHITE, C_ORANGE, C_BLUE, C_ORANGE_D, C_LINE_D, C_GREY, C_BLUE_L, C_GREEN, C_RED };
static u8 *bg, *clean, *work;                 // the backdrop, backdrop + panel + trail, what is on screen
static u16 pal[256];
static bool active;
static volatile bool show_pending;              // a menu panel waiting for the vertical blank (subui_show)
static float ox, oy, sc, fx_, fz_;             // world -> map: (p - o) along the right / forward axes, x scale
static int px0, py0;                           // map centre on screen
enum { PANEL_Y1 = 180 };                       // the panel's bottom (below it: the EA Trax strip)
enum { LX0 = 12, LX1 = 124, LY0 = 30, LY1 = PANEL_Y1 - 2 };   // the standings' panel (left of the map)
static u32 list_sig;                           // what the standings show (redrawn only when it changes)
static int old_x[8], old_y[8], old_n;          // last drawn dots
static PrintConsole *con, dummy; static u16 dummy_map[32 * 32];

bool submap_open(PrintConsole *console)
{
    con = console;
    FILE *f = fopen("nitro:/menu/submap.ssxb", "rb");
    if (!f) return false;
    char m[4]; u16 wh[2];
    fread(m, 4, 1, f); fread(wh, 2, 2, f); fread(pal, 2, 256, f);
    bg = malloc(W * H); clean = malloc(W * H); work = malloc(W * H);
    if (!bg || !clean || !work) { fclose(f); return false; }
    fread(bg, 1, W * H, f); fclose(f);
    pal[C_BLACK] = RGB15(1, 2, 4); pal[C_WHITE] = RGB15(31, 31, 31); pal[C_ORANGE] = RGB15(31, 19, 3);
    pal[C_BLUE] = RGB15(6, 16, 31); pal[C_ORANGE_D] = RGB15(22, 10, 2); pal[C_LINE_D] = RGB15(3, 5, 10);
    pal[C_GREY] = RGB15(18, 20, 24); pal[C_BLUE_L] = RGB15(20, 26, 31); pal[C_GREEN] = RGB15(10, 28, 10);
    pal[C_RED] = RGB15(31, 8, 6);
    return true;
}

static void dot(u8 *b, int x, int y, int r, u8 c)
{
    for (int j = -r - 1; j <= r + 1; j++) for (int i = -r - 1; i <= r + 1; i++) {
        int d = i * i + j * j, X = x + i, Y = y + j;
        if (X < 0 || X >= W || Y < 0 || Y >= H) continue;
        if (d <= r * r) b[Y * W + X] = c;
        else if (d <= (r + 1) * (r + 1)) b[Y * W + X] = C_BLACK;
    }
}
static void line(u8 *b, int x0, int y0, int x1, int y1, int r, u8 c)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) {
            int X = x0 + i, Y = y0 + j;
            if (X >= 0 && X < W && Y >= 0 && Y < H && i * i + j * j <= r * r + 1) b[Y * W + X] = c;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void rect(u8 *b, int x, int y, int w, int h, u8 c) { for (int j = y; j < y + h; j++) memset(b + j * W + x, c, w); }

// to VRAM: rows x0..x1 (made even) of lines y0..y1, in halfwords (VRAM takes no byte writes)
static void push(const u8 *b, int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > W - 1) x1 = W - 1; if (y1 > H - 1) y1 = H - 1;
    x0 &= ~1; x1 |= 1;
    vu16 *v = (vu16 *)BG_BMP_RAM_SUB(0);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x += 2) v[(y * W + x) >> 1] = b[y * W + x] | (b[y * W + x + 1] << 8);
}

static void to_map(const s32 p[3], int *x, int *y)        // world (game units) -> screen pixel
{
    float dx = p[0] - ox, dz = p[2] - oy;
    float a = dx * fx_ + dz * fz_;                         // along the course (start -> finish)
    float b = dx * fz_ - dz * fx_;                         // across it
    *x = px0 + (int)(b * sc); *y = py0 + (int)(a * sc);
}

static bool bitmap_on;
static void trax_render(u8 *b);
static void bitmap_mode(void)
{
    if (bitmap_on) return;
    // the console keeps printing, into RAM: its tiles and map in bank H are about to become the bitmap
    dummy = *con; dummy.fontBgMap = dummy_map; consoleSelect(&dummy);
    videoSetModeSub(MODE_5_2D);
    vramSetBankH(VRAM_H_SUB_BG); vramSetBankI(VRAM_I_SUB_BG_0x06208000);
    REG_BG3CNT_SUB = BG_BMP8_256x256 | BG_BMP_BASE(0) | BG_PRIORITY(0);
    REG_BG3PA_SUB = 256; REG_BG3PB_SUB = 0; REG_BG3PC_SUB = 0; REG_BG3PD_SUB = 256; REG_BG3X_SUB = 0; REG_BG3Y_SUB = 0;
    REG_DISPCNT_SUB = MODE_5_2D | DISPLAY_BG3_ACTIVE;
    dmaCopy(pal, BG_PALETTE_SUB, 512);
    bitmap_on = true;
}

// the picture, the panel (darkened backdrop), the title bar and its rule (0x22a24's orange), into clean
static void frame(const char *title, const char *sub)
{
    memcpy(clean, bg, W * H);
    const int X0 = 10, Y0 = 26, X1 = 246, Y1 = PANEL_Y1;
    for (int y = Y0; y < Y1; y++) for (int x = X0; x < X1; x++) { u8 c = clean[y * W + x]; if (c < PAL_DARK) clean[y * W + x] = c + PAL_DARK; }
    for (int x = X0; x < X1; x++) { clean[Y0 * W + x] = C_ORANGE; clean[(Y1 - 1) * W + x] = C_ORANGE; }
    for (int y = Y0; y < Y1; y++) { clean[y * W + X0] = C_ORANGE; clean[y * W + X1 - 1] = C_ORANGE; }
    rect(clean, 0, 4, W, 18, C_ORANGE_D); rect(clean, 0, 21, W, 1, C_ORANGE); rect(clean, 0, 4, W, 1, C_ORANGE);
    const u8 tc[4] = { 0, C_WHITE, C_BLACK, 0 };
    hud_soft_text(clean, 8, 7, 1, 0, L(title), tc);
    hud_soft_text(clean, 248, 9, 2, 1, L(sub), tc);
}

void submap_begin(const Track *t, const char *title, const char *sub)
{
    if (!bg) return;
    bitmap_mode();
    frame(title, sub);
    const int X0 = 10, Y0 = 26, X1 = 246, Y1 = PANEL_Y1;
    const u8 sc4[4] = { 0, C_BLUE_L, C_LINE_D, 0 };

    // the course seen from above: start -> finish runs down the panel
    int n = t->h.nraceline;
    const RacePoint *r = t->race;
    float dx = r[n - 1].x - r[0].x, dz = r[n - 1].z - r[0].z, l = sqrtf(dx * dx + dz * dz);
    if (l < 1) { dx = 0; dz = 1; l = 1; }
    fx_ = dx / l; fz_ = dz / l;
    float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f;
    for (int i = 0; i < n; i++) {
        float a = r[i].x * fx_ + r[i].z * fz_, b = r[i].x * fz_ - r[i].z * fx_;
        if (a < amin) amin = a; if (a > amax) amax = a; if (b < bmin) bmin = b; if (b > bmax) bmax = b;
    }
    float wa = amax - amin, wb = bmax - bmin;
    // port: the map on the right of the panel (the riders' standings on the left, submap_list)
    const int MX0 = LX1 + 4, MX1 = X1 - 6;
    sc = fminf((Y1 - Y0 - 20) / (wa > 1 ? wa : 1), (MX1 - MX0 - 12) / (wb > 1 ? wb : 1));
    // centre: o is the world point at the middle of the box
    float ca = (amin + amax) / 2, cb = (bmin + bmax) / 2;
    ox = ca * fx_ + cb * fz_; oy = ca * fz_ - cb * fx_;
    px0 = (MX0 + MX1) / 2; py0 = (Y0 + Y1) / 2;
    for (int y = Y0 + 6; y < Y1 - 6; y++) clean[y * W + LX1 + 1] = C_ORANGE_D;   // between the standings and the map
    int lx = 0, ly = 0;
    for (int pass = 0; pass < 2; pass++)                    // a dark edge, then the white line
        for (int i = 0; i < n; i++) {
            s32 p[3] = { r[i].x, r[i].y, r[i].z }; int x, y; to_map(p, &x, &y);
            if (i) line(clean, lx, ly, x, y, pass ? 1 : 2, pass ? C_WHITE : C_LINE_D);
            lx = x; ly = y;
        }
    { s32 p[3] = { r[0].x, r[0].y, r[0].z }; int x, y; to_map(p, &x, &y); dot(clean, x, y, 3, C_GREEN); }
    { s32 p[3] = { r[n - 1].x, r[n - 1].y, r[n - 1].z }; int x, y; to_map(p, &x, &y);   // finish: a chequered flag
      for (int j = -3; j < 3; j++) for (int i = -4; i < 4; i++) if (x + i >= 0 && x + i < W && y + j >= 0 && y + j < H)
          clean[(y + j) * W + x + i] = ((i + 4) / 2 + (j + 3) / 2) & 1 ? C_BLACK : C_WHITE; }
    (void)sc4;
    memcpy(work, clean, W * H);
    list_sig = 0;
    trax_render(work);
    push(work, 0, 0, W - 1, H - 1);
    old_n = 0; active = true;
}

static bool dots_dim;
void submap_dim(bool on) { dots_dim = on; }
void submap_update(const s32 player[3], const s32 (*others)[3], int nothers)
{
    if (!active) return;
    int nx[8], ny[8], n = 0;
    for (int k = 0; k < nothers && n < 7; k++) { to_map(others[k], &nx[n], &ny[n]); n++; }
    to_map(player, &nx[n], &ny[n]); n++;
    for (int k = 0; k < old_n; k++)                         // the old dots: back to the clean picture
        for (int y = old_y[k] - 4; y <= old_y[k] + 4; y++) {
            if (y < 0 || y >= H) continue;
            for (int x = old_x[k] - 4; x <= old_x[k] + 4; x++) if (x >= 0 && x < W) work[y * W + x] = clean[y * W + x];
        }
    for (int k = 0; k < n; k++) dot(work, nx[k], ny[k], k == n - 1 ? 3 : 2, dots_dim ? C_GREY : k == n - 1 ? C_ORANGE : C_BLUE);
    for (int k = 0; k < old_n; k++) push(work, old_x[k] - 4, old_y[k] - 4, old_x[k] + 4, old_y[k] + 4);
    for (int k = 0; k < n; k++) push(work, nx[k] - 4, ny[k] - 4, nx[k] + 4, ny[k] + 4);
    memcpy(old_x, nx, sizeof(int) * n); memcpy(old_y, ny, sizeof(int) * n); old_n = n;
}

// port: the riders' standings left of the map, leader on top: a rank, the rider's name and a value (time gap or
// score); the player's row highlighted (an orange bar behind dark text). Redrawn only when something in it changed.
void submap_list(const SubRow *rows, int n, const char *head)
{
    if (!active) return;
    static const u8 TW[4] = { 0, C_WHITE, C_BLACK, 0 }, TG[4] = { 0, C_GREY, C_BLACK, 0 },
                    TGR[4] = { 0, C_GREEN, C_BLACK, 0 }, TR[4] = { 0, C_RED, C_BLACK, 0 }, TO[4] = { 0, C_ORANGE, C_BLACK, 0 };
    static u32 row_sig[10];
    if (!list_sig) { for (int i = 0; i < 10; i++) row_sig[i] = 0; list_sig = 1; }
    int y = LY0 + 4;
    if (head && *head) {                                      // (a heading: drawn once per map)
        if (row_sig[9] != 1) { hud_soft_text(work, LX0 + 4, y - 2, 3, 0, L(head), TO); push(work, LX0, y - 2, LX1, y + 10); row_sig[9] = 1; }
        y += 12;
    }
    int rh = n > 6 ? 15 : 19;                                 // row height: room for up to 9
    // each row redrawn only when it changed (the gap ticks over ten times a second: one row, not the panel)
    for (int i = 0; i < 9; i++, y += rh) {
        if (y + rh > LY1) break;
        u32 h = 2166136261u;
        if (i < n) {
            const SubRow *r = &rows[i];
            for (const char *c = r->name; *c; c++) h = (h ^ (u8)*c) * 16777619u;
            for (const char *c = r->val; *c; c++) h = (h ^ (u8)*c) * 16777619u;
            h = (h ^ (r->me | r->vcol << 1 | r->rank << 4 | rh << 12 | r->dim << 20)) * 16777619u;
        }
        h |= 1;
        if (i >= n) h = 2;                                    // empty
        if (h == row_sig[i]) continue;
        row_sig[i] = h;
        int by = y - 3, bh = rh - 2;
        for (int j = by; j < by + rh; j++) if (j < LY1) memcpy(work + j * W + LX0, clean + j * W + LX0, LX1 - LX0);
        if (i < n) {
            const SubRow *r = &rows[i];
            if (r->me) {                                      // the player: a dark orange bar framed in orange
                int bx = LX0 + 2, bw = LX1 - LX0 - 4;          // (grey at the gate)
                rect(work, bx, by, bw, bh, r->dim ? C_GREY : C_ORANGE); rect(work, bx + 1, by + 1, bw - 2, bh - 2, r->dim ? C_LINE_D : C_ORANGE_D);
            }
            char rk[4]; snprintf(rk, sizeof(rk), "%d", r->rank);
            hud_soft_text(work, LX0 + 12, y, 2, 1, rk, r->me && !r->dim ? TW : TG);
            dot(work, LX0 + 19, y + 5, 2, r->dim ? C_GREY : r->me ? C_ORANGE : C_BLUE);
            hud_soft_text(work, LX0 + 25, y, 2, 0, r->name, r->dim ? TG : TW);
            if (r->val[0]) hud_soft_text(work, LX1 - 4, y + 1, 3, 1, r->val, r->vcol == 1 ? TGR : r->vcol == 2 ? TR : r->me ? TW : TG);
        }
        push(work, LX0, by, LX1 - 1, by + rh - 1 < LY1 - 1 ? by + rh - 1 : LY1 - 1);
    }
}

// port: the Radio BIG strip (the exe's name for its play list) under the panel - the play list's current song,
// on every bottom screen (menus and races). Too long for the strip, it scrolls to its end and back, pausing at each end.
enum { TR_X0 = 10, TR_X1 = 246, TR_Y0 = 182, TR_Y1 = 192, TR_TX0 = 72, TR_TX1 = 242 };
static char tr_text[64]; static int tr_w, tr_off, tr_dir = 1, tr_wait, tr_tick; static bool tr_dirty = true;
static void trax_render(u8 *b)
{
    static const u8 TL[4] = { 0, C_ORANGE, C_BLACK, 0 }, TT[4] = { 0, C_WHITE, C_BLACK, 0 };
    rect(b, TR_X0, TR_Y0, TR_X1 - TR_X0, TR_Y1 - TR_Y0, C_LINE_D);
    int R = TR_TX1 - TR_TX0;
    int x = tr_w <= R ? TR_TX0 : TR_TX0 - tr_off;
    if (tr_text[0]) hud_soft_text(b, x, TR_Y0 + 1, 3, 0, tr_text, TT);
    rect(b, TR_X0, TR_Y0, TR_TX0 - 1 - TR_X0, TR_Y1 - TR_Y0, C_LINE_D);   // the label over what scrolled under it
    rect(b, TR_TX1, TR_Y0, TR_X1 - TR_TX1, TR_Y1 - TR_Y0, C_LINE_D);
    for (int y = TR_Y0; y < TR_Y1; y++) {                // the backdrop either side of the strip, as it was
        memcpy(b + y * W, clean + y * W, TR_X0); memcpy(b + y * W + TR_X1, clean + y * W + TR_X1, W - TR_X1);
    }
    rect(b, TR_X0, TR_Y0, TR_X1 - TR_X0, 1, C_ORANGE_D);
    rect(b, TR_TX0 - 3, TR_Y0 + 2, 1, TR_Y1 - TR_Y0 - 3, C_ORANGE_D);   // between the label and the song
    hud_soft_text(b, TR_X0 + 3, TR_Y0 + 1, 3, 0, "RADIO BIG", TL);
}
void submap_trax(const char *text, int dvb)
{
    if (!bg) return;
    if (strncmp(text, tr_text, sizeof(tr_text) - 1)) {
        snprintf(tr_text, sizeof(tr_text), "%s", text);
        tr_w = hud_soft_width(3, tr_text); tr_off = 0; tr_dir = 1; tr_wait = 90; tr_dirty = true;
    }
    int R = TR_TX1 - TR_TX0;
    if (tr_w > R) {                                     // ping-pong: 1 pixel every 2 frames, 1.5 s at each end
        if (tr_wait > 0) tr_wait -= dvb;
        else if ((tr_tick += dvb) >= 2) {
            tr_tick = 0; tr_off += tr_dir; tr_dirty = true;
            if (tr_off >= tr_w - R) { tr_off = tr_w - R; tr_dir = -1; tr_wait = 90; }
            if (tr_off <= 0) { tr_off = 0; tr_dir = 1; tr_wait = 90; }
        }
    }
    if (!bitmap_on || !tr_dirty) return;
    tr_dirty = false;
    trax_render(work);
    if (!show_pending) push(work, 0, TR_Y0, W - 1, TR_Y1 - 1);   // (a whole panel on its way carries it)
}

// back to the text console (the menus use it)
void submap_end(void)
{
    if (!active && !bitmap_on) return;
    active = false; bitmap_on = false;
    videoSetModeSub(MODE_0_2D);
    vramSetBankH(VRAM_H_SUB_BG); vramSetBankI(VRAM_I_LCD);
    consoleInit(con, 0, BgType_Text4bpp, BgSize_T_256x256, 8, 0, false, true);
    consoleSelect(con);
}

bool submap_active(void) { return active; }
// leaving a race: the map stops, the bitmap stays up for the menus' panels (the text console - debug - only shows
// when asked for)
void submap_release(void)
{
    active = false;
    if (!bitmap_on) REG_DISPCNT_SUB &= ~DISPLAY_BG0_ACTIVE;
}

// ---- the menus' bottom screen
static const u8 SUC[6][4] = { { 0, C_WHITE, C_BLACK, 0 }, { 0, C_ORANGE, C_BLACK, 0 }, { 0, C_BLUE_L, C_LINE_D, 0 },
                              { 0, C_GREY, C_BLACK, 0 }, { 0, C_GREEN, C_BLACK, 0 }, { 0, C_ORANGE_D, C_BLACK, 0 } };

void subui_begin(const char *title, const char *sub)
{
    if (!bg) return;
    while (show_pending) swiWaitForVBlank();          // the last panel still being copied out of work
    bitmap_mode();
    frame(title, sub ? sub : "");
    memcpy(work, clean, W * H);
}

int subui_text(int x, int y, int font, int align, const char *s, int c)
{
    return hud_soft_text(work, x, y, font, align, L(s), SUC[c]);
}

int subui_wrap(int x, int y, int w, int font, const char *s, int c, int lh)
{
    s = L(s);
    char line[96]; int n = 0, y0 = y;
    while (*s) {
        const char *e = s; while (*e && *e != ' ') e++;
        char tryl[96];
        snprintf(tryl, sizeof(tryl), "%.*s%s%.*s", n, line, n ? " " : "", (int)(e - s), s);
        if (n && hud_soft_width(font, tryl) > w) { line[n] = 0; hud_soft_text(work, x, y, font, 0, line, SUC[c]); y += lh; n = 0; continue; }
        n = snprintf(line, sizeof(line), "%s", tryl);
        s = *e ? e + 1 : e;
        while (*s == ' ') s++;
    }
    if (n) { line[n] = 0; hud_soft_text(work, x, y, font, 0, line, SUC[c]); y += lh; }
    return y - y0;
}

void subui_rect(int x, int y, int w, int h, int c)
{
    static const u8 CC[6] = { C_WHITE, C_ORANGE, C_BLUE, C_GREY, C_GREEN, C_ORANGE_D };
    rect(work, x, y, w, h, CC[c]);
}

void subui_bar(int x, int y, int w, int h, int v, int max)
{
    rect(work, x, y, w, h, C_LINE_D);
    int f = max > 0 ? v * (w - 2) / max : 0; if (f < 0) f = 0; if (f > w - 2) f = w - 2;
    rect(work, x + 1, y + 1, f, h - 2, C_ORANGE);
}

// the panel goes to VRAM from the vertical blank interrupt (submap_vblank): written during the display, the
// bottom screen showed half the old panel and half the new one (tearing)
void subui_show(void)
{
    if (!bitmap_on) return;
    trax_render(work);
    DC_FlushRange(work, W * H);
    show_pending = true;
}
// a picture (.subi: 48 colours, in palette slots 208..255) into the panel at x, y scaled to w x h; it shows with the
// next subui_show (its colours go to the palette in the same vertical blank)
enum { IMG_PAL = 208, IMG_N = 48 };
static u16 img_pal[IMG_N]; static volatile bool img_pal_new;
bool subui_image(int x, int y, int w, int h, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char m[4]; u16 wh[2];
    if (fread(m, 4, 1, f) != 1 || memcmp(m, "SUBI", 4) || fread(wh, 2, 2, f) != 2) { fclose(f); return false; }
    u16 p[IMG_N]; fread(p, 2, IMG_N, f);
    u8 *px = malloc(wh[0] * wh[1]);
    if (!px) { fclose(f); return false; }
    fread(px, 1, wh[0] * wh[1], f); fclose(f);
    while (show_pending) swiWaitForVBlank();
    for (int j = 0; j < h; j++) {
        int sy = j * wh[1] / h, Y = y + j; if (Y < 0 || Y >= H) continue;
        for (int i = 0; i < w; i++) { int X = x + i; if (X >= 0 && X < W) work[Y * W + X] = IMG_PAL + px[sy * wh[0] + i * wh[0] / w]; }
    }
    free(px);
    for (int k = 0; k < IMG_N; k++) pal[IMG_PAL + k] = p[k];
    memcpy(img_pal, p, sizeof(p)); img_pal_new = true;
    // a thin frame round it in the panel's orange
    for (int i = x - 1; i <= x + w; i++) { if (i >= 0 && i < W) { if (y > 0) work[(y - 1) * W + i] = C_ORANGE; if (y + h < H) work[(y + h) * W + i] = C_ORANGE; } }
    for (int j = y; j < y + h; j++) { if (j >= 0 && j < H) { if (x > 0) work[j * W + x - 1] = C_ORANGE; if (x + w < W) work[j * W + x + w] = C_ORANGE; } }
    return true;
}
void submap_vblank(void)
{
    if (!show_pending) return;
    if (img_pal_new) { dmaCopyHalfWords(3, img_pal, BG_PALETTE_SUB + IMG_PAL, sizeof(img_pal)); img_pal_new = false; }
    dmaCopyHalfWords(3, work, BG_BMP_RAM_SUB(0), W * H);
    show_pending = false;
}

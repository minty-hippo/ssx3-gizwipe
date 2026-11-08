// SSX 3 GizWipe - front end. Top screen: the Gizmondo menu art (title, ssxbg, peak maps with the route overlays,
// course screenshots) drawn through the HUD pass; bottom screen: the lists and the exe's event descriptions.
#include <stdio.h>
#include "xtext.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "frontend.h"
#include "rumble.h"
#include "hud.h"
#include "career.h"
#include "profiles.h"
#include "net.h"
#include "submap.h"
#include "lang.h"
#include "voice.h"
#include "music.h"

// names: the exe's string block at 0xba900; descriptions: pointer table 0x9b160 (English); peaks / tracks as the
// ghost files' start positions and the screenshots (p<peak>t<track>ss<n>) show them
const Event EVENTS[15] = {
    { "R&B",            "ara1", "RACE",                 XT_2, 1, 1, 0, 4, true },
    { "Metro City",     "bra2", "RACE",                 XT_3,       1, 2, 1, 4, true },
    { "Snow Jam",       "ASS1", "SLOPESTYLE",           XT_4,  1, 3, 2, 2, false },
    { XT_5,   "BHP1", "SUPER PIPE",           XT_6,        1, 4, 3, 2, false },
    { "Happiness",      "ABC1", "RACE vs rival",        XT_7,                           1, 5, 4, 2, true },
    { XT_8,  "ABC1", "SLOPESTYLE vs rival",  XT_9,                     1, 5, 5, 2, false },
    { XT_10, "CRA3", "RACE",                 XT_11,         2, 1, 6, 4, true },
    { "Intimidator",    "DRA4", "RACE",                 XT_12,                      2, 2, 7, 4, true },
    { "Style Mile",     "DSS2", "SLOPESTYLE",           XT_13,     2, 3, 8, 2, false },
    { "Ruthless",       "DBC2", "RACE vs rival",        XT_7,                           2, 4, 9, 2, true },
    { XT_14,   "DBC2", "SLOPESTYLE vs rival",  XT_9,                     2, 4, 10, 2, false },
    { "Gravitude",      "ERA5", "RACE",                 XT_15,            3, 1, 11, 4, true },
    { "Kick Doubt",     "ESS3", "SLOPESTYLE",           XT_16,                  3, 2, 12, 2, false },
    { "The Throne",     "EBC3", "RACE vs rival",        XT_7,                           3, 3, 13, 2, true },
    { "Throne Jam",     "EBC3", "SLOPESTYLE vs rival",  XT_9,                     3, 3, 14, 2, false },
};

static const char *PEAK_DESC[3] = {
    XT_17,
    XT_18,
    XT_19,
};

//[...][...]------------ images (menu/<name>.ssxi, .ssxa)
typedef struct { int tex, w, h; } Img;
#define MAXIMG 48                                       // (24 ran out on the rider select: one silhouette strip never loaded)
static struct { char name[20]; Img img; } cache[MAXIMG];
static int ncache;
// retired images: the frame already sent to the 3D engine (drawn during the next display) may still read them, so
// their VRAM is given back two menu frames later; freed at once, the next screen's images were uploaded over them
// and that last frame drew the old layout with the new pictures (the pink flash of silhouettes on leaving the track select)
static struct { int tex, at; } grave[MAXIMG]; static int ngrave, fe_clock;
static void reap(bool all)
{
    int k = 0;
    for (int i = 0; i < ngrave; i++) {
        if (all || fe_clock - grave[i].at >= 2) glDeleteTextures(1, &grave[i].tex);
        else grave[k++] = grave[i];
    }
    ngrave = k;
}
static void retire_at(int i)                            // cache entry i -> the graveyard
{
    if (cache[i].img.tex) {
        if (ngrave >= MAXIMG) reap(true);
        grave[ngrave].tex = cache[i].img.tex; grave[ngrave].at = fe_clock; ngrave++;
    }
    cache[i] = cache[--ncache];
}
// a screen change mid-frame left this frame with nothing drawn: not sent, the 3D engine keeps drawing the last
// screen until the new one is ready (sent empty, the top screen flashed the clear colour while images loaded)
static bool hold;
bool fe_hold(void) { bool h = hold; hold = false; return h; }
void fe_flushed(void) { fe_clock++; }                  // a menu frame went to the 3D engine
static void retire_all(void) { while (ncache) retire_at(ncache - 1); }
static void retire_prefix(const char *pre)            // e.g. this track's map route and screenshots
{
    size_t n = strlen(pre);
    for (int i = ncache - 1; i >= 0; i--) if (!strncmp(cache[i].name, pre, n)) retire_at(i);
}

static Img load_ssxa(const char *name)                 // A3I5, alpha from brightness, white (tinted by the colour)
{
    Img r = { 0, 0, 0 };
    char path[48]; snprintf(path, sizeof(path), "nitro:/menu/%s.ssxa", name);
    FILE *f = fopen(path, "rb");
    if (!f) return r;
    char m[4]; u16 wh[2]; fread(m, 4, 1, f); fread(wh, 2, 2, f);
    u8 *tx = malloc(wh[0] * wh[1]); fread(tx, 1, wh[0] * wh[1], f); fclose(f);
    static u16 pal[32]; for (int i = 0; i < 32; i++) pal[i] = RGB15(31, 31, 31);
    glGenTextures(1, &r.tex);
    if (!tex_upload(r.tex, GL_RGB32_A3, wh[0], wh[1], TEXGEN_TEXCOORD, tx, pal, 32) && ngrave) {
        reap(true); tex_upload(r.tex, GL_RGB32_A3, wh[0], wh[1], TEXGEN_TEXCOORD, tx, pal, 32);
    }
    free(tx);
    r.w = wh[0]; r.h = wh[1];
    return r;
}

static Img img(const char *name)
{
    for (int i = 0; i < ncache; i++) if (!strcmp(cache[i].name, name)) return cache[i].img;
    Img r = { 0, 0, 0 };
    if (ncache >= MAXIMG) return r;
    if (name[0] == '*') r = load_ssxa(name + 1);         // "*name": an alpha texture
    else {
        char path[48]; snprintf(path, sizeof(path), "nitro:/menu/%s.ssxi", name);
        FILE *f = fopen(path, "rb");
        if (!f) return r;
        struct { char m[4]; u16 w, h, tw, th; u8 flags, pad[3]; } __attribute__((packed)) hd;
        fread(&hd, sizeof(hd), 1, f);
        u16 *pal = malloc(512); u8 *tx = malloc(hd.tw * hd.th);
        fread(pal, 2, 256, f); fread(tx, 1, hd.tw * hd.th, f);
        fclose(f);
        glGenTextures(1, &r.tex);
        int prm = TEXGEN_TEXCOORD | ((hd.flags & 1) ? GL_TEXTURE_COLOR0_TRANSPARENT : 0);
        if (!tex_upload(r.tex, GL_RGB256, hd.tw, hd.th, prm, tx, pal, 256) && ngrave) {   // no room: give back the retired ones now
            reap(true); tex_upload(r.tex, GL_RGB256, hd.tw, hd.th, prm, tx, pal, 256);
        }
        free(pal); free(tx);
        r.w = hd.w; r.h = hd.h;
    }
    strncpy(cache[ncache].name, name, 19); cache[ncache].img = r; ncache++;
    return r;
}

int fe_image(const char *name, int *w, int *h) { Img i = img(name); if (w) *w = i.w; if (h) *h = i.h; return i.tex; }

void fe_leave(void)                                     // leaving the menus for a course: everything goes now
{
    for (int i = 0; i < ncache; i++) if (cache[i].img.tex) glDeleteTextures(1, &cache[i].img.tex);
    ncache = 0; reap(true);
}

static int screen = FE_TITLE, sel_peak, sel_event, sel_rider, sel_main, frames, anim;
static bool dirty = true;
static int play_mode;                                   // 0 [...], 1 Single Event, 2 Shadow Racing, 3 Multiplayer
#define career_mode (play_mode == 0)
Options g_opt = { 10, 10, 1, 0, 0xff, 1, 0, 1 };
static int prof_page, arr_l, arr_r;
static int vc_sel, vc_row, vc_pend, vc_msg_t; static bool vc_edit, vc_confirm; static const char *vc_msg;   // Voice Clips
static int sel_opt, msg_t, sel_sh; static bool confirm, watching;
bool fe_watch(void) { return watching; }                              // options cursor; "no shadow" message timer
// the bottom screen's button line (smallest font, grey, along the panel's foot)
static void sub_hints(const char *a, const char *b, const char *c)
{
    char s[96]; int n = 0;
    if (a) n += snprintf(s + n, sizeof(s) - n, "A %s   ", L(a));
    if (b) n += snprintf(s + n, sizeof(s) - n, "B %s   ", L(b));
    if (c) n += snprintf(s + n, sizeof(s) - n, "%s", c);
    subui_text(128, 168, 3, 2, s, SU_GREY);
}
static const char *const PLACE_TXT[3] = { "1st", "2nd", "3rd" };
static int sx_sin_tab(int a) { return (int)(sinf((a & 0x7ff) * 6.2831853f / 2048.0f) * 16384.0f); }
static int map_t;                                       // the route overlay's wipe (0x307ac: 140 px over ~0.5 s)

bool fe_career(void) { return career_mode; }
bool fe_shadow(void) { return play_mode == 2; }
bool fe_mp(void) { return play_mode == 3 && net_active(); }
static int sel_mp, sel_game, mp_hover = -1; static char mp_msg[48]; static int mp_msg_t;
void fe_mp_message(const char *m) { snprintf(mp_msg, sizeof(mp_msg), "%s", m); mp_msg_t = 240; }
void fe_mp_rematch(void)
{
    net_ctl.rider = 0xff; net_ctl.ready = 0; net_ctl.pause = 0;
    if (net_is_host()) { net_s.phase = NP_EVENT; net_s.go = 0; net_s.paused_by = 0xff; fe_enter(FE_EVENT); }
    else fe_enter(FE_MPLOBBY);
}
static Img img(const char *name);
static int ss_y, ss_w, ss_h, ss_shown = -1;          // the track select's slideshow on the bottom screen
void fe_enter(int sc)
{
    if (sc == FE_DONE) fe_leave(); else { retire_all(); hold = true; }
    screen = sc; dirty = true; frames = 0; if (sc != FE_DONE) watching = false;
    // rider select: both silhouette strips before the rider's model takes the texture memory (the second strip,
    // loaded later when moving to a rider on it, found no room and never drew)
    if (sc == FE_RIDER) { img("characters1"); img("characters2"); }
}
int fe_screen(void) { return screen; }

static void draw(const char *name, int x, int y, int w, int h, int layer)
{
    Img i = img(name);
    hud_image(i.tex, x, y, w, h, 0, 0, i.w, i.h, RGB15(31, 31, 31), layer);
}

// word-wrapped text for the bottom console (32 columns)
static void wrap(const char *s)
{
    int col = 0;
    while (*s) {
        const char *e = s; while (*e && *e != ' ') e++;
        int n = e - s;
        if (col && col + 1 + n > 31) { printf("\n"); col = 0; }
        else if (col) { printf(" "); col++; }
        printf("%.*s", n, s); col += n;
        s = *e ? e + 1 : e;
        while (*s == ' ') s++;
    }
    printf("\n");
}

// word-wrapped HUD text in a box w pixels wide; returns the height used
int wrap_hud(int x, int y, int w, const char *s, u16 color, int lh)
{
    s = L(s);
    char line[64]; int n = 0, y0 = y;
    while (*s) {
        const char *e = s; while (*e && *e != ' ') e++;
        char tryl[64];
        snprintf(tryl, sizeof(tryl), "%.*s%s%.*s", n, line, n ? " " : "", (int)(e - s), s);
        if (n && hud_text_width(tryl) > w) { line[n] = 0; hud_text(x, y, 0, line, color); y += lh; n = 0; continue; }
        n = snprintf(line, sizeof(line), "%s", tryl);
        s = *e ? e + 1 : e;
        while (*s == ' ') s++;
    }
    if (n) { line[n] = 0; hud_text(x, y, 0, line, color); y += lh; }
    return y - y0;
}

static int peak_events(int peak, int *out)
{
    int n = 0;
    for (int i = 0; i < 15; i++) if (EVENTS[i].peak == peak) out[n++] = i;
    return n;
}

static void fmt(char *o, int ms) { if (ms <= 0) strcpy(o, "-:--.--"); else sprintf(o, "%d:%02d.%02d", ms / 60000, ms / 1000 % 60, ms % 1000 / 10); }

//[...][...]------------ the Gizmondo menu frame (0x22a24), 320x240 -> 256x192
#define SX(v) ((v) * 4 / 5)
static const u16 C_WHITE = RGB15(31, 31, 31), C_TITLE = RGB15(31, 30, 31);   // 0xffdf
static const u16 C_BAR = RGB15(22, 10, 2);                                     // 0xff1054b0: the selection bar
static const u16 C_BLACK = RGB15(0, 0, 0), C_GREY = RGB15(10, 10, 12);

// 18 snowflakes (0x31ed8): three bitmaps, drifting at random, turning, fading as they fall; re-thrown at the top
typedef struct { float x, y, vx, vy, a, va; u8 type; } Flake;
static Flake flakes[18]; static bool flakes_init;
static u32 frnd(void) { static u32 s = 0x1234567; s = s * 1664525u + 1013904223u; return s >> 8; }
static void flake_reset(Flake *f, bool top)
{
    f->x = (float)(frnd() % 300) - 22; f->y = top ? -16 - (float)(frnd() % 60) : (float)(frnd() % 200);
    f->vx = ((int)(frnd() % 64) - 32) / 64.0f; f->vy = 0.35f + (frnd() % 64) / 96.0f;
    f->a = (frnd() % 628) / 100.0f; f->va = ((int)(frnd() % 64) - 32) / 600.0f; f->type = frnd() % 3;
}
static void draw_flakes(void)
{
    if (!flakes_init) { for (int i = 0; i < 18; i++) flake_reset(&flakes[i], false); flakes_init = true; }
    static const char *const FN[3] = { "*snowflake", "*snowflake2", "*snowflake3" };
    for (int i = 0; i < 18; i++) {
        Flake *f = &flakes[i];
        f->vx += ((int)(frnd() % 32) - 14) / 512.0f; f->vy += ((int)(frnd() % 24) - 12) / 512.0f;
        if (f->vx < -0.8f) f->vx = -0.8f; if (f->vx > 0.8f) f->vx = 0.8f;
        if (f->vy < 0.25f) f->vy = 0.25f; if (f->vy > 1.2f) f->vy = 1.2f;
        f->x += f->vx; f->y += f->vy; f->a += f->va;
        if (f->y > 200 || f->x < -30 || f->x > 286) { flake_reset(f, true); continue; }
        int al = 31 - (int)(f->y * 24 / 192); if (al < 4) al = 4;
        Img im = img(FN[f->type]);
        float sz = 10 + (i % 3) * 3;
        hud_image_rot(im.tex, f->x, f->y, sz, sz, f->a, 32, 32, C_WHITE, al, 1);
    }
}

// the marching dashed lines (0x79e90 / 0x79fd4: 4 lit pixels in 8, moving one pixel every 3 frames) that slide to
// each screen's place over 10 frames (0x22840)
static int line_x, line_y, from_x, from_y, to_x, to_y, slide_t = 10;
static void lines_to(int x, int y)
{
    if (x == to_x && y == to_y) return;
    from_x = line_x; from_y = line_y; to_x = x; to_y = y; slide_t = 0;
}
static void draw_lines(void)
{
    if (slide_t < 10) { slide_t++; line_x = from_x + (to_x - from_x) * slide_t / 10; line_y = from_y + (to_y - from_y) * slide_t / 10; }
    else { line_x = to_x; line_y = to_y; }
    int ph = (anim / 3) & 7;
    for (int x = -8 + ph; x < 256; x += 8) hud_rect(x < 0 ? 0 : x, line_y, x < 0 ? 4 + x : 4, 1, C_WHITE, 2);
    for (int y = -8 + ph; y < 192; y += 8) hud_rect(line_x, y < 0 ? 0 : y, 1, y < 0 ? 4 + y : 4, C_WHITE, 2);
}

// the frame: mountain backdrop, snow, the screen title with its gradient and double rule, the SSX3 logo (with its
// shadow) top right, the orange corner bars bottom left
static void frame(const char *title, bool snow)
{
    draw("ssxbg", 0, -5, 256, 205, 0);
    if (snow) draw_flakes();
    draw_lines();
    if (title) {
        hud_font(1);
        hud_text(SX(0x11), SX(14), 0, title, C_TITLE);
        int x = SX(0x11) + hud_text_width(title) + 6;
        Img g = img("grad");
        hud_image(g.tex, x, SX(0x1e), 35, 2, 0, 0, g.w, g.h, C_WHITE, 2);
        hud_rect(x + 35, SX(0x1e), 256 - x - 35, 1, C_WHITE, 2);
        hud_rect(x + 35, SX(0x1e) + 1, 256 - x - 35, 1, C_GREY, 2);
    }
    Img sh = img("*logobigshad"), lg = img("logobig");
    hud_image(sh.tex, SX(0x13d - 96) + 2, 2, SX(96), SX(64), 0, 0, 96, 64, C_BLACK, 1);
    hud_image(lg.tex, SX(0x13d - 96), 0, SX(96), SX(64), 0, 0, lg.w, lg.h, C_WHITE, 2);
    Img b1 = img("orangebar1"), b2 = img("orangebar2");
    hud_image(b1.tex, 0, SX(240 - 4 - 149), SX(16), SX(149), 0, 0, b1.w, b1.h, C_WHITE, 2);
    hud_image(b2.tex, 0, 192 - 3, SX(160), 3, 0, 0, b2.w, b2.h, C_WHITE, 2);
}

// button hints in the dark box bottom right (0x20610 / 0x7a808)

// a list in the exe's style: the orange bar behind the chosen line, chosen text white, the rest black
static void list(int x, int y, int w, int step, const char *const *items, int n, int sel, bool centre, const bool *dim)
{
    hud_font(2);
    hud_shadow(false);
    hud_rect(x, y + sel * step - 1, w, step, C_BAR, 1);
    for (int i = 0; i < n; i++) {
        u16 c = i == sel ? C_WHITE : (dim && dim[i]) ? RGB15(12, 12, 14) : C_BLACK;
        hud_text(centre ? x + w / 2 : x + 3, y + i * step + 1, centre ? 2 : 0, items[i], c);
    }
    hud_shadow(true);
}

//[...][...]------------ Buy Attributes (0x2718c / 0x2765c)
// +2 per purchase up to 100, priced by the value's tens (0xa5cd8); what was bought on this visit can be sold back
// for the same price; leaving saves
static int attr_ch, attr_sel; static u8 attr_in[ATTR_N];
static const char *const ATTR_DESC[ATTR_N] = {        // the game's own descriptions (translated by lang.c)
    XT_20,
    XT_21,
    XT_22,
    XT_23,
    XT_24,
    XT_25,
    XT_26,
};
static void attr_enter(int ch) { attr_ch = ch; memcpy(attr_in, career.attr[ch], ATTR_N); }

static bool attr_frame(u32 down, const char *rname)
{
    const u16 G = RGB15(16, 31, 14), D = RGB15(6, 8, 12), B = RGB15(31, 18, 4);
    u8 *a = career.attr[attr_ch];
    if (down & KEY_UP) { attr_sel = (attr_sel + ATTR_N - 1) % ATTR_N; dirty = true; }
    if (down & KEY_DOWN) { attr_sel = (attr_sel + 1) % ATTR_N; dirty = true; }
    if (down & (KEY_RIGHT | KEY_A)) {
        int c = attr_cost(a[attr_sel]);
        if (a[attr_sel] < 100 && career.cash >= c) { a[attr_sel] += 2; career.cash -= c; dirty = true; }
    }
    if ((down & KEY_LEFT) && a[attr_sel] > attr_in[attr_sel]) { a[attr_sel] -= 2; career.cash += attr_cost(a[attr_sel]); dirty = true; }
    if (down & KEY_B) {
        if (memcmp(attr_in, a, ATTR_N)) fe_save();
        fe_enter(FE_RIDER); fe_show_rider(sel_rider); return false;
    }
    char s[48], c[20];
    if (dirty) {
        char nm[16]; snprintf(nm, sizeof(nm), "%s", rname); nm[0] = toupper((u8)nm[0]);
        subui_begin(XT_27, nm);
        subui_text(20, 32, 2, 0, ATTR_NAME[attr_sel], SU_ORANGE);
        snprintf(s, sizeof(s), "%d", a[attr_sel]); subui_text(236, 32, 2, 1, s, SU_WHITE);
        subui_bar(20, 46, 216, 7, a[attr_sel], 100);
        int y = 60 + subui_wrap(20, 60, 216, 3, ATTR_DESC[attr_sel], SU_BLUE, 10);
        y = y + 4 + subui_wrap(20, y + 4, 216, 3, XT_28, SU_GREY, 10);
        if (a[attr_sel] < 100) { career_fmt(c, attr_cost(a[attr_sel])); snprintf(s, sizeof(s), "+2  %s", c); }
        else snprintf(s, sizeof(s), "100");
        subui_text(20, 140, 2, 0, s, SU_WHITE);
        career_fmt(c, career.cash); subui_text(20, 154, 2, 0, XT_29, SU_GREY);
        subui_text(236, 154, 2, 1, c, SU_GREEN);
        sub_hints("Buy", "Done", "LEFT -2");
        subui_show();
        dirty = false;
    }
    lines_to(SX(0x6e), SX(0x40));
    hud_begin();
    frame(XT_27, false);
    hud_font(2);
    career_fmt(c, career.cash); snprintf(s, sizeof(s), "You have: %s", c); hud_text(SX(0x4d), SX(0x38), 0, s, C_WHITE);
    hud_rect_a(4, SX(0x4c), 248, ATTR_N * 15 + 4, C_BLACK, 12, 1);
    for (int k = 0; k < ATTR_N; k++) {
        int y = SX(0x4c) + 3 + k * 15;
        bool on = k == attr_sel;
        if (on) hud_rect(6, y - 2, 244, 14, C_BAR, 1);
        hud_font(2);
        hud_text(10, y, 0, ATTR_NAME[k], C_WHITE);
        hud_rect(94, y + 2, 100, 7, D, 2);
        if (attr_in[k] > 0) hud_rect(94, y + 2, attr_in[k], 7, B, 3);
        if (a[k] > attr_in[k]) hud_rect(94 + attr_in[k], y + 2, a[k] - attr_in[k], 7, G, 3);
        hud_font(3);
        snprintf(s, sizeof(s), "%d", a[k]); hud_text(198, y + 1, 0, s, C_WHITE);
        if (a[k] < 100) { career_fmt(c, attr_cost(a[k])); hud_text(250, y + 1, 1, c, career.cash >= attr_cost(a[k]) ? G : RGB15(20, 20, 20)); }
    }
    hud_font(0);
    hud_end();
    return false;
}

//[...][...]------------ screens
bool fe_frame(u32 down, int *event, int *rider, int nriders, const char *const *names, const int *best_ms)
{
    frames++; anim++; reap(false); hold = false;
    music_duck(screen == FE_VOICE ? 0 : 100);           // port: the music fades out on Voice Clips and back in after
    music_duck_step();
    char s[64];
    int lst[8], n;
    switch (screen) {
    case FE_TITLE: {
        if (down & (KEY_START | KEY_A)) { fe_enter(FE_MAIN); return false; }
        if (dirty) {
            subui_begin("SSX 3", "GizWipe");
            subui_text(128, 70, 1, 2, "Press Start", SU_WHITE);
            subui_text(128, 100, 3, 2, "Gizmondo SSX 3 on the Nintendo DS", SU_BLUE);
            subui_show();
            dirty = false;
        }
        hud_begin();
        // title.bmp fades up from black over the first second, the snow falls over it
        int k = frames * 31 / 60; if (k > 31) k = 31;
        draw("title", 0, -32, 256, 256, 0);
        if (k < 31) hud_rect_a(0, 0, 256, 192, C_BLACK, 31 - k, 3);
        draw_flakes();
        hud_font(2);
        if (frames > 60 && ((frames / 30) & 1)) hud_text(128, 168, 2, "Press Start", C_WHITE);
        // port: a Rumble Pak in slot 2 is announced for the first 4 seconds on the title, with a short buzz
        if (rumble_present() && frames > 30 && frames < 270) {
            if (frames == 31) rumble_pulse(250, 2);
            hud_text(128, 152, 2, L("Rumble Pak detected"), RGB15(31, 24, 6));
        }
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_MAIN: {                                    // 0x23dfc
        // (Voice Clips: port addition)
        static const char *const ITEMS[6] = { XT_30, XT_31, XT_32, "Multiplayer", "Voice Clips", "Options" };
        static const char *const HELP[6] = { XT_33,
                                             XT_34,
                                             XT_35,
                                             XT_36,
                                             "Record your own sounds with the microphone for SUPER UBER, winning, passing and knockdowns.",
                                             XT_37 };
        if (down & KEY_UP) { sel_main = (sel_main + 5) % 6; dirty = true; }
        if (down & KEY_DOWN) { sel_main = (sel_main + 1) % 6; dirty = true; }
        if (down & KEY_B) { fe_enter(FE_TITLE); return false; }
        if (down & (KEY_A | KEY_START)) {
            if (sel_main == 5) { fe_enter(FE_OPT); sel_opt = 0; return false; }
            if (sel_main == 4) { fe_enter(FE_VOICE); vc_edit = false; return false; }
            if (sel_main == 3) { fe_enter(FE_MP); sel_mp = 0; return false; }
            play_mode = sel_main; fe_enter(FE_PEAK); return false;
        }
        if (dirty) {
            subui_begin("Main Menu", NULL);
            subui_text(20, 34, 1, 0, ITEMS[sel_main], SU_ORANGE);
            subui_wrap(20, 56, 216, 2, HELP[sel_main], SU_WHITE, 13);
            char c[20]; career_fmt(c, career.cash);
            subui_text(20, 150, 2, 0, XT_29, SU_GREY); subui_text(236, 150, 2, 1, c, SU_GREEN);
            sub_hints("Accept", "Back", NULL);
            subui_show();
            dirty = false;
        }
        lines_to(SX(0x84) - 3, SX(0x55) - 4);
        hud_begin();
        frame("Main Menu", true);
        list(SX(0x84), SX(0x55) - 6, SX(0x96), 12, ITEMS, 6, sel_main, true, NULL);   // (the description: bottom screen only)
        hud_end();
        return false;
    }
    case FE_PEAK: {                                    // 0x2ec6c
        static const char *const PK[3] = { "Peak 1", "Peak 2", "Peak 3" };
        if (down & (KEY_UP | KEY_LEFT)) { sel_peak = (sel_peak + 2) % 3; dirty = true; map_t = 0; }
        if (down & (KEY_DOWN | KEY_RIGHT)) { sel_peak = (sel_peak + 1) % 3; dirty = true; map_t = 0; }
        if (down & KEY_B) { if (fe_mp()) { net_s.phase = NP_LOBBY; fe_enter(FE_MPLOBBY); } else fe_enter(FE_MAIN); return false; }
        if (down & KEY_A) { fe_enter(FE_EVENT); sel_event = 0; map_t = 0; return false; }
        if (dirty) {
            snprintf(s, sizeof(s), "Peak %d", sel_peak + 1);
            subui_begin(s, career_mode ? XT_30 : play_mode == 3 ? "Multiplayer" : play_mode == 2 ? XT_32 : XT_31);
            int y = 32 + subui_wrap(20, 32, 216, 3, PEAK_DESC[sel_peak], SU_WHITE, 10);
            int pl[8], np = peak_events(sel_peak + 1, pl);
            y += 6;
            for (int i = 0; i < np && y < 160; i++) {          // the peak's events: kind, and how you placed
                u8 pf = career.placed[pl[i]];
                subui_text(20, y, 2, 0, EVENTS[pl[i]].name, career_unlocked(pl[i]) ? SU_WHITE : SU_GREY);
                subui_text(132, y + 2, 3, 0, L_kind_short(EVENTS[pl[i]].kind), SU_BLUE);
                const char *m = (pf & 1) ? "1st" : (pf & 2) ? "2nd" : (pf & 4) ? "3rd" : career_unlocked(pl[i]) ? "" : "LOCKED";
                subui_text(236, y + 2, 3, 1, m, (pf & 7) ? SU_GREEN : SU_GREY);
                y += 13;
            }
            sub_hints("Accept", "Back", NULL);
            subui_show();
            dirty = false;
        }
        lines_to(SX(0x6f), SX(0x38) - 3);
        hud_begin();
        frame("Select Peak", false);
        list(SX(0x33) - 16, SX(0x4a), SX(0x45) + 8, 13, PK, 3, sel_peak, true, NULL);
        // the peak's line (PeakSel_n) wipes in over the map from the left (0x2ec6c: 140 px, time / 4)
        map_t += 7; if (map_t > 112) map_t = 112;
        draw("peaksel_bg", SX(0x78), SX(0x38), 112, 112, 1);
        Img pk; snprintf(s, sizeof(s), "peaksel_%d", sel_peak + 1); pk = img(s);
        hud_image(pk.tex, SX(0x78), SX(0x38), map_t, 112, 0, 0, pk.w * map_t / 112, pk.h, C_WHITE, 2);
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_EVENT: {                                   // 0x307ac
        n = peak_events(sel_peak + 1, lst);
        if (down & KEY_UP) { sel_event = (sel_event + n - 1) % n; dirty = true; frames = 0; map_t = 0; }
        if (down & KEY_DOWN) { sel_event = (sel_event + 1) % n; dirty = true; frames = 0; map_t = 0; }
        if (down & KEY_B) { fe_enter(FE_PEAK); map_t = 0; return false; }
        const Event *e = &EVENTS[lst[sel_event]];
        bool open = career_unlocked(lst[sel_event]);
        // locked events (career earnings below the event table's +0x30) stay shut; SELECT+A opens them anyway
        if (play_mode == 2) open = fe_has_shadow(lst[sel_event]);   // Shadow Racing: only the tracks you[...]
        if ((down & KEY_A) && play_mode == 2 && !open) msg_t = 120;    // "[...]"
        else if ((down & KEY_A) && (open || (keysHeld() & KEY_SELECT))) {
            *event = lst[sel_event];
            if (fe_mp()) {                             // the session's event: everyone picks a rider, the host first
                net_s.event = lst[sel_event]; net_s.chooser = 0; net_s.go = 0; net_s.paused_by = 0xff;
                net_ctl.rider = 0xff; net_ctl.ready = 0; net_s.phase = NP_RIDER; mp_hover = -1;
            }
            fe_enter(FE_RIDER); fe_show_rider(sel_rider); return false;
        }
        if (msg_t > 0) msg_t--;
        if (dirty) {
            snprintf(s, sizeof(s), "Peak %d", e->peak);
            subui_begin(e->name, s);
            subui_text(20, 32, 2, 0, L_kind(e->kind), SU_ORANGE);
            int y = 46 + subui_wrap(20, 46, 216, 3, e->desc, SU_WHITE, 10);
            y += 4;
            // port: the course's screenshots cycle on the right of the panel (they were a small inset over the top
            // screen's map); the times, rank and cash go down the left, each prize beside its place
            ss_y = y + 1; ss_h = 156 - ss_y; if (ss_h > 78) ss_h = 78; ss_w = ss_h * 4 / 3; ss_shown = -1;
            if (e->timed && best_ms[lst[sel_event]] > 0) {
                fmt(s, best_ms[lst[sel_event]]);
                subui_text(20, y, 3, 0, "Best Time", SU_GREY); subui_text(124, y, 3, 1, s, SU_GREEN); y += 11;
            }
            u8 pf = career.placed[lst[sel_event]];
            if (pf & 7) { subui_text(20, y, 3, 0, "Rank", SU_GREY); subui_text(124, y, 3, 1, (pf & 1) ? "1st" : (pf & 2) ? "2nd" : "3rd", SU_GREEN); y += 11; }
            if (career_mode) {                        // the event's cash (0x7098c) and what opens it
                char a2[20]; const s32 *r = EV_REWARD[lst[sel_event]];
                for (int k = 0; k < 3 && r[k] && y < 156; k++) {
                    career_fmt(a2, r[k]); subui_text(20, y, 3, 0, PLACE_TXT[k], SU_GREY); subui_text(46, y, 3, 0, a2, SU_GREEN); y += 10;
                }
                if (!open && y < 156) { career_fmt(a2, EV_UNLOCK[lst[sel_event]]); subui_text(20, y, 3, 0, "LOCKED", SU_RED); subui_text(20, y + 10, 3, 0, a2, SU_RED); y += 20; }
                career_fmt(a2, career.cash); subui_text(20, 160, 3, 0, XT_29, SU_GREY); subui_text(236, 160, 3, 1, a2, SU_GREEN);
            }
            sub_hints("Accept", "Back", open ? NULL : "SELECT+A");
            subui_show();
            dirty = false;
            {   // the other tracks' map, route and screenshots go (not the frame, fonts and list: re-uploading
                // those on every move was what tore the top screen)
                char keep1[20], keep2[20], keep3[20];
                snprintf(keep1, sizeof(keep1), "peak%d_bg", e->peak); snprintf(keep2, sizeof(keep2), "peak%d_t%d", e->peak, e->tnum);
                snprintf(keep3, sizeof(keep3), "p%dt%dss", e->peak, e->tnum);
                for (int i = ncache - 1; i >= 0; i--) {
                    const char *nm = cache[i].name;
                    bool trk = !strncmp(nm, "peak", 4) || (nm[0] == 'p' && nm[1] >= '0' && nm[1] <= '9');
                    if (trk && strcmp(nm, keep1) && strcmp(nm, keep2) && strncmp(nm, keep3, strlen(keep3))) retire_at(i);
                }
            }
        }
        {   // the slideshow on the bottom screen: a new picture every two seconds
            int k = (frames / 120) % 3;
            if (k != ss_shown && ss_h >= 30) {
                char path[48]; snprintf(path, sizeof(path), "nitro:/menu/p%dt%dss%d.subi", e->peak, e->tnum, 1 + k);
                if (subui_image(236 - ss_w, ss_y, ss_w, ss_h, path)) subui_show();
                ss_shown = k;
            }
        }
        lines_to(SX(0x6e), SX(0x38) - 3);
        hud_begin();
        frame(XT_38, false);
        const char *names_[8]; bool dim[8];
        for (int i = 0; i < n; i++) { names_[i] = EVENTS[lst[i]].name; dim[i] = play_mode == 2 ? !fe_has_shadow(lst[i]) : !career_unlocked(lst[i]); }
        list(SX(10), SX(0x49) - 2, SX(0x6e), 13, names_, n, sel_event, false, dim);
        hud_font(3);
        snprintf(s, sizeof(s), "%s", L_kind(e->kind)); hud_text(SX(10), SX(0x49) + n * 13 + 2, 0, s, C_TITLE);
        if (play_mode == 2 && msg_t > 0) wrap_hud(SX(10), SX(0x49) + n * 13 + 12, SX(0x6e), XT_39, RGB15(31, 12, 8), 9);
        else if (play_mode == 2 && !open) hud_text(SX(10), SX(0x49) + n * 13 + 12, 0, "No shadow yet", RGB15(31, 12, 8));
        else if (!open) hud_text(SX(10), SX(0x49) + n * 13 + 12, 0, "LOCKED  (SELECT+A)", RGB15(31, 12, 8));
        else if (e->timed && best_ms[lst[sel_event]] > 0) { char b[16]; fmt(b, best_ms[lst[sel_event]]); snprintf(s, sizeof(s), "Best %s", b); hud_text(SX(10), SX(0x49) + n * 13 + 12, 0, s, RGB15(16, 31, 14)); }
        hud_font(2);
        snprintf(s, sizeof(s), "Peak %d", e->peak); hud_text(SX(0xbe), SX(0x2a), 0, s, C_WHITE);
        // the peak map and the route wiping in (0x307ac); the course's screenshots cycle on the bottom screen
        map_t += 7; if (map_t > 112) map_t = 112;
        snprintf(s, sizeof(s), "peak%d_bg", e->peak); draw(s, SX(0x78), SX(0x38), 112, 112, 1);
        Img rt; snprintf(s, sizeof(s), "peak%d_t%d", e->peak, e->tnum); rt = img(s);
        hud_image(rt.tex, SX(0x78), SX(0x38), map_t, 112, 0, 0, rt.w * map_t / 112, rt.h, C_WHITE, 2);
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_RIDER: {
        bool mp = fe_mp(), turn = true;
        if (mp) {                                      // 0x255a4: "Select Rider" on your turn, "[...]" otherwise
            if (net_s.phase == NP_END || (!net_is_host() && net_age(0) > 180)) {
                fe_mp_message(net_s.phase == NP_END ? XT_40 : "Connection lost"); net_leave(); fe_enter(FE_MP); return false;
            }
            if (net_s.phase == NP_LOAD && net_ctl.rider != 0xff) { *event = net_s.event; *rider = net_ctl.rider; fe_leave(); screen = FE_DONE; return true; }
            if (net_s.phase == NP_EVENT || net_s.phase == NP_LOBBY) { fe_enter(FE_MPLOBBY); return false; }
            *event = net_s.event;
            int ch = net_s.chooser;
            turn = ch == net_me() && net_ctl.rider == 0xff;
            if (net_is_host()) {                       // the host hands the turn on as each player picks
                net_s.ctl[0] = net_ctl;
                while (ch < NET_MAX && (!((net_s.mask >> ch) & 1) || net_s.ctl[ch].rider != 0xff)) ch++;
                net_s.chooser = ch < NET_MAX ? ch : 0;
                if (ch >= NET_MAX) net_s.phase = NP_LOAD;
            }
            if (!turn) {                               // show the chooser's cursor
                int h = net_s.ctl[net_s.chooser].hover;
                if (net_s.chooser != net_me() && h < nriders && h != mp_hover) { mp_hover = h; sel_rider = h; fe_show_rider(h); dirty = true; }
                down = 0;
            } else { if (mp_hover >= 0 && sel_rider != net_ctl.hover) fe_show_rider(sel_rider); mp_hover = -1; }
            net_ctl.hover = sel_rider;
        }
        if (mp && turn && (down & (KEY_A | KEY_START))) { net_ctl.rider = sel_rider; net_ctl.hover = sel_rider; dirty = true; return false; }
        if (mp && (down & (KEY_B | KEY_X | KEY_Y))) {
            if ((down & KEY_B) && net_is_host()) { net_s.phase = NP_EVENT; fe_enter(FE_EVENT); }
            return false;
        }
        if (down & KEY_LEFT) { sel_rider = (sel_rider + nriders - 1) % nriders; fe_show_rider(sel_rider); dirty = true; }
        if (down & KEY_RIGHT) { sel_rider = (sel_rider + 1) % nriders; fe_show_rider(sel_rider); dirty = true; }
        if (down & KEY_B) { fe_enter(FE_EVENT); return false; }
        if (down & (KEY_A | KEY_START)) { *rider = sel_rider; fe_leave(); screen = FE_DONE; return true; }
        if (down & KEY_Y) { fe_enter(FE_ATTR); attr_enter(fe_char_of(sel_rider)); return false; }
        if (down & KEY_X) { fe_enter(FE_PROFILE); prof_page = 0; return false; }
        if (dirty) {
            char nm[16]; snprintf(nm, sizeof(nm), "%s", names[sel_rider]); nm[0] = toupper((u8)nm[0]);
            subui_begin(nm, EVENTS[*event].name);
            // the rider's bio (the attributes are on the top screen already)
            subui_text(20, 32, 2, 0, PROF_TITLE[3], SU_ORANGE);
            subui_wrap(20, 48, 216, 3, PROFILE[fe_char_of(sel_rider)].bio, SU_WHITE, 10);
            { char xy[48]; snprintf(xy, sizeof(xy), "X %s   Y %s", LX("Profile", XT_41), L("Attributes"));
              sub_hints(fe_mp() ? "Select" : "Race", "Back", fe_mp() ? NULL : xy); }
            subui_show();
            dirty = false;
        }
        fe_draw_rider(frames * 6, 172, 94, 112, 8, false);   // on its turntable, in the middle of the right-hand square
        lines_to(114, SX(0x38) - 3);                   // port: right of the stat bars (it ran through them)
        hud_begin();
        frame(mp && !turn ? XT_42 : XT_43, true);
        // the riders' silhouettes (characters1/2: ten across, the chosen one orange)
        int cx = fe_char_of(sel_rider);                // the strips are in the exe's rider order
        Img ch = img(cx < 5 ? "characters1" : "characters2");
        int row = cx % 5;
        // port: the strip as wide as the room left of the dashed line (it was 77 x 26), the name and stats lower
        const int SW = 102, SH = 35, NY = SX(0x38) + 1 + SH + 7;
        hud_image(ch.tex, SX(10), SX(0x38) + 1, SW, SH, 0, row * 33, 96, 33, C_WHITE, 2);
        snprintf(s, sizeof(s), "%s", names[sel_rider]); s[0] = toupper((u8)s[0]);
        hud_font(1); hud_text(SX(10), NY, 0, s, C_WHITE);
        const u8 *a = career.attr[fe_char_of(sel_rider)];
        hud_font(3);
        for (int k = 0; k < ATTR_N; k++) {
            int y = NY + 21 + k * 10;
            hud_text(SX(10), y, 0, ATTR_NAME[k], C_TITLE);
            hud_rect(SX(10) + 58, y + 2, 40, 5, RGB15(6, 8, 12), 1);
            hud_rect(SX(10) + 58, y + 2, a[k] * 40 / 100, 5, RGB15(31, 18, 4), 2);
        }
        if (mp) {                                      // whose turn it is, and who has picked
            hud_font(3);
            int yy = SX(0x38) + 4;
            for (int p = 0; p < NET_MAX; p++) {
                if (!((net_s.mask >> p) & 1)) continue;
                const NetCtl *c = p == net_me() ? &net_ctl : &net_s.ctl[p];
                char nm[12]; snprintf(nm, sizeof(nm), "%s", c->name);
                snprintf(s, sizeof(s), "%s%s", nm, p == net_s.chooser && net_s.phase == NP_RIDER ? "  choosing" : c->rider != 0xff ? "  ready" : "");
                hud_text(SX(0xc0), yy, 0, s, p == net_me() ? C_TITLE : C_WHITE); yy += 10;
            }
        } else {
        }
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_ATTR:
        return attr_frame(down, names[sel_rider]);
    case FE_PROFILE: {                                 // Rider Profile (0x2b6cc):[...]BIO
        int c = fe_char_of(sel_rider);
        if (down & (KEY_RIGHT | KEY_R | KEY_DOWN)) { prof_page = (prof_page + 1) & 3; dirty = true; arr_r = 8; }
        if (down & (KEY_LEFT | KEY_L | KEY_UP)) { prof_page = (prof_page + 3) & 3; dirty = true; arr_l = 8; }
        if (down & (KEY_B | KEY_A | KEY_START)) { fe_enter(FE_RIDER); fe_show_rider(sel_rider); return false; }
        if (dirty) {
            subui_begin(XT_41, PROF_TITLE[prof_page]);
            int y = 32;
            if (prof_page == 0) for (int i = 0; i < 8; i++) { subui_text(20, y, 3, 0, PROF_L0[i], SU_ORANGE); subui_text(110, y, 3, 0, PROFILE[c].dna[i], SU_WHITE); y += 16; }
            else if (prof_page == 1) for (int i = 0; i < 8 && y < 160; i++) { subui_text(20, y, 3, 0, PROF_L1[i], SU_ORANGE); subui_text(110, y, 3, 0, PROFILE[c].fav[i], SU_WHITE); y += 16; }
            else if (prof_page == 2) for (int i = 0; i < 4 && y < 160; i++) { y += subui_wrap(20, y, 216, 3, PROF_Q[i], SU_ORANGE, 10); y += 2 + subui_wrap(28, y, 208, 3, PROFILE[c].qa[i], SU_WHITE, 10); }
            else subui_wrap(20, y, 216, 3, PROFILE[c].bio, SU_WHITE, 10);
            sub_hints(NULL, "Back", "< >");
            subui_show();
            dirty = false;
        }
        // the rider in the middle of the screen, turning, playing a winning celebration (react_pos..9, 23-27:
        // what a first place plays at the finish); the profile pages are on the bottom screen
        fe_draw_rider(0x600 + frames * 6, 128, 96, 100, 23 + c % 5, frames == 1);   // turning like the rider select
        hud_begin();
        frame(XT_41, true);
        hud_end();
        return false;
    }
    case FE_SHADOWS: {                                 // Shadow Manager: "[...]"
        int ev[15], n = 0; char row[15][40];
        for (int e = 0; e < 15; e++) if (fe_shadow_info(e, row[n], sizeof(row[n]))) ev[n++] = e;
        if (sel_sh >= n) sel_sh = n ? n - 1 : 0;
        if (!confirm) {
            if (n && (down & KEY_UP)) { sel_sh = (sel_sh + n - 1) % n; dirty = true; }
            if (n && (down & KEY_DOWN)) { sel_sh = (sel_sh + 1) % n; dirty = true; }
            if (n && (down & KEY_A)) { confirm = true; dirty = true; }
            else if (n && (down & KEY_X)) {             // Watch Shadow: the shadow on its own, as a replay
                watching = true; *event = ev[sel_sh]; fe_leave(); screen = FE_DONE; return true;
            }
            else if (down & KEY_B) { fe_enter(FE_OPT); sel_opt = 5; return false; }
        } else {
            if (down & KEY_A) { fe_delete_shadow(ev[sel_sh]); confirm = false; dirty = true; }
            else if (down & KEY_B) { confirm = false; dirty = true; }
        }
        if (dirty) {
            subui_begin(XT_44, NULL);
            subui_wrap(20, 32, 216, 3, XT_45, SU_BLUE, 10);
            if (!n) subui_text(128, 90, 2, 2, XT_46, SU_GREY);
            for (int i = 0; i < n && i < 9; i++) subui_text(20, 50 + i * 13, 3, 0, row[i], i == sel_sh ? SU_ORANGE : SU_WHITE);
            if (confirm) sub_hints("Delete", "Cancel", NULL);
            else sub_hints("Delete", "Back", n ? "X Watch Shadow" : NULL);
            subui_show();
            dirty = false;
        }
        lines_to(SX(0x84) - 3, SX(0x55) - 4);
        hud_begin();
        frame(XT_47, true);
        const char *it[15]; for (int i = 0; i < n; i++) it[i] = row[i];
        hud_font(2);
        if (!n) hud_text(128, SX(0x55), 2, XT_46, C_BLACK);
        else list(SX(0x60), SX(0x48), SX(0xc0), 11, it, n, sel_sh, true, NULL);
        if (confirm) {
            hud_rect_a(SX(30), SX(0x80), SX(0xe0) - SX(30), 40, C_BLACK, 24, 4);
            hud_font(3); wrap_hud(SX(30) + 6, SX(0x80) + 4, SX(0xe0) - SX(30) - 12, XT_48, C_WHITE, 10);
        }
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_MP: {                                      // 0x1b0ac: Host / Join, the device name
        static const char *const IT[2] = { "Host", "Join" };
        static const char *const HL[2] = { XT_49, XT_50 };
        if (down & (KEY_UP | KEY_DOWN)) { sel_mp ^= 1; dirty = true; }
        if (down & KEY_B) { fe_enter(FE_MAIN); return false; }
        if (down & KEY_A) {
            if (!net_init()) fe_mp_message("Wireless could not be started.");
            else if (sel_mp == 0) { net_host(); play_mode = 3; fe_enter(FE_MPLOBBY); return false; }
            else { net_scan(); play_mode = 3; sel_game = 0; fe_enter(FE_MPJOIN); return false; }
        }
        if (mp_msg_t > 0) mp_msg_t--;
        if (dirty) {
            subui_begin("Multiplayer", NULL);
            subui_text(20, 34, 1, 0, IT[sel_mp], SU_ORANGE);
            subui_wrap(20, 56, 216, 2, HL[sel_mp], SU_WHITE, 13);
            subui_wrap(20, 90, 216, 3, XT_36, SU_BLUE, 10);
            subui_text(20, 130, 2, 0, "Device Name", SU_GREY); subui_text(236, 130, 2, 1, net_my_name(), SU_WHITE);
            sub_hints("Accept", "Back", NULL);
            subui_show();
            dirty = false;
        }
        lines_to(SX(0x84) - 3, SX(0x55) - 4);
        hud_begin();
        frame("Multiplayer", true);
        list(SX(0x84), SX(0x55), SX(0x96), 12, IT, 2, sel_mp, true, NULL);
        hud_font(2);
        hud_text(SX(0xa0) + 40, SX(0x86), 2, "Device Name", C_TITLE);
        hud_text(SX(0xa0) + 40, SX(0x92), 2, net_my_name(), C_WHITE);
        hud_rect_a(SX(10) - 2, SX(0x55) + sel_mp * 12 - 2, SX(0x75) + 4, 34, C_BLACK, 14, 1);
        hud_font(3);
        wrap_hud(SX(10), SX(0x55) + sel_mp * 12, SX(0x75), mp_msg_t > 0 ? mp_msg : HL[sel_mp], mp_msg_t > 0 ? RGB15(31, 12, 8) : C_TITLE, 10);
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_MPJOIN: {                                  // 0x1b770 (joining): the hosts in range, then Connecting
        static NetGame g[6]; static int ng; static bool trying;
        if (frames == 1) trying = false;
        if (frames % 30 == 1) {
            subui_begin("Join", "Multiplayer");
            subui_wrap(20, 34, 216, 2, trying ? "Connecting" : ng ? XT_51 : XT_52, SU_WHITE, 13);
            subui_wrap(20, 60, 216, 3, XT_50, SU_BLUE, 10);
            subui_text(20, 130, 2, 0, "Device Name", SU_GREY); subui_text(236, 130, 2, 1, net_my_name(), SU_WHITE);
            sub_hints(trying || !ng ? NULL : "Connect", "Back", NULL);
            subui_show();
        }
        if (!trying) {
            ng = net_games(g, 6);
            if (ng && sel_game >= ng) sel_game = ng - 1;
            if (down & KEY_UP) sel_game = ng ? (sel_game + ng - 1) % ng : 0;
            if (down & KEY_DOWN) sel_game = ng ? (sel_game + 1) % ng : 0;
            if ((down & KEY_A) && ng) { trying = net_join(g[sel_game].index); }
        } else {
            int st = net_join_status();
            if (st == 1) { fe_enter(FE_MPLOBBY); return false; }
            if (st < 0) { trying = false; fe_mp_message(XT_53); net_scan(); }
        }
        if (down & KEY_B) { net_leave(); fe_enter(FE_MP); return false; }
        if (mp_msg_t > 0) mp_msg_t--;
        lines_to(SX(0x84) - 3, SX(0x55) - 4);
        hud_begin();
        frame("Join", true);
        hud_font(2);
        if (trying) hud_text(128, SX(0x50), 2, "Connecting", C_WHITE);
        else {
            snprintf(s, sizeof(s), XT_54 "%.*s", (frames / 20) % 4, "...");
            hud_text(128, SX(0x34), 2, s, C_WHITE);
            if (!ng) hud_text(128, SX(0x6c), 2, "None Found", C_TITLE);
            else {
                const char *it[6]; char nb[6][32];
                for (int i = 0; i < ng; i++) { snprintf(nb[i], 32, "%s  (%d/%d)", g[i].name, g[i].players, NET_MAX); it[i] = nb[i]; }
                list(SX(0x40), SX(0x48), SX(0xc0), 13, it, ng, sel_game, true, NULL);
            }
        }
        if (mp_msg_t > 0) { hud_font(3); hud_text(128, SX(0xb0), 2, mp_msg, RGB15(31, 12, 8)); }
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_MPLOBBY: {                                 // 0x1b770: the players; the host accepts them and starts
        bool h = net_is_host();
        static u32 lob_pend;
        if (frames % 30 == 1 || lob_pend != net_pending) {
            lob_pend = net_pending;
            subui_begin(h ? "Host" : "Join", "Multiplayer");
            int y = 34;
            for (int p = 0; p < NET_MAX; p++) {
                if (!((net_s.mask >> p) & 1)) continue;
                const char *nm = p == net_me() ? net_ctl.name : net_s.ctl[p].name;
                char pn[4]; snprintf(pn, sizeof(pn), "%d", p + 1);
                subui_text(20, y, 2, 0, pn, SU_GREY); subui_text(36, y, 2, 0, nm, p == net_me() ? SU_ORANGE : SU_WHITE);
                if (p == 0) subui_text(236, y, 3, 1, "Host", SU_BLUE);
                y += 15;
            }
            subui_wrap(20, 110, 216, 3, h ? XT_49 : XT_50, SU_BLUE, 10);
            {   // the buttons this moment (the panel is redrawn every 30 frames and when a request comes in)
                int np = 0; for (int p = 0; p < NET_MAX; p++) np += (net_s.mask >> p) & 1;
                if (h && net_pending) sub_hints("Accept", "Reject", NULL);
                else sub_hints(h && np > 1 ? "Start" : NULL, "Quit", NULL);
            }
            subui_show();
        }
        if (!h) {
            if (net_s.phase == NP_RIDER && ((net_s.mask >> net_me()) & 1)) {
                *event = net_s.event; net_ctl.rider = 0xff; net_ctl.ready = 0; mp_hover = -1;
                fe_enter(FE_RIDER); fe_show_rider(sel_rider); return false;
            }
            bool lost = net_age(0) > 180, rej = net_me() && net_s.ctl[net_me()].ready == 0xee;
            if (net_s.phase == NP_END || lost || rej) {
                fe_mp_message(rej ? XT_55 : net_s.phase == NP_END ? XT_40 : "Connection lost");
                net_leave(); fe_enter(FE_MP); return false;
            }
        } else if (net_pending) {                      // "[...]"
            int p = 1; while (p < NET_MAX && !((net_pending >> p) & 1)) p++;
            if (down & KEY_A) net_accept(p, true);
            else if (down & KEY_B) net_accept(p, false);
            down = 0;
        } else {
            int np = 0; for (int p = 0; p < NET_MAX; p++) np += (net_s.mask >> p) & 1;
            if ((down & (KEY_A | KEY_START)) && np > 1) { net_s.phase = NP_EVENT; fe_enter(FE_PEAK); return false; }
        }
        if (down & KEY_B) {
            if (h) { net_s.phase = NP_END; for (int i = 0; i < 20; i++) { net_update(); swiWaitForVBlank(); } }
            net_leave(); fe_enter(FE_MP); return false;
        }
        lines_to(SX(0x84) - 3, SX(0x55) - 4);
        hud_begin();
        frame(h ? "Host" : "Join", true);
        hud_font(2);
        const char *top = h ? XT_56 : !((net_s.mask >> net_me()) & 1) ? "Waiting" :
                          net_s.phase == NP_EVENT ? XT_57 : "Connected";
        wrap_hud(SX(0x20), SX(0x34), SX(0x100), top, C_WHITE, 12);
        int y = SX(0x5a);
        for (int p = 0; p < NET_MAX; p++) {
            if (!((net_s.mask >> p) & 1)) continue;
            const char *nm = p == net_me() ? net_ctl.name : net_s.ctl[p].name;
            snprintf(s, sizeof(s), "%d  %s%s", p + 1, nm, p == 0 ? "  (host)" : "");
            hud_text(SX(0x30), y, 0, s, p == net_me() ? C_TITLE : C_WHITE); y += 13;
        }
        if (h && net_pending) {
            int p = 1; while (p < NET_MAX && !((net_pending >> p) & 1)) p++;
            hud_rect_a(SX(0x20), SX(0xa0), SX(0x100), 30, C_BLACK, 20, 3);
            snprintf(s, sizeof(s), XT_58, net_s.ctl[p].name);
            hud_font(3); hud_text(128, SX(0xa4), 2, s, C_WHITE);
        } else if (h) {
            int np = 0; for (int p = 0; p < NET_MAX; p++) np += (net_s.mask >> p) & 1;
            if (np < 2) { hud_font(3); hud_text(128, SX(0xb4), 2, XT_59, C_TITLE); }
        }
        hud_font(0);
        hud_end();
        return false;
    }
    case FE_LANG: {                                    // 0x1fbd8 / 0x1fd8c: five flags circling the globe
        static int rot, target, spd;
        if (frames == 1) { rot = target = g_lang * 0x198; spd = 0; }
        if (spd == 0) {
            // (RIGHT brings the flag on the right round to the front, LEFT the one on the left)
            if (down & KEY_RIGHT) { g_lang = (g_lang + 4) % 5; target = g_lang * 0x198; spd = -16; dirty = true; }
            else if (down & KEY_LEFT) { g_lang = (g_lang + 1) % 5; target = g_lang * 0x198; spd = 16; dirty = true; }
            else if (down & (KEY_A | KEY_START)) {
                bool first = g_opt.lang > 4;
                g_opt.lang = g_lang; fe_save();
                fe_enter(first ? FE_TITLE : FE_OPT); return false;
            }
            else if ((down & KEY_B) && g_opt.lang <= 4) { g_lang = g_opt.lang; fe_enter(FE_OPT); return false; }
        }
        if (spd) {                                     // turn 1/5 of the way round at 16 a frame
            rot = (rot + spd) & 0x7ff;
            int d = ((target - rot) & 0x7ff); if (d > 0x400) d -= 0x800;
            if ((d < 0 ? -d : d) <= 16) { rot = target & 0x7ff; spd = 0; }
        }
        if (dirty) {
            static const char *const LN[5] = { "English", "Francais", "Deutsch", "Italiano", "Espanol" };
            subui_begin(L("Select Language"), NULL);           // just the language being chosen, between arrows
            subui_text(128, 84, 1, 2, LN[g_lang], SU_ORANGE);
            for (int k = 0; k < 7; k++) {                      // arrow heads either side
                subui_rect(48 + k, 90 - k, 1, 2 * k + 1, SU_GREY);
                subui_rect(207 - k, 90 - k, 1, 2 * k + 1, SU_GREY);
            }
            sub_hints("Accept", NULL, "< >");
            subui_show();
            dirty = false;
        }
        hud_begin();
        draw(XT_60, 0, 0, 256, 192, 0);
        // flags by depth, back to front; the globe between the back ones and the front ones (Gizmondo 320 x 240 x 4/5)
        static const char *const FL[5] = { "gb", "spain", "italy", "german", "french" };   // slot i: getter 1, 5 - i + 1
        bool used[5] = { 0 }; int prev = -1;
        for (int n = 0; n < 5; n++) {
            int best = -1, bc = 0x20000;
            for (int i = 0; i < 5; i++) {
                if (used[i]) continue;
                int a = (rot + i * 0x198) & 0x7ff, c = sx_sin_tab(a + 0x200);
                if (c < bc) { bc = c; best = i; }
            }
            used[best] = true;
            if (prev < 0 && bc >= 0) draw(XT_61, SX(0x1e), SX(0x14), SX(0x104), SX(200), 2);
            prev = bc;
            int a = (rot + best * 0x198) & 0x7ff, sn = sx_sin_tab(a), cs = sx_sin_tab(a + 0x200);
            int hw = (cs * 0xa0 >> 17) + 0x28, hh = (cs * 0x68 >> 17) + 0x1a;
            int x = ((sn * 500) >> 16) - hw + 0xa0, y = ((cs * 0x58) >> 16) - hh + 0x78;
            Img fl = img(FL[best]);
            hud_image(fl.tex, SX(x), SX(y), SX(hw * 2), SX(hh * 2), 0, 0, 64, 44, C_WHITE, cs < 0 ? 1 : 3);   // (flags at 64 x 64: 128 x 0x58 of 128 x 128)
        }
        if (prev < 0) draw(XT_61, SX(0x1e), SX(0x14), SX(0x104), SX(200), 2);
        hud_end();
        return false;
    }
    case FE_VOICE: {                                   // port addition: Voice Clips (voice.c)
        // the list: 0 Voice Clips on / off, 1 music ducking on / off, 2.. the four slots (greyed while off)
        static const char *const VH[VC_N] = { "Plays when you reach SUPER UBER (in place of the crowd).",
                                              "Plays when you win a race.",
                                              "Plays now and then as you pass another rider.",
                                              "Plays when you knock another rider down." };
        enum { VL_ON = 0, VL_DUCK = 1, VL_SLOT = 2, VL_N = 2 + VC_N };
        // the edit rows: 0 Record, 1 Play, 2 / 3 the two game-sound banks, 4..13 sliders, 14 Clear
        enum { VR_SND = 2, VR_SL = 4, VR_N = 15, VR_CLEAR = 14 };
        static const char *const RN[VR_N] = { "Record", "Play", "Sound 1", "Sound 2", "Pitch", "Autotune", "Chorus", "Echo",
                                              "Echo Delay", "Tone", "Bass", "Low-pass", "High-pass", "Volume", "Clear" };
        if (vc_sel < 0 || vc_sel >= VL_N) vc_sel = 0;
        int slot = vc_sel >= VL_SLOT ? vc_sel - VL_SLOT : 0;
        VoiceFx *fx = voice_fx(slot);
        s8 *val[10] = { &fx->pitch, (s8 *)&fx->tune, (s8 *)&fx->chorus, (s8 *)&fx->echo, (s8 *)&fx->echo_ms,
                        &fx->tone, &fx->bass, (s8 *)&fx->lp, (s8 *)&fx->hp, (s8 *)&fx->vol };
        static const s8 lo[10] = { -12, 0, 0, 0, 1, -5, -5, 0, 0, 0 }, hi[10] = { 12, 10, 10, 10, 10, 5, 5, 10, 10, 10 };
        if (voice_recording()) {                       // a take: A / B (or 2.5 s) ends it
            if ((down & (KEY_A | KEY_B)) || voice_rec_ms() >= VC_MAX * 1000 / VC_RATE - 20) {
                bool ok = voice_record_end();
                vc_msg = ok ? "Recorded." : "Nothing heard - try again closer to the microphone."; vc_msg_t = 150;
                if (ok) voice_play(slot);
                dirty = true;
            }
            down = 0;
        }
        if (!vc_edit) {
            int step = 0;
            if (down & KEY_UP) step = -1;
            if (down & KEY_DOWN) step = 1;
            if (step) {                                // (the slots are skipped while Voice Clips is off)
                int n = g_opt.voice ? VL_N : VL_SLOT;
                vc_sel = (vc_sel + n + step) % n; dirty = true;
            }
            if (vc_sel <= VL_DUCK && (down & (KEY_A | KEY_LEFT | KEY_RIGHT))) {
                u8 *t = vc_sel == VL_ON ? &g_opt.voice : &g_opt.vduck;
                *t = !*t; fe_save(); dirty = true;
                if (!g_opt.voice && vc_sel >= VL_SLOT) vc_sel = VL_ON;
            } else if (vc_sel >= VL_SLOT && g_opt.voice) {
                if (down & KEY_X) voice_play(slot);
                if (down & KEY_A) { vc_edit = true; vc_row = voice_has(slot) ? 1 : 0; vc_confirm = false; dirty = true; }
            }
            if (down & KEY_B) { voice_stop(); fe_enter(FE_MAIN); return false; }
        } else {
            if (down & KEY_UP) { vc_row = (vc_row + VR_N - 1) % VR_N; vc_confirm = false; dirty = true; }
            if (down & KEY_DOWN) { vc_row = (vc_row + 1) % VR_N; vc_confirm = false; dirty = true; }
            int dir = (down & KEY_RIGHT) ? 1 : (down & KEY_LEFT) ? -1 : 0;
            if (dir && vc_row >= VR_SL && vc_row < VR_CLEAR) {
                int k = vc_row - VR_SL, v = *val[k] + dir;
                if (v >= lo[k] && v <= hi[k]) { *val[k] = (s8)v; vc_pend = 40; dirty = true; }
            }
            if (dir && (vc_row == VR_SND || vc_row == VR_SND + 1)) {   // a game sound to play with the clip
                u8 *b = &fx->bank[vc_row - VR_SND];
                *b = (u8)((*b + VC_NSND + dir) % VC_NSND); voice_save(slot); dirty = true;
                voice_bank_preview(*b);
            }
            if (vc_pend > 0 && --vc_pend == 0) { voice_apply(slot); voice_save(slot); }   // the effects re-rendered once the sliders rest
            bool play = (down & KEY_X) != 0;
            if (down & KEY_A) {
                if (vc_row == 0) {
                    if (voice_record(slot)) { vc_msg = NULL; dirty = true; }
                    else { vc_msg = "The microphone could not be started."; vc_msg_t = 150; dirty = true; }
                } else if (vc_row == VR_CLEAR) {
                    if (vc_confirm) { voice_clear(slot); vc_confirm = false; vc_row = 0; vc_msg = "Cleared."; vc_msg_t = 120; }
                    else vc_confirm = true;
                    dirty = true;
                } else play = true;
            }
            if (play) { if (vc_pend) { vc_pend = 0; voice_apply(slot); voice_save(slot); } voice_play(slot); }
            if (down & KEY_B) { if (vc_pend) { vc_pend = 0; voice_apply(slot); } voice_save(slot); vc_edit = false; dirty = true; }
        }
        if (vc_msg_t > 0 && --vc_msg_t == 0) { vc_msg = NULL; dirty = true; }
        if (dirty) {
            slot = vc_sel >= VL_SLOT ? vc_sel - VL_SLOT : 0; fx = voice_fx(slot);   // (the selection may have just moved)
            subui_begin("Voice Clips", vc_sel >= VL_SLOT ? VC_NAME[slot] : NULL);
            if (!vc_edit && vc_sel < VL_SLOT) {
                static const char *const TH[2] = {
                    "Turn on to play your own clips in races. While off, the clips stay saved but never play.",
                    "Lower the music while a clip plays in a race, so the clip can be heard." };
                subui_text(20, 34, 1, 0, vc_sel == VL_ON ? "Voice Clips" : "Music Ducking", SU_ORANGE);
                subui_wrap(20, 56, 216, 2, TH[vc_sel], SU_WHITE, 13);
                if (!voice_fat()) subui_wrap(20, 128, 216, 3, "No memory card: the clips last until the DS is switched off.", SU_GREY, 10);
                sub_hints("Change", "Back", NULL);
            } else if (!vc_edit) {
                subui_text(20, 34, 1, 0, VC_NAME[slot], SU_ORANGE);
                int y = 56 + subui_wrap(20, 56, 216, 2, VH[slot], SU_WHITE, 13);
                char b[48];
                if (voice_ms(slot)) snprintf(b, sizeof(b), "%d.%d s recorded", voice_ms(slot) / 1000, voice_ms(slot) / 100 % 10);
                else snprintf(b, sizeof(b), "Nothing recorded yet");
                subui_text(20, y + 6, 2, 0, b, voice_ms(slot) ? SU_GREEN : SU_GREY);
                if (fx->bank[0] || fx->bank[1]) {
                    snprintf(b, sizeof(b), "+ %s%s%s", VC_SND_NAME[fx->bank[0] ? fx->bank[0] : fx->bank[1]],
                             fx->bank[0] && fx->bank[1] ? ", " : "", fx->bank[0] && fx->bank[1] ? VC_SND_NAME[fx->bank[1]] : "");
                    subui_text(20, y + 20, 2, 0, b, SU_BLUE);
                }
                sub_hints("Edit", "Back", voice_has(slot) ? "X Play" : NULL);
            } else {
                for (int i = 0; i < VR_N; i++) {               // (small type: fifteen rows)
                    int y = 27 + i * 9; bool on = i == vc_row;
                    subui_text(20, y, 3, 0, i == VR_CLEAR && vc_confirm ? "Clear - A again" : RN[i], on ? SU_ORANGE : SU_WHITE);
                    char b[20] = "";
                    int k = i - VR_SL, v = k >= 0 && k < 10 ? *val[k] : 0;
                    if (i == 0) snprintf(b, sizeof(b), "%s", voice_ms(slot) ? "again" : "new");
                    else if (i == 1) { if (voice_ms(slot)) snprintf(b, sizeof(b), "%d.%d s", voice_ms(slot) / 1000, voice_ms(slot) / 100 % 10); else snprintf(b, sizeof(b), "-"); }
                    else if (i == VR_SND || i == VR_SND + 1) snprintf(b, sizeof(b), "%s", VC_SND_NAME[fx->bank[i - VR_SND]]);
                    else if (k == 0 || k == 5 || k == 6) snprintf(b, sizeof(b), v ? "%+d" : "0", v);
                    else if (k == 4) snprintf(b, sizeof(b), "%d ms", 40 + v * 45);
                    else if (k == 7) { if (v) snprintf(b, sizeof(b), "%d Hz", (int)(4000.0f * powf(0.75f, v - 1))); else snprintf(b, sizeof(b), "Off"); }
                    else if (k == 8) { if (v) snprintf(b, sizeof(b), "%d Hz", (int)(80.0f * powf(1.43f, v - 1))); else snprintf(b, sizeof(b), "Off"); }
                    else if (k >= 0 && k < 10) snprintf(b, sizeof(b), "%d", v);
                    subui_text(236, y, 3, 1, b, on ? SU_ORANGE : SU_BLUE);
                    if (k >= 0 && k < 10) subui_bar(124, y + 2, 64, 5, v - lo[k], hi[k] - lo[k]);
                }
                sub_hints(vc_row == 0 ? "Record" : vc_row == VR_CLEAR ? "Clear" : "Play", "Back",
                          vc_row >= VR_SND && vc_row < VR_CLEAR ? "< >" : "X Play");
            }
            subui_show();
            dirty = false;
        }
        lines_to(SX(0x84) - 3, SX(0x55) - 4);
        hud_begin();
        frame("Voice Clips", true);
        char ib[VL_N][40]; const char *it[VL_N]; bool dim[VL_N];
        snprintf(ib[0], sizeof(ib[0]), "%s: %s", L("Voice Clips"), g_opt.voice ? L("On") : L("Off"));
        snprintf(ib[1], sizeof(ib[1]), "%s: %s", L("Music Ducking"), g_opt.vduck ? L("On") : L("Off"));
        dim[0] = dim[1] = false;
        for (int i = 0; i < VC_N; i++) { snprintf(ib[VL_SLOT + i], sizeof(ib[0]), "%s", L(VC_NAME[i])); dim[VL_SLOT + i] = !g_opt.voice; }
        for (int i = 0; i < VL_N; i++) it[i] = ib[i];
        list(SX(0x84), SX(0x55) - 6, SX(0x96), 12, it, VL_N, vc_sel, true, dim);
        int my = SX(0x55) - 6 + VL_N * 12 + 6;
        if (voice_recording()) {                       // the take's time and the microphone's level
            int t = voice_rec_ms(), full = VC_MAX * 1000 / VC_RATE, x = SX(0x84), w = SX(0x96);
            hud_font(2); hud_text(x + w / 2, my, 2, "REC", (frames / 15) & 1 ? RGB15(31, 6, 4) : RGB15(20, 4, 3)); hud_font(0);
            hud_rect(x, my + 14, w, 5, RGB15(6, 8, 12), 1); hud_rect(x, my + 14, w * t / full, 5, RGB15(31, 6, 4), 2);
            hud_rect(x, my + 22, w, 5, RGB15(6, 8, 12), 1); hud_rect(x, my + 22, w * voice_rec_level() / 100, 5, RGB15(10, 28, 10), 2);
        } else if (vc_msg) {                           // (the bottom list is full: messages up here)
            hud_font(3); hud_text(SX(0x84) + SX(0x96) / 2, my, 2, vc_msg, C_WHITE); hud_font(0);
        }
        hud_end();
        return false;
    }
    case FE_OPT: {                                     // Options: Master Volume, Sound FX Volume, Camera (0x22a24 style)
        // (Rumble: port addition, the Rumble Pak in slot 2 - "No Pak" when none is in)
        const char *const OI[7] = { XT_62, LX("Sound FX Volume", "Sound FX"), "Camera", "Frame Rate", "Rumble", XT_44, "Language" };
        static const char *const OH[7] = { XT_63, XT_64, XT_65, "Cap the race at 30 frames a second.", "Shake the Rumble Pak on crashes, landings and bumps.", XT_45, "Select Language" };
        static const char *const CAM[3] = { "Near", "Mid", "Far" };
        u8 *v[5] = { &g_opt.master, &g_opt.fx, &g_opt.cam, &g_opt.fps30, &g_opt.rumble };
        const int mx[5] = { 10, 10, 2, 1, 1 };
        if (down & KEY_UP) { sel_opt = (sel_opt + 6) % 7; dirty = true; }
        if (down & KEY_DOWN) { sel_opt = (sel_opt + 1) % 7; dirty = true; }
        if (sel_opt < 5 && (down & KEY_LEFT) && *v[sel_opt] > 0) { (*v[sel_opt])--; dirty = true; fe_apply_options(); }
        if (sel_opt < 5 && (down & KEY_RIGHT) && *v[sel_opt] < mx[sel_opt]) { (*v[sel_opt])++; dirty = true; fe_apply_options(); if (sel_opt == 4) rumble_pulse(200, 3); }
        if (sel_opt == 5 && (down & KEY_A)) { fe_enter(FE_SHADOWS); sel_sh = 0; confirm = false; return false; }
        if (sel_opt == 6 && (down & KEY_A)) { fe_save(); fe_enter(FE_LANG); return false; }
        if (down & (KEY_B | KEY_A | KEY_START)) { fe_save(); fe_enter(FE_MAIN); return false; }
        char vs[7][12];
        for (int i = 0; i < 3; i++) { if (i == 2) snprintf(vs[i], 12, "%s", CAM[g_opt.cam]); else snprintf(vs[i], 12, "%d", *v[i]); }
        snprintf(vs[3], 12, "%s", g_opt.fps30 ? "30" : "60");
        snprintf(vs[4], 12, "%s", !rumble_present() ? L("No Pak") : g_opt.rumble ? L("On") : L("Off"));
        vs[5][0] = 0; vs[6][0] = 0;
        if (dirty) {
            subui_begin("Options", NULL);
            for (int i = 0; i < 7; i++) {
                int y = 32 + i * 14;
                subui_text(20, y, 2, 0, OI[i], i == sel_opt ? SU_ORANGE : SU_WHITE);
                subui_text(236, y, 2, 1, vs[i], i == sel_opt ? SU_ORANGE : SU_BLUE);
                if (i < 2) subui_bar(150, y + 3, 60, 7, *v[i], 10);
            }
            subui_wrap(20, 136, 216, 3, OH[sel_opt], SU_GREY, 10);
            sub_hints("Accept", "Back", "< >");
            subui_show();
            dirty = false;
        }
        lines_to(SX(0x84) - 3, SX(0x55) - 4);
        hud_begin();
        frame("Options", true);
        char ib[7][40]; const char *it[7];
        for (int i = 0; i < 7; i++) { snprintf(ib[i], sizeof(ib[i]), i < 5 ? "%s: %s" : "%s", L(OI[i]), L(vs[i])); it[i] = ib[i]; }
        list(SX(0x84), SX(0x55) - 6, SX(0x96), 12, it, 7, sel_opt, true, NULL);   // (the description: bottom screen only)
        hud_end();
        return false;
    }
    }
    return false;
}

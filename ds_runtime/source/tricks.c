// SSX 3 GizWipe - air tricks, grabs, trick points / boost meter and wipeouts, ported from ssx_3.exe
//
//   air controls (0x4298c): LEFT / RIGHT spin, UP front flip, DOWN back flip (0x41c2c sets the trick up,
//   0x42128 runs it, 0x42814 / 0x42650 settle a spin on the next half turn and a flip on the next full turn);
//   grabs (0x413cc): the four grab buttons, combos pick one of 15 per-character grabs (0xa6b60), BOOST while
//   holding one = tweak / uber trick; points (0x484ac, 0x42128) fill the boost meter on landing
//   (10000 points = full, 0x48758); landing mid-flip or mid-grab wipes out (0x3f748 / 0x3fedc).
#include <string.h>
#include "phys_int.h"
#include "grabs.h"
#include "trick_str.h"
#include "anim_ids.h"

//[...][...]------------ animation (0x3bf6c / 0x3bcf4)
static u16 g_len[NANIM_IDS];

void phys_set_anim_lengths(const u16 *ms, int n)
{
    for (int i = 0; i < NANIM_IDS; i++) g_len[i] = i < n ? ms[i] : 0;
}

// 0x3bc94: the hold points (ms) of the punches, grabs, tweaks and ubers (ids 0x55..0x96, table 0xa95a0). A grab
// clip runs into the grab, then out of it again; 0x3bcf4 pins the clip at its hold point while the buttons are
// held (0x181a). On release 0x413cc plays it on forwards from there to its end (the clip's own way out of the
// grab, tweak or uber trick); only a grab let go while still swinging in runs backwards (0x1810). Other clips
// hold at their end.
static const u16 HOLD_MS[66] = {
    180, 180, 180, 180, 500, 510, 600, 540, 550, 550, 600, 600, 600, 500, 500, 500, 600, 590, 600, 600, 600, 550,
    600, 600, 600, 600, 500, 500, 500, 500, 480, 500, 500, 500, 400, 500, 500, 500, 500, 500, 400, 500, 400, 420,
    300, 400, 500, 450, 1100, 700, 1000, 710, 700, 820, 780, 1000, 1000, 730, 1050, 1000, 1000, 1200, 1400, 1550,
    1000, 1100,
};

void anim_play(Phys *p, int id, bool once, bool rev)
{
    if (id < 0 || id >= NANIM_IDS) return;
    p->anim = id;
    p->anim_len = g_len[id] ? g_len[id] : 1000;
    p->anim_once = once; p->anim_rev = rev; p->anim_done = false;
    p->anim_hold = (once && id >= 0x55 && id <= 0x96) ? HOLD_MS[id - 0x55] : 0;
    if (p->anim_hold > p->anim_len) p->anim_hold = p->anim_len;
    p->anim_ms = rev ? (p->anim_hold ? p->anim_hold : p->anim_len) : 0;
    p->anim_rate = 256;
}
// grabs play at the Tricks rate (0x413cc: 0x1814 = 0x1998 * trick_k / 256; 0x3bcf4 advances dt * rate / 100 +
// dt / 2), relative to the base rider (0x1998 = 60) whose timing the port's clips were tuned to
static void grab_rate(Phys *p)
{
    int r = p->st_grab * p->trick_k / 256;
    p->anim_rate = 256 * (50 + r) / (50 + 60);
}

static void anim_reverse(Phys *p)                                  // play the current clip back to its start
{
    p->anim_rev = true; p->anim_once = true; p->anim_done = false;
}
static void anim_finish(Phys *p)                                   // let go of the hold: on to the clip's end
{
    p->anim_rev = false; p->anim_once = true; p->anim_hold = 0; p->anim_done = false;
}
// 0x413cc state 4 sub 3 (0x3c13c): a tweak / uber trick let go - an uber's looping cycle clip goes back to the
// uber clip at its hold point; either way the clip plays on forwards to its end, then the air pose
static void trick_out(Phys *p)
{
    p->grab_sub = 3;
    if (p->uber_from) {
        anim_play(p, p->uber_from, true, false); grab_rate(p);
        p->anim_ms = p->anim_hold; p->uber_from = 0;
    }
    anim_finish(p);
}

void anim_tick(Phys *p)
{
    int dt = p->anim_rate > 0 ? DT * p->anim_rate / 256 : DT;
    if (p->anim_rev) {
        p->anim_ms -= dt;
        if (p->anim_ms <= 0) { p->anim_ms = 0; p->anim_done = true; }
        return;
    }
    p->anim_ms += dt;
    if (p->anim_hold && p->anim_ms >= p->anim_hold) {             // pinned at the hold point (0x3bcf4, 0x181a)
        p->anim_ms = p->anim_hold; p->anim_done = true;
        return;
    }
    if (p->anim_ms >= p->anim_len) {
        p->anim_done = true;
        if (p->anim_once) p->anim_ms = p->anim_len;
        else p->anim_ms -= p->anim_len;
    }
}

static const char *tstr(int id);
// a trick's name for the HUD when the landing's own name (trick_name) is empty: the exe's name for the grab / tweak /
// uber clip from the grab tables, else the clip's file name tidied up ("canadian_bacon_1_l3" -> "Canadian Bacon")
const char *phys_trick_name(int id)
{
    if (id < 0 || id >= NANIM_IDS) return "";
    for (int c = 0; c < 10; c++)
        for (int i = 0; i < 15; i++) {
            const GrabDef *g = &GRABS[c][i];
            if (g->grab == id && g->name) return tstr(g->name);
            if (g->tweak == id && g->tname) return tstr(g->tname);
            for (int u = 0; u < 3; u++) if (g->uber[u] == id && g->uname[u]) return tstr(g->uname[u]);
        }
    static char b[32];
    const char *n = ANIM_NAME[id];
    if (!strncmp(n, "g_", 2)) n += 2; else if (!strncmp(n, "tw_", 3)) n += 3; else if (!strncmp(n, "uber_", 5)) n += 5;
    int k = 0; bool up = true;
    for (; *n && k < 30; n++) {
        if (*n == '_') {
            if ((n[1] >= '0' && n[1] <= '9') || !strncmp(n + 1, "cyc", 3) || !strncmp(n + 1, "l1", 2) || !strncmp(n + 1, "l2", 2) || !strncmp(n + 1, "l3", 2)) break;
            b[k++] = ' '; up = true;
        } else { b[k++] = up && *n >= 'a' && *n <= 'z' ? *n - 32 : *n; up = false; }
    }
    b[k] = 0;
    return b;
}

//[...][...]------------ trick record
void tricks_reset(Phys *p)                                         // 0x413ac
{
    p->grab_idx = -1; p->tweak = 0; p->uber_from = 0;
}

static void trick_setup(Phys *p, int spin, int flip)               // 0x41c2c
{
    if (!p->nm_halves && !p->nm_flips && !p->nm_ngrab) p->nm_switch = p->stance != p->natural;
    p->flip_type = flip; p->spin_type = spin;
    p->spin_acc = 0; p->flip_acc = 0;
    p->t_into = p->t_cyc = p->t_out = 0;
    p->flip_start = 0; p->spin_start = p->heading;
    p->spin_btn = 0; p->flip_btn = 0; p->spin_dir = 0; p->flip_dir = 0;
    p->spin_out = false; p->flip_out = false;
    bool sw = p->stance != 0;
    if (spin == 7 || spin == 8) {
        bool fs = (spin == 7) != sw;                               // frontside / backside spin
        p->t_into = fs ? 0x4b : 0x4a; p->t_cyc = fs ? 0x2e : 0x2f; p->t_out = fs ? 0x54 : 0x53;
        p->spin_btn = spin == 7 ? PB_LEFT : PB_RIGHT;
        p->spin_dir = spin == 7 ? -1 : 1;
        p->spin_out = true;
        return;
    }
    int back = sx_angdiff(p->travel | 1, p->heading + p->stance + p->spin_fix);
    if (flip == 1) {
        if (spin == 0) { p->t_into = 0x44; p->t_cyc = 0x29; p->t_out = 0x4e; }
        else if (!sw) { p->t_into = 0x46; p->t_cyc = 0x2a; p->t_out = 0x4f; }
        else { p->t_into = 0x47; p->t_cyc = 0x2b; p->t_out = 0x50; }
        p->flip_btn = PB_TUCK;
        p->flip_dir = ABS(back) > 0x200 ? -1 : 1;
    } else if (flip == 2) {
        if (spin == 0) {
            if (!p->half_turn) { p->t_into = 0x45; p->t_cyc = 0x28; p->t_out = 0x4d; }
            else p->t_into = p->t_cyc = p->t_out = A_AIR1;
        } else if (!sw) { p->t_into = 0x48; p->t_cyc = 0x2c; p->t_out = 0x51; }
        else { p->t_into = 0x49; p->t_cyc = 0x2d; p->t_out = 0x52; }
        p->flip_btn = PB_BRAKE;
        p->flip_dir = ABS(back) < 0x201 ? -1 : 1;
    } else return;
    p->flip_out = true;
}

static void trick_combo(Phys *p)                                   // 0x41ef0: add a spin to a flip / a flip to a spin
{
    u32 b = p->buttons;
    int back = sx_angdiff(p->travel | 1, p->stance + p->heading + p->spin_fix);
    if (p->trick_kind == 3 || p->trick_kind == 4) {
        if ((b & PB_LEFT) && p->spin_type != 8 && p->spin_type != 7) {
            p->spin_rel = false; p->spin_type = 7; p->spin_dir = -1; p->spin_btn = PB_LEFT; p->spin_start = p->heading;
        } else if ((b & PB_RIGHT) && p->spin_type != 7 && p->spin_type != 8) {
            p->spin_rel = false; p->spin_type = 8; p->spin_dir = 1; p->spin_btn = PB_RIGHT; p->spin_start = p->heading;
        }
    } else if ((p->trick_kind == 1 || p->trick_kind == 2) && p->flip_type == 0) {
        if (b & PB_TUCK) {
            p->flip_btn = PB_TUCK; p->flip_rel = false; p->flip_start = 0; p->flip_type = 1;
            p->flip_dir = ABS(back) > 0x200 ? -1 : 1;
        } else if (b & PB_BRAKE) {
            p->flip_btn = PB_BRAKE; p->flip_rel = false; p->flip_start = 0; p->flip_type = 2;
            p->flip_dir = ABS(back) < 0x201 ? -1 : 1;
        }
    }
}

static void award(Phys *p)                                         // 0x4362c: count a completed 180 / flip
{
    p->tricks_in_air++;
    p->trick_done = true;
}

static bool spin_settle(Phys *p)                                   // 0x42814
{
    int d = sx_angdiff(p->spin_target, p->heading), ad = ABS(d);
    if (ad < 0xfb) p->spin_step = p->spin_step - p->heading + sx_ease(p->heading, p->spin_target);
    else p->spin_step += p->st_settle * ((p->trick_k + 0x300) / 4) / 256 * DT8 * p->spin_dir / 256;
    if (ad < 0x200 && p->t_out && p->anim != p->t_out && p->spin_out && p->grab_state == 0) anim_play(p, p->t_out, true, false);
    if (ad < 0x200 && p->grab_state) p->spin_out = false;
    if (ad < 10) { p->heading = p->spin_target; return true; }
    return false;
}

static bool flip_settle(Phys *p)                                   // 0x42650
{
    u32 rem = p->flip_dir < 1 ? (u32)(-(p->pitch + p->flip_start)) & 0x7ff : (u32)(p->pitch - p->flip_start) & 0x7ff;
    if (rem == 0) return true;
    if (rem > 0x4c8) {
        if (p->grab_state) p->flip_out = false;
        if (p->anim == p->t_cyc && p->flip_out) anim_play(p, p->t_out, true, false);
    }
    int st = rem < 0x781 ? p->st_settle : p->st_settle_slow;
    p->flip_step += st * ((p->trick_k + 0x300) / 4) / 256 * p->flip_dir * DT8 / 256;
    u32 a = p->pitch + p->flip_step;
    if (p->flip_dir < 1) a = -a;
    if (rem > 0x400 && (a & 0x7ff) < 0x400) {
        p->pitch = p->flip_dir > 0 ? 0x7ff : 1;
        p->flip_step = 0;
        return true;
    }
    return false;
}

static void spin_flip(Phys *p)                                     // 0x42128
{
    if (p->anim_done && p->anim == p->t_into && p->grab_state == 0) anim_play(p, p->t_cyc, false, false);
    trick_combo(p);
    s32 kb = p->trick_k + 0x300;                                   // 0xb8 + 0x300
    if (!p->spin_rel) {
        int d = sx_angdiff(p->spin_start, p->heading);
        if (!(p->buttons & p->spin_btn)) {                         // released: settle on the next half turn
            p->spin_rel = true;
            p->spin_target = d * p->spin_dir < 0 ? p->spin_start : p->spin_start + 0x400;
        } else {
            s32 st = (p->flip_dir == 0 || p->flip_rel) ? p->st_spin : p->st_spin_fast;
            st = st * (kb / 4) / 256;
            p->spin_step += st * DT8 * p->spin_dir / 256;
        }
    }
    int prog = 0;
    if (p->flip_dir) {
        prog = p->flip_dir < 1 ? (0x800 - p->pitch) - p->flip_start : p->pitch - p->flip_start;
        if (!p->flip_rel) {
            if (!(p->buttons & p->flip_btn) && prog > 0x600) p->flip_rel = true;
            else p->flip_step += p->st_spin * p->trick_k / 256 * p->flip_dir * DT8 / 256;
        }
    }
    bool sdone = false, fdone = false;
    if (p->spin_rel) sdone = spin_settle(p);
    if (p->flip_rel) fdone = flip_settle(p);
    // completion logic, literally as in the exe
    bool crash_zone = false;
    if (!sdone && p->spin_dir != 0) {
        if (p->flip_dir != 0) { if (p->flip_rel) { if (p->grab_state != 0) crash_zone = true; } else crash_zone = true; }
        else if (p->grab_state != 0) crash_zone = true;
    } else {
        if (fdone || p->flip_dir == 0) {
            p->trick_done = true; p->trick_kind = 0;
            if (p->flip_dir != 0) { if (p->flip_rel) { if (p->grab_state != 0) crash_zone = true; } else crash_zone = true; }
            else if (p->grab_state != 0) crash_zone = true;
        } else if (p->flip_rel) { if (p->grab_state != 0) crash_zone = true; }
        else crash_zone = true;
    }
    if (crash_zone && prog > 0x100 && prog < 0x600) { p->land_ok = false; p->flip_crash = prog + 0x400; }
    // count half turns / flips, trick points
    p->spin_acc += ABS(p->spin_step);
    if (p->spin_acc > 0x200) { award(p); p->spin_acc -= 0x400; p->half_turn = !p->half_turn; p->nm_halves++; p->nm_spin = p->spin_type; }
    p->flip_acc += p->flip_step;
    if (ABS(p->flip_acc) > 0x400) { award(p); p->flip_acc -= p->flip_dir * 0x800; p->nm_flips++; p->nm_flip = p->flip_type; }
    p->points += ABS(p->flip_step) * 0x708 / 2048 + ABS(p->spin_step) * 500 / 1024;
}

static void air_basic(Phys *p)                                     // 0x4298c (no tricks running yet)
{
    p->spin_rel = false; p->flip_rel = false; p->spin_target = 0;
    if (p->trick_kind == 0 && p->grab_state == 0) {
        u32 b = p->buttons;
        if ((b & PB_LEFT) && !p->held_left && !p->held_up) { trick_setup(p, 7, 0); anim_play(p, p->t_into, true, false); p->trick_kind = 1; }
        if ((b & PB_RIGHT) && !p->held_right && !p->held_up) { trick_setup(p, 8, 0); anim_play(p, p->t_into, true, false); p->trick_kind = 2; }
        if ((b & PB_TUCK) && !p->held_up) { trick_setup(p, 0, 1); anim_play(p, p->t_into, true, false); p->trick_kind = 3; }
        if ((b & PB_BRAKE) && !p->held_down) { trick_setup(p, 0, 2); anim_play(p, p->t_into, true, false); p->trick_kind = 4; }
    }
    if (p->grab_state == 0 && p->trick_kind == 0) {                // air pose by height above the ground
        int h = p->height / 256, want;
        if (h < 0x12d) {
            p->held_up = true;
            p->air_anim = A_AIR1;
            want = (p->vel[1] < -10000 && p->air_ms > 1000) ? A_FALLING : A_AIR1;
            if (p->anim == A_FALLING) want = A_FALLING;
        } else if (h < 0x259) want = p->air_anim = A_AIR2;
        else want = p->air_anim = A_AIR3;
        // 0x4298c: the long-drop pose (0x4c) is played once and holds on its last frame (0x181a), not looped
        if (p->anim != want) anim_play(p, want, want == A_FALLING, false);
    }
}

//[...][...]------------ grabs (0x413cc)
static bool combo_held(const Phys *p, int c)                       // 0x41b54
{
    const u16 *m = GRAB_COMBO[c];
    u32 b = p->buttons;
    return (b & m[0]) && (!m[1] || (b & m[1])) && (!m[2] || (b & m[2])) && (!m[3] || (b & m[3]));
}

static void grabs(Phys *p)
{
    const GrabDef *G = GRABS[p->character % 10];
    switch (p->grab_state) {
    case 0:
        for (int i = 14; i >= 0; i--)
            if (combo_held(p, G[i].combo)) { p->grab_idx = i; p->grab_state = 1; p->tweak_latch = false; break; }
        if (p->grab_state == 0) return;
        break;
    case 1:
        anim_play(p, G[p->grab_idx].grab, true, false); grab_rate(p);
        p->grab_state = 2; if (p->pressed & PB_BOOST) p->tweak_latch = true;
        if (!p->nm_ngrab && !p->nm_halves && !p->nm_flips) p->nm_switch = p->stance != p->natural;
        if (p->nm_ngrab == 0 || p->nm_grab[(p->nm_ngrab - 1) & 1] != G[p->grab_idx].name) {   // grabs in this air
            p->nm_grab[p->nm_ngrab & 1] = G[p->grab_idx].name;
            if (p->nm_ngrab < 9) p->nm_ngrab++;
        }
        break;
    case 2:
        // the port remembers a B tap made while the grab is still swinging in (the exe only reads B once the grab
        // is held), so a quick grab-then-B still tweaks / ubers instead of being lost
        if (p->pressed & PB_BOOST) p->tweak_latch = true;
        if (p->anim_done) p->grab_state = 4;
        break;
    case 3:
        if (p->anim_done) {
            p->grab_state = 0; p->grab_sub = 0; p->tweak = 0;
            anim_play(p, p->air_anim ? p->air_anim : A_AIR1, false, false);
        }
        return;
    case 4: {
        const GrabDef *g = &G[p->grab_idx];
        if (p->grab_sub == 0) {
            if ((p->buttons & PB_BOOST) || p->tweak_latch) {       // tweak, or an uber trick with a full meter
                p->grab_sub = 1; p->tweak_latch = false;
                int lv = p->boost_level;
                int id = g->tweak; p->tweak = 1;
                if (lv >= 1 && lv <= 3 && g->uber[lv - 1]) {       // 0x43704
                    id = g->uber[lv - 1]; p->tweak = 1 + lv; if (p->letters_pend < 9) p->letters_pend++;
                }
                snd(p, S_TWEAK);                           // port addition, tweaks and ubers: the exe plays nothing
                                                           // here (its Tweak.wav is only used by the front end)
                anim_play(p, id, true, false); grab_rate(p);
                u16 nm = p->tweak >= 2 && g->uname[p->tweak - 2] ? g->uname[p->tweak - 2] : g->tname;   // tweak / uber name
                if (nm && p->nm_ngrab) p->nm_grab[(p->nm_ngrab - 1) & 1] = nm;
            }
        } else if (p->grab_sub == 1) {
            if (p->anim_done) {
                p->grab_sub = 2;
                // 0x3c0d8: some ubers go on into a looping cycle clip while held (0x3c13c maps it back on release)
                static const u8 CYC[][2] = { { 0x8c, 0x9e }, { 0x8e, 0xa0 }, { 0x91, 0xa3 }, { 0x92, 0xa4 }, { 0x93, 0xa5 }, { 0x96, 0xa8 } };
                for (unsigned k = 0; k < sizeof(CYC) / 2; k++)
                    if (p->anim == CYC[k][0]) { anim_play(p, CYC[k][1], false, false); grab_rate(p); p->uber_from = CYC[k][0]; break; }
            }
        } else if (p->grab_sub == 2) {
            if (!(p->buttons & PB_BOOST)) trick_out(p);
        } else if (p->grab_sub == 3) {
            if (p->anim_done) {                                    // the trick's own way out done: the air pose
                p->grab_state = 0; p->grab_sub = 0; p->tweak = 0;
                anim_play(p, p->air_anim ? p->air_anim : A_AIR1, false, false);
                return;
            }
        }
        break;
    }
    default:
        return;
    }
    if (p->grab_idx < 0) return;
    if (!combo_held(p, G[p->grab_idx].combo)) {                    // grab buttons released: unwind
        if (p->grab_state == 2) { p->grab_state = 3; anim_reverse(p); }
        else if (p->grab_state == 4) {
            if (p->grab_sub == 0) { p->grab_state = 3; anim_finish(p); }   // the grab clip on out of the grab
            else if (p->grab_sub != 3) trick_out(p);
        }
    } else if (G[p->grab_idx].combo < 14) {                        // a bigger combo took over
        for (int i = 14; i > p->grab_idx; i--)
            if (combo_held(p, G[i].combo) && G[i].combo > G[p->grab_idx].combo) {
                if (p->grab_state == 2) { p->grab_state = 3; anim_reverse(p); }
                else if (p->grab_state == 4 && p->grab_sub == 0) { p->grab_state = 3; anim_finish(p); }
                else if (p->grab_state == 4 && p->grab_sub != 3) trick_out(p);
                break;
            }
    }
}

//[...][...]------------ air handler (0x43124)
void tricks_takeoff(Phys *p)                                       // 0x3c514 / 0x3c56c
{
    p->held_up = p->held_down = p->held_left = p->held_right = p->held_boost = p->held_jump = false;
    tricks_reset(p);
    if (!p->rail_state) snd(p, S_JUMP);
    p->jump = true; p->grab_sub = 0; p->grab_state = 0;
    p->air_anim = A_AIR1;
    p->jstate = 4; p->t_state = 0;
    p->spin_step = p->flip_step = 0; p->trick_kind = 0;
    p->land_ok = true;
    anim_play(p, A_AIR1, false, false);
}

void air_tricks(Phys *p)
{
    p->boosting = false;
    if (p->jstate == 7) { wipeout_step(p); return; }
    if (p->jstate == 3) p->jstate = 4;
    else if (p->jstate != 4) {                                     // went airborne without jumping
        u32 b = p->buttons;
        if (b & PB_TUCK) p->held_up = true;
        if (b & PB_BRAKE) p->held_down = true;
        if (b & PB_BOOST) p->held_boost = true;
        if (b & PB_JUMP) p->held_jump = true;
        p->jstate = 4; p->grab_sub = 0; p->grab_state = 0;
        tricks_reset(p);
        p->air_anim = A_AIR1;
        anim_play(p, A_AIR1, false, false);
        p->trick_kind = 0;
        p->land_ok = true;
    } else {
        // 0x1618: landing now is only safe outside a grab (or near the end of letting go of one)
        p->land_ok = !(p->grab_state == 4 || p->grab_state == 2 ||
                       (p->grab_state == 3 && p->anim_ms < p->anim_len - 400));   // (the exe's test, 0x43124)
        if (p->trick_kind == 0) air_basic(p);
        else spin_flip(p);
    }
    grabs(p);
    u32 b = p->buttons;
    if (!(b & PB_TUCK)) p->held_up = false;
    if (!(b & PB_BRAKE)) p->held_down = false;
    if (!(b & PB_LEFT)) p->held_left = false;
    if (!(b & PB_RIGHT)) p->held_right = false;
    if (!(b & PB_BOOST)) p->held_boost = false;
    if (!(b & PB_JUMP)) p->held_jump = false;
}

//[...][...]------------ points and the boost meter
void trick_points_step(Phys *p)                                    // 0x484ac
{
    if (p->grab_state == 4) {
        if (!p->grab_bonus) {                                      // first step holding: points for the grab itself
            int a = p->anim, add = 0;
            if (a == 0x59 || a == 0x5a || a == 0x60 || a == 0x66 || a == 0x69) add = 250;
            else if (a >= 0x5b && a <= 0x5e) add = 200;
            else if (a == 0x5f || (a >= 0x61 && a <= 0x65) || a == 0x67 || a == 0x68 || (a >= 0x6a && a <= 0x6e)) add = 300;
            else if (a >= 0x85 && a <= 0x8a) add = 500;
            else if (a >= 0x8b && a <= 0x90) add = 800;
            else if (a >= 0x91 && a <= 0x96) add = 1200;
            p->points += add;
            p->grab_bonus = true;
        }
        int k = p->tweak == 0 ? 1 : p->tweak == 1 ? 2 : p->tweak == 2 ? 4 : p->tweak == 3 ? 5 : 1;
        p->points += DT * k;
        p->trick_done = true;
    } else {
        if (p->grab_state == 0) p->grab_bonus = false;
        if (p->grab_state == 5) { p->points += DT * 2; p->trick_done = true; }   // grinding
    }
}

// how often this trick was among the last 10 (0x440b4: the score is divided by repeats + 1; rails don't count)

static const char *tstr(int id) { id -= TRICK_STR_FIRST; return id >= 0 && id < TRICK_STR_N ? TRICK_STR[id] : ""; }
static void cat(char *o, const char *s) { if (!*s) return; size_t n = strlen(o); if (n) o[n++] = ' '; snprintf(o + n, 48 - n, "%s", s); }
static void catn(char *o, int v) { char b[12]; snprintf(b, sizeof(b), "%d", v); cat(o, b); }
enum { S_AIR = 1155, S_BACKFLIP = 1157, S_BS = 1158, S_BSRAIL = 1159, S_DOUBLE = 1163, S_5050 = 1164,
       S_FRONTFLIP = 1166, S_FS = 1167, S_FSRAIL = 1168, S_GRABCOMBO = 1170, S_MISTY = 1178, S_QUAD = 1182,
       S_RODEO = 1185, S_SWITCH = 1195, S_TO = 1197, S_TOFAKIE = 1198, S_TRIPLE = 1201 };

// 0x45128: "Switch" + rail (0x4494c) + spins / flips (0x441d0) + grabs (0x44ad0) + "To Fakie"
static void trick_name(Phys *p, char *o)
{
    o[0] = 0;
    bool spun = p->nm_halves > 0, flipped = p->nm_flips > 0;
    if (p->nm_switch && (spun || p->nm_rail)) cat(o, tstr(S_SWITCH));
    if (p->nm_rail) cat(o, tstr(p->nm_rail == 2 ? S_BSRAIL : p->nm_rail == 3 ? S_FSRAIL : S_5050));
    static const int MULT[5] = { 0, 0, S_DOUBLE, S_TRIPLE, S_QUAD };
    int flipname = p->nm_flip == 1 ? S_FRONTFLIP : S_BACKFLIP;
    if (spun && flipped) {
        if (p->nm_flips == 1 && p->nm_halves >= 3) {                     // corked: Misty / Rodeo
            if (p->nm_halves > 3) catn(o, p->nm_halves * 180);
            cat(o, tstr(p->nm_flip == 1 ? S_MISTY : S_RODEO));
        } else {
            catn(o, p->nm_halves * 180);
            if (p->nm_flips >= 2 && p->nm_flips <= 4) cat(o, tstr(MULT[p->nm_flips]));
            else if (p->nm_flips > 4) { char b[8]; snprintf(b, sizeof(b), "x%d", p->nm_flips); cat(o, b); }
            cat(o, tstr(flipname));
        }
    } else if (spun) {
        bool fs = (p->nm_spin == 7) != (p->nm_switch != (p->natural != 0)); // as trick_setup picks the clips (by the mirror state)
        cat(o, tstr(fs ? S_FS : S_BS));
        catn(o, p->nm_halves * 180);
    } else if (flipped) {
        if (p->nm_flips >= 2 && p->nm_flips <= 4) cat(o, tstr(MULT[p->nm_flips]));
        cat(o, tstr(flipname));
    }
    if (p->nm_ngrab > 2) cat(o, tstr(S_GRABCOMBO));
    else if (p->nm_ngrab == 2) { cat(o, tstr(p->nm_grab[0])); cat(o, tstr(S_TO)); cat(o, tstr(p->nm_grab[1])); }
    else if (p->nm_ngrab == 1) cat(o, tstr(p->nm_grab[0]));
    else if (spun && (p->nm_halves & 1)) cat(o, tstr(S_TOFAKIE));
    if (!o[0]) snprintf(o, 48, "%s", tstr(S_AIR));
}

// 0x440b4 / 0x43588: repeats go by the trick's whole name ("540 Indy" and "360 Indy" are different tricks), against
// the last names landed: the first repeat is free, each one after divides the points one more time. Rail tricks
// never count. The list holds 10; once full, the newest name replaces the 10th (the exe clamps its index).
static u32 name_hash(const char *s) { u32 h = 2166136261u; for (; *s; s++) h = (h ^ (u8)*s) * 16777619u; return h | 1; }
static int repeats(Phys *p, const char *name)
{
    if (strstr(name, tstr(S_FSRAIL)) || strstr(name, tstr(S_BSRAIL))) return 0;
    u32 h = name_hash(name); int n = 0;
    for (int i = 0; i < p->nrecent; i++) if ((u32)p->recent[i] == h) n++;
    return n > 0 ? n - 1 : 0;
}
static void remember(Phys *p, const char *name)
{
    if (!name[0]) return;
    int i = p->nrecent < 9 ? p->nrecent : 9;
    p->recent[i] = (int)name_hash(name);
    if (p->nrecent < 10) p->nrecent++;
}

void tricks_land(Phys *p)                                          // 0x3f624 + 0x488b8: bank the trick
{
    if (p->trick_done && p->points > 0) trick_name(p, p->trick_text);
    p->nm_spin = p->nm_flip = p->nm_halves = p->nm_flips = p->nm_ngrab = 0; p->nm_switch = false;
    u8 rail_was = p->nm_rail; p->nm_rail = 0; (void)rail_was;
    if (p->trick_done && p->points > 0) {
        int add = p->points * 0x6400 / 10000;
        p->boost += add;
        if (p->boost >= 0x6400) p->boost = 0x6400;
        int key = p->grab_idx >= 0 && p->grab_state != 5 ? 1000 + p->grab_idx : p->grab_state == 5 ? -1 : 2000 + p->trick_kind;
        int div = repeats(p, p->trick_text) + 1;
        int pts = p->points * p->mult / div;                       // 0x4406c: points x multiplier / repeats
        int combo_part = (p->combo_lvl + 10) * pts / 20;          // 0x44188: this trick's share of the combo bonus
        // 0x3f624: chain the combo
        p->combo_landed++;
        p->combo_ms = 0x898;
        if (p->combo_landed > 1) p->combo_lvl = p->combo_lvl == 0 ? 1 : p->combo_lvl == 1 ? 3 : p->combo_lvl == 3 ? 5 : 9;
        p->combo_bank += combo_part;
        p->score += pts;
        p->last_points = pts;
        p->last_trick = p->grab_idx >= 0 && p->grab_state != 5 ? GRABS[p->character % 10][p->grab_idx].grab : -1;
        remember(p, p->trick_text); (void)key;
        // 0x45128: the uber tricks of the jump light that many SUPER UBER letters (at most 9)
        int was = p->letters;
        p->letters += p->letters_pend; if (p->letters > 9) p->letters = 9;
        // port addition: a ping for each letter lit, the crowd when the ninth makes it SUPER UBER (the exe only
        // lights the HUD); main.c shows a "SUPER UBER!" banner on that landing
        if (p->letters > was) snd(p, p->letters == 9 ? S_CROWD : S_PING);
        if (p->letters == 9 && was < 9) p->super_uber_now = true;
    }
    p->letters_pend = 0;
    p->points = 0; p->trick_done = false; p->tricks_in_air = 0; p->mult = 1;   // 0x488b8
    p->grab_state = 0; p->grab_sub = 0; p->tweak = 0; p->grab_idx = -1;
    p->trick_kind = 0; p->spin_step = p->flip_step = 0;
}

void combo_step(Phys *p)                                           // 0x4341c (+ the 0x15ac countdown of 0x40c04)
{
    if (p->combo_ms > 0 && !p->air && !p->rail_state) p->combo_ms -= DT;
    int t = p->combo_ms;
    p->combo_meter = t > 0x6a4 ? 5 : t > 0x4af ? 4 : t > 699 ? 3 : t >= 200 ? 2 : t >= 11 ? 1 : 0;
    if (t < 11 && p->combo_landed) {                               // the combo is over: pay the bonus
        if (p->combo_lvl > 2) { p->score += p->combo_bank; p->last_bonus = p->combo_bank; p->bonus_n++; p->bonus_lvl = p->combo_lvl; }
        p->combo_ms = 0; p->combo_lvl = 1; p->combo_landed = 0; p->combo_bank = 0;   // 0x4350c
    }
}

void combo_break(Phys *p) { p->combo_ms = 0; p->combo_lvl = 1; p->combo_landed = 0; p->combo_bank = 0; }

void boost_meter_step(Phys *p)                                     // 0x4099c
{
    if ((p->boost & ~0xff) > 0x63ff || p->boost_level == 3) {      // a full meter unlocks uber tricks
        p->full_ms = 0;
        if (!p->boost_full) p->boost_full = true;
        p->boost = 0x6400;
    }
    if (p->boost_full) p->boost_level = p->letters < 4 ? 1 : p->letters < 9 ? 2 : 3;   // 0x4099c: by the letters
    p->boost_ms += DT;
    if (p->boost_ms > 1000 && p->boost > 0x100) {                  // the meter drains slowly when not used for tricks
        p->boost_ms = 0;
        if (!p->trick_done && p->grab_state == 0 && p->trick_kind == 0 && p->boost_level != 3) {
            int hi = p->boost >> 8;
            p->boost -= hi < 0x21 ? 0x80 : hi < 0x42 ? 0x100 : 0x200;
        }
    }
    if (p->boost_full && !p->air) {                                // the full state wears off after 25 s on the ground
        if (p->boost_level == 3) p->full_ms = 0;
        else if ((p->full_ms += DT) > 24999) { p->full_ms = 0; p->boost_full = false; p->boost_level = 0; }
    }
}

//[...][...]------------ wipeouts (0x3f748, 0x3f928 clip sets, 0x3fedc / 0x3fbd8 sequence)
void wipeout(Phys *p)
{
    if (p->jstate == 7) return;
    int pen = (p->points / 2) * 0x6400 / 10000;
    if (pen < 0x1900) pen = 0x1900;
    if (p->boost_level != 3) {
        if (pen < p->boost) p->boost -= pen;
        else { p->boost = 0; p->boost_level = 0; p->boost_full = false; }
    }
    p->points = 0; p->trick_done = false; p->mult = 1; p->letters_pend = 0;   // 0x3f748: the jump's letters are lost
    combo_break(p);                                                // a wipeout loses the combo
    p->jstate = 7; p->land_ok = true;
    p->grab_state = 0; p->grab_sub = 0; p->trick_kind = 0; p->spin_step = p->flip_step = 0;
    p->crash_type = p->air ? (p->flip_crash ? 4 : 0) : (p->flip_crash ? 5 : 2);
    p->crash_phase = 0; p->crash_ms = 0; p->recover = 0; p->crash_timer = 0xc80;
    p->can_rise = false; p->slow = false; p->flip_crash = 0;
    p->crashes++;
    // 0x3f928: a crash on the ground tumbles in cartwheels, one in the air lands on its back (face up)
    p->crash_set = (p->crash_type == 2 || p->crash_type == 5) ? 1 : 2;
    snd(p, S_WIPEOUT1);
}

void wipeout_step(Phys *p)
{
    p->crash_ms += DT;
    p->turn = 0; p->boosting = false;
    if (p->pressed & PB_BOOST) {                                   // mash boost to get up sooner
        p->recover += 20 + (p->crash_ms * 7 % 31);
        if (p->recover > 100) p->recover = 100;
    } else if (p->recover > 0 && p->recover < 100) p->recover--;
    if (p->recover >= 100 && !p->can_rise) {                        // mashed enough: bonus boost, get up
        p->boost += 0xa00; if (p->boost > 0x6400) p->boost = 0x6400;
        snd(p, S_RECOVER);
        p->can_rise = true;
    }
    p->crash_timer -= DT;
    if (p->crash_timer < 1) { p->can_rise = true; p->crash_timer = 0; }
    // 0x3fedc / 0x3fbd8, state 0x168c: 0 the hit, 1 hit playing, 3 tumbling in the air, 4 rolling into the ground,
    // 5 on the ground (sliding / cartwheeling), 6 getting up, 7 done. Clip set (0x3f928) by the crash:
    //   cartwheel: air 0xb0 [...], air recover 0xb2, (no roll-in), ground 0xaa [...], get up 0xad
    //   face up:   air 0xaf wa_fwd_face_up,  air recover 0xb1, roll-in 0xae, ground 0xa9 wg_fwd_face_up, get up 0xac
    static const int SET[2][5] = { { 176, 178, 0, 170, 173 }, { 175, 177, 174, 169, 172 } };
    const int *c = SET[p->crash_set == 1 ? 0 : 1];
    int hit = (p->crash_type == 0 || p->crash_type == 1) ? A_WIPE_HIT_AIR : p->crash_type == 2 ? A_WIPE_HIT_GROUND : 0;
    if (p->crash_phase == 0) {                                     // the hit (none for a crashed flip: 0x16a0 = 0)
        if (hit) anim_play(p, hit, true, false); else p->anim_done = true;
        p->crash_phase = 1;
    }
    if (p->can_rise && p->crash_ms > 500 && p->crash_phase != 6 && p->crash_phase != 7) p->crash_phase = 6;
    if (!p->air) {
        if (p->crash_phase == 1 && p->anim_done) { anim_play(p, c[3], false, false); p->crash_phase = 5; }
        if (p->crash_phase == 3) {                                 // landed from the tumble: roll in, then slide
            p->crash_phase = 4;
            if (c[2]) anim_play(p, c[2], true, false); else p->anim_done = true;
        }
        if (p->crash_phase == 4 && p->anim_done) { anim_play(p, c[3], false, false); p->crash_phase = 5; }
        if (p->crash_phase == 6 && p->anim != c[4] && p->anim != c[1]) anim_play(p, c[4], true, false);
    } else {
        if (p->crash_phase == 1 && p->anim_done) { anim_play(p, c[0], false, false); p->crash_phase = 3; }
        if (p->crash_phase == 4 || p->crash_phase == 5) { anim_play(p, c[0], false, false); p->crash_phase = 3; }
        if (p->crash_phase == 6 && p->anim != c[1] && p->anim != c[4]) anim_play(p, c[1], true, false);   // recover in the air
    }
    if (p->crash_phase == 6 && p->anim_once && p->anim_done && (p->anim == c[4] || p->anim == c[1])) p->crash_phase = 7;
    if (p->crash_phase == 7) {                                     // back to riding (or the air pose)
        p->jstate = p->air ? 4 : 0; p->crash_phase = 0; p->recover = 0;
        anim_play(p, p->air ? (p->air_anim ? p->air_anim : A_AIR1) : A_RIDE, false, false);
    }
}

//[...][...]------------ riding animations (0x3c9b0 & co., simplified)
void ground_anim(Phys *p)
{
    if (p->jstate == 7 || p->jstate == 5 || p->finished) return;
    if (p->anim == A_PUNCH_HS || p->anim == A_PUNCH_TS) return;   // punch_input owns these
    if (p->anim_once && !p->anim_done && (p->anim == A_STUMBLE_HS || p->anim == A_STUMBLE_TS)) return;
    if (p->anim_once && !p->anim_done && (p->anim == A_GATE_INTO || p->anim == A_SWITCH || p->anim == A_BUMP || p->anim == A_LAND || p->anim == A_LAND_HARD)) return;
    u32 b = p->buttons; bool sw = p->stance != 0;
    int want;
    if (p->jstate == 2) want = A_CROUCH;
    // (the world is drawn X-mirrored: an on-screen right turn is the heelside clip for an unmirrored rider; the
    //  toeside / heelside pairs were the wrong way round before)
    else if (b & PB_BRAKE) want = (b & PB_LEFT) ? (sw ? A_BRAKE_TS : A_BRAKE_HS) : (sw ? A_BRAKE_HS : A_BRAKE_TS);
    else if ((b & PB_TUCK) && p->kmh >= 0x2e) {
        if (b & PB_RIGHT) want = sw ? A_FTURN_TS : A_FTURN_HS;
        else if (b & PB_LEFT) want = sw ? A_FTURN_HS : A_FTURN_TS;
        else want = A_CROUCH;
    } else if (b & PB_RIGHT) want = sw ? A_NTURN_TS : A_NTURN_HS;
    else if (b & PB_LEFT) want = sw ? A_NTURN_HS : A_NTURN_TS;
    else want = A_RIDE;
    // 0x3c9b0: a turn or a brake is a lean that plays once and holds there while the button is held (0x181a);
    // let go and it plays back out (0x1810) to the riding pose before anything else starts. Pressed again on the
    // way out, it leans back in from where it is.
    #define LEAN(a) ((a) == A_NTURN_TS || (a) == A_NTURN_HS || (a) == A_FTURN_TS || (a) == A_FTURN_HS || (a) == A_BRAKE_HS || (a) == A_BRAKE_TS)
    if (p->anim == want) {
        if (LEAN(want) && p->anim_rev) { p->anim_rev = false; p->anim_done = false; }
        return;
    }
    if (LEAN(p->anim) && p->anim_ms > 0) {
        if (!p->anim_rev) { anim_reverse(p); return; }
        if (!p->anim_done) return;
    }
    anim_play(p, want, LEAN(want), false);
    #undef LEAN
}

//[...][...]------------ punches (0x40c04, 0x52a68, 0x52b0c)
// On the ground, X punches to one side and Y to the other (0x100 -> punch_hs, 0x200 -> punch_ts, swapped when the
// rider is drawn mirrored). The clip stops at its 180 ms hold point while the button is held - from there the punch
// is out (0x23c8 / 0x23c9) - and plays back when it is let go.
void punch_input(Phys *p)
{
    bool mir = p->stance != 0;
    int want = 0;
    if (p->buttons & PB_GRAB3) want = mir ? A_PUNCH_TS : A_PUNCH_HS;
    else if (p->buttons & PB_GRAB4) want = mir ? A_PUNCH_HS : A_PUNCH_TS;
    bool in = p->anim == A_PUNCH_HS || p->anim == A_PUNCH_TS;
    if (want && (!in || p->anim != want || p->anim_rev)) {
        if (!(in && p->anim == want && p->anim_rev && p->anim_ms > 0)) { anim_play(p, want, true, false); p->punch_hit = 0; }
        else p->anim_rev = false, p->anim_done = false;                // pressed again on the way back
        in = true;
    }
    if (!in) { p->punch = 0; return; }
    p->punch = !p->anim_rev && !p->punch_hit && p->anim_ms >= 180 ? ((p->anim == A_PUNCH_HS) != mir ? 1 : 2) : 0;
    if (!want && !p->anim_rev) { if (p->anim_done) snd(p, S_SWING); anim_reverse(p); }   // 0x40c04: Swing (0x23) as a held punch comes back
    if (p->anim_rev && p->anim_done) { p->punch = 0; anim_play(p, A_RIDE, false, false); }
}

void phys_knockdown(Phys *p)
{
    if (p->jstate == 7 || p->air || p->rail_state || p->finished) return;
    p->punch = 0;
    wipeout(p);
}

void phys_stumble(Phys *p, int side)
{
    if (p->jstate == 7 || p->air || p->finished) return;
    bool mir = p->stance != 0;
    int id = (side == 2) != mir ? A_STUMBLE_HS : A_STUMBLE_TS;      // 0x31 b_hard_from_hs / 0x32 b_hard_from_ts
    if (p->anim != id) anim_play(p, id, true, false);
    p->punch = 0;
    for (int k = 0; k < 3; k += 2) p->vel[k] = p->vel[k] * 7 / 8;
}

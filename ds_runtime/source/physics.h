// SSX 3 GizWipe - rider physics, ported from the Gizmondo ssx_3.exe (see PHYSICS.md for the function map)
#pragma once
#include <nds.h>
#include "track.h"

// input bits, same meaning as the original's rider+0x16f4 button word
#define PB_RIGHT   0x0001      // turn rate +  (heading increases) / spin right / rail balance
#define PB_LEFT    0x0002      // turn rate -                       / spin left
#define PB_TUCK    0x0004      // ground: tuck            air: front flip
#define PB_BRAKE   0x0008      // ground: brake           air: back flip
#define PB_JUMP    0x0010      // hold = crouch / charge, release = jump
#define PB_GRAB1   0x0020      // the four grab buttons; combos select the grab (table 0xa6a70)
#define PB_GRAB2   0x0040
#define PB_BOOST   0x0080      // boost; while holding a grab: tweak / uber trick
#define PB_GRAB3   0x0100
#define PB_GRAB4   0x0200

#define PHYS_TICK_MS 40        // the original steps riders at a fixed 40 ms (0x68510 -> 0x72758(game, 0x28))

typedef struct {
    // motion (original offsets in comments)
    s32 pos[3];                // 0x1838  24.8 game units (1 unit = 1 inch)
    s32 vel[3];                // 0x185c  pos(24.8) += vel * dt8 / 64  ->  vel/64 = units per second
    s32 up[3];                 // 0x18f8  rider up, |up| = 0x4000
    s32 nrm[3];                // 0x18e8  ground normal, 0x4000
    s32 contact[3];            // 0x18d0  probe hit point, 24.8
    s32 mat[3][3];             // 0x1868  orientation: mat[row][0]=right, [1]=up, [2]=forward (0x4000)
    s32 height;                // 0x18dc  |pos - contact|
    int surf;                  // 0x18f4  surface type of the probe hit (face flags bits 16..19)
    int heading;               // 0x1848  11-bit
    int stance;                // 0x19b8  0 or 0x400 (switch)
    int facing;                // 0x1824  stance + heading
    int travel;                // 0x18e4  eased atan2(vx, vz); -1 when not moving
    int turn;                  // 0x1854
    int lean;                  // 0x184c  roll for drawing
    int pitch;                 // 0x176c  flip angle for drawing
    int tol;                   // 0x18e0  ground stick tolerance
    int momentum;              // 0x18c0  0x100 = neutral
    int speed;                 // 0x18b0  units per second
    int speed_avg;             // 0x18b8
    int kmh;                   // 0x18b4  speed * 0.0923 (inch/s -> km/h), capped 120
    int brake_k;               // 0x18c4
    bool air;                  // 0x1960
    bool jump;                 // 0x18c8  apply jump impulse on the next step
    int jstate;                // 0x20f4  0 ride, 2 crouch, 3 take-off, 4 air, 5 landing, 7 wipeout
    int charge;                // 0x1620  ms crouched (<= 1000)
    int t_state;               // 0x1624  ms since take-off
    int miss;                  // 0x171c  probe misses in a row
    int lost;                  // 0x16f0  steps on a lost surface
    int land_heading;          // 0x1908
    int spin_fix;              // 0x17f8  heading correction after leaving a rail
    int air_ticks;             // 0x1964
    int air_ms;                // 0x189c
    int uphill_ctr;            // 0x1958
    int sw_ctr;                // 0x80
    int wall_ms, wall_hits;    // 0x84, 0x88
    int tree_ms;               // 0x18a0
    bool boosting;
    int bad_landing;           // 0x1724
    u32 buttons, pressed;      // 0x16f4, 0x16f8

    // boost meter and trick points
    int boost;                 // 0x1734  meter, 0x6400 = full
    int boost_ms;              // 0x1740
    int boost_level;           // 0x1730  0..3 (3 = unlimited boost)
    int letters, letters_pend; // 0x1748 / 0x174c: SUPER UBER letters lit (0..9) and earned this jump
    bool boost_full;           // 0x1754
    int full_ms;               // 0x1758
    int points;                // 0x410   points of the trick in progress (10000 = a full meter)
    int score;                 // banked points
    int last_points;           // points of the last landed trick (HUD)
    int last_trick;            // anim id naming the last landed trick (HUD), -1 none
    // combos (0x3f624, 0x4341c, 0x44188, 0x48974): land tricks within 2.2 s of each other
    int combo_ms;              // 0x15ac  time left to land the next trick
    int combo_lvl;             // 0x474   1, 3, 5, 9
    int combo_landed;          // 0x478
    int combo_bank;            // 0x46c   paid out when the combo ends at level > 2
    int combo_meter;           // 0x14    0..5 for the HUD
    int last_bonus;            // last combo bonus paid (HUD)
    int bonus_n, bonus_lvl;    // port: combo bonuses paid so far, and the last one's level (HUD count-down)
    int recent[10], nrecent;   // last tricks: repeats score less (0x440b4)
    int crashes;

    // air tricks (0x41c2c / 0x42128): the trick record at rider+0x18..0x64
    int trick_kind;            // 0x1664  0 none, 1 spin left, 2 spin right, 3 front flip, 4 back flip
    int flip_type, spin_type;  // 0x18, 0x1c
    int t_into, t_cyc, t_out;  // 0x20..0x28 animation ids
    int flip_start, spin_start;// 0x2c, 0x30
    u32 spin_btn, flip_btn;    // 0x34, 0x38
    int spin_dir, flip_dir;    // 0x3c, 0x40
    bool spin_out, flip_out;   // 0x44, 0x45
    int spin_acc, flip_acc;    // 0x48, 0x4c
    bool spin_rel, flip_rel;   // 0x5c, 0x5d
    int spin_target;           // 0x60
    int spin_step, flip_step;  // 0x1764, 0x1760 (per-step rotation)
    bool half_turn;            // 0x1904
    bool held_up, held_down, held_left, held_right, held_boost, held_jump;   // 0x400..0x405
    bool land_ok;              // 0x1618
    int flip_crash;            // 0x16e0
    bool trick_done;           // 0x1608
    int tricks_in_air;         // 0x1788

    // grabs (0x413cc)
    int grab_state;            // 0x1670  0 none, 1 start, 2 into, 3 out, 4 holding, 5 on a rail
    int grab_sub;              // 0x1674
    int grab_idx;              // index into GRABS[char]
    bool grab_bonus;           // 0x14a4
    u8 tweak;                  // 0x175c..0x175f: 1 tweak, 2..4 uber level

    // rails (0x4e278, 0x4ddd0, rail branch of 0x4a60c)
    int rail_state;            // 0x17e8  0 off, 1 locking on, 2 grinding
    int rail;                  // 0x17a4  rail index
    int rail_seg;              // 0x17b4
    int rail_dir;              // 0x17ec  0 or 0x400 relative to the rail's point order
    int slide;                 // 0x17e4  board angle on the rail (0x200 / 0x600)
    int balance;               // 0x17bc
    int bal_limit;             // 0x17c4
    int rail_cd;               // 0x17f0  steps before landing on the same rail again
    int dpad;                  // 0x1808  +1 left / -1 right on the rail
    int rail_set;              // 0x17c8  animation set 2 (backside) / 3 (frontside)
    bool rail_fell;            // 0x1805  leave sideways (lost balance)
    bool rail_ends;            // 0x90    allow extending the end segments
    s32 rail_vec[3];           // 0x17a8  segment direction
    int rail_t;                // 0x8c    projection parameter (0..256)
    int rails_ridden;

    // punches between riders (0x40c04 player input, 0x73898 / 0x74090 hits)
    u8 punch_hit;              // this punch has landed (one hit per punch)
    u8 punch;                  // 0x23c8 / 0x23c9: 1 = heelside punch out, 2 = toeside punch out (past 180 ms)
    // wipeout (0x3f748 / 0x3fedc)
    int crash_type;            // 0x1690
    int crash_phase;           // 0x168c
    int crash_set;             // 0x169c  1 cartwheel clips, 2 face-up clips
    int crash_ms;              // 0x1694
    int recover;               // 0x16d4 boost-mash meter
    int crash_timer;           // 0x16dc
    bool can_rise;             // 0x16d0
    bool slow;                 // 0x16d8

    // animation (0x3bf6c plays, 0x3bcf4 advances); the renderer draws ANIM_NAME[anim] at anim_ms
    int anim, anim_ms, anim_len, air_anim;     // air_anim = 0x1688
    bool anim_once, anim_done, anim_rev;

    // stats (0x1968..) at their neutral values ([...] 50 -> modifier 0, see 0x64414)
    int st_glide;              // 0x1968  momentum added per step
    int st_tuck;               // 0x1970  momentum added per step while tucking / crouching
    int st_turn_tuck;          // 0x1974  turn acceleration while tucking
    int st_turn;               // 0x1978  turn acceleration
    int st_spin_fast;          // 0x197c  spin acceleration while also flipping
    int st_settle;             // 0x1980  spin / flip settle speed
    int st_spin;               // 0x1984  spin and flip acceleration
    int st_settle_slow;        // 0x1988  end of a flip
    int st_edge;               // 0x1990  carve: velocity -> facing blend
    int st_boost;              // 0x1994  momentum per boost step
    int st_friction;           // 0x1898  32 (64 in mode 2)
    int st_grab;               // 0x1998  grab animation rate (x trick_k / 256, percent)
    int st_tough;              // 0x199c  toughness (knock-downs between riders, 0x73ecc)
    u8 attr[7];                // the rider's attributes (profile +0x9a8): acceleration, edging, speed, spin,
                               // stability, toughness, tricks; 0 = not set (neutral 50)
    // the trick's name as 0x45128 builds it (0x4494c rails, 0x441d0 spins / flips, 0x44ad0 grabs)
    u8 nm_spin, nm_flip, nm_halves, nm_flips, nm_ngrab, nm_rail; bool nm_switch;
    u16 nm_grab[2];
    char trick_text[48];       // the last banked trick's name
    int anim_hold;             // ms at which a one-shot clip is pinned (0 = its end)
    bool tweak_latch;
    bool super_uber_now;       // port: this landing filled SUPER UBER (main.c clears it after showing it)          // port: B tapped during the grab wind-up
    int uber_from;             // the uber clip a looping cycle clip came from (0x3c13c)
    int anim_rate;             // animation playback, 256 = normal (grabs: the Tricks attribute)
    int natural;               // 0x19bc: the rider's natural stance (0x400 = goofy)
    int character;             // exe character index (0 zoe, 1 moby, 2 psymon, 3 griff, 4 viggo, 5 elise, 6 nate, 7 mac, 8 alegra, 9 kaori)

    // sound (ids = the exe's sound table at 0xcc338; 0x52ff0 one-shots, 0x53058 the rider's loop voice)
    u8 sfx_q[8]; u8 sfx_n;
    s8 loop_snd;               // current loop sound id, -1 none
    bool braking;

    // race (0x4ef68 pickups, 0x4f170 pickup timers, 0x4bc4c finish line, 0x494a0 after the finish)
    int speed_ms;              // 0xb0   speed pickup time left (momentum x 0x13b/256 per step)
    int trick_ms;              // 0xb4   trick pickup time left
    int trick_k;               // 0xb8   spin / flip rate factor: 0x100, 0x180 with the trick pickup
    int mult;                  // 0x428  trick score multiplier (x2/x3/x5/x10 pickups, in the air or grinding)
    int picks_taken;
    bool hold;                 // before the start: stand in the gate, no physics
    bool finished;             // 0x16ec
    bool race_over;            // 0x16ed  5 s after the finish
    int fin_ms;                // 0x16fc  time since the finish
    int react;                 // 0x167c / 0x1680  celebration clip (set by the game when it knows the result)

    // superpipe (event mode 2: 0x4bd84 replaces 0x4a60c, 0x50f90 replaces 0x508d8)
    bool pipe;
    int lip;                   // 0x64   steps left of lip contact
    int lipk;                  // 0x68   momentum feed at the lip (0x200..0x1000)
    s32 lip_n[3];              // 0x6c   lip line normal (into the pipe), y = 0x800
    s32 lip_y;                 // 0x78   lip height (24.8)
    int lip_head;              // 0x7c   heading when leaving the wall
    bool wall_ride;            // riding up the vertical part (bVar2 in 0x4bd84)

    // bookkeeping for the DS side
    int seg_hint;              // race-line index near the rider
    float s;                   // race-line arclen of the rider
    int respawns, wall_bounces, tree_hits;
    const Track *trk;
} Phys;

void phys_set_anim_lengths(const u16 *ms_by_id, int n);           // clip lengths by animation id (ms)
void phys_spawn(Phys *p, const Track *t, float s);              // place on the race line at arclen s (like 0x51f48)
void phys_place(Phys *p, const Track *t, int spawn);            // race start at a spawn marker, at rest (0x66268)
void phys_step(Phys *p, u32 buttons);                           // one 40 ms step
void phys_hold(Phys *p, int anim);                              // countdown: play a looping clip, no movement
int  sx_atan2(int x, int z);                                    // 0x8538c
void build_matrix(Phys *p, const s32 up[3], int yaw);            // 0x50000: orientation from up + yaw
void phys_knockdown(Phys *p);                                    // knocked down by a punch (0x52a68 -> 0x3f748)
void phys_stumble(Phys *p, int side);                            // punched but kept your feet (0x52b0c)
int  sx_angdiff(int a, int b);                                  // 0x58a50: b - a in (-0x400, 0x400]
int  sx_ease(int a, int b);                                     // 0x5872c: a -> b by 1/8
const char *phys_trick_name(int anim_id);                       // HUD name for a trick animation

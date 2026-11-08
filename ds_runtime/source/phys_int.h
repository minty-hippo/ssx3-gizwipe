// SSX 3 GizWipe - internal interface between physics.c, tricks.c and rails.c
#pragma once
#include "physics.h"
#include "sxtab.h"

#define SIN(a) ((s32)sx_sin[(a) & 0x7ff])
#define COS(a) ((s32)sx_sin[((a) + 0x200) & 0x7ff])
#define DT 40                                    // ms per step
#define DT8 10                                   // dt * 256 / 1000
#define ABS(x) ((x) < 0 ? -(x) : (x))
#define SURF_WALL(s) ((s) == 2 || (s) == 7 || (s) == 9)
#define SURF_LOST(s) ((s) == 3 || (s) == 8)
#define GRAVITY (DT8 * 0x5f8c / 256)            // per step

// animation ids used by the rider code (indices of the exe's clip table, see anim_ids.h)
enum {
    A_RIDE_FAST_TO_NORM = 0, A_SWITCH = 2, A_CROUCH = 3, A_FTURN_TS = 4, A_FTURN_HS = 5,
    A_RIDE = 8, A_NTURN_TS = 9, A_NTURN_HS = 10, A_BRAKE_HS = 13, A_BRAKE_TS = 14,
    A_IDLE_STAND = 8, A_LAND = 15, A_LAND_HARD = 16, A_GATE_INTO = 18, A_GATE = 19, A_REACT_POS = 20, A_REACT_NEG = 28, A_AIR1 = 37, A_AIR2 = 38, A_AIR3 = 39, A_BUMP = 48, A_STUMBLE_HS = 49, A_STUMBLE_TS = 50, A_PUNCH_HS = 0x57, A_PUNCH_TS = 0x58,
    A_FALLING = 76,
    A_WIPE_LIE = 169, A_WIPE_GETUP = 172, A_WIPE_AIR = 175, A_WIPE_HIT_GROUND = 179, A_WIPE_HIT_AIR = 180,
};

// sound ids (index of the exe's table at 0xcc338)
enum { S_BOOST = 5, S_BOOST_OUT = 6, S_COUNT = 7, S_TRICK_BEEP = 8, S_PICKUP = 9, S_RESPAWN = 10, S_RECOVER = 11, S_START = 12, S_FLARE = 14, S_FIREWORK = 15,
       S_TWEAK = 16, S_PING = 17, S_CROWD = 18, S_COLLIDE = 19, S_ICE_CARVE = 20, S_ICE_LAND = 21, S_JUMP = 22, S_PACKED_LAND = 23,
       S_PACKED_CARVE = 24, S_SLIDE = 25, S_POWDER_LAND = 26, S_POWDER_CARVE = 27, S_RAIL_LAND = 29,
       S_RAIL_LEAVE = 30, S_RAIL_LOOP = 31, S_WIPEOUT1 = 32, S_WIPEOUT2 = 33, S_WIND = 34, S_SWING = 35 };
static inline void snd(Phys *p, int id) { if (p->sfx_n < 8) p->sfx_q[p->sfx_n++] = id; }

// physics.c
u32  isqrt(u32 v);
u32  isqrt64(u64 v);
void vnorm(s32 v[3], s32 len);
s32  vlen_v(const s32 v[3]);
bool ray24(Phys *p, const s32 a[3], const s32 b[3], s32 hit[3], s32 n[3], int *surf);
void build_matrix(Phys *p, const s32 up[3], int yaw);
void travel_update(Phys *p);
void boost_input(Phys *p);

// tricks.c
void anim_play(Phys *p, int id, bool once, bool rev);
void anim_tick(Phys *p);
void tricks_reset(Phys *p);                      // 0x413ac + trick record
void tricks_takeoff(Phys *p);                    // 0x3c514
void air_tricks(Phys *p);                        // 0x43124 body: 0x4298c / 0x42128 / 0x413cc
void trick_points_step(Phys *p);                 // 0x484ac
void tricks_land(Phys *p);
void combo_step(Phys *p);                        // 0x4341c
void combo_break(Phys *p);                       // 0x3f624: bank the points into the meter
void wipeout(Phys *p);                           // 0x3f748
void wipeout_step(Phys *p);                      // 0x3fedc
void ground_anim(Phys *p);
void punch_input(Phys *p);                       // 0x40c04: X / Y on the ground punch (punch_hs / punch_ts)                       // animation choice while riding (0x3c9b0 & co., simplified)
void boost_meter_step(Phys *p);                  // 0x4099c

// rails.c
void rail_acquire(Phys *p);                      // 0x4e278
void rail_step(Phys *p, const s32 old_pos[3]);   // rail branch of 0x4a60c
void rail_leave(Phys *p);                        // 0x4d7a0
void rail_input(Phys *p);                        // 0x403ac / 0x40450
void rail_normal(Phys *p, s32 out[3]);           // 0x4dc64

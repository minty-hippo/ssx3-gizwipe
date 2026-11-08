// SSX 3 GizWipe - race opponents: the exe's recorded ghosts (data/gst/<event>_<n>.ght, played back like 0x541dc)
#pragma once
#include "physics.h"

typedef struct {
    u8 *frames; int n;           // 12-byte frames, one per 40 ms rider step from GO
    int event, id;               // file <event>_<id>.ght
    int result;                  // header +0x10: the recorded score (trick events)
    s32 start[3];                // recorded start position (header +0x18), game units
    s32 acc[3];                  // 0x23f8 accumulated deltas
    s32 off[3];                  // 0x23dc offset to this rider's spawn, decays x31/32 per step
    int cur;                     // next frame
    int cur256, speed;           // 0x23cc playback position (8.8) and 0x23d4 speed (256 = 1 frame per step)
    s32 push[3];                 // 0x23e8 collision push (24.8)
    int steps;
    int phase;                   // animation phase of the current clip, 0..255
    bool finished;
    int fin_steps;               // steps from GO to the end of the recording (= its finish)
    float s;                     // race-line progress
    int seg_hint;
    Phys ph;                     // render state: pos, mat, stance, anim
    // punches (0x73ecc decides, 0x550a4 throws, 0x74090 lands; 0x54094 / 0x54864 knocked down)
    int aggr;                    // 0x2424 0..100: chance of deciding to punch; +25 knocked down, +10 stumbled
    int think_ms;                // time to the next decision (0x23f4)
    int delay_ms;                // 0x241c: decided, waiting this long (0 / 1 / 2 / 3 s)
    int want_ms;                 // 0x2418: looking for a punch for 1.5 s
    int punch_id, punch_ms;      // the punch clip being thrown (0 = none) and its time
    int react_id, react_ms;      // stumble / knock-down clip and its time
    int knock_ms;                // 0x2408: knocked down (5 s), sliding instead of following the recording
    s32 kvel[3];                 // slide velocity, units per step x 256
    s32 last[3];                 // last position (units), for the slide's starting velocity
    int tough;                   // 0x199c toughness
    bool shadow;                 // a Shadow Racing shadow (0x19ac = 2): no bumps, no punches (0x73898)
    bool remote;                 // another player of a wireless session (net.c): posed from what that console sends
    u8 pid, kseen, net_flags;    // their player number, flags (net.h); the last knock-down counter of theirs already applied
} Ghost;

int  ghost_events_for(const char *track, int out[2]);           // events of a course (index of the exe's event table)
bool ghost_pick(Ghost *g, int event, int *used, int nused);
bool ghost_from_mem(Ghost *g, const u8 *file, int bytes);       // a recording in memory (the player's shadow)     // random file of the event, not one already used
void ghost_start(Ghost *g, const Track *t, int spawn);          // stand at spawn marker `spawn`
void ghost_step(Ghost *g, Track *t);                            // one 40 ms step
void ghost_free(Ghost *g);
bool ghost_bump(Ghost *g, Phys *p);                             // rider-rider collision (0x553bc)
int  ghost_scores(int event, s32 *out, int max);
// punches between the player and a ghost within 400 units (0x73898): returns 1 = the player was knocked down,
// 2 = the ghost was, 3 = a stumble, 0 = nothing
int  ghost_fight(Ghost *g, Phys *p);
// a remote player: the local player's punch landing on them (they are knocked down on their own console):
// 2 = knocked down, 3 = a stumble, 0 = no hit
int  ghost_punch_remote(Ghost *g, Phys *p);
bool ghost_bump_remote(Ghost *g, Phys *p);                      // pushed apart (only the local rider moves)
void ghost_rewind(Ghost *g, Track *t, int steps);               // replay: play back the recording backwards                // header +0x10 of every <event>_*.ght

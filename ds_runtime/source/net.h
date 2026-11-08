// SSX 3 GizWipe - local wireless multiplayer (card to card), in place of the Gizmondo's[...]s (CCOMMS,
// 0x356fc; menus 0x1b0ac Host / Join, 0x1b770 lobby). Up to 4 consoles: the host is player 0, clients are their
// association IDs. Everything is shared as state the host relays every frame (no reliable messages to lose): the
// session phase, the event, each player's name / rider / readiness / pause, and every rider's pose for drawing.
#pragma once
#include <nds.h>

#define NET_MAX 4

enum { NP_OFF, NP_LOBBY, NP_EVENT, NP_RIDER, NP_LOAD, NP_RACE, NP_RESULTS, NP_END };

typedef struct __attribute__((packed)) {      // one rider, as drawn by the others (44 bytes)
    s32 pos[3];                                // game units
    s32 s;                                     // race-line progress (units)
    s32 result;                                // trick events: score; races: finish time (ms), 0 = riding
    s16 fwd[3], up[3];                         // orientation (2.14)
    s16 pitch, lean;
    u8 anim, phase, flags, character;          // flags: 1 stance, 2 air, 4 rail, 8 finished, 16 down
    u16 kmh;
} NetRider;

typedef struct __attribute__((packed)) {      // a player's choices (16 bytes)
    char name[10];
    u8 rider;                                  // 0xff = not chosen
    u8 hover;                                  // rider select cursor (shown to the others while choosing)
    u8 ready;                                  // phase-specific: chosen / loaded / finished / wants a rematch
    u8 pause;                                  // pressed START in the event
    u8 knock_to, knock_seq;                    // the last knock-down this player landed (target, counter)
} NetCtl;

typedef struct __attribute__((packed)) {      // host -> everyone (8 + 8 + 64 + 176 = 256 bytes)
    u8 magic, phase, event, chooser;
    u8 mask;                                   // accepted players
    u8 paused_by;                              // 0xff = running
    u8 go;                                     // countdown started
    u8 seq;
    u16 mac[NET_MAX];                          // last two bytes of each player's MAC (a client finds its slot)
    NetCtl ctl[NET_MAX];
    NetRider r[NET_MAX];
} NetHost;

typedef struct __attribute__((packed)) {      // client -> host (2 + 16 + 44 = 62 bytes)
    u8 magic, pid;
    NetCtl ctl;
    NetRider r;
} NetClient;

typedef struct { char name[24]; int players; int index; } NetGame;   // a host found by a scan

bool net_init(void);                           // DSWifi in local mode (once)
void net_host(void);                           // become a host and advertise the game
void net_scan(void);                           // client: look for hosts
int  net_games(NetGame *out, int max);         // hosts found so far
bool net_join(int index);                      // connect to one
int  net_join_status(void);                    // 0 connecting, 1 connected, -1 failed / dropped
void net_leave(void);                          // back to idle
void net_update(void);                         // once a frame: send this frame's packet

bool net_active(void);
bool net_is_host(void);
int  net_me(void);                             // my player number
const char *net_my_name(void);
extern NetHost net_s;                          // the shared state (the host's copy, or the last one received)
extern NetCtl  net_ctl;                        // mine (copied into net_s.ctl[me] on the host)
extern NetRider net_rider;                     // my rider's pose
extern u8 net_pending;                         // host: players connected but not yet accepted (mask)
extern u32 net_rx_frames;                      // frames since the last packet from the host (client) / per player age
void net_accept(int pid, bool yes);            // host: answer "[...]"
bool net_connected(int pid);                   // host: this player is associated
int  net_age(int pid);                         // frames since that player was last heard

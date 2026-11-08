// SSX 3 GizWipe - Rumble Pak support (port addition). libnds' slot-2 peripheral layer finds the device (the DS
// Rumble Pak and compatible carts); strength is a duty cycle over frames (the motor is on / off). Edge-activated
// devices jolt once per off->on edge, so they are pulsed on the "on" frames instead of held.
#include "rumble.h"
#include "phys_int.h"
#include "frontend.h"

static bool ok, edge; static u8 maxs;
static int left_ms, level, hold, tick; static bool motor;

void rumble_init(void)
{
#ifdef ARM9
    rumbleInit();
    ok = isRumbleInserted();
    if (ok) { edge = rumbleIsEdgeActivated(); maxs = rumbleGetMaxRawStrength(); if (!maxs) maxs = 1; }
#endif
}
bool rumble_present(void) { return ok; }

void rumble_pulse(int ms, int strength)
{
    if (strength > level || (strength == level && ms > left_ms)) { level = strength; left_ms = ms; }
    else if (left_ms < ms / 2) left_ms = ms / 2 > left_ms ? ms / 2 : left_ms;
}
void rumble_hold(int strength) { hold = strength; }

static void set(bool on)
{
#ifdef ARM9
    if (edge) { if (on) { setRumble(maxs); setRumble(0); } motor = false; return; }
    if (on != motor) { setRumble(on ? maxs : 0); motor = on; }
#else
    (void)on;
#endif
}

void rumble_frame(int dvb, bool on)
{
    if (!ok) return;
    if (!on || !g_opt.rumble) { left_ms = 0; level = 0; hold = 0; set(false); return; }
    if (left_ms > 0) { left_ms -= dvb * 17; if (left_ms <= 0) { left_ms = 0; level = 0; } }
    int s = level > hold ? level : hold;
    hold = 0;                                      // (held only while asked for every frame)
    tick++;
    bool want = s >= 3 ? true : s == 2 ? (tick % 3) != 0 : s == 1 ? (tick % 3) == 0 : false;
    set(want);
}

void rumble_sfx(int id)
{
    switch (id) {
    case S_WIPEOUT1: case S_WIPEOUT2: rumble_pulse(450, 3); break;      // a crash
    case 13:                          rumble_pulse(350, 3); break;      // knocked down (SND_KNOCKDOWN)
    case S_COLLIDE:                   rumble_pulse(120, 3); break;      // bumped into a rider
    case 28:                          rumble_pulse(80, 2); break;       // a punch (SND_PUNCH)
    case S_PACKED_LAND: case S_POWDER_LAND: case S_ICE_LAND: rumble_pulse(110, 2); break;   // landing
    case S_RAIL_LAND:                 rumble_pulse(90, 2); break;       // onto a rail
    case S_BOOST:                     rumble_pulse(120, 1); break;      // boost
    case S_START:                     rumble_pulse(160, 2); break;      // GO
    case S_COUNT:                     rumble_pulse(50, 1); break;       // 3 2 1
    default: break;
    }
}

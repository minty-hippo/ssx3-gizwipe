// SSX 3 GizWipe - Rumble Pak (slot-2) feedback (port addition: the Gizmondo had no rumble)
#pragma once
#include <nds.h>
void rumble_init(void);                    // detect a slot-2 rumble device (DS / DS Lite; none on a DSi)
bool rumble_present(void);
void rumble_pulse(int ms, int strength);   // strength 1 light .. 3 full; a stronger / longer pulse wins
void rumble_hold(int strength);            // a continuous level while it is called each frame (0 = none)
void rumble_frame(int dvb, bool on);       // once a frame: drive the motor (on = false: stopped, e.g. paused)
void rumble_sfx(int id);                   // the pulse for one of the rider's sound events (phys_int.h S_*)

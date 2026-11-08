// SSX 3 GizWipe - sound effects (sfx.ssfx) played by the exe's sound ids
#pragma once
#include <nds.h>
bool sfx_open(const char *path);
void sfx_play(int id);               // one-shot (0x52ff0)
void sfx_loop(int id);               // the rider's loop voice (0x53058); -1 = stop; same id = keep playing
void sfx_stop_all(void);
void sfx_volume(int pct);
void sfx_play_name(const char *name);   // a bank entry by its file name (EASplash)            // 0..100 (Options)

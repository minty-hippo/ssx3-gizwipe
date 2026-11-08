#pragma once
#include <nds.h>
bool music_play(const char *path, u32 vblanks);
void music_update(u32 vblanks, bool prime);
void music_stop(void);
void music_volume(int pct);
bool music_playlist(int song, u32 vblanks);   // the exe's play list from song 0..23, then shuffled at each song's end
int  music_song(void);                          // the play list's current song (-1: none)
void music_pause(u32 vblanks);                  // hold the music still (around loads); nests
void music_resume(u32 vblanks);
const char *music_song_name(void);              // "artist - title" of the play list's current song ("" if none)
bool music_next(u32 vblanks);                   // start a random different song from the play list
void music_duck(int pct);                       // fade the music to pct % of its volume (100: normal)
void music_duck_step(void);                     // a frame of that fade

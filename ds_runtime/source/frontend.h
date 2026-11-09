// SSX 3 GizWipe - front end: title, peak select, event select, rider select (Gizmondo menu art, fonts and texts)
#pragma once
#include <nds.h>

typedef struct {
    const char *name;          // event name as in the exe (0xba900 string block)
    const char *track;         // course file
    const char *kind;          // RACE / SLOPESTYLE / SUPER PIPE (+ vs rival)
    const char *desc;          // the exe's English event description (pointer table 0x9b160)
    u8 peak, tnum;             // peak 1..3, track number on that peak (route overlay / screenshots)
    u8 ghost_event, riders;    // ghost file event (= event index) and riders in the event (0xdde00 + 0xc)
    bool timed;                // a time race (else scored: trick events)
} Event;

extern const Event EVENTS[15];

enum { FE_TITLE, FE_MAIN, FE_PEAK, FE_EVENT, FE_RIDER, FE_ATTR, FE_DONE, FE_OPT, FE_SHADOWS, FE_PROFILE, FE_MP, FE_MPJOIN, FE_MPLOBBY, FE_LANG, FE_VOICE, FE_BOARD };

void fe_enter(int screen);                 // (re)load that screen's images
int  fe_screen(void);
// one frame of the front end; returns true when a race should start (event and rider chosen)
bool fe_frame(u32 down, int *event, int *rider, int nriders, const char *const *rider_names, const int *best_ms);
bool fe_hold(void);                       // the menu drew nothing this frame: don't send it
void fe_flushed(void);                    // a menu frame was sent
void fe_leave(void);                       // free the menu images (before a course loads)

// provided by main.c for the rider select screen
void fe_show_rider(int ri);                // load that rider's meshes
void fe_draw_rider(int yaw, int cx, int cy, int h, int anim, bool restart);   // draw it (3D, before the HUD pass): centred on (cx, cy), h px tall, animation id
int  fe_char_of(int rider);                // exe character index of RIDERS[rider]
void fe_save(void);                        // write the save (career, attributes)
typedef struct { u8 master, fx, cam, fps30, lang, rumble, voice, vduck, board; } Options;   // (voice, vduck: Voice Clips on, music ducking under them)
   // (rumble: port addition, Rumble Pak on / off)   // Options screen: volumes 0..10, camera 0 Near / 1 Mid / 2 Far, 30 fps cap, language (0xff = not chosen yet)
extern Options g_opt;
bool fe_shadow(void);
int  fe_image(const char *name, int *w, int *h);   // a menu/ image as a texture (cached until the next screen)
bool fe_watch(void);                       // Shadow Manager "Watch Shadow": play the shadow as a replay                      // Shadow Racing: against your own best run
bool fe_has_shadow(int ev);                // (main.c) a shadow is saved for the event
bool fe_shadow_info(int ev, char *buf, int n);   // (main.c) "<event>  <rider>  <time / score>" if there is one
void fe_delete_shadow(int ev);             // (main.c) the Shadow Manager's delete
void fe_apply_options(void);               // (main.c) volumes
bool fe_career(void);
bool fe_mp(void);                          // a wireless[...] (net.c)
void fe_mp_message(const char *m);         // (main.c -> lobby) why a session ended
void fe_mp_rematch(void);                  // back to the session's event / lobby screen after the results                      // [...] (cash, unlocks) rather than Single Event
int wrap_hud(int x, int y, int w, const char *s, u16 color, int lh);   // word-wrapped HUD text, returns height

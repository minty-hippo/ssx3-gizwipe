// SSX 3 GizWipe - the Gizmondo's five languages (see lang.c)
#pragma once
#include <nds.h>
extern int g_lang;                     // 0 English, 1 French, 2 German, 3 Italian, 4 Spanish (the exe's 0x8d0)
bool lang_open(void);
const char *L(const char *english);    // the chosen language's version of one of the exe's messages, else as given
// the port's English where it reads better, the exe's nearest message (translated) in the other languages
static inline const char *LX(const char *english, const char *exe_msg) { return g_lang ? L(exe_msg) : english; }
// an event kind ("RACE", "SLOPESTYLE", "SUPER PIPE", "... vs rival") as the exe's translated word
const char *L_kind(const char *kind);
const char *L_kind_short(const char *kind);

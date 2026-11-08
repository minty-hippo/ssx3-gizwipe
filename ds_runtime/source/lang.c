// SSX 3 GizWipe - the five languages (tools/ds_lang.py -> nitro:/lang.bin). Text is written in English throughout
// the port; when another language is chosen, an English string that is one of the exe's messages is swapped for
// that language's version as it is drawn (hud_text / wrap_hud). Anything the exe never said stays English.
#include <stdio.h>
#include "xtext.h"
#include <stdlib.h>
#include <string.h>
#include "lang.h"

static u8 *data; static int count; static const u32 *ent; static const char *text;
int g_lang;                                      // 0 English, 1 French, 2 German, 3 Italian, 4 Spanish

bool lang_open(void)
{
    FILE *f = fopen("nitro:/lang.bin", "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    data = malloc(n);
    if (!data || fread(data, 1, n, f) != (size_t)n || memcmp(data, "SSXL", 4)) { fclose(f); free(data); data = NULL; return false; }
    fclose(f);
    count = data[4] | (data[5] << 8);
    ent = (const u32 *)(data + 8); text = (const char *)(data + 8 + count * 20);
    return true;
}

static u32 fnv(const char *s)
{
    u32 h = 0x811c9dc5;
    for (; *s; s++) { h ^= (u8)*s; h *= 0x01000193; }
    return h;
}

const char *L(const char *en)
{
    if (!g_lang || !data || !en || !en[0]) return en;
    u32 h = fnv(en);
    int lo = 0, hi = count - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2; u32 v = ent[m * 5];
        if (v == h) return text + ent[m * 5 + g_lang];
        if (v < h) lo = m + 1; else hi = m - 1;
    }
    return en;
}

const char *L_kind_short(const char *kind)        // one word for a column: Race / Slopestyle / Super Pipe / Rival
{
    if (strstr(kind, "rival")) return g_lang ? L(XT_66) : "Rival";
    return L(strstr(kind, "PIPE") ? "Super Pipe" : strstr(kind, "SLOPE") ? "Slopestyle" : "Race");
}

const char *L_kind(const char *kind)
{
    if (!g_lang) return kind;
    const char *w = strstr(kind, "PIPE") ? "Super Pipe" : strstr(kind, "SLOPE") ? "Slopestyle" : "Race";
    return strstr(kind, "rival") ? L(XT_66) : L(w);
}

// SSX 3 GizWipe - sky: the course's panorama (data/tracks/Sky/sky<n>.bmp, chosen by the event table 0xdde00 + 0x34)
// on a cylinder around the camera, drawn before the course without fog. The fog colour becomes the panorama's
// horizon colour, as is the clear colour, so distant terrain fades into the sky.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sky.h"

static int tex;

void sky_free(void) { if (tex) glDeleteTextures(1, &tex); tex = 0; }

bool sky_load(int n)
{
    sky_free();
    char path[40]; snprintf(path, sizeof(path), "nitro:/sky/sky%d.ssxi", n);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    struct { char m[4]; u16 w, h, tw, th; u8 flags, pad[3]; } __attribute__((packed)) hd;
    fread(&hd, sizeof(hd), 1, f);
    u16 *pal = malloc(512); u8 *tx = malloc(hd.tw * hd.th); u16 col[2] = { 0, 0 };
    fread(pal, 2, 256, f); fread(tx, 1, hd.tw * hd.th, f); fread(col, 2, 2, f);
    fclose(f);
    glGenTextures(1, &tex); glBindTexture(0, tex);
    glTexImage2D(0, 0, GL_RGB256, hd.tw, hd.th, 0, TEXGEN_TEXCOORD | GL_TEXTURE_WRAP_S, tx);
    glColorTableEXT(0, 0, 256, 0, 0, pal);
    free(pal); free(tx);
    glFogColor(col[0] & 31, (col[0] >> 5) & 31, (col[0] >> 10) & 31, 31);
    // clear = horizon too: below the cylinder (looking down a slope past the drawn course) the background then
    // matches the fogged distance instead of showing a band of zenith blue
    glClearColor(col[0] & 31, (col[0] >> 5) & 31, (col[0] >> 10) & 31, 31);
    return true;
}

// cylinder of 32 segments, radius 4.8 x 32 = 154 DS units (inside the 160 far plane, beyond the drawn course),
// from 6 degrees below the horizon to 58 above; the panorama goes round twice
ITCM_CODE void sky_draw(const float cam[3])
{
    if (!tex) return;
    glPushMatrix();
    glTranslatef32((int)(cam[0] * 16), (int)(cam[1] * 16), (int)(cam[2] * 16));   // game units -> f32 DS units
    glScalef32(inttof32(32), inttof32(32), inttof32(32));
    glPolyFmt(POLY_ALPHA(31) | POLY_CULL_NONE | POLY_ID(6));
    glBindTexture(0, tex);
    glColor(RGB15(31, 31, 31));
    static v16 cx[33], cz[33]; static bool init;
    if (!init) {
        for (int i = 0; i <= 32; i++) { float a = i * 6.2831853f / 32; cx[i] = floattov16(4.8f * sinf(a)); cz[i] = floattov16(4.8f * cosf(a)); }
        init = true;
    }
    const v16 top = floattov16(7.9f), bot = floattov16(-0.5f);
    glBegin(GL_QUADS);
    for (int i = 0; i < 32; i++) {
        int u0 = i * 16, u1 = (i + 1) * 16;                       // 512 texels = twice round
        glTexCoord2t16(inttot16(u0), inttot16(0));   glVertex3v16(cx[i], top, cz[i]);
        glTexCoord2t16(inttot16(u0), inttot16(127)); glVertex3v16(cx[i], bot, cz[i]);
        glTexCoord2t16(inttot16(u1), inttot16(127)); glVertex3v16(cx[i + 1], bot, cz[i + 1]);
        glTexCoord2t16(inttot16(u1), inttot16(0));   glVertex3v16(cx[i + 1], top, cz[i + 1]);
    }
    glEnd();
    glPopMatrix(1);
}

// SSX 3 GizWipe - sky: the course's panorama (data/tracks/Sky/sky<n>.bmp, chosen by the event table 0xdde00 + 0x34)
// on a cylinder around the camera, drawn before the course without fog. The fog colour becomes the panorama's
// horizon colour, as is the clear colour, so distant terrain fades into the sky.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "sky.h"

static int tex;
// port: the panorama's foot carried on down to straight below. The cylinder ended 6 degrees under the horizon: from a
// camera high over the rider, looking down (R&B ~38 %, off the gully's bumps), its bottom edge stood above the
// walls' tops, and between the edge and the ridge - where the course is not modelled - the clear colour showed:
// sharp-edged white wedges between the trees. Below the edge a skirt now fades from the panorama's bottom row,
// column by column (no seam), into the fog colour the distance fades to
static u16 foot[33];                       // the bottom rows' colour under each of the cylinder's 33 edges
static u16 fogc = 0x7fff;
void sky_set_fog(u16 c) { fogc = c; }

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
    {   int rows = hd.th < 128 ? hd.th : 128;
        for (int i = 0; i <= 32; i++) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int dx = -4; dx <= 4; dx++) for (int y = rows - 4; y < rows; y++) {
                int x = ((i * 16 + dx) % hd.tw + hd.tw) % hd.tw;
                u16 c = pal[tx[y * hd.tw + x]]; r += c & 31; g += (c >> 5) & 31; b += (c >> 10) & 31; n++;
            }
            foot[i] = RGB15(r / n, g / n, b / n);
        }
    }
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

// cylinder of 32 segments, radius 4.8 x 16 = 77 DS units (every vertex inside the 160 far plane, beyond the drawn course),
// from 6 degrees below the horizon to 58 above; the panorama goes round twice
ITCM_CODE void sky_draw(const float cam[3])
{
    if (!tex) return;
    glPushMatrix();
    glTranslatef32((int)(cam[0] * 16), (int)(cam[1] * 16), (int)(cam[2] * 16));   // game units -> f32 DS units
    // port: at half the size it was (radius 77 DS units, ~20000 game units: still 3x past the fog's end, so nothing
    // of the course is ever behind it). At 32 the cylinder's top and the skirt's foot lay up to 295 units out, past
    // the 160 far plane: looking down or up, the geometry engine cut them there and the clear colour showed beyond
    glScalef32(inttof32(16), inttof32(16), inttof32(16));
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
    // the skirt below: the foot's colours down to -2.5 (~-28 degrees), then the fog colour to straight below
    glBindTexture(0, 0);
    const v16 mid = floattov16(-2.5f), low = floattov16(-7.9f);
    glBegin(GL_QUADS);
    for (int i = 0; i < 32; i++) {
        int j = i + 1;
        glColor(foot[i]); glVertex3v16(cx[i], bot, cz[i]);
        glColor(fogc);    glVertex3v16(cx[i], mid, cz[i]);
        glColor(fogc);    glVertex3v16(cx[j], mid, cz[j]);
        glColor(foot[j]); glVertex3v16(cx[j], bot, cz[j]);
        glVertex3v16(cx[i], mid, cz[i]); glVertex3v16(cx[i], low, cz[i]);
        glVertex3v16(cx[j], low, cz[j]); glVertex3v16(cx[j], mid, cz[j]);
    }
    glEnd();
    glColor(RGB15(31, 31, 31));
    glPopMatrix(1);
}

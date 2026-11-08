// SSX 3 GizWipe - skinned rider runtime (riders.ssxr + anims.ssxa)
#pragma once
#include <nds.h>
#include <stdio.h>

typedef struct { float m[3][4]; } Mat34;        // column-vector affine: p' = R p + t   (game units)
typedef struct { s32 m[3][4]; } MatX;           // fixed point: rotation 4096 = 1.0, translation game units * 16

typedef struct {
    u32 *dl;            // glCallList data
    int nslots;
    u8 slot_bone[24];
    int tex;            // GL texture id (0 = none)
    int tex_idx;        // riders.ssxr texture index (shared upload)
    int npolys;
} Mesh;

typedef struct AClipRec { char name[24]; u16 nframes; u8 interval, z; u16 nkeys, length; u32 off; } AClip;   // 36 (anims.ssxa)

typedef struct {
    s16 *frames;        // nframes * 32 * 7
    int nframes;
    int interval;       // 30 fps frames per key
    int length;         // 30 fps frames
} Clip;

typedef struct {
    FILE *rf, *af;
    s8 parent[32];
    u32 nanims;
    Mesh body, board;
    Mesh body_hi, board_hi;    // the player's high-detail model, drawn instead when the polygon budget allows
    bool use_hi; float board_scale_hi, board_lift_hi;
    Clip clip;
    struct AClipRec *recs;     // clip directory
    Clip *cache;               // loaded clips by directory index (frames == NULL: not loaded)
    float board_scale, board_lift;
    MatX bone[32];      // posed bone world matrices (rider space), fixed point
    u8 plist[32]; int plist_n; const void *plist_key[2];   // the bones to pose, parents first (rider.c pose_list)
} Rider;

bool rider_open(Rider *r);
bool rider_load_mesh(Rider *r, Mesh *m, const char *name);
bool rider_load_clip(Rider *r, const char *name);
bool rider_read_clip(Rider *r, const char *name, Clip *out);     // load into a caller-owned clip
int  rider_clip_index(Rider *r, const char *name);               // directory index or -1
const Clip *rider_clip(Rider *r, int index);                    // cached clip (loaded on first use)
void rider_pose_clip(Rider *r, const Clip *c, float key, bool loop);
void rider_pose(Rider *r, float frame);
void rider_pose_blend(Rider *r, const Clip *a, float ka, bool la, const Clip *b, float kb, bool lb, int w);
void rider_bone_pos(const Rider *r, int bone, float out[3]);    // bone origin in rider space (game units)
// draw at world matrix W (game units); current GL matrix must be the camera (modelview)
void rider_draw(Rider *r, const Mat34 *world, int board_bone);
extern bool rider_async; void rider_gx_wait(void);              // race riders: the body list left running (rider.c)
void rider_free_mesh(Mesh *m);
bool rider_set_tex(Rider *r, Mesh *m, const char *texname);   // swap a mesh's texture (by riders.ssxr name)
void rider_draw_static_x(const Mesh *m, const MatX *W);
void rider_draw_static(const Mesh *m, const Mat34 *world);       // unskinned mesh (trees, pickups) at W

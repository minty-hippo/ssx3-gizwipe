// SSX 3 GizWipe - DS track runtime (.ssxt v2 loader, segment streaming, LOD rendering)
#pragma once
#include <nds.h>
#include <stdio.h>

typedef struct {
    char magic[4];
    u32 ver, nseg, nmat, ntex, nlod;
    u32 off_raceline, off_markers, off_textures, off_materials, off_segments, nraceline, nmarkers;
    s32 bbmin[3], bbmax[3];
    u32 off_rails, nrails;                     // v4
} SsxtHeader;                                   // 84 bytes

typedef struct { s32 x, y, z, arclen; } RacePoint;
typedef struct { char name[24]; s32 pos[3]; s16 quat[4]; } Marker;
typedef struct { u16 w, h; u8 fmt, flags; u16 pad; u32 off_texels, off_pal; } TexRec;
typedef struct { s16 tex; u16 flags; } MatRec;
typedef struct { u16 npolys, nverts; u32 off_lists; u16 nlists, pad; } LodRec;
typedef struct {
    s32 origin[3];
    s16 bbmin[3], bbmax[3];
    s32 s0, s1;
    u32 off_collision, chunk_off, chunk_size;
    LodRec lod[4];
} SegRec;                                       // 92 bytes
typedef struct { u16 material, npolys; u32 off_dl; s16 bbmin[3], bbmax[3]; } ListRec;   // v5: one material in one 1024 x 1024 tile

#define MAX_SEGS 64
#define TRI_U16 7                                // collision triangle record, in u16s (v3)

// per-segment collision acceleration, built when a segment streams in
typedef struct {
    const s16 *V; const u16 *T;            // vertices (local), tris {a,b,c, mat|surf<<8, s16 normal[3]}
    u16 ntri, gx, gz, cell; u8 sh;          // grid size, cell size (units, a power of two: 1 << sh)
    s16 x0, z0;                             // grid origin (local)
    u32 *start; u16 *items;                 // cell -> [start[c], start[c+1]) into items
} ColGrid;
typedef struct { s32 x, y, z; } TreePos;
typedef struct { s32 pos[3]; u8 type, taken; u16 pad; } Pickup;   // type: 0 speed, 1 trick, 2 x2, 3 x3, 4 x5, 5 x10 (0x6268c)
typedef struct {                                 // grind rail, as built by the exe's loader (0x82854)
    int n;
    s32 (*pts)[3];                               // game units
    s32 (*dir)[3];                               // dir[i] = unit(pts[i+1] - pts[i]) * 0x4000
    s32 bbmin[3], bbmax[3];
    s32 r2;                                      // |bbmax - bbmin|^2
} TrackRail;

typedef struct {
    FILE *f;
    SsxtHeader h;
    RacePoint *race;
    Marker *markers;
    MatRec *mats;
    SegRec *segs;
    int *texnames;             // GL texture ids per texture
    u8 *chunk[MAX_SEGS];       // streamed segment data (NULL = not resident)
    int missing;               // segments due on screen this frame but not resident (test builds show it)
    u8 lod_cur[MAX_SEGS];      // the LOD each segment was last drawn at (+1; 0 = not drawn): hysteresis
    ColGrid *col[MAX_SEGS];
    u8 col_queued[MAX_SEGS];                // read in the background, collision grid still to build (col_tick)
    TreePos *trees; int ntrees;
    Pickup *picks; int npicks;                   // c_* markers (not c_point*)
    s32 spawn[4][3]; int nspawn;                 // start positions (game units)
    s32 fin_l[3], fin_r[3]; bool has_finish;     // finish line (0x4bc4c)
    s32 pipe[4][2][3]; int npipe;
    struct Glare { s32 pos[3]; u16 color; } *glares; int nglares;
    s32 fogs[16][3]; int nfogs; int pal_fail, tex_fail; u32 tex_sum[32], pal_sum[32]; int tex_bad, pal_bad;                  // 'fog' markers: volume fog patches (0x74e0c)   // glare_<colour>* light flares                // halfpipe lip lines pipe%i_l -> pipe%i_r (0x4a3cc)
    TrackRail *rails; int nrails;
    int rays, tri_tests;       // per-frame collision stats
    int resident, loads_this_frame, sync_loads;
    int pend_seg; u8 *pend_buf; u32 pend_done;      // segment being read a slice per frame (-1 none)
    u32 resident_bytes;
    float length;              // race line length (game units)
    s32 view[3], view_dir[2]; bool has_view;
    s32 cam[2], cam_f[2]; bool cam_ok, defer_far; s32 reach; s32 cam_y, cam_y_next, cam_tanp;   // cam_y_next: set before track_set_cam   // reach: beyond it (the fog's end) nothing is sent         // the camera (x, z) and its forward (x 1024): segments outside the view are not sent   // rider position: segments near it are drawn even when the race line says they are far
} Track;

bool track_load(Track *t, const char *path);
void track_free(Track *t);
void track_sample(const Track *t, float s, float out[3]);       // race-line point at arclen s
void track_update_stream(Track *t, float s, int max_loads);
void track_set_view(Track *t, const s32 pos[3], int heading);    // game units, 11-bit heading of travel
int  track_lod_for(const Track *t, int seg, float s, int bias);
void track_set_cam(Track *t, float x, float z, float fx, float fz, int reach);
int  track_draw_far(Track *t);       // the far tiles track_draw deferred: last thing of the frame (see track.c)   // before track_draw
int  track_draw(Track *t, float s, int bias);                    // returns polys submitted
// segment a->b (game units) against the collision mesh, one-sided like the original (0x68180/0x67164):
// nearest hit to a among faces whose normal faces the ray. n = face normal (0x4000), surf = surface type.
bool track_ray(Track *t, const s32 a[3], const s32 b[3], s32 hit[3], s32 n[3], int *surf);
bool track_ground(const Track *t, float x, float z, float y_max, float *y_out, float n_out[3]);
void track_tex_check(Track *t);              // test builds: compare the textures in VRAM with what was uploaded
extern u32 course_fmt, course_fmt_cull;      // the course's polygon format, and culled (lists flagged 0x4000)

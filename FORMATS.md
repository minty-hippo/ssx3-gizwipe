# SSX 3 (Gizmondo) – asset formats

Reverse-engineered from the Gizmondo release (`ssx_3.exe`, WinCE ARM, OpenGL ES 1.x via `libGLES_CM.dll`).
All integers little-endian. Status: ✅ decoded & verified visually · 🟡 partly understood · ❌ not yet.

## Containers
`*.pak` are plain ZIP archives (stored or deflate). `snd.pak` holds 61 RIFF WAVs, MS-ADPCM mono 22 050 Hz (72 min total).
Loader paths in the exe: `[...]`, `m[...]`, `[...]`, `models/gst.pak`,
`[...]`, `[...]`, `wav/snd.pak`.

## `.xtf` – track texture ✅
```
0x00 'XTF1'
0x04 u8[4]  01 05 04 01           (version)
0x08 u32 width, u32 height, u32 0, u32 mip_count
0x18 u8 indices[w*h] followed by the mip chain (w/2*h/2 ...), mip_count levels total
end-1024  RGBA8888 palette[256]
```

## `.xgt` – character / board texture ✅ (headerless; type implied by file size)
* `side² + 1024` → 8-bit indices + RGBA8888 palette (the `_low` skins, 64/128/256 px)
* `side² + 16384` → 8-bit indices + **32 shade palettes** × 256 × RGB444 (`0x0RGB`).
  Palettes 0-15 and 16-31 are two brightness ramps; palette 16 is the neutral/unlit colour.
  (Baked lighting by palette swap – on DS, use palette 16 and hardware lighting.)

## `.xng` – mesh ✅ (static) / ✅ (skinned)
```
0x00 'XNG1'
0x04 u8[4]  03 05 01 01
0x08 i32 nverts, i32 0, i32 nfaces, i32 0, i32 flags   (flags & 32 = skinned rider)
0x1C i32 xyz[nverts]                                    (integer units; riders: bone-local)
     face[nfaces], 60 bytes each:
       i16 idx[4]          triangle if idx[3] == -1 or idx[3] == idx[0]
       i32 uv[4][2]        16.16 fixed
       i32 color           -1 = textured, else RGB444 flat colour (high-detail riders: ignore)
       i32 ?, u32 flags (0x10/0x11), i32 0, i32 0
tail (skinned only):
     u8  bone_of_vertex[nverts]           (vertices are sorted by bone)
     'RPBN' u32 nbones(32)
     nbones × 16 bytes: i32 index, i32 0, i32 vertex_count, u8 next_sibling, u8 first_child, u16 0
                        (0xFF = none; left-child/right-sibling tree, root = bone 8)
tail (all): u8 1, 'texture.xgt\0' (+ uninitialised exporter garbage), then an i16 list (normals/strips? 🟡)
```
Rigid skinning: every vertex belongs to exactly one bone → maps 1:1 onto the DS matrix stack.

Skeleton (from the anim bone lengths): 8 root → 28 → {5 → 4 (board?), 25 pelvis} ;
25 → {21 hips, 22 spine}; 21 → 29/30 thighs → 26/27 shins → 11/12 → 9/10 feet;
22 → 23 → 31 chest → {6, 7 clavicles, 24 neck}; 6/7 → 0/1 → 2/3 → 13/14 → 17/18 → 15/16 (arms/hands);
24 → 19 head → 20.

## `.xga` – skeletal animation ✅
```
0x00 'XNGA'  0x04 u8[4] 01 05 02 01
0x08 u8 ?, u16 ?, 'RPBR', u16 ?, u32 ?, u8 nbones(32)
0x16 frames × nbones × 28 bytes:  i32 quat[4] (w,x,y,z in 2.14 fixed, stored conjugated), i32 translation[3]
```
frames = (filesize − 22) / (nbones × 28). Local transform = R(conj(q)) + T, composed parent→child.

## `slope.oct` – track geometry ✅ (full structural parse, byte-exact to end of file on all 12 tracks)
```
0x00 'OCT1'
0x04 u32 version 0x01010505
0x08 u32 nverts
0x0C i32 xyz[nverts]            Y-up integer units (~1 unit = 1 cm); loader shifts to 16.16
     u32 nfaces, u32 ?, u32 nnodes
     quadtree, depth-first (loader 0x822C4):
        node: 72 bytes  u32 nfaces | u32 ?, ? | i32 bbmin[3] | i32 bbmax[3] | i32 centre[3] | u32 ? |
                        u32 child_ptr[4] (raw PC pointers: non-zero = that child follows)
        nfaces x 60-byte face records (same layout as .xng):
           i16 idx[4] | i32 uv[4][2] 16.16 | i32 color (-1) | i32 material | u32 flags | i32 0 | i16 ?, i16 ?
           flags: 0x10 = triangle (4th index unused), hi16 = shade level
        then the children
     u32 nmarkers, nmarkers x 92 bytes: char name[64] | i32 pos[3] | i32 quat[4] (2.14)
         names: spawn, finish_left/right, camera, tree, glare_*, c_x2/x3/x5/x10, c_speed, c_trick,
                mdl_<trk>_collecta_*, fw*, fog, pipe*_l/_r, particle view 01, ...
     u32 nsplines, nsplines x { char name[32] | u32 n | i32 xyz[n] }
         'default' = race line (20-171 pts), r_railspline* = grind rails
     u8 nmaterials, nmaterials x 64 bytes: 'name.xtf\0' + render state   (face.material indexes this)
     u8 rgba[nverts]   per-vertex baked lighting colour
     EOF
```

## Others
* `tracks/Sky/*.bmp`, `bitmaps/*.bmp`, `fonts/*.bmp` – standard 8-bit Windows BMP ✅
* `fonts/*.fnt` – glyph metrics ❌ (small, trivial)
* `gst/*.ght` – ghost / replay data ❌ (not needed for a port)

## Exe landmarks (VA, image base 0x10000)
| what | address |
|---|---|
| OCT loader | `0x82CC4` |
| XNGA loader | `~0x80800` |
| XNG / RPBN loader | `~0x80190` |
| XTF loader | `~0x7F0D4` |
| file read helper | `0x1D730` |

## DS budget notes
* Tracks: 13-21 k faces each vs. DS limit 2048 polys / 6144 verts per frame → segmented + 4 LODs (see DS_FORMATS.md).
* Track textures: 7-13 × 256² 8-bit (~0.5-0.9 MB) vs. ~512 KB texture VRAM → downscale to 128² and/or 4-bit.
* Riders: low LOD is 98-117 faces with single-bone vertices – fits easily.
* Audio: 72 min → IMA-ADPCM 16 kHz ≈ 35 MB, streamed from NitroFS.

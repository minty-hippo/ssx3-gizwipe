# SSX 3 → DS: converted data formats (stage 2)

Produced by `tools/convert_all.py`. All files little-endian, 4-byte aligned, offsets from file start.
Everything is laid out so the DS code can `fread` it and hand pieces straight to the hardware.

## Coordinate convention
* Game units ≈ 1 cm, Y-up. DS world: **1.0 (4096 in f32) = 256 game units**.
* Track and rider `VTX_16` values are **game units ×1 relative to the segment origin** (tracks) or
  **game units ×16 in bone space** (riders). Render a track segment with
  `glTranslatef32(origin*16)` + `glScalef32(16 << 12)`-equivalent (scale 16), riders with scale 1.
* Texture coordinates are 12.4 texels for the converted texture size (repeat on).

## `tracks/<NAME>.ssxt` (version 4)
```
header (84 B)  'SSXT' u32 ver, nseg, nmat, ntex, nlod
               u32 off_raceline, off_markers, off_textures, off_materials, off_segments, nraceline, nmarkers
               ... i32 bbmin[3], bbmax[3]; u32 off_rails, nrails                                    (v4)
rails          nrails × { u32 npoints, first_point }, then all points i32 xyz (game units); every OCT spline
               except 'default' (r_railspline*, r_spline*) - grind rails (0x4e278)
               i32 bbmin[3], bbmax[3]
raceline       nraceline × { i32 x, y, z, arclen }
markers        nmarkers × { char name[24]; i32 pos[3]; i16 quat[4] }     (all markers; v3 keeps the tree* markers for rider/tree collision)
textures       ntex × { u16 w, h; u8 fmt=4 (GL_RGB256); u8 flags (1 = colour 0 transparent); u16; u32 off_texels, off_pal }
materials      nmat × { i16 texture (-1 none); u16 flags }
segments       nseg × 92 B:
                 i32 origin[3]; i16 bbmin[3], bbmax[3] (local); i32 s0, s1 (race-line arclen range);
                 u32 off_collision; u32 chunk_off, chunk_size;
                 4 × { u16 npolys, nverts; u32 off_lists; u16 nlists; u16 pad }      (LOD0..3)
per segment chunk (stream unit; every offset of the segment lies inside it):
  lists       nlists × { u16 material (0xFFFF = untextured LOD3); u16 npolys; u32 off_dl }
  dl          glCallList-ready: u32 nwords + packed GX (BEGIN_VTXS, COLOR, TEXCOORD, VTX_16, END_VTXS)
  collision   u16 ntri, nvert; i16 xyz[nvert] (local); { u16 a, b, c; u8 material; u8 surface; i16 normal[3] }[ntri]
              surface = OCT face flags bits 16..19 (0 = no collision, 2/7/9 walls, 3/8 out of bounds, 4/5 grip variants)
              normal = unit cross(b-a, c-a) × 0x4000 (faces are hit only from the front, as in the original)
```
LODs: 0 = original (quads kept), 1 ≈ 55 %, 2 ≈ 30 % (textured, re-quadded), 3 ≈ 10 % untextured with
texture colour baked into vertex colour. Segments are variable length (≤ 650 faces, 10–40 m).
Recommended LOD by race-line distance from the rider to the segment start (`tools/budget.py`):
in-segment → 0, < 35 m → 1, < 90 m → 2, < 240 m → 3, behind (< 25 m) → 3, else skip. Fog from ~160 m.

## `riders.ssxr`
```
'SSXR' u32 ver, nmesh, ntex, off_meshes, off_textures
mesh  × { char name[16]; i16 tex; u8 skinned; u8 nslots; u8 slot_bone[24]; u16 npolys; u16; u32 off_dl }
tex   × { char name[24]; u16 w, h; u8 fmt=4; u8 flags; u16; u32 off_texels, off_pal }
```
Skinned display lists contain `MTX_RESTORE(slot)` before any vertex whose bone differs.
Runtime per rider: for each slot `s` (1..nslots): build `view * rider * boneWorld[slot_bone[s-1]]`,
`glLoadMatrix4x3` + `MTX_STORE(s)`; then `glCallList(dl)`. Normals are bone-local (hardware lighting works).
Boards (`def_board`, `hi_res_board`) are static meshes; attach to bone 4 (unconfirmed – bone 4 is the only leaf under the root-side bone 5).
Since stage 6 the file also holds the static meshes of `data/sprites` (`tree`, `tree_low`, `speed_boost`, `trick_boost`,
`x2`, `x3`, `x5`, `x10`, `angry`, `ice_crystal`): Y-up, game units (VTX16 = units × 16). The tree quads use
UVs 0..1/8 in the source; the converter scales them ×8 to cover the texture. Texture `flags` bit 0: the source had a
magenta colour key, which is palette index 0 – upload with `GL_TEXTURE_COLOR0_TRANSPARENT`.

## `anims.ssxa`
```
'SSXA' u32 ver, nbones (32), nanims; i8 parent[32]
nanims × { char name[24]; u16 nframes; u8 hdr[6]; u32 off }
frames: nframes × 32 × { i16 qw, qx, qy, qz (2.14, ready to use); i16 tx, ty, tz }
```
Bone world = parent world × (R(q), t). Same skeleton for every rider (root = bone 8).

## `sfx.ssfx`
```
'SSFX' u32 ver, count; count × { char name[20]; u16; u16 rate; u32 off; u32 bytes; u16 loop; u8 loop; u8 }
```
Each entry is one DS hardware IMA-ADPCM sample (`SoundFormat_ADPCM`), 16 384 Hz mono.
Looping flags are guessed from names (Carve/Loop/Wind/Crowd) – confirm against the exe.

## `music/Music_NN.ssmu`
```
'SSMU' u32 ver, rate (16384), block_bytes (8192), samples_per_block (16377), nblocks, total_samples
nblocks × block_bytes: independent IMA-ADPCM blocks (4-byte header each)
```
Stream from NitroFS: decode block → PCM16 ring buffer (ARM9 or ARM7), or play blocks back-to-back.

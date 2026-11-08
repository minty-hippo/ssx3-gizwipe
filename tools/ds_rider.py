"""Convert SSX 3 (Gizmondo) riders, boards and animations to DS-ready files.

usage: python3 ds_rider.py <characters_dir> <out_dir> [--tex 128]

riders.ssxr
  'SSXR' u32 version(1) u32 nmesh u32 ntex
  u32 off_meshes u32 off_textures
  meshes  nmesh x { char name[16]; i16 tex; u8 skinned; u8 nslots; u8 slot_bone[24]; u16 npolys; u16 pad; u32 off_dl }
          dl: glCallList-ready; skinned meshes issue MTX_RESTORE(slot) before each vertex whose bone differs.
              Runtime: for each slot s: load bone world matrix (rider space) * view, MTX_STORE(s); then call dl.
              Vertex units: VTX_16 = game units * 16  (so 1 DS unit = 256 game units, same as tracks).
  textures ntex x { char name[24]; u16 w, h; u8 fmt (4 = GL_RGB256); u8 flags; u16 pad; u32 off_texels, off_pal }

anims.ssxa
  'SSXA' u32 version(1) u32 nbones(32) u32 nanims
  i8 parent[32]                               (-1 = root)
  nanims x { char name[24]; u16 nframes; u8 key_interval, 0; u16 nkeys_hdr, length_frames (30 fps); u32 off }
  frame data: nframes x nbones x { i16 q[4] (w,x,y,z 2.14, already conjugated: use directly), i16 t[3] }
"""
import os, sys, glob, struct, argparse, re
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssxmesh import read_xng
from ssxskel import read_rpbn, read_xga, world, skin
from ssxtex import read_xgt
from ds_track import pack_gx, rgb15, Blob

GX_MTX_RESTORE, GX_NORMAL = 0x14, 0x21
import ds_track
ds_track.NPARAM[GX_MTX_RESTORE] = 1; ds_track.NPARAM[GX_NORMAL] = 1
GX_BEGIN, GX_END, GX_COLOR, GX_TEXCOORD, GX_VTX16 = 0x40, 0x41, 0x20, 0x22, 0x23

def n10(v):
    v = np.clip(np.round(np.asarray(v) * 511), -512, 511).astype(int)
    return (v[0] & 0x3FF) | ((v[1] & 0x3FF) << 10) | ((v[2] & 0x3FF) << 20)

def texture_to_ds(rgba, size):
    """256-colour paletted texture. A magenta colour key (the sprite textures: trees) becomes palette index 0,
    flags bit 0 = draw with colour 0 transparent."""
    img = Image.fromarray(rgba, 'RGBA')
    a = np.asarray(img.convert('RGB')).astype(int)
    keyed = ((a[..., 0] > 200) & (a[..., 1] < 60) & (a[..., 2] > 200)).sum() > 16
    if img.width > size: img = img.resize((size, size), Image.NEAREST if keyed else Image.BOX)
    a = np.asarray(img.convert('RGB')).astype(int)
    key = (a[..., 0] > 200) & (a[..., 1] < 60) & (a[..., 2] > 200)
    if keyed:
        rgb = a.copy(); rgb[key] = a[~key].mean(0)               # keep the key out of the palette
        q = Image.fromarray(rgb.astype(np.uint8), 'RGB').quantize(255, method=Image.MEDIANCUT, dither=Image.NONE)
        idx = np.asarray(q, np.uint8).astype(int) + 1; idx[key] = 0
        p = q.getpalette()[:765]; p = [255, 0, 255] + p + [0] * (765 - len(p))
        pal = np.array(p).reshape(256, 3)
        return dict(w=q.width, h=q.height, texels=idx.astype(np.uint8).tobytes(),
                    pal=np.array([rgb15(c) for c in pal], np.uint16).tobytes(), flags=1)
    q = img.convert('RGB').quantize(256, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = np.array(q.getpalette()[:768] + [0] * (768 - len(q.getpalette()[:768]))).reshape(256, 3)
    return dict(w=q.width, h=q.height, texels=np.asarray(q, np.uint8).tobytes(),
                pal=np.array([rgb15(c) for c in pal], np.uint16).tobytes(), flags=0)

def mesh_dl(m, skel, tex_wh, rest_W):
    V = m['verts']
    if skel is not None:
        bone = skel['bone_of_vert']; used = sorted(set(bone.tolist()))
        slot = {b: i + 1 for i, b in enumerate(used)}             # slot 0 left for the caller
        # smooth normals from a posed mesh, then back into each vertex's bone frame
        P = skin(m, skel, rest_W)
    else:
        bone = np.zeros(len(V), int); used = []; slot = {}; P = V
    N = np.zeros_like(P, dtype=float)
    for f in m['faces']:
        p = P[f['idx']]; n = np.cross(p[1] - p[0], p[2] - p[0])
        for a in f['idx']: N[a] += n
    N /= np.maximum(np.linalg.norm(N, axis=1), 1e-9)[:, None]
    if skel is not None:
        N = np.array([rest_W[bone[i]][:3, :3].T @ N[i] for i in range(len(V))])
    cmds = []; cur = None; npoly = 0
    # the tree sprites map their quads to 0..1/8 of the texture: the whole image is meant (the rest is unused)
    uvmax = max((max(u, v) for f in m['faces'] for u, v in f['uv']), default=1)
    uvk = 8 if 0 < uvmax <= 0.126 else 1
    # port: emitted as strips where neighbouring faces share corners (same vertex, texture coordinate and colour -
    # so the same normal and bone), faces grouped by bone so the matrix restores are few. Every polygon is still
    # drawn as before; the geometry engine just transforms and lights each shared corner once instead of up to six
    # times (it works per vertex, and vertex RAM fills by vertex: the 570-polygon high-detail riders were ~1700 a
    # frame each).
    def corner(f, k):
        vi = f['idx'][k]
        col = f['color'] if (f['color'] != -1 and not (m['flags'] & 32)) else None
        st = None
        if tex_wh:
            if uvk == 8 and f['color'] != -1 and skel is not None:   # (riders only: the trees' colour words are colours)
                c = f['color'] & 0xffff; raw = (f['uv'][k][0] * 65536, f['uv'][k][1] * 65536)
                uu = ((c & 0xff) + raw[0] / 256) / 256; vv = ((c >> 8) + raw[1] / 256) / 256
                st = (int(round(uu * tex_wh[0] * 16)), int(round(vv * tex_wh[1] * 16)))
            else:
                st = (int(round(f['uv'][k][0] * uvk * tex_wh[0] * 16)), int(round(f['uv'][k][1] * uvk * tex_wh[1] * 16)))
        return (int(vi), st, col)
    def emit(out, st8, c):
        vi, stc, col = c
        if skel is not None and bone[vi] != st8[0]:
            st8[0] = bone[vi]; out.append((GX_MTX_RESTORE, [slot[st8[0]]]))
        if col is not None:
            out.append((GX_COLOR, [rgb15((((col >> 8) & 15) * 17, ((col >> 4) & 15) * 17, (col & 15) * 17))]))
        out.append((GX_NORMAL, [n10(N[vi])]))
        if stc is not None:
            s_, t_ = stc; out.append((GX_TEXCOORD, [((t_ & 0xFFFF) << 16) | (s_ & 0xFFFF)]))
        x, y, z = (int(round(a * 16)) for a in V[vi])
        out.append((GX_VTX16, [((y & 0xFFFF) << 16) | (x & 0xFFFF), z & 0xFFFF]))
    def bset(seq): return tuple(sorted(set(int(bone[c[0]]) for c in seq))) if skel is not None else ()
    def build(split):
        # split: strips only within faces of the same bone set (no matrix restores inside a strip) - against
        # longer strips across bones; whichever costs the geometry engine less (a restore ~36 cycles, a lit,
        # textured vertex ~25)
        out = []; st8 = [None]; nv = 0
        for n, prim, sprim in ((3, 0, 2), (4, 1, 3)):
            grp = [f for f in m['faces'] if len(f['idx']) == n]
            if not grp: continue
            polys = [tuple(corner(f, k) for k in range(n)) for f in grp]
            groups = {}
            for p_ in polys: groups.setdefault(bset(p_) if split else 0, []).append(p_)
            allstrips, allsingles = [], []
            for g in sorted(groups):
                sts, sgs = ds_track._strips(groups[g], n); allstrips += sts; allsingles += sgs
            def changes(seq, b0):                      # matrix restores emitting seq after bone b0
                k = 0
                for c in seq:
                    if skel is not None and bone[c[0]] != b0: k += 1; b0 = bone[c[0]]
                return k, b0
            left = sorted(allstrips, key=bset)          # strips: each next one the cheapest from the current bone
            while left:
                j = min(range(len(left)), key=lambda i: changes(left[i], st8[0])[0])
                stp = left.pop(j)
                out.append((GX_BEGIN, [sprim]))
                for c in stp: emit(out, st8, c)
                nv += len(stp)
                out.append((GX_END, []))
            if allsingles:                              # single polygons: also turned to start on the current
                out.append((GX_BEGIN, [prim]))          # bone (a rotation keeps the winding)
                left = list(allsingles)
                while left:
                    best = None
                    for i, p_ in enumerate(left):
                        for r in range(n):
                            q = p_[r:] + p_[:r]; k = changes(q, st8[0])[0]
                            if best is None or k < best[0]: best = (k, i, q)
                        if best[0] == 0: break
                    _, i, q = best; left.pop(i)
                    for c in q: emit(out, st8, c)
                    nv += n
                out.append((GX_END, []))
        nr = sum(1 for c in out if c[0] == GX_MTX_RESTORE)
        return out, nv, nr
    npoly = len(m['faces'])
    best = min((build(sp) for sp in ((False, True) if skel is not None else (False,))), key=lambda r: r[1] * 25 + r[2] * 36)
    cmds, nverts, _ = best
    mesh_dl.stats = (nverts, sum(1 for c in cmds if c[0] == GX_MTX_RESTORE))
    w = pack_gx(cmds)
    return [len(w)] + w, used, npoly

def main(cdir, out, tex_size=128, extra_dirs=()):
    """riders + boards from cdir; extra_dirs (e.g. sprites/: trees, pickups) add static meshes with their own textures"""
    os.makedirs(out, exist_ok=True)
    dirfiles = {d: {f.lower(): f for f in os.listdir(d)} for d in (cdir,) + tuple(extra_dirs)}
    files = dirfiles[cdir]
    anim0 = read_xga(os.path.join(cdir, 'a_switch.xga'))
    texnames, texs = [], []
    def tex_index(name, low, d=cdir):
        if not name: return -1
        files = dirfiles[d]
        cands = []
        if low:
            b = name[:-4]
            cands += [b + '_low.xgt', re.sub(r'(\d+)$', r'_\1', b) + '_low.xgt']
        cands.append(name)
        for c in cands:
            if c.lower() in files:
                key = c.lower()
                if key not in texnames:
                    texnames.append(key); texs.append(texture_to_ds(read_xgt(os.path.join(d, files[key]))[0], tex_size))
                return texnames.index(key)
        return -1
    blob = Blob(); blob.b += bytes(24)
    meshes = []
    srcs = [(cdir, f) for f in sorted(glob.glob(os.path.join(cdir, '*.xng')))]
    for d in extra_dirs: srcs += [(d, f) for f in sorted(glob.glob(os.path.join(d, '*.xng')))]
    for d, f in srcs:
        m = read_xng(f); name = os.path.basename(f)[:-4]
        tm = re.search(rb'([A-Za-z0-9_]+\.xgt)\0', m['tail'])
        ti = tex_index(tm.group(1).decode() if tm else None, name.endswith('_low') and d == cdir, d)
        wh = (texs[ti]['w'], texs[ti]['h']) if ti >= 0 else None
        if m['flags'] & 32:
            skel = read_rpbn(m); W = world(skel, anim0, 0, 'wxyz', True)
        else:
            skel = None; W = None
        dl, used, npoly = mesh_dl(m, skel, wh, W)
        off = blob.add(struct.pack(f'<{len(dl)}I', *dl))
        meshes.append((name, ti, skel is not None, used, npoly, off))
        print(f'  {name:14s} polys {npoly:4d} verts {mesh_dl.stats[0]:4d} restores {mesh_dl.stats[1]:4d} bones {len(used):2d} tex {texnames[ti] if ti >= 0 else "-"}')
    # every rider's own boards (0x3af34 dresses def_board in "<char>_board_<n>_low.xgt", the high-detail rider in
    # "<char>_board_<n>.xgt"); the runtime swaps them in by name
    for c in ('zoe', 'moby', 'psymon', 'griff', 'viggo', 'elise', 'nate', 'mac', 'alegra', 'kaori'):
        for n in range(4):
            for low in (True, False):
                nm = f'{c}_board_{n}' + ('_low' if low else '') + '.xgt'
                if nm in files and nm not in texnames:
                    texnames.append(nm); texs.append(texture_to_ds(read_xgt(os.path.join(cdir, files[nm]))[0], tex_size))
    mesh_off = blob.add(b''.join(struct.pack('<16shBB24sHHI', n.encode()[:15], ti, int(sk), len(u),
                                             bytes(u) + bytes(24 - len(u)), np_, 0, o) for n, ti, sk, u, np_, o in meshes))
    tex_off = blob.add(bytes(40 * len(texs)))
    for i, t in enumerate(texs):
        ot = blob.add(t['texels']); op = blob.add(t['pal'])
        struct.pack_into('<24sHHBBHII', blob.b, tex_off + 40 * i, texnames[i].encode()[:23], t['w'], t['h'], 4, t['flags'], 0, ot, op)
    struct.pack_into('<4s5I', blob.b, 0, b'SSXR', 1, len(meshes), len(texs), mesh_off, tex_off)
    open(os.path.join(out, 'riders.ssxr'), 'wb').write(bytes(blob.b))
    # animations
    m0 = read_xng(os.path.join(cdir, 'elise_low.xng')); parent = read_rpbn(m0)['parent']
    ab = Blob(); anims = sorted(glob.glob(os.path.join(cdir, '*.xga')))
    ab.b += struct.pack('<4s3I', b'SSXA', 1, 32, len(anims)) + struct.pack('<32b', *parent)
    tab = len(ab.b); ab.b += bytes(36 * len(anims))
    for i, f in enumerate(anims):
        a = read_xga(f); q = a['q'].copy(); q[..., 1:] *= -1          # store conjugated -> direct use
        fr = np.concatenate([np.round(q * 16384), np.round(a['t'])], -1).astype('<i2')
        o = ab.add(fr.tobytes())
        h = a['hdr']
        struct.pack_into('<24sH6sI', ab.b, tab + 36 * i, os.path.basename(f)[:-4].encode()[:23], a['nframes'],
                         struct.pack('<BBHH', h[0], 0, h[1], h[3] & 0xffff), o)   # key interval, 0, nkeys, length (30 fps frames)
    open(os.path.join(out, 'anims.ssxa'), 'wb').write(bytes(ab.b))
    print(f'riders.ssxr {len(blob.b)} bytes ({len(meshes)} meshes, {len(texs)} textures); anims.ssxa {len(ab.b)} bytes ({len(anims)} anims)')

if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('cdir'); ap.add_argument('out'); ap.add_argument('--tex', type=int, default=128)
    a = ap.parse_args(); main(a.cdir, a.out, a.tex)

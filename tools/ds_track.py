"""Convert an SSX 3 (Gizmondo) slope.oct track into a DS-ready .ssxt file.

usage: python3 ds_track.py <track_dir> <out.ssxt> [--seg 4000] [--tex 128]

.ssxt layout (all little-endian, every block 4-byte aligned, offsets from file start):
  header  'SSXT' u32 version(2) u32 nseg u32 nmat u32 ntex u32 nlod
          u32 off_raceline u32 off_markers u32 off_textures u32 off_materials u32 off_segments
          u32 nraceline u32 nmarkers  i32 bbmin[3] i32 bbmax[3]
  raceline   nraceline x { i32 x,y,z ; i32 arclen }
  markers    nmarkers  x { char name[24] ; i32 pos[3] ; i16 quat[4] (2.14) }
  textures   ntex x { u16 w, u16 h, u8 fmt (4 = GL_RGB256), u8 flags (1 = colour0 transparent),
                      u16 pad, u32 off_texels, u32 off_palette }   texels w*h bytes, palette 256 x RGB15
  materials  nmat x { i16 tex (-1 = untextured), u16 flags }
  segments   nseg x { i32 origin[3] ; i16 bbmin[3], i16 bbmax[3] ; i32 s0, s1 (race-line arclen range) ;
                      u32 off_collision ; u32 chunk_off, chunk_size  (all of this segment's lists, display
                      lists and collision live inside [chunk_off, chunk_off+chunk_size) -> stream per segment) ;
                      nlod x { u16 npolys, u16 nverts, u32 off_lists, u16 nlists, u16 pad } }
     LOD choice is by race-line distance from the camera (see budget.py LOD_DIST).
     lists:    nlists x { u16 material (0xFFFF = untextured, colour baked), u16 npolys, u32 off_dl, s16 bbmin[3], bbmax[3] } (v5: per material per 1024 x 1024 tile)
     dl:       glCallList-ready: u32 nwords, then packed GX commands
               (BEGIN_VTXS, COLOR, TEXCOORD, VTX_16, END_VTXS). VTX_16 units = 1 game unit relative
               to the segment origin -> render with glTranslate(origin) + glScale(16) when 1 DS unit =
               256 game units.
     collision u16 ntri, u16 nvert, i16 xyz[nvert] (local), {u16 a,b,c, u8 mat, u8 surface, i16 normal[3] (0x4000 = 1, cross(b-a, c-a))}[ntri]   (surface = OCT face flags bits 16..19; 0 = no collision)
"""
import os, sys, struct, argparse
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssxoct import read_oct
from ssxtex import read_xtf
from simplify import simplify

TILE = 1024
GX_BEGIN, GX_END, GX_COLOR, GX_TEXCOORD, GX_VTX16 = 0x40, 0x41, 0x20, 0x22, 0x23
NPARAM = {GX_BEGIN: 1, GX_END: 0, GX_COLOR: 1, GX_TEXCOORD: 1, GX_VTX16: 2}

def pack_gx(cmds):
    """cmds: list of (cmd, [params]) -> list of u32 words in packed format (4 cmds per header word)."""
    out = []
    for i in range(0, len(cmds), 4):
        grp = cmds[i:i + 4]
        hdr = 0
        for k, (c, _) in enumerate(grp): hdr |= c << (8 * k)
        out.append(hdr)
        for c, p in grp:
            assert len(p) == NPARAM[c]
            out.extend(p)
    return out

def rgb15(c):
    return (int(c[0]) >> 3) | ((int(c[1]) >> 3) << 5) | ((int(c[2]) >> 3) << 10)

def _strips(polys, n):
    """Greedy strips over polygons of n corners (3: triangle strips, 4: quad strips). polys: tuples of hashable
    corner keys in their original winding. Returns (strips, singles): strips as corner sequences in the DS's strip
    order (it flips every other triangle / pairs quad edges itself, so each polygon keeps its winding)."""
    edge = {}                                         # directed edge a->b -> polygons having it
    for i, p in enumerate(polys):
        for k in range(n): edge.setdefault((p[k], p[(k + 1) % n]), []).append(i)
    used = [False] * len(polys)
    def take(a, b, seen):                             # an unused polygon with directed edge a->b
        for i in edge.get((a, b), ()):
            if not used[i] and i not in seen: return i
        return -1
    def rot(p, a):                                    # p rotated to start at corner a
        k = p.index(a); return p[k:] + p[:k]
    def grow(seq, mark):
        seen = set()
        while True:
            if n == 3:
                j = len(seq) - 2                       # index of the triangle to add
                x, y = seq[-2], seq[-1]
                a, b = (x, y) if j % 2 == 0 else (y, x)
                i = take(a, b, seen)
                if i < 0: return seq
                q = rot(polys[i], a); d = q[2]
            else:
                x, y = seq[-2], seq[-1]                # next quad: y->x ... the strip pairs (x, y) = (v2i, v2i+1)
                i = take(x, y, seen)                   # the next quad has the directed edge x->y
                if i < 0: return seq
                q = rot(polys[i], x)                   # q = x, y, r, s (cyclic) -> next pair is (s, r)
                if q[1] != y: return seq           # (a polygon using a corner twice)
                d = None
            seen.add(i)
            if mark: used[i] = True
            if n == 3: seq.append(d)
            else: seq += [q[3], q[2]]
    def score(i):
        return sum(1 for k in range(n) for j in edge.get((polys[i][(k + 1) % n], polys[i][k]), ()) if not used[j] and j != i)
    order = sorted(range(len(polys)), key=score)
    strips, singles = [], []
    for i in order:
        if used[i]: continue
        p = polys[i]; best = None
        for r in range(n):
            q = p[r:] + p[:r]
            if n == 3: seq0 = list(q)
            else: seq0 = [q[0], q[1], q[3], q[2]]         # quad (v0 v1 v2 v3) as strip v0 v1 v3 v2
            used[i] = True
            seq = grow(list(seq0), False)              # trial (no marking)
            used[i] = False
            if best is None or len(seq) > len(best[1]): best = (seq0, seq)
        used[i] = True
        seq = grow(list(best[0]), True)
        if len(seq) == n: singles.append(p)
        else: strips.append(seq)
    return strips, singles


def build_dl(faces, V, vcol, tex_wh):
    """faces: list of (idx list, uv list) all same material. Triangles and quads go out as strips where they share
    edges (same corner = same vertex and texture coordinate): the geometry engine works per vertex, and the 3D
    scene was bound by it (track drawing ~27 scanlines a frame with separate triangles and quads)."""
    def corner(vi, uvk):
        if tex_wh:
            s = int(round(uvk[0] * tex_wh[0] * 16)); t = int(round(uvk[1] * tex_wh[1] * 16))
        else: s = t = 0
        return (int(vi), s, t)
    def emit(c):
        vi, s_, t_ = c
        cmds.append((GX_COLOR, [rgb15(vcol[vi])]))
        if tex_wh: cmds.append((GX_TEXCOORD, [((t_ & 0xFFFF) << 16) | (s_ & 0xFFFF)]))
        x, y, z = (int(a) for a in V[vi])
        cmds.append((GX_VTX16, [((y & 0xFFFF) << 16) | (x & 0xFFFF), z & 0xFFFF]))
    cmds = []
    for n, prim, sprim in ((3, 0, 2), (4, 1, 3)):
        polys = [tuple(corner(vi, f[1][k]) for k, vi in enumerate(f[0])) for f in faces if len(f[0]) == n]
        if not polys: continue
        strips, singles = _strips(polys, n)
        for st in strips:
            cmds.append((GX_BEGIN, [sprim]))
            for c in st: emit(c)
            cmds.append((GX_END, []))
        if singles:
            cmds.append((GX_BEGIN, [prim]))
            for p in singles:
                for c in p: emit(c)
            cmds.append((GX_END, []))
    words = pack_gx(cmds)
    return [len(words)] + words

def weld(P):
    """map each vertex to the first vertex with the identical position."""
    first = {}; out = np.empty(len(P), int)
    for i, p in enumerate(map(tuple, P)):
        out[i] = first.setdefault(p, i)
    return out

def drop_small(P, tris, mats, uvs, min_size, keep=None):
    """remove connected components whose bounding-box diagonal < min_size (but none touching a `keep` vertex:
    a piece of ground cut small by a segment border is not a small object - dropping it opened a hole there)."""
    par = list(range(len(P)))
    def fd(x):
        while par[x] != x: par[x] = par[par[x]]; x = par[x]
        return x
    for t in tris:
        for a in t[1:]: par[fd(a)] = fd(t[0])
    lo = {}; hi = {}
    for t in tris:
        r = fd(t[0])
        for a in t:
            lo[r] = np.minimum(lo.get(r, P[a]), P[a]); hi[r] = np.maximum(hi.get(r, P[a]), P[a])
    big = set()
    if keep is not None:
        for t in tris:
            if any(keep[a] for a in t): big.add(fd(t[0]))
    keep = [i for i, t in enumerate(tris) if fd(t[0]) in big or np.linalg.norm(hi[fd(t[0])] - lo[fd(t[0])]) >= min_size]
    return [tris[i] for i in keep], [mats[i] for i in keep], [uvs[i] for i in keep]

def requad(P, faces, cos_planar=0.985, check_uv=True):
    """greedily merge triangle pairs sharing an edge (same material, matching edge UVs, convex, near-planar)."""
    edge = {}
    for i, (t, uv, mt) in enumerate(faces):
        if len(t) != 3: continue
        for k in range(3):
            edge.setdefault(tuple(sorted((t[k], t[(k + 1) % 3]))), []).append(i)
    used = [False] * len(faces); out = []
    def nrm(t):
        n = np.cross(P[t[1]] - P[t[0]], P[t[2]] - P[t[0]]); l = np.linalg.norm(n); return n / l if l else n
    for e, fs in edge.items():
        if len(fs) != 2 or used[fs[0]] or used[fs[1]]: continue
        (ta, ua, ma), (tb, ub, mb) = faces[fs[0]], faces[fs[1]]
        if ma != mb or nrm(ta) @ nrm(tb) < cos_planar: continue
        # quad a0 a1 x a2 where edge (a1,a2) shared: rotate a so shared edge is a[1]-a[2]
        for r in range(3):
            if {ta[(r + 1) % 3], ta[(r + 2) % 3]} == set(e): break
        a = [ta[(r + k) % 3] for k in range(3)]; au = [ua[(r + k) % 3] for k in range(3)]
        x = [v for v in tb if v not in e][0]
        bu = {v: ub[tb.index(v)] for v in tb}
        if check_uv and np.abs(np.subtract(bu[a[1]], au[1])).max() > 1e-3 or np.abs(np.subtract(bu[a[2]], au[2])).max() > 1e-3: continue
        q = [a[0], a[1], x, a[2]]; qu = [au[0], au[1], bu[x], au[2]]
        # convexity: consecutive cross products agree with the face normal
        n = nrm(ta); Q = P[q]
        if any(np.cross(Q[(k + 1) % 4] - Q[k], Q[(k + 2) % 4] - Q[(k + 1) % 4]) @ n <= 0 for k in range(4)): continue
        used[fs[0]] = used[fs[1]] = True; out.append((q, qu, ma))
    out += [f for i, f in enumerate(faces) if not used[i]]
    return out

def arclen_assign(points, rl):
    seg = np.linalg.norm(np.diff(rl, axis=0), axis=1); L = np.concatenate([[0], np.cumsum(seg)])
    dense, dl = [], []
    for i in range(len(rl) - 1):
        for t in np.linspace(0, 1, 32, endpoint=False):
            dense.append(rl[i] * (1 - t) + rl[i + 1] * t); dl.append(L[i] + t * seg[i])
    dense.append(rl[-1]); dl.append(L[-1])
    dense = np.array(dense); dl = np.array(dl)
    best = np.zeros(len(points), int); dist = np.full(len(points), np.inf)
    for i in range(0, len(dense), 256):
        D = ((points[:, None, :] - dense[None, i:i + 256, :]) ** 2).sum(-1)
        j = D.argmin(1); dd = D[np.arange(len(points)), j]
        upd = dd < dist; dist[upd] = dd[upd]; best[upd] = j[upd] + i
    return dl[best], L

def convert_texture(path, size):
    rgba, info = read_xtf(path)
    d = open(path, 'rb').read(); pal = np.frombuffer(d[-1024:], np.uint8).reshape(256, 4)[:, [2, 1, 0, 3]]   # B, G, R, A
    w, h = info['w'], info['h']
    idx = np.frombuffer(d, np.uint8, w * h, 24).reshape(h, w)
    tw, th = min(size, w), min(size, h)
    if (tw, th) != (w, h):
        # area-average downscale in RGBA, then map back onto the original palette
        img = Image.fromarray(rgba, 'RGBA').resize((tw, th), Image.BOX)
        a = np.asarray(img).astype(np.int32)
        P = pal.astype(np.int32)
        flat = a.reshape(-1, 4)
        best = np.zeros(len(flat), np.uint8)
        for i in range(0, len(flat), 4096):
            D = ((flat[i:i + 4096, None, :3] - P[None, :, :3]) ** 2).sum(-1)
            D += np.where((flat[i:i + 4096, None, 3] < 128) != (P[None, :, 3] < 128), 1 << 24, 0)
            best[i:i + 4096] = D.argmin(1)
        idx = best.reshape(th, tw)
    transparent = pal[:, 3] < 128
    flags = 0
    if transparent[idx].any():
        # make palette slot 0 the (single) transparent colour
        tslot = int(np.nonzero(transparent)[0][0])
        idx = idx.copy(); idx[transparent[idx]] = tslot
        perm = np.arange(256); perm[[0, tslot]] = perm[[tslot, 0]]
        inv = np.argsort(perm); idx = inv[idx].astype(np.uint8); pal = pal[perm]
        flags = 1
    pal15 = np.array([rgb15(c) for c in pal], np.uint16)
    return dict(w=tw, h=th, texels=idx.astype(np.uint8).tobytes(), pal=pal15.tobytes(), flags=flags)

class Blob:
    def __init__(self): self.b = bytearray()
    def align(self):
        while len(self.b) % 4: self.b.append(0)
    def add(self, data):
        self.align(); o = len(self.b); self.b += data; return o

def convert(track_dir, out_path, seg_len=4000, tex_size=128, lod_ratios=(1.0, 0.55, 0.3, 0.1), log=print):
    m = read_oct(os.path.join(track_dir, 'slope.oct'))
    V = m['verts'].astype(np.int64); vcol = m['vcolors']
    rl = [s for s in m['splines'] if s['name'] == 'default'][0]['pts'].astype(float)
    cent = np.array([V[f['idx']].mean(0) for f in m['faces']], float)
    s, L = arclen_assign(cent, rl)
    # variable-length segments: cut along the race line so each holds <= max_faces faces,
    # never longer than seg_len and (unless forced by density) not shorter than seg_len / 4
    order = np.argsort(s); cuts = [0.0]; cnt = 0; max_faces = 650
    for fi in order:
        if s[fi] - cuts[-1] >= seg_len or (cnt >= max_faces and s[fi] - cuts[-1] >= seg_len / 4):
            cuts.append(float(s[fi])); cnt = 0
        cnt += 1
    cuts.append(float(L[-1]) + 1)
    cuts = np.array(cuts); nseg = len(cuts) - 1
    seg_of = np.clip(np.searchsorted(cuts, s, side='right') - 1, 0, nseg - 1)
    # textures (dedupe by file name)
    files = {f.lower(): f for f in os.listdir(track_dir)}
    texnames, mat_tex = [], []
    for name in m['materials']:
        if name and name.lower() in files:
            if name.lower() not in texnames: texnames.append(name.lower())
            mat_tex.append(texnames.index(name.lower()))
        else: mat_tex.append(-1)
    texs = [convert_texture(os.path.join(track_dir, files[n]), tex_size) for n in texnames]
    tex_avg = [read_xtf(os.path.join(track_dir, files[n]))[0][..., :3].reshape(-1, 3).mean(0) / 255.0 for n in texnames]
    mat_avg = [tex_avg[t] if t >= 0 else np.ones(3) for t in mat_tex]
    blob = Blob()
    blob.b += bytes(84)  # header placeholder (fixed size, 84 bytes)
    # race line
    rl_off = blob.add(b''.join(struct.pack('<4i', *(int(a) for a in p), int(l)) for p, l in zip(rl, L)))
    mk = list(m['markers'])   # v3: trees included (rider/tree collision, 0x4e8f4)
    mk_off = blob.add(b''.join(struct.pack('<24s3i4h', k['name'][:23].encode(), *k['pos'],
                                           *(int(round(q * 16384)) for q in k['quat'])) for k in mk))
    # grind rails: every spline except the race line (the exe takes splines whose name starts with 'r', 0x4e278)
    rails = [s for s in m['splines'] if s['name'] != 'default' and s['name'].startswith('r') and len(s['pts']) >= 2]
    rail_tab, rail_pts, k0 = b'', b'', 0
    for s in rails:
        P = np.asarray(s['pts']).astype(int)
        rail_tab += struct.pack('<2I', len(P), k0); k0 += len(P)
        rail_pts += b''.join(struct.pack('<3i', *(int(v) for v in q)) for q in P)
    rails_off = blob.add(rail_tab + rail_pts)
    tex_tab = bytearray()
    tex_data = []
    for t in texs: tex_data.append(t)
    tex_off = blob.add(bytes(16 * len(texs)))
    for i, t in enumerate(texs):
        o_t = blob.add(t['texels']); o_p = blob.add(t['pal'])
        struct.pack_into('<HHBBHII', blob.b, tex_off + 16 * i, t['w'], t['h'], 4, t['flags'], 0, o_t, o_p)
    mat_off = blob.add(b''.join(struct.pack('<hH', mt, 0) for mt in mat_tex))
    nlod = len(lod_ratios)
    seg_rec = 12 + 12 + 8 + 4 + 8 + nlod * 12
    seg_off = blob.add(bytes(seg_rec * nseg))
    stats = []
    # vertices on a segment border are locked in the simplified LODs. By position, not index: the same point can be
    # two vertices (a texture seam), and an unlocked copy moved - a crack between a simplified segment and its
    # neighbour, open to the sky (40 such border points on R&B, 206 on Snow Jam)
    used_pos = {}
    for fi, f in enumerate(m['faces']):
        for a in f['idx']: used_pos.setdefault(tuple(V[a]), set()).add(seg_of[fi])
    used_by = [used_pos.get(tuple(V[a]), set()) for a in range(len(V))]
    for sg in range(nseg):
        fids = np.nonzero(seg_of == sg)[0]
        if len(fids) == 0:
            struct.pack_into('<3i', blob.b, seg_off + sg * seg_rec, 0, 0, 0)  # empty segment
            stats.append([0] * nlod); continue
        gidx = sorted({a for fi in fids for a in m['faces'][fi]['idx']})
        remap = {g: i for i, g in enumerate(gidx)}
        blob.align(); chunk_start = len(blob.b)
        P = V[gidx]; lo, hi = P.min(0), P.max(0); origin = (lo + hi) // 2
        Lp = P - origin
        assert np.abs(Lp).max() < 32767, f'segment {sg} too large: {np.abs(Lp).max()}'
        lcol = vcol[gidx]
        faces = [(m['faces'][fi]['idx'], m['faces'][fi]['uv'], m['faces'][fi]['tex']) for fi in fids]
        # face flag bit 0: collision only, never drawn (every such face is untextured - Metro City's, The Junction's
        # and Intimidator's invisible walls; they came out as plain white blocks). Kept for the collision grid.
        fvis = [not (m['faces'][fi]['flags'] & 1) for fi in fids]
        locked = np.array([len(used_by[g]) > 1 for g in gidx])
        tris, tuv, tmat, tsurf, tvis = [], [], [], [], []
        for (idx, uv, mt), fi, vis in zip(faces, fids, fvis):
            li = [remap[a] for a in idx]
            for tri in ([0, 1, 2], [0, 2, 3])[:len(li) - 2]:
                tris.append([li[k] for k in tri]); tuv.append([uv[k] for k in tri]); tmat.append(mt)
                tsurf.append((m['faces'][fi]['flags'] >> 16) & 15); tvis.append(vis)
        # untextured far-LOD colour: vertex colour x mean texture colour of incident materials
        acc = np.zeros((len(gidx), 3)); cnt = np.zeros(len(gidx))
        for t_, mt, vis in zip(tris, tmat, tvis):
            if not vis: continue
            for a in t_: acc[a] += mat_avg[mt] if mt < len(mat_avg) else 1.0; cnt[a] += 1
        flat_col = (lcol[:, :3] * (acc / np.maximum(cnt, 1)[:, None])).clip(0, 255).astype(np.uint8)
        lods = []
        wmap = weld(Lp)                                   # weld duplicate positions for far LODs
        wtris = [[int(wmap[a]) for a in t] for t in tris]
        for li_, r in enumerate(lod_ratios):
            untextured = li_ == len(lod_ratios) - 1
            if li_ == 0:
                lf = [([remap[a] for a in idx], uv, mt) for (idx, uv, mt), vis in zip(faces, fvis) if vis]
            else:
                ok = [i for i, t in enumerate(wtris) if len(set(t)) == 3 and tvis[i]]
                ct, cm, cu = [wtris[i] for i in ok], [tmat[i] for i in ok], [tuv[i] for i in ok]
                wlk = locked.copy()
                for i in range(len(wlk)): wlk[wmap[i]] |= locked[i]
                ct, cm, cu = drop_small(Lp.astype(float), ct, cm, cu, (0, 150, 250, 600)[min(li_, 3)], None if untextured else wlk)
                tgt = max(4, int(len(tris) * r))
                if untextured:
                    st, su, sm = simplify(Lp.astype(float), ct, cu, [0] * len(ct), np.zeros_like(locked), tgt, seam_cos=2.0)
                    lf = [(t, [tuple(x) for x in u], 0xFFFF) for t, u in zip(st, su)]
                else:
                    wl = locked.copy()
                    for i in range(len(wl)): wl[wmap[i]] |= locked[i]
                    st, su, sm = simplify(Lp.astype(float), ct, cu, cm, wl, tgt)
                    lf = [(t, [tuple(x) for x in u], mt) for t, u, mt in zip(st, su, sm)]
                lf = requad(Lp.astype(float), lf, check_uv=not untextured)
            lists = []
            # v5: each material's faces split into TILE x TILE (x/z) tiles, each its own list with its box, so the
            # runtime can leave out the tiles outside the view or past the fog (the geometry engine works on all
            # it is sent; whole segments are too long to cull: ~30% fewer polygons sent at 1024, measured offline)
            lo2 = Lp.min(0)
            def tile_of(f):
                c = Lp[list(f[0])].mean(0)
                return (int((c[0] - lo2[0]) // TILE), int((c[2] - lo2[2]) // TILE))
            for mt in sorted({f[2] for f in lf}):
                ffm = [f for f in lf if f[2] == mt]
                for tk in sorted({tile_of(f) for f in ffm}):
                    ff = [(f[0], f[1]) for f in ffm if tile_of(f) == tk]
                    if mt == 0xFFFF:
                        dl = build_dl(ff, Lp, flat_col, None)
                    else:
                        ti = mat_tex[mt] if mt < len(mat_tex) else -1
                        wh = (texs[ti]['w'], texs[ti]['h']) if ti >= 0 else None
                        dl = build_dl(ff, Lp, lcol, wh)
                    Q = Lp[[a for f in ff for a in f[0]]]
                    bb = tuple(int(v) for v in Q.min(0)) + tuple(int(v) for v in Q.max(0))
                    lists.append((mt, len(ff), blob.add(struct.pack(f'<{len(dl)}I', *dl)), bb))
            lst_off = blob.add(b''.join(struct.pack('<HHI6h', mt, n, o, *bb) for mt, n, o, bb in lists))
            npoly = sum(l_[1] for l_ in lists)
            nvert = len({a for f in lf for a in f[0]})
            lods.append((npoly, nvert, lst_off, len(lists)))
        # collision: LOD0 triangles
        col = struct.pack('<HH', len(tris), len(gidx)) + Lp.astype('<i2').tobytes()
        def tri_normal(t):
            a, b, cc = Lp[t[0]].astype(np.float64), Lp[t[1]].astype(np.float64), Lp[t[2]].astype(np.float64)
            n = np.cross(b - a, cc - a); l = np.linalg.norm(n)
            return (0, 0, 0) if l < 1e-9 else tuple(int(v) for v in np.trunc(n * 16384 / l))
        col += b''.join(struct.pack('<3HBB3h', *t, mt & 0xFF, sf, *tri_normal(t)) for t, mt, sf in zip(tris, tmat, tsurf))
        col_off = blob.add(col); blob.align(); chunk_end = len(blob.b)
        rec = struct.pack('<3i3h3hiiIII', *origin.astype(int), *(lo - origin).astype(int), *(hi - origin).astype(int),
                          int(cuts[sg]), int(cuts[sg + 1]), col_off, chunk_start, chunk_end - chunk_start)
        rec += b''.join(struct.pack('<HHIHH', a, b, c, d, 0) for a, b, c, d in lods)
        blob.b[seg_off + sg * seg_rec: seg_off + (sg + 1) * seg_rec] = rec
        stats.append([l[0] for l in lods])
        log(f'  seg {sg:3d}: polys per LOD {[l[0] for l in lods]}')
    bb0 = V.min(0); bb1 = V.max(0)
    hdr = struct.pack('<4s10I2I6i2I', b'SSXT', 5, nseg, len(mat_tex), len(texs), nlod,
                      rl_off, mk_off, tex_off, mat_off, seg_off, len(rl), len(mk), *bb0, *bb1, rails_off, len(rails))
    blob.b[:len(hdr)] = hdr
    open(out_path, 'wb').write(bytes(blob.b))
    return dict(nseg=nseg, stats=stats, size=len(blob.b), tex_bytes=sum(len(t['texels']) + 512 for t in texs),
                ntex=len(texs), length=float(L[-1]))

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('track_dir'); ap.add_argument('out')
    ap.add_argument('--seg', type=int, default=4000); ap.add_argument('--tex', type=int, default=128)
    a = ap.parse_args()
    r = convert(a.track_dir, a.out, a.seg, a.tex)
    print({k: v for k, v in r.items() if k != 'stats'})

"""Decode a .ssxt (DS track) and render it the way the DS runtime would: 256x192, GX display lists,
texture x vertex colour, LOD schedule along the race line, linear fog. Validation tool."""
import struct, sys, os
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from budget import lod_for

NPARAM = {0x40: 1, 0x41: 0, 0x20: 1, 0x21: 1, 0x22: 1, 0x23: 2, 0x2A: 1, 0x2B: 1}

def decode_dl(d, off):
    n = struct.unpack_from('<I', d, off)[0]
    w = struct.unpack_from(f'<{n}I', d, off + 4)
    i = 0; prims = []; cur = None; col = 0x7FFF; tc = (0, 0)
    while i < n:
        hdr = w[i]; i += 1
        for k in range(4):
            c = (hdr >> (8 * k)) & 0xFF
            if c == 0: continue
            p = w[i:i + NPARAM[c]]; i += NPARAM[c]
            if c == 0x40: cur = [p[0], []]
            elif c == 0x41: prims.append(cur); cur = None
            elif c == 0x20: col = p[0]
            elif c == 0x22:
                s = p[0] & 0xFFFF; t = p[0] >> 16
                tc = ((s - 65536 if s & 0x8000 else s) / 16, (t - 65536 if t & 0x8000 else t) / 16)
            elif c == 0x23:
                x = p[0] & 0xFFFF; y = p[0] >> 16; z = p[1] & 0xFFFF
                sx = lambda a: a - 65536 if a & 0x8000 else a
                cur[1].append(((sx(x), sx(y), sx(z)), col, tc))
    return prims

class Track:
    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        (_, ver, self.nseg, self.nmat, self.ntex, self.nlod, rl, mk, tx, mt, sg, nrl, nmk) = struct.unpack_from('<4s12I', d, 0)
        self.rl = np.array([struct.unpack_from('<4i', d, rl + 16 * i) for i in range(nrl)], float)
        self.markers = [(d[mk + 44 * i: mk + 44 * i + 24].split(b'\0')[0].decode(), struct.unpack_from('<3i', d, mk + 44 * i + 24)) for i in range(nmk)]
        self.tex = []
        for i in range(self.ntex):
            w, h, fmt, fl, _, ot, op = struct.unpack_from('<HHBBHII', d, tx + 16 * i)
            pal = np.frombuffer(d, '<u2', 256, op).astype(np.int32)
            rgb = np.stack([(pal & 31), (pal >> 5) & 31, (pal >> 10) & 31], 1) * 255 // 31
            img = rgb[np.frombuffer(d, np.uint8, w * h, ot)].reshape(h, w, 3)
            alpha = np.ones((h, w), bool)
            if fl & 1: alpha = np.frombuffer(d, np.uint8, w * h, ot).reshape(h, w) != 0
            self.tex.append((img.astype(np.uint8), alpha))
        self.mat = [struct.unpack_from('<hH', d, mt + 4 * i)[0] for i in range(self.nmat)]
        rec = 12 + 12 + 8 + 4 + 8 + self.nlod * 12
        self.segs = []
        for i in range(self.nseg):
            o = sg + i * rec
            origin = struct.unpack_from('<3i', d, o)
            lods = [struct.unpack_from('<HHIHH', d, o + 44 + 12 * l) for l in range(self.nlod)]
            s0, s1, col, coff, csz = struct.unpack_from('<iiIII', d, o + 24)
            self.segs.append((np.array(origin), lods, s0, s1, coff, csz))

    def prims(self, seg, lod):
        origin, lods = self.segs[seg][:2]
        npoly, nvert, lo, nl, _ = lods[lod]
        out = []
        for j in range(nl):
            mat, n, dl = struct.unpack_from('<HHI', self.d, lo + 8 * j)
            tex = None if mat == 0xFFFF or self.mat[mat] < 0 else self.tex[self.mat[mat]]
            for prim, verts in decode_dl(self.d, dl):
                k = 3 if prim == 0 else 4
                for q in range(0, len(verts), k):
                    out.append((tex, [(np.array(p) + origin, c, t) for p, c, t in verts[q:q + k]]))
        return out

def rgb15f(c): return np.array([c & 31, (c >> 5) & 31, (c >> 10) & 31], float) / 31

def render(trk, eye, target, W=256, H=192, fov=70, s_pos=0, fog=(16000, 26000), sky=(150, 180, 215)):
    f = target - eye; f /= np.linalg.norm(f); r = np.cross(f, [0, 1, 0]); r /= np.linalg.norm(r); u = np.cross(r, f)
    R = np.stack([r, u, -f]); fl = (H / 2) / np.tan(np.radians(fov) / 2)
    img = np.zeros((H, W, 3)); img[:] = sky; zb = np.full((H, W), np.inf); drawn = 0
    for sg in range(trk.nseg):
        lod = lod_for(s_pos, trk.segs[sg][2], trk.segs[sg][3], trk.nlod)
        if lod is None: continue
        for tex, vs in trk.prims(sg, lod):
            P = np.array([v[0] for v in vs], float); C = (P - eye) @ R.T
            if (C[:, 2] > -50).any(): continue           # near clip (crude)
            z = -C[:, 2]; sx = W / 2 + fl * C[:, 0] / z; sy = H / 2 - fl * C[:, 1] / z
            if sx.max() < 0 or sx.min() >= W or sy.max() < 0 or sy.min() >= H: continue
            drawn += 1
            cols = [rgb15f(v[1]) for v in vs]; tcs = [v[2] for v in vs]
            for tri in ([0, 1, 2], [0, 2, 3])[:len(vs) - 2]:
                X, Y, Z = sx[tri], sy[tri], z[tri]
                x0, x1 = int(max(0, np.floor(X.min()))), int(min(W - 1, np.ceil(X.max())))
                y0, y1 = int(max(0, np.floor(Y.min()))), int(min(H - 1, np.ceil(Y.max())))
                if x1 < x0 or y1 < y0: continue
                den = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0])
                if abs(den) < 1e-9: continue
                ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1] + 0.5
                l1 = ((xs - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (ys - Y[0])) / den
                l2 = ((X[1] - X[0]) * (ys - Y[0]) - (xs - X[0]) * (Y[1] - Y[0])) / den
                l0 = 1 - l1 - l2; m = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
                if not m.any(): continue
                L = np.stack([l0[m] / Z[0], l1[m] / Z[1], l2[m] / Z[2]], 1); dep = 1 / L.sum(1); L *= dep[:, None]
                yy = (ys[m] - .5).astype(int); xx = (xs[m] - .5).astype(int)
                ok = dep < zb[yy, xx]; yy, xx, dep, L = yy[ok], xx[ok], dep[ok], L[ok]
                col = L @ np.array([cols[i] for i in tri])
                if tex is not None:
                    timg, ta = tex; th, tw = ta.shape
                    s_ = (L @ np.array([tcs[i][0] for i in tri])).astype(int) % tw
                    t_ = (L @ np.array([tcs[i][1] for i in tri])).astype(int) % th
                    keep = ta[t_, s_]; yy, xx, dep, col, s_, t_ = yy[keep], xx[keep], dep[keep], col[keep], s_[keep], t_[keep]
                    col = col * timg[t_, s_]
                else:
                    col = col * 255
                fa = np.clip((dep - fog[0]) / (fog[1] - fog[0]), 0, 1)[:, None]
                img[yy, xx] = col * (1 - fa) + np.array(sky) * fa; zb[yy, xx] = dep
    return img.clip(0, 255).astype(np.uint8), drawn

def view_at(trk, s_frac, back=900, up=420, ahead=2500, **kw):
    rl = trk.rl; L = rl[-1, 3]; s = s_frac * L
    i = int(np.searchsorted(rl[:, 3], s)); i = min(max(i, 1), len(rl) - 1)
    def at(sv):
        j = int(np.clip(np.searchsorted(rl[:, 3], sv), 1, len(rl) - 1))
        a, b = rl[j - 1], rl[j]; t = (sv - a[3]) / max(1e-6, b[3] - a[3])
        return a[:3] * (1 - t) + b[:3] * t
    p = at(s); eye = at(max(0, s - back)) + [0, up, 0]; tgt = at(min(L, s + ahead))
    return render(trk, eye, tgt, s_pos=s, **kw)

def check_chunks(trk):
    """every list / display list / collision offset of a segment must lie inside its streaming chunk."""
    d = trk.d; bad = 0
    for i, (origin, lods, s0, s1, coff, csz) in enumerate(trk.segs):
        inside = lambda o: coff <= o < coff + csz
        for npoly, nvert, lo, nl, _ in lods:
            if nl == 0: continue
            bad += not inside(lo)
            for j in range(nl):
                mat, n, dl = struct.unpack_from('<HHI', d, lo + 8 * j)
                nw = struct.unpack_from('<I', d, dl)[0]
                bad += not (inside(dl) and dl + 4 + 4 * nw <= coff + csz)
    return bad

if __name__ == '__main__':
    trk = Track(sys.argv[1]); out = sys.argv[2]
    print('chunk violations:', check_chunks(trk))
    tiles = []
    for fr in [0.03, 0.3, 0.55, 0.8]:
        im, n = view_at(trk, fr); print(f'{fr:.2f}: {n} polys after crude culling'); tiles.append(im)
    top = np.concatenate(tiles[:2], 1); bot = np.concatenate(tiles[2:], 1)
    Image.fromarray(np.concatenate([top, bot], 0)).resize((1024, 768), Image.NEAREST).save(out)

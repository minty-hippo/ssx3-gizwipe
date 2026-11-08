"""SSX 3 (Gizmondo) .xng mesh reader -> OBJ, plus a tiny numpy preview rasteriser."""
import struct, sys, os
import numpy as np

FACE_SIZE = 60

def read_xng(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'XNG1', path
    nv, _a, nf, _b, flags = struct.unpack_from('<5i', d, 8)
    off = 28
    verts = np.array(struct.unpack_from(f'<{nv*3}i', d, off), np.float32).reshape(nv, 3)
    off += nv * 12
    faces = []
    for i in range(nf):
        r = struct.unpack_from('<4h8i iiiii', d, off + i * FACE_SIZE)
        idx = list(r[0:4])
        uv = [(r[4 + 2*k] / 65536.0, r[5 + 2*k] / 65536.0) for k in range(4)]
        color, _u1, fflags = r[12], r[13], r[14]
        n = 3 if (idx[3] == -1 or idx[3] == idx[0]) else 4
        faces.append(dict(idx=idx[:n], uv=uv[:n], color=color, flags=fflags))
    end = off + nf * FACE_SIZE
    return dict(verts=verts, faces=faces, flags=flags, tail=d[end:], nv=nv, nf=nf)

def color_rgb(c):
    if c == -1: return (1.0, 1.0, 1.0)
    return (((c >> 8) & 15) / 15, ((c >> 4) & 15) / 15, (c & 15) / 15)

def write_obj(m, path, texture=None):
    base = os.path.splitext(path)[0]
    with open(base + '.mtl', 'w') as f:
        f.write('newmtl tex\nKd 1 1 1\n' + (f'map_Kd {texture}\n' if texture else ''))
    with open(path, 'w') as f:
        f.write(f'mtllib {os.path.basename(base)}.mtl\nusemtl tex\n')
        for v in m['verts']: f.write('v %g %g %g\n' % tuple(v))
        t = 1
        for fc in m['faces']:
            for u, v in fc['uv']: f.write('vt %g %g\n' % (u, 1 - v))
            f.write('f ' + ' '.join(f'{i+1}/{t+k}' for k, i in enumerate(fc['idx'])) + '\n')
            t += len(fc['idx'])

def render(m, tex=None, size=384, yaw=30.0, pitch=15.0, up=1, texs=None):
    """Flat-shaded z-buffer render, textured if tex (HxWx3 uint8) given."""
    v = m['verts'].copy()
    if up != 1: v = v[:, [0, 2, 1]] * np.array([1, 1, -1])
    v -= (v.max(0) + v.min(0)) / 2
    a, b = np.radians(yaw), np.radians(pitch)
    Ry = np.array([[np.cos(a), 0, np.sin(a)], [0, 1, 0], [-np.sin(a), 0, np.cos(a)]])
    Rx = np.array([[1, 0, 0], [0, np.cos(b), -np.sin(b)], [0, np.sin(b), np.cos(b)]])
    v = v @ Ry.T @ Rx.T
    s = 0.9 * size / max(1e-6, np.ptp(v[:, :2], 0).max())
    P = np.stack([v[:, 0] * s + size / 2, size / 2 - v[:, 1] * s, v[:, 2]], 1)
    img = np.full((size, size, 3), 40, np.uint8); zb = np.full((size, size), 1e9)
    for fc in m['faces']:
        ids = fc['idx']
        tris = [(0, 1, 2)] + ([(0, 2, 3)] if len(ids) == 4 else [])
        for t in tris:
            p = P[[ids[k] for k in t]]; w = v[[ids[k] for k in t]]
            nrm = np.cross(w[1] - w[0], w[2] - w[0]); ln = np.linalg.norm(nrm)
            shade = 0.35 + 0.65 * abs(nrm[2] / ln) if ln else 1
            x0, y0 = np.floor(p[:, :2].min(0)).astype(int); x1, y1 = np.ceil(p[:, :2].max(0)).astype(int)
            x0, y0 = max(x0, 0), max(y0, 0); x1, y1 = min(x1, size - 1), min(y1, size - 1)
            if x1 < x0 or y1 < y0: continue
            ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1]
            d = (p[1, 0] - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (p[1, 1] - p[0, 1])
            if abs(d) < 1e-9: continue
            l1 = ((xs - p[0, 0]) * (p[2, 1] - p[0, 1]) - (p[2, 0] - p[0, 0]) * (ys - p[0, 1])) / d
            l2 = ((p[1, 0] - p[0, 0]) * (ys - p[0, 1]) - (xs - p[0, 0]) * (p[1, 1] - p[0, 1])) / d
            l0 = 1 - l1 - l2
            msk = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
            z = l0 * p[0, 2] + l1 * p[1, 2] + l2 * p[2, 2]
            yy, xx = ys[msk], xs[msk]; z = z[msk]
            ok = z < zb[yy, xx]; yy, xx, z = yy[ok], xx[ok], z[ok]
            T = texs.get(fc.get('tex')) if texs else tex
            if T is not None and fc['color'] == -1:
                uv = np.array([fc['uv'][k] for k in t])
                L = np.stack([l0[msk][ok], l1[msk][ok], l2[msk][ok]], 1)
                uu = (L @ uv[:, 0]) % 1.0; vv = (L @ uv[:, 1]) % 1.0
                th, tw = T.shape[:2]
                col = T[(vv * th).astype(int) % th, (uu * tw).astype(int) % tw].astype(float)
            else:
                col = np.array(color_rgb(fc['color'])) * 255
            img[yy, xx] = np.clip(col * shade, 0, 255).astype(np.uint8); zb[yy, xx] = z
    return img

if __name__ == '__main__':
    m = read_xng(sys.argv[1])
    print(sys.argv[1], 'verts', m['nv'], 'faces', m['nf'], 'flags', m['flags'], 'tail', len(m['tail']))
    if len(sys.argv) > 2: write_obj(m, sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)

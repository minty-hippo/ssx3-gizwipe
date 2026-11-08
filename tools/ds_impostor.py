"""Tree impostor for the DS port's far trees: the tree mesh (sprites/tree_low.xng + treeeez.xgt) rendered from
the side (orthographic, z-buffered, unlit like the runtime draws it) into a 64 x 128 picture, colour 0 transparent.

usage: python3 ds_impostor.py <sprites_dir> <out.ssxi>
out: 'SSXI' u16 w, h; s16 x0, x1, y0, y1 (the picture's extent in mesh units); u16 pal[256] (RGB15, 0 = clear);
     u8 texels[w*h]
"""
import sys, struct, numpy as np
from PIL import Image
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from ssxmesh import read_xng
from ssxtex import read_xgt
from ds_track import rgb15

def main(sdir, out, W=64, H=128, SS=4):
    m = read_xng(sdir + '/tree_low.xng'); V = np.asarray(m['verts'], float)
    tex = read_xgt(sdir + '/treeeez.xgt')[0]; th, tw = tex.shape[:2]
    key = (tex[..., 0] > 200) & (tex[..., 1] < 60) & (tex[..., 2] > 200)
    x0, x1 = -max(abs(V[:, 0].min()), V[:, 0].max(), abs(V[:, 2].min()), V[:, 2].max()), 0
    x1 = -x0; y0, y1 = V[:, 1].min(), V[:, 1].max()
    w, h = W * SS, H * SS
    col = np.zeros((h, w, 3)); zb = np.full((h, w), -1e9); cov = np.zeros((h, w), bool)
    def px(p): return ((p[0] - x0) / (x1 - x0) * w, (y1 - p[1]) / (y1 - y0) * h)
    for f in m['faces']:
        P = V[f['idx']]; UV = np.asarray(f['uv']) * 8          # the tree's UVs span 1/8 of the texture: all of it
        S = np.array([px(p) for p in P])
        xmin, xmax = int(max(0, np.floor(S[:, 0].min()))), int(min(w - 1, np.ceil(S[:, 0].max())))
        ymin, ymax = int(max(0, np.floor(S[:, 1].min()))), int(min(h - 1, np.ceil(S[:, 1].max())))
        (ax, ay), (bx, by), (cx, cy) = S
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-9: continue
        ys, xs = np.mgrid[ymin:ymax + 1, xmin:xmax + 1] + 0.5
        l0 = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / den
        l1 = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not inside.any(): continue
        z = l0 * P[0, 2] + l1 * P[1, 2] + l2 * P[2, 2]
        u = (l0 * UV[0, 0] + l1 * UV[1, 0] + l2 * UV[2, 0]) % 1.0
        v = (l0 * UV[0, 1] + l1 * UV[1, 1] + l2 * UV[2, 1]) % 1.0
        tx = np.clip((u * tw).astype(int), 0, tw - 1); ty = np.clip((v * th).astype(int), 0, th - 1)
        opaque = inside & ~key[ty, tx]
        yy = ys.astype(int); xx = xs.astype(int)
        sel = opaque & (z > zb[yy, xx])
        zb[yy[sel], xx[sel]] = z[sel]; col[yy[sel], xx[sel]] = tex[ty[sel], tx[sel], :3]; cov[yy[sel], xx[sel]] = True
    c = cov.reshape(H, SS, W, SS).mean((1, 3))
    rgb = (col * cov[..., None]).reshape(H, SS, W, SS, 3).sum((1, 3)) / np.maximum(cov.reshape(H, SS, W, SS).sum((1, 3)), 1)[..., None]
    alpha = c >= 0.4
    q = Image.fromarray(np.where(alpha[..., None], rgb, rgb[alpha].mean(0)).astype(np.uint8), 'RGB').quantize(255, method=Image.MEDIANCUT, dither=Image.NONE)
    idx = np.asarray(q, np.uint8).astype(int) + 1; idx[~alpha] = 0
    p = q.getpalette()[:765]; p = [255, 0, 255] + p + [0] * (765 - len(p))
    pal = [rgb15(c) for c in np.array(p).reshape(256, 3)]
    # (a preview: python3 ds_impostor.py ... --preview)
    if "--preview" in sys.argv:     Image.fromarray(np.where(alpha[..., None], rgb, 255).astype(np.uint8)).resize((W * 4, H * 4), Image.NEAREST).save(out + '.png')
    with open(out, 'wb') as f:
        f.write(struct.pack('<4sHH4h', b'SSXI', W, H, int(x0), int(x1), int(y0), int(y1)))
        f.write(struct.pack('<256H', *pal)); f.write(idx.astype(np.uint8).tobytes())
    print('impostor', W, H, 'extent x', x0, x1, 'y', y0, y1, 'opaque', alpha.mean())

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

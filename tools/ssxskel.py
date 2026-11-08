"""SSX 3 (Gizmondo) skeleton (RPBN in skinned .xng) + .xga animation reader and CPU skinning."""
import struct, numpy as np

def read_rpbn(m):
    t = m['tail']; nv = m['nv']
    bone_of_vert = np.frombuffer(t, np.uint8, nv).copy()
    r = t[nv:]; assert r[:4] == b'RPBN'
    n = struct.unpack_from('<I', r, 4)[0]
    recs = [struct.unpack_from('<iiiBBH', r, 8 + 16 * i) for i in range(n)]
    # left-child / right-sibling tree: byte@12 = next sibling, byte@13 = first child
    sib = [x[3] if x[3] != 255 else -1 for x in recs]
    child = [x[4] if x[4] != 255 else -1 for x in recs]
    parent = [-1] * n
    for p in range(n):
        c = child[p]
        while c != -1:
            parent[c] = p; c = sib[c]
    return dict(bone_of_vert=bone_of_vert, nbones=n, child=child, sib=sib, parent=parent,
                vcount=[x[2] for x in recs], rest=r[8 + 16 * n:])

def read_xga(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'XNGA'
    nfr = d[8]; nfr16 = struct.unpack_from('<H', d, 9)[0]
    assert d[11:15] == b'RPBR'
    u16, u32 = struct.unpack_from('<HI', d, 15)
    nb = d[21]
    off = 22
    frames = (len(d) - off) // (nb * 28)
    a = np.frombuffer(d, '<i4', frames * nb * 7, off).reshape(frames, nb, 7).astype(np.float64)
    q = a[..., 0:4] / 16384.0        # w x y z ?
    tr = a[..., 4:7]
    return dict(nframes=frames, nbones=nb, q=q, t=tr, hdr=(nfr, nfr16, u16, u32))

def qmat(q, order='wxyz'):
    if order == 'wxyz': w, x, y, z = q
    else: x, y, z, w = q
    n = np.sqrt(w*w + x*x + y*y + z*z) or 1; w, x, y, z = w/n, x/n, y/n, z/n
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])

def world(skel, anim, frame=0, order='wxyz', conj=False):
    n = skel['nbones']; W = [None] * n
    def get(i):
        if W[i] is not None: return W[i]
        q = anim['q'][frame, i].copy()
        if conj: q[1:] *= -1
        R = qmat(q, order); T = anim['t'][frame, i]
        M = np.eye(4); M[:3, :3] = R; M[:3, 3] = T
        p = skel['parent'][i]
        W[i] = (get(p) @ M) if p >= 0 else M
        return W[i]
    return [get(i) for i in range(n)]

def skin(m, skel, Ws):
    V = m['verts']; out = np.zeros_like(V)
    for i, b in enumerate(skel['bone_of_vert']):
        M = Ws[b]; out[i] = M[:3, :3] @ V[i] + M[:3, 3]
    return out

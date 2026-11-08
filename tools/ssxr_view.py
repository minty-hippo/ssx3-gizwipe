"""Validate riders.ssxr + anims.ssxa: replay display lists with a simulated GX matrix stack."""
import struct, sys, os, numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssxt_view import NPARAM
NPARAM[0x14] = 1

def load_anims(p):
    d = open(p, 'rb').read(); _, v, nb, na = struct.unpack_from('<4s3I', d, 0)
    parent = struct.unpack_from('<32b', d, 16); A = {}
    for i in range(na):
        name, nf, hdr, off = struct.unpack_from('<24sH6sI', d, 48 + 36 * i)
        A[name.split(b'\0')[0].decode()] = np.frombuffer(d, '<i2', nf * nb * 7, off).reshape(nf, nb, 7).astype(float)
    return parent, A

def pose(parent, fr):
    W = [None] * 32
    def g(i):
        if W[i] is None:
            w, x, y, z = fr[i, :4] / 16384; n = np.sqrt(w*w+x*x+y*y+z*z); w, x, y, z = w/n, x/n, y/n, z/n
            M = np.eye(4); M[:3, :3] = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
            M[:3, 3] = fr[i, 4:]
            W[i] = g(parent[i]) @ M if parent[i] >= 0 else M
        return W[i]
    return [g(i) for i in range(32)]

def load_riders(p):
    d = open(p, 'rb').read(); _, v, nm, nt, mo, to = struct.unpack_from('<4s5I', d, 0)
    meshes = {}
    for i in range(nm):
        name, ti, sk, ns, sb, npoly, _, off = struct.unpack_from('<16shBB24sHHI', d, mo + struct.calcsize('<16shBB24sHHI') * i)
        meshes[name.split(b'\0')[0].decode()] = dict(tex=ti, skinned=sk, slots=list(sb[:ns]), off=off)
    texs = []
    for i in range(nt):
        name, w, h, fmt, fl, _, ot, op = struct.unpack_from('<24sHHBBHII', d, to + 40 * i)
        pal = np.frombuffer(d, '<u2', 256, op).astype(int)
        rgb = np.stack([pal & 31, (pal >> 5) & 31, (pal >> 10) & 31], 1) * 255 // 31
        texs.append(rgb[np.frombuffer(d, np.uint8, w * h, ot)].reshape(h, w, 3).astype(np.uint8))
    return d, meshes, texs

def replay(d, mesh, W):
    """returns list of (positions[k,3], texcoords[k,2], normals) per primitive (tris/quads)"""
    n = struct.unpack_from('<I', d, mesh['off'])[0]; w = struct.unpack_from(f'<{n}I', d, mesh['off'] + 4)
    stack = {s + 1: W[b] for s, b in enumerate(mesh['slots'])}; M = np.eye(4)
    i = 0; prims = []; cur = None; tc = (0, 0); sx = lambda a: a - 65536 if a & 0x8000 else a
    while i < n:
        hdr = w[i]; i += 1
        for k in range(4):
            c = (hdr >> (8 * k)) & 0xFF
            if not c: continue
            p = w[i:i + NPARAM[c]]; i += NPARAM[c]
            if c == 0x14: M = stack[p[0]]
            elif c == 0x40: cur = (p[0], [])
            elif c == 0x41: prims.append(cur)
            elif c == 0x22: tc = (sx(p[0] & 0xFFFF) / 16, sx(p[0] >> 16) / 16)
            elif c == 0x23:
                v = np.array([sx(p[0] & 0xFFFF), sx(p[0] >> 16), sx(p[1] & 0xFFFF), 16.0]) / 16
                cur[1].append(((M @ v)[:3], tc))
    out = []
    for prim, vs in prims:
        k = 3 if prim == 0 else 4
        out += [vs[j:j + k] for j in range(0, len(vs), k)]
    return out

if __name__ == '__main__':
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from ssxmesh import render
    parent, A = load_anims(sys.argv[1]); d, meshes, texs = load_riders(sys.argv[2])
    tiles = []
    for name, anim, fr in [('elise_low', 'a_switch', 0), ('moby_high', 'a_spin_fs_cyc', 3), ('zoe_high', 'a_flip_fwd_cyc', 2), ('psymon_low', 'a_rodeo_bs_cyc', 1), ('mac_high', 'a_misty_fs_cyc', 2)]:
        W = pose(parent, A[anim][fr]); prims = replay(d, meshes[name], W)
        V = []; F = []
        for pr in prims:
            F.append(dict(idx=list(range(len(V), len(V) + len(pr))), uv=[t for _, t in pr], color=-1)); V += [p for p, _ in pr]
        tex = texs[meshes[name]['tex']]; h, w_ = tex.shape[:2]
        for f in F: f['uv'] = [(u / w_, v / h) for u, v in f['uv']]
        V = np.array(V, np.float32)
        up = W[20][:3, 3] - W[25][:3, 3]; up /= np.linalg.norm(up)
        y = np.array([0, 1., 0]); v = np.cross(up, y); c = up @ y
        K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]]); R = np.eye(3) + K + K @ K / (1 + c)
        tiles.append(render(dict(verts=V @ R.T, faces=F), tex, size=260, yaw=200, pitch=10))
    Image.fromarray(np.concatenate(tiles, 1)).save(sys.argv[3])

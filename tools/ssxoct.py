"""SSX 3 (Gizmondo) slope.oct reader (full structural parse, matches loader at 0x82CC4 / 0x822C4).

  'OCT1'
  u32 version              0x01010505
  u32 nverts
  i32 xyz[nverts]          Y-up integer units (loader shifts to 16.16)
  u32 nfaces, u32 nverts2?, u32 nnodes
  quadtree, depth-first:
     node header 72 bytes: u32 nfaces | 2 x u32 | i32 bbmin[3] | i32 bbmax[3] | i32 centre[3] | u32 ? |
                           u32 child_ptr[4] (raw PC pointers; non-zero = child follows)
     nfaces x 60-byte face records:
        i16 idx[4] | i32 uv[4][2] (16.16) | i32 color (-1) | i32 material |
        u32 flags (0x10 = triangle, hi16 = shade level) | i32 0 | i16 ?, i16 ?
     then the children
  u32 nmarkers, nmarkers x 92 bytes: char name[64] | i32 pos[3] | i32 quat[4] (2.14)
  u32 nsplines, each: char name[32] | u32 n | i32 xyz[n]   ('default' = race line, r_railspline* = grind rails)
  u8 nmaterials, nmaterials x 64 bytes: 'name.xtf\\0' + render state
  u8 rgba[nverts]          per-vertex baked lighting colour
  (end of file)
"""
import struct, sys
import numpy as np

def read_oct(path, keep_nodes=False):
    d = open(path, 'rb').read()
    assert d[:4] == b'OCT1'
    nv = struct.unpack_from('<I', d, 8)[0]
    verts = np.frombuffer(d, '<i4', nv * 3, 12).reshape(nv, 3).astype(np.float32)
    o = 12 + nv * 12
    nf_hdr, nv2, nnodes = struct.unpack_from('<3I', d, o); o += 12
    faces, nodes = [], []

    def node(o, depth, parent):
        h = struct.unpack_from('<18i', d, o)
        cnt = h[0] & 0xFFFF
        me = len(nodes)
        nodes.append(dict(depth=depth, parent=parent, bbmin=h[3:6], bbmax=h[6:9], centre=h[9:12],
                          first_face=len(faces), nfaces=cnt))
        o += 72
        for _ in range(cnt):
            r = struct.unpack_from('<4h8iiiIi2h', d, o); o += 60
            fl = r[14]; n = 3 if (fl & 0x10) else 4
            faces.append(dict(idx=list(r[0:n]),
                              uv=[(r[4 + 2*k] / 65536.0, r[5 + 2*k] / 65536.0) for k in range(n)],
                              color=r[12], tex=r[13], flags=fl, shade=fl >> 16, node=me))
        for c in h[13:17]:
            if c: o = node(o, depth + 1, me)
        return o

    o = node(o, 0, -1)
    nmk = struct.unpack_from('<I', d, o)[0]; o += 4
    markers = []
    for _ in range(nmk):
        name = d[o:o + 64].split(b'\0')[0].decode('latin1')
        pos = struct.unpack_from('<3i', d, o + 64); q = struct.unpack_from('<4i', d, o + 76)
        markers.append(dict(name=name, pos=pos, quat=tuple(x / 16384 for x in q)))
        o += 92
    nspl = struct.unpack_from('<I', d, o)[0]; o += 4
    splines = []
    for _ in range(nspl):
        name = d[o:o + 32].split(b'\0')[0].decode('latin1'); o += 32
        n = struct.unpack_from('<I', d, o)[0]; o += 4
        pts = np.frombuffer(d, '<i4', n * 3, o).reshape(n, 3).copy(); o += n * 12
        splines.append(dict(name=name, pts=pts))
    nmat = d[o]; o += 1
    mats = []
    for _ in range(nmat):
        mats.append(d[o:o + 64].split(b'\0')[0].decode('latin1')); o += 64
    # vertex colours are B, G, R, A like the textures (the vertices by glare_orange lights are high in byte 2)
    vcol = np.frombuffer(d, np.uint8, nv * 4, o).reshape(nv, 4)[:, [2, 1, 0, 3]].copy(); o += nv * 4
    assert o == len(d), (o, len(d))
    assert len(faces) == nf_hdr and len(nodes) == nnodes, (len(faces), nf_hdr, len(nodes), nnodes)
    return dict(verts=verts, faces=faces, nv=nv, nf=len(faces), nf_header=nf_hdr, materials=mats,
                markers=markers, splines=splines, nodes=nodes if keep_nodes else None, nnodes=len(nodes),
                vcolors=vcol, flags=0, tail=b'')

if __name__ == '__main__':
    import collections
    m = read_oct(sys.argv[1])
    print('verts', m['nv'], 'faces', m['nf'], 'nodes', m['nnodes'], 'materials', m['materials'])
    print('markers', collections.Counter(k['name'] for k in m['markers']).most_common(20))
    print('splines', [(sp['name'], len(sp['pts'])) for sp in m['splines']][:12])
    print('vertex colour range', m['vcolors'].min(0), m['vcolors'].max(0))

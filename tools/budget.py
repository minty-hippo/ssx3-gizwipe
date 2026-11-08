"""Per-frame polygon budget estimate for a converted .ssxt (before hardware frustum culling).
LOD is chosen per segment from race-line distance between the rider and the segment."""
import struct, sys, numpy as np
# (max distance ahead, lod); segments behind the rider within BEHIND get the last LOD; beyond VIEW: not drawn
LOD_DIST = [(1, 0), (3500, 1), (9000, 2), (24000, 3)]   # LOD0 only for the segment the rider is in
BEHIND = 2500

def seg_table(path):
    d = open(path, 'rb').read()
    _, ver, nseg, nmat, ntex, nlod = struct.unpack_from('<4s5I', d, 0)
    seg_off = struct.unpack_from('<I', d, 4 * 10)[0]
    rec = 12 + 12 + 8 + 4 + 8 + nlod * 12
    s01 = np.array([struct.unpack_from('<ii', d, seg_off + i * rec + 24) for i in range(nseg)])
    st = np.array([[struct.unpack_from('<H', d, seg_off + i * rec + 44 + l * 12)[0] for l in range(nlod)] for i in range(nseg)])
    return s01, st

def lod_for(s, s0, s1, nlod):
    if s1 < s - BEHIND: return None
    if s1 < s: return nlod - 1
    d = max(0, s0 - s)
    for lim, l in LOD_DIST:
        if d < lim: return min(l, nlod - 1)
    return None

def frame(s01, st, s):
    tot = 0
    for (s0, s1), row in zip(s01, st):
        l = lod_for(s, s0, s1, len(row))
        if l is not None: tot += row[l]
    return tot

def report(path, step=500):
    s01, st = seg_table(path)
    w = [frame(s01, st, s) for s in range(0, int(s01[-1, 1]), step)]
    return st, w

if __name__ == '__main__':
    for p in sys.argv[1:]:
        st, w = report(p)
        print(f'{p}: {len(st)} segs, LOD totals {st.sum(0)}, per-frame max {max(w)} mean {int(np.mean(w))} p95 {int(np.percentile(w, 95))}')

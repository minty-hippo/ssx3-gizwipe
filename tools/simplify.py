"""Small quadric-error-metric triangle simplifier with per-corner UV carry-over.

Collapses edges u<-v (v merges into u, u keeps its position). Locked vertices never move.
Each face carries its own corner UVs; when a face loses corner v and gains u, the new corner UV is
taken from that face's own affine position->UV map evaluated at u (works because SSX UVs are
locally planar projections).
"""
import heapq
import numpy as np

def _plane(p0, p1, p2):
    n = np.cross(p1 - p0, p2 - p0); l = np.linalg.norm(n)
    if l < 1e-9: return None, 0.0
    n /= l; d = -n @ p0
    return np.array([n[0], n[1], n[2], d]), l * 0.5

def _affine_uv(P, UV):
    """least-squares affine map pos->uv in the triangle's plane: returns f(x)."""
    p0 = P[0]; e1 = P[1] - p0; e2 = P[2] - p0
    G = np.array([[e1 @ e1, e1 @ e2], [e1 @ e2, e2 @ e2]])
    if abs(np.linalg.det(G)) < 1e-9: return lambda x: UV[0]
    Gi = np.linalg.inv(G); du1 = UV[1] - UV[0]; du2 = UV[2] - UV[0]
    def f(x):
        r = x - p0; a, b = Gi @ np.array([r @ e1, r @ e2])
        return UV[0] + a * du1 + b * du2
    return f

def simplify(V, tris, uvs, mats, locked, target, seam_cos=-0.94):
    """V (n,3) float; tris list of [a,b,c]; uvs list of 3x2 arrays; mats list; locked bool array.
    Returns (tris, uvs, mats) with about `target` triangles."""
    V = np.asarray(V, float)
    tris = [list(t) for t in tris]; uvs = [np.array(u, float) for u in uvs]; mats = list(mats)
    alive = [True] * len(tris)
    vf = [set() for _ in range(len(V))]
    for i, t in enumerate(tris):
        for a in t: vf[a].add(i)
    Q = np.zeros((len(V), 4, 4))
    for t in tris:
        pl, area = _plane(V[t[0]], V[t[1]], V[t[2]])
        if pl is None: continue
        K = np.outer(pl, pl) * area
        for a in t: Q[a] += K
    # boundary edges & material seams -> lock their vertices (keeps segment/material borders crack-free)
    edge_faces = {}
    for i, t in enumerate(tris):
        for k in range(3):
            e = tuple(sorted((t[k], t[(k + 1) % 3])))
            edge_faces.setdefault(e, []).append(i)
    hard = np.array(locked, bool).copy()          # segment-shared: never move
    seam_nb = [[] for _ in range(len(V))]         # seam/boundary neighbours per vertex
    seam_edges = set()
    for e, fs in edge_faces.items():
        if len(fs) != 2 or mats[fs[0]] != mats[fs[1]]:
            seam_edges.add(e); seam_nb[e[0]].append(e[1]); seam_nb[e[1]].append(e[0])
    seam_v = np.array([len(x) > 0 for x in seam_nb])
    def can_move(u, v):
        if hard[v]: return False
        if not seam_v[v] or seam_cos > 1: return True   # seam_cos > 1: free mode (far LOD, hidden in fog)
        # seam vertex: may only slide along its own (nearly straight) seam onto a seam neighbour
        nb = seam_nb[v]
        if len(nb) != 2 or u not in nb: return False
        a = V[nb[0]] - V[v]; b = V[nb[1]] - V[v]
        return (a @ b) < seam_cos * np.linalg.norm(a) * np.linalg.norm(b)   # default: within ~20 deg of straight
    locked = hard
    def cost(u, v):  # move v onto u
        x = np.append(V[u], 1.0)
        return x @ (Q[u] + Q[v]) @ x
    heap = []
    for (a, b) in edge_faces:
        if can_move(a, b): heapq.heappush(heap, (cost(a, b), a, b))
        if can_move(b, a): heapq.heappush(heap, (cost(b, a), b, a))
    nalive = sum(alive); merged = {}
    def find(x):
        while x in merged: x = merged[x]
        return x
    while nalive > target and heap:
        c, u, v = heapq.heappop(heap)
        if v in merged or u in merged or u == v or not can_move(u, v): continue
        if abs(c - cost(u, v)) > 1e-6 * max(1.0, abs(c)): continue  # stale
        # reject flips: faces around v (not containing u) must keep normal direction
        ok = True; newuv = {}
        for fi in vf[v]:
            t = tris[fi]
            if u in t: continue
            P = V[t].copy(); n0 = np.cross(P[1] - P[0], P[2] - P[0])
            P[t.index(v)] = V[u]; n1 = np.cross(P[1] - P[0], P[2] - P[0])
            if n0 @ n1 <= 0.2 * np.linalg.norm(n0) * np.linalg.norm(n1): ok = False; break
            newuv[fi] = _affine_uv(V[t], uvs[fi])(V[u])
        if not ok: continue
        for fi in list(vf[v]):
            t = tris[fi]
            if u in t:
                alive[fi] = False; nalive -= 1
                for a in t: vf[a].discard(fi)
            else:
                k = t.index(v); t[k] = u; uvs[fi][k] = newuv[fi]; vf[u].add(fi)
        vf[v].clear(); merged[v] = u; Q[u] += Q[v]
        if seam_v[v]:   # splice v out of the seam chain
            for w in seam_nb[v]:
                if w != u:
                    seam_nb[w] = [u if x == v else x for x in seam_nb[w]]
                    seam_nb[u] = [w if x == v else x for x in seam_nb[u]]
            seam_nb[u] = list(dict.fromkeys(x for x in seam_nb[u] if x != u))
        nbrs = {a for fi in vf[u] for a in tris[fi] if a != u}
        for w in nbrs:
            if can_move(u, w): heapq.heappush(heap, (cost(u, w), u, w))
            if can_move(w, u): heapq.heappush(heap, (cost(w, u), w, u))
    keep = [i for i in range(len(tris)) if alive[i]]
    return [tris[i] for i in keep], [uvs[i] for i in keep], [mats[i] for i in keep]

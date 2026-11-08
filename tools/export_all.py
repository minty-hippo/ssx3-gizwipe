"""Export SSX 3 (Gizmondo) assets to PNG / OBJ.

usage: python3 export_all.py <extracted-data-dir> <out-dir>
  data dir holds the unzipped .pak folders: tracks/ characters/ sprites/ bitmaps/ fonts/
"""
import os, sys, glob, re, shutil
import numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ssxtex import read_xtf, read_xgt
from ssxmesh import read_xng
from ssxoct import read_oct
from ssxskel import read_rpbn, read_xga, world, skin

def save_png(rgba, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    Image.fromarray(rgba, 'RGBA').save(path)

def write_obj_multi(path, verts, faces, matnames, scale=1.0):
    """faces: list of dict(idx, uv, mat). matnames: mat id -> png filename (or None)."""
    base = os.path.splitext(path)[0]
    os.makedirs(os.path.dirname(path), exist_ok=True)
    used = sorted({f['mat'] for f in faces}, key=str)
    with open(base + '.mtl', 'w') as f:
        for m in used:
            if isinstance(m, str) and m.startswith('c'):   # flat RGB444 colour material
                c = int(m[1:]); kd = (((c >> 8) & 15) / 15, ((c >> 4) & 15) / 15, (c & 15) / 15)
                f.write(f'newmtl m{m}\nKd %.3f %.3f %.3f\n' % kd)
            else:
                f.write(f'newmtl m{m}\nKd 1 1 1\n')
            if matnames.get(m): f.write(f'map_Kd {matnames[m]}\n')
            f.write('\n')
    with open(path, 'w') as f:
        f.write(f'mtllib {os.path.basename(base)}.mtl\n')
        for v in verts: f.write('v %g %g %g\n' % tuple(v * scale))
        t = 1
        for m in used:
            f.write(f'usemtl m{m}\n')
            for fc in faces:
                if fc['mat'] != m: continue
                for u, v in fc['uv']: f.write('vt %g %g\n' % (u, 1 - v))
                f.write('f ' + ' '.join(f'{i+1}/{t+k}' for k, i in enumerate(fc['idx'])) + '\n')
                t += len(fc['idx'])

def mesh_texname(m):
    mm = re.search(rb'([A-Za-z0-9_]+\.xgt)\0', m['tail'])
    return mm.group(1).decode() if mm else None

def upright(V, W, head=20, pelvis=25):
    up = W[head][:3, 3] - W[pelvis][:3, 3]; up /= np.linalg.norm(up)
    y = np.array([0, 1.0, 0]); v = np.cross(up, y); c = up @ y
    K = np.array([[0, -v[2], v[1]], [v[2], 0, -v[0]], [-v[1], v[0], 0]])
    R = np.eye(3) + K + K @ K / (1 + c)
    return V @ R.T

def main(data, out):
    lower = lambda d: {f.lower(): f for f in os.listdir(d)}
    report = []
    # --- tracks ---
    for oct_path in sorted(glob.glob(f'{data}/tracks/*/slope.oct')):
        tdir = os.path.dirname(oct_path); name = os.path.basename(tdir)
        od = f'{out}/tracks/{name}'; os.makedirs(od, exist_ok=True)
        files = lower(tdir)
        for x in glob.glob(f'{tdir}/*.xtf'):
            save_png(read_xtf(x)[0], f'{od}/{os.path.basename(x)[:-4]}.png')
        m = read_oct(oct_path)
        matnames = {}
        for i, n in enumerate(m['materials']):
            if n and n.lower() in files: matnames[i] = n[:-4] + '.png'
        faces = [dict(idx=f['idx'], uv=f['uv'], mat=f['tex']) for f in m['faces']]
        # track data is already Y-up (area-weighted face normals point +Y); scale to ~metres (1/100)
        V = m['verts']
        write_obj_multi(f'{od}/{name}.obj', V, faces, matnames, 0.01)
        report.append(f'track {name}: {m["nv"]} verts, {m["nf"]}/{m["nf_header"]} faces, {len(matnames)} textures')
    for x in glob.glob(f'{data}/tracks/Sky/*.bmp'):
        os.makedirs(f'{out}/tracks/Sky', exist_ok=True)
        Image.open(x).convert('RGB').save(f'{out}/tracks/Sky/{os.path.basename(x)[:-4]}.png')
    # --- character textures ---
    cdir = f'{data}/characters'; cfiles = lower(cdir)
    for x in glob.glob(f'{cdir}/*.xgt'):
        save_png(read_xgt(x)[0], f'{out}/characters/textures/{os.path.basename(x)[:-4]}.png')
    # --- riders (skinned) posed with an animation frame ---
    anim = read_xga(f'{cdir}/a_switch.xga')
    for x in sorted(glob.glob(f'{cdir}/*.xng')):
        m = read_xng(x); nm = os.path.basename(x)[:-4]
        tex = mesh_texname(m)
        if m['flags'] & 32:
            s = read_rpbn(m); W = world(s, anim, 0, 'wxyz', True)
            V = upright(skin(m, s, W), W)
            kind = 'rider'
        else:
            V = m['verts']; kind = 'static'
        faces = [dict(idx=f['idx'], uv=f['uv'], mat=0) for f in m['faces']]
        tpng = None
        if tex and tex.lower() in cfiles: tpng = f'textures/{cfiles[tex.lower()][:-4]}.png'
        write_obj_multi(f'{out}/characters/{nm}.obj', V, faces, {0: tpng}, 0.01)
        report.append(f'{kind} {nm}: {m["nv"]} verts, {m["nf"]} faces, texture {tex}')
    # --- sprites / pickups ---
    for x in sorted(glob.glob(f'{data}/sprites/*.xng')):
        m = read_xng(x); nm = os.path.basename(x)[:-4]
        faces = [dict(idx=f['idx'], uv=f['uv'], mat=(0 if f['color'] == -1 else f'c{f["color"]}')) for f in m['faces']]
        write_obj_multi(f'{out}/sprites/{nm}.obj', m['verts'], faces, {}, 0.01)
    for x in glob.glob(f'{data}/sprites/*.xgt'):
        try: save_png(read_xgt(x)[0], f'{out}/sprites/{os.path.basename(x)[:-4]}.png')
        except ValueError as e: report.append(f'skip {x}: {e}')
    # --- frontend bitmaps & fonts ---
    for grp in ('bitmaps', 'fonts'):
        for x in glob.glob(f'{data}/{grp}/*.bmp'):
            os.makedirs(f'{out}/{grp}', exist_ok=True)
            Image.open(x).save(f'{out}/{grp}/{os.path.basename(x)[:-4]}.png')
        for x in glob.glob(f'{data}/{grp}/*.fnt'):
            shutil.copy(x, f'{out}/{grp}/')
    open(f'{out}/export_report.txt', 'w').write('\n'.join(report) + '\n')
    print('\n'.join(report))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])

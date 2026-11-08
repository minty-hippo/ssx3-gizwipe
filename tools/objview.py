"""Minimal OBJ+MTL perspective software renderer (validation / previews)."""
import numpy as np, os, sys
from PIL import Image

def load_obj(path):
    d = os.path.dirname(path); V=[]; VT=[]; F=[]; mat=None; mtl={}
    for line in open(path):
        p = line.split()
        if not p: continue
        if p[0]=='mtllib':
            cur=None
            for l in open(os.path.join(d,p[1])):
                q=l.split()
                if not q: continue
                if q[0]=='newmtl': cur=q[1]; mtl[cur]={'kd':(1,1,1),'tex':None}
                elif q[0]=='Kd': mtl[cur]['kd']=tuple(map(float,q[1:4]))
                elif q[0]=='map_Kd': mtl[cur]['tex']=np.asarray(Image.open(os.path.join(d,q[1])).convert('RGB'))
        elif p[0]=='v': V.append(list(map(float,p[1:4])))
        elif p[0]=='vt': VT.append(list(map(float,p[1:3])))
        elif p[0]=='usemtl': mat=p[1]
        elif p[0]=='f':
            ids=[int(x.split('/')[0])-1 for x in p[1:]]; ts=[int(x.split('/')[1])-1 for x in p[1:]]
            F.append((ids,ts,mat))
    return np.array(V),np.array(VT),F,mtl

def look_at(eye, target, up=(0,1,0)):
    f=np.array(target,float)-eye; f/=np.linalg.norm(f)
    r=np.cross(f,up); r/=np.linalg.norm(r); u=np.cross(r,f)
    return np.stack([r,u,-f])

def render(V,VT,F,mtl,eye,target,W=512,H=384,fov=60,near=0.5):
    R=look_at(np.array(eye,float),target); C=(V-eye)@R.T
    fl=(H/2)/np.tan(np.radians(fov)/2)
    img=np.zeros((H,W,3),np.uint8); img[:]=(150,180,215); zb=np.full((H,W),np.inf)
    for ids,ts,m in F:
        for t in ([0,1,2],[0,2,3])[:len(ids)-2]:
            c=C[[ids[k] for k in t]]
            if (c[:,2]>-near).any(): continue
            z=-c[:,2]; sx=W/2+fl*c[:,0]/z; sy=H/2-fl*c[:,1]/z
            x0,x1=int(max(0,np.floor(sx.min()))),int(min(W-1,np.ceil(sx.max())))
            y0,y1=int(max(0,np.floor(sy.min()))),int(min(H-1,np.ceil(sy.max())))
            if x1<x0 or y1<y0: continue
            den=(sx[1]-sx[0])*(sy[2]-sy[0])-(sx[2]-sx[0])*(sy[1]-sy[0])
            if abs(den)<1e-9: continue
            ys,xs=np.mgrid[y0:y1+1,x0:x1+1]+0.5
            l1=((xs-sx[0])*(sy[2]-sy[0])-(sx[2]-sx[0])*(ys-sy[0]))/den
            l2=((sx[1]-sx[0])*(ys-sy[0])-(xs-sx[0])*(sy[1]-sy[0]))/den
            l0=1-l1-l2; msk=(l0>=0)&(l1>=0)&(l2>=0)
            if not msk.any(): continue
            iz=l0/z[0]+l1/z[1]+l2/z[2]; dep=1/iz
            yy=(ys[msk]-0.5).astype(int); xx=(xs[msk]-0.5).astype(int); dd=dep[msk]
            ok=dd<zb[yy,xx]; yy,xx,dd=yy[ok],xx[ok],dd[ok]
            if not len(yy): continue
            mm=mtl.get(m,{'kd':(1,1,1),'tex':None}); w=c[:,:]
            n=np.cross(w[1]-w[0],w[2]-w[0]); n/=np.linalg.norm(n)+1e-9
            shade=0.55+0.45*abs(n@np.array([0.3,0.8,0.5])/np.linalg.norm([0.3,0.8,0.5]))
            if mm['tex'] is not None:
                uv=VT[[ts[k] for k in t]]
                L=np.stack([l0[msk][ok]/z[0],l1[msk][ok]/z[1],l2[msk][ok]/z[2]],1)*dd[:,None]
                u=(L@uv[:,0])%1; v=(1-(L@uv[:,1]))%1
                T=mm['tex']; col=T[(v*T.shape[0]).astype(int)%T.shape[0],(u*T.shape[1]).astype(int)%T.shape[1]].astype(float)
            else: col=np.array(mm['kd'])*255
            img[yy,xx]=np.clip(col*shade,0,255).astype(np.uint8); zb[yy,xx]=dd
    return img

"""Reconstruct the photographed Chula woodland with calibrated multi-view plane sweeps.

Arguments are the capture directory, solved camera JSON, and output directory. Two reference views
look past opposite parts of the canopy. Frames 2059 and 2062 are withheld from depth estimation.
"""
import argparse
import json
import struct
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from scipy.ndimage import distance_transform_edt, gaussian_filter, map_coordinates, median_filter, uniform_filter
import camera_fit as camera

# Manual limits on the far bank stop before the guardrail and beneath the camera-window corners.
BANDS = {
    2057:(616,910,177,204), 2058:(604,915,175,203), 2059:(597,946,172,210),
    2060:(588,944,176,209), 2061:(575,885,176,201), 2062:(565,823,176,195),
    2063:(560,789,177,191), 2064:(558,756,177,189)
}
# Foreground signals and their hanging signs are masked explicitly. Their moving projection differs
# from the woodland behind them. The 2058 texture view is clear of the crossing and vehicle.
OCCLUDERS = {
    2059:[[(711,101),(955,136),(956,145),(875,144),(708,114)],[(876,130),(888,130),(889,216),(874,216)],
          [(558,165),(695,165),(695,207),(558,207)],[(747,100),(759,100),(759,130),(747,130)],
          [(787,105),(805,105),(805,134),(787,134)],[(826,112),(842,112),(842,139),(826,139)],
          [(928,125),(936,125),(937,217),(927,217)]],
    2060:[[(615,127),(803,132),(804,151),(620,150)],[(740,140),(751,140),(751,199),(739,199)],
          [(754,134),(764,134),(764,200),(754,200)],[(776,138),(807,137),(808,160),(775,157)],
          [(795,142),(806,142),(806,200),(794,200)],[(641,69),(662,69),(662,86),(641,86)]],
    2061:[[(574,142),(698,140),(699,160),(574,159)],[(664,149),(676,149),(675,194),(664,194)],
          [(682,145),(696,145),(696,194),(681,194)],[(717,141),(725,141),(725,195),(717,195)]],
    2062:[[(559,150),(655,147),(655,166),(558,165)],[(625,155),(637,155),(638,194),(625,194)],
          [(637,145),(648,145),(649,192),(636,192)],[(675,145),(683,145),(683,196),(675,196)]],
    2063:[[(551,154),(626,150),(629,169),(551,167)],[(597,157),(610,157),(610,190),(596,190)],
          [(630,144),(644,144),(644,191),(630,191)],[(747,166),(761,166),(761,194),(747,194)]],
    2064:[[(547,155),(599,153),(600,168),(547,170)],[(581,158),(597,158),(597,190),(581,190)],
          [(617,149),(629,149),(629,190),(617,190)],[(674,165),(689,165),(689,191),(674,191)]]
}
CHECKS=(2059,2062)
def vegetation(image, frame):
    r,g,b=image.transpose(2,0,1)
    l,h,y0,y1=BANDS[frame]
    mask=Image.new('L',(960,540))
    pen=ImageDraw.Draw(mask)
    pen.polygon([(l,20),(h,20),(h,y1),(l,y0)],fill=255)
    for polygon in OCCLUDERS.get(frame,[]):pen.polygon(polygon,fill=0)
    return (np.asarray(mask)>0)&(g>b*.97)&(g>=r*.87)&(r<156)&(b<140)&(r+g+b>19)

def sample(im,x,y):
    return map_coordinates(im,[y,x],order=1,mode='constant',prefilter=False)

def feature(image):
    l=image@np.array([.27,.53,.20],np.float32)
    blur=gaussian_filter(l,2)
    wide=gaussian_filter(l,6)
    return np.stack((l, (l-blur)*2.2,(blur-wide)*1.9),axis=0)

def inverse_rays(p,frame,uv):
    center,angles,f,k=camera.variables(p,frame)
    obs=(uv-[480.,270.])/f
    radius=np.linalg.norm(obs,axis=-1)
    ru=radius.copy()
    for _ in range(15):
        ru-=np.clip((ru*(1+k*ru**2)-radius)/np.maximum(.055,1+3*k*ru**2),-.10,.10)
    xy=obs*(ru/np.maximum(.0001,radius))[...,None]
    valid=(np.abs(ru*(1+k*ru**2)-radius)<.0005)&(ru<1.23)
    rays=np.concatenate((xy,np.ones_like(xy[...,:1])),axis=-1)@camera.mat(*angles)
    return center,rays,valid

def project(p,pts,frame):
    shape=pts.shape[:-1]
    return camera.project(pts.reshape(-1,3),*camera.variables(p,frame)).reshape(*shape,2).transpose(2,0,1)

def geographic(xyz):
    x,y,z=xyz.transpose(2,0,1)
    terrain=map_coordinates(camera.height,[np.clip(z/camera.scale*64,0,64),np.clip(x/camera.scale*64,0,64)],order=1)
    # Footprint expanded a few meters around the crown overhang in the orthophoto.
    return (x>174+.20*np.maximum(0,z-149))&(x<367)&(z>140+.30*(x-180))&(z<301)&(y>terrain+1.0)&(y<terrain+28)

def sweeps(p,ims,feats,masks,ref,cloud):
    from woodland_matches import prior,tracked_depths
    # Half-resolution lattice. Each cost aggregates a local patch at the same world depth.
    left,right={2058:(604,912),2060:(590,915),2061:(575,880)}[ref]
    top,bottom=(24,206) if ref==2058 else (80,208)
    u,v=np.meshgrid(np.arange(left,right,2),np.arange(top,bottom,2))
    c,r,valid=inverse_rays(p,ref,np.stack([u,v],axis=-1))
    valid &= masks[ref][v,u]
    anchor=feats[ref][:,v,u]
    sparse,distance=prior(cloud,u,v)
    refview=[n for n in (2057,2058,2060,2061,2063,2064) if n!=ref]
    zs=np.arange(32.,212.,1.5)
    volume=[]; votes=[]
    for dep in zs:
        world=c+r*dep
        costs=[]
        for n in refview:
            qx,qy=project(p,world,n)
            visible=sample(masks[n].astype(np.float32),qx,qy)>.80
            delta=np.abs(anchor-np.stack([sample(ch,qx,qy) for ch in feats[n]]))
            raw=.33*np.minimum(delta[0],45.)+.34*np.minimum(delta[1],32.)+.33*np.minimum(delta[2],32.)
            support=uniform_filter(visible.astype(np.float32),size=5)
            cost=uniform_filter(raw*visible,size=5)/np.maximum(support,.01)
            costs.append(np.where((support>.64)&visible,cost,99.))
        ranked=np.sort(np.stack(costs,axis=0),axis=0)
        # At least two independent neighboring recordings must support a candidate.
        value=.43*ranked[0]+.57*ranked[1]
        ok=geographic(world)&valid
        volume.append(np.where(ok,value,99.))
        votes.append(np.sum(np.array(costs)<17.,axis=0))
    volume=np.asarray(volume,dtype=np.float32)
    # Tie the dense sweep to measured, mutually matched SIFT depths. Smooth nearby support
    # within each small patch; re-evaluate photographic consistency at the smoothed depth.
    regularized=volume+.31*np.abs(zs[:,None,None]-sparse[None,...])
    ids=np.argmin(regularized,axis=0)
    dense=np.where(valid,zs[ids],sparse)
    z=median_filter(dense,size=5)
    depth=.8*z+.2*gaussian_filter(z,.85)
    ids=np.clip(np.rint((depth-zs[0])/(zs[1]-zs[0])).astype(int),0,len(zs)-1)
    best=np.take_along_axis(volume,ids[None],axis=0)[0]
    matches=np.take_along_axis(np.stack(votes),ids[None],axis=0)[0]
    ok=valid&(best<16.)&(matches>=2)&(distance<63)&(abs(depth-sparse)<28)&geographic(c+r*depth[...,None])
    tracked,views=tracked_depths(p,ims,masks,ref,refview,u,v,sparse)
    optical=(views>=2)&valid&np.isfinite(tracked)&(abs(tracked-sparse)<36)
    # Use only local, photo-confirmed anchor depths; never close a hole in the visibility mask.
    if optical.any():
        ids=distance_transform_edt(~optical,return_distances=False,return_indices=True)
        local=median_filter(tracked[tuple(ids)],size=5)
        optical &= (abs(tracked-local)<np.maximum(4.,local*.065))
        depth=np.where(optical,.70*local+.30*tracked,depth)
        ok |= optical
        ok &= geographic(c+r*depth[...,None])
    return u,v,c,r,depth,ok

def write_mesh(path,verts,normals,uv,indices):
    blob=bytearray();views=[];access=[]
    for vals,fmt,dim,kind in [(verts,'<f4','VEC3',5126),(normals,'<f4','VEC3',5126),(uv,'<f4','VEC2',5126),(indices,'<u4','SCALAR',5125)]:
        a=np.ascontiguousarray(vals,dtype=fmt)
        views.append(dict(buffer=0,byteOffset=len(blob),byteLength=a.nbytes,target=34963 if dim=='SCALAR' else 34962))
        ao=dict(bufferView=len(views)-1,count=len(a),componentType=kind,type=dim)
        if dim=='VEC3':ao.update(min=a.min(0).tolist(),max=a.max(0).tolist())
        access.append(ao);blob.extend(a.tobytes());blob.extend(bytes(-len(blob)%4))
    doc=dict(asset={'version':'2.0'},scenes=[{'nodes':[0]}],scene=0,nodes=[{'mesh':0}],meshes=[dict(primitives=[dict(attributes=dict(POSITION=0,NORMAL=1,TEXCOORD_0=2),indices=3,material=0)])],materials=[dict(doubleSided=True)],bufferViews=views,accessors=access,buffers=[dict(byteLength=len(blob))])
    js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*(-len(js)%4)
    path.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(js)+len(blob))+struct.pack('<I4s',len(js),b'JSON')+js+struct.pack('<I4s',len(blob),b'BIN\0')+blob)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('photos',type=Path);ap.add_argument('poses',type=Path);ap.add_argument('out',type=Path);ap.add_argument('--match-cache',type=Path);args=ap.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    p=json.loads(args.poses.read_text())
    imgs={n:Image.open(args.photos/f'{n}.jpg').convert('RGB') for n in camera.frames}
    ims={n:np.asarray(i.resize((960,540),Image.Resampling.LANCZOS),dtype=np.float32) for n,i in imgs.items()}
    masks={n:vegetation(im,n) for n,im in ims.items()};features={n:feature(im) for n,im in ims.items()}
    report={}
    from woodland_matches import feature_depths
    for ref in (2060,2061):
        cached=args.match_cache/f'sift-{ref}.npy' if args.match_cache else None
        if cached and cached.is_file():
            cloud=np.load(cached)
        else:
            cloud=feature_depths(p,imgs,masks,ref,[n for n in (2057,2058,2060,2061,2063,2064) if n!=ref])
        u,v,c,r,depth,ok=sweeps(p,ims,features,masks,ref,cloud)
        pts=c+r*depth[...,None]
        cells=np.arange(u.size).reshape(u.shape)
        tl,tr,bl,br=cells[:-1,:-1],cells[:-1,1:],cells[1:,:-1],cells[1:,1:]
        tris=np.concatenate((np.stack((tl,tr,bl),-1).reshape(-1,3),np.stack((bl,tr,br),-1).reshape(-1,3)))
        valid=ok.ravel()[tris].all(1)
        triang_depth=depth.ravel()[tris]
        valid &= (np.ptp(triang_depth,axis=1)<np.minimum(4.8,triang_depth.mean(1)*.050))
        world=pts.reshape(-1,3)
        a,b,d=world[tris[:,0]],world[tris[:,1]],world[tris[:,2]]
        edge=np.maximum.reduce((np.linalg.norm(a-b,axis=1),np.linalg.norm(a-d,axis=1),np.linalg.norm(b-d,axis=1)))
        valid &= (edge < np.minimum(2.65,triang_depth.mean(1)*.023))
        fn=np.cross(b-a,d-a);fn/=np.maximum(.00001,np.linalg.norm(fn,axis=1,keepdims=True))
        center=(a+b+d)/3
        view=c-center;view/=np.maximum(.00001,np.linalg.norm(view,axis=1,keepdims=True))
        front=(view*fn).sum(1)
        valid &= abs(front)>.16
        axis=np.where(front[:,None]<0,-fn,fn)
        # Reject grazing/sliver triangles that expose the unseen back of a crown when driving by.
        seen=np.zeros(len(tris),int)
        for n in (2057,2058,2060,2061,2063,2064):
            eye=camera.variables(p,n)[0]-center
            eye/=np.maximum(.00001,np.linalg.norm(eye,axis=1,keepdims=True))
            seen+=((axis*eye).sum(1)>.18)
        valid &= (seen>=4)
        tris[front<0]=tris[front<0][:,[0,2,1]]
        triangles=tris[valid]
        # Reject disconnected pieces too small to establish a coherent three-dimensional patch.
        from scipy.sparse import coo_matrix
        from scipy.sparse.csgraph import connected_components
        edges=np.vstack([triangles[:,[0,1]],triangles[:,[1,2]],triangles[:,[2,0]]])
        graph=coo_matrix((np.ones(len(edges)),edges.T),shape=(u.size,u.size))
        _,region=connected_components(graph,directed=False)
        tri_regions=region[triangles[:,0]]
        counts=np.bincount(tri_regions)
        triangles=triangles[counts[tri_regions]>=50]
        ids,ind=np.unique(triangles,return_inverse=True)
        vid=ind.reshape(-1,3);verts=pts.reshape(-1,3)[ids]
        normals=np.zeros_like(verts)
        fn=np.cross(verts[vid[:,1]]-verts[vid[:,0]],verts[vid[:,2]]-verts[vid[:,0]])
        for i in range(3):np.add.at(normals,vid[:,i],fn)
        normals/=np.maximum(1e-5,np.linalg.norm(normals,axis=1,keepdims=True))
        coords=np.stack((u.ravel()[ids],v.ravel()[ids]),axis=-1)
        lo=coords.min(axis=0)-2;hi=coords.max(axis=0)+3
        uv=(coords-lo+.25)/(hi-lo)
        write_mesh(args.out/f'scan-{ref}.meshdata',verts,normals,uv,vid.ravel())
        # Texture only supported mesh footprints so excluded source content cannot enter coarser mips.
        tex=imgs[ref].crop(tuple((np.r_[lo,hi]*2).tolist())).convert('RGBA')
        mask=Image.new('L',tex.size);draw=ImageDraw.Draw(mask)
        pixels=((coords-lo)*2).astype(int)
        for tri in vid:draw.polygon([tuple(x.tolist()) for x in pixels[tri]],fill=255)
        # The outer few texels taper away from unknown space. Clear unobserved RGB, including
        # source pixels masked for highway fixtures, before writing the distributable texture.
        footprint=np.asarray(mask)>0
        distance=distance_transform_edt(footprint)
        source_row=lo[1]+np.indices(footprint.shape)[0]*.5
        feather=5.+5.5*np.clip((148.-source_row)/27.,0,1)
        opacity=np.clip((distance-1.2)/feather,0,1)
        opacity=opacity*opacity*(3-2*opacity)
        rgba=np.array(tex)
        rgba[:,:,3]=np.round(opacity*255).astype('uint8')
        rgba[rgba[:,:,3]==0,:3]=0
        Image.fromarray(rgba).save(args.out/f'scan-{ref}.png')
        # Evaluate original reference radiance against unseen images and the same vertices assigned one median depth.
        detail={}
        for check in CHECKS:
            a=ims[ref][v.ravel()[ids],u.ravel()[ids]]@np.array([.25,.60,.15])
            observed=features[check][0]
            proj=camera.project(verts,*camera.variables(p,check))
            flat=camera.project(c+r.reshape(-1,3)[ids]*np.median(depth.ravel()[ids]),*camera.variables(p,check))
            both=(sample(masks[check].astype(float),*proj.T)>.9)&(sample(masks[check].astype(float),*flat.T)>.9)
            b=sample(observed,*proj.T);null=sample(observed,*flat.T)
            def score(arr):
                return {'mae':round(float(np.abs(a[both]-arr[both]).mean()),2),'correlation':round(float(np.corrcoef(a[both],arr[both])[0,1]),3)}
            detail[str(check)]={'n':int(both.sum()),'stereo':score(b),'constant_depth':score(null)}
        report[str(ref)]={'feature_triangulations':len(cloud),'vertices':len(ids),'triangles':len(vid),'depth_p05_p50_p95_m':np.quantile(depth.ravel()[ids],[.05,.5,.95]).round(2).tolist(),'bounds':[verts.min(0).round(2).tolist(),verts.max(0).round(2).tolist()],'holdout':detail}
    (args.out/'scan-report.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()

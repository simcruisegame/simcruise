"""Triangulate repeatable leaf/crown features to anchor dense stereo at the road edge."""
import cv2
import numpy as np
from scipy.spatial import cKDTree
import woodland_stereo as stereo

def feature_depths(p,images,masks,ref,train):
    sift=cv2.SIFT_create(nfeatures=12000,nOctaveLayers=4,contrastThreshold=.012,edgeThreshold=14,sigma=1.25)
    features={}
    for n in [ref,*train]:
        rgb=np.asarray(images[n].resize((1440,810)),dtype=np.uint8)
        gray=cv2.cvtColor(rgb,cv2.COLOR_RGB2GRAY)
        mask=cv2.resize(masks[n].astype(np.uint8)*255,(1440,810),interpolation=cv2.INTER_NEAREST)
        kp,descr=sift.detectAndCompute(gray,mask)
        features[n]=(np.array([k.pt for k in kp])/1.5,descr)
    anchor,da=features[ref]
    c,r,valid=stereo.inverse_rays(p,ref,anchor)
    rays=r/np.linalg.norm(r,axis=-1,keepdims=True)
    clouds=[]
    for n in train:
        target,dt=features[n]
        # Only unambiguous mutual matches establish a triangulation.
        match=cv2.BFMatcher(cv2.NORM_L2)
        pairs=match.knnMatch(da,dt,k=2)
        rev=match.match(dt,da)
        reverse={m.queryIdx:m.trainIdx for m in rev}
        matches=[a for a,b in pairs if a.distance<.78*b.distance and reverse.get(a.trainIdx)==a.queryIdx]
        ii=np.array([m.queryIdx for m in matches]);jj=np.array([m.trainIdx for m in matches])
        if not len(ii):continue
        tar,vr,ok=stereo.inverse_rays(p,n,target[jj]);v=vr/np.linalg.norm(vr,axis=1,keepdims=True)
        h=rays[ii];dtar=tar-c
        d=np.sum(h*v,axis=1);div=1-d*d
        along=(h@dtar-d*(v@dtar))/np.maximum(div,.0001)
        depth=along/np.linalg.norm(r[ii],axis=1)
        world=c+r[ii]*depth[:,None]
        uv=stereo.camera.project(world,*stereo.camera.variables(p,n))
        err=np.linalg.norm(uv-target[jj],axis=1)
        geo=stereo.geographic(world[:,None,:]).ravel()
        valid_k=geo&valid[ii]&ok&(div>.002)&(depth>25)&(depth<260)&(err<8.5)
        print('SIFT',ref,n,'pairs',len(matches),'geographic',np.count_nonzero(valid_k),'px', np.median(err[valid_k]) if np.any(valid_k) else None,flush=True)
        for a,depth_m,res in zip(anchor[ii][valid_k],depth[valid_k],err[valid_k]):
            clouds.append([a[0],a[1],depth_m,res,n])
    return np.array(clouds)

def prior(cloud,u,v):
    xy=np.stack([u,v],axis=-1);tree=cKDTree(cloud[:,:2])
    d,i=tree.query(xy,k=min(12,len(cloud)))
    z=cloud[i,2]
    # Weighted median rejects mismatches from repeating needles/leaves.
    weights=1./((2.+d)**1.6*(1.+cloud[i,3]*.16))
    order=np.argsort(z,axis=-1)
    w=np.take_along_axis(weights,order,axis=-1)
    k=(np.cumsum(w,axis=-1)<.50*w.sum(axis=-1,keepdims=True)).sum(axis=-1)[...,None]
    m=np.take_along_axis(np.take_along_axis(z,order,axis=-1),k,axis=-1)[...,0]
    return m,d[...,0]

def tracked_depths(p,ims,masks,ref,train,u,v,starting):
    """Track patches in both directions, initialized by sparse stereo; discard inconsistent tracks."""
    xy=np.stack([u,v],axis=-1).astype(np.float32)
    coords=xy.reshape(-1,1,2)
    c,r,ray_ok=stereo.inverse_rays(p,ref,xy.reshape(-1,2))
    rh=r/np.linalg.norm(r,axis=1,keepdims=True)
    ground_guess=c+r*starting.reshape(-1,1)
    observed={}
    for n in [ref,*train]:
        gr=cv2.cvtColor(ims[n].astype(np.uint8),cv2.COLOR_RGB2GRAY)
        observed[n]=cv2.createCLAHE(1.7,(10,8)).apply(gr)
    candidate=[]
    for n in train:
        projected=stereo.camera.project(ground_guess,*stereo.camera.variables(p,n)).astype(np.float32)
        est=projected.reshape(-1,1,2).copy()
        opts=dict(winSize=(17,17),maxLevel=2,criteria=(cv2.TERM_CRITERIA_COUNT|cv2.TERM_CRITERIA_EPS,22,.025),flags=cv2.OPTFLOW_USE_INITIAL_FLOW,minEigThreshold=.00015)
        to,good,error=cv2.calcOpticalFlowPyrLK(observed[ref],observed[n],coords,est,**opts)
        back,who,_=cv2.calcOpticalFlowPyrLK(observed[n],observed[ref],to,coords.copy(),**opts)
        target=to.reshape(-1,2)
        t,vectors,r_valid=stereo.inverse_rays(p,n,target)
        direction=vectors/np.linalg.norm(vectors,axis=1,keepdims=True)
        dot=(direction*rh).sum(1);ratio=1-dot*dot
        along=(rh@(t-c)-dot*(direction@(t-c)))/np.maximum(.0001,ratio)
        dist=along/np.linalg.norm(r,axis=1)
        world=c+r*dist[:,None]; reproj=stereo.camera.project(world,*stereo.camera.variables(p,n))
        err=np.linalg.norm(reproj-target,axis=1)
        returning=np.linalg.norm(back.reshape(-1,2)-coords.reshape(-1,2),axis=1)
        accepted=(ray_ok&r_valid&(good.ravel()==1)&(who.ravel()==1)&(returning<1.2)&(err<4.2)&(ratio>.001)&(dist>29)&(dist<220)
                  &(stereo.sample(masks[n].astype(float),*target.T)>.8)
                  &(np.linalg.norm(projected-target,axis=1)<24)&(error.ravel()<22)&stereo.geographic(world[:,None,:]).ravel())
        candidate.append(np.where(accepted,dist,np.nan))
    values=np.stack(candidate)
    # Two image pairs must agree in range. The overlapping support spans three real camera positions.
    with np.errstate(all='ignore'):
        import warnings
        with warnings.catch_warnings():
            warnings.simplefilter('ignore',RuntimeWarning)
            median=np.nanmedian(values,axis=0)
            close=np.abs(values-median[None])<np.minimum(10.,.095*median)[None]
            refined=np.nanmedian(np.where(close,values,np.nan),axis=0)
    return refined.reshape(u.shape),close.sum(axis=0).reshape(u.shape)

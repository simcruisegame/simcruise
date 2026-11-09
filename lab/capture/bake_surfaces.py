"""Bake street-camera pixels onto physical surfaces in a surveyed tile.

Run camera_fit.py first, then pass its JSON result and a folder of 1920x1080
sequence frames (2055.jpg through 2064.jpg). Outputs are independent of the
player and ship with the tile. Image coordinates below are on a 960x540 grid.
"""
import argparse
from PIL import Image, ImageDraw, ImageFilter
from scipy.ndimage import distance_transform_edt, gaussian_filter, map_coordinates, percentile_filter
from camera_fit import *

def rectify(image, corners, size):
    w,h=size
    dst=[(0,0),(w,0),(w,h),(0,h)]
    system=[]; rhs=[]
    for (x,y),(u,v) in zip(dst,np.array(corners)*2):
        system.extend([[x,y,1,0,0,0,-u*x,-u*y],[0,0,0,x,y,1,-v*x,-v*y]])
        rhs.extend([u,v])
    coeff=np.linalg.solve(system,rhs)
    return image.transform(size,Image.Transform.PERSPECTIVE,coeff,Image.Resampling.BICUBIC)

def fuel_assets(images):
    # Quads run clockwise from upper left and contain only the named static surface.
    panels=[
        ('exxon-short',2058,[(26,157),(84,134.7),(84,145.4),(26,169.6)],(160,26)),
        ('exxon-long',2058,[(85.6,134.9),(278,145.4),(277.2,152.1),(85.6,145.2)],(320,16)),
        ('exxon-prices',2057,[(223.8,91.7),(262.4,83.7),(260.3,174.5),(224.5,180.7)],(80,176)),
        ('bp-prices',2062,[(223.9,54.7),(262.2,46.7),(259.4,140.3),(221.6,148.8)],(80,178)),
        ('bp-short',2063,[(116.8,161.2),(165.2,150.9),(165.2,158.9),(116.8,170.3)],(128,22)),
        ('bp-long',2063,[(166,151.4),(282.7,157.3),(282,163.3),(166,159.1)],(192,14)),
        ('exxon-dispenser',2058,[(146.7,181.8),(164.8,178.7),(164.4,213.9),(147,217.6)],(40,76)),
        ('mart-brick',2059,[(84,186.4),(111,179.6),(111,202.1),(84,210.2)],(68,56)),
        ('pizza-front',2056,[(131.2,154.3),(189.7,141.0),(191.3,209.7),(132.2,222.0)],(144,158)),
        ('pizza-side',2056,[(78,192.4),(125.6,181.3),(127.5,222.2),(78.8,229.7)],(120,82)),
    ]
    for name,n,quad,sz in panels:
        im=rectify(images[n],quad,sz)
        im.save(D/f'capture-{name}.jpg',quality=93,subsampling=0)

# Masks omit the camera car, camera-window reflections, near-frame vignetting,
# oncoming traffic and cast mast shadows. Only static road material enters the mosaic.
near_bounds={
 2055:[(130,406),(303,244),(683,244),(887,402)],
 2056:[(108,406),(310,239),(690,238),(900,398)],
 2057:[(99,406),(302,238),(675,239),(893,396)],
 2058:[(99,406),(198,289),(404,221),(651,221),(884,272),(888,395)],
 2059:[(140,400),(227,304),(390,283),(533,270),(707,278),(877,331),(875,394)],
 2060:[(109,397),(301,273),(347,234),(683,235),(886,328),(884,395)],
 2061:[(122,404),(287,271),(345,236),(672,236),(876,319),(891,397)],
 2062:[(131,406),(344,239),(671,235),(879,327),(892,397)],
 2063:[(155,405),(312,274),(352,237),(674,237),(876,325),(894,401)],
 2064:[(146,409),(300,270),(349,239),(683,238),(894,323),(895,401)]
}

# Distant footprints overlap the next frame. Leave a hole around the moving
# red pickup on frames 2059/2060. Mast shadows stay in the lighting system.
mid_bounds={
 2055:[(190,300),(392,199),(650,199),(842,300)],
 2056:[(148,300),(420,197),(620,197),(866,299)],
 2057:[(115,300),(417,194),(584,194),(866,295)],
 2058:[(124,285),(326,238),(459,194),(584,195),(870,267),(850,283)],
 2059:[(185,325),(274,283),(467,208),(565,209),(569,225),(535,236),(450,241),(442,259),(318,286)],
 2060:[(164,334),(377,247),(448,203),(570,204),(848,287),(854,319)],
 2061:[(169,334),(380,242),(465,198),(577,202),(848,297),(854,325)],
 2062:[(152,338),(384,239),(467,197),(578,198),(865,301),(865,335)],
 2063:[(180,330),(392,230),(465,195),(581,200),(866,306),(877,329)],
 2064:[(180,335),(393,230),(468,198),(578,200),(876,303),(874,335)]
}
def ground_mosaic(images,params):
    # The narrow rectangle rotates with the road, so its texture memory tracks the actual corridor.
    axis=np.array([.956,.293]);axis/=np.linalg.norm(axis)
    side_axis=np.array([-axis[1],axis[0]])
    origin=np.array([51.,70.]); size=np.array([265.,34.])
    origin-=side_axis*13.
    nx,nz=3072,400
    v,u=np.indices((nz,nx))
    q=origin+((u+.5)/nx*size[0])[:,:,None]*axis+((v+.5)/nz*size[1])[:,:,None]*side_axis
    world_grid=(q/scale*64).clip(0,63.99)
    y=map_coordinates(height,[world_grid[:,:,1],world_grid[:,:,0]],order=1)
    xyz=np.dstack([q[:,:,0],y,q[:,:,1]]).reshape(-1,3)
    total=np.zeros((len(xyz),3),np.float32);weights=np.zeros(len(xyz),np.float32)
    for frame,polygon in near_bounds.items():
        cam,angles,f,k=variables(params,frame)
        local=(xyz-cam)@mat(*angles).T
        valid=(local[:,2]>2.) & (local[:,2]<44.) & (np.abs(local[:,0])<local[:,2]*1.0)
        idx=np.where(valid)[0]; pos=project(xyz[idx],cam,angles,f,k)
        mask=Image.new('L',(960,540));ImageDraw.Draw(mask).polygon(polygon,fill=255);ImageDraw.Draw(mask).polygon(mid_bounds[frame],fill=255)
        edge=distance_transform_edt(np.array(mask)>0)
        feather=map_coordinates(edge,[pos[:,1],pos[:,0]],order=1,mode='constant')
        valid2=feather>1;idx=idx[valid2];pos=pos[valid2];feather=feather[valid2]
        weight=np.minimum(1.,feather/10.)**2*np.minimum(1.,15/local[idx,2])**2
        image=np.asarray(images[frame].convert('RGB')).astype(np.float32)
        colors=np.stack([map_coordinates(image[:,:,c],[pos[:,1]*2,pos[:,0]*2],order=1) for c in range(3)],axis=1)
        # Neutralize the rear window's broad exposure gradient without modifying localized road details.
        r=np.linalg.norm((pos-[480.,230.])/[700.,550.],axis=1)
        colors*=np.clip(1.+.11*r*r,1.,1.08)[:,None]
        total[idx]+=colors*weight[:,None];weights[idx]+=weight
    rgb=total/np.maximum(weights[:,None],1e-9)
    alpha=np.clip(weights*1.65,0,1)
    gray=(rgb @ np.array([.2126,.7152,.0722])).reshape(nz,nx)
    support=weights.reshape(nz,nx).clip(0,1)
    # Normalize exposure differences on metre scales. Red carries aggregate and
    # repairs around neutral .5, green carries the bright measured road markings.
    # Normalized blur does not pull the undersampled background into the edges.
    smooth=gaussian_filter(gray*support,5.5)/np.maximum(.0001,gaussian_filter(support,5.5))
    detail=np.clip(.5+(gray-smooth)/105.,.12,.86)
    marking=np.clip((gray-155.)/60.,0,1)*np.clip((gray-smooth-12)/29.,0,1)
    feather=distance_transform_edt(support>.05)
    alpha=alpha.reshape(nz,nx)*np.clip(feather/13.,0,1)
    # Bridge the gaps between camera samples only along this same carriageway.
    # Each cross-section inherits nearby photographed asphalt on its own side
    # of the median. The field shader gives positively surveyed turf precedence.
    lo=np.full(nx,np.nan);hi=np.full(nx,np.nan)
    for col in range(nx):
        valid=np.flatnonzero(support[:,col]>.09)
        if valid.size>=4:lo[col],hi[col]=valid[0],valid[-1]
    good=np.isfinite(lo);columns=np.arange(nx)
    lo=np.interp(columns,columns[good],lo[good]);hi=np.interp(columns,columns[good],hi[good])
    # Use well-observed nearby widths; never bridge across the end of the run.
    lo=gaussian_filter(percentile_filter(lo,15,size=221),18)
    hi=gaussian_filter(percentile_filter(hi,85,size=221),18)
    vicinity=np.clip(1.-distance_transform_edt(~good)/150.,0,1)
    envelope=np.clip((v-lo)/6.,0,1)*np.clip((hi-v)/6.,0,1)*vicinity
    out=np.stack((detail,marking,envelope,alpha),axis=-1)
    out=np.round(np.clip(out,0,1)*255.).astype(np.uint8)
    dest=D/'capture-us360-road.png'
    Image.fromarray(out).save(dest,optimize=True)
    print('Road area with >=50% confidence in square metres',round(float((alpha>=.5).mean()*np.prod(size)),1))
    return dict(image=dest.name,origin=(origin/scale).tolist(),axis=axis.tolist(),extent=(size/scale).tolist())

def verge_color(images):
    observations={
        2062:[(9,270),(133,240),(271,216),(318,212),(194,265),(12,326)],
        2061:[(15,304),(62,291),(76,291),(57,315),(20,338)],
        2059:[(9,285),(70,268),(104,269),(78,294),(14,339)],
        2058:[(91,246),(153,228),(195,222),(166,236),(98,252)]}
    samples=[]
    for frame,region in observations.items():
        mask=Image.new("L",(960,540));ImageDraw.Draw(mask).polygon(region,fill=255)
        pixels=np.asarray(images[frame].resize(mask.size))[np.asarray(mask)>0].astype(float)
        green=pixels[(pixels[:,1]>pixels[:,0]*1.08)&(pixels[:,1]>pixels[:,2]*1.3)]
        if len(green):samples.append(np.median(green,axis=0))
    return (np.median(samples,axis=0)/255.).round(4).tolist()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('frames',type=Path);parser.add_argument('parameters',type=Path)
    args=parser.parse_args();p=np.array(json.loads(args.parameters.read_text()))
    images={k:Image.open(args.frames/f'{k}.jpg') for k in near_bounds}
    fuel_assets(images)
    fixtures={}
    for name in features:
        top,base=fixture(p,name);fixtures[name]=dict(point=(base[[0,2]]/scale).tolist(),height=float(top[1]-base[1]))
    record=dict(turf=verge_color(images),sequence=config['sequence'],road=ground_mosaic(images,p),fixtures=fixtures,roof_height=float(4.9+p[4]))
    from trace_junction import uv
    # Frame pairs 2058/2059 put the wooded-side rail 26–31 m across
    # the westbound centreline. Its head and tail are approximate.
    record['rails']=[[uv(pt) for pt in [(13,26.0),(32,26.8),(52,28.0),(72,29.5),(105,31.6)]]]
    # Compact traffic island in the Chula approach. The field project's
    # local orthophoto establishes the outline; the street photo shows the curb.
    record['curbed_turf']=[[uv(pt) for pt in [(0.,-37.8),(3.9,-37.8),(4.25,-30.),(4.24,-20.),(4.5,-13.4),(4.3,-11.7),(1.6,-11.5),(.8,-13.1),(.3,-20.)]]]
    (D/'capture-18585-25424.json').write_text(json.dumps(record,indent=1)+'\n')
if __name__=='__main__':main()

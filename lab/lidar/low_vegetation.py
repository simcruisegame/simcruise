"""Find low vegetation cells supported by classified airborne points and green, near-infrared aerial pixels."""
import argparse, json, math
from collections import Counter,defaultdict
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from pyproj import Transformer, CRS
from PIL import Image
import shapely
from shapely.geometry import shape, box
from shapely.ops import unary_union

CELL = 1.35
HALF = 20037508.342789244
STEP = HALF * 2 / 65536
# Buffers include the mapped carriageway, typical shoulders and the cell foliage radius.
ROAD_BUFFER = {"motorway":13, "trunk":11, "primary":8, "secondary":7, "tertiary":6.3,
               "minor":5.3, "service":3.3, "track":2.5, "path":2, "rail":4, "transit":4}
MAX_RADIUS = .68

def raster(path, bands):
    array = np.array(Image.open(path).convert("RGBA"),dtype=np.float32)
    if np.mean(array[:,:,3]>0)<.90:
        raise ValueError("Insufficient aerial coverage")
    return np.where(array[:,:,3:4]>0,array[:,:,:bands],np.nan)

def map_obstacles(map_path, footprint_file, crs, extent):
    wgs_to_m=Transformer.from_crs(4326,crs,always_xy=True).transform
    metric_to_wgs=Transformer.from_crs(crs,4326,always_xy=True).transform
    area=shapely.transform(box(*extent).buffer(30),metric_to_wgs,interleaved=False)
    roads=[];solid=[]
    for path in [map_path,footprint_file] if footprint_file else [map_path]:
        for f in json.loads(Path(path).read_text())["features"]:
            g=shape(f["geometry"]); prop=f.get("properties",{})
            if not g.intersects(area):continue
            g=shapely.transform(g,wgs_to_m,interleaved=False)
            layer=prop.get("layer", "building")
            if layer=="transportation":
                w=ROAD_BUFFER.get(prop.get("class"),4.)
                if prop.get("subclass")=="driveway":w=max(w,3.)
                roads.append(g);solid.append(g.buffer(w))
            elif layer in ("building","water"):
                solid.append(g.buffer(1.35))
    return unary_union(roads),unary_union(solid)

def write_rows(args):
    points=np.load(args.points)["p"]
    crs=CRS.from_user_input(args.crs)
    if not crs.is_projected or abs(crs.axis_info[0].unit_conversion_factor-1)>.00001:
        raise ValueError("Use projected ground-metre coordinates")
    if crs.to_epsg()==3857:
        raise ValueError("Reproject Web Mercator to a local metre grid before calculating radii")
    x,y,z,cls=points[:,:4].T
    rgb=raster(args.rgb,3);nir=raster(args.nir,1)[:,:,0]
    if rgb.shape[:2]!=nir.shape:raise ValueError("Aerial rasters must be registered")
    red=rgb[:,:,0];ndvi=(nir-red)/(nir+red+1.)
    # Pixels across the full footprint must indicate live vegetation. The green test
    # also removes most painted/bright roofs with a similar two-band ratio.
    green=(ndvi>.19)&(rgb[:,:,1]>red*.98)&(rgb[:,:,1]>rgb[:,:,2]*1.03)&(nir>40)
    ground=points[cls==2]
    if len(ground)<80:raise ValueError("Insufficient ground returns")
    ground_tree=cKDTree(ground[:,:2])
    def above_ground(xyz):
        d,idx=ground_tree.query(xyz[:,:2],k=8)
        zref=np.median(ground[idx,2],1)
        good=(d[:,-1]<5.5)&(np.ptp(ground[idx,2],1)<.65)
        return xyz[:,2]-zref,good
    vegetation=(cls==3)|(cls==4)
    v=points[vegetation,:3]
    if len(v)<12:return [],{"class_3_4_returns":len(v),"status":"insufficient_classified_vegetation"}
    agl,ok=above_ground(v)
    ok&=(agl>=.55)&(agl<=2.85);v=np.column_stack([v[ok,:2],agl[ok]])
    folds=(np.floor(v[:,0]/.45).astype(int)+3*np.floor(v[:,1]/.45).astype(int))%4
    held_out=folds==0
    strategy="disjoint 0.45 m cells"
    if points.shape[1]>=5:
        lines=points[vegetation,4][ok].astype(int)
        unique,counts=np.unique(lines,return_counts=True)
        if len(unique)>=2 and np.min(counts)>=100:
            withheld=int(unique[counts.argmax()])
            held_out=lines==withheld
            strategy=f"entire flight strip {withheld} withheld from vegetation fitting"
    train=v[~held_out];test=v[held_out]
    if not len(train) or not len(test):return [],{"status":"insufficient_heldout_returns"}
    ttree=cKDTree(test[:,:2]); tall=points[cls==5,:3]
    tall_tree=cKDTree(tall[:,:2]) if len(tall) else None
    extent=(x.min(),y.min(),x.max(),y.max())
    roads,obstacles=map_obstacles(args.map,args.footprints,crs,extent)
    groups=defaultdict(list)
    for i,key in enumerate(np.floor(train[:,:2]/CELL).astype(int)):groups[tuple(key)].append(i)
    uv=Transformer.from_crs(crs,3857,always_xy=True)
    tx,ty=args.tile
    west=tx*STEP-HALF;north=HALF-ty*STEP
    image_height=green.shape[0]
    rejects=Counter();accepted=[];hold_res=[]
    for key,ids in groups.items():
        s=train[ids];center=np.median(s[:,:2],axis=0)
        if len(s)<5 or len(np.unique(np.floor(s[:,:2]/.45),axis=0))<3:
            rejects["sparse_training"]+=1;continue
        query=shapely.Point(*center);road_distance=roads.distance(query)
        if not np.isfinite(road_distance) or road_distance>25 or obstacles.intersects(query):
            rejects["mapped_road_building_water_or_interior"]+=1;continue
        ex,ny=uv.transform(*center);u=(ex-west)/STEP;v0=(north-ny)/STEP
        # Check center plus eight points extending beyond the rendered leaf mesh.
        angle=np.arange(8)*np.pi/4
        ax,ay=uv.transform(center[0]+np.r_[0,.99*np.cos(angle)],center[1]+np.r_[0,.99*np.sin(angle)])
        xx=np.floor((ax-west)/STEP*green.shape[1]).astype(int);yy=np.floor((north-ay)/STEP*image_height).astype(int)
        if np.any((xx<0)|(yy<0)|(xx>=green.shape[1])|(yy>=image_height)) or not green[yy,xx].all():
            rejects["aerial_vegetation_or_tile_edge"]+=1;continue
        if tall_tree and len(tall_tree.query_ball_point(center,1.25))>6:
            rejects["tall_overstory"]+=1;continue
        hi=float(np.percentile(s[:,2],80))
        footprint=np.ptp(s[:,:2],axis=0)
        if min(footprint)<.35 or hi<.72:
            rejects["narrow_or_low_support"]+=1;continue
        candidates=test[ttree.query_ball_point(center,1.05)]
        if len(candidates)<2 or len(np.unique(np.floor(candidates[:,:2]/.45),axis=0))<2:
            rejects["sparse_spatial_holdout"]+=1;continue
        ph=float(np.percentile(candidates[:,2],80));error=abs(ph-hi)
        if error>max(.35,.23*hi):
            rejects["height_disagreement"]+=1;continue
        # Confine geometry to the validated holdout and training envelope.
        h=round(min(hi,ph),2);rad=round(float(np.clip(min(footprint)*.50,.24,MAX_RADIUS)),2)
        pixel=np.median(rgb[yy,xx,:],axis=0).astype(int).tolist()
        accepted.append([round(u,6),round(v0,6),h,rad,*pixel]);hold_res.append(error)
    return accepted,{"class_3_4_returns":int(((cls==3)|(cls==4)).sum()),"candidate_cells":len(groups),
                     "accepted_cells":len(accepted),"holdout_strategy":strategy,"rejections":dict(rejects),
                     "holdout_height_mae_m":float(np.mean(hold_res)) if hold_res else None,
                     "holdout_height_p90_m":float(np.percentile(hold_res,90)) if hold_res else None}

if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__)
    for name in ["points","rgb","nir","map","crs","output","aerial_id","point_source","point_date"]:
        p.add_argument("--"+name.replace("_","-"),required=True)
    p.add_argument("--tile",nargs=2,type=int,required=True);p.add_argument("--footprints");a=p.parse_args()
    rows,qa=write_rows(a)
    out=dict(version=1,tile=a.tile,source=a.point_source,date=a.point_date,aerial=a.aerial_id,qa=qa,cells=rows)
    Path(a.output).write_text(json.dumps(out,separators=(",",":")));print(json.dumps(qa,indent=2))

"""Author footprint-clipped roof surfaces from classified, georeferenced lidar.

Use metric Z (convert survey feet first). Input NPZ 'p' columns are x, y, z,
ASPRS class. LAS/LAZ is also supported. Output is grouped by z16 tile.

python roof_profiles.py --points sample.npz --crs EPSG:6346 --footprints buildings.geojson \
  --source "Miami-Dade ITD / NOAA Digital Coast" --source-url https://.../ept.json \
  --date 2021-04-07/2021-06-10 --output game/data --report review.json
Requires numpy, scipy, shapely >= 2.1, pyproj, and laspy for LAS/LAZ input.
"""
import argparse
import json
from pathlib import Path
from collections import Counter, defaultdict
import math
import numpy as np
from scipy.spatial import cKDTree
from pyproj import CRS, Transformer
import shapely
from shapely.geometry import box, shape

RESOLUTION = 2.0
NORM = 100000
ZOOM = 16
N = 2 ** ZOOM


def tile_xy(lon, lat):
    return np.array([(lon + 180) / 360 * N, (1 - np.arcsinh(np.tan(np.radians(lat))) / np.pi) * N / 2])


def ground_reference(poly, data):
    x, y, z, classes = data.T
    ring = shapely.contains_xy(poly.buffer(10).difference(poly), x, y) & (classes == 2)
    if ring.sum() < 45:
        raise ValueError('insufficient classified ground')
    cx, cy = poly.centroid.coords[0]
    direction = (np.arctan2(y[ring]-cy, x[ring]-cx)+np.pi) % (2*np.pi)
    sectors = np.minimum(3, (direction/(np.pi/2)).astype(int))
    med = [np.median(z[ring][sectors == q]) for q in range(4) if (sectors == q).sum() >= 7]
    if len(med) < 3 or np.ptp(med) > 1.0:
        raise ValueError('local ground coverage or grade')
    return float(np.median(med)), int(ring.sum())


def measured_cells(poly, data):
    x, y, z, classes = data.T
    good = shapely.contains_xy(poly.buffer(-.25), x, y) & (classes == 6)
    points = data[good,:3]
    if len(points) < 120:
        raise ValueError('insufficient classified roof')
    # Voxel medians remove flight-strip density weighting and isolated returns.
    groups = defaultdict(list)
    for x, y, z in points:
        groups[(math.floor(x/.60), math.floor(y/.60))].append((x,y,z))
    centers = np.array([np.median(v,axis=0) for v in groups.values()])
    ix0, iy0, ix1, iy1 = poly.bounds
    gx,gy = np.meshgrid(np.arange(np.floor(ix0)+.5,ix1,1),np.arange(np.floor(iy0)+.5,iy1,1))
    inner = shapely.contains_xy(poly.buffer(-.5),gx,gy)
    cells = set(map(tuple,np.floor(points[:,:2]).astype(int)))
    coverage = sum((int(a),int(b)) in cells for a,b in zip(gx[inner],gy[inner])) / max(1,inner.sum())
    if coverage < .83:
        raise ValueError('sparse interior cells')
    return points, centers, float(coverage)


def heights(samples, xy, max_gap, *, interior=True):
    """Robust local tangent estimate; reject unsupported edges and multi-level surfaces."""
    if len(samples) < 10:
        raise ValueError('too few cells')
    tree = cKDTree(samples[:,:2])
    dist, idx = tree.query(xy,k=min(12,len(samples)))
    if np.any(dist[:,0] > max_gap):
        raise ValueError(f'gap {dist[:,0].max():.2f} exceeds {max_gap:.2f} m')
    vals = []
    for query, ds, ids in zip(xy,dist,idx):
        s = samples[ids]
        design = np.c_[np.ones(len(s)),s[:,:2]-query]
        weight = np.exp(-.5*(ds/1.25)**2)
        z = s[:,2]
        for _ in range(2):
            root = np.sqrt(weight)
            coef = np.linalg.lstsq(design*root[:,None],z*root,rcond=None)[0]
            residual = np.abs(design@coef-z)
            weight *= np.minimum(1.,.2/np.maximum(.001,residual))
        if interior and (np.median(residual) > .36 or np.linalg.norm(coef[1:]) > 1.5):
            raise ValueError('unsupported steep or discontinuous surface')
        vals.append(np.clip(coef[0], z.min()-.16, z.max()+.16))
    return np.asarray(vals)


def mesh_xy(poly):
    """Intersect each grid cell with the original shell and all interior rings."""
    x0,y0,x1,y1 = poly.bounds
    lookup = {}; vertices = []; faces = []
    for x in np.arange(np.floor(x0/RESOLUTION)*RESOLUTION,x1,RESOLUTION):
        for y in np.arange(np.floor(y0/RESOLUTION)*RESOLUTION,y1,RESOLUTION):
            piece = poly.intersection(box(x,y,x+RESOLUTION,y+RESOLUTION))
            if piece.area < 1.e-7: continue
            for tri in shapely.constrained_delaunay_triangles(piece).geoms:
                ids=[]
                for xy in np.asarray(tri.exterior.coords)[:3]:
                    key=tuple(np.round(xy,5))
                    if key not in lookup:
                        lookup[key]=len(vertices);vertices.append(xy)
                    ids.append(lookup[key])
                if len(set(ids))==3: faces.append(ids)
    if not faces: raise ValueError('empty triangulation')
    return np.asarray(vertices),np.asarray(faces)


def predict_mesh(vertices, faces, query, fitted):
    # Raster validation samples the actual exported interpolation. The triangle index
    # respects clipped concavities/holes and is independent of what the fitter saw.
    polygons=shapely.polygons(vertices[faces])
    pairs=shapely.STRtree(polygons).query(shapely.points(query),predicate='intersects')
    output=np.full(len(query),np.nan)
    for q,t in pairs.T:
        a,b,c=vertices[faces[t]]
        v=query[q]-a
        weights=np.linalg.solve(np.stack((b-a,c-a),axis=1),v)
        h0,h1,h2=fitted[faces[t]]
        output[q]=h0+(h1-h0)*weights[0]+(h2-h0)*weights[1]
    return output


def author(geom, data, to_geo):
    if not geom.is_valid or not 25 < geom.area < 16000:
        raise ValueError('unsupported footprint')
    if data.size==0: raise ValueError('no nearby lidar')
    base, ground_count=ground_reference(geom,data)
    raw, samples, coverage=measured_cells(geom,data)
    samples[:,2]-=base
    if np.percentile(samples[:,2],5)<1.7 or np.percentile(samples[:,2],95)>100:
        raise ValueError('invalid ground-to-roof heights')
    verts, faces = mesh_xy(geom)
    folds=(np.floor(samples[:,0]/2).astype(int)+2*np.floor(samples[:,1]/2).astype(int))%5
    absolute=[];flat=[]
    for fold in range(5):
        train=samples[folds!=fold];held=samples[folds==fold]
        if len(held)<8:raise ValueError('too few spatial holdout cells')
        # Score only actual held-out sites, interpolated from the training mesh.
        fit=heights(train,verts,4.5,interior=False)
        pred=predict_mesh(verts,faces,held[:,:2],fit)
        if not np.isfinite(pred).all():raise ValueError('uncovered validation sites')
        absolute.extend(abs(pred-held[:,2]));flat.extend(abs(held[:,2]-np.median(train[:,2])))
    mae=float(np.mean(absolute));p90=float(np.percentile(absolute,90));flat_mae=float(np.mean(flat))
    if mae>.27 or p90>.52 or mae>flat_mae+.025:
        raise ValueError(f'spatial holdout error MAE={mae:.2f}, P90={p90:.2f}')
    h=heights(samples,verts,2.9)
    lon,lat=to_geo.transform(verts[:,0],verts[:,1]);world=tile_xy(lon,lat).T
    geo_geom=shapely.transform(geom,to_geo.transform,interleaved=False)
    rings=[geo_geom.exterior,*geo_geom.interiors]
    rings=[tile_xy(*np.array(r.coords)[:-1].T).T for r in rings]
    tile=np.floor(np.mean(rings[0],axis=0)).astype(int)
    encoded=np.array(np.round((world-tile)*NORM),dtype=int)
    rings=[np.round((r-tile)*NORM).astype(int).tolist() for r in rings]
    perimeter={};count=Counter()
    for i,face in enumerate(faces):
        a,b,c=encoded[face]
        if (b[0]-a[0])*(c[1]-a[1]) < (b[1]-a[1])*(c[0]-a[0]):
            faces[i]=face[::-1].copy()
        for u,v in zip(faces[i],np.roll(faces[i],-1)):
            key=tuple(sorted((int(u),int(v))))
            count[key]+=1;perimeter[key]=(u,v)
    edges=np.array([perimeter[key] for key,v in count.items() if v==1])
    hcm=np.rint(h*100).astype(int)
    metrics=dict(class6=int(len(raw)),ground=ground_count,coverage=round(coverage,3),
                 spatial_folds=5,heldout_voxels=len(absolute),mae_m=round(mae,3),
                 p90_m=round(p90,3),flat_mae_m=round(flat_mae,3))
    return tile.tolist(),dict(footprint=rings,eave_m=float(hcm[edges].min()/100),
       v=np.c_[encoded,hcm].tolist(),f=faces.ravel().tolist(),edge=edges.ravel().tolist(),qa=metrics)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('points','crs','footprints','source','source-url','date','output','report'):
        parser.add_argument('--'+key,required=True)
    parser.add_argument('--point-manifest',help='JSON array of source point tiles and SHA-256 hashes for reproducibility')
    args=parser.parse_args()
    if args.points.lower().endswith('.npz'):
        cloud=np.load(args.points)['p']
    else:
        import laspy
        las=laspy.read(args.points)
        cloud=np.c_[las.x,las.y,las.z,las.classification]
    if cloud.ndim!=2 or cloud.shape[1]!=4:raise ValueError('expected x,y,z,class')
    source_crs=CRS.from_user_input(args.crs)
    features=json.loads(Path(args.footprints).read_text())['features']
    report=[];tiles=defaultdict(list)
    for feat in features:
        source_geom=shape(feat['geometry'])
        if source_geom.geom_type!='Polygon':continue
        lon,lat=source_geom.centroid.coords[0]
        local=CRS.from_proj4(f'+proj=aeqd +lat_0={lat} +lon_0={lon} +datum=WGS84 +units=m')
        to_local=Transformer.from_crs(4326,local,always_xy=True)
        to_geo=Transformer.from_crs(local,4326,always_xy=True)
        # Project the expanded geographic bounds before doing the finer metric filters.
        outline=shapely.transform(source_geom,to_local.transform,interleaved=False)
        extent=shapely.transform(outline.buffer(12),Transformer.from_crs(local,source_crs,always_xy=True).transform,interleaved=False).bounds
        sel=(cloud[:,0]>=extent[0])&(cloud[:,0]<=extent[2])&(cloud[:,1]>=extent[1])&(cloud[:,1]<=extent[3])
        subset=cloud[sel].copy()
        tr=Transformer.from_crs(source_crs,local,always_xy=True)
        subset[:,0],subset[:,1]=tr.transform(subset[:,0],subset[:,1])
        # Vector tiles can group distant polygons under one feature ID.
        fid=f"{feat.get('id','')}@{lon:.6f},{lat:.6f}"
        try:
            tile,mesh=author(outline,subset,to_geo);mesh['reference']=fid
            tiles[tuple(tile)].append(mesh)
            report.append(dict(id=fid,status='accepted',tile=tile,vertices=len(mesh['v']),faces=len(mesh['f'])//3,**mesh['qa']))
        except ValueError as err:report.append(dict(id=fid,status='rejected',reason=str(err)))
    out=Path(args.output);out.mkdir(exist_ok=True,parents=True)
    for tile,roofs in tiles.items():
        file=out/f'lidar-roofs-{tile[0]}-{tile[1]}.json'
        doc=dict(schema=1,tile=tile,source=args.source,source_url=args.source_url,acquired=args.date,roofs=roofs)
        if args.point_manifest:
            doc['point_tiles']=json.loads(Path(args.point_manifest).read_text())
        file.write_text(json.dumps(doc,separators=(',',':'))+'\n')
    Path(args.report).write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report))
if __name__=='__main__':main()

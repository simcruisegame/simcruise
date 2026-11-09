"""Generate the roof packet and report independent spatial-cell residuals from a local LAZ leaf."""
import argparse
import hashlib
import json
from pathlib import Path

import laspy
import numpy as np
import shapely
from scipy.interpolate import LinearNDInterpolator
from scipy.optimize import least_squares
from scipy.spatial import cKDTree, Delaunay
from shapely.geometry import Polygon


def roof_cloud(survey, source):
    if hashlib.sha256(source.read_bytes()).hexdigest() != survey['leaf_sha256']:
        raise ValueError('Federal LAZ input checksum does not match the inspected survey')
    points = laspy.read(source)
    half = 20037508.342789244
    width = 2*half/65536
    tx, ty = survey['tile']
    xy = np.column_stack((((np.asarray(points.x)+half)/width-tx)*survey['side_m'],
                          ((half-np.asarray(points.y))/width-ty)*survey['side_m']))
    z = np.asarray(points.z)
    classes = np.asarray(points.classification)
    footprint = Polygon(np.asarray(survey['ring'])*survey['side_m'])
    ground = (classes == 2) & shapely.contains_xy(footprint.buffer(10), *xy.T)
    model = np.column_stack((xy, np.ones(len(xy))))
    keep = ground.copy()
    for _ in range(3):
        plane = np.linalg.lstsq(model[keep], z[keep], rcond=None)[0]
        keep = ground & (abs(z-model@plane) < .35)
    heights = z-model@plane
    inside = (classes == 1) & shapely.contains_xy(footprint, *xy.T) & (heights>9) & (heights<48)
    xy, heights = xy[inside], heights[inside]
    hold = (np.floor(xy[:,0]/1.5)+2*np.floor(xy[:,1]/1.5)).astype(int)%5 == 0
    return footprint, xy, heights, hold, int(keep.sum())


def generate(survey, source):
    footprint, xy, h, hold, ground_count = roof_cloud(survey, source)
    train = ~hold
    sides = np.asarray(survey['section_bands'], object)
    levels = sides[:,0].astype(float)
    radii = sides[:,1].astype(float)*.5
    theta = np.arctan2(-.44,.898)+np.pi/8
    def radial(points, center, height):
        vectors = points-center
        angles = np.arctan2(vectors[:,1], vectors[:,0])
        bands = np.clip(np.searchsorted(levels,height)-1,0,len(levels)-2)
        spacing = 2*np.pi/sides[bands,2].astype(float)
        radial_scale = np.cos(spacing/2)/np.cos((angles-theta)%spacing-spacing/2)
        return np.linalg.norm(vectors,axis=1)-np.interp(height,levels,radii)*radial_scale
    center = np.median(xy[train & (h>37)],axis=0)
    tower = train & (h>24.4) & (h<42.8) & (np.linalg.norm(xy-center,axis=1)<9)
    center = least_squares(lambda c:radial(xy[tower], c, h[tower]),center,loss='huber',f_scale=.25).x
    distance = np.linalg.norm(xy-center,axis=1)
    ordinary = train & (h>14.2) & (h<24.8) & (distance>5.3)
    roof_tree = cKDTree(xy[ordinary])
    ordinary_height = h[ordinary]
    outline = []
    ring = np.asarray(footprint.exterior.coords)[:-1]
    for a,b in zip(ring,np.roll(ring,-1,axis=0)):
        steps = int(np.ceil(np.linalg.norm(a-b)/1.25))
        outline.extend(a+(b-a)*(step/steps) for step in range(steps))
    samples = list(outline)
    inner = footprint.buffer(-.38)
    for x in np.arange(footprint.bounds[0],footprint.bounds[2]+1,1.4):
        for y in np.arange(footprint.bounds[1],footprint.bounds[3]+1,1.4):
            if shapely.contains_xy(inner,x,y):
                samples.append([x,y])
    roofxy = np.asarray(samples)
    distances, nearest = roof_tree.query(roofxy,k=7)
    roofz = np.array([np.median(ordinary_height[idx[ds<=max(1.5,ds[0]+.55)]]) for ds,idx in zip(distances,nearest)])
    verts, faces, finishes = [], [], []
    def vertex(p,z):
        verts.append([int(round(p[0]/survey['side_m']*100000)),int(round(p[1]/survey['side_m']*100000)),int(round(z*100))])
        return len(verts)-1
    def face(a,b,c,finish):
        faces.extend([int(a),int(b),int(c)])
        finishes.append(finish)
    for pt,z in zip(roofxy,roofz):
        vertex(pt,z)
    boundary = footprint.buffer(.001)
    for a,b,c in Delaunay(roofxy).simplices:
        if boundary.covers(Polygon(roofxy[[a,b,c]])):
            face(a,b,c,'0')
    edges = [i for j in range(len(outline)) for i in (j,(j+1)%len(outline))]
    angles = theta+np.arange(16)*np.pi/8
    direction = np.column_stack((np.cos(angles),np.sin(angles)))
    previous = None
    for i,(z,diameter,n,finish) in enumerate(sides):
        sector = 2*np.pi/n
        rr = diameter*.5*np.cos(sector/2)/np.cos((angles-theta)%sector-sector/2)
        current = [vertex(p,z) for p in center+direction*rr[:,None]]
        if previous is not None:
            for j in range(16):
                k = (j+1)%16
                face(previous[j],previous[k],current[k],sides[i-1,3])
                face(previous[j],current[k],current[j],sides[i-1,3])
        previous = current
    for j in range(1,15):
        face(previous[0],previous[j],previous[j+1],'2')
    profile = dict(name='Wyoming State Capitol',eave_m=round(float(np.percentile(roofz[:len(outline)],10)),2),
                   footprint=[np.rint(np.array(survey['ring'])*100000).astype(int).tolist()],
                   v=verts,f=faces,edge=edges,face_finish=''.join(finishes),
                   color_observations={'1':survey['gold_srgb'],'2':survey['drum_srgb']})
    low = hold & (distance>10) & (h>14.2) & (h<24.8)
    high = hold & (distance<10) & (h>24.4) & (h<42.8)
    roof_residual = abs(LinearNDInterpolator(roofxy,roofz)(xy[low])-h[low])
    radial_residual = abs(radial(xy[high],center,h[high]))
    report = dict(ground_controls=ground_count, roof_holdout=int(np.isfinite(roof_residual).sum()),
                  roof_error_m=np.nanpercentile(roof_residual,[50,90]).tolist(),
                  tower_holdout=int(high.sum()), tower_radial_error_m=np.percentile(radial_residual,[50,90]).tolist())
    return dict(schema=1,tile=survey['tile'],roofs=[profile]), report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('laz', type=Path)
    parser.add_argument('geometry', type=Path)
    args = parser.parse_args()
    configuration = json.loads(Path(__file__).with_name('survey.json').read_text())
    result, metrics = generate(configuration,args.laz)
    args.geometry.write_text(json.dumps(result,separators=(',',':'))+'\n')
    print(json.dumps(metrics,indent=2))

"""Corroborate missing Overture outlines with a second mapping and separately dated airborne imagery."""
import argparse
import json
import math
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter, map_coordinates
from shapely.affinity import translate
from shapely.geometry import Polygon

from district_sources import Photos, SourceCache, geometry_source


def polygon(rings):
    if not rings or not rings[0]:
        return Polygon()
    value = Polygon(rings[0], rings[1:])
    return value if value.is_valid else Polygon()


def iou(left, right):
    if left.is_empty or right.is_empty or not left.intersects(right):
        return 0.
    return left.intersection(right).area/left.union(right).area


def registration(base, observations, side):
    ties = []
    for index, reference in enumerate(base):
        if reference.is_empty:
            continue
        west, north, east, south = reference.bounds
        if min(west,north,1-east,1-south)*side < 4:
            continue
        scores = sorted(((iou(reference, item), j) for j, item in enumerate(observations)
                         if reference.intersects(item)), reverse=True)
        if not scores:
            continue
        overlap, j = scores[0]
        observed = observations[j]
        shift = (np.array(reference.centroid.coords[0])-np.array(observed.centroid.coords[0]))*side
        if (overlap > .45 and (len(scores)==1 or scores[1][0]<.15) and
            np.linalg.norm(shift)<=7 and .65<reference.area/observed.area<1.55):
            ties.append((index, reference, observed, overlap, shift))
    train = [row for row in ties if row[0]%2==0]
    hold = [row for row in ties if row[0]%2==1]
    fit = {'pass':False, 'training':len(train), 'blind':len(hold)}
    if min(len(train),len(hold)) < 5:
        return fit, np.zeros(2)
    delta = np.median([row[4] for row in train],axis=0)
    scatter = np.median([np.linalg.norm(row[4]-delta) for row in train])
    initial = np.array([row[3] for row in hold])
    corrected = np.median([iou(row[1],translate(row[2],xoff=delta[0]/side,yoff=delta[1]/side)) for row in hold])
    native_offsets = [np.linalg.norm(row[4]) for row in hold]
    if np.linalg.norm(delta)<=5 and scatter<=1.7 and corrected>=np.median(initial)+.07:
        fit.update(kind='tile-rigid-offset', shiftMeters=np.round(delta,3).tolist(),
                   beforeIoU=round(float(np.median(initial)),3),afterIoU=round(float(corrected),3),
                   medianResidualMeters=round(float(scatter),3))
        fit['pass']=True
        return fit, delta/side
    if (len(train)>=7 and len(hold)>=7 and np.median(initial)>=.8 and np.percentile(initial,25)>=.65 and
        np.median(native_offsets)<=2 and np.percentile(native_offsets,75)<=3):
        fit.update(kind='native-checked',beforeIoU=round(float(np.median(initial)),3),
                   originMedianM=round(float(np.median(native_offsets)),3),originQ75M=round(float(np.percentile(native_offsets,75)),3))
        fit['pass']=True
    return fit, np.zeros(2)


def unique_pairs(candidates, observed, source, side):
    possible = []
    for i, (_, outline) in enumerate(candidates):
        for j, survey in enumerate(observed):
            overlap = iou(outline,survey)
            if overlap > .20:
                possible.append((i,j,overlap))
    left = [pair[0] for pair in possible]
    right = [pair[1] for pair in possible]
    result = []
    for i,j,overlap in possible:
        entry, outline = candidates[i]
        witness = source[j]['origin']
        if (overlap < .70 or left.count(i)!=1 or right.count(j)!=1 or witness['SOURCE']!='ORNL' or
            witness['VAL_METHOD'] not in ('Manual','Automated','Spatial Intersect') or entry['geometrySource']!='Microsoft ML Buildings'):
            continue
        distance = outline.centroid.distance(observed[j].centroid)*side
        ratio = outline.area/observed[j].area
        if distance<=3 and .70<=ratio<=1.43 and isinstance(witness['IMAGE_DATE'],(int,float)) and witness['IMAGE_DATE']>0:
            result.append((entry,outline,witness,overlap,distance))
    return result


def image_edges(outline, raster, smoothed, side):
    # Compare normal-direction color jumps along an entire edge with parallel
    # controls on both sides. One fitted offset serves every sample on the edge.
    pixel_per_meter = raster.shape[1]/side
    xy = np.asarray(outline.exterior.coords)*raster.shape[1]
    tested = []
    for start,end in zip(xy[:-1],xy[1:]):
        vector=end-start
        length=np.linalg.norm(vector)
        if length/pixel_per_meter<3:
            continue
        count=max(4,min(35,round(length/pixel_per_meter/1.2)))
        line=start+vector*np.linspace(.14,.86,count)[:,None]
        # A black no-data seam inside a footprint must never resemble a roof edge.
        if np.any(line<13) or np.any(line>raster.shape[1]-13):
            return None
        normal=np.array([vector[1],-vector[0]])/length
        def profile(offset):
            a=line+normal*pixel_per_meter*(offset-.95)
            b=line+normal*pixel_per_meter*(offset+.95)
            def sample(points):
                return np.stack([map_coordinates(smoothed[:,:,k],[points[:,1],points[:,0]],order=1,mode='nearest') for k in range(3)],axis=1)
            return np.sqrt(np.mean((sample(a)-sample(b))**2,axis=1))
        offsets=np.arange(-1.8,1.81,.45)
        for distance in (-4,0,4):
            points=np.rint(line+normal*pixel_per_meter*distance).astype(int)
            points=np.clip(points,0,raster.shape[1]-1)
            if np.mean(raster[points[:,1],points[:,0]].max(axis=1)>5)<.95:
                return None
        profiles=np.array([profile(v) for v in offsets])
        medians=np.median(profiles,axis=1)
        best=int(np.argmax(medians))
        control=np.median([np.median(profile(v)) for v in (-6.,-4.5,4.5,6.)])
        supported=medians[best]>=.10 and np.mean(profiles[best]>=.075)>=.56 and medians[best]>=1.4*control
        tested.append((length, bool(supported)))
    if not tested:
        return None
    supported=sum(1 for _,ok in tested if ok)
    fraction=sum(length for length,ok in tested if ok)/sum(length for length,_ in tested)
    if supported<3 or fraction<.65:
        return None
    return {'edges':supported,'edgeFraction':round(fraction,3)}


def generate(data, cache):
    tile=data['tile']
    if len(tile)!=2 or any(type(v) is not int or not 0<=v<65536 for v in tile):
        raise ValueError('Expected a zoom-16 tile')
    if len(data['baseline'])>1500 or len(data['candidates'])>64:
        raise ValueError('Dense district exceeded screening budget')
    side=611.496226/math.cosh(math.pi*(1-2*(tile[1]+.5)/65536))
    source=geometry_source(tile,cache)
    observed=[polygon(row['geo']) for row in source]
    base=[polygon(row) for row in data['baseline']]
    ties=[geometry if source[i]['origin']['SOURCE']=='ORNL' else Polygon() for i,geometry in enumerate(observed)]
    fit,offset=registration(base,ties,side)
    result={'release':data['release'],'registration':fit,'features':{}}
    if not fit['pass']:
        return result
    transformed=[translate(r,xoff=offset[0],yoff=offset[1]) for r in observed]
    candidates=[(r,polygon([r['ring']])) for r in data['candidates']]
    matches=unique_pairs(candidates,transformed,source,side)
    controls=[translate(r,xoff=12/side) for r in transformed]
    if not matches or unique_pairs(candidates,controls,source,side):
        return result
    photos=Photos(tile,cache)
    filtered={}
    for entry,outline,witness,overlap,distance in matches:
        date=datetime.fromtimestamp(witness['IMAGE_DATE']/1000,timezone.utc).date().isoformat()
        receipts=[]
        for acquisition,identifier,raster in photos.at(outline.centroid):
            if acquisition==date:
                continue
            if identifier not in filtered:
                filtered[identifier]=gaussian_filter(raster.astype(np.float32)/255.,sigma=(.8,.8,0))
            scores=image_edges(outline,raster,filtered[identifier],side)
            if scores:
                receipts.append({'item':identifier,'date':acquisition,**scores})
        if len(receipts)!=2:
            continue
        result['features'][entry['id']]={'outlineHash':entry['outlineHash'],'source':witness['SOURCE'],
            'validationMethod':witness['VAL_METHOD'],'sourceDate':date,'iou':round(overlap,3),
            'centroidM':round(distance,2),'images':receipts}
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs',type=Path)
    parser.add_argument('cache',type=Path)
    parser.add_argument('output',type=Path)
    parser.add_argument('--offline',action='store_true')
    args=parser.parse_args()
    data=json.loads(args.inputs.read_text())
    cache=SourceCache(args.cache,args.offline)
    district=generate(data,cache)
    key='-'.join(map(str,data['tile']))
    args.output.write_text(json.dumps({'schema':1,'tiles':{key:district}},indent=2)+'\n')
    print(len(district['features']),'corroborated outlines;',cache.requests,'remote objects;',cache.size,'body bytes')

"""Condition the US 360/Chula Road surface near the surveyed junction.

The source elevation lattice and road centerlines remain in each tile. This
adds sparse elevations used by both the rendered terrain and its collider.
Road grades are inferred from a spatially continuous neighborhood of measured
centerline heights. Required authoring packages: NumPy and SciPy.
"""
import json
from pathlib import Path
import numpy as np
from scipy.ndimage import map_coordinates
from scipy.spatial import cKDTree

DATA = Path(__file__).resolve().parents[1] / 'game/data'
TILE = 40075016.686 / 65536 * np.cos(np.radians(37.369103))
ANCHOR = np.array((18585,25424))
# Limit this approximation to the measured junction approaches.
JUNCTION = np.array([.38,.26]) * TILE
MAX_DISTANCE = 620.0
STEP = 64
records = {}
for dx in range(-2,3):
    for dy in range(-2,3):
        key=(dx,dy)
        path=DATA/('%d_%d.json'%tuple(ANCHOR+key))
        if path.exists():
            info=json.loads(path.read_text())
            if info.get('region')=='Chula' and info['grid']==STEP:
                records[key]=(path,info,np.array(info['h']).reshape(STEP+1,STEP+1))

# Triangulated terrain on both sides will query the same worldwide XY. Use
# shared grid vertices at borders instead of processing individual tile edges.
def elevation(p):
    cell=tuple(np.floor(p/TILE).astype(int))
    if cell not in records:return np.nan
    local=(p/TILE-cell)*STEP
    return map_coordinates(records[cell][2],[[local[1]],[local[0]]],order=1,mode='nearest')[0]

samples=[];heights=[];widths=[]
for key,(_,info,_) in records.items():
    for road in info['roads']:
        if road[2] not in ('Chula Road','Patrick Henry Highway','Turnaround'):continue
        pts=(np.array(road[3])+key)*TILE
        for a,b in zip(pts,pts[1:]):
            length=np.linalg.norm(b-a)
            if length<.01:continue
            for t in (np.arange(max(1,int(np.ceil(length/2))))+.5)/max(1,int(np.ceil(length/2))):
                p=a+(b-a)*t
                if np.linalg.norm(p-JUNCTION)>MAX_DISTANCE+100:continue
                value=elevation(p)
                if np.isfinite(value):
                    samples.append(p);heights.append(value);widths.append(road[4])
samples=np.array(samples);heights=np.array(heights);widths=np.array(widths)
tree=cKDTree(samples)

def smoothstep(x):
    t=np.clip(x,0,1)
    return t*t*(3-2*t)

def grade(p,original):
    distance,nearest=tree.query(p)
    outer_width=widths[nearest]/2+17.0
    if distance>=outer_width or np.linalg.norm(p-JUNCTION)>=MAX_DISTANCE:return None
    # Weighted least squares estimates a smooth longitudinal terrain gradient.
    # Parallel carriageways and the crossing influence the same intersection.
    ix=np.array(tree.query_ball_point(p,58))
    offset=samples[ix]-p
    weight=np.exp(-np.sum(offset*offset,axis=1)/(2*19**2))
    # Keep the transverse slope small where the underlying DEM slopes toward a
    # ditch. A gentle regularization also makes the one-line fit well determined.
    A=np.column_stack([np.ones(len(ix)),offset])
    lhs=A.T@(A*weight[:,None]);lhs[1:,1:]+=np.eye(2)*weight.sum()*65.
    coeff=np.linalg.solve(lhs,A.T@(heights[ix]*weight))
    # Cap the inferred banking across each local road. The underlying slope
    # along the road remains free to rise or descend with the landscape.
    neighbors=samples[np.array(tree.query_ball_point(samples[nearest],13))]
    _,axes=np.linalg.eigh(np.cov(neighbors.T))
    lateral=axes[:,0]
    gradient=coeff[1:]
    cross_grade=gradient@lateral
    correction=(np.clip(cross_grade,-.025,.025)-cross_grade)*(p-samples[nearest])@lateral
    center_height=coeff[0]+correction
    edge=smoothstep((outer_width-distance)/8)
    extent=smoothstep((MAX_DISTANCE-np.linalg.norm(p-JUNCTION))/65)
    return round(float(original+(center_height-original)*edge*extent),3)

for key,(path,info,z) in records.items():
    graded=[]
    for y in range(STEP+1):
        for x in range(STEP+1):
            p=TILE*(np.array(key)+np.array((x,y))/STEP)
            value=grade(p,z[y,x])
            if value is not None and abs(value-z[y,x])>.001:
                graded.extend((y*(STEP+1)+x,value))
    if graded:
        info['roadbed']=graded
        path.write_text(json.dumps(info,separators=(',',':')))
        print(path.name,len(graded)//2,'graded lattice vertices')

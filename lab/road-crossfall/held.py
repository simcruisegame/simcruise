"""Evaluate the actual Godot triangle mesh against a different ground-return node.
Usage: held.py BASE.json SECTIONS.json GODOT_MESH.bin TRAINING_ROOF_RESULT.json HELD.laz [HELD.laz ...]
Dependencies: numpy and laspy[lazrs]. Use original files from the named EPT project.
"""
import argparse
import json
import math
from collections import defaultdict
from pathlib import Path

import laspy
import numpy as np


class RoadMesh:
    def __init__(self, file, side):
        with open(file, 'rb') as stream:
            count, size = np.fromfile(stream, '<u4', 2)
            self.xyz = np.fromfile(stream, '<f4', int(count)*3).reshape(-1, 3).astype(float)
            self.faces = np.fromfile(stream, '<u4', int(size)).reshape(-1, 3)
        self.side = side
        self.xz = self.xyz[:, [0, 2]]
        self.cells = defaultdict(list)
        mid = self.xz[self.faces].mean(axis=1)
        for face, cell in zip(self.faces, np.floor(mid/side*64).astype(int)):
            self.cells[tuple(cell)].append(face)

    def height(self, p):
        faces = np.asarray(self.cells[tuple(np.floor(p/self.side*64).astype(int))])
        tri = self.xz[faces]
        lhs = (tri[:, 1:]-tri[:, 0, None]).transpose(0, 2, 1)
        uv = np.linalg.solve(lhs, (p-tri[:, 0])[..., None])[..., 0]
        weights = np.column_stack([1-uv.sum(axis=1), uv])
        pick = weights.min(axis=1).argmax()
        if weights[pick].min() < -.0002:
            raise ValueError('Query was outside every output face')
        return weights[pick] @ self.xyz[faces[pick], 1]


def evaluate(base, derived, mesh_file, train, files):
    x, y = base['id']
    if derived['id'] != base['id'] or derived['project']['name'] != train['project']['name']:
        raise ValueError('Work units or coordinates disagree')
    stem = 'roof-laz-'+derived['project']['name']+'-'
    training = {n['key'] for n in train['nodes']}
    width = 40075016.68557849/65536
    side = width / math.cosh(math.pi*(1-2*(y+.5)/65536))
    west = x*width-20037508.342789244
    north = 20037508.342789244-y*width
    points = []
    for file in files:
        basename = file.name.removesuffix('.laz')
        if not basename.startswith(stem) or basename[len(stem):] in training:
            raise ValueError('A held node must be independent and belong to the same EPT work unit')
        cloud = laspy.read(file)
        mask = (np.asarray(cloud.classification)==2) & ~np.asarray(cloud.withheld).astype(bool)
        points.append(np.column_stack([(np.asarray(cloud.x)[mask]-west)*side/width,
                                      (north-np.asarray(cloud.y)[mask])*side/width, np.asarray(cloud.z)[mask],
                                      np.asarray(cloud.point_source_id)[mask]]))
    cloud = np.concatenate(points)
    grid = np.array(base['h']).reshape(base['grid']+1, -1)
    model = RoadMesh(mesh_file, side)

    def old(p):
        gx, gy = p/side*base['grid']
        ix, iy = int(gx), int(gy)
        u, v = gx-ix, gy-iy
        a, b = grid[iy, ix:ix+2]
        c, d = grid[iy+1, ix:ix+2]
        return a+(b-a)*u+(c-a)*v if u+v<=1 else d+(c-d)*(1-u)+(b-d)*(1-v)

    output=[]
    for rows in derived['runs']:
        by_segment=[]
        stations=np.array([[r[3]*side,r[4]*side] for r in rows])
        for i, (a,b) in enumerate(zip(stations[:-1],stations[1:])):
            delta=b-a
            length=np.linalg.norm(delta)
            tangent=delta/length
            lateral=np.array([-tangent[1], tangent[0]])
            strip=rows[i][12]  # independent validation strip, never used for calculating either slope
            nearby=cloud[(np.linalg.norm(cloud[:,:2]-a,axis=1)<12) & (cloud[:,3]==strip)]
            along=(nearby[:,:2]-a)@tangent
            across=(nearby[:,:2]-a)@lateral
            progress=i*6+along
            total=(len(rows)-1)*6
            select=(along>=0)&(along<length)&(np.abs(across)<=rows[i][7]) & (np.minimum(progress,total-progress)>min(7,total*.3))
            picked=nearby[select];u=across[select]
            if len(picked)<5 or min((u<-.6).sum(),(u>.6).sum())<2:continue
            grade=.5*(rows[i][8]+rows[i+1][8])
            training_grade=along[select]*grade
            truth=picked[:,2]-training_grade
            centers=a+along[select,None]*tangent
            base_center=np.array([old(p) for p in centers])
            errors=[]
            for predict in [old,model.height]:
                residual=np.array([predict(p) for p in picked[:,:2]])-base_center-truth
                errors.append(np.abs(residual-np.median(residual)).tolist())
            by_segment.append(errors)
        if by_segment:
            errors=[np.concatenate([s[k] for s in by_segment]) for k in [0,1]]
            output.append({'way':rows[0][1],'samples':len(errors[0]),'oldCm':round(float(np.mean(errors[0]))*100,3),'meshCm':round(float(np.mean(errors[1]))*100,3)})
    return output


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['base','sections','mesh','training']:
        parser.add_argument(name,type=Path)
    parser.add_argument('held',nargs='+',type=Path)
    args=parser.parse_args()
    load=lambda f:json.loads(f.read_bytes())
    print(json.dumps(evaluate(load(args.base),load(args.sections),args.mesh,load(args.training),args.held),indent=2))

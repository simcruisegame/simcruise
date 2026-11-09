"""Compare two game renders with a withheld, lens-corrected woodland capture.

Arguments: frame, original photo, solved camera JSON, game render with scan, game render without scan.
Use the frame's fitted pose and the undistorted horizontal field of view for both game renders.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image
from woodland_stereo import sample, vegetation


def compare(frame, photo, parameters, enabled, disabled):
    source=np.asarray(Image.open(photo).convert('RGB').resize((960,540)),dtype=float)
    on=np.asarray(Image.open(enabled).convert('RGB'),dtype=float)
    off=np.asarray(Image.open(disabled).convert('RGB'),dtype=float)
    if on.shape != off.shape:raise ValueError('Both renders must use the same resolution')
    hh,ww=on.shape[:2]
    f,k=parameters[2:4]
    yy,xx=np.indices((hh,ww))
    uv=np.stack([(xx+.5-ww*.5)/(ww/960*f),(yy+.5-hh*.5)/(ww/960*f)],-1)
    projection=np.array([480,270])+uv*(f*(1+k*(uv**2).sum(-1)))[...,None]
    rx,ry=projection.transpose(2,0,1)
    target=np.stack([sample(source[:,:,c],rx,ry) for c in range(3)],axis=-1)
    mask=sample(vegetation(source,frame).astype(float),rx,ry)>.9
    affected=(np.max(abs(on-off),axis=-1)>9)&mask
    def mae(rgb,area):return round(float(abs(rgb-target)[area].mean()),2)
    return dict(affected_forest_pixels=int(affected.sum()),forest_pixels=int(mask.sum()),
                rgb_mae_forest_on=mae(on,mask),rgb_mae_forest_off=mae(off,mask),
                rgb_mae_affected_on=mae(on,affected),rgb_mae_affected_off=mae(off,affected))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('frame',type=int);parser.add_argument('photo',type=Path)
    parser.add_argument('poses',type=Path);parser.add_argument('enabled',type=Path);parser.add_argument('disabled',type=Path)
    cmd=parser.parse_args()
    print(json.dumps(compare(cmd.frame,cmd.photo,json.loads(cmd.poses.read_text()),cmd.enabled,cmd.disabled),indent=2))

"""Build the observed eastbound sky sector at US 360 and Chula Road.

Use the original 1920x1080 KartaView sequence 3690649, frames 2057–2064,
by miken (CC BY-SA 4.0). The output sky texture is a CC BY-SA 4.0 adaptation.
Usage: python bake_sky.py /path/to/frames
"""
import argparse
import json
import warnings
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage
from camera_fit import frames, mat, variables

HERE=Path(__file__).resolve().parent
OUT=HERE.parents[1]/'game/data/capture-chula-sky.png'
# Conservative upper envelopes in 480x270 reference pixels, every 40 pixels.
# They run above the canopy, signals, signs, trees and wires in each frame.
ENVELOPE={
    2057:[47,52,48,33,46,49,75,77,45,35,20,15,9],
    2058:[57,61,55,58,52,52,78,78,45,37,28,20,16],
    2059:[52,62,61,55,55,58,77,75,44,36,27,33,40],
    2060:[51,57,46,38,38,30,27,27,33,39,45,55,62],
    2061:[60,68,63,61,56,55,52,50,48,49,57,66,72],
    2062:[63,68,51,14,54,57,61,63,54,52,55,64,71],
    2063:[60,70,66,62,51,55,65,64,52,51,59,68,70],
    2064:[57,62,47,25,47,57,62,64,54,54,57,65,71]}
# Angular rectangle in degrees, clockwise from north, elevation increasing up.
AZ_MIN,AZ_MAX=18.,191.
EL_MIN,EL_MAX=-2.,57.
W,H=1038,354


def observe(p,frame,src,rays):
    img=np.asarray(Image.open(src/f'{frame}.jpg').convert('RGB').resize((960,540),Image.Resampling.LANCZOS),dtype=float)
    _,angles,f,k=variables(p,frame)
    camera=rays@mat(*angles).T
    plane=camera[...,:2]/np.maximum(camera[...,2:],.01)
    r2=np.sum(plane*plane,axis=-1)
    uv=plane*(f*(1+k*r2))[...,None]+[480,270]
    x,y=uv[...,0],uv[...,1]
    yy,xx=np.indices((540,960));r,g,b=img.transpose(2,0,1)
    # Bound the lens footprint well inside the dark windshield rims. The top
    # right edge of the dashcam image also has a fixed windshield reflection.
    envelope=np.interp(xx[0]/2,np.arange(13)*40,ENVELOPE[frame])*2
    mask=(xx>=74)&(xx<=882)&(yy>=10)&(yy<envelope[None,:]+8)
    mask&=(b>r*1.015)&(b>=g*.99)&(g>82)
    # Reject high-contrast wires and pole tips independently of their colour.
    lum=.2126*r+.7152*g+.0722*b
    edges=np.hypot(ndimage.sobel(lum,axis=0),ndimage.sobel(lum,axis=1))>72
    mask&=~ndimage.binary_dilation(edges,iterations=3)
    interior=ndimage.distance_transform_edt(mask)
    margin=ndimage.map_coordinates(interior,[y,x],order=1,mode='constant',cval=0)
    valid=(camera[...,2]>.4)&(r2<1.15)&(margin>=3.)
    rgb=np.stack([ndimage.map_coordinates(c,[y,x],order=1,mode='nearest') for c in (r,g,b)],axis=-1)
    rgb[~valid]=np.nan
    return rgb


def bake(src):
    config=json.loads((HERE/'chula-camera.json').read_text())
    az=np.radians(AZ_MIN+(np.arange(W)+.5)*(AZ_MAX-AZ_MIN)/W)
    elev=np.radians(EL_MAX-(np.arange(H)+.5)*(EL_MAX-EL_MIN)/H)
    az,elev=np.meshgrid(az,elev)
    rays=np.stack([np.sin(az)*np.cos(elev),np.sin(elev),-np.cos(az)*np.cos(elev)],axis=-1)
    inputs=np.array([observe(np.array(config),frame,src,rays) for frame in frames],dtype='float32')
    # Median per direction rejects passing vehicles, poles and cloud outliers.
    # At least four contributing, colour-consistent observations are required.
    with warnings.catch_warnings():
        warnings.simplefilter('ignore',category=RuntimeWarning)
        observed=np.nanmedian(inputs,axis=0)
        error=np.max(np.abs(inputs-observed[None,...]),axis=-1)
        spread=np.nanmedian(error,axis=0)
    count=np.sum(error<15.,axis=0)
    eligible=(count>=4)&(spread<10)
    eligible=ndimage.binary_opening(eligible,iterations=2)
    if np.count_nonzero(eligible)<4000:raise RuntimeError('Insufficient multi-view sky coverage')
    yy,xx=np.nonzero(eligible)
    degrees=np.degrees(elev[:,0])
    # Follow the clear-sky luminance gradient outside photographed elevations.
    # The fit uses only sky samples; the angular endpoints keep it bounded.
    row_med=[];levels=[]
    for y in range(H):
        if eligible[y].sum()>45:
            levels.append(degrees[y]);row_med.append(np.percentile(observed[y,eligible[y]],35,axis=0))
    slope,intercept=np.polyfit(levels,row_med,1)
    slope=np.clip(slope,-7.,1.)
    color_grad=np.clip(intercept[None,:]+np.clip(degrees,-1,24)[:,None]*slope[None,:],0,255)
    gradient=np.broadcast_to(color_grad[:,None,:],(H,W,3))
    # Reconstruct small gaps by normalized convolution of sky-only residuals.
    # For unobserved directions, residual cloud detail fades back to the
    # measured clear-sky gradient. No foreground RGB enters this calculation.
    valid=eligible.astype(float)
    residual=np.where(eligible[...,None],observed-gradient,0.)
    fill=np.zeros_like(residual)
    for sigma in (35,15,6,2):
        support=ndimage.gaussian_filter(valid,sigma,mode='constant')
        smoothed=ndimage.gaussian_filter(residual,(sigma,sigma,0),mode='constant')
        local=smoothed/np.maximum(support[...,None],.0001)
        amount=np.clip(support/.38,0,1)[...,None]
        fill=fill*(1-amount)+local*amount
    observed_weight=ndimage.gaussian_filter(valid,1)*valid
    rgb=gradient+fill*(1-observed_weight[...,None])+residual*observed_weight[...,None]
    # A single continuous window avoids cloud-shaped holes at invalid pixels.
    # Convexification applies only to blend coverage; it never admits new
    # photographed content. Near the horizon HDR terrain/sky takes over.
    from scipy.spatial import ConvexHull
    from PIL import ImageDraw
    hull=ConvexHull(np.column_stack((xx,yy)))
    silhouette=Image.new('L',(W,H))
    ImageDraw.Draw(silhouette).polygon([(int(xx[i]),int(yy[i])) for i in hull.vertices],fill=255)
    gap_degrees=ndimage.distance_transform_edt(np.asarray(silhouette)==0)/6.
    def smoothstep(v):
        t=np.clip(v,0,1);return t*t*(3-2*t)
    alpha=1-smoothstep(gap_degrees/24.)
    alpha*=smoothstep((elev*180/np.pi+.6)/4.4)
    rgba=np.dstack((np.clip(rgb,0,255),255*alpha)).round().astype('uint8')
    Image.fromarray(rgba).save(OUT,optimize=True)
    y,x=np.nonzero(eligible)
    print(f'{len(x)} directly observed texels / {W*H}; median inter-frame residual {np.median(spread[y,x]):.2f} / 255')
    print('High-confidence angular extent in deg:',np.degrees(az[y,x]).min(),np.degrees(az[y,x]).max(),np.degrees(elev[y,x]).min(),np.degrees(elev[y,x]).max())


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('frames',type=Path)
    bake(parser.parse_args().frames)

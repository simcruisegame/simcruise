from source import *
from shapely.geometry import LineString,Point
from scipy.ndimage import map_coordinates,uniform_filter1d,median_filter
import numpy as np
def response(im,meta,xy):
 b=meta['bbox'];w,h=im.shape[1],im.shape[0]
 ix=(xy[...,0]-b[0])/(b[2]-b[0])*w-.5;iy=(b[3]-xy[...,1])/(b[3]-b[1])*h-.5
 col=np.stack([map_coordinates(im[:,:,i],[iy,ix],order=1) for i in range(3)],axis=2)
 lum=col@np.array([.25,.55,.20]);med=median_filter(lum,size=(1,17));avg=uniform_filter1d(lum,size=3,axis=1);edge=avg-med
 # Narrow physical ridge, bright compared with its immediate left and right shoulders.
 flank=np.maximum((np.roll(lum,7,axis=1)+np.roll(lum,5,axis=1))*.5,(np.roll(lum,-7,axis=1)+np.roll(lum,-5,axis=1))*.5)
 r=np.minimum(edge,avg-flank)
 return r,lum
def profile(imname,meta,line,s0,along,offset):
 scale=meta['scale'];mids=np.array([line.interpolate(s0+s/scale).coords[0] for s in along]);d=np.gradient(mids,axis=0);d=d/np.linalg.norm(d,axis=1)[:,None];n=np.c_[d[:,1],-d[:,0]];xy=mids[:,None,:]+n[:,None,:]*offset[None,:,None]/scale
 image=np.asarray(Image.open(imname).convert('RGB')).astype(float)
 r,lum=response(image,meta,xy)
 return r,lum

from pathlib import Path
import json,sys,math,os
from statistics import median
from PIL import Image,ImageDraw,ImageFont
import numpy as np
from align import read,nearrows,along
p=Path(os.environ.get('SD_SECTION_WORK',Path(__file__).parent));site=Path(os.environ.get('SD_SECTION_SITE',p/'medford-proof'));sel=json.loads((site/'selection.json').read_text());loc=sel['center'];sets=read(sel['key'],loc)
centerrow=next(r for r in nearrows(sets['ma25'],(0,0),loc[2]) if r[1]==sel['result']['trace'][2]['stateId']);xyz=centerrow[3];start=centerrow[4][1]
width=sel['result']['values']['travelWayFt']*.3048/2;right=sel['result']['values']['rightShoulderFt']*.3048;left=sel['result']['values']['leftShoulderFt']*.3048
s=np.arange(-42,62,.20);cross=np.arange(-9,12,.075);stations=[along(xyz,start+st) for st in s];ax=np.array([v[0] for v in stations]);angles=np.deg2rad([v[1] for v in stations]);norm=np.stack([np.cos(angles),-np.sin(angles)],axis=1)
points=ax[:,None,:]+norm[:,None,:]*cross[None,:,None];xx=544+points[:,:,0]*8-.5; yy=544-points[:,:,1]*8-.5
def read_rgb(epoch):
 im=np.array(Image.open(site/(str(epoch)+'.jpg')).convert('RGB')).astype(float)
 x0=xx.astype(int);y0=yy.astype(int);x1=x0+1;y1=y0+1;fx=(xx-x0)[:,:,None];fy=(yy-y0)[:,:,None]
 return ((im[y0,x0]*(1-fx)+im[y0,x1]*fx)*(1-fy)+(im[y1,x0]*(1-fx)+im[y1,x1]*fx)*fy)
def ridge(im,kind):
 red,green,blue=np.moveaxis(im,-1,0);neutral=np.minimum(np.minimum(red,green),blue)
 if kind=='yellow':lum=.55*red+.45*green-.5*blue
 elif kind=='edge':
  # Difference between samples in the road and verge across a 60 cm interval.
  return np.linalg.norm(np.roll(im,4,axis=1)-np.roll(im,-4,axis=1),axis=-1)
 else:lum=neutral
 return lum-(np.roll(lum,-8,axis=1)+np.roll(lum,8,axis=1))*.5
def byrow(mat,t):
 return np.array([np.interp(v,cross,row) for v,row in zip(np.broadcast_to(t,len(s)),mat)])
def top(mat,mask,frac=.25):
 v=np.sort(mat[mask],axis=0);return v[-max(1,int(len(v)*frac)):].mean(axis=0)
epochs={k:read_rgb(k) for k in (2025,2023,2019) if (site/f'{k}.jpg').exists()}
if __name__=='__main__':
 for k,im in epochs.items():
  for lo,hi in [(-40,-24),(-24,-9),(-9,6),(6,21),(21,36),(36,51),(51,60)]:
   flag=(s>=lo)&(s<hi);white=ridge(im,'white')
   def near(kind,window,frac=.5):
    arr=top(ridge(im,kind),flag,frac);idx=np.where((cross>window[0])&(cross<window[1]))[0];return (round(cross[idx[np.argmax(arr[idx])]],2),round(max(arr[idx]),1))
   print(k,lo,hi,'divider',near('white',(-2.75,2.75),.25),'white',near('white',(2.5,7),.55),'yellow',near('yellow',(-6.5,-1.8),.55),'pave',near('edge',(5.5,10),.6))
 im=Image.new('RGB',(1040,492),'white');d=ImageDraw.Draw(im);font=ImageFont.truetype('/System/Library/Fonts/Helvetica.ttc',14)
 for i,(k,data) in enumerate(epochs.items()):
  tex=Image.fromarray(np.uint8(data)).transpose(Image.Transpose.TRANSPOSE).resize((1040,140));im.paste(tex,(0,i*164+22));d.text((9,i*164+3),str(k)+'   −42 m upstream  →  +62 m downstream  | transverse scale −9 m → +12 m',font=font,fill='black')
 im.save(site/'transects.jpg',quality=90)

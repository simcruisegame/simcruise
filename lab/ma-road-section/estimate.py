from transects import *
from collections import Counter
from PIL import Image,ImageFilter
# Relative orthophoto registration from stable background patches, omitting the
# tested carriageway and its adjacent edges altogether.
def register(epoch):
 a=np.asarray(Image.open(site/'2025.jpg').convert('L').filter(ImageFilter.GaussianBlur(.8)),dtype=float);b=np.asarray(Image.open(site/(str(epoch)+'.jpg')).convert('L').filter(ImageFilter.GaussianBlur(.8)),dtype=float)
 def gradient(im):
  x=np.minimum(65,np.hypot(np.diff(im,axis=0,prepend=im[:1]),np.diff(im,axis=1,prepend=im[:,:1])))
  return x
 a,b=gradient(a),gradient(b);votes=[]
 for y in range(66,1020,66):
  for x in range(66,1020,66):
   east=(x-544)*.125;north=(544-y)*.125
   if np.linalg.norm(ax-np.array([east,north]),axis=1).min()<17:continue
   section=(slice(y-32,y+32),slice(x-32,x+32));target=a[section][::2,::2].ravel();target-=target.mean()
   if np.std(target)<4.5:continue
   scores=[]
   for dy in range(-8,9):
    for dx in range(-8,9):
     cand=b[y-32+dy:y+32+dy:2,x-32+dx:x+32+dx:2].ravel();cand-=cand.mean()
     score=np.dot(target,cand)/(np.linalg.norm(target)*np.linalg.norm(cand)+.01);scores.append((score,dx,dy))
   score,dx,dy=max(scores)
   if score>.48:votes.append((dx,dy,score))
 if len(votes)<6:raise ValueError(('Unregistered ortho',epoch,len(votes)))
 pos=np.median(np.array(votes)[:,:2],axis=0);strong=[v for v in votes if np.linalg.norm(v[:2]-pos)<=2.2]
 if len(strong)<6:raise ValueError(('Divergent ortho',epoch,len(votes),len(strong)))
 return [*map(float,np.median(np.array(strong)[:,:2],axis=0)),len(votes),len(strong)]
def shifted_rgb(epoch,offset):
 import transects as tr
 prev=tr.xx.copy(),tr.yy.copy();tr.xx=tr.xx+offset[0];tr.yy=tr.yy+offset[1]
 res=tr.read_rgb(epoch);tr.xx,tr.yy=prev;return res
def scan_peak(mat,mask,desired,reach,percentile):
 weights=np.percentile(mat[mask],percentile,axis=0);ii=np.where(abs(cross-desired)<=reach)[0];i=ii[np.argmax(weights[ii])]
 return float(cross[i]),float(weights[i])
def tofloat(x):return float(x)
if __name__=='__main__':
 reg={2025:[0,0,0,0]}
 for epoch in [k for k in (2023,2019) if k in epochs]:
  try:reg[epoch]=register(epoch)
  except ValueError as e:
   if epoch==2023:raise
   print('Epoch unregistered',e,flush=True);reg[epoch]=[0,0,0,0]
 print('Registration: relative pixel offsets and accepted/tightly agreeing background patches',reg,flush=True)
 ims={k:shifted_rgb(k,r) for k,r in reg.items()}
 scores={k:{'yellow':ridge(im,'yellow'),'white':ridge(im,'white'),'edge':ridge(im,'edge')} for k,im in ims.items()}
 # Fit upstream using recent two acquisitions. A recurring white edge plus an
 # inboard repeating dash and a yellow left boundary must form the two reported travel lanes.
 all_knots=[]
 for pos in np.arange(-36,6,8):
  flag=(s>=pos-4)&(s<pos+4);combined=[]
  for c0 in np.arange(-3.0,3.01,.075):
   pieces=[]
   for k in [2025,2023]:
    rs=scores[k]
    e=np.median(byrow(rs['white'],c0+width)[flag])
    y=np.median(byrow(rs['yellow'],c0-width)[flag])
    d=np.percentile(byrow(rs['white'],c0)[flag],85)
    pieces.append((e,y,d))
   combined.append((sum(min(50,max(-15,e))+min(24,max(-8,y))+.24*min(40,max(-10,d)) for e,y,d in pieces),c0,pieces))
  val,c0,pieces=max(combined)
  print('fit',pos,round(c0,2),np.round(pieces,1).tolist());all_knots.append((pos,c0))
 # 1D linear model over this short portion of a surveyed curved axis. Only upstream samples fit.
 knots=np.array(all_knots);slope,bias=np.polyfit(knots[:,0],knots[:,1],1);print('FIT correction',bias,slope)
 at=slope*s+bias
 for epoch in reg:
  print('epoch',epoch)
  for pos in np.arange(-31,59,8):
   flag=(s>pos-4)&(s<pos+4);c0=slope*pos+bias;rs=scores[epoch]
   result=[scan_peak(rs['white'],flag,c0+width,.90,50),scan_peak(rs['yellow'],flag,c0-width,.90,50),scan_peak(rs['edge'],flag,c0+width+right,1.65,50),scan_peak(rs['edge'],flag,c0-width-left,1.15,50)]
   print(round(pos),round(c0,2),np.round(result,2).tolist())
 (site/'registration.json').write_text(json.dumps({'offset':reg,'knots':all_knots,'bias':bias,'slope':slope},indent=2,default=float))

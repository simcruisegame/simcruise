# Grass field prototype: a driver's-eye render of a roadside lawn, three ways.
#  A "now": the game's method (one 640 px CC0 photo at 2.3 m and 9.7 m).
#  B "field": no-repeat hex tiling of the same photo + a meso field (clumps, wear, dry
#     patches, weeds, kerb strip) driven by graph facts + parallax blade shells near the car.
import sys, math, numpy as np
from PIL import Image
W,H=1280,540; EYE=1.1; PITCH=math.radians(-6); FOV=math.radians(74)
tex=np.asarray(Image.open(sys.argv[1]).convert("RGB").resize((512,512),Image.LANCZOS)).astype(np.float32)/255; tex=tex**2.2
T=tex.shape[0]; mean=tex.reshape(-1,3).mean(0)
# mip pyramid
mips=[tex]
while mips[-1].shape[0]>4:
    a=mips[-1]; mips.append(0.25*(a[0::2,0::2]+a[1::2,0::2]+a[0::2,1::2]+a[1::2,1::2]))
def sample(u,v,foot):  # u,v in texture repeats; foot = texels per pixel
    lvl=np.clip(np.log2(np.maximum(foot,1)),0,len(mips)-1)
    l0=np.floor(lvl).astype(int); f=(lvl-l0)[...,None]; out=0
    for k,wt in ((0,1-f),(1,f)):
        li=np.minimum(l0+k,len(mips)-1); res=np.zeros(u.shape+(3,),np.float32)
        for L in np.unique(li):
            m=li==L; M=mips[L]; n=M.shape[0]
            x=(np.floor((u[m]%1)*n).astype(int))%n; y=(np.floor((v[m]%1)*n).astype(int))%n
            res[m]=M[y,x]
        out=out+wt*res
    return out
# camera rays -> ground plane
j,i=np.mgrid[0:H,0:W].astype(np.float32)
fx=(W/2)/math.tan(FOV/2)
dx=(i-W/2)/fx; dy=-(j-H/2)/fx
# forward along +y, up z; pitch rotates
dz=dy*math.cos(PITCH)+math.sin(PITCH); dyw=-dy*math.sin(PITCH)+math.cos(PITCH)
ground=dz<-1e-4
t=np.where(ground,EYE/np.maximum(-dz,1e-6),0)
X=dx*t; Y=dyw*t; dist=t*np.sqrt(dx*dx+dyw*dyw+dz*dz)
foot_m=dist/fx/np.maximum(-dz/np.sqrt(dx*dx+dyw*dyw+dz*dz),0.02)   # metres per pixel along the ground
X=X+1.5   # the car is 1.5 m left of the verge's kerb at x=0 ... lawn is x>0
def hsh(a,b,s=0):
    h=np.sin(a*127.1+b*311.7+s*74.7)*43758.5453; return h-np.floor(h)
def vnoise(x,y,s=0):
    xi,yi=np.floor(x),np.floor(y); fx_,fy_=x-xi,y-yi; ux,uy=fx_*fx_*(3-2*fx_),fy_*fy_*(3-2*fy_)
    a,b,c,d=hsh(xi,yi,s),hsh(xi+1,yi,s),hsh(xi,yi+1,s),hsh(xi+1,yi+1,s)
    return a+(b-a)*ux+(c-a)*uy+(a-b-c+d)*ux*uy
def fbm(x,y,o=4,s=0):
    v=0;a=0.5
    for k in range(o): v+=a*vnoise(x,y,s+k); x,y=x*2.03+1.7,y*2.03+9.2; a*=0.5
    return v/(1-0.5**o)
def now():
    g1=sample(X/2.3,Y/2.3,foot_m/2.3*T)/mean; g2=sample(X/9.7+.31,Y/9.7+.31,foot_m/9.7*T)/mean
    g=g1*(1+(g2-1)*0.5)
    base=np.array([0.052,0.088,0.026])
    return base*(1+(g-1)*0.55*1)   # as the ground shader: detail weight 0.55 on veg
def hexsample(x,y,scale,foot):
    # Mikkelsen-style hex tiling: three random-offset, rotated lookups blended per hex cell
    s=x/scale; t=y/scale
    q=s*1.1547; r=t+0.5*q   # skew
    qi,ri=np.floor(q),np.floor(r); fq,fr=q-qi,r-ri
    out=0; wsum=0
    for (aq,ar) in ((0,0),(1,0),(0,1),(1,1)):
        cq,cr=qi+aq,ri+ar
        w=np.maximum(0,1-np.hypot(fq-aq,fr-ar)*0.9)**4
        ang=hsh(cq,cr,3)*6.283; ox,oy=hsh(cq,cr,5),hsh(cq,cr,7)
        u=s*np.cos(ang)-t*np.sin(ang)+ox; v=s*np.sin(ang)+t*np.cos(ang)+oy
        out=out+w[...,None]*sample(u,v,foot/scale*T); wsum=wsum+w
    col=out/np.maximum(wsum,1e-4)[...,None]
    # restore variance lost by blending (histogram-preserving trick, linearised)

    return col
def field(facts):
    g=hexsample(X,Y,1.7,foot_m)/mean
    # meso field: clumps (0.3-1 m), patch colour (2-8 m), wear near the kerb, dry patches, weeds
    clump=fbm(X*2.2,Y*2.2,3,11); patch=fbm(X/5,Y/5,3,23); dry=np.clip((fbm(X/7,Y/7,3,31)-(0.62-0.25*facts['dry']))*3,0,1)*facts['dry']
    kerb=np.exp(-np.maximum(X,0)/0.35)    # the 30 cm strip at the kerb: longer, darker, trodden at the edge
    weeds=np.clip((fbm(X*3,Y*3,2,41)-0.70)*12,0,1)*facts['weeds']
    green=np.array([0.050,0.090,0.024]); yellowgreen=np.array([0.085,0.105,0.035]); straw=np.array([0.16,0.13,0.07]); clover=np.array([0.034,0.070,0.018]); soil=np.array([0.09,0.07,0.05])
    base=green[None,None]*(1-patch[...,None]*0.6)+yellowgreen*(patch[...,None]*0.6)
    base=base*(0.75+0.5*clump[...,None])
    base=base*(1-dry[...,None])+straw*dry[...,None]
    base=base*(1-weeds[...,None])+clover*weeds[...,None]
    wear=np.clip(kerb*0.8+(fbm(X*0.8,Y*0.8,2,51)>0.8)*facts['wear'],0,1)[...,None]
    base=base*(1-0.5*wear)+soil*0.5*wear
    col=base*(1+(g-1)*0.65)
    # parallax blade shells near the car: march 8 shells from 6 cm down; first hit decides
    near=dist<22
    Hb=0.08*facts['height']; hit=np.zeros(X.shape,bool); out=col.copy()
    sx=dx/np.maximum(-dz,1e-6); sy=dyw/np.maximum(-dz,1e-6)
    for k in range(8):
        z=Hb*(1-(k+0.5)/8)
        px=X+sx*z; py=Y+sy*z
        dens=vnoise(px*40,py*40,61)*0.75+vnoise(px*90,py*13,62)*0.25+clump*0.3-0.1     # blades: thin, clumped
        blade=(dens>(0.35+0.6*z/Hb))&near&~hit&(px>0)
        tipcol=col*(1.15+0.4*(z/Hb))+np.array([0.010,0.008,0.0])*(z/Hb)
        out=np.where(blade[...,None],tipcol*(0.35+0.75*z/Hb),out)   # base of the blade is in its own shade
        hit|=blade
    out=np.where((near&~hit)[...,None],col*0.35,out)   # seen between blades: shaded thatch
    return np.where(near[...,None],out,col)
def shade(col,name):
    sun=3.2; sky=np.array([0.55,0.62,0.8])*0.9
    lit=col*(sun+sky)
    # road: plain asphalt for x<0, sky above
    road=(X<0)&ground
    lit=np.where(road[...,None],np.array([0.10,0.10,0.10])*(sun+sky).mean()*np.ones(3),lit)
    lit=np.where(ground[...,None],lit,np.array([0.55,0.70,0.95])*1.6)
    img=1-np.exp(-lit*1.1); img=img**(1/2.2)
    Image.fromarray((np.clip(img,0,1)*255).astype(np.uint8)).save(name)
shade(now(),'A-now.png')
shade(field({'dry':0.3,'weeds':0.5,'wear':0.5,'height':1.0}),'B-field.png')
shade(field({'dry':1.0,'weeds':0.2,'wear':0.4,'height':1.4}),'C-field-dormant.png')

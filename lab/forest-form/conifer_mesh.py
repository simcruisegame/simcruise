"""A three-dimensional tapered conifer crown with whorled, folded needle-bearing branches."""
import math
from pathlib import Path
import random
import sys
from PIL import Image, ImageDraw, ImageOps

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from crown_clusters import export, normalize, add_cards
DATA=Path(__file__).resolve().parents[2]/"game"/"data"


def needle_fan():
    src=Image.open(DATA/"spray-pine.png").convert("RGBA")
    alpha=src.getchannel("A")
    grey=ImageOps.autocontrast(ImageOps.grayscale(src),cutoff=1,mask=alpha.point(lambda v:255 if v>170 else 0))
    src=ImageOps.colorize(grey,(27,33,24),(151,158,110)).convert("RGBA")
    src.putalpha(alpha)
    rng=random.Random(250)
    im=Image.new("RGBA",(192,256))
    draw=ImageDraw.Draw(im)
    draw.line([(98,251),(92,130),(100,8)],fill=(92,84,61,255),width=3)
    for j in range(23):
        y=239-j*10
        t=(j+.5)/23
        span=math.sin(math.pi*t)**.8 * (1-.32*t)
        for side in (-1,1):
            angle=side*(28+34*span+rng.uniform(-17,17))
            w=round(5+rng.uniform(8,15)*span);h=round(24+rng.uniform(41,72)*span)
            piece=src.resize((w,h),Image.Resampling.LANCZOS).rotate(-angle,resample=Image.Resampling.BICUBIC,expand=True)
            x=96+math.sin(math.radians(angle))*.46*h;cy=y-math.cos(math.radians(angle))*.46*h
            im.alpha_composite(piece,(round(x-piece.width*.5),round(cy-piece.height*.5)))
    im.save(DATA/"forest-needle-fan.png",optimize=True)


def surface():
    return [],[],[],[],[]


def tube(mesh,start,end,r0,r1,sides):
    p,n,uv,col,idx=mesh
    base=len(p)
    dx,dy,dz=normalize(tuple(e-s for e,s in zip(end,start)))
    if abs(dy)>.95:u=(1,0,0)
    else:u=normalize((-dz,0,dx))
    v=(dy*u[2]-dz*u[1],dz*u[0]-dx*u[2],dx*u[1]-dy*u[0])
    for level,(pivot,r) in enumerate(((start,r0),(end,r1))):
        for k in range(sides):
            a=math.tau*k/sides
            d=tuple(math.cos(a)*u[i]+math.sin(a)*v[i] for i in range(3))
            p.append(tuple(pivot[i]+r*d[i] for i in range(3)));n.append(d);uv.append((k/sides,level));col.append((1,1,1,1))
    for k in range(sides):
        i=base+k;j=base+(k+1)%sides
        idx.extend((i,i+sides,j,j,i+sides,j+sides))


def mesh(lod):
    wood,needles=surface(),surface()
    tube(wood,(0,0,0),(.005,1.,-.004),.018, .0008,7 if lod<2 else 4)
    p,n,uv,col,idx=needles
    randomizer=random.Random(7310)
    core_sites=[]
    total=22
    for j in range(total):
        t=j/(total-1)
        # An open lower bole and taper to one terminal leader. Radius is normalized below.
        y=.13+.844*t
        radius=.52*(1.-t)**.86*min(1.,.58+3.8*t)+.014
        count=round(12-6*t)
        azimuth=randomizer.random()*math.tau
        for k in range(count):
            a=azimuth+math.tau*(k+randomizer.uniform(-.20,.20))/count
            rad=radius*randomizer.uniform(.83,1.10)
            root_y=y+randomizer.uniform(-.037,.037)*(1.-t)
            droop=randomizer.uniform(-.08,.13)*(1.-.77*t)
            tilt=randomizer.uniform(-.98,.98)
            plunge=randomizer.uniform(.14,.46)
            lateral=randomizer.uniform(-.14,.14)*rad
            light=randomizer.uniform(.74,1.08)
            if lod==2 and j%4==1: continue
            if lod==1 and j%5==1: continue
            ca,sa=math.cos(a),math.sin(a)
            if k%2==0 and j%2==0 and (lod<2 or (j%4==0 and k%4==0)):
                # A dark, porous interior sits behind separate outer needle sprays.
                extent=.31*rad
                core_sites.append(((ca*rad*.46,y-droop*.28,sa*rad*.46),extent,.11*(1.-.78*t),
                                   a,.62+.27*t,k%4))
            if lod==0:
                tube(wood,(.003,root_y,0),(ca*rad*.84,root_y-droop*.79,sa*rad*.84),.0035*(1.-.70*t),.0005,3)
            base=len(p)
            # Each branch is a folded fan. At the crest of the fold needles sit
            # higher than either edge; the ends lift without forming horizontal tiers.
            for s in ((0.,1.) if lod==2 else (0.,.52,1.)):
                lateral_offset=lateral*math.sin(s*math.pi)
                center_y=root_y-droop*(1.38*s-.55*s*s)+.006*t*s
                half_width=(.42 if lod==2 else .25+.26*math.sin(math.pi*(.04+s*.87)))*rad
                for q in (-1,0,1):
                    side=half_width*q+lateral_offset
                    py=center_y+q*half_width*tilt-abs(q)*half_width*plunge
                    p.append((ca*rad*s-sa*side+.005*t,py,sa*rad*s+ca*side-.004*t))
                    n.append(normalize((ca*.56-sa*q*.43,.65+ .20*(q==0),sa*.56+ca*q*.43)))
                    uv.append(((q+1)*.5,1.-s))
                    shade=(.74+.23*s)*light
                    col.append((shade,shade,shade,1.))
            for i in ((0,1) if lod==2 else (0,1,3,4)):
                idx.extend((base+i,base+i+3,base+i+1,base+i+1,base+i+3,base+i+4))
    pts=needles[0]
    span=max(max(p[a] for p in pts)-min(p[a] for p in pts) for a in (0,2))
    core=add_cards(core_sites,"pine",lod)
    for surf in [wood,needles,core]:
        # Raise the leaf-bearing branches above the bole with a continuous remap.
        # The tapered main trunk alone runs from the original anchor to the measured top.
        trunk_vertices = (14 if lod<2 else 8) if surf is wood else 0
        surf[0][:]=[(x/span,max(0.,min(1.,y)) if k<trunk_vertices else .165+.835*y,z/span) for k,(x,y,z) in enumerate(surf[0])]
    export(DATA/f"forest-conifer-{lod}.meshdata",[wood,needles,core])
    print(lod,sum(len(s[4])//3 for s in (wood,needles,core)))


if __name__=="__main__":
    needle_fan()
    for lod in range(3):mesh(lod)

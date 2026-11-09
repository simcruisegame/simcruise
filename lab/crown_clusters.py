"""Bake woodland leaf clusters using the bundled botanical photographs and branch geometry.

Run from the project root with Python 3 and Pillow. The same branch endpoints and cluster
centres drive every detail level so driving towards a tree does not change its branching.
"""
from array import array
import json
import math
from pathlib import Path
import random
import struct
from PIL import Image, ImageOps

DATA = Path("game/data")
ART = Path("game/art")


def make_texture(species):
    rng = random.Random(1142 if species == 'leaf' else 702)
    source = Image.open(DATA / f'spray-{species}.png').convert('RGBA')
    alpha = source.getchannel('A')
    grey = ImageOps.grayscale(source)
    low = (26, 43, 29) if species == 'pine' else (30, 43, 23)
    high = (122, 145, 84) if species == 'pine' else (143, 159, 74)
    # Retain the photographed variation within each illuminated leaf or needle.
    grey = ImageOps.autocontrast(grey, cutoff=1, mask=alpha.point(lambda x: 255 if x > 192 else 0))
    source = ImageOps.colorize(grey, low, high).convert('RGBA')
    source.putalpha(alpha)
    atlas = Image.new('RGBA', (768, 768))
    for quadrant in range(4):
        span = 384
        out = Image.new('RGBA', (span, span))
        stems = []
        # Local sprays grow in neighboring fans. Leave irregular sky gaps between them.
        fans = []
        for i in range(8):
            angle = math.tau*i/8 + rng.uniform(-.18,.18)
            r = rng.uniform(.22,.32)
            fans.append((.50+math.cos(angle)*r, .49+math.sin(angle)*r, rng.uniform(.81,1.20)))
        fans.extend([(.43,.44,1.16),(.56,.54,.87)])
        for layer in range(3):
            count = (33, 40, 32)[layer]
            for i in range(count):
                fx,fy,fan_light = rng.choice(fans)
                angle = rng.uniform(0, math.tau)
                r = math.sqrt(rng.random())
                x = fx + math.cos(angle)*r*.117
                y = fy + math.sin(angle)*r*.105
                heading = -math.degrees(math.atan2(x-.50,.71-y)) + rng.uniform(-47,47)
                length = rng.uniform(.16,.27) * (1.13 if species == 'pine' else 1.)
                shade = (.46 + .18*layer) * fan_light * (1.16-(y-.35)*.58)*rng.uniform(.68,1.39)
                stems.append((layer, y, x, heading, length, shade))
        for layer,y,x,heading,length,shade in sorted(stems, key=lambda s:(s[0],-s[1])):
            w = int(span*length*rng.uniform(.56,.86))
            h = int(span*length)
            im = source.resize((w,h), Image.Resampling.LANCZOS)
            bands = [c.point(lambda v:round(min(v*shade,255))) for c in im.split()[:3]]
            im = Image.merge('RGBA', [*bands,im.getchannel('A')])
            im = im.rotate(heading, Image.Resampling.BICUBIC, expand=True)
            out.alpha_composite(im,(round(x*span-im.width/2), round(y*span-im.height/2)))
        atlas.paste(out,((quadrant%2)*span,(quadrant//2)*span))
    atlas.save(DATA / f'cluster-{species}.png', optimize=True)


def load_mesh(path):
    data = path.read_bytes()
    size = struct.unpack_from('<I',data,12)[0]
    doc = json.loads(data[20:20+size])
    binary = data[28+size:]
    def accessor(i):
        a=doc['accessors'][i]
        v=doc['bufferViews'][a['bufferView']]
        n={'SCALAR':1,'VEC2':2,'VEC3':3}[a['type']]
        fmt = '<' + {5126:'f',5125:'I',5123:'H'}[a['componentType']]*n
        pos=v.get('byteOffset',0)+a.get('byteOffset',0)
        stride=v.get('byteStride',struct.calcsize(fmt))
        return [struct.unpack_from(fmt,binary,pos+j*stride) for j in range(a['count'])]
    out = []
    for p in doc['meshes'][0]['primitives']:
        attrs=p['attributes']
        out.append((accessor(attrs['POSITION']),accessor(attrs['TEXCOORD_0']),[v[0] for v in accessor(p['indices'])]))
    return out


def normalize(v):
    length = max(.000001,math.sqrt(sum(i*i for i in v)))
    return tuple(i/length for i in v)


def leaf_points(kind):
    if kind == 'oak':
        positions, uv, _ = load_mesh(ART/'oak0.glb')[0]
        return [(x/7.75,y/8.61,z/7.75) for (x,y,z),q in zip(positions,uv) if q[0]>.52]
    pos,_,_ = load_mesh(DATA/f'wood-{kind}-0.meshdata')[1]
    return pos


def cluster_sites(kind):
    rng = random.Random(2743)
    points = leaf_points(kind)
    points=rng.sample(points,min(1650,len(points)))
    count = {'oak':37,'leaf':35,'pine':42}[kind]
    centers=[rng.choice(points)]
    def distance(a,b):
        return (a[0]-b[0])**2+(a[1]-b[1])**2*1.25+(a[2]-b[2])**2
    far=[100.]*len(points)
    for _ in range(1,count):
        far=[min(f,distance(p,centers[-1])) for p,f in zip(points,far)]
        centers.append(points[max(range(len(far)),key=far.__getitem__)])
    for _ in range(6):
        groups=[[] for _ in centers]
        for p in points:
            i=min(range(count),key=lambda i:distance(p,centers[i]))
            groups[i].append(p)
        centers=[tuple(sum(v[k] for v in g)/len(g) for k in range(3)) if g else c for c,g in zip(centers,groups)]
    sites=[]
    for j,((x,y,z),g) in enumerate(zip(centers,groups)):
        # The envelope surrounds the leafy twigs and naturally hollows beneath the lowest limbs.
        ext=[max(.048, sorted(abs(p[k]-c) for p in g)[int(len(g)*.80)]) for k,c in enumerate((x,y,z))]
        width=max(.085,min(.215,max(ext[0],ext[2]) * 1.63))
        limb=max(.082,min(.153,ext[1]*1.72))
        if kind=='pine':
            width*=1.05
            limb*=.90
        sites.append(((x,y,z),width,limb,rng.uniform(0,math.tau),rng.uniform(.77,1.05),j%4))
    return sites


def add_cards(sites,kind,lod):
    pos,nor,uv,col,indices=[],[],[],[],[]
    for number,((x,y,z),width,height,phase,brightness,quadrant) in enumerate(sites):
        # Inclined, curved branch fans avoid the upright cut-paper edges of crossed tree cards.
        tilt = .34*math.sin(number*1.731) + (.30 if kind=='pine' else .08)
        planes=(4,3,3)[lod]
        for face in range(planes):
            angle=phase+face*math.pi/planes
            c,s=math.cos(angle), math.sin(angle)
            # A recessed centre and drooping corners form a folded spray with a real depth profile.
            base=len(pos)
            for row,v in enumerate((-1.,0.,1.)):
                for column,u in enumerate((-1.,0.,1.)):
                    oy = v*height*1.37 - abs(u)*height*.19 + u*math.sin(phase+face)*height*.35
                    depth=width*(.50*(1.-abs(u))+(tilt+.14*face)*v+(.10 if face%2==0 else -.10))
                    px=x+c*u*width*1.54-s*depth
                    pz=z+s*u*width*1.54+c*depth
                    py=y+oy
                    pos.append((px,py,pz))
                    # Rounded crown normals keep neighboring sheets under consistent sunlight.
                    nor.append(normalize((x*.70+(px-x)*2.0,(y-.48)*1.50+(py-y)*2.4+.09,z*.70+(pz-z)*2.0)))
                    shade=(.63+.37*min(1,max(0,(y-.16)/.80)))*brightness*(.90+.10*(v+1)*.5)
                    col.append((shade,shade,shade,1.))
                    uv.append(((quadrant%2+(u+1)*.5)*.5,(quadrant//2+(1.-v)*.5)*.5))
            if lod==2:
                indices.extend(base+k for k in [0,2,4,2,8,4,8,6,4,6,0,4])
            else:
                for q in (0,1,3,4):
                    k=base+q
                    indices.extend([k,k+1,k+3,k+1,k+4,k+3])
    return pos,nor,uv,col,indices


def distant_wood(kind):
    # Only the trunk and primary forks project more than a pixel at this distance.
    points=[];normals=[];uv=[];indices=[]
    def tube(a,b,r0,r1):
        axis=normalize(tuple(b[k]-a[k] for k in range(3)))
        tangent=normalize((axis[1],-axis[0],0.))
        side=(axis[1]*tangent[2]-axis[2]*tangent[1],
              axis[2]*tangent[0]-axis[0]*tangent[2],
              axis[0]*tangent[1]-axis[1]*tangent[0])
        start=len(points)
        for pt,r in ((a,r0),(b,r1)):
            for j in range(5):
                angle=j*math.tau/5
                n=tuple(tangent[k]*math.cos(angle)+side[k]*math.sin(angle) for k in range(3))
                points.append(tuple(pt[k]+n[k]*r for k in range(3)))
                normals.append(n);uv.append((j/5,pt[1]))
        for j in range(5):
            next_j=(j+1)%5
            indices.extend([start+j,start+next_j,start+j+5,start+next_j,start+next_j+5,start+j+5])
    trunk=[(0.,0.,0.),(.009,.22,-.006),(.014,.43,.001),(.033,.68,.019),(.013,.93,.032)]
    radii=(.018,.015,.010,.006,.001)
    for i in range(4):tube(trunk[i],trunk[i+1],radii[i],radii[i+1])
    for i in range(8):
        angle=2.39996*i; y=.28+i*.052
        a=(.014,y,.001); b=(math.cos(angle)*.14,y+.065,math.sin(angle)*.14)
        tip=(math.cos(angle)*.24,y+(.08 if kind=='pine' else .19),math.sin(angle)*.24)
        tube(a,b,.007,.004);tube(b,tip,.004,.0007)
    return points,normals,uv,None,indices


def wood(kind,lod):
    if kind=='oak':
        p,uv,idx=load_mesh(ART/f'oak{lod}.glb')[0]
        mask=[q[0]<.50 for q in uv]
        idx=[i for n in range(0,len(idx),3) if all(mask[k] for k in idx[n:n+3]) for i in idx[n:n+3]]
        p=[(x/7.75,y/8.61,z/7.75) for x,y,z in p]
    else:
        p,uv,idx=load_mesh(DATA/f'wood-{kind}-{lod}.meshdata')[0]
    use=sorted(set(idx))
    new={v:i for i,v in enumerate(use)}
    p=[p[i] for i in use]
    uv=[uv[i] for i in use]
    idx=[new[i] for i in idx]
    nor=[[0.,0.,0.] for _ in p]
    for j in range(0,len(idx),3):
        a,b,c=[p[i] for i in idx[j:j+3]]
        x,y,z=[b[i]-a[i] for i in range(3)]
        u,v,w=[c[i]-a[i] for i in range(3)]
        normal=normalize((y*w-z*v,z*u-x*w,x*v-y*u))
        for i in idx[j:j+3]:
            for k in range(3):nor[i][k]+=normal[k]
    return p,[normalize(n) for n in nor],uv,None,idx


def export(path,surfaces):
    blob=bytearray(); views=[]; accessors=[]; prims=[]
    def attribute(rows,fmt,width,index=False):
        n=len(rows)
        values=array(fmt,rows if index else (v for row in rows for v in row)).tobytes()
        views.append(dict(buffer=0,byteOffset=len(blob),byteLength=len(values),target=34963 if index else 34962))
        blob.extend(values);blob.extend(bytes(-len(blob)%4))
        acc=dict(bufferView=len(views)-1,componentType=5126 if fmt=='f' else 5125,count=n,type='SCALAR' if index else f'VEC{width}')
        if width==3:
            acc.update(min=list(map(min,zip(*rows))),max=list(map(max,zip(*rows))))
        accessors.append(acc)
        return len(accessors)-1
    for pos,normal,uv,colors,ind in surfaces:
        attrs=dict(POSITION=attribute(pos,'f',3),NORMAL=attribute(normal,'f',3),TEXCOORD_0=attribute(uv,'f',2))
        if colors: attrs['COLOR_0']=attribute(colors,'f',4)
        prims.append(dict(attributes=attrs,indices=attribute(ind,'I',1,True),material=len(prims)))
    doc=dict(asset=dict(version='2.0'),scene=0,scenes=[dict(nodes=[0])],nodes=[dict(mesh=0)],meshes=[dict(primitives=prims)],materials=[dict(doubleSided=True) for _ in prims],buffers=[dict(byteLength=len(blob))],bufferViews=views,accessors=accessors)
    js=json.dumps(doc,separators=(',',':')).encode();js+=b' '*(-len(js)%4)
    path.write_bytes(struct.pack('<4sII',b'glTF',2,28+len(js)+len(blob))+struct.pack('<I4s',len(js),b'JSON')+js+struct.pack('<I4s',len(blob),b'BIN\0')+blob)


if __name__=='__main__':
    for species in ('leaf','pine'): make_texture(species)
    for kind in ('oak','leaf','pine'):
        sites=cluster_sites(kind)
        for level in range(3):
            cards=add_cards(sites,kind,level)
            # Close-up bark and individually modelled street oak leaves are already in the source asset.
            surfaces=[cards] if level==0 else [wood(kind,level) if level==1 else distant_wood(kind),cards]
            export(DATA/f'crown-{kind}-{level}.meshdata',surfaces)

    # A low thicket shares the leaf atlas and keeps its own sparse, spreading silhouette.
    rng=random.Random(455)
    sites=[]
    for i in range(11):
        angle=i*2.39996
        r=.09+.22*(i/10)
        sites.append(((math.cos(angle)*r,.28+.33*(1-i/10)+rng.uniform(-.11,.11),math.sin(angle)*r),
                      rng.uniform(.12,.18),rng.uniform(.15,.21),angle,rng.uniform(.83,1.12),i%4))
    export(DATA/'forest-understory.meshdata',[add_cards(sites,'leaf',2)])

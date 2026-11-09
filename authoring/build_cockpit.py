"""Build the original, metre-scale NB cockpit. Requires Pillow and NumPy.

Black-interior reference: https://bringatrailer.com/listing/1999-mazda-mx-5-miata-205/
No pixels from the reference are used. Vertex groups are batched by material.
"""
import io
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'game/data/nb-cockpit.meshdata'
FONT = ROOT / 'game/art/ui.ttf'
vertices = {}
placements = {}
textures = {}
materials = {}
group = 'Interior'
pi, sin, cos, exp = math.pi, math.sin, math.cos, math.exp


def material(name, color, roughness, metallic=0, texture=None):
    rgb = [int(color[k:k+2], 16) / 255 for k in (0, 2, 4)]
    rgb = [c/12.92 if c <= 0.04045 else ((c+.055)/1.055)**2.4 for c in rgb]
    materials[name] = (rgb, roughness, metallic, texture)


def grid(material_name, rows, columns, fn, uv=lambda u,v:(u,v), reverse=False):
    pts = np.array([[fn(i/columns,j/rows) for i in range(columns+1)] for j in range(rows+1)])
    key = (group, material_name)
    pos, norm, tex, idx = vertices.setdefault(key, ([],[],[],[]))
    base = len(pos)
    dx = np.gradient(pts,axis=1)
    dy = np.gradient(pts,axis=0)
    normals = np.cross(dx,dy) * (-1 if reverse else 1)
    normals /= np.maximum(np.linalg.norm(normals,axis=2)[:,:,None],1e-10)
    pos.extend(pts.reshape(-1,3)); norm.extend(normals.reshape(-1,3))
    for j in range(rows+1):
        for i in range(columns+1):
            tex.append(uv(i/columns,j/rows))
    for j in range(rows):
        for i in range(columns):
            a = base+j*(columns+1)+i
            b,c,d = a+1,a+columns+1,a+columns+2
            idx.extend((a,c,b,b,c,d) if reverse else (a,b,c,b,d,c))


def polyline(material_name, points, radius, sides=8):
    """A rounded extrusion following a curve. Cross sections stay in a coherent frame."""
    pts = np.asarray(points)
    def section(u,v):
        i = round(v*(len(pts)-1))
        tangent = pts[min(len(pts)-1,i+1)]-pts[max(0,i-1)]
        tangent = tangent/np.linalg.norm(tangent)
        # Most curves run in XY. Vertical depth curves use a different reference.
        reference = (0,0,1) if abs(tangent[2])<.94 else (1,0,0)
        right = np.cross(tangent,reference); right/=np.linalg.norm(right)
        up = np.cross(tangent,right)
        return pts[i]+radius*(right*cos(2*pi*u)+up*sin(2*pi*u))
    grid(material_name,len(pts)-1,sides,section)


def rounded_rect(cx,cy,w,h,r):
    points=[]
    for x,y,a in [(1,1,0),(-1,1,90),(-1,-1,180),(1,-1,270)]:
        for t in np.linspace(a,a+90,9):
            points.append((cx+x*(w/2-r)+r*cos(t*pi/180),cy+y*(h/2-r)+r*sin(t*pi/180)))
    return np.array(points)


def front_panel(mat, outline, z, bevel=.003, depth=.012, crown=0):
    """Soft-edged moulding extruded towards the driver, with a planar UV-mapped face."""
    outline=np.array(outline)
    center=outline.mean(axis=0); delta=outline-center; half=np.ptp(outline,axis=0)/2
    scales=[(1-bevel/half,-depth),(np.ones(2),-depth*.64),(np.ones(2),-bevel),(1-bevel/half,0)]
    def point(u,v):
        xy=delta[round(u*len(outline))%len(outline)]
        s,d=scales[round(v*3)]
        return (*list(center+xy*s),z+d)
    grid(mat,3,len(outline),point)
    inner=delta*scales[-1][0]
    def facept(u,v):
        xy=inner[round(u*len(outline))%len(outline)]*v
        return (*list(center+xy),z+crown*(1-v*v))
    def faceuv(u,v):
        xy=inner[round(u*len(outline))%len(outline)]*v/(2*half)
        return (.5+xy[0],.5-xy[1])
    grid(mat,6 if crown else 1,len(outline),facept,faceuv,True)


def ellipse(cx,cy,rx,ry,n=64):
    return [(cx+rx*cos(a*2*pi/n),cy+ry*sin(a*2*pi/n)) for a in range(n)]


def face(mat,cx,cy,z,w,h):
    grid(mat,1,1,lambda u,v:(cx+(u-.5)*w,cy+(v-.5)*h,z),lambda u,v:(u,1-v))


def draw_text(draw, x, y, text, size, fill, anchor='mm'):
    draw.text((x,y), text, font=ImageFont.truetype(str(FONT),size), fill=fill, anchor=anchor)


def gauge(kind):
    img=Image.new('RGB',(384,384),(39,39,41)); d=ImageDraw.Draw(img)
    def point(degrees,r):
        a=math.radians(degrees)
        return (192+r*sin(a),192-r*cos(a))
    main=kind in ('Tach','Speed')
    dial=(231,232,225)
    start,end=(-130,130) if main else (-54,54)
    d.ellipse((8,8,376,376),fill=(43,43,45),outline=(70,69,66),width=2)
    if main:
        tach=kind=='Tach'
        divisions=8 if tach else 15
        segments=divisions*5
        if tach:
            red_start=start+(end-start)*7/8
            for a in np.linspace(red_start,end,100):
                d.line([point(a,116),point(a,163)],fill=(182,55,44),width=3)
        for k in range(segments+1):
            a=start+(end-start)*k/segments
            d.line([point(a,165),point(a,144 if k%5==0 else 156)],fill=dial,width=4 if k%5==0 else 2)
        units=range(9) if tach else range(0,141,20)
        for number in units:
            a=start+(end-start)*number/(8 if tach else 150)
            draw_text(d,*point(a,122),str(number),35 if tach else 31,dial)
        if tach:
            draw_text(d,192,264,'x1000r/min',18,dial)
        else:
            for number in range(20,241,20):
                a=start+(end-start)*(number/1.609344)/150
                d.line([point(a,100),point(a,94)],fill=dial,width=2)
                draw_text(d,*point(a,77),str(number),18,dial)
            draw_text(d,192,253,'km/h',14,dial)
            draw_text(d,192,273,'mph',20,dial)
    else:
        for a in (start,0,end):
            d.line([point(a,144),point(a,122)],fill=dial,width=7)
        for a,letter in [(start,{'Fuel':'E','Oil':'L','Temp':'C'}[kind]),(end,{'Fuel':'F','Oil':'H','Temp':'H'}[kind])]:
            draw_text(d,*point(a,100),letter,47,dial)
        # Simple factory-style icons are drawn here from geometric primitives.
        c=(215,216,209)
        if kind=='Oil':
            d.line([(158,143),(174,163),(208,163),(231,142),(215,149),(202,137),(181,137),(174,145),(158,143)], fill=c,width=5)
            d.line([(187,136),(187,128),(201,128)],fill=c,width=5)
        if kind=='Fuel':
            d.rectangle((184,124,204,153),outline=c,width=4)
            d.rectangle((187,127,201,138),outline=c,width=3)
            d.line([(181,154),(205,154),(205,132),(214,141),(215,151),(211,153)],fill=c,width=3)
        if kind=='Temp':
            d.line([(190,122),(190,148)],fill=c,width=6)
            d.ellipse((184,142,197,155),fill=c)
            d.line([(178,156),(184,154),(191,157),(199,154),(207,156)],fill=c,width=3)
    return img


def artwork():
    for k in ('Tach','Speed','Oil','Temp','Fuel'): textures[k]=gauge(k)
    img=Image.new('RGB',(512,416),(43,44,45)); d=ImageDraw.Draw(img)
    def br(x,y,w,h,fill,outline=None):
        d.rounded_rectangle((x,y,x+w,y+h),radius=7,fill=fill,outline=outline,width=2)
    br(8,9,496,397,(35,35,37),(79,78,77))
    br(20,22,472,85,(51,51,53),(67,69,68));br(52,46,406,17,(14,15,15))
    br(23,125,464,165,(44,44,45),(92,89,88))
    br(113,147,235,41,(110,121,101),(29,29,29))
    draw_text(d,225,169,'FM  89.1',26,(30,39,34))
    for x in [67,431]:
        d.ellipse((x-23,146,x+23,193),fill=(30,32,32),outline=(104,104,99),width=4)
        d.line((x,150,x,160),fill=(200,192,178),width=2)
    for i in range(6):
        x=38+i*73
        br(x,229,62,32,(52,53,54),(93,92,88));draw_text(d,x+31,245,str(i+1),18,(222,219,209))
    br(27,308,457,76,(34,35,35),(66,65,64))
    for j in range(3):
        x=95+157*j
        d.ellipse((x-29,316,x+29,374),fill=(44,45,46),outline=(115,114,107),width=4)
        d.line((x,322,x,340),fill=(217,211,193),width=3)
    textures['Radio']=img
    img=Image.new('RGB',(256,256),(45,45,45));d=ImageDraw.Draw(img)
    d.ellipse((20,20,236,236),fill=(46,47,47),outline=(65,66,64),width=4)
    d.rounded_rectangle((83,65,173,179),7,fill=(21,24,25),outline=(156,156,148),width=4)
    draw_text(d,128,82,'NARDI',17,(221,222,209))
    # The 1999 US Sport wheel uses a small cyan monogram on its central badge.
    d.line([(100,147),(100,103),(119,142),(119,98)],fill=(41,157,188),width=6)
    d.line([(125,98),(133,145),(151,147),(154,115),(149,99),(138,98)],fill=(41,157,188),width=6)
    draw_text(d,128,164,'TORINO',12,(211,213,200))
    textures['Badge']=img


def build_interior():
    global group
    for name,color,rough,metal in [
        ('Dash','393a3b',.66,0),('Upper','454546',.68,0),('Trim','252627',.64,0),
        ('Bezel','50504f',.41,.12),('Rubber','292a2b',.74,0),('Leather','343536',.55,0),
        ('Stitch','474747',.71,0),('Green','24543d',.25,.45),('Lens','18201f',.32,.23),
        ('Carpet','202122',.98,0),('Door','333435',.74,0),('Mark','bfc1b8',.64,0),('Pointer','e2dfd0',.50,0),('LCD','9da68a',.65,0),('Mirror','8e9b9d',.2,.4)]:
        material(name,color,rough,metal)
    for name in textures:
        material(name,'ffffff',.62,0,name)
    # Dashboard cross section: cowl, upper pad, fascia, then lower knee pad.
    row_yz=[(.715,-.64),(.747,-.55),(.759,-.43),(.757,-.34),(.752,-.27),
            (.739,-.235),(.719,-.216),(.682,-.212),(.619,-.23),(.543,-.255),(.44,-.30)]
    def dashpt(u,v):
        x=(u-.5)*1.58
        y,z=row_yz[round(v*(len(row_yz)-1))]
        shoulder=exp(-((x+.337)/.234)**4)
        center=exp(-(x/.136)**4)
        z+=.043*abs(x/.79)**2 - .105*shoulder*max(0,(z+.59)/.38)
        y+=.020*center*exp(-((y-.735)/.09)**2)
        return (x,y,z)
    grid('Dash',len(row_yz)-1,100,dashpt,reverse=True)
    # Raised, overhanging instrument hood. The face is recessed behind its lip.
    def hoodpt(u,v):
        a=u*pi;rx=.223-.015*v;ry=.122-.023*v
        return (-.337+rx*cos(a),.705+ry*sin(a)-.014*v,-.165-.168*v)
    grid('Upper',10,80,hoodpt)
    arc=[hoodpt(i/80,0) for i in range(81)]
    polyline('Trim',arc,.006)
    cluster=[(-.337+.218*cos(t),.705+.118*sin(t)) for t in np.linspace(0,pi,81)]
    cluster+= [(-.54,.674),(-.132,.674)]
    front_panel('Trim',cluster,-.202,.003,.01)
    # Gauge cups, printed scales and independent pointers.
    for kind,dx,y,r in [('Tach',-.070,.738,.058),('Speed',.070,.738,.058),
                        ('Oil',0,.782,.024),('Fuel',-.160,.717,.028),('Temp',.160,.717,.028)]:
        x=-.337+dx
        front_panel('Bezel',ellipse(x,y,r*1.06,r*1.06),-.196,.002,.008)
        front_panel(kind,ellipse(x,y,r,r),-.193,.0005,.001)
        name=kind+'Needle';group=name;placements[group]=[x,y,-.188]
        front_panel('Pointer',[(0,r*.81),(-r*.036,0),(-r*.025,-r*.13),(r*.025,-r*.13),(r*.036,0)],.001,.00015,.001)
        front_panel('Trim',ellipse(0,0,r*.096,r*.096,24),.002,.0003,.001)
        group='Interior'
    # The narrow telltale window and trip LCD are separate from the two large dials.
    front_panel('Rubber',rounded_rect(-.337,.728,.042,.034,.005),-.192,.0013,.002)
    front_panel('Bezel',rounded_rect(-.337,.696,.064,.013,.004),-.192,.0008,.003)
    front_panel('LCD',rounded_rect(-.337,.696,.060,.0105,.003),-.190,.0005,.002)
    # Circular eyeball vents. Curved, tilted louvers meet a domed center disc.
    for x,y,z in [(-.650,.704,-.16),(-.064,.742,-.182),(.067,.742,-.182),(.651,.704,-.155)]:
        r=.033
        front_panel('Trim',ellipse(x,y,r*1.19,r*1.19),z,.002,.012)
        edge=[(x+r*cos(a),y+r*sin(a),z+.002) for a in np.linspace(0,2*pi,65)]
        polyline('Bezel',edge,.0025)
        for s in np.linspace(-.78,.78,6):
            half=r*(1-s*s)**.5*.91
            polyline('Bezel',[(x+half*t,y+r*s+.08*half*t,z+.002-.002*t*t) for t in np.linspace(-1,1,8)],.0014)
        polyline('Bezel',[(x+t*.006,y+t*r*.78,z+.004) for t in [-1,1]],.0017)
    front_panel('Rubber',ellipse(.002,.719,.013,.013),-.175,.001,.004)
    front_panel('Bezel',rounded_rect(.002,.61,.218,.192,.013),-.176,.008,.024)
    face('Radio',.002,.61,-.174,.192,.168)
    # Passenger airbag and glovebox split lines follow the pad.
    polyline('Trim',[(.179,.740,-.244),(.220,.723,-.216),(.268,.718,-.207),(.542,.718,-.183),(.573,.724,-.183),(.585,.741,-.199)],.0022)
    polyline('Trim',[(.112,.645,-.211),(.247,.646,-.200),(.547,.648,-.18),(.729,.650,-.165)],.0024)
    # Door rolls, swept A pillars and convertible windshield header.
    for side in (-1,1):
        def pillar(t):
            return np.array((side*(.741-.080*t+.010*sin(pi*t)),.743+.461*t,-.509+.545*t))
        line=[pillar(t) for t in np.linspace(0,1,18)]
        polyline('Trim',line,.029,18)
        polyline('Rubber',[p+np.array((-side*.020,0,-.003)) for p in line],.009,12)
        polyline('Green',[p+np.array((side*.021,.005,-.008)) for p in line],.011,12)
        polyline('Dash',[(side*.75,.694,-.478),(side*.763,.711,-.17),(side*.778,.710,.22),(side*.775,.699,.59)],.032,14)
        # Continuous padded inner door, window ledge and kick panel. The room
        # below the dashboard stays enclosed when the car banks on a camber.
        door_profile=[(.788,.724),(.765,.728),(.744,.706),(.748,.657),(.751,.616),(.722,.570),(.704,.534),(.732,.500),(.749,.310),(.727,.208)]
        def doorpt(u,v):
            xx,yy=door_profile[round(v*(len(door_profile)-1))]
            return (side*(xx-.034*(1-u)**3), yy-.033*(1-u)**2,-.61+u*1.31)
        grid('Door',len(door_profile)-1,20,doorpt,reverse=side<0)
        polyline('Rubber',[(side*.785,.716,-.47),(side*.787,.726,-.17),(side*.792,.726,.22),(side*.787,.715,.59)],.006)
    grid('Carpet',2,2,lambda u,v:((u-.5)*1.56,.20,-.62+1.4*v),reverse=True)
    grid('Carpet',3,3,lambda u,v:((u-.5)*1.59,.19+v*.53,-.530+.104*v))
    # Center console and rubber shift gaiter.
    grid('Trim',3,8,lambda u,v:((u-.5)*(.24-.035*v),.376+.069*(1-(2*u-1)**4)-.056*v,-.306+.78*v),reverse=True)
    for sign in (-1,1):
        grid('Trim',1,2,lambda u,v:(sign*(.12-.0175*u),.20+v*(.177-.056*u),-.306+.78*u))
    # The driver's side door mirror, with its view sampled from the same rear
    # viewport as the center mirror. UV cropping selects the outboard field.
    polyline('Trim',[(-.748,.703,-.340),(-.791,.719,-.333),(-.830,.752,-.330)],.018,12)
    group='LeftMirror';placements[group]=[-.864,.753,-.330]
    mirror_outline=rounded_rect(0,0,.153,.083,.026)
    front_panel('Green',mirror_outline,.010,.010,.063,.002)
    front_panel('Trim',rounded_rect(0,0,.147,.077,.023),.014,.003,.009)
    group='LeftGlass';placements[group]=placements['LeftMirror']
    front_panel('Mirror',rounded_rect(0,0,.137,.069,.022),.015,.0004,.001)
    group='Interior'
    header_profile=[(-.018,-.020),(-.024,-.006),(-.022,.016),(-.006,.031),(.012,.033),(.024,.018),(.026,-.012),(.017,-.032),(-.008,-.036),(-.018,-.020)]
    def headerpt(u,v):
        x=(2*u-1)*.678;dy,dz=header_profile[round(v*(len(header_profile)-1))]
        curve=1-(x/.678)**2
        return (x,1.204+.028*curve+dy,.030-.058*curve+dz)
    grid('Trim',len(header_profile)-1,32,headerpt)
    for side in (-1,1):
        # Stowed sun visors are carried on the windshield header.
        points=[headerpt(i,0) for i in (np.linspace(.08,.42,20) if side<0 else np.linspace(.58,.92,20))]
        polyline('Rubber',[(x,y+.005,z+.017) for x,y,z in points],.012,14)
    # Body-colour bonnet. Twin front wheel-arch crests flatten over the nose.
    def bonnet(u,v):
        w=.729+.060*sin(v*pi)-.035*v;x=(u*2-1)*w
        y=.718-.063*v-.082*v**3 + .037*exp(-((abs(x)-(.56+.05*v))/.083)**2)*sin(pi*(.15+.72*v))
        y+=.019*(1-(x/w)**2)-.049*(abs(x)/w)**8
        return (x,y,-.59-v*1.25)
    grid('Green',23,78,bonnet)
    for side in (-1,1):
        pts=[np.array(bonnet(.5+side*(.345+.023*t),t))+np.array((0,.0012,0)) for t in np.linspace(.005,.96,38)]
        polyline('Trim',pts,.00135,6)
        # Parked windshield wiper and pressed metal arm.
        x=side*.35
        polyline('Trim',[(x-.23,.756,-.547),(x,.759,-.562),(x+.209,.753,-.559)],.0028)
        polyline('Rubber',[(x-.242,.757,-.550),(x,.760,-.565),(x+.230,.755,-.562)],.0036)
        polyline('Trim',[(x+.077,.738,-.515),(x+.048,.751,-.541),(x-.052,.758,-.562)],.0033)
    # Column stalks ahead of the wheel.
    for side in (-1,1):
        polyline('Trim',[(-.337+side*.044,.695,-.138),(-.337+side*.103,.706,-.137),(-.337+side*.150,.722,-.125)],.006)
        polyline('Rubber',[(-.337+side*.116,.713,-.130),(-.337+side*.154,.723,-.124)],.009)
    group='Wheel';placements[group]=[-.337,.700,-.036]
    ring=[(.168*sin(a),.169*cos(a),0) for a in np.linspace(0,2*pi,121)]
    polyline('Leather',ring,.0147,18)
    seam=[(.157*sin(a),.158*cos(a),.008) for a in np.linspace(0,2*pi,121)]
    polyline('Stitch',seam,.00085,7)
    # Airbag and three flexible horn spokes on the NB1 Nardi steering wheel.
    for side in [-1,1]:
        shape=[(side*x,y) for x,y in [(.033,.035),(.072,.037),(.123,.024),(.161,.018),(.159,-.013),(.122,-.006),(.081,-.002),(.058,-.028)]]
        if side==1:shape.reverse()
        front_panel('Leather',shape,.016,.005,.024,.005)
    spoke=[(-.042,-.035),(.042,-.035),(.025,-.099),(.023,-.157),(-.023,-.157),(-.025,-.099)]
    front_panel('Leather',spoke[::-1],.009,.003,.02,.004)
    airbag=[(.073*cos(a)*(1+.20*sin(a)),.015+.068*sin(a)-.007*(1-sin(a))**2) for a in np.linspace(0,2*pi,73)[:-1]]
    front_panel('Leather',airbag,.030,.007,.029,.008)
    front_panel('Badge',ellipse(0,.016,.024,.024),.039,.001,.002)
    group='Interior'
    # Rear-view mirror face gets a horizontally mirrored rear camera at runtime.
    polyline('Trim',[(.005,1.173,-.219),(.005,1.152,-.20),(.013,1.133,-.171)],.008)
    front_panel('Trim',rounded_rect(.005,1.168,.024,.028,.006),-.216,.001,.004)
    front_panel('Trim',rounded_rect(.013,1.117,.210,.063,.014),-.154,.005,.021)
    group='RearGlass'
    front_panel('Mirror',rounded_rect(.013,1.118,.197,.052,.011),-.153,.0005,.001)


def write_glb():
    content=bytearray(); views=[]; accessors=[]; meshes=[]; nodes=[];images=[]
    def buffer(data):
        content.extend(b'\0'*(-len(content)%4))
        ix=len(views);views.append(dict(buffer=0,byteOffset=len(content),byteLength=len(data)));content.extend(data)
        return ix
    def array(data,typ,kind,component):
        arr=np.asarray(data,dtype=typ)
        acc=dict(bufferView=buffer(arr.tobytes()),componentType=component,count=len(arr),type=kind)
        if kind=='VEC3':
            acc.update(min=arr.min(axis=0).tolist(),max=arr.max(axis=0).tolist())
        accessors.append(acc)
        return len(accessors)-1
    for img in textures.values():
        f=io.BytesIO();img.save(f,format='PNG',optimize=True)
        images.append(dict(bufferView=buffer(f.getvalue()),mimeType='image/png'))
    mats=[]
    for name,(rgb,rough,metal,tex) in materials.items():
        pbr=dict(baseColorFactor=rgb+[1],roughnessFactor=rough,metallicFactor=metal)
        if tex:pbr['baseColorTexture']={'index':list(textures).index(tex)}
        mats.append(dict(name=name,pbrMetallicRoughness=pbr,doubleSided=True))
    for name in dict.fromkeys(group for group,_ in vertices):
        prim=[]
        for (group,mat),(pos,norm,uv,idx) in vertices.items():
            if group!=name:continue
            attrs=dict(POSITION=array(pos,'<f4','VEC3',5126),NORMAL=array(norm,'<f4','VEC3',5126),TEXCOORD_0=array(uv,'<f4','VEC2',5126))
            prim.append(dict(attributes=attrs,indices=array(idx,'<u4','SCALAR',5125),material=list(materials).index(mat)))
        nodes.append(dict(name=name,mesh=len(meshes),translation=placements.get(name,[0.,0.,0.])))
        meshes.append(dict(name=name,primitives=prim))
    doc=dict(asset=dict(version='2.0'),scene=0,scenes=[dict(nodes=list(range(len(nodes))))],nodes=nodes,
             meshes=meshes,materials=mats,textures=[dict(source=i) for i in range(len(images))],images=images,
             accessors=accessors,bufferViews=views,buffers=[dict(byteLength=len(content))])
    meta=json.dumps(doc,separators=(',',':')).encode();meta+=b' '*(-len(meta)%4)
    content.extend(b'\0'*(-len(content)%4))
    OUT.write_bytes(struct.pack('<4sIII4s',b'glTF',2,28+len(meta)+len(content),len(meta),b'JSON')+meta+struct.pack('<I4s',len(content),b'BIN\0')+content)


if __name__=='__main__':
    artwork();build_interior();write_glb()
    rng=np.random.default_rng(1999)
    noise=rng.normal(0,1,(128,128))
    grain=239+6*noise+4*np.roll(noise,1,axis=0)+3*np.roll(noise,1,axis=1)
    Image.fromarray(np.clip(grain,208,255).astype('uint8')).save(OUT.with_name('nb-vinyl.png'))

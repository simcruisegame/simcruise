extends RefCounted
# The 1999 NB Sports Package 15 x 6 five-spoke alloy on a 195/50R15 tyre, built
# procedurally once per run and shared by the four hubs. One right-hand wheel in
# metres with its axle along +X; the left side reuses it rotated half a turn, so
# both sides show the same casting from outside, as on the car.
#
# Casting: angular metal coverage per radius re-measured from a 1999 Sport-wheel
# side photograph (Wikimedia Commons "Mazda miata 1999 blue side"): five narrow
# spokes about 20 degrees wide from 50 % to 85 % of the flange radius, swept
# about 13 degrees clockwise from hub to rim, flaring into the rim with fillets,
# four lugs on a 100 mm circle and a plain domed cap (no maker mark).
# Tyre: 195/50R15 profile with a rim protector rib, a rounded shoulder, four
# circumferential grooves and raised size/construction lettering on the outer
# sidewall through a normal map (game/art/tyre_sidewall_normal.png, no brand).
# Building takes about 0.1 s; wheel() caches the mesh for the session.

static var _wheel: ArrayMesh
static var _caliper: ArrayMesh

static func wheel() -> ArrayMesh:
 if _wheel == null:
  _wheel = build_wheel()
 return _wheel

static func caliper() -> ArrayMesh:
 if _caliper == null:
  _caliper = build_caliper()
 return _caliper

static func material(color:Color,metal:float,rough:float) -> StandardMaterial3D:
 var m=StandardMaterial3D.new()
 m.albedo_color=color;m.metallic=metal;m.roughness=rough
 m.cull_mode=BaseMaterial3D.CULL_DISABLED
 return m
static func surface(mat) -> SurfaceTool:
 var st=SurfaceTool.new()
 st.begin(Mesh.PRIMITIVE_TRIANGLES);st.set_material(mat)
 return st
static func vertex(st,p,n):
 st.set_normal(n);st.add_vertex(p)
static func quad(st,a,b,c,d,na,nb,nc,nd):
 vertex(st,a,na);vertex(st,c,nc);vertex(st,b,nb)
 vertex(st,a,na);vertex(st,d,nd);vertex(st,c,nc)
static func radial(a):return Vector3(0,cos(a),sin(a))
static func ring(st,profile,origin=Vector3.ZERO,segments=72):
 for j in range(1,profile.size()):
  var a:Vector2=profile[j-1];var b:Vector2=profile[j]
  var normal=Vector2(b.y-a.y,a.x-b.x).normalized()
  for k in segments:
   var r=radial(TAU*k/segments);var q=radial(TAU*(k+1)/segments)
   var n=Vector3.RIGHT*normal.x+r*normal.y;var m=Vector3.RIGHT*normal.x+q*normal.y
   quad(st,origin+Vector3.RIGHT*a.x+r*a.y,origin+Vector3.RIGHT*a.x+q*a.y,origin+Vector3.RIGHT*b.x+q*b.y,origin+Vector3.RIGHT*b.x+r*b.y,n,m,m,n)

# Fraction of one 72-degree sector occupied by cast metal at a given radius (m).
# The flange radius is 0.201 m; windows run from about 0.085 to 0.178 m.
const CAST_SECTION = [
 Vector2(.059,1.0), Vector2(.068,.98), Vector2(.077,.82),
 Vector2(.0865,.56), Vector2(.0965,.40), Vector2(.108,.32),
 Vector2(.121,.285), Vector2(.135,.275), Vector2(.149,.28),
 Vector2(.160,.30), Vector2(.169,.37), Vector2(.176,.55),
 Vector2(.1825,.86), Vector2(.187,1.), Vector2(.191,1.)]
const SWEEP_RAD := .23    # spoke centre-line sweep from hub to rim, mostly in the outer half

static func casting_slope(i):
 if i==0 or i==CAST_SECTION.size()-1:
  return 0.
 var previous=CAST_SECTION[i-1]
 var middle=CAST_SECTION[i]
 var next=CAST_SECTION[i+1]
 var left=(middle.y-previous.y)/(middle.x-previous.x)
 var right=(next.y-middle.y)/(next.x-middle.x)
 return 0. if left*right<=0. else 2.*left*right/(left+right)

static func covered_fraction(radius):
 for i in range(1,CAST_SECTION.size()):
  var a=CAST_SECTION[i-1]
  var b=CAST_SECTION[i]
  if radius<=b.x:
   var t=clampf((radius-a.x)/(b.x-a.x),0.,1.)
   var interval=b.x-a.x
   return (2.*t*t*t-3.*t*t+1.)*a.y+(t*t*t-2.*t*t+t)*interval*casting_slope(i-1) \
    +(-2.*t*t*t+3.*t*t)*b.y+(t*t*t-t*t)*interval*casting_slope(i)
 return 1.

static func sweep(radius):
 return -SWEEP_RAD*pow(clampf((radius-.062)/.125,0.,1.),1.7)

static func spoke_point(radius, lateral, phase):
 var angle=phase+sweep(radius)+lateral*PI/5.*covered_fraction(radius)
 # The cast face rises from the lug bowl, then rolls into the outer return.
 var crown=.0075*maxf(0.,1.-lateral*lateral)
 var axial=.073+.007*exp(-pow((radius-.105)/.052,2))-.012*absf(lateral)**3
 axial-=.009*smoothstep(.160,.19,radius)
 return radial(angle)*radius+Vector3.RIGHT*(axial+crown)

static func spoke_normal(radius,lateral,phase):
 var across=spoke_point(radius,lateral+.0005,phase)-spoke_point(radius,lateral-.0005,phase)
 var out=spoke_point(radius+.00008,lateral,phase)-spoke_point(radius-.00008,lateral,phase)
 var n=out.cross(across).normalized()
 return n if n.x>=0 else -n

static func cast_spokes(alloy):
 # Smooth compound-curved castings, including the dark-facing return wall of each opening.
 for k in 5:
  var phase=k*TAU/5.
  var previous=[]
  var last_normals=[]
  for row in 41:
   var radius=lerpf(.059,.191,row/40.)
   var next=[]
   var normals=[]
   for j in 13:
    var u=-1.+j/6.
    next.append(spoke_point(radius,u,phase))
    normals.append(spoke_normal(radius,u,phase))
   if row>0:
    for j in 12:
     quad(alloy,previous[j],next[j],next[j+1],previous[j+1],last_normals[j],normals[j],normals[j+1],last_normals[j+1])
    for j in [0,12]:
     var a=previous[j]
     var b=next[j]
     var n=(b-a).cross(Vector3.LEFT).normalized() * (1. if j==0 else -1.)
     quad(alloy,a,b,b+Vector3.LEFT*.013,a+Vector3.LEFT*.013,n,n,n,n)
   previous=next
   last_normals=normals

static func hub_point(radius,angle):
 var yz=radial(angle)*radius
 var axial=.080-.006*smoothstep(.030,.065,radius)
 for i in 4:
  var center=radial((i+.5)*PI*.5)*.050
  axial-=.013*(1.-smoothstep(.009,.0175,yz.distance_to(center)))
 return yz+Vector3.RIGHT*axial

static func hub_normal(radius,angle):
 var a=hub_point(radius+.0001,angle)-hub_point(radius-.0001,angle)
 var b=hub_point(radius,angle+.001)-hub_point(radius,angle-.001)
 return a.cross(b).normalized()

static func cast_hub(alloy,dark,cap):
 for row in 12:
  var r1=lerpf(.001,.066,row/12.)
  var r2=lerpf(.001,.066,(row+1)/12.)
  for j in 84:
   var a=j*TAU/84.
   var b=(j+1)*TAU/84.
   quad(alloy,hub_point(r1,a),hub_point(r2,a),hub_point(r2,b),hub_point(r1,b),
    hub_normal(r1,a),hub_normal(r2,a),hub_normal(r2,b),hub_normal(r1,b))
 # Four visible lug recesses on 100-mm PCD; hexagonal fasteners sit inside.
 for i in 4:
  var center=radial((i+.5)*PI*.5)*.05
  ring(dark,[Vector2(.070,.0085),Vector2(.070,.0125),Vector2(.073,.012),Vector2(.072,.0085)],center,30)
  ring(alloy,[Vector2(.069,.008),Vector2(.076,.0098),Vector2(.081,.0088),Vector2(.083,.0068),Vector2(.083,.0002)],center,6)
 # Plain domed centre cap in a dark seating ring.
 ring(dark,[Vector2(.0804,.0262),Vector2(.0806,.0296)])
 ring(cap,[Vector2(.0805,.0262),Vector2(.0826,.0245),Vector2(.0841,.020),Vector2(.0851,.013),Vector2(.0856,.006),Vector2(.0857,.0005)],Vector3.ZERO,48)
 # The molded valve stem occupies one small section of the outer flange.
 ring(dark,[Vector2(.057,.0038),Vector2(.075,.0038),Vector2(.076,.0029),Vector2(.076,.0002)],radial(PI/5.)*.172,8)

# Lathe with texture coordinates: u runs twice round the tyre (two lettering
# repeats), v follows each profile point's own value.
static func lathe(st,profile:Array,segments:int):
 for j in range(1,profile.size()):
  var a:Vector3=profile[j-1];var b:Vector3=profile[j]
  var normal=Vector2(b.y-a.y,a.x-b.x).normalized()
  for k in segments:
   var t0=TAU*k/segments;var t1=TAU*(k+1)/segments
   var r0=radial(t0);var r1=radial(t1)
   var n0=Vector3.RIGHT*normal.x+r0*normal.y;var n1=Vector3.RIGHT*normal.x+r1*normal.y
   var u0=-2.*k/segments;var u1=-2.*(k+1)/segments
   var p00=Vector3.RIGHT*a.x+r0*a.y;var p01=Vector3.RIGHT*a.x+r1*a.y
   var p10=Vector3.RIGHT*b.x+r0*b.y;var p11=Vector3.RIGHT*b.x+r1*b.y
   for corner in [[p00,n0,Vector2(u0,a.z)],[p11,n1,Vector2(u1,b.z)],[p01,n1,Vector2(u1,a.z)],[p00,n0,Vector2(u0,a.z)],[p10,n0,Vector2(u0,b.z)],[p11,n1,Vector2(u1,b.z)]]:
    st.set_normal(corner[1]);st.set_uv(corner[2]);st.add_vertex(corner[0])

# Tyre cross-section as (axial, radius, v). The outer sidewall between the
# rim protector and the shoulder carries the lettering band (v 0..1, row 0
# towards the tread); everything else samples a flat strip of the map.
static func tyre_profile() -> Array:
 var flat=.02
 var band=func(r:float)->float:return clampf((.2625-r)/(.2625-.2135),0.,1.)
 var outer=[]
 for p in [Vector2(.0772,.1925),Vector2(.0832,.1985),Vector2(.0902,.2055),Vector2(.0928,.2105),Vector2(.0938,.2135),
  Vector2(.0962,.2255),Vector2(.0975,.2375),Vector2(.0969,.2500),Vector2(.0950,.2625),Vector2(.0912,.2715),
  Vector2(.0858,.2785),Vector2(.0790,.2838),Vector2(.0712,.2868)]:
  outer.append(Vector3(p.x,p.y,band.call(p.y)))
 var tread=[]
 for x in [.060,.023,-.023,-.060]:
  tread.append_array([Vector3(x+.0045,.2878,flat),Vector3(x+.0028,.2838,flat),Vector3(x-.0028,.2838,flat),Vector3(x-.0045,.2878,flat)])
 var profile=outer.duplicate()
 profile.append(Vector3(.066,.2876,flat))
 profile.append_array(tread)
 profile.append(Vector3(-.066,.2876,flat))
 for i in range(outer.size()-1,-1,-1):
  var p:Vector3=outer[i]
  profile.append(Vector3(-p.x,p.y,flat))
 return profile

static func build_wheel() -> ArrayMesh:
 var mesh=ArrayMesh.new()
 var rubber=material(Color("#1d1e1e"),0.,.88)
 rubber.normal_enabled=true;rubber.normal_texture=preload("res://art/tyre_sidewall_normal.png");rubber.normal_scale=1.
 rubber.texture_filter=BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
 var tire=surface(rubber)
 lathe(tire,tyre_profile(),96)
 tire.generate_tangents()
 tire.commit(mesh)
 var alloy=surface(material(Color("#c2c6c8"),.48,.36))
 # Rim: bead seat, flange and a polished-looking lip, then the inner barrel.
 ring(alloy,[Vector2(.077,.181),Vector2(.083,.188),Vector2(.083,.196),Vector2(.079,.2012),Vector2(.073,.2002),Vector2(.0715,.196),Vector2(-.078,.194),Vector2(-.08,.181),Vector2(.077,.181)],Vector3.ZERO,96)
 var dark=surface(material(Color("#3e403d"),.26,.61))
 var cap=surface(material(Color("#b9bdbf"),.62,.28))
 cast_spokes(alloy)
 cast_hub(alloy,dark,cap)
 alloy.commit(mesh)
 dark.commit(mesh)
 cap.commit(mesh)
 var disc=surface(material(Color("#6a6c6a"),.7,.42))
 ring(disc,[Vector2(.038,.04),Vector2(.038,.125),Vector2(.028,.125),Vector2(.028,.04)])
 disc.commit(mesh)
 return mesh

# Single-piston sliding caliper hugging the rotor behind the axle: an annular
# sector straddling the disc plane (centred on the node, symmetric in X, so the
# same mesh serves both sides without mirroring).
static func build_caliper() -> ArrayMesh:
 var st=surface(material(Color("#3b3d3b"),.32,.58))
 var centre=deg_to_rad(76.);var half=deg_to_rad(23.)
 var inner=.086;var outer=.133;var w=.025
 var steps=10
 var section=[Vector2(-w,inner),Vector2(w,inner),Vector2(w,outer-.008),Vector2(w-.008,outer),Vector2(-w+.008,outer),Vector2(-w,outer-.008),Vector2(-w,inner)]
 for j in range(1,section.size()):
  var a:Vector2=section[j-1];var b:Vector2=section[j]
  var normal=Vector2(b.y-a.y,a.x-b.x).normalized()
  for k in steps:
   var t0=centre-half+2.*half*k/steps;var t1=centre-half+2.*half*(k+1)/steps
   var r0=radial(t0);var r1=radial(t1)
   quad(st,Vector3.RIGHT*a.x+r0*a.y,Vector3.RIGHT*a.x+r1*a.y,Vector3.RIGHT*b.x+r1*b.y,Vector3.RIGHT*b.x+r0*b.y,
    Vector3.RIGHT*normal.x+r0*normal.y,Vector3.RIGHT*normal.x+r1*normal.y,Vector3.RIGHT*normal.x+r1*normal.y,Vector3.RIGHT*normal.x+r0*normal.y)
 # End caps.
 for t in [centre-half,centre+half]:
  var r=radial(t);var along=Vector3(0,-sin(t),cos(t))*(1. if t>centre else -1.)
  for j in range(1,section.size()-1):
   var a:Vector2=section[0];var b:Vector2=section[j];var c:Vector2=section[j+1]
   var pa=Vector3.RIGHT*a.x+r*a.y;var pb=Vector3.RIGHT*b.x+r*b.y;var pc=Vector3.RIGHT*c.x+r*c.y
   if (pb-pa).cross(pc-pa).dot(along)<0.:
    var swap=pb;pb=pc;pc=swap
   vertex(st,pa,along);vertex(st,pb,along);vertex(st,pc,along)
 return st.commit()

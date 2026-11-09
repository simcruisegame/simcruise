extends SceneTree
# One right-hand wheel in metres, with its axle along +X. Mirror by rotating the
# finished mesh on the opposite side. The visual hub follows the contact model.
static func material(color:Color,metal:float,rough:float):
 var m=StandardMaterial3D.new()
 m.albedo_color=color;m.metallic=metal;m.roughness=rough
 m.cull_mode=BaseMaterial3D.CULL_DISABLED
 return m
static func surface(mat):
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
# Fraction of one 72-degree sector occupied by cast metal at a given radius.
# Angular measurements come from the front-face hole outlines on the factory wheel.
const CAST_SECTION = [
 Vector2(.059,1.0), Vector2(.067,.99), Vector2(.080,.84),
 Vector2(.0905,.66), Vector2(.1045,.45), Vector2(.1206,.35),
 Vector2(.1367,.54), Vector2(.1508,.65), Vector2(.163,.71),
 Vector2(.174,.84), Vector2(.185,1.), Vector2(.191,1.)]

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

static func spoke_point(radius, lateral, phase):
 var angle=phase+lateral*PI/5.*covered_fraction(radius)
 # The cast face rises from the lug bowl, then rolls into the outer return.
 var crown=.010*maxf(0.,1.-lateral*lateral)
 var axial=.073+.007*exp(-pow((radius-.105)/.052,2))-.013*absf(lateral)**3
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
  for row in 37:
   var radius=lerpf(.059,.191,row/36.)
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

static func cast_hub(alloy,dark):
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
 ring(dark,[Vector2(.0804,.026),Vector2(.0804,.0295)])
 ring(alloy,[Vector2(.0805,.001),Vector2(.0812,.025),Vector2(.0805,.027),Vector2(.0803,.0274)])
 # The molded valve stem occupies one small section of the outer flange.
 ring(dark,[Vector2(.057,.0038),Vector2(.075,.0038),Vector2(.076,.0029),Vector2(.076,.0002)],radial(PI/5.)*.172,8)

static func build_wheel():
 var mesh=ArrayMesh.new()
 var tire=surface(material(Color("#242525"),0.,.94))
 var profile=[Vector2(.078,.186),Vector2(.089,.196),Vector2(.0975,.229),Vector2(.096,.258),Vector2(.089,.276),Vector2(.077,.285)]
 for x in [.060,.023,-.023,-.060]:
  profile.append_array([Vector2(x+.002,.288),Vector2(x,.284),Vector2(x-.002,.288)])
 profile.append_array([Vector2(-.077,.285),Vector2(-.092,.273),Vector2(-.0975,.236),Vector2(-.087,.198),Vector2(-.077,.186)])
 ring(tire,profile);tire.commit(mesh)
 var alloy=surface(material(Color("#cbced0"),.36,.46))
 ring(alloy,[Vector2(.077,.181),Vector2(.083,.188),Vector2(.082,.197),Vector2(.077,.201),Vector2(.071,.199),Vector2(-.078,.194),Vector2(-.08,.181),Vector2(.077,.181)])
 var dark=surface(material(Color("#444641"),.26,.61))
 cast_spokes(alloy)
 cast_hub(alloy,dark)
 alloy.commit(mesh)
 dark.commit(mesh)
 var disc=surface(material(Color("#696b69"),.7,.42))
 ring(disc,[Vector2(.038,.04),Vector2(.038,.125),Vector2(.028,.125),Vector2(.028,.04)])
 disc.commit(mesh)
 return mesh
func _initialize():
 var args=OS.get_cmdline_user_args()
 var output="res://art/nb_sport_alloy.res" if args.is_empty() else args[0]
 var status=ResourceSaver.save(build_wheel(),output,ResourceSaver.FLAG_COMPRESS)
 if status!=OK:push_error("Could not write the baked Sport wheel: "+error_string(status))
 quit(status)

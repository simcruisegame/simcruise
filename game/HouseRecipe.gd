extends RefCounted
## Geometry recipes for detached houses (WorldTile.survey_building): an eave
## overhang with fascia board and soffit, gutters and downspouts on the eaves,
## a front door with a stoop on the wall that faces the nearest street, an
## attached garage door beside it on larger footprints, a small porch roof over
## some doors and a brick chimney on some roofs. Every choice comes from data
## every US house has (footprint shape and area, height, NSI use and stories,
## the street network) and a per-building hash, so the same house is built the
## same way on every machine, and no place is special-cased.

# Flat finishes the recipes add (Drive.gd creates them; missing keys fall back).
const COLORS={"house_door0":"#6b2b26","house_door1":"#25282b","house_door2":"#2f3f52","house_door3":"#7b5535","house_door4":"#3d4b3c","house_garage":"#dedbd2","house_seam":"#9a978f"}
const FASCIA:=.2   # 1x8 fascia board, also the depth of the boxed eave
# --plain-houses keeps the flush-roofed boxes, for A/B captures.
static var enabled:=not "--plain-houses" in OS.get_cmdline_user_args()

static func pick(t,key:String,fallback:String)->String:
 return key if t.materials.has(key) else fallback

## Roof surface height of the survey gable, extended past the walls so the
## overhang keeps the roof pitch (eaves drop, rakes keep their slope).
static func height(p:Vector2,axis:Vector2,offset:float,width:float,eave:float,rise:float)->float:
 return eave+rise*minf(1.,1.-absf(p.dot(axis)-offset)/width)

static func ring_points(t,ring)->PackedVector2Array:
 var out:=PackedVector2Array()
 for uv in ring:
  var p:Vector2=t.point(uv)
  if out.size() and out[-1].distance_to(p)<.04:continue
  out.append(p)
 if out.size()>2 and out[0].distance_to(out[-1])<.04:out.remove_at(out.size()-1)
 return out

static func area(ring:PackedVector2Array)->float:
 var s:=0.
 for i in ring.size():s+=ring[i].cross(ring[(i+1)%ring.size()])
 return s*.5

## +1 or -1: the factor that turns edge.orthogonal() into the outward normal.
static func outward_sign(ring:PackedVector2Array)->float:
 return 1. if area(ring)>0 else -1.

static func normal_of(ring:PackedVector2Array,i:int,turn:float)->Vector2:
 return (ring[(i+1)%ring.size()]-ring[i]).normalized().orthogonal()*turn

## The footprint pushed out by o metres (mitred corners, miters capped).
static func grow(ring:PackedVector2Array,o:float,turn:float)->PackedVector2Array:
 var out:=PackedVector2Array()
 var n:=ring.size()
 for i in n:
  var n1:=normal_of(ring,(i+n-1)%n,turn);var n2:=normal_of(ring,i,turn)
  var m:=n1+n2
  if m.length_squared()<1e-6:m=n1
  m=m.normalized()
  out.append(ring[i]+m*o/maxf(m.dot(n1),.4))
 return out

## Chooses the front wall and what goes on it. Returns {} when the house has
## no wall long enough for a door.
static func plan(t,ring:PackedVector2Array,center:Vector2,id:int,use:String)->Dictionary:
 if ring.size()<3:return {}
 var turn:=outward_sign(ring)
 var road=t.nearest(center,true)
 var best:=-INF;var front:={}
 for i in ring.size():
  var a:=ring[i];var d:=ring[(i+1)%ring.size()];var length:=a.distance_to(d)
  if length<2.6:continue
  var out:=normal_of(ring,i,turn)
  # Face the street; among walls that do, prefer the longer one.
  var score:=minf(length,12.)*.02
  if road.size():score+=out.dot((road[0]-(a+d)*.5).normalized())
  if score>best:best=score;front={"a":a,"d":d,"length":length,"out":out}
 if front.is_empty():return {}
 var length:float=front.length;var floor_area:=absf(area(ring))
 var doors:=[];var garage:=Vector2.ZERO
 if use=="duplex" and length>=7.:doors=[length*.3,length*.7]
 else:
  var garage_w:=0.
  # An attached garage takes part of a larger footprint (a single bay is 9 ft,
  # a double 16 ft, both 7 ft high); about three in four such houses have one.
  if floor_area>=120. and length>=7.6 and (id>>3)%4!=0:garage_w=4.88 if length>=11. and floor_area>=160. else 2.74
  if garage_w>0.:
   var left:=(id>>5)%2==0
   var g0:=.6 if left else length-.6-garage_w
   garage=Vector2(g0,g0+garage_w)
   doors=[(garage.y+length)*.5 if left else garage.x*.5]
  else:doors=[clampf(length*(.35+.3*float((id>>5)%7)/6.),1.,length-1.)]
 front.doors=doors;front.garage=garage
 front.porch=(id>>9)%5<2
 front.door_key="house_door%d"%((id>>11)%5)
 return front

static func same_edge(front:Dictionary,a:Vector2,d:Vector2)->bool:
 return not front.is_empty() and a.distance_to(front.a)<.05 and d.distance_to(front.d)<.05

## True when a ground-floor window centred s metres along the front wall would
## overlap the door or the garage door.
static func blocks(front:Dictionary,s:float)->bool:
 for c in front.doors:
  if absf(s-c)<1.45:return true
 return front.garage.y>0. and s>front.garage.x-.75 and s<front.garage.y+.75

static func panel(t,key:String,c:Vector2,line:Vector2,half:float,lo:float,hi:float,normal:Vector3):
 t.quad(key,t.vert(c-line*half,lo),t.vert(c+line*half,lo),t.vert(c+line*half,hi),t.vert(c-line*half,hi),normal)

## An open box: the face along +out, both ends, the top unless top_key is
## empty, and the face along -out when asked.
static func box(t,key:String,top_key:String,c:Vector2,line:Vector2,out:Vector2,half_l:float,half_o:float,bottom:float,top:float,back:=false):
 var p0:=c-line*half_l-out*half_o;var p1:=c+line*half_l-out*half_o;var p2:=c+line*half_l+out*half_o;var p3:=c-line*half_l+out*half_o
 if top_key!="":t.quad(top_key,t.vert(p3,top),t.vert(p2,top),t.vert(p1,top),t.vert(p0,top),Vector3.UP)
 t.quad(key,t.vert(p3,bottom),t.vert(p2,bottom),t.vert(p2,top),t.vert(p3,top),Vector3(out.x,0,out.y))
 t.quad(key,t.vert(p0,bottom),t.vert(p3,bottom),t.vert(p3,top),t.vert(p0,top),Vector3(-line.x,0,-line.y))
 t.quad(key,t.vert(p2,bottom),t.vert(p1,bottom),t.vert(p1,top),t.vert(p2,top),Vector3(line.x,0,line.y))
 if back:t.quad(key,t.vert(p1,bottom),t.vert(p0,bottom),t.vert(p0,top),t.vert(p1,top),Vector3(-out.x,0,-out.y))

## Front door(s) with frame and stoop, garage door, and the porch roof.
static func draw_front(t,front:Dictionary,base:float,axis:Vector2,offset:float,width:float,eave:float,rise:float,roof_key:String):
 if front.is_empty():return
 var a:Vector2=front.a;var length:float=front.length;var line:Vector2=(front.d-a)/length;var out:Vector2=front.out
 var door:Vector2=a+line*float(front.doors[0])
 t.house_fronts.append_array([t.vert(door,base),Vector3(out.x,0,out.y)])
 if not enabled:return
 var normal:=Vector3(out.x,0,out.y)
 var trim:="house_trim";var dark:=pick(t,"trim",trim)
 var concrete:=pick(t,"walk",pick(t,"curb",trim))
 if front.garage.y>0.:
  var half:float=(front.garage.y-front.garage.x)*.5;var c:Vector2=a+line*(front.garage.x+half)
  var floor_y:=clampf(t.height_at(c+out*1.5),base-.3,base+.3)
  var top:=floor_y+2.13
  if top+.15<minf(height(c-line*half,axis,offset,width,eave,rise),height(c+line*half,axis,offset,width,eave,rise)):
   panel(t,trim,c+out*.02,line,half+.12,floor_y-.05,top+.12,normal)
   panel(t,pick(t,"house_garage",trim),c+out*.034,line,half,floor_y,top,normal)
   var seam:=pick(t,"house_seam",dark)
   for k in range(1,4):
    var y:=floor_y+2.13*k/4.
    panel(t,seam,c+out*.04,line,half,y-.012,y+.012,normal)
 for n in front.doors.size():
  var s:float=front.doors[n];var c:Vector2=a+line*s
  var ground:=clampf(t.height_at(c+out*1.2),base-.6,base+.3)
  var sill:=maxf(base,ground)+.18;var top:=sill+2.03
  var wall_top:=minf(height(c-line*.6,axis,offset,width,eave,rise),height(c+line*.6,axis,offset,width,eave,rise))
  if top+.12>wall_top:continue
  panel(t,trim,c+out*.02,line,.56,sill-.02,top+.1,normal)
  panel(t,pick(t,front.door_key,dark),c+out*.034,line,.455,sill,top,normal)
  # Concrete stoop, with a second step where the lot falls away from the door.
  box(t,concrete,concrete,c+out*.55,line,out,.75,.55,ground-.15,sill-.03)
  if sill-ground>.42:box(t,concrete,concrete,c+out*1.27,line,out,.75,.17,ground-.15,(sill-.03+ground)*.5)
  if n>0 or not front.porch:continue
  # A shed porch roof over the door, tucked under the eave.
  var half:=minf(1.3,minf(s-.2,length-.2-s))
  if half<.8:continue
  wall_top=minf(height(c-line*half,axis,offset,width,eave,rise),height(c+line*half,axis,offset,width,eave,rise))
  var hi:=minf(top+.55,wall_top-.32);var lo:=hi-.3;var depth:=1.55
  if hi<top+.2:continue
  var w0:=c-line*half;var w1:=c+line*half;var o0:=w0+out*depth;var o1:=w1+out*depth
  var v0:Vector3=t.vert(w0,hi);var v1:Vector3=t.vert(w1,hi);var v2:Vector3=t.vert(o1,lo);var v3:Vector3=t.vert(o0,lo)
  var up:=(v1-v0).cross(v3-v0).normalized()
  if up.y<0:up=-up
  var roof=t.tool(roof_key);var side:float=t.side
  t.tri(roof,v0,v1,v2,w0/side,w1/side,o1/side,up);t.tri(roof,v0,v2,v3,w0/side,o1/side,o0/side,up)
  t.quad(trim,t.vert(w0,hi-.12),t.vert(w1,hi-.12),t.vert(o1,lo-.14),t.vert(o0,lo-.14),Vector3.DOWN)
  t.quad(trim,t.vert(o0,lo-.16),t.vert(o1,lo-.16),t.vert(o1,lo+.01),t.vert(o0,lo+.01),normal)
  t.quad(trim,t.vert(w0,hi-.12),t.vert(o0,lo-.16),t.vert(o0,lo+.01),t.vert(w0,hi),Vector3(-line.x,0,-line.y))
  t.quad(trim,t.vert(w1,hi-.12),t.vert(o1,lo-.16),t.vert(o1,lo+.01),t.vert(w1,hi),Vector3(line.x,0,line.y))
  for post in [o0+line*.12-out*.12,o1-line*.12-out*.12]:
   box(t,trim,"",post,line,out,.055,.055,t.height_at(post)-.1,lo-.14)

## The roof pushed out past the walls, closed underneath by a soffit and at
## its edge by a fascia board, with gutters and downspouts along the eaves.
## Returns false (and draws nothing) when the grown outline cannot be
## triangulated, so the caller can fall back to the flush roof.
static func eave_roof(t,ring:PackedVector2Array,roof_key:String,axis:Vector2,offset:float,width:float,eave:float,rise:float,overhang:float)->bool:
 var turn:=outward_sign(ring)
 var outer:=grow(ring,overhang,turn)
 var triangles:=Geometry2D.triangulate_polygon(outer)
 if triangles.is_empty():return false
 var roof=t.tool(roof_key);var side:float=t.side
 for j in range(0,triangles.size(),3):
  var face=[outer[triangles[j]],outer[triangles[j+1]],outer[triangles[j+2]]]
  for sign_side in [-1.,1.]:
   var part=t.roof_half(face,axis,offset,sign_side)
   for i in range(1,part.size()-1):
    var p0:Vector2=part[0];var p1:Vector2=part[i];var p2:Vector2=part[i+1]
    var va:Vector3=t.vert(p0,height(p0,axis,offset,width,eave,rise));var vb:Vector3=t.vert(p1,height(p1,axis,offset,width,eave,rise));var vc:Vector3=t.vert(p2,height(p2,axis,offset,width,eave,rise))
    var n:=(vb-va).cross(vc-va).normalized()
    if n.y<0:n=-n
    t.tri(roof,va,vb,vc,p0/side,p1/side,p2/side,n)
 var trim:="house_trim"
 for i in ring.size():
  var a:=ring[i];var d:=ring[(i+1)%ring.size()];var oa:=outer[i];var od:=outer[(i+1)%ring.size()]
  var out:=normal_of(ring,i,turn);var normal:=Vector3(out.x,0,out.y)
  var inner:=[a];var edge:=[oa]
  var da:=a.dot(axis)-offset;var dd:=d.dot(axis)-offset;var ea:=oa.dot(axis)-offset;var ed:=od.dot(axis)-offset
  if da*dd<0 and ea*ed<0:
   inner.append(a.lerp(d,da/(da-dd)));edge.append(oa.lerp(od,ea/(ea-ed)))
  inner.append(d);edge.append(od)
  for k in range(1,edge.size()):
   var l:Vector2=edge[k-1];var r:Vector2=edge[k];var il:Vector2=inner[k-1];var ir:Vector2=inner[k]
   var hl:=height(l,axis,offset,width,eave,rise);var hr:=height(r,axis,offset,width,eave,rise)
   t.quad(trim,t.vert(l,hl-FASCIA),t.vert(r,hr-FASCIA),t.vert(r,hr),t.vert(l,hl),normal)
   t.quad(trim,t.vert(il,height(il,axis,offset,width,eave,rise)-FASCIA),t.vert(ir,height(ir,axis,offset,width,eave,rise)-FASCIA),t.vert(r,hr-FASCIA),t.vert(l,hl-FASCIA),Vector3.DOWN)
  # Gutters hang on eaves (walls parallel to the ridge); downspouts on long ones.
  var length:=a.distance_to(d)
  if absf(out.dot(axis))<.9 or length<3.:continue
  var line:=(d-a)/length;var h:=height(oa,axis,offset,width,eave,rise)
  var ga:=oa+out*.11;var gd:=od+out*.11
  t.quad(trim,t.vert(ga,h-.17),t.vert(gd,h-.17),t.vert(gd,h-.03),t.vert(ga,h-.03),normal)
  t.quad(trim,t.vert(oa,h-.17),t.vert(od,h-.17),t.vert(gd,h-.17),t.vert(ga,h-.17),Vector3.DOWN)
  if length<6.:continue
  for e in [-1.,1.]:
   # Front face and the end facing the nearer corner (the one seen past the house).
   var spout:Vector2=(a+line*.3 if e<0 else d-line*.3)+out*.05
   var foot:float=t.height_at(spout)-.05;var head:=height(spout,axis,offset,width,eave,rise)-FASCIA
   panel(t,trim,spout+out*.035,line,.04,foot,head,normal)
   t.quad(trim,t.vert(spout+line*.04*e,foot),t.vert(spout+line*.04*e+out*.035,foot),t.vert(spout+line*.04*e+out*.035,head),t.vert(spout+line*.04*e,head),Vector3(line.x*e,0,line.y*e))
 return true

## A brick chimney through the roof on about three houses in ten.
static func chimney(t,ring:PackedVector2Array,id:int,axis:Vector2,offset:float,width:float,eave:float,rise:float,wall_key:String):
 if rise<1. or (id>>14)%10>=3:return
 var along:=axis.orthogonal()
 var lo:=INF;var hi:=-INF
 for p in ring:lo=minf(lo,p.dot(along));hi=maxf(hi,p.dot(along))
 if hi-lo<6.:return
 var s:=lerpf(lo,hi,.22 if (id>>17)%2==0 else .78)
 var across:=offset+width*(.32 if (id>>18)%2==0 else -.32)
 var c:Vector2=axis*across+along*s
 var bottom:=INF
 for e in [Vector2(-1,-1),Vector2(1,-1),Vector2(1,1),Vector2(-1,1)]:
  var corner:Vector2=c+axis*.38*e.x+along*.55*e.y
  if not Geometry2D.is_point_in_polygon(corner,ring):return
  bottom=minf(bottom,height(corner,axis,offset,width,eave,rise))
 var brick:="siding%d"%(12+id%4) if t.materials.has("siding15") else wall_key
 box(t,brick,pick(t,"cap","house_trim"),c,along,axis,.55,.38,bottom-.1,eave+rise+.75,true)

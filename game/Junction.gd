extends RefCounted
static func contains_canopy(t,building):
 var canopies=t.info.get("street_objects",{}).get("canopies",[])
 if canopies.is_empty():return false
 var c=t.point([building[4],building[5]])
 var outline=PackedVector2Array()
 for v in building[0][0]:outline.append(t.point(v))
 for f in canopies:
  var poly=PackedVector2Array();var center=Vector2.ZERO
  for v in f.ring:var p=t.point(v);poly.append(p);center+=p/f.ring.size()
  if Geometry2D.is_point_in_polygon(c,poly) or Geometry2D.is_point_in_polygon(center,outline):return true
 return false
static func box(t,center:Vector3,axis:Vector2,dimensions:Vector3,key):
 var a=Vector3(axis.x,0,axis.y);var b=Vector3(-axis.y,0,axis.x)
 var h=Vector3.UP*dimensions.y*.5;var x=a*dimensions.x*.5;var z=b*dimensions.z*.5
 var corners=[center-x-z,center+x-z,center+x+z,center-x+z]
 for i in 4:
  var v=corners[i];var w=corners[(i+1)%4]
  t.quad(key,v-h,w-h,w+h,v+h,((v+w)*.5-center).normalized())
 t.quad(key,corners[0]+h,corners[1]+h,corners[2]+h,corners[3]+h,Vector3.UP)
 t.quad(key,corners[3]-h,corners[2]-h,corners[1]-h,corners[0]-h,Vector3.DOWN)
static func build_canopy(t,f):
 var pts=[];var c=Vector2.ZERO;var a=Vector2.RIGHT;var long_sq=0.
 for v in f.ring:pts.append(t.point(v));c+=t.point(v)/f.ring.size()
 for i in pts.size():
  var delta=pts[(i+1)%pts.size()]-pts[i]
  if delta.length_squared()>long_sq:long_sq=delta.length_squared();a=delta.normalized()
 var across=a.orthogonal();var length=0.;var width=0.
 for p in pts:length=maxf(length,2.*absf((p-c).dot(a)));width=maxf(width,2.*absf((p-c).dot(across)))
 var deck=t.height_at(c)+(float(t.capture.record.roof_height)-.31 if not t.capture.record.is_empty() else 4.45);var key="fuel_red" if "exxon" in str(f.brand).to_lower() else ("fuel_green" if str(f.brand).to_lower()=="bp" else "fuel_steel")
 box(t,t.vert(c,deck),a,Vector3(length,.62,width),"fuel_white")
 box(t,t.vert(c,deck+.12),a,Vector3(length+.06,.27,width+.06),key)
 if not t.capture.record.is_empty():t.capture.fascia(t,f,deck)
 var count=clampi(roundi(length/7.6),1,8)
 for i in count:
  var p=c+a*((i+.5)/count-.5)*length
  var floor_y=t.height_at(p)
  t.post(t.vert(p,floor_y),t.vert(p,deck-.29),.12,.12,"fuel_white",8)
  box(t,t.vert(p,floor_y+.11),a,Vector3(2.25,.22,1.13),"cap")
  box(t,t.vert(p,floor_y+.85),a,Vector3(1.03,1.48,.64),"fuel_white")
  box(t,t.vert(p,floor_y+1.28),a,Vector3(1.06,.43,.69),"house_glass")
  box(t,t.vert(p,floor_y+.51),a,Vector3(1.06,.55,.69),key)
  if not t.capture.record.is_empty() and str(f.brand).to_lower()=="exxon":t.capture.dispenser(t,p,across,floor_y)
  # No pump collider on a mapped road (WorldTile.wall_on_road).
  if not t.wall_on_road(p-a*.56,p+a*.56,floor_y+1.65):
   var shape=BoxShape3D.new();shape.size=Vector3(1.12,1.65,.74)
   var body=StaticBody3D.new();body.name="FuelPump";var col=CollisionShape3D.new();col.shape=shape
   body.add_child(col);body.position=t.vert(p,floor_y+.825);body.rotation.y=-a.angle();t.add_child(body,true)
  for signum in [-1.,1.]:
   var light=p+across*width*.30*signum
   box(t,t.vert(light,deck-.322),a,Vector3(.48,.034,.30),"globe")
static func build_signal(t,uv):
 var p=t.point(uv);var best=[];var distance=INF
 for seg in t.segments:
  if seg[5] not in ["motorway","trunk","primary","secondary","tertiary","minor"]:continue
  var q=Geometry2D.get_closest_point_to_segment(p,seg[0],seg[1])
  var score=q.distance_to(p)+(0. if seg[5] in ["trunk","primary"] else 3.)
  if score<distance:distance=score;best=seg
 if best.is_empty() or distance>25.:return
 var dir=(best[1]-best[0]).normalized()
 if best[4]==-1:dir=-dir
 var right=Vector2(-dir.y,dir.x)
 var pole=p+dir*9.+right*(best[3]*.5+1.7);var y=t.height_at(pole)
 var top=t.vert(pole,y+6.35);var arm=top+Vector3(-right.x,0,-right.y)*(best[3]+1.55)
 t.post(t.vert(pole,y),top,.15,.09,"signal_steel",9)
 t.post(top,arm,.102,.057,"signal_steel",8)
 for i in 2:
  var h=top.lerp(arm,.47+.38*i)
  box(t,h-Vector3.UP*.57,dir,Vector3(.25,.98,.34),"signal_yellow")
  var face=Vector3(-dir.x,0,-dir.y)
  for lens in 3:
   var mid=h-Vector3.UP*(.27+.30*lens)+face*.14
   t.post(mid,mid+face*.084,.128,.119,"signal_dark",10)
   var center=mid+face*.086
   var rim=right*.098
   var m=Vector3(rim.x,0,rim.y)
   var up=Vector3.UP*.098
   t.quad("signal_go" if lens==2 else "signal_dark",center-m-up,center+m-up,center+m+up,center-m+up,face)
 var bar=p-dir*6.;var off=right*best[3]*.46;var extent=dir*.17
 for j in 6:
  var q=bar-off+off*2.*j/6.;var r=bar-off+off*2.*(j+1)/6.
  t.quad("whitepaint",t.vert(q-extent,t.height_at(q-extent)+.047),t.vert(q+extent,t.height_at(q+extent)+.047),t.vert(r+extent,t.height_at(r+extent)+.047),t.vert(r-extent,t.height_at(r-extent)+.047),Vector3.UP)
static func build(t):
 for f in t.info.get("street_objects",{}).get("canopies",[]):build_canopy(t,f)
 if not t.capture.record.is_empty():t.capture.build_fixtures(t)
 else:
  for p in t.info.get("street_objects",{}).get("signals",[]):build_signal(t,p)

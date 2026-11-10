extends RefCounted
## Bridge decks from the helper's `bridges` rows: [class, name, points in tile
## units, width, deck height per point, layer, oneway]. The helper sets each
## deck straight in profile between the road benches of its approaches, because
## a bare-earth elevation model has no deck: without this the car dropped into
## the creek or valley under every bridge.
##
## Geometry per bridge, all batched into the tile's shared meshes: an asphalt
## deck the width of the road plus 0.6 m shoulders, edge and centre lines,
## concrete parapets 0.85 m high on both sides (they collide, so a car stays on
## the bridge), a 1.1 m slab with fascia and soffit, and pier pairs every 25 m
## wherever the ground is more than 2 m below the soffit.
##
## One surface. The deck's driving surface is one set of quads (deck_quads),
## kept in tile.deck_surface (a RoadSurface.gd quad index). The deck collider is
## built from exactly those quads, and WorldTile.deck_at() and
## height_at(p, near_y) read them, so the tyres and every height query agree on
## a bridge (checks/RoadProfile.gd fails a tile where they differ by >0.1 m).
## Before, deck_at() interpolated the helper's centreline while the collider
## was a separate mesh, and the parapets shared its collider.
##
## Continuity with the approaches. The helper's end heights come from the
## approach profile it fitted, but what the tyres meet is the ribbon this tile
## actually built (RoadSurface.gd: joined onto crossing roads, banked in curves)
## or the ground. A deck that missed it left a face that stopped cars dead
## (Allston MA, 1.5-2.7 m) or a step that launched them (Stowe VT, 0.48 m), and
## a deck whose grade differed from its approach left a kink. So every real deck
## end (not a cut at the tile edge, and not a joint with another deck row) is
## fitted to the surface it meets:
##   * height and grade: the deck is resampled every 2 m and a C1 correction
##     moves its end onto that surface with the approach's grade, fading to the
##     helper's profile over 30 m (half the deck when both ends are fitted);
##     the fade never runs past a cut at the tile edge, so the neighbouring
##     tile's part of the deck still meets this one exactly;
##   * cross slope: the deck starts with the approach ribbon's superelevation
##     and fades to level over the same distance;
##   * only a surface within 4 m of the helper's end height counts, so a road
##     passing under the abutment never pulls the deck down.
const SHOULDER_M=.6
const PARAPET_W=.45
const PARAPET_H=.85
const SLAB_M=1.1
const PIER_SPACING_M=25.
const PIER_CLEAR_M=2.
const PAINT_LIFT=.012
const RESAMPLE_M=2.
const RAMP_M=30.
const FIT_REACH_M=4.
const MAX_GRADE=.15
const MAX_CROSSFALL=.08
const EDGE_EPS_M=.05
const JOINT_M=1.

static var _asphalt:StandardMaterial3D
static var _concrete:StandardMaterial3D

# Before the ground mesh (after the road ribbons): read the rows, fit their
# ends, index the driving quads, and push terrain grid nodes that would poke
# through a deck just below it (as RoadSurface.gd does under ribbons), so the
# abutment ground never stands above the deck surface.
static func prepare(tile)->void:
 var rows:Array=tile.info.get("bridges",[])
 if rows.is_empty():return
 var decks=[]
 for row in rows:
  if not row is Array or row.size()<5:continue
  var pts:Array=row[2];var heights:Array=row[4]
  if pts.size()<2 or pts.size()!=heights.size():continue
  var path=PackedVector3Array()
  for k in pts.size():
   var p=Vector3(float(pts[k][0])*tile.side,float(heights[k]),float(pts[k][1])*tile.side)
   # A segment shorter than 0.3 m has no reliable direction; its parapets would
   # stand across the road.
   if path.size()>0 and Vector2(p.x-path[-1].x,p.z-path[-1].z).length()<.3:continue
   path.append(p)
  if path.size()<2:continue
  decks.append({"path":resample(path),"width":float(row[3]),"cls":str(row[0]),"oneway":row.size()>6 and int(row[6])!=0})
 # Every end is fitted against the helper's heights of the other decks, so the
 # order of the rows does not matter.
 var helper=[]
 for d in decks:helper.append({"path":d.path.duplicate(),"half":d.width*.5+SHOULDER_M})
 var stats={"ends":0,"fitted":0,"joints":0,"max_shift":0.}
 for i in decks.size():fit(tile,decks[i],helper,i,stats)
 tile.info["bridge_fit"]=stats
 var index=preload("res://RoadSurface.gd").new()
 var owner=PackedInt32Array()
 for i in decks.size():
  var d=decks[i]
  d["quads"]=deck_quads(d)
  for q in d.quads:index.add_quad(q[0],q[1],q[2],q[3]);owner.append(i)
 if index.quads.is_empty():return
 index.active=true
 # Which deck each quad belongs to (drivable_at skips the deck asking).
 index.set_meta("owner",owner)
 tile.deck_surface=index
 tile.bridge_decks=decks
 tile.info["deck_lowered"]=index.lower_grid(tile.info.h,tile.grid_steps,tile.side,tile.grid_bilinear())

# After the ground mesh: draw the prepared decks and build their collider from
# the same quads deck_at() and height_at() read.
static func build(tile)->int:
 var decks:Array=tile.bridge_decks
 if decks.is_empty():return 0
 ensure_materials(tile.materials)
 var faces=PackedVector3Array()
 var open=0.
 for i in decks.size():
  var d=decks[i]
  for q in d.quads:faces.append_array(PackedVector3Array([q[0],q[1],q[2],q[0],q[2],q[3]]))
  deck(tile,d,decks,i)
  open+=d.get("open_m",0.)
 tile.info["bridge_open_parapet_m"]=snappedf(open,.1)
 var shape=ConcavePolygonShape3D.new();shape.backface_collision=true;shape.set_faces(faces)
 tile.collider(shape,"deck")
 return decks.size()

# The driving surface: one quad per path segment, the deck width plus shoulders,
# tilted by the cross slope. Corners: a left, b left, b right, a right.
static func deck_quads(d:Dictionary)->Array:
 var path:PackedVector3Array=d.path;var crossfall:PackedFloat64Array=d.crossfall
 var n=normals(path);var half=d.width*.5+SHOULDER_M
 var out=[]
 for k in range(1,path.size()):
  var a=path[k-1];var b=path[k]
  if Vector2(b.x-a.x,b.z-a.z).length()<.01:continue
  out.append([at(a,n[k-1],half,0,crossfall[k-1]),at(b,n[k],half,0,crossfall[k]),at(b,n[k],-half,0,crossfall[k]),at(a,n[k-1],-half,0,crossfall[k-1])])
 return out

# Points no more than RESAMPLE_M apart, so a height correction has vertices to act on.
static func resample(path:PackedVector3Array)->PackedVector3Array:
 var out=PackedVector3Array([path[0]])
 for k in range(1,path.size()):
  var a=path[k-1];var b=path[k]
  var n=maxi(1,ceili(Vector2(b.x-a.x,b.z-a.z).length()/RESAMPLE_M))
  for j in range(1,n+1):out.append(a.lerp(b,float(j)/n))
 return out

static func stations(path:PackedVector3Array)->PackedFloat64Array:
 var s=PackedFloat64Array([0.])
 for k in range(1,path.size()):s.append(s[k-1]+Vector2(path[k].x-path[k-1].x,path[k].z-path[k-1].z).length())
 return s

static func on_tile_edge(tile,p:Vector3)->bool:
 return p.x<=EDGE_EPS_M or p.z<=EDGE_EPS_M or p.x>=tile.side-EDGE_EPS_M or p.z>=tile.side-EDGE_EPS_M

# Heights of the roads at p other than deck `self_index`: road ribbons and the
# other decks (their helper profile).
static func roads_at(tile,p:Vector2,decks:Array,self_index:int)->PackedFloat64Array:
 var out=PackedFloat64Array()
 if tile.road_surface:
  for h in tile.road_surface.heights(p):out.append(h)
 for j in decks.size():
  if j==self_index:continue
  var h=polyline_height(decks[j].path,p,decks[j].half)
  if not is_nan(h):out.append(h)
 return out

# Height of a deck centreline polyline at p when p is within `reach` of it.
static func polyline_height(path:PackedVector3Array,p:Vector2,reach:float)->float:
 var best=NAN;var best_d=reach
 for k in range(1,path.size()):
  var a=Vector2(path[k-1].x,path[k-1].z);var b=Vector2(path[k].x,path[k].z);var ab=b-a;var l2=ab.length_squared()
  if l2<1e-6:continue
  var t=clampf((p-a).dot(ab)/l2,0.,1.);var d=p.distance_to(a+ab*t)
  if d<=best_d:best_d=d;best=lerpf(path[k-1].y,path[k].y,t)
 return best

static func closest(values:PackedFloat64Array,to:float)->float:
 var best=NAN
 for v in values:
  if is_nan(best) or absf(v-to)<absf(best-to):best=v
 return best

# An end where another deck row continues (the helper split one bridge chain
# where its class, width or direction changes): both rows share the helper's
# profile there already, so neither is moved.
static func is_joint(at:Vector2,z0:float,decks:Array,self_index:int)->bool:
 for j in decks.size():
  if j==self_index:continue
  var h=polyline_height(decks[j].path,at,JOINT_M)
  if not is_nan(h) and absf(h-z0)<.3:return true
 return false

# The surface a deck end meets: the nearest road within 0.5-2.5 m beyond the
# end (a ribbon can stop short of the abutment), else the ground. Returns
# [height, distance out it was read at, grade climbing into the deck per metre
# (NAN unknown), cross slope in the deck's normal], or [] when none is near z0.
static func approach(tile,at:Vector2,inward:Vector2,normal:Vector2,half:float,z0:float,decks:Array,self_index:int)->Array:
 for out in [.5,1.5,2.5]:
  var p=at-inward*out
  var c1=closest(roads_at(tile,p,decks,self_index),z0)
  if is_nan(c1) or absf(c1-z0)>FIT_REACH_M:continue
  var c2=closest(roads_at(tile,p-inward*4.,decks,self_index),c1)
  var grade=clampf((c1-c2)/4.,-MAX_GRADE,MAX_GRADE) if not is_nan(c2) and absf(c2-c1)<4.*MAX_GRADE+.3 else NAN
  var e=0.;var o=minf(3.,half-.9)
  if o>.5:
   var hl=closest(roads_at(tile,p+normal*o,decks,self_index),c1);var hr=closest(roads_at(tile,p-normal*o,decks,self_index),c1)
   if not is_nan(hl) and not is_nan(hr) and absf(hl-hr)<2.*o*MAX_CROSSFALL+.05:e=clampf(-(hl-hr)/(2.*o),-MAX_CROSSFALL,MAX_CROSSFALL)
  return [c1,out,grade,e]
 # No road: the approach is on the ground (a track, or a road without a ribbon).
 var g1=tile.height_at(at-inward*.5)
 if absf(g1-z0)>FIT_REACH_M:return []
 var g2=tile.height_at(at-inward*4.5)
 return [g1,.5,clampf((g1-g2)/4.,-MAX_GRADE,MAX_GRADE),0.]

# Fit both real ends of deck d to the surface it meets (see the header).
static func fit(tile,d:Dictionary,decks:Array,self_index:int,stats:Dictionary)->void:
 var path:PackedVector3Array=d.path
 var crossfall=PackedFloat64Array();crossfall.resize(path.size());crossfall.fill(0.)
 d["crossfall"]=crossfall
 var s=stations(path);var L=s[-1]
 if L<1.:return
 var n=normals(path)
 var ends=[];var cuts=0
 for e in [0,path.size()-1]:
  if on_tile_edge(tile,path[e]):cuts+=1;continue
  stats.ends+=1
  var other=path[1] if e==0 else path[-2]
  var inward=Vector2(other.x-path[e].x,other.z-path[e].z).normalized()
  var at=Vector2(path[e].x,path[e].z)
  var z0=path[e].y
  if is_joint(at,z0,decks,self_index):stats.joints+=1;continue
  var meet=approach(tile,at,inward,n[e],d.width*.5,z0,decks,self_index)
  if meet.is_empty():continue
  var grade:float=meet[2]
  var target:float=meet[0]+(grade*meet[1] if not is_nan(grade) else 0.)
  ends.append({"index":e,"shift":target-z0,"grade":grade,"e":meet[3]})
  stats.fitted+=1;stats.max_shift=maxf(stats.max_shift,absf(target-z0))
 if ends.is_empty():return
 # Fade length per end: 30 m, at most half the deck when both ends are fitted,
 # and never past a cut at the tile edge, where the neighbouring tile's part of
 # the deck (fitted by that tile) must meet this one unchanged.
 var R=minf(RAMP_M,L*(.5 if ends.size()==2 else 1.))
 var base=PackedFloat64Array()
 for k in path.size():base.append(path[k].y)
 var shift_a=0.;var shift_b=0.
 for end in ends:
  if end.index==0:shift_a=end.shift
  else:shift_b=end.shift
 for k in path.size():
  var y=base[k]
  # Height: a smoothstep from each end's shift to none, flat at both ends of the
  # fade. A short deck fitted at both ends blends straight from one shift to
  # the other instead.
  if ends.size()==2 and L<2.*RAMP_M:
   y+=shift_a+(shift_b-shift_a)*smooth(s[k]/L)
  else:
   for end in ends:
    var dist=s[k] if end.index==0 else L-s[k]
    y+=end.shift*(1.-smooth(dist/R))
  for end in ends:
   var dist=s[k] if end.index==0 else L-s[k]
   # Cross slope: the approach's at the end, level by R.
   crossfall[k]+=end.e*(1.-smooth(dist/R))
   # Grade: f(x)=Δg·x·(1-x/R)² turns the deck's end grade into the approach's,
   # and is zero with zero slope at R.
   if is_nan(end.grade) or dist>=R:continue
   var i=end.index;var j=1 if i==0 else path.size()-2
   var own=(base[j]-base[i])/maxf(.01,absf(s[j]-s[i]))
   y+=(end.grade-own)*dist*pow(1.-dist/R,2.)
  path[k]=Vector3(path[k].x,y,path[k].z)
 d["path"]=path
 d["crossfall"]=crossfall

static func smooth(x:float)->float:
 var t=clampf(x,0.,1.)
 return t*t*(3.-2.*t)

static func ensure_materials(materials:Dictionary)->void:
 if _asphalt==null:
  _asphalt=StandardMaterial3D.new()
  _asphalt.albedo_texture=preload("res://art/asphalt.jpg")
  # The shared asphalt photograph is pale; this tint matches the terrain shader's
  # road grey (#484a49 under the same grit texture).
  _asphalt.albedo_color=Color(.40,.41,.41)
  _asphalt.roughness=.9
  _asphalt.texture_filter=BaseMaterial3D.TEXTURE_FILTER_LINEAR_WITH_MIPMAPS_ANISOTROPIC
 if _concrete==null:
  # Cast-in-place concrete: a pale warm grey with a soft procedural normal map
  # for form-board and weathering variation. Generated, so nothing to license.
  _concrete=StandardMaterial3D.new()
  _concrete.albedo_color=Color(.63,.62,.58)
  _concrete.roughness=.88
  var noise=FastNoiseLite.new();noise.frequency=.05;noise.fractal_octaves=4
  var bump=NoiseTexture2D.new();bump.width=256;bump.height=256;bump.seamless=true;bump.as_normal_map=true;bump.bump_strength=2.5;bump.noise=noise
  _concrete.normal_enabled=true;_concrete.normal_texture=bump;_concrete.normal_scale=.35
  _concrete.uv1_triplanar=true;_concrete.uv1_world_triplanar=true;_concrete.uv1_scale=Vector3(.25,.25,.25)
 if not materials.has("bridge_asphalt"):materials["bridge_asphalt"]=_asphalt
 if not materials.has("bridge_concrete"):materials["bridge_concrete"]=_concrete
 if not materials.has("whitepaint"):var w=StandardMaterial3D.new();w.albedo_color=Color("#d8d5c2");materials["whitepaint"]=w
 if not materials.has("yellowpaint"):var y=StandardMaterial3D.new();y.albedo_color=Color("#c6a354");materials["yellowpaint"]=y

# Left-pointing horizontal unit normal at each point, mitred at the joints.
static func normals(path:PackedVector3Array)->PackedVector2Array:
 var out=PackedVector2Array()
 for k in path.size():
  var a=path[maxi(k-1,0)];var b=path[mini(k+1,path.size()-1)]
  var t=Vector2(b.x-a.x,b.z-a.z).normalized()
  var n=Vector2(-t.y,t.x)
  if k>0 and k<path.size()-1:
   var t1=Vector2(path[k].x-path[k-1].x,path[k].z-path[k-1].z).normalized()
   var miter=n.dot(Vector2(-t1.y,t1.x))
   n/=clampf(miter,.5,1.)
  out.append(n)
 return out

# A point `offset` along the normal from the centreline, on a deck whose cross
# slope e lowers the side the normal points to (the helper's convention).
static func at(p:Vector3,n:Vector2,offset:float,lift:float,e:=0.)->Vector3:
 return Vector3(p.x+n.x*offset,p.y+lift-e*offset,p.z+n.y*offset)

# Whether a car can drive at p near height y on something other than deck
# `self_index`: another deck (a twin carriageway or a ramp joining the bridge)
# or a road ribbon at deck level (a street meeting the abutment).
const OPEN_BELOW_M=1.5
const OPEN_ABOVE_M=1.
static func drivable_at(tile,p:Vector2,y:float,_decks:Array,self_index:int)->bool:
 var s=tile.deck_surface
 if s:
  var owner:PackedInt32Array=s.get_meta("owner",PackedInt32Array())
  var bin:float=preload("res://RoadSurface.gd").BIN
  for idx in s.bins.get(Vector2i(floori(p.x/bin),floori(p.y/bin)),[]):
   if idx<owner.size() and owner[idx]==self_index:continue
   var q:Array=s.quads[idx]
   var h=s.tri_height(p,q[0],q[1],q[2])
   if is_nan(h):h=s.tri_height(p,q[0],q[2],q[3])
   if not is_nan(h) and h>y-OPEN_BELOW_M and h<y+OPEN_ABOVE_M:return true
 if tile.road_surface:
  for h in tile.road_surface.heights(p):
   if h>y-OPEN_BELOW_M and h<y+OPEN_ABOVE_M:return true
 return false

# decks and self_index: a parapet piece standing on another drivable surface
# at deck level is left out, so it never walls off a twin carriageway whose
# deck overlaps this one, or a street joining at the abutment (Allston MA:
# the westbound Cambridge St parapet stood inside the eastbound lanes).
static func deck(tile,d:Dictionary,decks:=[],self_index:=-1)->void:
 var path:PackedVector3Array=d.path;var crossfall:PackedFloat64Array=d.crossfall;var width:float=d.width
 var n=normals(path)
 var half=width*.5+SHOULDER_M
 var outer=half+PARAPET_W
 var station=0.
 for k in range(1,path.size()):
  var a=path[k-1];var b=path[k];var na=n[k-1];var nb=n[k]
  var ea=crossfall[k-1];var eb=crossfall[k]
  var length=Vector2(b.x-a.x,b.z-a.z).length()
  if length<.01:continue
  var v0=station/3.;var v1=(station+length)/3.
  # Deck surface: drawn here; it collides through the deck collider built from
  # the same quads (deck_quads, build).
  surface(tile,"bridge_asphalt",at(a,na,half,0,ea),at(b,nb,half,0,eb),at(b,nb,-half,0,eb),at(a,na,-half,0,ea),Vector2(half*2/3.,v1-v0),false)
  for side_sign in [1.,-1.]:
   var s=side_sign
   var open=false
   for f in [0.,.5,1.]:
    var c=at(a.lerp(b,f),na.lerp(nb,f).normalized(),s*(half+PARAPET_W*.5),0,lerpf(ea,eb,f))
    open=open or drivable_at(tile,Vector2(c.x,c.z),c.y,decks,self_index)
   if open:
    d["open_m"]=d.get("open_m",0.)+length
    continue
   # Parapet: inner face, top, outer face; fascia below the deck edge.
   surface(tile,"bridge_concrete",at(a,na,s*half,0,ea),at(b,nb,s*half,0,eb),at(b,nb,s*half,PARAPET_H,eb),at(a,na,s*half,PARAPET_H,ea),Vector2(1,1),true)
   surface(tile,"bridge_concrete",at(a,na,s*half,PARAPET_H,ea),at(b,nb,s*half,PARAPET_H,eb),at(b,nb,s*outer,PARAPET_H,eb),at(a,na,s*outer,PARAPET_H,ea),Vector2(1,1),true)
   surface(tile,"bridge_concrete",at(a,na,s*outer,PARAPET_H,ea),at(b,nb,s*outer,PARAPET_H,eb),at(b,nb,s*outer,-SLAB_M,eb),at(a,na,s*outer,-SLAB_M,ea),Vector2(1,1),false)
  # Soffit.
  surface(tile,"bridge_concrete",at(a,na,outer,-SLAB_M,ea),at(b,nb,outer,-SLAB_M,eb),at(b,nb,-outer,-SLAB_M,eb),at(a,na,-outer,-SLAB_M,ea),Vector2(1,1),false)
  paint(tile,a,b,na,nb,width,d.cls,d.oneway,station,length,ea,eb)
  piers(tile,a,b,na,nb,half,station,length)
  station+=length

# One quad on the tile's batched mesh, optionally also in the shared collider.
static func surface(tile,key:String,p0:Vector3,p1:Vector3,p2:Vector3,p3:Vector3,tiles:Vector2,collide:bool)->void:
 var normal=(p1-p0).cross(p3-p0).normalized()
 if normal.y<0 and absf(normal.y)>.5:normal=-normal
 tile.quad(key,p0,p1,p2,p3,normal,tiles)
 if collide:tile.collision_faces.append_array(PackedVector3Array([p0,p1,p2,p0,p2,p3]))

# MUTCD-style markings: white edge lines; two-way decks get a double yellow
# centre line; lanes in the same direction are split by 10 ft dashes on a
# 40 ft cycle (3.05 m paint, 12.2 m period), anchored to the deck's station.
const DASH_M=3.05
const DASH_PERIOD_M=12.2
const LANE_M=3.65
static func paint(tile,a:Vector3,b:Vector3,na:Vector2,nb:Vector2,width:float,cls:String,oneway:bool,station:float,length:float,ea:=0.,eb:=0.)->void:
 var edge=width*.5-.25
 for s in [1.,-1.]:
  stripe(tile,"whitepaint",a,b,na,nb,s*edge,.15,ea,eb)
 var lanes=maxi(1,roundi(width/LANE_M))
 if oneway:
  for k in range(1,lanes):dashes(tile,a,b,na,nb,-width*.5+width*k/lanes,station,length,ea,eb)
  return
 for off in [-.12,.12]:stripe(tile,"yellowpaint",a,b,na,nb,off,.11,ea,eb)
 # Wide two-way decks: dashed lanes on each side of the centre line.
 var per_side=maxi(1,lanes/2)
 for k in range(1,per_side):
  for s in [1.,-1.]:dashes(tile,a,b,na,nb,s*width*.5*k/per_side,station,length,ea,eb)

static func dashes(tile,a:Vector3,b:Vector3,na:Vector2,nb:Vector2,offset:float,station:float,length:float,ea:=0.,eb:=0.)->void:
 var start=floorf(station/DASH_PERIOD_M)*DASH_PERIOD_M
 var s=start
 while s<station+length:
  var f0=clampf((s-station)/length,0.,1.);var f1=clampf((s+DASH_M-station)/length,0.,1.)
  if f1>f0:
   stripe(tile,"whitepaint",a.lerp(b,f0),a.lerp(b,f1),na.lerp(nb,f0).normalized(),na.lerp(nb,f1).normalized(),offset,.12,lerpf(ea,eb,f0),lerpf(ea,eb,f1))
  s+=DASH_PERIOD_M

static func stripe(tile,key:String,a:Vector3,b:Vector3,na:Vector2,nb:Vector2,offset:float,w:float,ea:=0.,eb:=0.)->void:
 var p0=at(a,na,offset-w*.5,PAINT_LIFT,ea);var p1=at(b,nb,offset-w*.5,PAINT_LIFT,eb);var p2=at(b,nb,offset+w*.5,PAINT_LIFT,eb);var p3=at(a,na,offset+w*.5,PAINT_LIFT,ea)
 tile.quad(key,p0,p1,p2,p3,Vector3.UP)

# Pier pairs where the ground falls well below the soffit.
static func piers(tile,a:Vector3,b:Vector3,na:Vector2,nb:Vector2,half:float,station:float,length:float)->void:
 var first=ceilf(station/PIER_SPACING_M)*PIER_SPACING_M
 var s=first
 while s<station+length:
  var f=(s-station)/length
  var c=a.lerp(b,f);var n=na.lerp(nb,f).normalized()
  for offset in [half-1.6,-(half-1.6)]:
   var base=Vector2(c.x+n.x*offset,c.z+n.y*offset)
   if base.x<0 or base.y<0 or base.x>tile.side or base.y>tile.side:continue
   var ground=tile.height_at(base)
   var top=c.y-SLAB_M
   if top-ground<PIER_CLEAR_M:continue
   tile.post(Vector3(base.x,ground-.5,base.y),Vector3(base.x,top,base.y),.55,.5,"bridge_concrete",8)
  s+=PIER_SPACING_M

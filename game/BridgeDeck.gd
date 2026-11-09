extends RefCounted
## Bridge decks from the helper's `bridges` rows: [class, name, points in tile
## units, width, deck height per point, layer]. The helper sets each deck
## straight in profile between the road benches of its approaches, because a
## bare-earth elevation model has no deck: without this the car dropped into
## the creek or valley under every bridge.
##
## Geometry per bridge, all batched into the tile's shared meshes and collider:
## an asphalt deck the width of the road plus 0.6 m shoulders, edge and centre
## lines, concrete parapets 0.85 m high on both sides (they collide, so a car
## stays on the bridge), a 1.1 m slab with fascia and soffit, and pier pairs
## every 25 m wherever the ground is more than 2 m below the soffit.
const SHOULDER_M=.6
const PARAPET_W=.45
const PARAPET_H=.85
const SLAB_M=1.1
const PIER_SPACING_M=25.
const PIER_CLEAR_M=2.
const PAINT_LIFT=.012

static var _asphalt:StandardMaterial3D
static var _concrete:StandardMaterial3D

static func build(tile)->int:
 var rows:Array=tile.info.get("bridges",[])
 if rows.is_empty():return 0
 ensure_materials(tile.materials)
 var built=0
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
  deck(tile,path,float(row[3]),str(row[0]))
  built+=1
 return built

static func ensure_materials(materials:Dictionary)->void:
 if _asphalt==null:
  _asphalt=StandardMaterial3D.new()
  _asphalt.albedo_texture=preload("res://art/asphalt.jpg")
  _asphalt.albedo_color=Color(.82,.82,.80)
  _asphalt.roughness=.92
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

static func at(p:Vector3,n:Vector2,offset:float,lift:float)->Vector3:
 return Vector3(p.x+n.x*offset,p.y+lift,p.z+n.y*offset)

static func deck(tile,path:PackedVector3Array,width:float,cls:String)->void:
 var n=normals(path)
 var half=width*.5+SHOULDER_M
 var outer=half+PARAPET_W
 var station=0.
 for k in range(1,path.size()):
  var a=path[k-1];var b=path[k];var na=n[k-1];var nb=n[k]
  var length=Vector2(b.x-a.x,b.z-a.z).length()
  if length<.01:continue
  var v0=station/3.;var v1=(station+length)/3.
  # Deck surface (drives and collides).
  surface(tile,"bridge_asphalt",at(a,na,half,0),at(b,nb,half,0),at(b,nb,-half,0),at(a,na,-half,0),Vector2(half*2/3.,v1-v0),true)
  for side_sign in [1.,-1.]:
   var s=side_sign
   # Parapet: inner face, top, outer face; fascia below the deck edge.
   surface(tile,"bridge_concrete",at(a,na,s*half,0),at(b,nb,s*half,0),at(b,nb,s*half,PARAPET_H),at(a,na,s*half,PARAPET_H),Vector2(1,1),true)
   surface(tile,"bridge_concrete",at(a,na,s*half,PARAPET_H),at(b,nb,s*half,PARAPET_H),at(b,nb,s*outer,PARAPET_H),at(a,na,s*outer,PARAPET_H),Vector2(1,1),true)
   surface(tile,"bridge_concrete",at(a,na,s*outer,PARAPET_H),at(b,nb,s*outer,PARAPET_H),at(b,nb,s*outer,-SLAB_M),at(a,na,s*outer,-SLAB_M),Vector2(1,1),false)
  # Soffit.
  surface(tile,"bridge_concrete",at(a,na,outer,-SLAB_M),at(b,nb,outer,-SLAB_M),at(b,nb,-outer,-SLAB_M),at(a,na,-outer,-SLAB_M),Vector2(1,1),false)
  paint(tile,a,b,na,nb,width,cls,station,length)
  piers(tile,a,b,na,nb,half,station,length)
  station+=length

# One quad on the tile's batched mesh, optionally also in the shared collider.
static func surface(tile,key:String,p0:Vector3,p1:Vector3,p2:Vector3,p3:Vector3,tiles:Vector2,collide:bool)->void:
 var normal=(p1-p0).cross(p3-p0).normalized()
 if normal.y<0 and absf(normal.y)>.5:normal=-normal
 tile.quad(key,p0,p1,p2,p3,normal,tiles)
 if collide:tile.collision_faces.append_array(PackedVector3Array([p0,p1,p2,p0,p2,p3]))

# Edge lines, and a centre line: double yellow on two-way rural roads.
static func paint(tile,a:Vector3,b:Vector3,na:Vector2,nb:Vector2,width:float,cls:String,station:float,length:float)->void:
 var edge=width*.5-.25
 for s in [1.,-1.]:
  stripe(tile,"whitepaint",a,b,na,nb,s*edge,.15)
 if cls in ["motorway","trunk"]:return
 for off in [-.12,.12]:stripe(tile,"yellowpaint",a,b,na,nb,off,.11)

static func stripe(tile,key:String,a:Vector3,b:Vector3,na:Vector2,nb:Vector2,offset:float,w:float)->void:
 var p0=at(a,na,offset-w*.5,PAINT_LIFT);var p1=at(b,nb,offset-w*.5,PAINT_LIFT);var p2=at(b,nb,offset+w*.5,PAINT_LIFT);var p3=at(a,na,offset+w*.5,PAINT_LIFT)
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

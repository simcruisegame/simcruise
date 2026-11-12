extends SceneTree
# Scratch probe (not committed): --dir= --tile= --from=x,y --to=x,y
const TILE=preload("res://WorldTile.gd")
const EARTH=40075016.68557849
func _initialize():
 var dir="";var id="";var a=Vector2.ZERO;var b=Vector2.ZERO
 for arg in OS.get_cmdline_user_args():
  if arg.begins_with("--dir="):dir=arg.trim_prefix("--dir=")
  elif arg.begins_with("--tile="):id=arg.trim_prefix("--tile=")
  elif arg.begins_with("--from="):var s=arg.trim_prefix("--from=").split(",");a=Vector2(float(s[0]),float(s[1]))
  elif arg.begins_with("--to="):var s=arg.trim_prefix("--to=").split(",");b=Vector2(float(s[0]),float(s[1]))
 var data=JSON.parse_string(FileAccess.get_file_as_string(dir.path_join(id+".json")))
 var side=EARTH/65536./cosh(PI*(1.-2.*(float(data.id[1])+.5)/65536.))
 var tile=TILE.new()
 tile.build(data,side,preload("res://checks/RoadProfile.gd").new().materials())
 print("side ",side," fit ",tile.info.get("bridge_fit",{}))
 for i in tile.bridge_decks.size():
  var d=tile.bridge_decks[i]
  print("deck ",i," ",d.cls," w ",d.width," ",d.path[0]," -> ",d.path[-1])
 var n=int(ceil(a.distance_to(b)/.5))
 for k in n+1:
  var p=a.lerp(b,float(k)/maxf(n,1))
  var r=tile.road_surface.heights(p) if tile.road_surface else []
  var dk=[]
  if tile.deck_surface:
   var owner=tile.deck_surface.get_meta("owner")
   for idx in tile.deck_surface.bins.get(Vector2i(floori(p.x/8.),floori(p.y/8.)),[]):
    var q=tile.deck_surface.quads[idx]
    var h=tile.deck_surface.tri_height(p,q[0],q[1],q[2])
    if is_nan(h):h=tile.deck_surface.tri_height(p,q[0],q[2],q[3])
    if not is_nan(h):dk.append("%.2f#%d"%[h,owner[idx]])
  var named=[]
  if tile.road_surface:
   for li in tile.road_surface.lines.size():
    var L=tile.road_surface.lines[li];var secs=L.sections
    for i in range(1,secs.size()):
     for kk in secs[i].size()-1:
      var qa=secs[i-1][kk];var qb=secs[i-1][kk+1];var qc=secs[i][kk+1];var qd=secs[i][kk]
      var h=tile.road_surface.tri_height(p,qa,qb,qc)
      if is_nan(h):h=tile.road_surface.tri_height(p,qa,qc,qd)
      if not is_nan(h):named.append("%s#%d@%d:%.2f"%[L.cls,li,i,h])
  print("%6.1f %6.1f ground %.2f ribbons %s decks %s %s"%[p.x,p.y,tile.terrain_height(p),str(Array(r).map(func(x):return snappedf(x,.01))),str(dk),str(named)])
 tile.free()
 quit(0)

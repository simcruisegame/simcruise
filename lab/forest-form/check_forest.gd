extends SceneTree
# godot --headless --path game --script ../lab/forest-form/check_forest.gd

func _initialize():
 var id=Vector2i(12629,23892)
 var mask=preload("res://ForestForm.gd").for_tile(id)
 if mask==null or preload("res://ForestForm.gd").for_tile(Vector2i(18166,27914))!=null:
  fail("Expected one authored tile and one absent tile");return
 var tile=preload("res://WorldTile.gd").new()
 tile.side=440.
 tile.grid_steps=1
 tile.info={"live_canopy":true,"id":[id.x,id.y],"h":[10,11,9,10]}
 var rows=[]
 for y in 12:
  for x in 12:rows.append([(x+.5)/12.,(y+.5)/12.,15.,9.])
 tile.replace_crowns(rows)
 var base=tile.grove.duplicate(true)
 tile.forest_form=mask
 tile.replace_crowns(rows)
 var eligible=0
 var changed=0
 var retained=0
 for i in rows.size():
  if tile.grove[i].slice(0,5)!=base[i].slice(0,5):
   tile.free();fail("Anchor, height, rotation, diameter or collision width changed");return
  if mask.conifer(Vector2(rows[i][0],rows[i][1])):
   eligible+=1
   if int(tile.grove[i][5])==4:changed+=1
   else:retained+=1
  elif tile.grove[i][5]!=base[i][5]:
   tile.free();fail("Form changed outside accepted cells");return
 if changed<5 or retained<1:
  tile.free();fail("Expected dominant conifer and retained illustrative forms");return
 var civic=[]
 for rec in rows:
  civic.append([rec[0],rec[1],rec[2],rec[3],.31,"City surveyed maple",1,rec[0],rec[1],0,"matched","city"])
 tile.replace_crowns(civic)
 for row in tile.grove:
  if row[5]!=1 or row[4]!=.31:
   tile.free();fail("Municipal survey was overridden");return
 print("FOREST FORM PASS sampled=",rows.size()," eligible=",eligible," inferred=",changed,
       " minority=",retained," city preserved=",tile.grove.size()," map-only changes=0")
 tile.free();quit(0)

func fail(reason):
 printerr("FOREST FORM FAILED ",reason)
 quit(2)

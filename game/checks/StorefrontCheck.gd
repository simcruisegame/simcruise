extends SceneTree
const STORE=preload("res://MappedStorefront.gd")

func _initialize():
 var outline=PackedVector2Array([Vector2(0,0),Vector2(20,0),Vector2(20,20),Vector2(0,20)])
 var bounds=Rect2(0,0,20,20)
 var rings=[[[0,0],[.2,0],[.2,.2],[0,.2]],[[.07,.07],[.14,.07],[.14,.14],[.07,.14]]]
 var building=[rings,[],7.,0.,.1,.1,"building",""]
 var entrance=[[[.03,.09],"Verified place","shop"]]
 var cases=[
  [entrance,bounds,true],
  [[[[.10,.10],"Courtyard café","cafe"]],bounds,false],
  [[[[.26,.09],"Across the road","restaurant"]],bounds,false],
  [[[[.03,.09],"Historic monument","monument"]],bounds,false],
  [[[[.03,.09],"Legacy record"]],bounds,false],
  [entrance,Rect2(0,0,60,60),false],
  [[],bounds,false]]
 for c in cases:
  if STORE.accepts(building,c[0],outline,c[1],100.)!=c[2]:
   push_error("Incorrect retail association")
   quit(1)
   return
 building[7]="retail"
 if not STORE.accepts(building,[],outline,bounds,100.):
  quit(1)
  return
 print("STOREFRONT source category, footprint, courtyard, across-road, uncategorized and existing retail checks passed")
 quit()

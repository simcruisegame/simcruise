extends SceneTree
# Normal street selection and actual drivetrain inputs on US 89 at Snake River Overlook.
const PLACE="Snake River Overlook highway check"
var game
var phase=0
var started=0
var quiet=0
var driving_frames=0
var contacts=4
var output=""
var mode=""
var origin=Vector2.ZERO
var result={}
var last_road=""
var previous_rings=0
var original_contacts=[]
var checked_contacts=false

func _initialize():
 var args=OS.get_cmdline_user_args()
 var k=args.find("--forest-drive")
 if k<0 or k+2>=args.size():quit(8);return
 mode=args[k+1];output=args[k+2]
 if mode not in ["before","after"]:quit(8);return
 started=Time.get_ticks_msec()
 game=load("res://Drive.tscn").instantiate()
 game.grove.observed_forest.disabled=(mode=="before")
 root.add_child(game)

func fail(reason):
 printerr("FOREST DRIVE FAILED ",reason," phase=",phase," tile=",game.origin," selected=",game.current_title," contacts=",game.vehicle.wheel_contact)
 phase=10
 game.request_game_close(4)

func snapshot_contacts():
 var all_contacts=[]
 for trunk in game.grove.trunks:
  if trunk.collision_layer!=0:
   var shape=trunk.get_child(0).shape
   all_contacts.append([trunk.position,shape.height,shape.radius])
 return all_contacts

func _physics_process(_dt):
 if phase==10:return false
 if Time.get_ticks_msec()-started>100000:fail("timeout");return false
 var forest=game.grove.observed_forest
 if phase==0 and game.ready_drive:
  for outside in [Vector2(12638.,23892.),Vector2(12745.,25483.),Vector2(19827.,24228.),Vector2(18166.,27914.)]:
   if forest.eligible(outside,442.)!=forest.EMPTY_REGION:fail("wrong geographic eligibility");return false
  if forest.local_surveys_read!=0 or forest.terrain_material or not forest.tiles.is_empty():fail("loaded Wyoming at initial Virginia spawn");return false
  game.choose_place(-110.622981566,43.753607726,PLACE,"North Highway 89",220.1754)
  phase=1
  return false
 if phase>=1 and phase<=3:
  if not game.ready_drive or not game.jump.is_empty() or game.current_title!=PLACE:return false
 if phase==1:
  game.view=1;game.vehicle.set_manual(false)
  if game.tiles.size()<5 or not game.tiles.get(Vector2i(12629,23892)) or not game.tiles[Vector2i(12629,23892)].info.get("canopy_ready",false):return false
  if mode=="after" and forest.tiles.is_empty():return false
  if mode=="after" and not checked_contacts:
   if forest.ground_nodes.size()!=12 or game.grove.forest_groups.is_empty():return false
   var driving_position=game.pos
   # Sample a real mapped tree off the highway during the same tick. This changes the visual/near-collider
   # query location only. The simulated rigid body and its road route retain their original transform.
   var checked_tree=false
   for id in forest.tiles:
    if not game.tiles.has(id):continue
    var tile=game.tiles[id]
    for row in tile.grove:
     var loc=Vector2(row[0].x,row[0].z)
     for measured in forest.tiles[id]:
      if loc.distance_to(measured[0]*game.side)<measured[3]*.6:
       game.pos=game.tile_offset(id)+loc
       checked_tree=true
       break
     if checked_tree:break
    if checked_tree:break
   if not checked_tree:fail("no overlapping original trunk for the collider test");return false
   game.grove.last_revision=-1;game.grove.refresh(game);original_contacts=snapshot_contacts()
   # Disable only visual evidence, then compare the normal colliders at the identical state.
   forest.disabled=true;game.grove.last_revision=-1;game.grove.refresh(game)
   if original_contacts.is_empty() or original_contacts!=snapshot_contacts():fail("trunk collision altered");return false
   game.pos=driving_position
   forest.disabled=false;game.grove.last_revision=-1;game.grove.refresh(game);checked_contacts=true
   return false
  if mode=="before" and (forest.local_surveys_read>0 or not forest.tiles.is_empty()):fail("disabled survey was active");return false
  quiet=quiet+1 if game.vehicle.wheel_contact==4 else 0
  if quiet<25:return false
  origin=game.pos;phase=2;Input.action_press("gas",.61)
  return false
 if phase==2:
  contacts=mini(contacts,game.vehicle.wheel_contact);driving_frames+=1
  if game.pos.distance_to(origin)<65.:return false
  Input.action_release("gas")
  var ground_visible=0
  for node in forest.ground_nodes.values():
   if not node is MeshInstance3D:fail("unexpected nonvisual ground node");return false
   if node.visible:ground_visible+=1
  result={"mode":mode,"route":game.road_name,"travelM":game.pos.distance_to(origin),"speedMPS":absf(game.velocity),"minWheelContacts":contacts,"physicsFrames":driving_frames,"normalCamera":game.view,"tiles":game.tiles.size(),"surveyTiles":forest.tiles.size(),"groundTiles":forest.ground_nodes.size(),"visibleGroundTiles":ground_visible,"activeTrunksAtToggle":original_contacts.size(),"unchangedTrunkColliders":checked_contacts,"geographicNegativeLocations":4,"beforeDepartureReads":forest.local_surveys_read}
  if contacts!=4 or game.road_name!="North Highway 89":fail("road or contact changed");return false
  previous_rings=forest.local_surveys_read;phase=4
  game.choose_place(-77.47826,37.55230,"Virginia exit","West Cary Street")
  return false
 if phase==4 and game.ready_drive and game.jump.is_empty() and game.current_title=="Virginia exit":
  quiet=0;phase=5
 if phase==5:
  quiet+=1
  if quiet<12:return false
  if forest.local_surveys_read!=previous_rings or forest.region!=forest.EMPTY_REGION or forest.terrain_material!=null or not forest.ground_nodes.is_empty() or not forest.tiles.is_empty() or not game.grove.survey_foliage_materials.is_empty() or not game.grove.forest_groups.is_empty():fail("survey persisted outside Wyoming");return false
  result["VirginiaExit"]={"activeSurveyTiles":forest.tiles.size(),"activeSurveyMaterials":game.grove.survey_foliage_materials.size(),"activeFarGroups":game.grove.forest_groups.size(),"newSurveyReads":forest.local_surveys_read-previous_rings}
  var file=FileAccess.open(output,FileAccess.WRITE);file.store_string(JSON.stringify(result," "));file.close()
  print("FOREST DRIVE PASSED ",JSON.stringify(result))
  phase=10;game.request_game_close(0)
 return false

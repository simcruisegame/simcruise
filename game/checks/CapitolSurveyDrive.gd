extends Node
## --capitol-drive-proof /absolute/cockpit.jpg runs the visible scene and ordinary player controls.
var game
const STOP := "Capitol Avenue · Cheyenne, WY"
var phase := 0
var started := 0
var settled := 0
var minimum_contacts := 4
var peak_speed := 0.
var start := Vector2.ZERO
var direction := Vector2.ZERO
var measured_faces := 0
var destination := ""

func _ready() -> void:
 game = get_parent()
 started = Time.get_ticks_msec()
 var args := OS.get_cmdline_user_args()
 var idx := args.find("--capitol-drive-proof")
 if idx >= 0 and idx + 1 < args.size():
  destination = args[idx + 1]

func _physics_process(_dt: float) -> void:
 if Time.get_ticks_msec() - started > 105000:
  finish(false, "timeout")
  return
 if not game.ready_drive or not game.jump.is_empty():
  return
 if phase == 0:
  game.hud.open_search()
  var clicked := false
  for choice in game.hud.choices.get_children():
   if choice is Button and choice.text == STOP:
    choice.pressed.emit()
    clicked = true
    break
  if not clicked:
   finish(false, "missing Capitol Avenue menu stop")
   return
  phase = 1
  return
 if game.current_title != STOP:
  return
 var tile = game.tiles.get(Vector2i(13686,24537))
 if tile == null or not tile.structure_layer or not tile.info.has("building_status"):
  return
 if phase == 1:
  var projected := {}
  for key in tile.structure_layer.materials:
   if str(key).begins_with("survey_projection_") and ("_1_" in str(key) or "_2_" in str(key)):
    projected[tile.structure_layer.materials[key]] = true
  for node in tile.structure_layer.find_children("*", "MeshInstance3D", true, false):
   for surface in node.mesh.get_surface_count():
    if projected.has(node.mesh.surface_get_material(surface)):
     measured_faces += node.mesh.surface_get_arrays(surface)[Mesh.ARRAY_VERTEX].size()/3
  if measured_faces < 700:
   finish(false, "missing projected scene mesh")
   return
  direction = Vector2(sin(deg_to_rad(334.)),-cos(deg_to_rad(334.)))
  game.view = 1
  game.vehicle.set_manual(false)
  phase = 2
 if phase == 2:
  settled = settled + 1 if game.vehicle.wheel_contact == 4 else 0
  if settled < 24:
   return
  start = game.pos
  phase = 3
  Input.action_press("gas", .56)
  return
 if phase != 3:
  return
 minimum_contacts = mini(minimum_contacts, game.vehicle.wheel_contact)
 peak_speed = maxf(peak_speed, absf(game.velocity))
 var travel: Vector2 = game.pos-start
 if travel.length() < 42.:
  return
 var ok: bool = minimum_contacts==4 and travel.dot(direction)>38 and game.road_name=="Capitol Avenue"
 phase = 4
 Input.action_release("gas")
 if not destination.is_empty():
  await RenderingServer.frame_post_draw
  if get_viewport().get_texture().get_image().save_jpg(destination,.94) != OK:
   ok=false
 finish(ok, "northbound approach")

func finish(ok: bool, label: String) -> void:
 set_physics_process(false)
 Input.action_release("gas")
 print("CAPITOL DRIVE ", "PASS" if ok else "FAIL", " ", label, " road=", game.road_name,
   " travel_m=", game.pos.distance_to(start), " peak_mps=", peak_speed, " min_wheels=", minimum_contacts,
   " measured_faces=", measured_faces)
 game.request_game_close(0 if ok else 1)

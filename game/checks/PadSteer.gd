extends Node
var game
var frame=0
var device=7
func _ready():
 game=get_parent()
func move_stick(amount):
 var event=InputEventJoypadMotion.new()
 event.device=device;event.axis=JOY_AXIS_LEFT_X;event.axis_value=amount
 Input.parse_input_event(event)
 Input.flush_buffered_events()
func finish(ok,message):
 move_stick(0.)
 set_physics_process(false)
 print(("PAD STEERING PASSED " if ok else "PAD STEERING FAILED ")+message)
 game.request_game_close(0 if ok else 1)
func _physics_process(_dt):
 if not game.ready_drive or not game.jump.is_empty():return
 frame+=1
 if frame==15:move_stick(-.30)
 if frame==26:
  var left=Input.get_action_strength("pad_left")
  if left<=0. or Input.get_action_strength("left")!=0. or game.vehicle.steering<=0.:
   finish(false,"left input routing "+str([left,game.vehicle.steering]));return
  move_stick(.30)
 if frame==46:
  if Input.get_action_strength("pad_right")<=0. or game.vehicle.steering>=0.:
   finish(false,"right axis direction");return
  move_stick(0.)
 if frame==68:
  if absf(game.vehicle.steering)>.0001:finish(false,"stick did not center");return
  var key=InputEventKey.new();key.physical_keycode=KEY_A;key.pressed=true;Input.parse_input_event(key)
  Input.flush_buffered_events()
 if frame==91:
  if game.vehicle.steering_input<.95:finish(false,"keyboard full steering");return
  var key=InputEventKey.new();key.physical_keycode=KEY_A;key.pressed=false;Input.parse_input_event(key)
  finish(true,"both analog directions, center and full keyboard lock")

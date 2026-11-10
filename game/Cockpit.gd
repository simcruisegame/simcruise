extends Node3D
# The close-range cabin has its own geometry layer, so the exterior windshield
# and low-detail closed dashboard cannot clip into the driver's view.
const DRIVER_LAYER := 4
const EYE := Vector3(-.347, 1.009, .503)
const SIGHT := Vector3(-.347, -2.00, -23.5)
const DRIVER_FOV := 67.0
var driver_fov := DRIVER_FOV   # set by Drive.gd from ViewLens.gd
# Driver head model. The eye is no longer welded to the chassis: a critically
# damped spring (natural frequency HEAD_OMEGA, damping ratio 1) moves it against
# the felt body acceleration, braking adds a small nose-down pitch, the gaze
# leads into the steered direction at speed, road roughness adds millimetre
# vibration and the view keeps HORIZON_FOLLOW of the body roll. head_motion
# (the remembered 0..1 preference) scales every term except the horizon.
const HEAD_OMEGA := 9.0                 # rad/s; 98 % settled ~0.8 s after a step (with ACCEL_FILTER_S)
const HEAD_LATERAL_M_PER_G := .030      # steady eye offset per g of lateral acceleration
const HEAD_LONGITUDINAL_M_PER_G := .020 # per g of braking or acceleration
const HEAD_LATERAL_MAX_M := .030
const HEAD_LONGITUDINAL_MAX_M := .020
const BRAKE_PITCH_DEG := 1.5            # nose-down at 1 g of braking
const GAZE_LEAD_DEG := 4.0              # full lead from 14 m/s upward
const HORIZON_FOLLOW := .70             # share of body roll kept by the view
const VIBRATION_M := .0022              # shake amplitude at 30 m/s on a rough surface
const ACCEL_FILTER_S := .06             # smooths the per-frame velocity difference
const G := 9.80665

var cabin: Node3D
var wheel: Node3D
var tach: Node3D
var speedometer: Node3D
var oil: Node3D
var trip_label: Label3D
var shown_tenths := -1
var rear_view: SubViewport
var rear_camera: Camera3D
var mirror_elapsed := 1.0
var camera_prepared := false
var head_motion := 1.0
var head_offset := Vector3.ZERO
var head_velocity := Vector3.ZERO
var felt := Vector3.ZERO
var previous_velocity := Vector3.ZERO
var head_started := false
var gaze_deg := 0.0
var brake_pitch_deg := 0.0
var vibration := Vector3.ZERO
var roughness := 0.0
var travel := [0.,0.,0.,0.]
var travel_slow := 0.0
var head_rng := RandomNumberGenerator.new()

func _init() -> void:
 head_rng.seed = 4181

func _ready() -> void:
 visible = false
 var document := GLTFDocument.new()
 var state := GLTFState.new()
 var bytes := FileAccess.get_file_as_bytes("res://data/nb-cockpit.meshdata")
 if document.append_from_buffer(bytes, "", state) != OK:
  push_error("Could not load Miata cabin")
  return
 cabin = document.generate_scene(state)
 add_child(cabin)
 for part in cabin.find_children("*", "MeshInstance3D", true, false):
  part.layers = DRIVER_LAYER
  part.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 finish_trim()
 wheel = cabin.find_child("Wheel", true, false)
 tach = cabin.find_child("TachNeedle", true, false)
 speedometer = cabin.find_child("SpeedNeedle", true, false)
 oil = cabin.find_child("OilNeedle", true, false)
 cabin.find_child("TempNeedle", true, false).rotation.z = deg_to_rad(4.)
 cabin.find_child("FuelNeedle", true, false).rotation.z = deg_to_rad(-30.)
 trip_label = Label3D.new()
 trip_label.name = "TripDisplay"
 trip_label.layers = DRIVER_LAYER
 trip_label.font = preload("res://art/ui.ttf")
 trip_label.font_size = 32
 trip_label.outline_size = 0
 trip_label.pixel_size = .00024
 trip_label.modulate = Color("#33372e")
 trip_label.position = Vector3(-.337, .696, -.188)
 trip_label.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
 cabin.add_child(trip_label)
 if DisplayServer.get_name() != "headless":
  setup_rear_view()

func finish_trim() -> void:
 var grain := Image.new()
 if grain.load_png_from_buffer(FileAccess.get_file_as_bytes("res://data/nb-vinyl.png")) != OK:
  return
 grain.generate_mipmaps()
 var grain_texture := ImageTexture.create_from_image(grain)
 for part in cabin.find_children("*", "MeshInstance3D", true, false):
  for i in part.mesh.get_surface_count():
   var mat: StandardMaterial3D = part.mesh.surface_get_material(i)
   if mat.resource_name in ["Dash","Door","Upper","Leather","Rubber","Trim"]:
    mat.albedo_texture = grain_texture
    mat.uv1_triplanar = true
    mat.uv1_scale = Vector3(17.0,17.0,17.0)
    mat.metallic_specular = .25
   if mat.resource_name == "Green":
    # Painted hood and mirror seen from the seat: the same racing-green
    # clearcoat as the exterior (CarFinish.gd PAINT_COLOR).
    mat.albedo_color = Color("#0f3b2a")
    mat.metallic = .2
    mat.roughness = .42
    mat.clearcoat_enabled = true
    mat.clearcoat = 1.
    mat.clearcoat_roughness = .07
   if mat.resource_name == "Leather":
    # Sports Package seats are black cloth: matte, with a finer weave than the vinyl.
    mat.albedo_color = Color("#1f2021")
    mat.roughness = .95
    mat.metallic_specular = .12
    mat.uv1_scale = Vector3(46.0,46.0,46.0)

func setup_rear_view() -> void:
 rear_view = SubViewport.new()
 rear_view.size = Vector2i(320, 88)
 rear_view.render_target_update_mode = SubViewport.UPDATE_DISABLED
 rear_view.positional_shadow_atlas_size = 0
 rear_view.world_3d = get_world_3d()
 add_child(rear_view)
 rear_camera = Camera3D.new()
 rear_camera.keep_aspect = Camera3D.KEEP_WIDTH
 rear_camera.fov = 70.0
 rear_camera.near = .2
 rear_camera.far = 225.0
 rear_camera.cull_mask = 1
 # Re-aimed from _process every 0.15 s, so it must not be interpolated either.
 rear_camera.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
 rear_view.add_child(rear_camera)
 rear_camera.current = true
 var mirror := StandardMaterial3D.new()
 mirror.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
 mirror.albedo_texture = rear_view.get_texture()
 mirror.uv1_scale = Vector3(-.67, .67, 1.0)
 mirror.uv1_offset = Vector3(.835, .165, 0.0)
 mirror.albedo_color = Color(.90,.92,.91)
 var glass: MeshInstance3D = cabin.find_child("RearGlass", true, false)
 glass.material_override = mirror
 var side_mirror: StandardMaterial3D = mirror.duplicate()
 side_mirror.uv1_scale = Vector3(-.32, .64, 1.0)
 side_mirror.uv1_offset = Vector3(.98, .18, 0.0)
 var left_glass: MeshInstance3D = cabin.find_child("LeftGlass", true, false)
 left_glass.material_override = side_mirror

func set_view(active: bool, camera: Camera3D, car, signed_speed: float, dt: float, trip_distance_m := 0.0) -> void:
 if not camera_prepared:
  camera_prepared = true
  # Drive positions this camera every rendered frame, so it must not be
  # interpolated itself (Godot 4.7 physics interpolation: nodes moved in
  # _process set physics_interpolation_mode to OFF, or they jitter).
  camera.physics_interpolation_mode = Node.PHYSICS_INTERPOLATION_MODE_OFF
 visible = active and cabin != null
 camera.set_cull_mask_value(2, not visible)
 camera.set_cull_mask_value(3, visible)
 camera.fov = driver_fov if visible else 62.0
 if not visible:
  mirror_elapsed = 1.0
  return
 # The NB steering column is inclined. Turn around its own axis, keeping the
 # top farther from the driver and preserving the wheel's inclined plane.
 wheel.basis = Basis(Vector3.RIGHT, deg_to_rad(-14.0)) * Basis(Vector3.BACK, car.steering * 14.5)
 tach.rotation.z = deg_to_rad(130.0 - 260.0 * clampf(car.rpm / 8000.0, 0.0, 1.0))
 speedometer.rotation.z = deg_to_rad(130.0 - 260.0 * clampf(absf(signed_speed) * 2.236936 / 150.0, 0.0, 1.0))
 var tenths := floori(maxf(0.0, trip_distance_m) / 160.9344)
 if shown_tenths != tenths:
  shown_tenths = tenths
  trip_label.text = "TRIP      %4.1f" % (float(tenths % 100000) / 10.0)
 oil.rotation.z = deg_to_rad(58.0 if car.stalled else 0.0)
 mirror_elapsed += dt
 if rear_view and mirror_elapsed >= .15:
  mirror_elapsed = 0.0
  var body: Transform3D = car.global_transform
  rear_camera.global_transform = body * Transform3D(Basis(Vector3.UP, PI), Vector3(.01, 1.11, .36))
  rear_view.render_target_update_mode = SubViewport.UPDATE_ONCE

# Forget the head's motion history after a teleport or a view change.
func reset_head() -> void:
 head_offset = Vector3.ZERO
 head_velocity = Vector3.ZERO
 felt = Vector3.ZERO
 head_started = false
 gaze_deg = 0.0
 brake_pitch_deg = 0.0
 vibration = Vector3.ZERO
 roughness = 0.0
 travel_slow = 0.0

# Advance the head model by one rendered frame and return the driver view as
# [eye, look target, up] in world space. body is the (interpolated) car
# transform; car supplies linear_velocity, steering and the four contact
# lengths through connected_wheels; forward_speed is the signed road speed.
func head_view(body: Transform3D, car, forward_speed: float, dt: float) -> Array:
 dt = clampf(dt, 1.0/240.0, 1.0/20.0)
 var velocity: Vector3 = car.linear_velocity
 if not head_started or velocity.distance_to(previous_velocity) > 25.0:
  previous_velocity = velocity
  head_started = true
  for i in mini(4, car.connected_wheels.size()):travel[i] = car.connected_wheels[i].position.y
 var raw := (velocity - previous_velocity) / dt
 previous_velocity = velocity
 # Specific force the seat applies, in body axes: acceleration minus gravity.
 # Spikes from contacts and tile seams are clamped before the filter.
 var specific := body.basis.inverse() * (raw + Vector3.UP * G)
 specific = specific.clamp(Vector3.ONE * -20.0, Vector3.ONE * 20.0)
 felt += (specific - felt) * (1.0 - exp(-dt / ACCEL_FILTER_S))
 # Critically damped spring toward the steady offset, opposite to the felt force.
 var target := Vector3(clampf(-felt.x / G * HEAD_LATERAL_M_PER_G, -HEAD_LATERAL_MAX_M, HEAD_LATERAL_MAX_M), 0.0, clampf(-felt.z / G * HEAD_LONGITUDINAL_M_PER_G, -HEAD_LONGITUDINAL_MAX_M, HEAD_LONGITUDINAL_MAX_M))
 var spring := HEAD_OMEGA * HEAD_OMEGA * (target - head_offset) - 2.0 * HEAD_OMEGA * head_velocity
 head_velocity += spring * dt
 head_offset += head_velocity * dt
 head_offset.x = clampf(head_offset.x, -HEAD_LATERAL_MAX_M, HEAD_LATERAL_MAX_M)
 head_offset.z = clampf(head_offset.z, -HEAD_LONGITUDINAL_MAX_M, HEAD_LONGITUDINAL_MAX_M)
 # Braking (positive felt.z, the seat pushing the driver forward) dips the gaze.
 brake_pitch_deg += (clampf(felt.z / G, 0.0, 1.0) * BRAKE_PITCH_DEG - brake_pitch_deg) * (1.0 - exp(-dt / .12))
 # Gaze lead: positive is to the right. Lateral acceleration and the rack both
 # point into the corner; Powertrain steering is positive to the left.
 var lead := clampf(felt.x / (.5 * G), -1.0, 1.0) - .5 * clampf(car.steering / .10, -1.0, 1.0)
 lead = clampf(lead, -1.0, 1.0) * smoothstep(3.0, 14.0, absf(forward_speed))
 gaze_deg += (lead * GAZE_LEAD_DEG - gaze_deg) * (1.0 - exp(-dt / .25))
 # Road roughness from the suspension travel rate, high-passed so steady roll
 # or dive does not shake the view.
 var rate := 0.0
 for i in mini(4, car.connected_wheels.size()):
  var y: float = car.connected_wheels[i].position.y
  rate += minf(1.5, absf(y - travel[i]) / dt)
  travel[i] = y
 rate *= .25
 travel_slow += (rate - travel_slow) * (1.0 - exp(-dt / .5))
 roughness += (clampf((rate - travel_slow) / .35, 0.0, 1.0) - roughness) * (1.0 - exp(-dt / .1))
 var amplitude := VIBRATION_M * (.25 + .75 * clampf(absf(forward_speed) / 30.0, 0.0, 1.0)) * (.35 + .65 * roughness)
 var shake := Vector3(head_rng.randf_range(-1.0, 1.0), head_rng.randf_range(-1.0, 1.0), .4 * head_rng.randf_range(-1.0, 1.0)) * amplitude
 vibration += (shake - vibration) * (1.0 - exp(-dt * 30.0))
 var scale := clampf(head_motion, 0.0, 1.0)
 var eye := EYE + (head_offset + vibration) * scale
 var look := Basis(Vector3.UP, -deg_to_rad(gaze_deg * scale)) * (Basis(Vector3.RIGHT, -deg_to_rad(brake_pitch_deg * scale)) * (SIGHT - EYE))
 var body_up := body.basis.y.normalized()
 var up := body_up.slerp(Vector3.UP, 1.0 - HORIZON_FOLLOW)
 if up.length_squared() < .25:up = body_up
 return [body * eye, body * (eye + look), up.normalized()]

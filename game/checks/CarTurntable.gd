extends Node
## --car-turntable OUTPUT_DIRECTORY [--car=<id>] [--sun=elevation,azimuth]
## [--lamps]: a studio sheet of the car exactly as Drive.gd built it (CarFinish
## for the 1999 roadster, CarBody for the others). Waits for the bundled start
## to load and the car to settle on its wheels, then shows only the car and a
## neutral concrete pad (camera cull mask: the car's layer and a studio layer)
## under the game's own daylight sky and a studio sun of its own, and saves
## JPEGs: front, front_34_left, front_34_right, side_left, side_right,
## rear_34_left, rear_34_right, rear, top; close-ups headlight, taillight,
## wheel, interior (through the windscreen) and cockpit (the in-game driver
## view). --lamps adds dusk shots with the head and tail lamps on
## (lamps_front, lamps_rear), the brake lamps too (lamps_brake), and the low
## beams on the bundled street with the world shown (lamps_street). Views are
## in the car's frame with a long lens, the way product photographs are taken,
## so proportions read against reference photographs. --car-dirt=<0..1> keeps
## road grime (CarWear.gd); otherwise the car is clean.
## Run windowed with --quality=ultra (4x MSAA, 4096 shadow atlas), e.g.
##   godot --path game --windowed --resolution 1600x1000 -- --car-turntable /abs/out --quality=ultra
var game
var out := ""
var clock := 0.
var still := 0.
var sun_elevation := 42.
var sun_azimuth := 215.     # degrees clockwise from the car's nose: light from front left
var lamps := false
const STUDIO_LAYER := 1 << 11
const CAR_LAYER := 2

# name, eye (car frame, metres, -Z forward, +X right), target, vertical fov
const VIEWS := [
 ["front", Vector3(0., .80, -10.5), Vector3(0., .55, 0.), 11.5],
 ["front_34_left", Vector3(-7.4, 1.35, -7.6), Vector3(0., .5, -.1), 14.],
 ["front_34_right", Vector3(7.4, 1.35, -7.6), Vector3(0., .5, -.1), 14.],
 ["side_left", Vector3(-11.5, .75, 0.), Vector3(0., .62, 0.), 15.],
 ["side_right", Vector3(11.5, .75, 0.), Vector3(0., .62, 0.), 15.],
 ["rear_34_left", Vector3(-7.2, 1.45, 7.8), Vector3(0., .55, .1), 14.],
 ["rear_34_right", Vector3(7.2, 1.45, 7.8), Vector3(0., .55, .1), 14.],
 ["rear", Vector3(0., .95, 10.5), Vector3(0., .58, 0.), 11.5],
 ["top", Vector3(0., 16., .001), Vector3(0., 0., 0.), 16.],
 ["headlight", Vector3(-1.75, .78, -3.75), Vector3(-.52, .5, -1.86), 18.],
 ["taillight", Vector3(1.9, .95, 3.55), Vector3(.55, .68, 1.82), 20.],
 ["wheel", Vector3(-3.1, .55, -1.75), Vector3(-.78, .3, -1.13), 17.],
 ["interior", Vector3(-.35, 1.85, -2.45), Vector3(-.15, .78, .25), 36.],
]

func _ready():
 game = get_parent()
 var args := OS.get_cmdline_user_args()
 out = args[args.find("--car-turntable") + 1]
 DirAccess.make_dir_recursive_absolute(out)
 lamps = "--lamps" in args
 for a in args:
  if a.begins_with("--sun="):
   var p := a.trim_prefix("--sun=").split(",")
   if p.size() == 2:sun_elevation = float(p[0]);sun_azimuth = float(p[1])
 game.live_weather = false
 # Hold the car on the rear parking brake (no stop-lamp glow) so it cannot creep.
 Input.action_press("parking_brake")

func _process(dt):
 if not game.ready_drive or not game.jump.is_empty() or game.job:return
 clock += dt
 still = still + dt if game.vehicle.linear_velocity.length() < .03 else 0.
 if clock > 120.:
  print("TURNTABLE the car never came to rest");game.request_game_close(1);set_process(false);return
 if clock < 6. or still < 2.:return
 set_process(false)
 shoot()

func shoot():
 var vehicle = game.vehicle
 game.hud.set_level(game.hud.OFF, false)
 game.hud.root.visible = false
 if game.car_wear.material and not Array(OS.get_cmdline_user_args()).any(func(a): return a.begins_with("--car-dirt=")):
  game.car_wear.dirt = 0.;game.car_wear.push()   # a clean car unless asked
 var body: Transform3D = vehicle.global_transform
 # The ground plane in the car's frame: where the four tyres touch the road.
 var space = vehicle.get_world_3d().direct_space_state
 var ground := 0.
 var touching := 0
 for mount in vehicle.MOUNTS:
  var from: Vector3 = body * (mount + Vector3.UP * .5)
  var hit = space.intersect_ray(PhysicsRayQueryParameters3D.create(from, from + body.basis.y * -2., 1, [vehicle.get_rid()]))
  if not hit.is_empty():
   ground += (body.affine_inverse() * hit.position).y;touching += 1
 ground = ground / touching if touching else -.33
 var studio := Node3D.new();studio.name = "Turntable";game.add_child(studio)
 studio.global_transform = body
 var pad := MeshInstance3D.new();pad.layers = STUDIO_LAYER
 var disc := CylinderMesh.new();disc.top_radius = 60.;disc.bottom_radius = 60.;disc.height = .02;disc.radial_segments = 96
 var concrete := StandardMaterial3D.new()
 concrete.albedo_color = Color("#8d8b86");concrete.roughness = .88;concrete.metallic_specular = .35
 var grain := FastNoiseLite.new();grain.frequency = .9
 var tex := NoiseTexture2D.new();tex.noise = grain;tex.seamless = true;tex.width = 512;tex.height = 512
 var ramp := Gradient.new();ramp.set_color(0, Color(.86, .86, .85));ramp.set_color(1, Color(1., 1., 1.));tex.color_ramp = ramp
 concrete.albedo_texture = tex;concrete.uv1_scale = Vector3(36., 36., 36.)
 disc.material = concrete;pad.mesh = disc
 pad.position = Vector3(0., ground - .01, 0.)
 studio.add_child(pad)
 # The studio sun: the game's sun settings, our own direction, lighting and
 # shadowing only the car and the pad.
 var sun: DirectionalLight3D = game.sun.duplicate()
 game.sun.visible = false
 studio.add_child(sun)
 var el := deg_to_rad(sun_elevation);var az := deg_to_rad(sun_azimuth)
 var toward_sun := Vector3(sin(az) * cos(el), sin(el), -cos(az) * cos(el))
 sun.global_basis = Basis.looking_at(-(body.basis * toward_sun), Vector3.UP)
 sun.light_cull_mask = CAR_LAYER | STUDIO_LAYER
 sun.shadow_caster_mask = CAR_LAYER | STUDIO_LAYER
 sun.directional_shadow_max_distance = 40.
 sun.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS
 game.probe.visible = false
 var probe := ReflectionProbe.new();probe.size = Vector3(60., 20., 60.);probe.cull_mask = STUDIO_LAYER
 probe.update_mode = ReflectionProbe.UPDATE_ALWAYS;probe.intensity = .8;probe.position = Vector3(0., 1.2, 0.)
 studio.add_child(probe)
 var cam := Camera3D.new();cam.cull_mask = CAR_LAYER | STUDIO_LAYER;cam.near = .05;cam.far = 2000.
 var env: Environment = game.world_environment.duplicate()
 env.fog_enabled = false;env.volumetric_fog_enabled = false
 cam.environment = env
 game.add_child(cam);cam.current = true
 for v in VIEWS:
  var eye: Vector3 = body * Vector3(v[1].x, v[1].y + ground, v[1].z)
  var target: Vector3 = body * Vector3(v[2].x, v[2].y + ground, v[2].z)
  cam.fov = v[3]
  cam.global_position = eye
  cam.look_at(target, body.basis.y if v[0] != "top" else -body.basis.z)
  await save(v[0])
 if lamps:
  # Dusk: the head and tail lamps on (Drive.set_head_lamps), then the brake lamps too.
  sun.visible = false;probe.visible = false
  var dusk := Environment.new()
  dusk.background_mode = Environment.BG_COLOR;dusk.background_color = Color("#141b26")
  dusk.ambient_light_source = Environment.AMBIENT_SOURCE_COLOR;dusk.ambient_light_color = Color("#2a3446");dusk.ambient_light_energy = .35
  dusk.reflected_light_source = Environment.REFLECTION_SOURCE_BG
  dusk.tonemap_mode = env.tonemap_mode;dusk.tonemap_exposure = env.tonemap_exposure;dusk.glow_enabled = true
  cam.environment = dusk
  game.set_head_lamps(true)
  for v in [["lamps_front", Vector3(-2.6, .9, -7.5), Vector3(0., .5, -1.)], ["lamps_rear", Vector3(2.6, 1.1, 7.5), Vector3(0., .6, 1.)], ["lamps_brake", Vector3(2.6, 1.1, 7.5), Vector3(0., .6, 1.)]]:
   if v[0] == "lamps_brake":
    for m in game.lamp_materials:m.emission_energy_multiplier = game.BRAKE_GLOW
   cam.fov = 22.
   cam.global_position = body * (v[1] + Vector3.UP * ground)
   cam.look_at(body * (v[2] + Vector3.UP * ground), body.basis.y)
   await save(v[0])
  # The same at dusk on the bundled street, low beams on the road.
  for m in game.lamp_materials:m.emission_energy_multiplier = game.TAIL_GLOW
  pad.visible = false;cam.cull_mask = 0xFFFFF
  cam.global_position = body * Vector3(-2.6, ground + .9, -7.5)
  cam.look_at(body * Vector3(0., ground + .5, -1.), body.basis.y)
  await save("lamps_street")
  game.set_head_lamps(false)
 # The in-game driver view, the way the player sees the cabin.
 cam.current = false;game.camera.current = true
 game.view = 1;game.camera_snap = true
 for i in 30:await RenderingServer.frame_post_draw
 await save("cockpit")
 var drift: float = vehicle.global_position.distance_to(body.origin)
 print("TURNTABLE done %s ground %.3f m below the body origin, %d contacts, moved %.3f m while shooting" % [vehicle.spec.id, -ground, vehicle.wheel_contact, drift])
 Input.action_release("parking_brake")
 game.request_game_close(0)

func save(name: String) -> void:
 for i in 24:await RenderingServer.frame_post_draw
 var image := get_viewport().get_texture().get_image()
 image.save_jpg(out.path_join("%s_%s.jpg" % [game.vehicle.spec.id, name]), .92)
 print("TURNTABLE %s" % name)

extends Node
## A car that has been driven: road grime that builds with the miles you drive
## and washes off in real rain (RoadGrime.gdshader over every exterior mesh of
## whichever car is mounted, as material_overlay).
##
## Accumulation per km, by the surface under the tyres (WorldTile.wheel_grip):
## paved 0.002 (about 500 km to fully grimy), gravel or mapped unpaved 0.05,
## verge or field 0.12. While the live weather station reports precipitation
## (helper/conditions.mjs "wet": RA, DZ, SN, SH, TS …) the paint is wet and the
## grime washes off over about 40 minutes; it dries over about ten minutes
## after the rain stops. The level is remembered (preference "car_dirt") and a
## new player starts lightly used, 0.15. Scripted captures and checks leave the
## car clean so photo comparisons stay repeatable.

const PAVED_PER_KM := .002
const LOOSE_PER_KM := .05
const FIELD_PER_KM := .12
const WASH_PER_S := 1. / 2400.
const DRY_PER_S := 1. / 600.

var game
var material: ShaderMaterial
var dirt := .15
var wet := 0.
var saved := .15
var save_clock := 0.

func setup(owner_game) -> void:
 game = owner_game
 if not game.graphics.managed:return
 dirt = float(game.driving_preferences.values.get("car_dirt", .15));saved = dirt
 for a in OS.get_cmdline_user_args():
  if a.begins_with("--car-dirt="):dirt = clampf(float(a.trim_prefix("--car-dirt=")), 0., 1.);saved = dirt   # A/B captures
 material = ShaderMaterial.new()
 material.shader = preload("res://RoadGrime.gdshader")
 var vehicle = game.vehicle
 var hubs := []
 for i in 4:
  var hub: Vector3 = vehicle.MOUNTS[i] - Vector3.UP * vehicle.spec.static_length_m
  hubs.append(Vector4(hub.x, hub.y, hub.z, vehicle.R))
 for k in 4:material.set_shader_parameter(["wheel_fl","wheel_fr","wheel_rl","wheel_rr"][k], hubs[k])
 for mi in vehicle.find_children("*", "MeshInstance3D", true, false):
  if is_interior(mi):continue
  mi.material_overlay = material
 push()

func is_interior(node: Node) -> bool:
 var n := node
 while n != null and n != game.vehicle:
  if n == game.cockpit:return true
  n = n.get_parent()
 return false

func push() -> void:
 if material == null:return
 material.set_shader_parameter("dirt", dirt)
 material.set_shader_parameter("wet", wet)

func _process(dt: float) -> void:
 if material == null or not game.ready_drive:return
 material.set_shader_parameter("car_inverse", Projection(game.vehicle.get_global_transform_interpolated().affine_inverse()))
 var speed: float = absf(game.velocity)
 var grip := 0.
 for g in game.vehicle.surface_grip:grip += float(g) * .25
 var per_km := PAVED_PER_KM if grip >= .9 else (LOOSE_PER_KM if grip >= .72 else FIELD_PER_KM)
 dirt = minf(1., dirt + speed * dt / 1000. * per_km)
 var raining: bool = game.live_weather and bool(game.observation.get("wet", false))
 if raining:
  wet = minf(1., wet + dt / 30.)
  dirt = maxf(0., dirt - dt * WASH_PER_S)
 else:
  wet = maxf(0., wet - dt * DRY_PER_S)
 push()
 save_clock += dt
 if save_clock > 30. and absf(dirt - saved) > .005:
  save_clock = 0.;saved = dirt
  game.driving_preferences.remember("car_dirt", snappedf(dirt, .001))

func label() -> String:
 return "Car %s · grime %d %%" % ["wet" if wet > .2 else "dry", roundi(dirt * 100.)]

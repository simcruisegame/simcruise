extends RefCounted
## A car as data. Every number Powertrain.gd and CarSuspension.gd use lives
## here, so a new car is a JSON file in res://cars/ instead of new code.
## The defaults are the 1999 NB Sport exactly as Powertrain.gd and
## NB1999Sport.gd defined it, so the Miata's calibration (checks/Track.gd) is
## unchanged; res://cars/nb1999.json documents the same values with sources.
##
## Units are SI unless a field name says otherwise. A JSON file overrides any
## field by name; unknown fields are reported once and ignored.

const CARS_DIR := "res://cars"

# Identity
var id := "nb1999"
var name := "1999 Roadster Sport"
var make := ""
var body := "roadster"
var model_scene := ""          # res:// path of the exterior; "" uses the Miata finish
var seats := 2

# Mass and body
var mass_kg := 1043.
var front_weight := .515
var cg_height_m := .435
var inertia := Vector3(1130,1260,370)
var collision_box := Vector3(1.56,.49,3.65)
var collision_box_y := .46
var angular_damp := .02

# Geometry
var wheelbase_m := 2.266
var track_front_m := 1.414
var track_rear_m := 1.44
var mount_height_m := .51
var hub_drop_m := .205         # wheel node below the mount, visual only

# Tyres (size gives the free radius)
var tire_width_m := .195
var tire_aspect_ratio := .50
var rim_in := 15.
var static_mu := .963
var lateral_grip_scale := 1.105
var load_sensitivity := .095   # grip lost per unit of extra load over the static corner load
var reference_corner_load_n := 2557.  # the static corner load that sensitivity is measured from (≈ m·g/4)
# Grip of each axle's tyres relative to static_mu, for staggered sizes or compounds.
var axle_grip := [1.,1.]
var mu_min := .7
var mu_max := 1.16
var slip_stiffness := 10.8     # tanh(slip angle × this)
var slip_falloff := .13        # grip lost past the peak
var slip_falloff_from := .18
var slip_falloff_to := .65
var camber_thrust := .48
var longitudinal_stiffness := 12.
var relaxation_m := .35
var wheel_inertia := 1.05
var rolling_resistance := .012

# Steering
var parking_rack_deg := 36.6
var road_reference_deg := 35.5
var ackermann_blend := .49
var assist_lateral_g := .95
var limit_slip_surplus_deg := 5.

# Brakes (torque per wheel at full pedal)
var brake_front_nm := 1150.
var brake_rear_nm := 650.
var parking_brake_nm := 900.

# Aerodynamics
var drag_area_m2 := .65        # Cd × frontal area

# Engine (combustion). Torque curve: [rpm, N·m] pairs.
var engine := "combustion"     # or "electric"
var torque_curve := [Vector2(800,94),Vector2(1750,127),Vector2(3000,146),Vector2(4150,151),Vector2(5000,161),Vector2(5900,156),Vector2(6500,153),Vector2(7000,137)]
var torque_past_curve_nm := 120.
var idle_rpm := 850.
var idle_omega := 89.          # rad/s on restarts
var governor_rpm := 875.
var fuel_cut_rpm := 7040.
var max_gear_rpm := 7200.      # manual shifts refused above this
var engine_inertia := .18
var engine_friction_nm := 10.
var engine_friction_per_rpm := .0020
var min_omega_auto := 78.
var max_omega := 750.
var stall_omega := 35.

# Gearbox and clutch
var gears := [3.136,1.888,1.330,1.0,.814]
var reverse_ratio := 3.758
var final_drive := 4.30
var driveline_efficiency := .895
var shift_seconds := .21
var upshift_rpm := 6700.
var upshift_min_omega := 590.  # engine rad/s at the wheels before an automatic upshift
var downshift_rpm := 2100.
var clutch_stiffness := 12.
var clutch_capacity_nm := 210.
var clutch_back_nm := 200.
var launch_rpm := 1000.
var launch_rpm_per_throttle := 2250.
var manual_clutch_stiffness := 10.
var transmission := "manual"   # manual (player may pick automatic), automatic, dct, single-speed

# Traction control: off on the 1999 car, standard on the modern ones.
var traction_control := false
var tc_target_slip := .12
var tc_gain := 4.

# Drivetrain
var layout := "rwd"            # rwd, fwd, awd
var awd_front_share := .5
var front_diff_bias := 1.
var rear_diff_bias := 2.        # Torsen-style bias ratio (1 = open)
var center_bias := 1.

# Electric (used when engine == "electric")
var motor_peak_torque_nm := 0.
var motor_peak_power_w := 0.
var motor_max_rpm := 0.
var regen_torque_nm := 0.      # at the motor when coasting

# Suspension, per axle [front, rear]
var coil_lb_in := [168.,130.]          # with lever_ratio gives the wheel rate (NB Sport row)
var lever_ratio := [.68,.77]
var wheel_rate_n_m := [0.,0.]          # used instead when > 0
var bar_diameter_m := [.022,.011]
var bar_torsion_length_m := [.822,.813]
var bar_arm_m := [.216,.122]
var bar_lever_ratio := [.548,.586]
var bar_rate_n_m := [0.,0.]            # used instead when > 0
var static_length_m := .217
var min_travel_m := .074
var max_travel_m := .315
var bump_gap_m := .054
var bump_stiffness := 800000.
var max_contact_load_n := 14200.
var static_camber_deg := [-.53,-1.10]
var camber_per_m := [23.,25.]
var caster_deg := 6.
var toe_per_wheel_deg := .15
var unsprung_corner_kg := 23.
# Dampers: "dyno" interpolates a force table (shaft in/s, compression lbf,
# rebound lbf) through the lever ratio; "ratio" uses a fraction of critical
# damping with a knee. NB Sport: front ratio estimate, rear Bilstein Hard S trace.
var damper := ["ratio","dyno"]
var damper_ratio_rebound := [.56,.56]
var damper_ratio_compression := [.33,.33]
var damper_knee_mps := [.12,.12]
var damper_knee_slope := [.48,.48]
var damper_dyno := [[],[Vector3(0.0,0,0),Vector3(1.0,42,73),Vector3(1.1,45,80),Vector3(2.0,56,127),Vector3(3.0,66,146),Vector3(4.0,72,160),Vector3(6.0,87,183),Vector3(8.0,100,206),Vector3(9.98,112,224)]]

# Calibration targets and their sources (checks/CarTrack.gd holds the car to them)
var target_0_60_s := 7.97
var target_70_0_ft := 173.
var target_skidpad_g := .89
var sources := {}
# A known gap, stated: CarTrack reports the car but does not fail on it.
var calibration_open := ""

func tire_radius_m() -> float:
 return (rim_in*.0254*.5)+(tire_width_m*tire_aspect_ratio)

func mounts() -> Array:
 var hw=wheelbase_m*.5
 return [Vector3(-track_front_m*.5,mount_height_m,-hw),Vector3(track_front_m*.5,mount_height_m,-hw),
  Vector3(-track_rear_m*.5,mount_height_m,hw),Vector3(track_rear_m*.5,mount_height_m,hw)]

func driven(i:int) -> bool:
 if layout=="awd":return true
 return i<2 if layout=="fwd" else i>1

static func available() -> Array:
 var out=[]
 for file in DirAccess.get_files_at(CARS_DIR):
  if file.get_extension()=="json":out.append(file.get_basename())
 out.sort()
 return out

static func load_id(car_id:String):
 var spec=new()
 if car_id=="":return spec
 var path=CARS_DIR.path_join(car_id+".json")
 if not FileAccess.file_exists(path):
  push_warning("Car %s not found; using the default car"%car_id)
  return spec
 spec.apply(JSON.parse_string(FileAccess.get_file_as_string(path)))
 return spec

# Copy fields from parsed JSON. Vectors are written as arrays; a torque curve
# or dyno table as arrays of [x, y(, z)] rows.
func apply(data) -> void:
 if not data is Dictionary:push_warning("Car file is not a JSON object");return
 var props={}
 for p in get_property_list():props[p.name]=p.type
 for key in data:
  if key.begins_with("_"):continue
  if key=="sources":
   if data[key] is Dictionary:sources=data[key]
   continue
  if not props.has(key):push_warning("Car %s: unknown field %s"%[id,key]);continue
  var v=data[key]
  match props[key]:
   TYPE_VECTOR2:if v is Array and v.size()==2:set(key,Vector2(v[0],v[1]))
   TYPE_VECTOR3:if v is Array and v.size()==3:set(key,Vector3(v[0],v[1],v[2]))
   TYPE_FLOAT:set(key,float(v))
   TYPE_INT:set(key,int(v))
   TYPE_STRING:set(key,str(v))
   TYPE_ARRAY:set(key,convert_array(key,v))
   _:set(key,v)

func convert_array(key:String,v) -> Array:
 if not v is Array:return []
 if key=="torque_curve":
  return v.map(func(r):return Vector2(r[0],r[1]))
 if key=="damper_dyno":
  return v.map(func(axle):return axle.map(func(r):return Vector3(r[0],r[1],r[2])))
 return v.map(func(x):return float(x) if (x is float or x is int) else x)

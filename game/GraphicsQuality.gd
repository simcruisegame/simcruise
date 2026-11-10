extends Node
## Graphics presets (Low · Medium · High · Ultra), chosen automatically from the
## GPU the renderer actually opened, plus two runtime tricks that spend pixels
## where a driver is looking:
##
##  * Speed-aware detail. At speed the periphery smears (motion, and the eye
##    locks on the road ahead), so mesh LOD switches earlier as speed rises.
##    Cars pass a building at 30 m/s in under a second; nobody counts its sills.
##  * Adaptive resolution. The 3D render scale follows the frame budget: when a
##    dense town drops frames the scale falls in small steps (FSR 1 upscales it
##    back), and climbs again once there is headroom. The HUD is never scaled.
##
## Headless runs and the scripted QA flags keep the project.godot values, so
## every existing capture and calibration check renders exactly as before.
## Preference "quality" (DrivingPreferences.gd): "auto", "low", "medium", "high", "ultra".

const ORDER := ["low","medium","high","ultra"]
# render_scale: 3D resolution before upscaling. fsr: FSR 1 upscale (else bilinear).
# msaa: Viewport.MSAA_*. shadow_atlas: directional shadow map size.
# shadow_m: sun shadow reach. lod: mesh_lod_threshold in pixels (higher = coarser).
# traffic/birds: ambient life counts (AmbientLife.gd). adaptive: follow the frame budget.
const PRESETS := {
 "low":    {"render_scale":.67,"fsr":true, "msaa":Viewport.MSAA_DISABLED,"shadow_atlas":1024,"shadow_m":70., "lod":2.5,"aniso":Viewport.ANISOTROPY_2X, "traffic":6, "birds":24, "adaptive":true, "target_fps":30.},
 "medium": {"render_scale":.8, "fsr":true, "msaa":Viewport.MSAA_2X,     "shadow_atlas":2048,"shadow_m":120.,"lod":1.5,"aniso":Viewport.ANISOTROPY_4X, "traffic":12,"birds":60, "adaptive":true, "target_fps":50.},
 "high":   {"render_scale":.9, "fsr":true, "msaa":Viewport.MSAA_2X,     "shadow_atlas":4096,"shadow_m":180.,"lod":1.0,"aniso":Viewport.ANISOTROPY_8X, "traffic":20,"birds":120,"adaptive":true, "target_fps":58.},
 "ultra":  {"render_scale":1.0,"fsr":false,"msaa":Viewport.MSAA_4X,     "shadow_atlas":4096,"shadow_m":260.,"lod":.75,"aniso":Viewport.ANISOTROPY_16X,"traffic":32,"birds":200,"adaptive":false,"target_fps":58.},
}
const MIN_ADAPTIVE_SCALE := .6
const SPEED_LOD_GAIN := .045        # extra LOD pixels per m/s of speed (x2 at ~22 m/s with lod 1)

var game
var level := "high"
var chosen := "auto"
var managed := true                  # false: QA / headless, leave project settings alone
var adapter := ""
var adapter_type := -1
var scale := 1.0
var frame_ms_avg := 16.7
var settle := 0.0
var measuring := false

static func detect(name: String, kind: int) -> String:
 # RenderingDevice.DeviceType: 1 integrated, 2 discrete, 3 virtual, 4 cpu.
 var n := name.to_lower()
 if kind == 4 or n.contains("llvmpipe") or n.contains("swiftshader") or n.contains("microsoft basic"):return "low"
 # Apple silicon reports itself as integrated but is far faster than a laptop iGPU.
 if n.contains("apple"):
  return "high" if n.contains("max") or n.contains("ultra") or n.contains("pro") else "medium"
 if kind == 1 or n.contains("intel") and not n.contains("arc"):return "low"
 for fast in ["rtx 40","rtx 50","rtx 30","rx 7","rx 9","rtx a","quadro rtx 8"]:
  if n.contains(fast):return "ultra"
 # Laptop "Max-Q" parts run well below their desktop names: one tier down.
 # RTX 20-series and RX 6000 (including laptop Max-Q parts such as the RTX 2070
 # Super Max-Q this was tuned on) hold 60 fps on High with adaptive resolution.
 if n.contains("rtx 20") or n.contains("rx 6") or n.contains("gtx 1080") or n.contains("arc"):return "high"
 return "medium"

func setup(owner_game) -> void:
 game = owner_game
 adapter = RenderingServer.get_video_adapter_name()
 adapter_type = RenderingServer.get_video_adapter_type()
 var args := OS.get_cmdline_user_args()
 managed = DisplayServer.get_name() != "headless"
 for flag in game.BUNDLED_START_FLAGS + ["--screenshots","--car-showcase"]:
  if flag in args and flag != "--bundled-start":managed = false
 chosen = str(game.driving_preferences.values.get("quality","auto"))
 for a in args:
  if a.begins_with("--quality="):
   managed = true;chosen = a.trim_prefix("--quality=")
 apply(chosen)

func resolved(choice: String) -> String:
 return detect(adapter, adapter_type) if choice == "auto" or not PRESETS.has(choice) else choice

func apply(choice: String) -> void:
 chosen = choice
 level = resolved(choice)
 if not managed:return
 var p: Dictionary = PRESETS[level]
 var vp := get_viewport()
 scale = p.render_scale
 # FSR needs the Forward+ renderer; the Mobile renderer scales bilinearly.
 var fsr_ok := RenderingServer.get_current_rendering_method() == "forward_plus"
 vp.scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR if p.fsr and fsr_ok else Viewport.SCALING_3D_MODE_BILINEAR
 vp.fsr_sharpness = .35
 vp.scaling_3d_scale = scale
 vp.msaa_3d = p.msaa
 vp.anisotropic_filtering_level = p.aniso
 vp.mesh_lod_threshold = p.lod
 RenderingServer.directional_shadow_atlas_set_size(p.shadow_atlas, true)
 if game.sun:game.sun.directional_shadow_max_distance = p.shadow_m
 if game.has_node("AmbientLife"):game.get_node("AmbientLife").configure(p.traffic, p.birds)
 cinematic_lighting(level)
 # How far the car moves before its reflection probe re-renders the surroundings.
 game.probe_step_m = {"low":40.,"medium":24.,"high":12.,"ultra":8.}[level]

func label() -> String:
 var name: String = level.capitalize()
 return ("Auto · " + name if chosen == "auto" else name) + (" (%d%% res)" % roundi(scale*100.) if managed else "")

func cycle() -> void:
 var options := ["auto"] + ORDER
 var next: String = options[(options.find(chosen)+1) % options.size()]
 managed = true
 apply(next)
 game.driving_preferences.remember("quality", next)
 game.say("Graphics · " + label(), 3.)

func preset() -> Dictionary:
 return PRESETS[level]

func _process(dt: float) -> void:
 if not managed or game == null or not game.ready_drive:return
 var p: Dictionary = PRESETS[level]
 var vp := get_viewport()
 # Speed-aware detail: coarser LOD as speed rises (the periphery is smeared anyway).
 vp.mesh_lod_threshold = p.lod * (1. + absf(game.velocity) * SPEED_LOD_GAIN)
 if not p.adaptive:return
 # Adaptive resolution: an exponential average of the GPU's own time for the
 # 3D viewport (frame time is pinned at the vsync interval, so it cannot tell
 # how much headroom is left), small steps, a dead band and a settle time so
 # the image never visibly pumps. Drivers that report no GPU time fall back
 # to the frame time and only ever step down.
 var rid := vp.get_viewport_rid()
 if not measuring:
  RenderingServer.viewport_set_measure_render_time(rid, true);measuring = true
 var gpu_ms := RenderingServer.viewport_get_measured_render_time_gpu(rid)
 var load_ms := gpu_ms if gpu_ms > 0. else dt * 1000.
 frame_ms_avg = lerpf(frame_ms_avg, load_ms, .05)
 settle -= dt
 if settle > 0.:return
 var budget: float = 1000. / p.target_fps
 if frame_ms_avg > budget * (.92 if gpu_ms > 0. else 1.08) and scale > MIN_ADAPTIVE_SCALE:
  scale = maxf(MIN_ADAPTIVE_SCALE, scale - .05);settle = .75
 elif gpu_ms > 0. and frame_ms_avg < budget * .62 and scale < p.render_scale:
  scale = minf(p.render_scale, scale + .05);settle = 2.
 else:return
 vp.scaling_3d_scale = scale

## Lighting the Mobile renderer cannot do. When the game runs on Forward+
## (desktop GPUs; see docs/research/real-world-detail.md), High and Ultra add:
##   SSAO  — contact darkening in corners, under cars, where walls meet ground
##   SSIL  — screen-space bounce: a sunlit wall warms the street beside it
##   SDFGI — large-scale indirect light and sky occlusion across the open world
##   volumetric fog — depth haze and light shafts that respond to the sun
##   glow  — a soft bloom on bright sky, glass and chrome
## Low and Medium, and the Mobile renderer, keep the direct-light look.
func cinematic_lighting(preset_name: String) -> void:
 var env: Environment = game.world_environment
 if env == null:return
 var forward := RenderingServer.get_current_rendering_method() == "forward_plus"
 var on := forward and preset_name in ["high", "ultra"]
 env.ssao_enabled = on
 env.ssao_radius = 1.6;env.ssao_intensity = 2.2;env.ssao_power = 1.6;env.ssao_detail = .5
 env.ssil_enabled = on
 env.ssil_radius = 6.;env.ssil_intensity = 1.1
 env.sdfgi_enabled = on and preset_name == "ultra"
 env.sdfgi_use_occlusion = true;env.sdfgi_cascades = 6;env.sdfgi_min_cell_size = .4
 env.sdfgi_y_scale = Environment.SDFGI_Y_SCALE_75_PERCENT
 env.volumetric_fog_enabled = on
 env.volumetric_fog_density = .0006;env.volumetric_fog_anisotropy = .6;env.volumetric_fog_sky_affect = 0.
 env.volumetric_fog_length = 220.;env.volumetric_fog_albedo = Color("#d8dde0")
 env.glow_enabled = on
 env.glow_intensity = .4;env.glow_bloom = .02;env.glow_hdr_threshold = 1.4
 # FSR 2 is a temporal upscaler: sharper than bilinear at the same cost.
 if forward:get_viewport().scaling_3d_mode = Viewport.SCALING_3D_MODE_FSR2 if scale < 1. else Viewport.SCALING_3D_MODE_BILINEAR

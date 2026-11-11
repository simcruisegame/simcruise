extends RefCounted

# Each option has its own tiny atomic record. Two open game windows changing
# different settings cannot overwrite each other's changes.
# A preference is typed by its default: a stored value is accepted only when
# its Variant type matches (ConfigFile keeps bool, int and float apart, so a
# saved 1 and 1.0 read back as different types in Godot 4.7).
#   manual, street_photos, spectral_ground, sound : bool
#   hud_level   : int   0 Off · 1 Glance · 2 Full (Hud.gd)
#   head_motion : float 0..1 scale of the driver head model (Cockpit.gd)
#   rumble      : bool  Xbox controller rumble from the tyres, ABS and road (Haptics.gd)
#   rumble_strength : float 0..1 scale of both rumble motors
# SIMCRUISE_QA_PREFS_DIR points a headless check at an isolated directory, so no
# check ever reads or writes the player's own records.
#   car         : String id of a file in res://cars (CarSpec.gd)
#   resume_place: String "lon,lat,title" to reopen after switching cars, used once
#   lens        : String "natural", "screen" or "wide" (ViewLens.gd)
#   car_dirt    : float 0..1 road grime on the car (CarWear.gd)
#   view_cm     : float  eye-to-screen distance for the "screen" lens
#   quality     : String "auto", "low", "medium", "high" or "ultra" (GraphicsQuality.gd)
const DEFAULTS := {"manual":false,"street_photos":true,"spectral_ground":false,"sound":true,"hud_level":1,"head_motion":1.0,"car":"nb1999","resume_place":"","quality":"auto","lens":"natural","view_cm":60.0,"car_dirt":0.15,"rumble":true,"rumble_strength":1.0}
const QA_FLAGS := ["--car-audit","--bethel-facade-ab","--survey-stops","--seam-check","--pad-steering","--building-check","--street-check","--reference","--us-check","--field-check","--profile-trees","--road-test","--car-turntable"]
var values := DEFAULTS.duplicate()
var directory := ""

func open() -> void:
 var args := OS.get_cmdline_user_args()
 var isolated := OS.get_environment("SIMCRUISE_QA_PREFS_DIR")
 if "--preference-check" in args or not isolated.is_empty():
  directory = isolated
 else:
  for flag in QA_FLAGS:
   if flag in args:
    return
  directory = ProjectSettings.globalize_path("user://driving-preferences")
 if directory.is_empty():return
 for name in DEFAULTS:
  var filename := directory.path_join(str(name)+".cfg")
  if not FileAccess.file_exists(filename):continue
  var record := ConfigFile.new()
  if record.load(filename) == OK:
   # Boolean records keep their original "enabled" key; typed records use "value".
   var value = record.get_value("driving","value",record.get_value("driving","enabled",DEFAULTS[name]))
   if typeof(value) == typeof(DEFAULTS[name]):values[name]=coerce(name,value)

# Keep every remembered value inside the range the game can use.
static func coerce(name: String, value):
 match name:
  "hud_level":return clampi(value,0,2)
  "head_motion":return clampf(value,0.,1.)
  "view_cm":return clampf(value,20.,300.)
  "car_dirt":return clampf(value,0.,1.)
  "rumble_strength":return clampf(value,0.,1.)
 return value

func remember(name: String, value) -> void:
 if not DEFAULTS.has(name) or typeof(value) != typeof(DEFAULTS[name]):
  push_warning("Ignored a mistyped driving preference: "+name)
  return
 value = coerce(name,value)
 if values[name] == value:return
 if directory.is_empty():
  values[name] = value
  return
 if DirAccess.make_dir_recursive_absolute(directory) != OK:
  push_warning("Could not create the driving preference directory")
  return
 var filename := directory.path_join(name+".cfg")
 var temporary := filename+".%d.tmp"%OS.get_process_id()
 var record := ConfigFile.new()
 record.set_value("driving","enabled" if value is bool else "value",value)
 if record.save(temporary) != OK or DirAccess.rename_absolute(temporary,filename) != OK:
  if FileAccess.file_exists(temporary):DirAccess.remove_absolute(temporary)
  push_warning("Could not save the "+name+" driving preference")
  return
 values[name] = value

extends RefCounted
# Input provenance, UTC conversions and control photos are in lab/daylight.
# Distances blend the observation into the ordinary playable afternoon.
const FULL_M := 145.0
const END_M := 350.0
const NONE := 0
const CLEAR := 1
const CIRRUS := 2
const BROKEN := 3
const ARCHIVE := [
 {"location":Vector2(37.101444,-109.990814),"epoch":1682864207,"kind":CLEAR,"cloud":0.0,"veil":0.0,"extent":0.0,"label":"Apr 30 2023 · clear"},
 {"location":Vector2(38.139858,-109.371372),"epoch":1521569129,"kind":CIRRUS,"cloud":0.320,"veil":0.276,"extent":0.0605,"label":"Mar 20 2018 · high cloud"},
 {"location":Vector2(43.753821,-110.624090),"epoch":1748627255,"kind":CLEAR,"cloud":0.0,"veil":0.0,"extent":0.0,"label":"May 30 2025 · regionally clear"},
 {"location":Vector2(37.800861,-80.444100),"epoch":1468018226,"kind":BROKEN,"cloud":0.78,"veil":0.0,"extent":0.0,"label":"Jul 8 2016 · broken cloud"}
]
var reference_label := ""
var sky_kind := NONE
var sky_weight := 0.0
var solar_epoch := 0
var solar_weight := 0.0
var veil := 0.0
var extent := 0.0
var cloud := 0.12

func select(location:Vector2, winter:bool, live:bool, observation:Dictionary, frame_epoch:int, frame_weight:float) -> void:
 cloud=0.90 if winter else 0.12
 solar_epoch=frame_epoch
 solar_weight=clampf(frame_weight,0.,1.) if frame_epoch>0 else 0.
 sky_kind=NONE
 sky_weight=0.
 veil=0.
 extent=0.
 reference_label=""
 if live:
  solar_epoch=0
  solar_weight=0.
  cloud=float(observation.get("cloud",cloud))
  if observation.get("clear_sky",false):
   sky_kind=CLEAR
   sky_weight=1.
  return
 for sample in ARCHIVE:
  var delta:Vector2=location-sample.location
  if absf(delta.x)>.0033 or absf(delta.y)>.005:continue
  # A nearby independent camera can supply a different date. Keep its actual
  # sun and discard any cloud evidence collected for the other photograph.
  if frame_epoch>0 and absi(frame_epoch-int(sample.epoch))>3600:continue
  var metres:=(delta*Vector2(111195.,111195.*cos(deg_to_rad(location.x)))).length()
  if metres>=END_M:continue
  sky_weight=1.-smoothstep(FULL_M,END_M,metres)
  solar_epoch=frame_epoch if frame_epoch>0 else int(sample.epoch)
  solar_weight=maxf(solar_weight,sky_weight)
  sky_kind=int(sample.kind)
  veil=float(sample.veil)
  extent=float(sample.extent)
  cloud=lerpf(cloud,float(sample.cloud),sky_weight)
  if sky_weight>=.5:reference_label=str(sample.label)
  return

func apply_station(record:Dictionary,weight:float) -> void:
 if record.get("status")!="available":return
 var clear=record.get("kind")=="scattering"
 sky_weight=clampf(float(record.confidence)*weight,0.,1.)
 sky_kind=CIRRUS if clear else BROKEN
 veil=float(record.get("veil",0.))
 cloud=lerpf(cloud,float(record.cloud),sky_weight)
 if sky_weight>=.35:
  var label="GOES clear" if clear else "GOES cloud cover"
  reference_label="%s · %.0f km · %s"%[record.station,record.km,label]

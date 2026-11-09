extends SceneTree
# godot --headless --path game --script res://checks/DaylightWeatherCheck.gd
const POLICY=preload("res://DaylightWeather.gd")
func _initialize():
 var policy=POLICY.new()
 var ok=true
 for sample in POLICY.ARCHIVE:
  policy.select(sample.location,false,false,{},0,0.)
  ok=ok and policy.solar_epoch==sample.epoch and policy.sky_kind==sample.kind and policy.sky_weight==1.
  var east=Vector2(0.,1.)/(111195.*cos(deg_to_rad(sample.location.x)))
  policy.select(sample.location+east*250.,false,false,{},0,0.)
  ok=ok and policy.sky_weight>.40 and policy.sky_weight<.60 and policy.solar_weight==policy.sky_weight
  policy.select(sample.location+east*375.,false,false,{},0,0.)
  ok=ok and policy.sky_weight==0. and policy.solar_epoch==0
  policy.select(sample.location,false,false,{},int(sample.epoch)+86400,1.)
  ok=ok and policy.sky_weight==0. and policy.solar_epoch==int(sample.epoch)+86400
  policy.select(sample.location,false,true,{"cloud":1.,"clear_sky":false},sample.epoch,1.)
  ok=ok and policy.sky_kind==POLICY.NONE and policy.solar_epoch==0 and policy.cloud==1.
 for loc in [Vector2(38.8899,-77.0091),Vector2(42.8,-74.15),Vector2(44.02,-103.14)]:
  policy.select(loc,true,true,{"cloud":0.,"clear_sky":true},0,0.)
  ok=ok and policy.sky_kind==POLICY.CLEAR and policy.solar_epoch==0 and policy.cloud==0.
  policy.select(loc,true,true,{},0,0.)
  ok=ok and policy.sky_kind==POLICY.NONE and policy.cloud==.90
  policy.select(loc,false,false,{},0,0.)
  ok=ok and policy.sky_kind==POLICY.NONE and policy.solar_epoch==0 and policy.cloud==.12
 print("DAYLIGHT CONDITIONS ","PASS" if ok else "FAIL")
 quit(0 if ok else 1)

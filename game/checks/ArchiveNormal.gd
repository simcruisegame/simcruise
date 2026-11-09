extends Node
# Ordinary place selection, controller/cockpit, nearest licensed photo and live guard.
# Never supplies a reference ID, test epoch, camera, weather override or tile coordinate.
var game
var started=false
var ticks=0
func _ready():
 game=get_parent()
func _process(_dt):
 ticks+=1
 if ticks>4800:print("NORMAL failed to resolve");set_process(false);game.request_game_close(3);return
 if not game.ready_drive:return
 if not started:
  game.choose_place(-96.675003,40.808014,"Historic daylight road check");started=true;return
 if not game.jump.is_empty() or game.tiles.size()<4:return
 var frame:Dictionary=game.street_detail.nearby_reference(game).get("frame",{})
 if frame.is_empty() or game.archive_weather.last.get("status")!="available" or game.daylight.weather.reference_label.is_empty():return
 var weather=game.archive_weather.last
 print("ARCHIVE NORMAL ",JSON.stringify({"place":game.player_coordinates(),"frame_id":frame.id,"captureUnix":frame.captureUnix,"appliedEpoch":game.daylight.applied_epoch,"station":weather.station,"satellite":weather.satellite,"label":game.weather_label(),"reference_filter":game.street_detail.qa_reference_id,"camera_override":game.street_detail.qa_camera_tile!=null,"wheel_contacts":game.vehicle.wheel_contact}))
 # Explicitly exercise user switches without invoking a live network request.
 game.live_weather=true
 game.apply_daylight(game.player_coordinates(),"",int(frame.captureUnix),1.)
 var correct=game.daylight.weather.sky_kind==0 and game.daylight.applied_epoch==0
 game.live_weather=false
 game.apply_daylight(game.player_coordinates(),"",0,0.)
 correct=correct and game.daylight.weather.sky_kind==0 and game.daylight.weather.reference_label.is_empty()
 print("ARCHIVE fallback live/undated ",correct)
 set_process(false);game.request_game_close(0 if correct else 4)

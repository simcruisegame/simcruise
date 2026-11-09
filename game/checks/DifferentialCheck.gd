extends SceneTree
const VehicleSpec = preload("res://NB1999Sport.gd")

func _initialize():
 var cases=0
 for input in [-600.,0.,600.]:
  for left in [-50.,0.,50.]:
   for right in [-50.,0.,50.]:
    for driving in [false,true]:
     var split=VehicleSpec.differential_torque(input,left,right,1.05,1./360.,driving)
     var exchanged=split.x-input*.5
     var signed_work=exchanged*(left-right)
     if absf(split.x+split.y-input)>.001 or signed_work>.001 or (not driving and split.x!=split.y):
      push_error("Axle torque or passive friction check failed");quit(1);return
     if absf(split.x)>VehicleSpec.DRIVE_BIAS_RATIO*absf(split.y)+.001 or absf(split.y)>VehicleSpec.DRIVE_BIAS_RATIO*absf(split.x)+.001:
      push_error("Exceeded the specified development bias ratio");quit(1);return
     cases+=1
 var left_slip=VehicleSpec.differential_torque(600.,48.,40.,1.05,1./360.,true)
 print("DIFFERENTIAL PASSED ",cases," torque and energy cases; 600 Nm, faster left: ",left_slip)
 quit()

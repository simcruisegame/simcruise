extends SceneTree
func _initialize():
 var s=load("res://CarSpec.gd").load_id("nb1999_turbo")
 print("SPEC ",s.id," ",s.name," ",s.finish_wheel," ",s.boost_torque_curve.size())
 quit()

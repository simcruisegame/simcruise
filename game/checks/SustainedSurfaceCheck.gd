extends SceneTree

const TILE = preload("res://WorldTile.gd")


func _initialize():
	var tile = TILE.new()
	tile.side = 400.
	tile.info = {}
	tile.segments = [
		[Vector2(0, 50), Vector2(100, 50), "mapped gravel", 8., 0, "minor", "unpaved"],
		[Vector2(20, 50), Vector2(20, 90), "farm access", 4., 0, "service"],
		[Vector2(50, 10), Vector2(50, 90), "paved crossroad", 6., 0, "minor", "paved"]
	]
	tile.has_mapped_unpaved = true
	tile.road_index()
	var ok = is_equal_approx(tile.surface_grip(Vector2(11, 50)), 1.)
	ok = ok and is_equal_approx(tile.wheel_grip(Vector2(11, 50)), .76)
	ok = ok and is_equal_approx(tile.wheel_grip(Vector2(20, 51)), .76)
	ok = ok and is_equal_approx(tile.wheel_grip(Vector2(50, 52)), 1.)
	ok = ok and is_equal_approx(tile.wheel_grip(Vector2(50, 50)), 1.)
	ok = ok and is_equal_approx(tile.wheel_grip(Vector2(20, 80)), 1.)
	ok = ok and is_equal_approx(tile.wheel_grip(Vector2(150, 30)), .65)
	tile.has_mapped_unpaved = false
	ok = ok and is_equal_approx(tile.wheel_grip(Vector2(11, 50)), 1.)
	tile.free()
	if ok:
		print(
			"SURFACE CHECK PASSED: confirmed loose road, mapped paved crossing, unknown access, off-road fallback and placement exclusion"
		)
	else:
		push_error("Mapped surface precedence failed")
	quit(0 if ok else 1)

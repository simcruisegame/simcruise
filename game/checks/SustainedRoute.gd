extends Node
# Only tuning inputs and observations live here. The player and normal streamer own every movement.
const STREET = "Princeton Road"
const START = Vector2(-96.5909, 40.56724)
const END_METRES = 1440.
const TARGET_MPS = 13.
var game
var path = ""
var trace: FileAccess
var started = 0
var origin_time = 0
var distance = 0.
var active = false
var done = false
var frames = []
var last_frame = 0
var last_id = Vector2i.ZERO
var last_pos = Vector2.ZERO
var last_velocity = Vector3.ZERO
var last_height = 0.
var ticks = 0
var seconds = 0.
var waiting_ticks = 0
var max_freeze = 0
var current_freeze = 0
var seen = {}
var seam_rows = []
var min_contacts = 4
var contact_ticks = [0, 0, 0, 0, 0]
var max_cross_track = 0.
var max_step = 0.
var max_vy = 0.
var max_pitch = 0.
var max_roll = 0.
var min_grip = 1.
var speeds = []
var highest_grip = 0.
var grip_counts = {}
var brake_test = {}
var last_speed = 0.
var sample_file = ""
var snapshots = []


func _ready():
	game = get_parent()
	call_deferred("configure")


func configure():
	path = OS.get_environment("SUSTAINED_OUTPUT")
	if path.is_empty():
		push_error("Run this scene with lab/sustained/run_route.py")
		game.request_game_close(2)
		return
	trace = FileAccess.open(path + "/trace.csv", FileAccess.WRITE)
	(
		trace
		. store_line(
			"tick,metres,lat,lon,tile_x,tile_y,speed_mps,y_m,vy_mps,contacts,grip_min,cross_track_m,freeze"
		)
	)
	# Match player options for an offline sky and no street photographs. Regular map, aerial, canopy and buildings still stream.
	game.street_detail.enabled = false
	game.recent_user_choice = true
	game.recent_mode = false
	game.live_weather = false
	game.vehicle.set_manual(false)
	game.choose_place(START.x, START.y, "Princeton Road route QA", STREET, 90.)
	game.view = 1
	started = Time.get_ticks_msec()
	sample_file = OS.get_environment("SUSTAINED_SCREEN")


func _process(_dt):
	if active and not done:
		var now = Time.get_ticks_usec()
		if last_frame:
			frames.append((now - last_frame) * .001)
		last_frame = now
		if (
			sample_file != ""
			and snapshots.size() < 3
			and distance > float([100., 770., 1350.][snapshots.size()])
		):
			var d = roundi(distance)
			var name = "drive-%04dm.jpg" % d
			var img = get_viewport().get_texture().get_image()
			img.save_jpg(path + "/" + name, .9)
			record_surface_pixels(name, img)
			snapshots.append(name)


# Register a sparse central road patch back into the public aerial image.
# This checks agreement with an overhead observation under different lighting.
func record_surface_pixels(filename: String, saved: Image):
	var visible = game.camera.get_viewport().get_visible_rect().size
	var pixels = []
	var state = game.get_world_3d().direct_space_state
	for y in range(275, 318, 5):
		for x in range(320, 651, 10):
			var at = Vector2(x / 960., y / 600.) * visible
			var start = game.camera.project_ray_origin(at)
			var axis = game.camera.project_ray_normal(at)
			var query = PhysicsRayQueryParameters3D.create(
				start, start + axis * 130., 1, [game.vehicle.get_rid()]
			)
			var hit = state.intersect_ray(query)
			if hit.is_empty():
				continue
			var loc = Vector2(hit.position.x, hit.position.z)
			var id = game.tile_id(loc)
			if not game.tiles.has(id):
				continue
			var tile = game.tiles[id]
			var uv = (loc - game.tile_offset(id)) / game.side
			var pick = tile.nearest(uv * game.side, false, STREET)
			if (
				pick.is_empty()
				or pick[2] > 1.65
				or absf(hit.position.y - tile.height_at(uv * game.side)) > .16
			):
				continue
			var color = saved.get_pixel(
				roundi(x * saved.get_width() / 960.), roundi(y * saved.get_height() / 600.)
			)
			pixels.append([x, y, id.x, id.y, uv.x, uv.y, color.r8, color.g8, color.b8])
	var output = FileAccess.open(path + "/" + filename + ".rays.json", FileAccess.WRITE)
	output.store_string(JSON.stringify(pixels))


func quantile(values: Array, f: float):
	if values.is_empty():
		return null
	return values[clampi(roundi(f * (values.size() - 1)), 0, values.size() - 1)]


func finish(ok: bool, reason: String):
	if done:
		return
	done = true
	for action in ["left", "right", "gas", "brake", "handbrake"]:
		Input.action_release(action)
	trace.flush()
	frames.sort()
	speeds.sort()
	var visits = []
	for id in seen:
		visits.append([id.x, id.y])
	var result = {
		"pass": ok,
		"reason": reason,
		"route": STREET,
		"start_lon_lat": [START.x, START.y],
		"end_lon_lat": [game.player_coordinates().y, game.player_coordinates().x],
		"distance_m": distance,
		"game_distance_m": game.distance,
		"ticks": ticks,
		"simulated_s": seconds,
		"drive_wall_s": (Time.get_ticks_msec() - origin_time) * .001,
		"startup_s": (origin_time - started) * .001,
		"brake_test": brake_test,
		"seams": seam_rows,
		"visited_tiles": visits,
		"max_cross_track_m": max_cross_track,
		"max_single_step_m": max_step,
		"wheel_contact_histogram": contact_ticks,
		"wheel_grip_samples": grip_counts,
		"min_contacts": min_contacts,
		"road_grip_range": [min_grip, highest_grip],
		"max_abs_vy_mps": max_vy,
		"max_pitch_deg": max_pitch,
		"max_roll_deg": max_roll,
		"wait_ticks": waiting_ticks,
		"max_wait_ticks": max_freeze,
		"process_frame_ms":
		[quantile(frames, .50), quantile(frames, .95), quantile(frames, .99), quantile(frames, 1.)],
		"sampled_frames": frames.size(),
		"rolling_speed_mps":
		[quantile(speeds, .05), quantile(speeds, .50), quantile(speeds, .95), quantile(speeds, 1.)],
		"game_counted_source_requests": game.calls,
		"screens": snapshots,
		"physics_hz": Engine.physics_ticks_per_second,
		"display": DisplayServer.get_name()
	}
	var file = FileAccess.open(path + "/result.json", FileAccess.WRITE)
	file.store_string(JSON.stringify(result, "  ") + "\n")
	file.close()
	print("SUSTAINED ", JSON.stringify(result))
	game.request_game_close(0 if ok else 1)


func _physics_process(dt):
	if not started or done:
		return
	if Time.get_ticks_msec() - started > 200000:
		finish(false, "wall timeout")
		return
	if (
		not game.ready_drive
		or not game.jump.is_empty()
		or game.current_title != "Princeton Road route QA"
	):
		return
	var car = game.vehicle
	var here = Vector2(car.position.x, car.position.z)
	var id = game.tile_id(here)
	if not active:
		if car.wheel_contact != 4:
			return
		last_pos = here
		last_id = id
		last_height = car.position.y
		origin_time = Time.get_ticks_msec()
		active = true
		seen[id] = true
		print("SUSTAINED start ", id, " side=", game.side)
	var step_m = here.distance_to(last_pos)
	distance += step_m
	max_step = maxf(max_step, step_m)
	var speed = Vector2(car.linear_velocity.x, car.linear_velocity.z).length()
	ticks += 1
	seconds += dt
	if car.freeze:
		waiting_ticks += 1
		current_freeze += 1
		max_freeze = maxi(max_freeze, current_freeze)
	else:
		current_freeze = 0
	var error = 0.
	var probe = here + Vector2.RIGHT * maxf(5, minf(14., speed * .8))
	var destination = probe
	var best = INF
	var localbest = INF
	for tile_id in game.tiles:
		if abs(tile_id.x - id.x) > 1 or abs(tile_id.y - id.y) > 1:
			continue
		var offset = game.tile_offset(tile_id)
		for segment in game.tiles[tile_id].segments:
			if segment[2] != STREET:
				continue
			var a = segment[0] + offset
			var b = segment[1] + offset
			var tangent = (b - a).normalized()
			if tangent.x < 0:
				tangent = -tangent
			var lane = Vector2(-tangent.y, tangent.x) * minf(1.8, segment[3] * .18)
			var ahead = Geometry2D.get_closest_point_to_segment(probe, a, b)
			var delta = ahead.distance_to(probe)
			if delta < best:
				best = delta
				destination = ahead + lane
			var middle = Geometry2D.get_closest_point_to_segment(here, a + lane, b + lane)
			localbest = minf(localbest, here.distance_to(middle))
	if best == INF:
		finish(false, "mapped route lost")
		return
	error = localbest
	max_cross_track = maxf(max_cross_track, error)
	var aim = destination - here
	var correction = angle_difference(game.yaw, atan2(-aim.x, -aim.y))
	Input.action_release("left")
	Input.action_release("right")
	if absf(correction) > .002:
		Input.action_press("left" if correction > 0 else "right", minf(.7, absf(correction) * 1.8))
	Input.action_release("gas")
	Input.action_release("handbrake")
	if speed < TARGET_MPS + .45:
		Input.action_press("gas", clampf(.105 + (TARGET_MPS - speed) * .32, 0., .66))
	if speed > TARGET_MPS + 1.:
		Input.action_press("handbrake", clampf((speed - TARGET_MPS - 1.) * .12, 0., .30))
	var gmin = float(car.surface_grip.min())
	var gmax = float(car.surface_grip.max())
	if speed > 3. and not car.freeze:
		min_contacts = mini(min_contacts, car.wheel_contact)
		contact_ticks[car.wheel_contact] += 1
		min_grip = minf(min_grip, gmin)
		highest_grip = maxf(highest_grip, gmax)
		for grip in car.surface_grip:
			var key = "%.2f" % float(grip)
			grip_counts[key] = int(grip_counts.get(key, 0)) + 1
		max_vy = maxf(max_vy, absf(car.linear_velocity.y))
		max_pitch = maxf(max_pitch, absf(rad_to_deg(car.rotation.x)))
		max_roll = maxf(max_roll, absf(rad_to_deg(car.rotation.z)))
		if distance > 70.:
			speeds.append(speed)
	if last_id != id:
		var seam = {
			"from": [last_id.x, last_id.y],
			"to": [id.x, id.y],
			"m": distance,
			"speed": speed,
			"contacts": car.wheel_contact,
			"step_y_m": car.position.y - last_height,
			"step_velocity_mps": car.linear_velocity.distance_to(last_velocity),
			"tick": ticks
		}
		seam_rows.append(seam)
		seen[id] = true
		print("SUSTAINED seam ", seam)
	var ll = game.player_coordinates()
	if ticks % 6 == 0 or last_id != id:
		trace.store_line(
			(
				"%d,%f,%f,%f,%d,%d,%f,%f,%f,%d,%f,%f,%d"
				% [
					ticks,
					distance,
					ll.x,
					ll.y,
					id.x,
					id.y,
					speed,
					car.position.y,
					car.linear_velocity.y,
					car.wheel_contact,
					gmin,
					error,
					int(car.freeze)
				]
			)
		)
	last_pos = here
	last_id = id
	last_velocity = car.linear_velocity
	last_height = car.position.y
	if ticks % 600 == 0:
		print(
			"SUSTAINED progress ",
			roundi(distance),
			"m ",
			id,
			" v=",
			snappedf(speed, .1),
			" lateral=",
			snappedf(error, .01)
		)
	if error > 3.5 or step_m > 1.:
		finish(false, "left lane or discontinuous position")
		return
	if distance >= END_METRES and brake_test.is_empty():
		brake_test = {"begin_m": distance, "begin_mps": speed, "begin_tick": ticks}
	if not brake_test.is_empty():
		Input.action_release("gas")
		Input.action_press("handbrake")
		if not brake_test.has("at_12mps_m") and last_speed >= 12. and speed < 12.:
			brake_test["at_12mps_m"] = distance - step_m * (12. - speed) / (last_speed - speed)
		if speed < .5 and last_speed >= .5:
			var at_half = distance - step_m * (.5 - speed) / (last_speed - speed)
			brake_test["distance_12_to_0_5_mps_m"] = (
				at_half - float(brake_test.get("at_12mps_m", at_half))
			)
			brake_test["duration_s"] = (ticks - int(brake_test.begin_tick)) * dt
			brake_test["full_distance_m"] = distance - float(brake_test.begin_m)
			finish(
				(
					seam_rows.size() >= 3
					and seen.size() >= 4
					and min_contacts >= 3
					and max_cross_track < 1.5
				),
				"completed continuous route and full service brake"
			)
	last_speed = speed

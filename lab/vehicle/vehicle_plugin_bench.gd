extends SceneTree

var car: RigidBody3D
var world := Node3D.new()
var is_shift: bool
var hz: int
var tick := 0
var launch_tick := 0
var brake_tick := 0
var speed_at_brake := 0.0
var brake_position := Vector3.ZERO
var start_position := Vector3.ZERO
var peak_speed := 0.0
var min_contacts := 4
var max_lateral := 0.0
var finished := false
var turn_mode := false
var turn_tick := 0
var heading_at_turn := 0.0
var turn_yaw_deg := 0.0

func _initialize() -> void:
    seed(4367)
    turn_mode = "--turn" in OS.get_cmdline_user_args()
    is_shift = ResourceLoader.exists("res://Car/Scenes/Roadster.tscn")
    hz = Engine.physics_ticks_per_second
    build.call_deferred()

func build() -> void:
    root.add_child(world)
    var floor_body := StaticBody3D.new()
    var floor_shape := BoxShape3D.new()
    floor_shape.size = Vector3(1000, 1, 1000)
    var floor_collider := CollisionShape3D.new()
    floor_collider.shape = floor_shape
    floor_body.add_child(floor_collider)
    floor_body.position.y = -0.5
    floor_body.add_to_group("Road")
    world.add_child(floor_body)
    var path := "res://Car/Scenes/Roadster.tscn" if is_shift else "res://addons/gevp/scenes/simcade_car.tscn"
    car = load(path).instantiate()
    car.position.y = 0.45
    world.add_child(car)
    physics_frame.connect(step)

func step() -> void:
    if finished:
        return
    tick += 1
    # Settle for 2 s; Shift's manual clutch needs a prior gear change.
    if is_shift and tick == hz:
        Input.action_press("Clutch")
    if is_shift and tick == hz + 4:
        Input.action_press("ShiftUp")
    if is_shift and tick == hz + 7:
        Input.action_release("ShiftUp")
    if tick == hz*2:
        launch_tick = tick
        start_position = car.global_position
        if is_shift:
            Input.action_press("Gas")
        else:
            car.throttle_input = 1.0
    if is_shift and tick >= hz*2 and tick <= hz*3:
        Input.action_press("Clutch", maxf(0., 1.-float(tick-hz*2)/float(hz)))
    if is_shift and tick == hz*3:
        Input.action_release("Clutch")
    var velocity := Vector2(car.linear_velocity.x,car.linear_velocity.z).length()
    peak_speed = maxf(peak_speed, velocity)
    if launch_tick > 0:
        max_lateral = maxf(max_lateral, absf(car.position.x-start_position.x))
        var hits := 0
        for ray in car.find_children("*", "RayCast3D", false, false):
            if ray.is_colliding():
                hits += 1
        min_contacts = mini(min_contacts, hits)
    if turn_mode and turn_tick == 0 and launch_tick > 0 and velocity >= 30./3.6:
        turn_tick = tick
        heading_at_turn = car.rotation.y
        if is_shift:
            Input.action_press("SteerLeft", 0.25)
        else:
            car.steering_input = 0.25
    if turn_mode and turn_tick > 0 and tick == turn_tick+hz:
        turn_yaw_deg = rad_to_deg(wrapf(car.rotation.y-heading_at_turn,-PI,PI))
        if is_shift:
            Input.action_release("SteerLeft")
        else:
            car.steering_input = 0.
    var start_braking := tick == turn_tick+hz if turn_mode else velocity >= 30./3.6
    if launch_tick > 0 and brake_tick == 0 and start_braking and (not turn_mode or turn_tick > 0):
        brake_tick = tick
        speed_at_brake = velocity
        brake_position = car.global_position
        if is_shift:
            Input.action_release("Gas")
            Input.action_press("Clutch")
            Input.action_press("Brake")
        else:
            car.throttle_input = 0.
            car.brake_input = 1.
    if (brake_tick > 0 and velocity <= 0.15) or tick > hz*24:
        finished = true
        print("RESULT ", JSON.stringify({
            "trial":"quarter_steer_1_s" if turn_mode else "straight", "turn_yaw_deg":turn_yaw_deg,
            "variant": "Shift" if is_shift else "GEVP", "physics_hz":hz, "mass_kg":car.mass,
            "elapsed_s":float(tick)/hz, "peak_kph":peak_speed*3.6,
            "brake_started_kph":speed_at_brake*3.6, "final_kph":velocity*3.6,
            "accel_0_to_30_s":float((turn_tick if turn_mode else brake_tick)-launch_tick)/hz if brake_tick else null,
            "braking_s":float(tick-brake_tick)/hz if brake_tick else null,
            "braking_planar_m":Vector2(brake_position.x-car.position.x,brake_position.z-car.position.z).length() if brake_tick else null,
            "min_wheel_contacts":min_contacts, "max_abs_lateral_m":max_lateral,
            "gear":car.transmission.current_gear if is_shift else car.current_gear,
            "engine_rpm":car.engine.engine_rpm if is_shift else car.motor_rpm}))
        clean.call_deferred(0 if brake_tick > 0 and velocity<0.16 else 2)

func clean(code: int) -> void:
    world.queue_free()
    await process_frame
    await process_frame
    quit(code)

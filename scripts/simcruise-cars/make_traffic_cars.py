"""Writes data/cars/<id>.json for the traffic models that become drivable.

The driving data of each car is its real counterpart's published figures where
they exist (named in each car's "sources"), with the derived numbers computed
the way the earlier car files were (godot-final:game/cars, and the retired
generated cars at 8d103f0^): corner ride frequency to wheel rate, inertia from
the body box, hub height at the tyre's free radius.

Run from the project root:  python scripts/simcruise-cars/make_traffic_cars.py
The automatics' upshift_min_omega is 95 % of upshift_rpm: below 92 % the kickdown
could take back an upshift a slipping converter made early (a 1-2-1-2 hunt).
Calibration (static_mu, lateral_grip_scale) is then set from the CarTrack bench
(SimCruise.Vehicle.CarTrack.<id>) so that 0-60, 70-0 and skidpad land in band.
"""
import json
import math
import os

G = 9.80665


def tyre_radius(width, aspect, rim_in):
    return rim_in * 0.0254 / 2 + width * aspect


def derive(c, body_w, body_h, body_l, ride_hz, unsprung, static_len, contact_factor=1.388):
    m, fw = c["mass_kg"], c["front_weight"]
    r = tyre_radius(c["tire_width_m"], c["tire_aspect_ratio"], c["rim_in"])
    sprung_f = m * fw / 2 - unsprung
    sprung_r = m * (1 - fw) / 2 - unsprung
    d = {
        "inertia": [round(0.93 * m * (body_l ** 2 + body_h ** 2) / 12),
                    round(0.916 * m * (body_l ** 2 + body_w ** 2) / 12),
                    round(0.142 * m * body_w ** 2)],
        "collision_box": [body_w, round(body_h * 0.6, 2), round(body_l * 0.94, 2)],
        "collision_box_y": round(r + body_h * 0.3, 2),
        "mount_height_m": round(r + static_len, 4),
        "static_length_m": static_len,
        "unsprung_corner_kg": unsprung,
        "wheel_rate_n_m": [round(sprung_f * (2 * math.pi * ride_hz[0]) ** 2), round(sprung_r * (2 * math.pi * ride_hz[1]) ** 2)],
        "reference_corner_load_n": round(m * G / 4, 1),
        "max_contact_load_n": round(contact_factor * m * G),
        "coil_lb_in": [0.0, 0.0], "lever_ratio": [1.0, 1.0], "bar_diameter_m": [0.0, 0.0],
        "damper": ["ratio", "ratio"], "damper_dyno": [[], []],
    }
    d.update(c)
    return d


CARS = []

# --- Tesla Model 3 RWD: the retired "Volta" (8d103f0^:game/cars/volta.json) was this car under a
# fictional name, calibrated on CarTrack; its data moves onto the real model unchanged.
CARS.append(derive({
    "_comment": "Tesla Model 3 Rear-Wheel Drive (2017-2023), single motor and single reduction gear. Driving data from the retired Volta, which was this car under another name; model: Sketchfab 'Tesla M3 model' by wintrez (CC BY 4.0).",
    "id": "model3", "name": "Model 3 Rear-Wheel Drive", "make": "Tesla", "body": "sedan", "seats": 5,
    "mass_kg": 1645.0, "front_weight": 0.47, "cg_height_m": 0.46,
    "wheelbase_m": 2.875, "track_front_m": 1.58, "track_rear_m": 1.58,
    "tire_width_m": 0.235, "tire_aspect_ratio": 0.45, "rim_in": 18.0,
    "static_mu": 0.9821, "lateral_grip_scale": 1.0251, "drag_area_m2": 0.51,
    "parking_rack_deg": 35.0, "road_reference_deg": 33.0,
    "brake_front_nm": 2200.0, "brake_rear_nm": 1300.0, "parking_brake_nm": 1300.0,
    "engine": "electric", "motor_peak_torque_nm": 430.0, "motor_peak_power_w": 184928, "motor_max_rpm": 17000.0, "regen_torque_nm": 110.0,
    "torque_curve": [[0, 430], [17000, 104]],
    "gears": [1.0], "reverse_ratio": 1.0, "final_drive": 9.04, "driveline_efficiency": 0.95, "shift_seconds": 0.0,
    "idle_rpm": 0.0, "idle_omega": 0.0, "min_omega_auto": 0.0, "max_omega": 1900.0,
    "layout": "rwd", "rear_diff_bias": 1.0, "transmission": "single-speed",
    "bar_rate_n_m": [25000.0, 20000.0], "damper_ratio_rebound": [0.5, 0.5], "damper_ratio_compression": [0.3, 0.3],
    "damper_knee_mps": [0.12, 0.12], "damper_knee_slope": [0.45, 0.45],
    "static_camber_deg": [-0.6, -1.0], "camber_per_m": [18.0, 20.0], "caster_deg": 6.0, "toe_per_wheel_deg": 0.05,
    "target_0_60_s": 5.4, "target_70_0_ft": 165.0, "target_skidpad_g": 0.88,
    "traction_control": True, "abs_enabled": True, "slip_ratio_falloff": 0.1,
    "model_scene": "data/art/traffic/model3-3a602469/scene.gltf", "model_rig": {"fit": True},
    "driver_eye": [-0.37, 1.14, -0.07],
    "sources": {
        "calibration": "Car and Driver measured a Model 3 Standard at 5.4 s to 60 mph and 0.88 g on 235/45R-18 all-season tyres. The 70-0 mph target (165 ft) is an estimate.",
        "motor": "About 286 hp quoted for the current Standard; 430 N m motor torque and a 9.04 reduction are estimates from earlier RWD versions.",
        "mass": "About 3627 lb for the RWD sedan.",
        "estimates": "Weight split, centre of gravity, inertia (scaled), spring and bar rates (1.5/1.7 Hz), dampers, regeneration, brakes, tyre grip (calibrated)."},
}, 1.73, 1.0, 4.69, (1.5, 1.7), 35.0, 0.217))

# --- 2018 Ford F-150 Raptor SuperCab (the model is fitted as the SuperCab).
CARS.append(derive({
    "_comment": "2018 Ford F-150 Raptor SuperCab: 3.5 L high-output twin-turbo V6, ten-speed automatic, four-wheel drive in 4A. Model: Sketchfab '2018 Ford F-150 Raptor' by Galaxy Car Showroom (CC BY 4.0).",
    "id": "f150raptor18", "name": "F-150 Raptor SuperCab", "make": "Ford", "body": "pickup", "seats": 5,
    "mass_kg": 2584.0, "front_weight": 0.57, "cg_height_m": 0.86,
    "wheelbase_m": 3.393, "track_front_m": 1.869, "track_rear_m": 1.869,
    "tire_width_m": 0.315, "tire_aspect_ratio": 0.70, "rim_in": 17.0,
    "static_mu": 0.80, "lateral_grip_scale": 1.10, "load_sensitivity": 0.11, "drag_area_m2": 1.95,
    "parking_rack_deg": 33.0, "road_reference_deg": 31.0,
    "brake_front_nm": 4200.0, "brake_rear_nm": 2300.0, "parking_brake_nm": 2600.0,
    "engine": "combustion",
    "torque_curve": [[700, 300], [1500, 520], [2500, 660], [3500, 691], [4500, 665], [5000, 641], [5500, 570], [6000, 480]],
    "torque_past_curve_nm": 400.0, "idle_rpm": 650.0, "idle_omega": 68.0, "governor_rpm": 675.0,
    "fuel_cut_rpm": 6000.0, "max_gear_rpm": 6200.0, "engine_inertia": 0.25,
    "gears": [4.696, 2.985, 2.146, 1.769, 1.52, 1.275, 1.0, 0.854, 0.689, 0.636], "reverse_ratio": 4.866, "final_drive": 4.10,
    "driveline_efficiency": 0.86, "shift_seconds": 0.15, "upshift_rpm": 5600.0, "upshift_min_omega": 557.0, "downshift_rpm": 1300.0,
    "clutch_stiffness": 12.0, "clutch_capacity_nm": 900.0, "clutch_back_nm": 800.0, "launch_rpm": 1200, "launch_rpm_per_throttle": 1800.0,
    "converter_stall_ratio": 2.0, "converter_coupling_ratio": 0.87,
    "layout": "awd", "awd_front_share": 0.4, "center_bias": 3.0, "front_diff_bias": 1.0, "rear_diff_bias": 1.3, "transmission": "automatic",
    "bar_rate_n_m": [26000.0, 0.0], "min_travel_m": 0.12, "max_travel_m": 0.5, "bump_gap_m": 0.1,
    "damper_ratio_rebound": [0.42, 0.42], "damper_ratio_compression": [0.22, 0.22], "damper_knee_mps": [0.2, 0.2], "damper_knee_slope": [0.4, 0.4],
    "static_camber_deg": [-0.3, 0.0], "camber_per_m": [10.0, 0.0], "caster_deg": 6.0, "toe_per_wheel_deg": 0.05,
    "target_0_60_s": 5.7, "target_70_0_ft": 206.0, "target_skidpad_g": 0.68,
    "calibration_open": "0-60 mph 5.5 s against 5.7 s: the launch is traction-limited (four-wheel drive with traction control), so converter and launch speed do not move it; 5.5 s lies between Car and Driver's two SuperCab tests (2017: 5.0 s, 2018: 5.7 s).",
    "traction_control": True, "abs_enabled": True, "slip_ratio_falloff": 0.1,
    "model_scene": "data/art/traffic/f150raptor18-096aa1ba/scene.gltf", "model_rig": {"fit": True},
    "driver_eye": [-0.43, 1.74, -0.05],
    "sources": {
        "calibration": "Car and Driver, 2018 F-150 Raptor SuperCab: 5.7 s to 60 mph, reported to match their 2017 SuperCab in every other measure: 206 ft from 70 mph, 0.68 g on a 300 ft skidpad, 5696 lb as tested (the 2017 truck ran 5.0 s).",
        "engine": "450 hp at 5000 rpm, 510 lb-ft at 3500 rpm (Ford, 3.5 L high-output EcoBoost); curve shape estimated.",
        "gearing": "10R80 ratios (published) and the Raptor's 4.10 axle.",
        "tyres": "BFGoodrich All-Terrain T/A KO2 LT315/70R17 (Ford).",
        "estimates": "Weight split, centre of gravity, inertia (scaled), long-travel spring rates (1.2/1.4 Hz), Fox dampers as ratios, 4A front share, brakes, tyre grip (calibrated)."},
}, 2.19, 1.99, 5.89, (1.2, 1.4), 80.0, 0.30, 1.373))

# --- 2013 Jeep Grand Cherokee SRT8 (WK2).
CARS.append(derive({
    "_comment": "2013 Jeep Grand Cherokee SRT8 (WK2): 6.4 L V8, five-speed automatic, Quadra-Trac active four-wheel drive. Model: Sketchfab '2013 Jeep Grand Cherokee SRT8' by Galaxy Car Showroom (CC BY 4.0).",
    "id": "grandcherokee13", "name": "Grand Cherokee SRT8", "make": "Jeep", "body": "suv", "seats": 5,
    "mass_kg": 2386.0, "front_weight": 0.53, "cg_height_m": 0.66,
    "wheelbase_m": 2.915, "track_front_m": 1.628, "track_rear_m": 1.635,
    "tire_width_m": 0.295, "tire_aspect_ratio": 0.45, "rim_in": 20.0,
    "static_mu": 1.08, "lateral_grip_scale": 0.975, "load_sensitivity": 0.11, "drag_area_m2": 1.10,
    "parking_rack_deg": 34.0, "road_reference_deg": 32.0,
    "brake_front_nm": 4600.0, "brake_rear_nm": 2600.0, "parking_brake_nm": 2400.0,
    "engine": "combustion",
    "torque_curve": [[700, 380], [2000, 520], [3000, 585], [4300, 630], [5000, 615], [6000, 557], [6400, 500]],
    "torque_past_curve_nm": 420.0, "idle_rpm": 650.0, "idle_omega": 68.0, "governor_rpm": 675.0,
    "fuel_cut_rpm": 6400.0, "max_gear_rpm": 6500.0, "engine_inertia": 0.30,
    "gears": [3.59, 2.19, 1.41, 1.0, 0.83], "reverse_ratio": 3.16, "final_drive": 3.70,
    "driveline_efficiency": 0.86, "shift_seconds": 0.25, "upshift_rpm": 6100.0, "upshift_min_omega": 607.0, "downshift_rpm": 1800.0,
    "clutch_stiffness": 12.0, "clutch_capacity_nm": 900.0, "clutch_back_nm": 800.0, "launch_rpm": 1500, "launch_rpm_per_throttle": 1900.0,
    "converter_stall_ratio": 2.0, "converter_coupling_ratio": 0.87,
    "layout": "awd", "awd_front_share": 0.4, "center_bias": 3.0, "front_diff_bias": 1.0, "rear_diff_bias": 2.0, "transmission": "automatic",
    "bar_rate_n_m": [38000.0, 22000.0],
    "damper_ratio_rebound": [0.5, 0.5], "damper_ratio_compression": [0.3, 0.3], "damper_knee_mps": [0.12, 0.12], "damper_knee_slope": [0.45, 0.45],
    "static_camber_deg": [-0.6, -0.8], "camber_per_m": [16.0, 18.0], "caster_deg": 7.0, "toe_per_wheel_deg": 0.05,
    "target_0_60_s": 5.0, "target_70_0_ft": 152.0, "target_skidpad_g": 0.87,
    "traction_control": True, "abs_enabled": True, "slip_ratio_falloff": 0.1,
    "model_scene": "data/art/traffic/grandcherokee13-74686a75/scene.gltf", "model_rig": {"fit": True},
    "driver_eye": [-0.38, 1.30, 0.0],
    "sources": {
        "calibration": "Edmunds, 2012 SRT8: 5.0 s to 60 mph from rest (4.7 s with a 1 ft rollout, long-term car), 0.87 g, 112 ft from 60 mph; 5261 lb as tested. 70-0 mph 152 ft is the 60-0 figure scaled by speed squared (estimate). Chrysler quoted 4.8 s.",
        "engine": "470 hp at 6000 rpm, 465 lb-ft at 4300 rpm (SRT, 6.4 L HEMI); curve shape estimated.",
        "gearing": "W5A580 five-speed ratios and 3.70 axle (published for the 2012-2013 SRT8).",
        "tyres": "295/45ZR20 all round.",
        "estimates": "Weight split, centre of gravity, inertia (scaled), spring and bar rates (1.6/1.8 Hz), dampers, front torque share (Quadra-Trac SRT 'auto' 40/60), brakes, tyre grip (calibrated)."},
}, 1.95, 1.76, 4.82, (1.6, 1.8), 55.0, 0.20))

# --- 2018 Honda Odyssey (fifth generation).
CARS.append(derive({
    "_comment": "2018 Honda Odyssey: 3.5 L V6, front-wheel drive, nine-speed automatic (EX-L). Model: Sketchfab 'Honda Odessey' by MattDoesBlender (CC BY 4.0).",
    "id": "odyssey18", "name": "Odyssey EX-L", "make": "Honda", "body": "minivan", "seats": 8,
    "mass_kg": 2036.0, "front_weight": 0.56, "cg_height_m": 0.64,
    "wheelbase_m": 3.0, "track_front_m": 1.725, "track_rear_m": 1.725,
    "tire_width_m": 0.235, "tire_aspect_ratio": 0.60, "rim_in": 18.0,
    "static_mu": 0.90, "lateral_grip_scale": 0.975, "load_sensitivity": 0.10, "drag_area_m2": 1.0,
    "parking_rack_deg": 36.0, "road_reference_deg": 34.0,
    "brake_front_nm": 3000.0, "brake_rear_nm": 1500.0, "parking_brake_nm": 1500.0,
    "engine": "combustion",
    "torque_curve": [[700, 220], [2000, 290], [3500, 330], [4700, 355], [6000, 332], [6800, 290]],
    "torque_past_curve_nm": 250.0, "idle_rpm": 700.0, "idle_omega": 73.0, "governor_rpm": 725.0,
    "fuel_cut_rpm": 6800.0, "max_gear_rpm": 6900.0, "engine_inertia": 0.18,
    "gears": [4.70, 2.84, 1.91, 1.38, 1.0, 0.81, 0.70, 0.58, 0.48], "reverse_ratio": 3.80, "final_drive": 3.86,
    "driveline_efficiency": 0.88, "shift_seconds": 0.2, "upshift_rpm": 6500.0, "upshift_min_omega": 647.0, "downshift_rpm": 1500.0,
    "clutch_stiffness": 12.0, "clutch_capacity_nm": 500.0, "clutch_back_nm": 450.0, "launch_rpm": 1400, "launch_rpm_per_throttle": 1600.0,
    "converter_stall_ratio": 1.9, "converter_coupling_ratio": 0.87,
    "layout": "fwd", "front_diff_bias": 1.0, "transmission": "automatic",
    "bar_rate_n_m": [24000.0, 14000.0],
    "damper_ratio_rebound": [0.45, 0.45], "damper_ratio_compression": [0.28, 0.28], "damper_knee_mps": [0.12, 0.12], "damper_knee_slope": [0.45, 0.45],
    "static_camber_deg": [-0.5, -0.8], "camber_per_m": [16.0, 18.0], "caster_deg": 5.0, "toe_per_wheel_deg": 0.05,
    "target_0_60_s": 6.7, "target_70_0_ft": 180.0, "target_skidpad_g": 0.74,
    "calibration_open": "0-60 mph 8.2 s against 6.7 s: traction control holds the front tyres to about 0.38 g off the line, and the converter model has no lock-up clutch, so it slips about 13 % in second and third where the real nine-speed is locked. Both are powertrain-model gaps, not car data.",
    "traction_control": True, "abs_enabled": True, "slip_ratio_falloff": 0.1,
    "model_scene": "data/art/traffic/odyssey-175ce6fb/scene.gltf", "model_rig": {"fit": True},
    "driver_eye": [-0.40, 1.30, 0.20],
    "sources": {
        "calibration": "Car and Driver, 2018 Odyssey nine-speed: 180 ft from 70 mph, 0.74 g (stability control off); ten-speed: 184 ft, 0.75 g. Car and Driver timed the ten-speed Elite at 6.6 s to 60 mph and report the 126 lb lighter nine-speed EX-L 0.1 s behind it (6.7 s, the target), both 15.2 s at 95 mph in the quarter mile.",
        "engine": "280 hp at 6000 rpm, 262 lb-ft at 4700 rpm (Honda J35Y6); curve shape estimated.",
        "gearing": "ZF 9HP ratios; the 3.86 final drive is an estimate.",
        "mass": "About 4488 lb for the EX-L.",
        "estimates": "Weight split, centre of gravity, inertia (scaled), spring and bar rates (1.3/1.5 Hz), dampers, final drive, brakes, tyre grip (calibrated)."},
}, 1.99, 1.74, 5.21, (1.3, 1.5), 40.0, 0.22))

# --- 2025 Honda CR-V 1.5 turbo with CVT (front drive).
CARS.append(derive({
    "_comment": "2025 Honda CR-V EX: 1.5 L turbo four, CVT, front-wheel drive. The CVT is modelled as its seven stepped ratios, the way Honda's controller steps it at full pedal. Model: Sketchfab '2025 Honda CR-V 1.5 VTEC Turbo CVT' by Palma123 JAAT (CC BY 4.0).",
    "id": "crv25", "name": "CR-V EX", "make": "Honda", "body": "suv", "seats": 5,
    "mass_kg": 1583.0, "front_weight": 0.59, "cg_height_m": 0.60,
    "wheelbase_m": 2.70, "track_front_m": 1.623, "track_rear_m": 1.641,
    "tire_width_m": 0.235, "tire_aspect_ratio": 0.60, "rim_in": 18.0,
    "static_mu": 0.95, "lateral_grip_scale": 1.025, "load_sensitivity": 0.10, "drag_area_m2": 0.85,
    "parking_rack_deg": 36.0, "road_reference_deg": 34.0,
    "brake_front_nm": 2300.0, "brake_rear_nm": 1100.0, "parking_brake_nm": 1200.0,
    "engine": "combustion",
    "torque_curve": [[700, 140], [1300, 200], [1700, 243], [5000, 243], [6000, 226], [6500, 200]],
    "torque_past_curve_nm": 170.0, "idle_rpm": 750.0, "idle_omega": 79.0, "governor_rpm": 775.0,
    "fuel_cut_rpm": 6500.0, "max_gear_rpm": 6600.0, "engine_inertia": 0.14,
    "gears": [2.53, 1.80, 1.36, 1.07, 0.86, 0.68, 0.54], "reverse_ratio": 1.80, "final_drive": 5.25,
    "driveline_efficiency": 0.86, "shift_seconds": 0.12, "upshift_rpm": 6000.0, "upshift_min_omega": 597.0, "downshift_rpm": 1500.0,
    "clutch_stiffness": 12.0, "clutch_capacity_nm": 350.0, "clutch_back_nm": 320.0, "launch_rpm": 1500, "launch_rpm_per_throttle": 2100.0,
    "converter_stall_ratio": 1.8, "converter_coupling_ratio": 0.87,
    "layout": "fwd", "front_diff_bias": 1.0, "transmission": "automatic",
    "bar_rate_n_m": [24000.0, 14000.0],
    "damper_ratio_rebound": [0.45, 0.45], "damper_ratio_compression": [0.28, 0.28], "damper_knee_mps": [0.12, 0.12], "damper_knee_slope": [0.45, 0.45],
    "static_camber_deg": [-0.5, -1.0], "camber_per_m": [16.0, 18.0], "caster_deg": 5.0, "toe_per_wheel_deg": 0.05,
    "target_0_60_s": 8.3, "target_70_0_ft": 175.0, "target_skidpad_g": 0.82,
    "traction_control": True, "abs_enabled": True, "slip_ratio_falloff": 0.1,
    "model_scene": "data/art/traffic/crv25-91c66738/scene.gltf", "model_rig": {"fit": True},
    "driver_eye": [-0.37, 1.24, 0.05],
    "sources": {
        "calibration": "Estimates: Car and Driver timed the 2023 CR-V Hybrid at 7.9 s to 60 mph and reported the 1.5 turbo as a few tenths slower (8.3 s used); 70-0 mph 175 ft and 0.82 g are typical of this class on 235/60R18 all-season tyres, not a test of this car.",
        "engine": "190 hp at 6000 rpm, 179 lb-ft from 1700 to 5000 rpm (Honda); curve shape estimated.",
        "gearing": "CVT as seven steps across its range; ratios and the 5.25 final drive are estimates.",
        "mass": "About 3490 lb for the front-drive EX.",
        "estimates": "Weight split, centre of gravity, inertia (scaled), spring and bar rates (1.4/1.6 Hz), dampers, gearing, brakes, tyre grip (calibrated)."},
}, 1.87, 1.68, 4.70, (1.4, 1.6), 38.0, 0.20))

root = os.path.join(os.path.dirname(__file__), "..", "..", "data", "cars")
for car in CARS:
    if car["engine"] == "electric":
        car.pop("torque_curve")
        car["torque_curve"] = [[0, car["motor_peak_torque_nm"]], [car["motor_max_rpm"], 104]]
    path = os.path.join(root, car["id"] + ".json")
    # The tyre fit (scripts/fit-tyres.ps1) owns tyre_model and the tmeasy_* keys: keep what it wrote.
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            old = json.load(f)
        for key, value in old.items():
            if key == "tyre_model" or key.startswith("tmeasy_"):
                car[key] = value
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(car, f, indent=1, ensure_ascii=False)
        f.write("\n")
    print("wrote", car["id"])

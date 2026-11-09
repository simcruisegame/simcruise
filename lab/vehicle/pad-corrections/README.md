# Unwinding the Xbox stick after a corner

At a simulated 24.2 mph on Wyoming's North Highway 89, the Miata was turning left. Letting go of the stick left 8.46°/s of left yaw after 200 ms. Moving the stick 70% right left **13.78°/s of left yaw**. The old mapping returned the steering control at 4.4 full-scale units per second when centered, but used its slower 2.75 units/s turn-in rate as soon as the opposite stick crossed the deadzone.

The revised mapping spends the necessary fraction of each 60 Hz tick unwinding at 4.4 units/s. Upon crossing center, remaining time builds opposite lock at 2.75 units/s. At the same US 89 location, releasing left 8.49°/s of left yaw and countersteering produced 0.26°/s of **right** yaw. Each trial drove from rest with the standard Miata powertrain and stayed on four contacts. Ground friction, tires, alignment, springs, Bilsteins and steering lock retain their previous values. The off-center analog mapping applies when the mapped thumbstick has priority over keyboard input. This addresses a reproducible delay when correcting the car. A human with the particular Xbox controller would still need to judge how the revised steering feels. [Normal-road measurements](evidence/road-after.csv) · [Previous behavior](evidence/road-before.csv)

## Bench and driving evidence

The Godot 4.7.2 test sends JoypadMotion events through the normal ±12% deadzone, reads the action strengths, and runs Jolt with the actual four-wheel controller on a level dry surface. The starting speeds are 25, 40 and 55 mph, followed by an unpowered coast in neutral. A mirrored held turn is followed by a release, a small correction or a large correction. Another set uses 100 and 200 ms continuous stick sweeps. The experiment includes mild service braking in the 40 mph trials. Every trial retained four contacts. The physical controller, Bluetooth transport and driver's hand are outside this test. The check measures elapsed game ticks from the command, with 16.7 ms granularity.

| Measured response | Before | Revised |
| --- | ---: | ---: |
| Countersteer reached center later than letting go, across 40 paired step trials | 40 | 0 |
| More yaw toward the original turn than letting go, by over 0.2°/s after 200 ms | 22 / 40 | 0 / 40 |
| Largest extra time reaching center | 83.3 ms | 0 ms |
| 40 mph seed, 70% left to 70% right, time to clear left steering | 116.7 ms | 66.7 ms |
| Same maneuver, signed yaw toward the original turn after 200 ms | 29.91°/s | 19.70°/s |
| Same maneuver, 100 ms continuous stick sweep, original-direction yaw after 200 ms | 27.45°/s | 18.70°/s |
| Same maneuver, 200 ms continuous stick sweep, original-direction yaw after 200 ms | 26.46°/s | 24.33°/s |

Both editions ran 78 step trials and 52 sweep trials. After mirroring the left and right runs, the largest difference in yaw at the 200 ms sample was 0.000018°/s for step inputs and 0.0028°/s for continuous sweeps. Normal-driving verification selected US 89 near 43.753608, −110.622982 and used actual accelerator and steering inputs. Three runs for each edition traveled about 37 m each, with the correction after releasing the throttle. Vehicle speed at correction varied by less than 0.04 mph across the six runs. The real-world road shape and the in-gear powertrain make these road figures differ from the level, neutral-gear bench. [Run summary](evidence/summary.json)

The original parking-circle, steering pulse, tire relaxation, locked-brake turn, ride, differential, rear Hard S dyno and acceleration/braking/skidpad checks produced byte-identical telemetry in both editions. The normal-game input test also exercised both pad directions, centering and full keyboard lock. [Existing checks](evidence/regressions.txt)

## Vehicle evidence and limits

Mazda's U.S. sales catalog identifies the 1999 package. Car and Driver drove a preproduction five-speed with Sport suspension, yellow Bilstein dampers and 195/50VR-15 Michelin Pilot SX tires. Their published instruments measured 0.89 g on the skidpad and 174 ft for 70–0 mph. They described abrupt recovery at the limit and warned that production tuning could differ. The comparison supports the model's steady dry-grip context. It does not provide a real steering-angle or yaw-rate trace for these maneuvers. The game still uses approximate tire lateral stiffness/relaxation, front damping, differential bias and suspension kinematics. The sampled rear Bilstein curve comes from one supplier dynamometer test. The 1999 10th Anniversary six-speed and later or competition tire packages supply no calibration points here. [Mazda U.S. catalog](https://importarchive.com/brochure/mazdamiata1999_02) · [Contemporary instrumented road test](https://www.caranddriver.com/reviews/a15142268/1999-mazda-mx-5-miata-road-test-review/) · [Mazda workshop manual, 02–11](https://www.mellens.net/mazda/Mazda-Miata-1999-2001.pdf) · [Fat Cat Motorsports’ comparative rear damper test](https://www.fatcatmotorsports.com/igallery/graphics/OE_NB_Showa_and_HardS_vs_NA_OE_Rpkg_and_aftermarket_HD1.GIF)

## Reproduce

From the source root, run the bench against an imported game checkout. The optional last argument switches to the continuous sweeps. The signed lateral axis is represented as positive left, matching the game's turn command.

```sh
godot --headless --fixed-fps 60 --path game --script "$PWD/lab/vehicle/pad-corrections/pad_response.gd" -- /tmp/pad-step
godot --headless --fixed-fps 60 --path game --script "$PWD/lab/vehicle/pad-corrections/pad_response.gd" -- /tmp/pad-sweep --sweep
python3 lab/vehicle/pad-corrections/verify.py /tmp/pad-step
python3 lab/vehicle/pad-corrections/verify.py /tmp/pad-sweep
godot --headless --path game --script "$PWD/lab/vehicle/pad-corrections/road_check.gd" -- /tmp/pad-road.csv
```

The ordinary road replay requires the atlas helper beside the executable or in the editor project's sibling tools directory. The level-surface runs are network-free. To verify the archived telemetry with Python's standard library, use `python3 lab/vehicle/pad-corrections/verify.py lab/vehicle/pad-corrections/evidence/after`. Apply `--baseline` to report the previous behavior. Running the new regression against the older measurements intentionally fails. All four sets of bench telemetry and their input definitions are in the evidence directory.

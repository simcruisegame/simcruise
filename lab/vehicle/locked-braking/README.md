# Locked-wheel braking and steering

The stock car coasts onto a flat pad at 50 mph. It brakes fully, then applies a left or right steering command after the front wheels have stopped rotating. At a locked contact the friction force opposes the wheel's sliding tread velocity. The previous solver kept assigning lateral force from the rolling tire curve and could yaw the car 71.74° from a straight approach. At full lock the solver now resolves the measured sliding velocity against the existing traction envelope. It blends back to ordinary rolling behavior as wheel rotation recovers.

The same simulation trials gave 0.33° peak heading deviation for both steering directions, four tire contacts and 27.5 m straight-line displacement in the steer-after-lock trial. These are deterministic comparisons of the simulated car. A measured stock-Miata combined-slip tire dataset would be needed to establish absolute agreement with a real car. Steering while the front wheels are rolling still uses the existing tire law.

From the checkout, using the matching Godot editor, run `godot --headless --path game --fixed-fps 60 --script res://checks/LockedBrakeTurn.gd`. The check includes straight, left and right trials. Adjacent checks cover the transient steering response, split grip, rear damper, turning radius and stock acceleration and braking.

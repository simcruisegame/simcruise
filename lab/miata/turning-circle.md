# Miata turning circle

The benchmark records the outer front tire sidewall through a complete powered turn on a level pad, in both directions at 1.0 and 1.8 m/s. The car drives a half turn first to establish tire forces, speed and body roll. Each full lap must return within 12 cm of its starting point. The two perpendicular swept diameters must agree within 4 cm, all four contacts must hold, and both wheel angles must fall in the factory service range.

The US 1999 Miata road review reports 30.2 ft curb-to-curb. Mazda specifies 38° ±3° inner and 33° ±3° outer at maximum steering in its front-wheel alignment table. The model uses 195 mm nominal tire section width and a representative contact edge. The vehicle-specific rack travel, loaded tire shoulder profile and tire scrub have not been directly captured.

[1999 five-speed road review and specifications](https://www.theautochannel.com/vehicles/new/reviews/1999/lweitzman_mazdamiata.html) · [Mazda MX-5 workshop manual, section 02–11](https://www.mellens.net/mazda/Mazda-Miata-1999-2001.pdf) · [MotorTrend 1999 model-year road test, 10th Anniversary six-speed comparison](https://www.motortrend.com/reviews/1999-mazda-miata-2)

From the source root, run `godot --headless --fixed-fps 60 --path game --script res://checks/TurningCircle.gd`. The raw four-lap results for the previous and current model are in [turning-circle.json](turning-circle.json). The low-speed rack fit uses the diameter and the manual tolerances together. Higher-speed stick response reaches the prior curve at 6 m/s. This test does not measure highway transients.

# 14. The owner's green NB as a measured reference twin: lowered, wide wheels, cage, carbon spoiler, validated against the real car's logs (owner-car-twin)
category verification · impact 4 · 3 d · depends: tyre-mf-combined-core, reality-loop

## Rationale
The best reference available is the owner's own car: same model, drivable on real roads, and measurable. A variant built from measured changes (ride height, wheel and tyre size, cage mass, spoiler) and validated against phone IMU and GNSS logs from the same roads closes the 'taste reality' loop for both handling and looks. It also tests that the tyre-construction and geometry systems generalise.

## Code claims (proposer)
nb1999.json is the only Miata spec. CarSpec has no inheritance or variant mechanism (each JSON is standalone). The tyre block in tyre-mf-combined-core derives the curve from size and construction. CarPaint takes base colour per car. lab/miata already holds appearance receipts. CarSpec.gd:153-163 suspension scalars (static_length, travel, bump_gap).

## Design (proposer)
1) CarSpec 'extends': 'nb1999' with a deep-merge, so nb1999_owner.json holds only measured deltas:
- Ride height: -25 mm or the measured value, shortening static_length and bump_gap. Measure fender-to-hub-centre at four corners.
- Spring and damper part numbers if changed.
- Wheels and tyres: the actual width, offset and size (e.g. 15x8 ET25 with 205/50R15 or 225/45R15). Feed the tyre block's construction and width so that K_alpha, mu and sigma scale with it.
- Cage: about +20-28 kg at about 0.75 m height (CG and inertia update via the Allen-style mass shift), plus an optional chassis-stiffness note.
- Carbon spoiler: lift_area rear of about -0.02 m² (estimate).
- Green paint: deltaE-matched from owner photos under known sun.
- Corner weights from scales if available. Alignment from the shop sheet. Tyre pressures.
2) Real-car logging protocol in lab/reality/owner-car/README.md:
- A phone in a fixed mount running a CSV IMU and GNSS logger at 50-100 Hz on a chosen public road segment, driven legally and safely.
- Steady-state circles in a legal empty lot at 2-3 speeds (for the ay against speed envelope), straight-line coast-downs from 50 mph in neutral (rolling plus aero), and gentle braking tests.
3) RealCarCompare tool: aligns the logs to the same OSM road in SimCruise, then compares
- the steady-state ay against steer-implied radius,
- the coast-down deceleration curve, with A and B fitted,
- the pitch and roll angle at a given g (from phone attitude),
- ride vertical acceleration PSD on the same pavement (with RoadRoughness on).
4) The visual variant: roll cage mesh, spoiler mesh, wide wheels via the SportWheel parameters, lowered stance. Turntable compared against owner photos.

## Gate
The base nb1999 stays bit-identical through all checks. The variant runs CarTrack, StepSteer and RollPitchCheck as report-only (there is no published figure). Twin acceptance:
- Coast-down deceleration at 30 and 50 mph within ±10 % of the logged values.
- Steady-state roll at 0.5 g within ±0.5 deg of the phone attitude.
- Silhouette IoU at least 0.95 and paint deltaE at most 5 against owner photos.
- Any change to the shared NB physics that worsens twin agreement must be justified in the CHANGELOG.

## Files
game/CarSpec.gd
game/cars/nb1999_owner.json
game/SportWheel.gd
game/CarFinish.gd
lab/reality/owner-car/README.md
lab/reality/owner-car/RealCarCompare.mjs
docs/cars.md

## Skeptic verdict
claims_hold false · already_done false · keep false

### Wrong claims
- Off-topic for the relayed user request. The user asked about an AI engine to help with traffic, extras and generation of detail. An owner-car handling and appearance twin does not answer that question, whatever its own merits.
- The claim that 'CarSpec has no inheritance', offered as a reason to need an 'extends' deep-merge, is misleading. CarSpec.gd's defaults are the 1999 NB (header comment, lines 1-9). load_id() (lines 203-211) starts from new() and apply() overrides only the keys a file contains. A nb1999_owner.json holding only the measured deltas plus a new id therefore already works today, with no new mechanism.
- The claim 'The tyre block in tyre-mf-combined-core derives the curve from size and construction' describes a proposal, not existing code. No such block exists. In physics, tire_width_m is used only for tire_radius_m() (CarSpec.gd:184) and for a mesh split in ModelRig.gd:183. Grip comes from scalar fields (static_mu, lateral_grip_scale, slip_stiffness, load_sensitivity), so wider tyres would change nothing about handling.
- 'CarPaint takes base colour per car' is wrong: there is no CarPaint.gd. The NB paint is a hard-coded constant, CarFinish.gd:43 PAINT_COLOR = #0f3b2a, applied to the 'body_British racing green' mesh. There is no per-car colour field. The base car is also already dark green.
- 'Carbon spoiler: lift_area rear of about -0.02 m2' assumes an aero lift model that does not exist. CarSpec has only drag_area_m2. Neither Powertrain.gd nor Drive.gd contains any lift or downforce term, so this needs new shared physics, not a data delta.
- 'Wide wheels via the SportWheel parameters' is wrong. SportWheel.gd builds one fixed 15x6 wheel on a 195/50R15 tyre and caches it in a static var (_wheel). It takes no width, offset or size parameters.
- 'Lowered stance' for the visual variant: the visual ride height is the constant CarFinish.gd:39 DROP = .025. It is not derived from static_length_m, so lowering the physics would not lower the model.
- 'CarSpec.gd:153-163 suspension scalars' is roughly right but the lines are off: static_length_m, min_travel_m, max_travel_m and bump_gap_m are at lines 155-158.
- The gate checks 'StepSteer' and 'RollPitchCheck' do not exist in game/checks. The closest existing checks are SteerTransient, RideCheck and TireStep.
- 'Allen-style mass shift' updating the CG and inertia: no such code exists. A grep for Allen, mass_shift and ballast finds nothing. mass_kg, cg_height_m and inertia are independent hand-set fields, so cage mass would need a new helper or hand-computed values.
- The claims that nb1999.json is the only Miata spec and that lab/miata holds appearance receipts are true (lab/miata/appearance/receipt.json and turntable/receipt.json). There is also a separate game/NB1999Sport.gd.

### Design problems
- Relevance: it does not address the user's question about an AI engine for traffic, extras and detail generation.
- 'Report-only' is not automatic. CarTrack --all (CarTrack.gd:33), LiftOff.gd:68, CombinedSlip.gd:58, TractionControlCheck.gd:38 and AbsReverse.gd:20 all iterate CarSpec.available(). The variant would inherit the NB's targets (7.97 s, 173 ft, 0.89 g) and fail CarTrack unless calibration_open is set. LiftOff and the other per-car gates have no report-only path, so a new car file can break scripts/check.sh.
- A new JSON file also appears in the in-game car picker (Drive.gd:874), so it becomes a selectable car, not just a test fixture.
- The physics deltas mostly cannot take effect yet. Width-dependent grip waits on the unbuilt tyre-mf-combined-core, aero lift is missing, and visual ride height and wheel geometry are constants. In practice the measured physics delta reduces to mass, CG, static_length and bump_gap, alignment, and spring and damper data.
- The measurement protocol is weak for the gates it sets. Phone attitude from a fused accelerometer and gyro at 0.5 g lateral is biased by the very acceleration being measured, so ±0.5 deg roll is optimistic without a gyro-integrated, calibrated mount. Coast-downs from 50 mph in neutral on public roads raise safety and legality issues and are sensitive to wind and grade. These need a known flat segment with runs in both directions.
- Comparing ride PSD on 'the same pavement' assumes RoadRoughness reproduces that specific road's profile. It is a synthetic, general roughness model, not measured pavement, so the PSD comparison cannot be like-for-like.
- Silhouette IoU of 0.95 or more against owner photos requires matched camera intrinsics and pose. No pose-recovery tooling for owner photos exists, so the threshold is arbitrary.
- Privacy: GNSS logs from the owner's car on real roads are personal location data. They should stay out of the repo or be trimmed and anonymised. The proposal says nothing about this.
- There is a conflict with realism-first calibration: the stock NB's grip scalars are fitted to Car and Driver's stock-car figures. Tuning shared NB physics toward a modified car (the CHANGELOG clause) risks pulling the published stock calibration.

### Corrected design (USE THIS where it differs)
Do not pursue this as the answer to the current request: the user asked about an AI or procedural engine for traffic, extras and detail generation. If an owner-car twin is wanted later, scope it down.

1. No 'extends' mechanism. Add game/cars/nb1999_owner.json holding only the deltas plus a unique id and name, relying on CarSpec's NB defaults. Set target_* to null-equivalents and a calibration_open note so CarTrack reports it rather than failing.
2. Before adding the file, decide how the per-car gates (LiftOff, CombinedSlip, TractionControlCheck, AbsReverse) treat it: either add a 'report_only' or 'variant_of' field that those checks honour, or keep the file under lab/ and load it by path, out of CarSpec.available() and the car picker.
3. Physics deltas, limited to what the code consumes today: mass_kg, cg_height_m and inertia, computed by a small documented script from the cage mass and position (not a runtime 'Allen' system); static_length_m and bump_gap_m from measured fender-to-hub heights; static_camber and toe from the alignment sheet; coil_lb_in and the damper table if the parts changed; tyre size for radius only. Mark wider-tyre grip and spoiler lift as blocked on tyre-mf-combined-core and a new shared aero-lift term, and keep both out of the twin.
4. Visual parameterisation as separate prerequisites: make CarFinish.DROP derive from the spec's ride height (or add an appearance ride-height field) and give SportWheel width, offset and diameter parameters with per-spec caching. Add a per-spec paint_color field read by CarFinish in place of the PAINT_COLOR constant, with the base NB keeping #0f3b2a.
5. Reality logging: use a gyro-fused attitude logger at 100 Hz or more on a rigid mount, run coast-downs in both directions on a known flat legal segment, and do steady-state circles only on private or permitted ground. Keep raw GNSS out of git and store only trimmed, relative traces.
6. Acceptance gates: steady-state roll gradient (deg/g) within 15 % rather than ±0.5 deg absolute; coast-down A and B fit within ±10 %; drop ride PSD and photo IoU until measured road profiles and a camera-pose tool exist.
7. The base nb1999 must stay bit-identical. Verify with checks/Track.gd and CarTrack on nb1999 before and after.

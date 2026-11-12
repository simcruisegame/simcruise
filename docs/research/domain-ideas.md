# Game domains and original ideas (12 November 2026)

This note divides SimCruise into domains. Four research agents studied the domains with real sources: papers, books, standards, sim developers and film crews. This note then gives new ideas for the game. Each idea:

- comes from a finding in the research, with its source,
- has a cost on the 28 W RTX 2070 Max-Q GPU (most ideas use the CPU only),
- has a test against reality.

Items marked **[unconfirmed]** were not checked at the source. Ideas only: this note changes no code. The threads that own each area decide what to build (see "Who owns what" at the end).

## The domains

| Domain | What it covers |
|---|---|
| 1. Vehicle dynamics | suspension, chassis flex, weight transfer, aero, brakes, tyre heat, road texture |
| 2. Powertrain | lash and shuffle, clutch, limited-slip diff, cold engine |
| 3. Human factors and haptics | how drivers sense speed and grip, latency, rumble, motion sickness, gaze |
| 4. Audio and acoustics | engine synthesis, tyre/road noise, wind, cabin, reflections |
| 5. Camera and cinema | field of view, film car rigs, shutter, head motion, grain, replays |
| 6. Perception and illusion (magic) | attention, change blindness, crowding, what makes an image read as a photo |
| 7. Rendering on low power | foveation, Lumen and shadow budgets, upscaling, atmosphere, distance |
| 8. Real-world data | crash, traffic, roughness, weather, season, night lights, signs, local AI |
| 9. Traffic behaviour | car-following, lane change, reaction time, regional culture, pedestrians |
| 10. Game design and culture | slow play, nostalgia, famous roads, car meets, atlases |

## Key findings (the ones the ideas use)

**Driving and the senses**

- F1. Self-aligning torque peaks at about 2-4° slip and falls to zero at peak grip. The steering goes light *before* the front tyres let go. https://en.wikipedia.org/wiki/Self_aligning_torque
- F2. The hand on a steering wheel is most sensitive at 3-5 Hz (0.13 m/s² RMS). Road vibration at the wheel holds most energy at 10-60 Hz; column resonance is at 20-50 Hz. (Giacomin, Brunel) https://bura.brunel.ac.uk/bitstream/2438/1435/1/gsdr2004fwf.pdf
- F3. Road texture scales (PIARC): microtexture < 0.5 mm sets friction; macrotexture 0.5-50 mm sets wet grip and noise; megatexture 50-500 mm sets vibration. https://en.wikipedia.org/wiki/Road_texture . HPMS gives IRI roughness per road segment. https://www.fhwa.dot.gov/Policyinformation/hpms/shapefiles.cfm
- F4. Tyre noise at 60 mph (FHWA OBSI): open-graded rubber asphalt 97.0 dBA, dense asphalt 100.5, longitudinally tined concrete 103.3, transversely tined concrete 106.2. Peak at 800 Hz (asphalt) and 1 kHz (concrete). Level rises about 35·log10(v). https://www.fhwa.dot.gov/Environment/noise/resources/pavement_effects_implementation_study/pei03.cfm
- F5. Drivers in a sim with a 1:1 field of view drive about 10% too fast. Rendering about 1.22 times wider than the screen angle removes most of the error. (Coventry) https://pureportal.coventry.ac.uk/en/publications/geometric-field-of-view-manipulations-affect-perceived-speed-in-d/
- F6. Edge rate (texture items passing per second: dashes, posts) predicts speed judgement better than total optic flow. (Larish & Flach 1990) https://corescholar.libraries.wright.edu/psychology/367
- F7. Fog that fades contrast with distance makes drivers slow down (85 → 71 km/h). Fog that fades contrast evenly makes them speed up (101 km/h). Wrong fog changes felt speed. (Pretto et al. 2012) https://elifesciences.org/articles/00281
- F8. About 20% of people notice latency under 40 ms total; the most sensitive notice +6-12 ms. (Melzi & Previati 2024) https://re.public.polimi.it/retrieve/3a55f199-2d18-4092-9c6a-4e9d74725400/melzi-previati-2024-driving-simulator-analysing-the-impact-of-mechanical-latency-on-the-perception-of-lateral-dynamics.pdf
- F9. Visual motion sickness is worst at 0.2-0.4 Hz. https://repository.lboro.ac.uk/articles/Frequency_characteristics_of_visually_induced_motion_sickness/9346865/files/16955915.pdf
- F10. Drivers look at the tangent point of a bend 1-2 s ahead (Land & Lee 1994, https://www.nature.com/articles/369742a0). During cornering, more than 95% of gaze falls in the 1-2 s guiding zone (https://www.frontiersin.org/journals/psychology/articles/10.3389/fpsyg.2019.01699/full). Attention narrows to the vanishing point as speed rises (DR(eye)VE, https://arxiv.org/pdf/1705.03854).
- F11. Seat-to-head vibration peaks at 5.3-6.5 Hz (gain about 2.2) and about 10 Hz. https://www.jniosh.johas.go.jp/en/indu_hel/doc/IH_48_5_565.pdf
- F12. The NB Miata had two diffs: 1999-2002 Torsen (bias about 2.5:1), 2003-05 Tochigi Fuji cone-clutch (about 2.0:1). https://miata.net/garage/diffguide
- F13. A 2-inertia, 1-lash-gap driveline model stays within 5% of a 24-inertia model for tip-in shuffle (SAE 2019-01-0345). https://digitalcommons.mtu.edu/michigantech-p/326
- F14. SHRP2 "Surface Accelerations Reference": lateral and longitudinal acceleration statistics from about 3,500 drivers and 34 million miles, open download. https://vtechworks.lib.vt.edu/handle/10919/114928
- F15. The Xbox controller has four motors (low, high, left trigger, right trigger), each 0-1. https://learn.microsoft.com/en-us/gaming/gdk/docs/reference/input/gameinput/structs/gameinputrumbleparams

**Sound and camera**

- F16. Open physical engine models: the Sound Design Toolkit motor (waveguide pipes, three outputs: intake, cabin vibration, exhaust; https://sdt.lim.di.unimi.it/docs/API/v2.2-078/group__motor.html) and engine-sim (MIT). A 2026 pulse-train resonator model (https://arxiv.org/abs/2603.09391, code CC BY-NC) beats harmonic-plus-noise by 21%.
- F17. Real onboard recording: up to 10 mics per car (firewall "glue" omni, each exhaust, cabin), exhaust peaks above 135 dB SPL. https://www.redsharknews.com/audio/item/3698-10-mics-per-car-what-it-takes-to-record-audio-for-videogames
- F18. Burnout's sound team sold speed with whooshes pre-fired from forward raycasts and exaggerated Doppler. https://designingsound.org/2017/08/31/burnout-a-sound-design-retrospective-with-ben-minto/
- F19. Ford v Ferrari: cameras low with asphalt in frame, hard-mounted with no vibration isolation, grain in the LUT. https://theasc.com/articles/ford-v-ferrari-lap-of-honor
- F20. Panning photos: at 1/500 s there is no sense of speed; 1/125 s is the threshold; 1/60 s gives strong blur. https://snapshot.asia.canon/en/article/motorsports-photography-techniques-1-panning-techniques
- F21. Top-down convertible at 75 mph: about 90 dB at the driver's ear. https://hearingreview.com/inside-hearing/research/another-risk-to-hearing-driving-a-convertible

**Perception, magic and rendering**

- F22. Looking is not seeing: changes made during blinks were missed more than 40% of the time even with gaze within 1°. Changes to "marginal-interest" objects were missed more. (O'Regan, Rensink et al. 2000) https://www2.psych.ubc.ca/~rensink/publications/abs.00.4.html
- F23. Magicians: "a big move covers a small move"; secret moves happen before the audience thinks the trick starts. (Macknik, Martinez-Conde 2008) https://www.gwern.net/docs/psychology/illusion-of-depth/2008-macknik.pdf . Teller: the lie you tell yourself fools you best; repetition gets noticed. https://www.smithsonianmag.com/arts-culture/teller-reveals-his-secrets-100744801/?all
- F24. Motion sensitivity falls to near zero about 17 ms into a saccade; eyes make about 3 saccades per second. https://www.frontiersin.org/journals/systems-neuroscience/articles/10.3389/fnsys.2015.00143/full
- F25. In CG vs photo tests, rough surfaces were rated "real" 0.71 vs 0.39 for smooth; soft shadows 0.62 vs 0.47. Number of lights and objects did not matter. (Rademacher 2001) https://techreports.cs.unc.edu/papers/03-031.pdf
- F26. Foveated rendering: 5-6× less work at 1080p (Guenter 2012). The periphery notices flicker, not blur (Patney 2016). Tuned noise can replace lost peripheral detail. https://research.nvidia.com/publication/2016-12_towards-foveated-rendering-gaze-tracked-virtual-reality.md , https://www.pdf.inf.usi.ch/projects/NoiseBasedEnhancement
- F27. Lumen traces reflection rays on every pixel with roughness below 0.4. https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-performance-guide-for-unreal-engine
- F28. Visual range ≈ 3.912 / extinction (Koschmieder). Disney "weenies" are landmarks that pull the eye and hide the edges. https://en.wikipedia.org/wiki/Visibility , https://www.above-the-garage.com/wienies/

**Data, traffic and design**

- F29. FHWA TMAS gives hourly counts per station; HPMS gives AADT, lanes, speed limit and IRI per segment; Census LODES gives home-to-work flows. https://www.fhwa.dot.gov/policyinformation/tables/tmasdata/
- F30. IDM defaults: T 1.5 s, s0 2 m, b 3 m/s²; two driver types reproduce the main traffic patterns. MOBIL politeness p 0.3. https://traffic-simulation.de/info/info_IDM.html
- F31. Reaction time: expected 0.7 s, brake light 1.25 s, surprise 1.5 s (Green 2000). Only 1-24% of drivers fully stop at a free stop sign. https://visualexpert.com/Resources/reactiontime.html , https://azbikelaw.org/stop-sign-compliance/
- F32. MUTCD advance sign distances: "Stop Ahead" at 55 mph 325 ft; curve to 35 mph from 55 mph 150 ft. https://www.txdot.gov/manuals/trf/sfb/warning_signs/warning_sign_advance_placement-i1007447/advance_placement_distance_for_warning_signs-i1007.html
- F33. USA-NPN Spring Index is CC BY 4.0 (https://usanpn.org/terms). VIIRS Black Marble night lights: use NOAA-20 (VJ146); Suomi NPP has stopped. https://www.earthdata.nasa.gov/data/projects/black-marble
- F34. State DOT camera terms differ: Iowa forbids reuse; Caltrans encourages it. Check each state.
- F35. Slow play: idleness and "ludocatalysts" (pauses with no consequence) are central to reflective games. https://dl.digra.org/index.php/dl/article/download/2162/2161/2158 . Nostalgia is triggered by music and season. https://www.southampton.ac.uk/~crsi/Sedikides%20&%20Wildschut,%202018,%20Review%20of%20General%20Psychology.pdf
- F36. Tail of the Dragon: 11 miles, a claimed 318 curves. Route 66 had its centennial on 11 November 2026. Cars & Coffee grew to 2,000+ cars on Saturday mornings by word of mouth.
- F37. Small LLMs on CPU: 1-3B models are practical; 7-8B runs at about 5-8 tok/s on DDR4 **[unconfirmed blog numbers]**.

## The ideas, ranked

Ranked by impact on "feels and looks like the real place and car" against cost. The top five are new systems that join several domains; the rest are smaller.

### 1. The Gaze Director: one model of where the driver looks, used by everything

**From:** F10, F22, F24, F26, F23.
**Idea:** Driver gaze is more than 95% predictable from the road itself: the tangent point, the lane 1-2 s ahead, the vanishing point, the mirrors. The CPU computes a small attention map each frame from the road geometry that the streamer already has. Many systems then read the same map:

- **GPU spend:** full detail in a zone of about 10°; outside it, variable-rate shading, lower material LOD and cheaper shadows. The zone narrows as speed rises, like real attention.
- **Camera:** a small yaw lean toward the tangent point 1-2 s before a bend, like the head turn of a real driver (single screens hide the apex).
- **Streaming:** LOD and texture swaps go first where the map is cold (see idea 4).
- **Detail placement:** the best props go where the eye goes.

**28 W fit:** prediction is CPU only. Research shows 50-70% less shading outside the fovea. This is the biggest GPU saving in this note.
**Test:** score the map against DR(eye)VE gaze (NSS, KL) (check its licence before use). Then a blind A/B with a webcam eye-tracker: can players tell foveated from full render? Goal: no better than chance. Log `stat GPU` saved.

### 2. One road, three senses: the road surface signature

**From:** F2, F3, F4, F15, F25.
**Idea:** Today the look, the sound and the feel of a road come from different places. Make one **road surface signature** per segment: OSM `surface`, HPMS IRI, pavement type (asphalt, chip seal, tined concrete, joint spacing) and age. Three outputs read it:

- **Feel:** the controller's low motor gets the 1-5 Hz body band from the IRI profile; the high motor gets the 20-60 Hz band from the macrotexture class; concrete slab joints give a slap at the real spacing.
- **Sound:** tyre noise level and spectrum from the FHWA OBSI table (+5.7 dB and a 1 kHz peak on transversely tined concrete; 800 Hz on asphalt).
- **Look:** the same class selects the road material's normal and roughness, and the wear mask.

When the road changes from new asphalt to old concrete, the player hears it, feels it and sees it at the same metre. Real drivers know roads this way.
**28 W fit:** CPU noise filters (about 30 biquads per wheel); the material selection already exists.
**Test:** at 60 mph, third-octave tyre levels within ±2 dB of the FHWA table. Phone accelerometer on the owner's steering wheel on two known roads: the 1-5 Hz to 20-60 Hz energy ratio matches the game within 20%.

### 3. Garage Day: one afternoon that measures the owner's own Miata

**From:** F11, F12, F13, F16, F17, F21, and gaps in all reports (no free recordings of the three cars, no eye height data, no cabin data).
**Idea:** The owner has the real car. One written protocol, with only a phone (no purchase), collects the data that no paper gives:

1. Phone IMU on the headrest and console on a known road: seat-to-head transfer function, tip-in shuffle frequency and decay (2nd gear at 2,000 rpm).
2. Phone at the tailpipe and at the headrest, 2,000 → 7,000 rpm in 3rd: engine orders for the synth.
3. A sine sweep from a phone speaker at the tailpipe, recorded at the driver's ear, top up and top down: the cabin transfer function, with the roll bar and wing fitted.
4. Rear wheel off the ground, try to turn it by hand with the other wheel down: Torsen (no lock) or cone-clutch (preload). This also confirms the model year.
5. Cold start with an OBD reader if one is to hand: idle rpm and coolant against time; tyre pressure cold and after 10 minutes.
6. Tape measure: eye height, seat position, steering wheel angle.

The recordings belong to the owner, so the licence is clean.
**28 W fit:** no runtime cost; this is calibration data.
**Test:** every value gets a gate in the car's benches: engine orders 2 and 4 within ±3 dB; shuffle frequency within 10%; head transfer peaks within ±1 Hz.

### 4. The magician's streamer: swap things only when nobody can see

**From:** F22, F23, F24, F10.
**Idea:** Change blindness needs a masking event. Driving is full of them: a wiper pass, a truck passing, the flash of an overpass shadow, sun glare, a glance at a mirror, a camera cut, a saccade at the start of a bend. The streamer queues LOD swaps, HLOD-to-full changes and mip upgrades, and releases them at those moments, first in "marginal-interest" regions (cold in the Gaze Director map). When a tile is late, the game makes a central event a little more salient (a brake light ahead, a bird) while the edge resolves. "A big move covers a small move." It never fakes a hazard.
**28 W fit:** the streamer can be more aggressive, so less is held in memory at full detail.
**Test:** players press a button whenever they see pop-in. Compare detection rates for masked and unmasked swaps. Goal: masked swaps detected at less than half the rate.

### 5. The honest speed gate

**From:** F5, F6, F7, F18, F20.
**Idea:** Make "drivers judge speed correctly" a measured gate, like the physics benches. The knobs:

- Rendered field of view about 1.22 times the physical screen angle, set from the screen size and the seat distance.
- Lane dashes, reflector posts and guard rail posts are never culled or merged by LOD, because edge rate carries speed.
- Fog and haze always fade contrast with distance, from the real local visibility (METAR) through Koschmieder.
- Tyre and wind noise at the real levels (wind to the 6th power of speed; about 90 dB top-down at 75 mph).
- Motion blur from a shutter set by speed, not frame rate, with 1/125 s equivalent as the start of visible blur.

**28 W fit:** almost free. A wider field of view puts more of the frame at the cheap edges.
**Test:** the Coventry protocol at home. Speedometer hidden, the player holds 30, 50 and 70 mph. Goal: mean error under 5% (sims are about 10% today). Repeat after any camera, fog or sound change.

### 6. The light trigger: grip warning from the steering, not the slide

**From:** F1, F15.
**Idea:** On a real car, the steering goes light as front grip runs out. An Xbox controller has no steering torque, so give that signal to the left impulse trigger: its level follows the *drop* in self-aligning torque from its peak. The warning arrives before the slide, as on the real car. The right trigger pulses with ABS and wheelspin.
**28 W fit:** one value per frame.
**Test:** on the skidpad bench, the trigger level peaks at 2-4° front slip, before peak lateral force. Then blind A/B laps: how often does the driver recover from understeer?

### 7. The grime floor: dirt is cheaper than shine

**From:** F25, F27.
**Idea:** Rough surfaces were the largest single cue that a picture is real. Lumen traces reflection rays on every pixel smoother than 0.4. Give every surface a floor of grime and micro-roughness (roads, car paint above the clear coat layer, glass, signs), plus soft shadows and contact shadows. Realism goes up and the most costly reflection rays go away.
**28 W fit:** saves GPU time.
**Test:** a blind "photo or game?" test against licensed photos of the same places, scored by d′; log the Lumen reflection milliseconds saved.

### 8. Your own past self, on a living paper atlas

**From:** F35, F36.
**Idea:** Store every drive as a small spline with its date, weather, season and music. On a road you drove before, your past self can drive with you as a quiet ghost, with no lap times. All drives ink into a paper road atlas in a Rand McNally-like style of our own: red highways, scenic overlays, mileage numbers between towns, and your driven roads with dates and notes in the margin. No fail states; pull-offs and overlooks are pauses with no cost.
**28 W fit:** one translucent car; the atlas is a one-off 2D render.
**Test:** the replay matches the original within 0.5 m; atlas mileages match OSM route lengths within 1%.

### 9. The real year: season and night from satellites

**From:** F33.
**Idea:** Leaf-out and fall colour per grid cell come from the NPN Spring Index (CC BY) and Sentinel-2 NDVI (with the Copernicus notice), so the fall colour moves down the Blue Ridge on the real dates. NOAA-20 VIIRS night radiance sets streetlight density and sky glow; drive out of town at night and the Milky Way appears.
**28 W fit:** a vegetation shader parameter and a sky parameter.
**Test:** game leaf-out date within 7 days of NPN observer records; game sky brightness against VIIRS at 100 points, Spearman r > 0.8.

### 10. Traffic that keeps the real clock and the local manners

**From:** F29, F30, F31, F14.
**Idea:** Each road gets an hourly × weekday × month volume curve from TMAS stations, scaled by HPMS AADT, with direction from LODES (inbound in the morning). Each car draws IDM and MOBIL values, stop-sign behaviour (most roll through when the road is clear) and reaction time from a regional mix. The SHRP2 acceleration statistics are the target for how hard traffic brakes and corners, and also a "drives like a real person" score for the player.
**28 W fit:** CPU lookup tables.
**Test:** hold out 10% of TMAS stations: hourly share within 15% MAPE. AI acceleration percentiles per road class match SHRP2.

### 11. Signs from the rulebook, curves with names

**From:** F32, F36.
**Idea:** An offline pass measures curve radius from OSM, gives each curve a MUTCD advisory speed and places the W1 sign at the table distance. Stop Ahead and merge signs the same way. Mapped OSM signs override. The same pass finds and counts curves, so famous roads get a "curve book" with the community's names.
**28 W fit:** instanced meshes.
**Test:** the detector finds 318 ±10% curves on US 129. In one county with a public sign inventory, at least 70% of generated curve signs are within 100 ft of a real sign.

### 12. Guard rail flutter and tunnel roar

**From:** F16, F18.
**Idea:** From OSM barriers, walls, tunnels and buildings, the CPU casts 8-16 sound rays per emitter and builds a bank of delayed taps with Doppler. You hear the real "zzzt" of guard rail posts and the roar in a tunnel when you downshift.
**28 W fit:** CPU only.
**Test:** pass a guard rail at known distance d: comb notches at f = c/(2d) in the recorded output.

### 13. The second unit: a replay director that shoots like a film crew

**From:** F19, F20.
**Idea:** A rule-based director picks shots from telemetry and map data: a low pod when edge rate is high, a rising "elevator" shot on crests (from the terrain), a long lens on vistas, a hard-mounted camera that passes road vibration through. Shutter is set per shot (1/60-1/30 s for rolling shots), and grain at 10-50% as in the film grade. Replays can render at 30 fps or offline to a file, so full motion blur is affordable.
**Test:** blind viewer scores against a replay cut by a person.

### 14. Comfort gates: latency and sickness

**From:** F8, F9.
**Idea:** Two ship gates. Input-to-photon plus physics-to-rumble under 40 ms. Camera pitch and roll in the 0.2-0.4 Hz band are reduced; bumps above 1 Hz stay.
**Test:** a 240 fps phone video of stick and screen; Simulator Sickness Questionnaire scores for 20-minute drives, filter on and off.

### 15. Smaller ideas

- **Model-year-correct diff** (F12): Torsen or cone-clutch, from Garage Day step 4.
- **Two-mass shuffle** (F13): the tip-in lurch that 0-60 calibration never finds.
- **Cold start** (data from Garage Day step 5): oil friction and cold tyres in the first 5-10 minutes.
- **Physical engine from the spec sheet** (F16): waveguide pipes from real firing order and pipe lengths; three emitters (intake, cabin, exhaust). Firing orders: NB 1-3-4-2; 289 1-5-4-8-6-3-7-2; LT2 1-8-7-2-6-5-4-3 **[unconfirmed]**.
- **Honest co-driver** (F37): a 1-3B CPU model reads facts from the place knowledge graph (advisory speeds, active fires, weather alerts) and gives short notes, generated during straights. Every claim must trace to a graph fact. It never names crash victims.
- **Saturday dawn meet** (F36): every real Saturday 7-9 am, a real parking lot fills with other players' recorded ghost cars, with a soft cap.
- **Grain, not blur** (F26): fill the cheap periphery with noise matched to a camera sensor, not blur; it hides foveation and gives a camera look.
- **Discover the detail** (F23): put the most variety where attention widens (stopped, parked, at junctions); a few variants are enough at speed in the periphery.

## Who owns what

| Idea | Owner thread |
|---|---|
| 1, 4, 7, grain, discover the detail | buildings / rendering ("Make buildings look like the real place") |
| 2 (feel), 6, 14, diff, shuffle, cold start, 3 (steps 1, 4, 5) | physics ("Make the Miata drive like the real car") |
| 2 (sound), 3 (steps 2, 3, 6), 12, engine, 13 | cars ("Make every traffic car drivable") |
| 5, 9, 10, 11 | live data ("Pull real-time real-world data into the game") |
| co-driver, sign and curve facts | knowledge graph |
| 8, Saturday meet | to-do list (new) |

## Gaps to fill next

Pneumatic trail values in mm; yaw perception thresholds; brake pad friction against temperature; C8 Z51 downforce; shuffle frequency in Hz; the licences of SDT, the Newson grain code and DR(eye)VE; which state DOTs allow camera reuse. Garage Day closes several of these for the Miata.

# Sound designer: study notes

Role: vehicle audio for SimCruise. Scope: engine voice, intake and exhaust, load, rev limiter, tyres, road, wind, cabin and exterior pickup, traffic voices, Doppler, mix and loudness.
Written 2026-11-12. Style: ASD-STE100 Simplified Technical English (short sentences, active voice, one instruction per sentence).
Marks: **[V]** = verified in a source on 2026-11-12. **[U]** = not verified; treat as an estimate. **[C]** = read from SimCruise code.

## 1. What the player must hear

1. The player must hear engine speed. Pitch must follow rpm with no lag that a driver can notice.
2. The player must hear engine load. Full throttle at 3000 rpm must sound different from a coast at 3000 rpm.
3. The player must hear grip. Scrub must start before the tyre limit. Squeal must start at the limit.
4. The player must hear speed. Tyre hiss and wind must rise with speed when the engine note is steady (cruise in top gear).
5. The player must hear the car type. An inline four and a cross-plane V8 must not sound alike.
6. The mix must never clip. The mix must not tire the ear on a one-hour drive.

## 2. Engine physics for sound

### 2.1 Firing frequency

- A four-stroke engine fires each cylinder once per two crank revolutions. **[V]** (standard four-stroke theory; also the f0 = rpm/120 cycle rate in Doerfler and Wyse 2026)
- Firing frequency f_fire = rpm x N_cyl / 120 Hz. **[V]**
- Inline four: f_fire = rpm / 30 (2 pulses per revolution). Engine order 2 dominates. **[V]**
- V8: f_fire = rpm / 15 (4 pulses per revolution). Engine order 4 dominates. **[V]**
- Even-fire V6: f_fire = rpm / 20 (engine order 3). **[V]**
- Cycle rate (one 720 degree cycle) = rpm / 120. Half orders come from cycle-to-cycle differences between cylinders. **[V]**

Numbers for the three cars that matter:

| Car | Idle | f_fire at idle | Fuel cut (data) | f_fire at fuel cut |
|---|---|---|---|---|
| Miata NB 1.8 inline four (nb2001_aaron.json) | 850 rpm | 28.3 Hz | 7250 rpm | 241.7 Hz |
| Corvette C8 LT2 cross-plane V8 (meridian_z.json) | 700 rpm | 46.7 Hz | 6600 rpm | 440 Hz |
| Mustang 1965 289 cross-plane V8 (mustang65.json) | 600 rpm | 40 Hz | 5000 rpm | 333 Hz |

### 2.2 Cross-plane V8 burble

- A cross-plane crank fires each bank at uneven intervals (90, 180, 270 degrees within a bank). **[V]** (crank geometry; also the code comment in SimCruiseInlineFourSynth.h)
- With two separate pipes, each pipe receives an uneven pulse train. The listener hears orders below order 4 (orders 0.5, 1, 1.5...). This is the "burble". **[U]** for the exact level of each order.
- If both pipes are equal in distance and gain, the burble cancels. The SimCruise test `SimCruise.Audio.CrossPlaneV8` checks this. **[C]**
- Firing orders: Ford 289: 1-5-4-2-6-3-7-8. GM LT2: 1-8-7-2-6-5-4-3. **[U]** (manual numbering from CarPresentation.json comments; not re-checked against a service manual here).

### 2.3 Load

- Combustion pressure rises with load. Exhaust pulses become sharper. Upper harmonics rise with load. **[V]** (Baldan et al. 2015 treat the exhaust pulse shape as the load-dependent source; Doerfler and Wyse 2026 model pulse shape and harmonic decay)
- On overrun (closed throttle, car drives engine) the exhaust is weak. Intake noise is weak. Pops can occur at high rpm. **[U]** for the pop probability.
- Rule: drive the timbre from delivered combustion torque. Do not drive it from the pedal alone. A pedal at 100 % on the limiter has no combustion during a fuel cut.

### 2.4 Rev limiter

- A fuel-cut limiter removes combustion for some cycles. The sound alternates between firing and silence at roughly 10-20 Hz. **[U]** (typical range, car-specific)
- Rule: the audio limiter must use the same rpm as the powertrain fuel cut. It must read it per car.

### 2.5 Intake, exhaust and cabin

- Exterior pickup: the exhaust dominates. Upper orders reach several kHz.
- Cabin pickup: the body and the soft top cut the high frequencies. Low orders and the body boom (about 100-150 Hz) dominate. **[U]** for the boom frequency of the NB Miata.
- Intake noise is strong under high load and wide throttle. It is louder in a roadster cabin than in a closed coupe. **[U]**
- Rule: switch pickup with a crossfade or a filter whose state continues. Do not reset filter state on a camera change (clicks).

## 3. Synthesis methods (choose by budget)

| Method | Source | Strength | Weakness |
|---|---|---|---|
| Harmonic tables / wavetable per firing event (SimCruise today) | Code [C] | Cheap. Deterministic. Easy to test. | Static timbre per load. No pipe resonance. |
| Granular / sample loops by rpm and load | Jagla et al. 2012 (PSOLA) [V] | Real recorded timbre. | Needs licensed recordings. Artefacts on fast rpm changes. |
| Physically informed (pulse source + pipe waveguides) | Baldan et al. 2015; Sound Design Toolkit motor model [V] | Pipe resonances. Intake and exhaust as separate outputs. | More tuning. More CPU. |
| Neural pulse-train resonator | Doerfler and Wyse 2026 (arXiv 2603.09391, CC BY-NC-SA) [V] | Better fit to recordings. | Licence is non-commercial. Needs training data. |

Rules:
- Keep the current procedural voice for this phase. Fix bugs only.
- Before any sample-based work, confirm the licence of every recording. Use only own recordings or recordings with a written licence.
- The paper arXiv 2603.09391 is CC BY-NC-SA. Do not copy its code into SimCruise unless the licence of the code is checked.

## 4. Tyres, road and wind

- Tyre/road noise rises about 30-40 dB per decade of speed (A-weighted pass-by). **[U]** (SimCruise uses amplitude ~ v^1.5, that is 30 dB per decade [C])
- Asphalt tyre noise peaks near 800-1000 Hz. Transversely tined concrete adds energy near 1 kHz. **[U]** (FHWA OBSI data, cited in docs/research/domain-ideas.md)
- Squeal is a tonal band. It starts when the tyre passes its peak slip. Scrub (broadband) starts before the peak. **[U]** for the exact squeal frequency of a 195/50R15 or 205/45R16 tyre.
- Wind noise power rises steeply with speed (aeroacoustic sources scale between v^5 and v^8 in power). **[U]**
- The owner asked for wind as only a faint hiss at highway speed (header comment, 10 November 2026). Keep that request. **[C]**

## 5. Spatial audio and Doppler

- Doppler shift: f' = f x c / (c - v_r), where v_r is the closing speed and c = 343 m/s. **[V]** (physics)
- At a closing speed of 30 m/s the pitch rises about 9.6 % (about 1.6 semitones). At 30 m/s opening it falls about 8 %.
- Unreal attenuation settings give spatialization, falloff, air absorption and focus. The attenuation page does not list a Doppler control. **[V]** (Epic "Sound Attenuation" docs). Doppler needs a pitch control on the source (Sound Cue Doppler node, or a pitch input on a MetaSound or synth). **[U]** for MetaSound-specific nodes.
- Air absorption: use a distance-driven low-pass for traffic voices. Distant cars must lose treble first.

## 6. Unreal implementation

- SimCruise uses `USynthComponent` subclasses that render DSP on the audio render thread (`OnGenerateAudio`). **[C]** Epic documents `USynthComponent` as the base for procedural audio components. **[V]**
- MetaSounds give a DSP graph, parameter inputs and sample-accurate scheduling. **[V]** (MetaSounds overview). Use MetaSounds only when a designer must tune without C++. The C++ synth is acceptable now.
- Route every source through a Sound Class and a Submix. A Submix lets the game apply a master limiter and measure loudness. **[U]** for exact UE 5.8 API names.
- Keep game-thread to audio-thread parameter hand-off lock-light. SimCruise copies a small struct under a critical section. **[C]** This is acceptable.

## 7. Loudness

- EBU R128: programme loudness target -23.0 LUFS; maximum true peak -1 dBTP. **[V]** (EBU R128 2014)
- Many console game publishers use about -24 LKFS integrated (Sony ASWG-R001) for home play. **[U]** (not re-read on 2026-11-12)
- Rule: measure integrated loudness of a 10-minute standard drive. Target -24 LUFS +-2 LU. Keep true peak below -1 dBTP.
- Rule: measure loudness range (LRA). A driving game with a highway cruise and a full-throttle run should stay within about 10-20 LU. **[U]**
- Rule: give the player at least master, engine and road/wind volume controls. Game Accessibility Guidelines and XAG ask for separate volume controls. **[U]** for the exact guideline numbers.

## 8. How to judge SimCruise

Files to read first:
- `Source/SimCruise/Private/Audio/SimCruiseInlineFourSynth.cpp` and `Public/Audio/SimCruiseInlineFourSynth.h` (engine voice).
- `Source/SimCruise/Private/Audio/SimCruiseRoadNoiseSynth.cpp` and its header (tyres, road, wind, cabin filter).
- `Source/SimCruise/Private/Audio/SimCruiseEngineSoundComponent.cpp` (USynthComponent glue).
- `Source/SimCruise/Private/Vehicle/SimCruiseVehicle.cpp` (`UpdateEngineSound`, `UpdateRoadNoise`).
- `Source/SimCruise/Private/World/SimCruiseTraffic.cpp` (`UpdateVoice`).
- `Content/Vehicles/CarPresentation.json` (`engine_sound` per car).
- `data/cars/*.json` (`idle_rpm`, `fuel_cut_rpm`, `max_gear_rpm`).
- Tests: `Private/Audio/Tests/SimCruiseAudioTests.cpp`, `SimCruiseRoadNoiseTests.cpp`.

Checks (each one must pass):
1. Pitch: an FFT of 1 s at steady rpm shows the strongest peak at rpm x N_cyl / 120 +-2 %. Do it for all three cars.
2. Limiter: the audio limiter rpm equals the car's `fuel_cut_rpm` +-50 rpm. The synth must not clamp pitch below `max_omega`.
3. Load: at fixed rpm, full load is at least 6 dB louder than overrun. Spectral centroid rises with load.
4. Cabin vs exterior: at 5 kHz the cabin is at least 12 dB down. A view change makes no click (peak step < 0.05 between buffers).
5. Tyres: no scrub below normalised slip 0.6. Scrub before squeal. Squeal level grows monotonically with slip.
6. Speed: at a steady engine note, road plus wind level rises at least 3 dB from 20 m/s to 30 m/s.
7. Headroom: the summed mix never exceeds -1 dBFS true peak in a worst case (limiter + pops + squeal + rough road).
8. Loudness: integrated loudness of the standard drive within -24 +-2 LUFS.
9. Determinism: same inputs give the same samples (existing test).
10. Traffic: a car passing at 25 m/s shows a pitch fall of about 14 % across the pass (Doppler), and treble falls with distance.

Test methods:
- Unit tests render the pure DSP classes offline (no audio device). Add cases, do not replace existing ones.
- For loudness, render the mix offline to WAV and measure with an R128 meter (for example `ffmpeg -af ebur128`).
- For listening tests, compare with owner-recorded clips of the real NB Miata. Use the same rpm and load. Record the comparison in `lab/vehicle/engine-sound`.

## 9. Common failure modes

1. Hard-coded rpm constants from one car used for all cars (limiter, pitch clamp, pop rpm).
2. Hard clipping (`Clamp`) instead of a soft limiter. It adds harsh odd harmonics at full load.
3. Smoothing coefficients applied once per buffer. Their time constant then changes with buffer size.
4. Filter state reset on a camera change. It clicks.
5. Engine timbre driven by the pedal, not by combustion. The limiter and the clutch then sound wrong.
6. Every traffic car with the same voice. Trucks and V6 cars sound like the player's four.
7. No Doppler and no air absorption on passing cars. Speed then feels low.
8. Only an on/off sound switch. The player cannot balance the engine against the road.
9. Level checks by peak or RMS only. Perceived loudness (LUFS) is not checked.
10. Recorded material with unclear licences.

## 10. References

1. S. Baldan, H. Lachambre, S. Delle Monache, P. Boussard, "Physically informed car engine sound synthesis for virtual and augmented environments", IEEE SIVE 2015. https://core.ac.uk/download/53578982.pdf (open access copy) **[V]** for title and year (via ref. [7] of arXiv 2603.09391); **[U]** for the full author list
2. J. Jagla, J. Maillard, N. Martin, "Sample-based engine noise synthesis using an enhanced pitch-synchronous overlap-and-add method", JASA 2012. **[V]** (citation via ref. [3] of arXiv 2603.09391; full text not read)
3. R. Doerfler, L. Wyse, "Physics-Informed Neural Engine Sound Modeling with Differentiable Pulse-Train Synthesis", arXiv 2603.09391, 2026, CC BY-NC-SA 4.0. https://arxiv.org/html/2603.09391 **[V]**
4. Sound Design Toolkit, motor model API. https://sdt.lim.di.unimi.it/docs/API/v2.2-078/group__motor.html (from docs/research/domain-ideas.md) **[U]**
5. EBU R 128 (2014), Loudness normalisation and permitted maximum level. https://tech.ebu.ch/docs/r/r128-2014.pdf **[V]**
6. EBU Tech 3344 / R128 s4 supplement. https://tech.ebu.ch/docs/r/r128s4.pdf **[U]**
7. Epic Games, MetaSounds in Unreal Engine. https://dev.epicgames.com/documentation/en-us/unreal-engine/metasounds-in-unreal-engine **[V]**
8. Epic Games, Sound Attenuation in Unreal Engine. https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-attenuation-in-unreal-engine **[V]**
9. Epic Games, USynthComponent API. https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AudioMixer/USynthComponent **[V]** (page listed; not read in full)
10. Ben Minto, Burnout sound design retrospective (pre-fired whooshes, exaggerated Doppler). https://designingsound.org/2017/08/31/burnout-a-sound-design-retrospective-with-ben-minto/ **[U]**
11. G. Taylor (Sony), "Loudness and how to measure it" (ASWG loudness talk). **[U]** (slides seen only as a search result)

Licence notes: EBU documents are free to download. Epic docs are reference only. arXiv 2603.09391 is non-commercial share-alike: learn from it, do not copy code or weights into the product.

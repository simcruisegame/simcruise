# The 1999 Miata's engine sound

The generator follows the stock four-cylinder, four-stroke firing cadence, two exhaust events per revolution. Its phase follows the simulated crank continuously. The engine solver supplies pedal opening, combustion torque including the idle governor, and signed clutch load averaged over its six normal substeps. These change exhaust pressure, inlet sound and airflow. Closed-throttle engine braking, clutch disengagement, interrupted torque during a shift and an actual stalled motor produce different output. The cabin camera attenuates high-frequency content. The existing mute toggle remains off by default. [Miata BP cylinder firing order](https://www.autozone.com/diy/repair-guides/626-1999-2001-firing-orders-repair-guide-p-0996b43f81b3cc00)

[Listen to the game's 15-second engine output](1999-inline-four-drive.mp3). The stock Sport solver accelerates, automatically upshifts, coasts, brakes, stalls in a tall manually selected gear and restarts in neutral. This preview is the sound output before the game's master volume. Every sample is original procedural audio. Engine-family references constrain its character. No verified dynamic recording of a stock 1999 BP-4W under synchronized RPM, throttle and microphone position was available for calibration. The exhaust transfer curve, intake balance and absolute sound-pressure level remain estimates.

## Measured results

* At an inferred firing rate of about 42 Hz, the cabin's relative levels across the first ten harmonics had a median absolute difference of 3.54 dB from the family-level interior recording. The prior three-harmonic signal differed by 14.70 dB. The same comparison against the separate exterior clip improved from 24.23 to 7.38 dB. We used three 0.65–0.80 s windows following startup. Order spacing is inferred from the recordings, which contain no tachometer or calibrated sound meter. The two reference perspectives come from the same owner. [Order levels and analysis windows](acoustics.json)
* At a fixed 3000 rpm the model's driven state was 9.66 dB louder than coasting. The pressure pulse retained more high-frequency energy under load. A separate Eunos neutral-rev recording also rises in level and high-frequency energy, but its aftermarket HKS system makes it unsuitable for matching stock loudness or exact pitch color. No deceleration crackle from the modified exhaust was copied. [Operating-state measurements](acoustics.json)
* Native Godot generated the 368 frames needed for one 60 Hz tick at 22.05 kHz in under 0.18 ms at the 95th percentile for each of six test conditions on this Mac. The ordinary main scene started muted, played and advanced its sample buffer when toggled, and stopped when toggled again. [CPU measurements](cpu.json)
* Jolt simulated 139.4 m over a 900-tick drive. The unmodified powertrain, the observer with sound enabled and the observer muted produced identical RPM, gear, shift timer, body position, forward velocity, contacts and stall state at every tick. The model reached 6706 rpm, shifted automatically, coasted with negative shaft load, stalled and restarted. [Drive receipt](drive-receipt.json)

From the source root, run the same test using the usual Godot editor binary.

```sh
godot --headless --path game --fixed-fps 60 --script res://checks/EngineSoundDrive.gd -- /tmp/engine-on --record
godot --headless --path game --fixed-fps 60 --script res://checks/EngineSoundDrive.gd -- /tmp/engine-off --muted
python3 lab/vehicle/engine-sound/check-drive.py /tmp/engine-on.json /tmp/engine-off.json
```

## Recording provenance

* TurboTheSergal identified two recordings published in June 2020 as the inside and outside of a Mazda MX-5 Miata. Model year, engine displacement, intake and muffler are unspecified. Both are CC BY 4.0. The project includes measured summary features only. [Interior, Freesound 523351](https://freesound.org/people/TurboTheSergal/sounds/523351/) · [Exterior, Freesound 523352](https://freesound.org/people/TurboTheSergal/sounds/523352/) · [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)
* Walter_Odington's free Eunos Roadster rev recording describes an aftermarket HKS exhaust through the manifold. It was used only to check the direction of the change between quiet and revving sound. [Freesound 18620, CC0](https://freesound.org/people/Walter_Odington/sounds/18620/)
* The owner of a 2001 Miata describes the Roadstersport III muffler with baffle removed. The recording's stronger resonances and deceleration crackle were excluded from stock-timbre calibration. [Owner's recording](https://archive.org/details/youtube-EuFVp_gs12c) · [Owner's configuration notes](https://archive.org/download/youtube-EuFVp_gs12c/EuFVp_gs12c.description)
* A separate 1999 five-speed seller described the car as stock apart from its wheels and supplied a cold-start video. An open redistribution license and synchronized tachometry could not be verified. The listing is a reference only. [Original auction](https://bringatrailer.com/listing/1999-mazda-mx-5-miata-129/)

# Technical artist: study notes

## Rules
- Physically based values: base colour is albedo in linear space. Walls ≤ 0.75, concrete 0.25-0.35, asphalt 0.05-0.12, grass 0.08-0.25 (team lighting notes). Metals use metallic = 1 with measured F0.
- Never bake light into albedo textures. Photos must be de-lit before use.
- Moving parts (steering wheel, needles) turn from sim data, never by a fixed speed: wheel angle = mean road-wheel angle x overall steering ratio (`SimCruiseCarPresentationComponent.cpp:44-47`).
- Gauges: static face texture + one rotating needle per dial; no per-frame canvas (team review 2026-11-13).
- Budget on the 28 W GPU: the AI detail layer ≤ 1.5 ms (estimate).

## Tests
- Daylight capture: patch luma / albedo within ±10 % of the plain material.
- Cockpit capture per car: needles match speed and rpm; wheel angle matches the sim.

## Sources
- Epic, Physically Based Materials: https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine
- B. Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 course notes.
- T. Akenine-Möller et al., *Real-Time Rendering*, 4th ed., CRC Press, 2018.

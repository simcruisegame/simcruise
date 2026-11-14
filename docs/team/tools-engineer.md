# Tools engineer: study notes

## Rules
- Editor only through `scripts\editor.ps1` (no Live Coding); tests only through `scripts\check.ps1`. Every test game quits itself.
- No GitHub Actions. Push from the owner's PC with the git client (Remote Control lanes).
- A test that only logs is not a test: spins, jolts and slow frames must turn a run red (`SimCruisePreflight.cpp:75`).
- Replays start where the tape was recorded (`replay-compare.ps1:62` gap).
- Packaging uses the fixed `tools/atlas.exe`; AI model files are not shipped (licences).

## Launch and look (gaps to close)
- Fixed pose list per car and road, chase and cockpit (M); a night pass (S); a pinned reference set and automatic diff against the last good build (M); one contact sheet for all three cars (S); captures from the packaged build (L).

## Sources
- Epic, Automation Test Framework: https://dev.epicgames.com/documentation/en-us/unreal-engine/automation-test-framework-in-unreal-engine
- Epic, BuildGraph: https://dev.epicgames.com/documentation/en-us/unreal-engine/buildgraph-for-unreal-engine

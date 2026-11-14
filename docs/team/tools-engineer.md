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

## What I now know (onboarding, 2026-11-14)

**How my area works today.**
- `scripts/preflight.ps1:78-100` maps each road key to start arguments. Hero keys are hard-wired. Each `us-*` key in `data/test-places.json` becomes `-SimCruiseStart=lat,lon` with no start road (:89). `pool` becomes `-SimCruiseRandomStart -SimCruiseStartSeed=$Seed` (:90). `random` draws `-RandomCount` (default 3) keys with `Get-Random -SetSeed $Seed`, and the default seed is today's date as yyyyMMdd (:84, :92-99).
- For each road and car, the script runs one `UnrealEditor.exe -game` under the editor lock. Every run uses Ultra, 1280x720, `-SimCruiseNoLive -SimCruiseNoNeural` and a fixed `-SimCruiseUtc=2026-11-12T17:00:00Z` (:43, :121-123). A run times out after 20 min (:45, :130).
- The script reads the last `PREFLIGHT PASS|FAIL` line from the log and counts engine errors (Ensure, Fatal, `LogSimCruise*: Error:`) (:137-147). ffmpeg makes a 10 fps mp4 and a 4x3 contact sheet (:155-157). The script then writes `report.md` and adds one row to `%LOCALAPPDATA%\SimCruise\preflight-index.csv` (:200-212).
- `SimCruisePreflight.cpp:372-388`: the robot waits up to 600 s for tiles, shaders and the view check (`ReadyTimeoutS`, :45). It settles for 2 s, then drives. Target speed is min(class speed limit, 30 m/s), with 3 m/s² lateral and 3 m/s² braking (:53-55, :567).
- `Judge` (`SimCruisePreflight.cpp:77-99`) fails a run on these limits from `SimCruisePreflight.h:78-93`: distance < 200 m, off road > 1 s, white sky > 25 %, frame p95 > 50 ms, any tile pop-in, any road step or gap, any border jolt, a spin, or a start more than 3 km from the asked point (:965-970). The 3 km check runs only when `-SimCruiseStart` is set.
- `SimCruiseGameMode.cpp:172-201` picks the start: `-SimCruiseStart` ("chosen"), else a seeded pool road, else the bundled Ocean Drive. The car snaps to the named road, else to the nearest public road, else to the ground (:559-585). A failed pool chain falls back to Ocean Drive (:404-410).
- `SimCruiseAtlasClient.cpp:92-113` looks for the helper at `SIMCRUISE_MAP_HELPER`, then `tools/atlas.exe`, then `bun helper/atlas.mjs`. The cache is `UserSettingsDir/SimCruise/map-cache`, or `SIMCRUISE_MAP_CACHE` when it is set.
- Helper cache: `helper/atlas-cache.mjs` `cacheReader` uses a TTL per file (default 30 days; the OpenFreeMap manifest has 1 day, `atlas.mjs:94`), a lock per file, a request interval per provider and a 9 s timeout. When a fetch fails, it gives back the stale file. `atlas.mjs:51` caps the cache at 96 MiB, and `cache-eviction.mjs` removes the least recently used files.
- `scripts/check-helper.sh`: 19 offline lab tests, 2 `bun test` suites and a bundle build. The tests that fetch live data do not run.
- `scripts/package.ps1:62-101` copies `data\` without the `*.onnx` files. It builds `tools\atlas.exe` with `bun build --compile` and only warns when the helper is missing.
- `scripts/playtest.ps1:53-62`: the gate uses only the last index row for the commit.

**Does it work at a random US place?**
- General: the `-SimCruiseStart` snap and the robot work at any lat/lon. A place with no road in range is a correct red (robot-moved-only). The helper's sources are US-wide, and `photograph()` uses NAIP for Hawaii and Alaska (`atlas.mjs:61-76`).
- Hard-wired: the hero keys. Also the single UTC time: 17:00Z is 11:40 solar time at Ocean Drive, but only about 07:00 at `us-anchorage-ak` (dark in November) and about 06:40 at `us-hilo-hi` (sun near the horizon) (estimate, from longitude/15 h).
- "random" is not random in the US. It picks from 18 fixed points, so a North Dakota section road gets tested only if it is `us-casselton-nd`.
- Poor map data (`us-welch-wv`, `us-hazard-ky`): the 600 s ready timeout lets the drive start before tiles arrive. A cold cache then shows as `tile-pop-in`, and the report does not say "cold cache" (estimate).
- Packaged build, offline with a cold cache: every place except Ocean Drive falls back to Miami (`GameMode.cpp:404`). Offline with a warm cache: stale files work, but after 1 day each tile first waits up to 9 s for the manifest fetch (`atlas.mjs:94`, `atlas-cache.mjs`) (estimate). A player can type lat,lon in place search (`SimCruiseAtlasUI.cpp:426`). No script runs or captures the packaged build at a place.

**Main gaps against "realistic everywhere".**
1. P1 | Random-place runs are not repeatable | `atlas.mjs:97` uses aerial only when it is already cached, lane counts are cache-only "pending" (:79-87), `.miss` files last 1 h, the cache is shared with normal play, and the 96 MiB LRU lets places evict each other | Add `-MapCache <dir>` (sets `SIMCRUISE_MAP_CACHE`), a warm-up pass, and write the helper `requests` count to `preflight.json`.
2. P1 | Fixed UTC gives night or dawn in the west | `preflight.ps1:43` | Compute UTC per place as 12:00 solar time = 12 h − lon/15 on the fixed date.
3. P1 | A pool fallback to Miami can PASS | `SimCruisePreflight.cpp:965` checks only `bRequestedStart` | Set `bFellBack` when `IsBundledPlace` and the road is not `ocean-drive`.
4. P1 | No "any lat/lon" or real random point | `preflight.ps1:78-100` | Add `-At lat,lon` and a seeded `random-us` that draws points on US land (for example Census TIGER places).
5. P2 | The play-test gate accepts any scope | `playtest.ps1:54` | Require the hero roads plus at least 3 random keys in the passing row.
6. P2 | No captures from the packaged build at a random place | `package.ps1`, `release.ps1` | Add a packaged preflight mode.

**My first three actions.**
1. Per-place solar UTC plus `-At lat,lon` in `scripts\preflight.ps1`. Done when `scripts\preflight.ps1 -Roads us-anchorage-ak,us-hilo-hi -Cars nb2001_lowered -Seconds 20` gives daylight contact sheets (I read them with Read) and `-At 47.10,-101.20` runs one place.
2. Pinned cache and a warm-up in `scripts\preflight.ps1`. Done when `-Roads random -Seed 20261114 -MapCache Saved\MapCache\pin` runs twice, the second run has helper requests = 0, and both runs give the same `place=` and matching sheets.
3. Pool fallback detection in `SimCruisePreflight.cpp`, and the seed in `report.md` and the index. Done when `-Roads pool` with the network off FAILs with `start-fell-back-to-bundled-place`, and the report shows the seed.

**Docs that are wrong or old.**
- `docs/team/ownership.md` rule 5 names `docs/team/test-places.md`. That file does not exist; the list is `data/test-places.json`.
- `docs/install.md` "Where it keeps things" puts the map cache in `%LOCALAPPDATA%\SimCruise\Saved`. The code uses `UserSettingsDir/SimCruise/map-cache` (`SimCruiseAtlasClient.cpp:112`). The requirements table says "up to about 1 GB of map cache", but the helper cap is 96 MiB (`atlas.mjs:51`).
- `docs/unreal-building.md:270` and `preflight.ps1:8` say "fixed midday time" and "12 runs" on the hero roads. The time is midday only in the east, and the doc does not list `us-*`, `random` or `pool`.
- Bugs in the new preflight diff: (a) `-RandomCount 0` makes `Get-Random -Count 0` throw (estimate: the parameter's ValidateRange starts at 1). Fix: clamp the count to at least 1. (b) `-Seed` is not in `report.md` or the index row (:176, :212), so a `pool` run cannot be repeated from its report. (c) The `-SetSeed` draw can differ between Windows PowerShell 5.1 and PowerShell 7 (estimate). Keep one host, or draw with your own seeded `System.Random`.

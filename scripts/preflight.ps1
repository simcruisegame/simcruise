<#
.SYNOPSIS
  The pre-flight check: the robot drives every hero road with every hero car, records a
  video of each drive and fails the build on what a player would see.

.DESCRIPTION
  For each road and car it starts the game (editor binaries, -game, Ultra graphics,
  1280x720, the one-editor lock of scripts\EditorLock.ps1, a fixed midday time) with
  -SimCruiseViewCheck -SimCruisePreflight. ASimCruisePreflight waits for the VIEWCHECK line,
  drives the road for -Seconds through the real input stack, records the view at 10 frames
  a second and writes one line:

    PREFLIGHT PASS|FAIL road=.. car=.. ... reasons=..

  and a preflight.json. This script encodes the frames to <road>-<car>.mp4 with ffmpeg, makes
  a 4 x 3 contact sheet <road>-<car>-sheet.png, and writes report.md with a table and links,
  all in Saved\Preflight\<date-time>\. A missing PREFLIGHT line, a timeout or an engine error
  fails the run. Each finished pre-flight adds one row (commit, result, report) to the
  PC-wide index %LOCALAPPDATA%\SimCruise\preflight-index.csv, which scripts\playtest.ps1 reads.

  Roads (scripts\preflight.ps1 -Roads ...):
    ocean-drive  Ocean Drive, Miami Beach (the bundled start, offline)
    dragon       US 129 Tail of the Dragon at Deals Gap, TN
    nebraska     Princeton Road gravel, Lancaster County, NE
    big-sur      CA-1 Cabrillo Highway, Big Sur, south of Bixby Bridge
    us-*         ordinary US places from data\test-places.json (city, suburb, farm, desert,
                 mountain town, interstate, coast, Alaska, Hawaii); the start snaps to the nearest road
    random       -RandomCount places drawn from data\test-places.json (seeded by -Seed; default
                 today's date, so one day's runs repeat)
    pool         a random road from data\start\start-pool.json (-SimCruiseRandomStart, seeded by -Seed)
  The world must work at a random US place, not only on the hero roads (docs\team\ownership.md).
  Cars: nb2001_aaron (Miata), mustang65 (Mustang), meridian_z (Corvette).
  The roads other than Ocean Drive need the map helper (tools\atlas.exe) and the network.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\preflight.ps1
  powershell -ExecutionPolicy Bypass -File scripts\preflight.ps1 -Roads ocean-drive -Cars nb2001_aaron -Seconds 20
#>
param(
    [string[]]$Roads = @('ocean-drive', 'dragon', 'nebraska', 'big-sur'),
    [string[]]$Cars = @('nb2001_aaron', 'mustang65', 'meridian_z'),
    [int]$Seconds = 60,
    [string]$Utc = '2026-11-12T17:00:00Z',
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 20,
    [int]$LockTimeoutMinutes = 120,
    [switch]$KeepFrames,
    # A keen driver on an empty road: no speed limit, 75 % of the car's skidpad grip, traffic off.
    [switch]$Keen,
    # The negative test: at speed the robot yanks the stick with the handbrake on. Every run must
    # FAIL with a spin; the script exits 0 when they do, and writes no row to the index.
    [switch]$ForceSpin,
    # The seed for 'random' and 'pool' (default: today's date as yyyyMMdd).
    [int]$Seed = -1,
    # How many places 'random' draws from data\test-places.json.
    [int]$RandomCount = 3,
    # More game arguments for every run, for A/B tests (for example "-DPCVars=simcruise.Reflex=1").
    [string]$ExtraArgs = ''
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }
$Ffmpeg = (Get-Command ffmpeg -ErrorAction SilentlyContinue).Source
if (-not $Ffmpeg) {
    $Found = Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter ffmpeg.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Found) { $Ffmpeg = $Found.FullName }
}
if (-not $Ffmpeg) { Write-Host 'No ffmpeg: install it once with "winget install Gyan.FFmpeg" (free), then run again.'; exit 2 }

# "powershell -File" hands "a,b" over as one string.
$Roads = @($Roads | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Cars = @($Cars | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

$Defs = @{
    'ocean-drive' = @('-SimCruiseBundledStart')
    'dragon'      = @('-SimCruiseStart=35.4792763,-83.9327265', '"-SimCruiseStartRoad=Calderwood Highway"')
    'nebraska'    = @('-SimCruiseStart=40.56724,-96.5909', '"-SimCruiseStartRoad=Princeton Road"')
    'big-sur'     = @('-SimCruiseStart=36.363197,-121.900041', '"-SimCruiseStartRoad=Cabrillo Highway"')
}
if ($Seed -lt 0) { $Seed = [int](Get-Date -Format 'yyyyMMdd') }
# Ordinary US places: the world must work everywhere, not only on the hero roads.
$PlacesFile = Join-Path $Root 'data\test-places.json'
$TestPlaces = @()
if (Test-Path $PlacesFile) { $TestPlaces = @((Get-Content $PlacesFile -Raw | ConvertFrom-Json)) }
# Each test place drives at its own local solar noon on the -Utc date (12 h - lon/15), so Anchorage
# and Hilo get daylight too; an explicit -Utc keeps one time for every run.
$PlaceUtc = @{}
$UtcGiven = $PSBoundParameters.ContainsKey('Utc')
$Day = ([datetime]::Parse($Utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AdjustToUniversal)).Date
foreach ($P in $TestPlaces) {
    $Defs[$P.key] = @("-SimCruiseStart=$($P.lat),$($P.lon)")
    $PlaceUtc[$P.key] = $Day.AddHours(12.0 - [double]$P.lon / 15.0).ToString('yyyy-MM-ddTHH:mm:ssZ')
}
$Defs['pool'] = @('-SimCruiseRandomStart', "-SimCruiseStartSeed=$Seed")
$Expanded = @()
foreach ($Road in $Roads) {
    if ($Road -eq 'random') {
        if (-not $TestPlaces.Count) { Write-Host "No places in $PlacesFile"; exit 2 }
        # A seeded System.Random partial shuffle: the same draw in Windows PowerShell 5.1 and PowerShell 7.
        $Rng = [System.Random]::new($Seed)
        $Keys = @($TestPlaces | ForEach-Object { $_.key })
        $Take = [Math]::Max(1, [Math]::Min($RandomCount, $Keys.Count))
        for ($i = 0; $i -lt $Take; $i++) { $j = $Rng.Next($i, $Keys.Count); $t = $Keys[$i]; $Keys[$i] = $Keys[$j]; $Keys[$j] = $t }
        $Draw = @($TestPlaces | Where-Object { $Keys[0..($Take - 1)] -contains $_.key })
        Write-Host "Random places (seed $Seed): $(($Draw | ForEach-Object { $_.key }) -join ', ')"
        $Expanded += @($Draw | ForEach-Object { $_.key })
    } else { $Expanded += $Road }
}
$Roads = $Expanded
$CarNames = @{ 'nb2001_aaron' = 'Miata'; 'mustang65' = 'Mustang'; 'meridian_z' = 'Corvette' }

# A game run needs no platform SDK check: skip the start-up call to Unreal Build Tool,
# which waits while another checkout builds.
$env:UE_SKIP_UBT_SDK_SETUP = '1'
$Commit = (& git -C $Root rev-parse --short HEAD).Trim()
if (& git -C $Root status --porcelain --untracked-files=no -- Source Config Content) { $Commit += '-dirty' }
$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$RunDir = Join-Path $Root "Saved\Preflight\$Stamp"
New-Item -ItemType Directory -Force $RunDir | Out-Null
Write-Host "Pre-flight of $Commit -> $RunDir"

$Rows = @()
foreach ($Road in $Roads) {
    if (-not $Defs.ContainsKey($Road)) { Write-Host "Unknown road $Road"; $Rows += [pscustomobject]@{ Road = $Road; Car = '-'; Result = 'FAIL'; Reasons = 'unknown road' }; continue }
    foreach ($Car in $Cars) {
        $Name = "$Road-$Car"
        $Out = Join-Path $RunDir $Name
        New-Item -ItemType Directory -Force $Out | Out-Null
        $Log = Join-Path $Out 'game.log'
        $Args = @("`"$Project`"", '-game', '-SimCruiseWorld', '-SimCruiseNoLive', '-SimCruiseNoNeural', '-SimCruiseGraphics=ultra', "-SimCruiseUtc=$(if (-not $UtcGiven -and $PlaceUtc.ContainsKey($Road)) { $PlaceUtc[$Road] } else { $Utc })",
            "-SimCruiseCar=$Car", '-SimCruiseViewCheck', '-SimCruisePreflight', "-SimCruisePreflightRoad=$Road", "-SimCruisePreflightSeconds=$Seconds",
            "`"-SimCruisePreflightOut=$Out`"", '-NoLiveCoding', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended', '-log', "`"-abslog=$Log`"") + $Defs[$Road] + @(if ($Keen) { '-SimCruisePreflightKeen' }) + @(if ($ForceSpin) { '-SimCruisePreflightForceSpin' })
        if ($ExtraArgs) { $Args += $ExtraArgs }
        if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "pre-flight $Name on $Root")) { exit 1 }
        $TimedOut = $false
        try {
            Write-Host "Pre-flight: $Name"
            $P = Start-Process -FilePath $Editor -ArgumentList ($Args -join ' ') -PassThru
            if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) {
                Stop-ProcessTree -ProcessId $P.Id
                $TimedOut = $true
            }
        }
        finally { Exit-EditorLock }

        $Line = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'PREFLIGHT (PASS|FAIL) .*' | Select-Object -Last 1 } else { $null }
        $ViewLine = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'VIEWCHECK (PASS|FAIL)' | Select-Object -Last 1 } else { $null }
        $Engine = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'Ensure condition failed|Fatal error|LogSimCruise\w*: Error:') } else { @() }
        $Json = Join-Path $Out 'preflight.json'
        $Stats = if (Test-Path $Json) { Get-Content $Json -Raw | ConvertFrom-Json } else { $null }
        $Reasons = @()
        if ($TimedOut) { $Reasons += "timed out after $TimeoutMinutes min" }
        if (-not $Line) { $Reasons += 'no PREFLIGHT line' }
        elseif ($Line.Matches[0].Groups[1].Value -ne 'PASS' -and $Stats) { $Reasons += $Stats.reasons }
        if ($Engine.Count) { $Reasons += "$($Engine.Count) engine errors" }
        $Result = if ($Reasons.Count) { 'FAIL' } else { 'PASS' }

        # The video and its contact sheet.
        $Frames = Join-Path $Out 'frames'
        $Video = Join-Path $RunDir "$Name.mp4"
        $Sheet = Join-Path $RunDir "$Name-sheet.png"
        $FrameCount = @(Get-ChildItem $Frames -Filter '*.jpg' -ErrorAction SilentlyContinue).Count
        if ($FrameCount -gt 0) {
            & $Ffmpeg -hide_banner -loglevel error -y -framerate 10 -i (Join-Path $Frames '%05d.jpg') -c:v libx264 -pix_fmt yuv420p -crf 23 $Video
            $Every = [Math]::Max(1, [Math]::Floor($FrameCount / 12))
            & $Ffmpeg -hide_banner -loglevel error -y -i (Join-Path $Frames '%05d.jpg') -vf "select='not(mod(n\,$Every))',scale=426:-1,tile=4x3" -frames:v 1 -fps_mode vfr $Sheet
            if (-not $KeepFrames -and (Test-Path $Video)) { Remove-Item -Recurse -Force $Frames }
        }
        else { $Reasons += 'no frames'; $Result = 'FAIL' }

        $Text = if ($Line) { $Line.Matches[0].Value } else { "PREFLIGHT FAIL road=$Road car=$Car reasons=$($Reasons -join ',')" }
        Write-Host "  $Text"
        if ($Result -eq 'FAIL' -and $Line -and $Line.Matches[0].Groups[1].Value -eq 'PASS') { Write-Host "  FAIL: $($Reasons -join '; ')" }
        $Rows += [pscustomobject]@{
            Road = $Road; Car = $Car; Result = $Result; Reasons = ($Reasons -join '; ')
            Stats = $Stats; View = if ($ViewLine) { $ViewLine.Matches[0].Groups[1].Value } else { '-' }
            Video = "$Name.mp4"; Sheet = "$Name-sheet.png"
        }
    }
}

# The report.
$Failed = @($Rows | Where-Object { $_.Result -ne 'PASS' })
$Verdict = if ($Failed.Count) { 'FAIL' } else { 'PASS' }
$Md = @("# Pre-flight $Stamp", '', "Commit ``$Commit``, $($Rows.Count) runs of $Seconds s, time $Utc (test places: their own solar noon), seed $Seed. Result: **$Verdict** ($($Rows.Count - $Failed.Count) passed, $($Failed.Count) failed).", '',
    '| Road | Car | Result | View | Distance (m) | Mean speed (m/s) | Off road (s) | Rear slip (deg) / backwards (s) | Past the edge (m / s) | Frame p95 (ms) / hitches | White sky (%) | Tile pop-in | Car pop-in | Flashes | Road steps (max cm) | Gaps | Jolts (at borders) | Stuck traffic | Video | Sheet | Reasons |',
    '|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
foreach ($R in $Rows) {
    $S = $R.Stats
    $Car = if ($CarNames.ContainsKey($R.Car)) { $CarNames[$R.Car] } else { $R.Car }
    if ($S) {
        $Md += "| $($R.Road) | $Car | $($R.Result) | $($R.View) | $([int]$S.distance_m) | $($S.mean_speed_mps) | $($S.offroad_seconds) | $($S.peak_rear_slip_deg) / $($S.backwards_seconds) | $($S.max_past_edge_m) / $($S.past_edge_seconds) | $($S.frame_p95_ms) / $($S.hitches_over_100ms) | $($S.sky_white_percent) | $($S.tile_popins) | $($S.car_popins) | $($S.flash_frames) | $($S.road_steps) ($($S.max_step_cm)) | $($S.road_gaps) | $($S.jolts) ($($S.border_jolts)) | $($S.stuck_traffic) | [video]($($R.Video)) | [sheet]($($R.Sheet)) | $($R.Reasons) |"
    } else {
        $Md += "| $($R.Road) | $Car | $($R.Result) | $($R.View) | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | - | $($R.Reasons) |"
    }
}
$Md += @('', 'Look at every contact sheet and watch the videos of failed runs before a play-test build goes out. Real game bugs go in docs/release/bugs.md with the run, the frame and a screenshot.')
$Md | Set-Content -Encoding utf8 (Join-Path $RunDir 'report.md')

if ($ForceSpin) {
    # The negative test passes when every run failed on a spin. It is not a pre-flight of the commit.
    $Missed = @($Rows | Where-Object { $_.Result -ne 'FAIL' -or $_.Reasons -notmatch 'spin-' })
    Write-Host "Report: $(Join-Path $RunDir 'report.md')"
    if ($Missed.Count) { Write-Host "FORCED-SPIN TEST FAILED: no spin FAIL in $(($Missed | ForEach-Object { "$($_.Road)-$($_.Car)" }) -join ', ')"; exit 1 }
    Write-Host 'FORCED-SPIN TEST PASSED: every run failed on the spin'
    exit 0
}

$IndexDir = Join-Path $env:LOCALAPPDATA 'SimCruise'
New-Item -ItemType Directory -Force $IndexDir | Out-Null
$Index = Join-Path $IndexDir 'preflight-index.csv'
$Header = 'date,commit,result,runs,roads,cars,report,reasons'
if (-not (Test-Path $Index)) { $Header | Set-Content -Encoding utf8 $Index }
else {
    # An index from before the reasons column gets it (old rows leave it empty).
    $Old = @(Get-Content $Index)
    if ($Old.Count -and $Old[0] -ne $Header) { $Old[0] = $Header; $Old | Set-Content -Encoding utf8 $Index }
}
# Why each failed run failed, in one quoted CSV field.
$Why = ($Failed | ForEach-Object { "$($_.Road)-$($_.Car): $($_.Reasons)" }) -join ' | '
"$Stamp,$Commit,$Verdict,$($Rows.Count),$($Roads -join ' '),$($Cars -join ' '),$(Join-Path $RunDir 'report.md'),`"$($Why -replace '"', '""')`"" | Add-Content -Encoding utf8 $Index

Write-Host "Report: $(Join-Path $RunDir 'report.md')"
if ($Failed.Count) { Write-Host "PRE-FLIGHT FAILED: $(($Failed | ForEach-Object { "$($_.Road)-$($_.Car)" }) -join ', ')"; exit 1 }
Write-Host 'PRE-FLIGHT PASSED'
exit 0

<#
.SYNOPSIS
  Launch the game for a test and keep what it really showed, for a review before anyone says "it works".

.DESCRIPTION
  Starts one game (editor binaries, -game, the game's top graphics preset by default, 1280x720,
  driver's seat, the one-editor lock) at a hero road or any start, with the view check and the
  pre-flight robot driving for -Seconds with -SimCruiseReview. The game quits itself; a game that
  hangs is stopped after -TimeoutMinutes, and a crash or a hang still gives a run folder and a
  FAIL. In Saved\Review\<yyyyMMdd-HHmm>-<commit>\<name>\ it keeps:
    review\NNN-<t>s-<event>.png + .json   full-size frames at the start, every 5 s and at each
                                          event, with the true speed, rpm, stick and wheel angles
    review\crops\                         steering wheel, cluster, mirrors and windscreen close-ups
    video.mp4, sheet.png                  the 10 fps video and a 4 x 3 contact sheet
    game.log, cmdline.txt, run.txt, gpu.txt, crash\ (on a crash), preflight.json, review.json
    review.md                             the fault list; the numbers fill some verdicts
  Then look at the frames and the crops, write a verdict (PASS, FAIL or N/A) for every TODO item
  and your name in review.md, and run scripts\review-check.ps1 on the folder.

.PARAMETER Road
  ocean-drive (default, offline), dragon, nebraska or big-sur. Or give -Start.
.PARAMETER Start
  lat,lon for any other place (with -StartRoad to pick the road).
.PARAMETER ExtraArgs
  More game arguments, for example '-SimCruisePreflightKeen' or '"-ExecCmds=simcruise.Mirrors 0"'.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\launch-review.ps1
  powershell -ExecutionPolicy Bypass -File scripts\launch-review.ps1 -Road dragon -Car mustang65 -Name dragon-steering
#>
param(
    # Show the game's window (default: off screen, -RenderOffScreen, nothing on the desktop).
    [switch]$ShowWindow,
    [string]$Road = 'ocean-drive',
    [string]$Start = '',
    [string]$StartRoad = '',
    [string]$Car = 'nb2001_aaron',
    [int]$Seconds = 60,
    [string]$Graphics = 'ultra',
    [string]$Utc = '2026-11-12T17:00:00Z',
    [string]$Name = '',
    [string[]]$ExtraArgs = @(),
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' }),
    [int]$TimeoutMinutes = 20,
    [int]$LockTimeoutMinutes = 120,
    [string]$OutRoot = '',
    # Leave out the cockpit steer test that runs first with the car at rest.
    [switch]$NoCockpit,
    # Random US places: N launches, one place type each (desert, mountains, plains, forest, coast,
    # suburb, small town, city), 45 s each, seeded (-Seed; 0 picks one and prints it).
    [int]$RandomUS = 0,
    [int]$Seed = 0,
    # The review sheet's kind (random runs add their own items).
    [string]$SheetKind = 'launch',
    # A test case in the same launch: 'standstill' (-SimCruiseStandstillTest) or 'speed' (-SimCruiseSpeedTest).
    # 'fallback': forced tile failures ahead of the start (-SimCruiseFailTiles). 'eye' and 'eyeoff': a live drive
    # (no -SimCruiseNoLive) with the satlas_landcover scan eye on, or off.
    [ValidateSet('', 'standstill', 'speed', 'fallback', 'eye', 'eyeoff')][string]$Case = '',
    # Tiles for -Case fallback (x_y, comma separated): the two north of Ocean Drive's start tile.
    [string]$FailTiles = '18180_27907,18181_27907'
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }
$Ffmpeg = Find-Ffmpeg

if ($RandomUS -gt 0) {
    # Random places: one launch each with this script, a real photo near each point where Commons
    # has one, and a summary table random-us.md.
    . (Join-Path $PSScriptRoot 'RandomPlaces.ps1')
    if ($Seed -eq 0) { $Seed = Get-Random -Minimum 1 -Maximum 999999 }
    $Places = Get-RandomUSPlaces -Count $RandomUS -Seed $Seed
    if (-not $OutRoot) { $OutRoot = (Get-ReviewRoot $Root) + "-random-$Seed" }
    New-Item -ItemType Directory -Force $OutRoot | Out-Null
    Write-Host "Random US review: $RandomUS places, seed $Seed -> $OutRoot"
    $Q = [string][char]96
    $Table = @("# Random US review, seed $Seed", '', "Repeat it: ${Q}scripts\launch-review.ps1 -RandomUS $RandomUS -Seed $Seed${Q}. Commit $(Get-ReviewCommit $Root).", '',
        '| # | Place type | Point (lat, lon) | Road | Car placed after (s) | Game result | Frame p95 (ms) | Photo | Run |', '|---|---|---|---|---|---|---|---|---|')
    foreach ($Pl in $Places) {
        $Name = "random-$($Pl.Index)-$($Pl.Type)"
        Write-Host "  $Name at $($Pl.Lat), $($Pl.Lon)"
        & powershell -ExecutionPolicy Bypass -File $PSCommandPath -Start "$($Pl.Lat),$($Pl.Lon)" -Car $Car -Seconds 45 -Graphics $Graphics -Utc $Utc -Name $Name `
            -OutRoot $OutRoot -NoCockpit -SheetKind 'random' -ShowWindow:$ShowWindow -TimeoutMinutes $TimeoutMinutes -LockTimeoutMinutes $LockTimeoutMinutes | Out-Null
        $Dir = Join-Path $OutRoot $Name
        $GameLog = Join-Path $Dir 'game.log'
        $Placed = if (Test-Path $GameLog) { Select-String -Path $GameLog -Pattern 'Car placed (on (.+?) )?at .*? after ([\d.]+) s' | Select-Object -First 1 } else { $null }
        $Road = if ($Placed -and $Placed.Matches[0].Groups[2].Value) { $Placed.Matches[0].Groups[2].Value } else { '-' }
        $Load = if ($Placed) { $Placed.Matches[0].Groups[3].Value } else { '-' }
        $PreFile = Join-Path $Dir 'preflight.json'
        $Pre = if (Test-Path $PreFile) { Get-Content $PreFile -Raw | ConvertFrom-Json } else { $null }
        $Photo = Save-CommonsPhoto -RunDir $Dir -Lat $Pl.Lat -Lon $Pl.Lon
        if ($Photo -and (Test-Path (Join-Path $Dir 'review.md'))) { Add-Content -Encoding utf8 (Join-Path $Dir 'review.md') @('', '## Real photo', '', "photo.jpg: $Photo") }
        $Result = if ($Pre) { $Pre.result + $(if ($Pre.reasons -ne 'ok') { " ($($Pre.reasons))" } else { '' }) } else { 'no result' }
        $P95 = if ($Pre) { $Pre.frame_p95_ms } else { '-' }
        $PhotoCell = if ($Photo) { 'photo.jpg' } else { 'none near' }
        $Table += "| $($Pl.Index) | $($Pl.Type) | $($Pl.Lat), $($Pl.Lon) | $Road | $Load | $Result | $P95 | $PhotoCell | [$Name]($Name/review.md) |"
        Write-Host "    road $Road, placed after $Load s, $Result"
    }
    $Table += @('', 'Look at each run''s review\ frames (and photo.jpg where there is one) and fill each review.md, then run scripts\review-check.ps1 on this folder.')
    $Table | Set-Content -Encoding utf8 (Join-Path $OutRoot 'random-us.md')
    Write-Host "Summary: $(Join-Path $OutRoot 'random-us.md') (seed $Seed)"
    exit 0
}

$Places = @{
    'ocean-drive' = @('-SimCruiseBundledStart')
    'dragon'      = @('-SimCruiseStart=35.4792763,-83.9327265', '"-SimCruiseStartRoad=Calderwood Highway"')
    'nebraska'    = @('-SimCruiseStart=40.56724,-96.5909', '"-SimCruiseStartRoad=Princeton Road"')
    'big-sur'     = @('-SimCruiseStart=36.363197,-121.900041', '"-SimCruiseStartRoad=Cabrillo Highway"')
}
if ($Start) {
    $PlaceArgs = @("-SimCruiseStart=$Start") + @(if ($StartRoad) { "`"-SimCruiseStartRoad=$StartRoad`"" })
    if (-not $Name) { $Name = "start-$($Start -replace '[^0-9.-]', '_')-$Car" }
} elseif ($Places.ContainsKey($Road)) {
    $PlaceArgs = $Places[$Road]
    if (-not $Name) { $Name = "$Road-$Car" }
} else { Write-Host "Unknown road $Road (ocean-drive, dragon, nebraska, big-sur) and no -Start"; exit 2 }

if (-not $OutRoot) { $OutRoot = Get-ReviewRoot $Root }
$RunDir = Join-Path $OutRoot $Name
New-Item -ItemType Directory -Force $RunDir | Out-Null
$Log = Join-Path $RunDir 'game.log'
# A game run needs no platform SDK check: skip the start-up call to Unreal Build Tool.
$env:UE_SKIP_UBT_SDK_SETUP = '1'
$GameArgs = @("`"$Project`"", '-game', '-SimCruiseWorld') + @(if ($Case -notin 'eye', 'eyeoff') { '-SimCruiseNoLive' }) + @('-SimCruiseNoNeural', "-SimCruiseGraphics=$Graphics", "-SimCruiseUtc=$Utc",
    "-SimCruiseCar=$Car", '-SimCruiseViewCheck', '-SimCruisePreflight', '-SimCruiseReview', "-SimCruisePreflightRoad=$Name", "-SimCruisePreflightSeconds=$Seconds",
    "`"-SimCruisePreflightOut=$RunDir`"", '-NoLiveCoding', '-nosplash', '-unattended', '-log', "`"-abslog=$Log`"") + @(Get-TestWindowArgs $ShowWindow) + @(Get-NativeResolution) + $PlaceArgs + $ExtraArgs + @(switch ($Case) { 'fallback' { "-SimCruiseFailTiles=$FailTiles" } 'eye' { '-SimCruiseScanEye=satlas_landcover' } 'standstill' { '-SimCruiseStandstillTest' } 'speed' { '-SimCruiseSpeedTest' } }) + @(if (-not $NoCockpit) { '-SimCruiseCockpitTest' })
$CommandLine = "`"$Editor`" " + ($GameArgs -join ' ')

if (-not (Enter-EditorLock -GpuTest -TimeoutMinutes $LockTimeoutMinutes -Why "launch review $Name on $Root")) { exit 1 }
$Started = Get-Date
$TimedOut = $false
$ExitCode = -1
try {
    Write-Host "Launch review: $Name -> $RunDir"
    $P = Start-Process -FilePath $Editor -ArgumentList ($GameArgs -join ' ') -PassThru
    if (-not $P.WaitForExit($TimeoutMinutes * 60 * 1000)) {
        Stop-ProcessTree -ProcessId $P.Id
        $TimedOut = $true
    } else { $ExitCode = $P.ExitCode }
}
finally { Exit-EditorLock }

$Evidence = Save-ReviewEvidence -RunDir $RunDir -Root $Root -Log $Log -CommandLine $CommandLine -Started $Started -TimedOut $TimedOut -ExitCode $ExitCode
$Line = if (Test-Path $Log) { Select-String -Path $Log -Pattern 'PREFLIGHT (PASS|FAIL) .*' | Select-Object -Last 1 } else { $null }
$Result = if ($Line) { $Line.Matches[0].Groups[1].Value } else { '' }
$Reasons = if ($Line -and $Line.Matches[0].Value -match 'reasons=(\S+)') { $Matches[1] } else { '' }
if ($Evidence.Crashed -or $Evidence.Hung -or -not $Result) { $Result = 'FAIL'; $Reasons = (@($Reasons) + @(if ($Evidence.Crashed) { 'crash' }) + @(if ($Evidence.Hung) { 'hang' }) + @(if (-not $Line) { 'no-PREFLIGHT-line' }) | Where-Object { $_ }) -join ',' }

# The video, the contact sheet and the close-ups.
$Frames = Join-Path $RunDir 'frames'
$FrameCount = @(Get-ChildItem $Frames -Filter '*.jpg' -ErrorAction SilentlyContinue).Count
if ($Ffmpeg -and $FrameCount -gt 0) {
    & $Ffmpeg -hide_banner -loglevel error -y -framerate 10 -i (Join-Path $Frames '%05d.jpg') -c:v libx264 -pix_fmt yuv420p -crf 23 (Join-Path $RunDir 'video.mp4')
    $Every = [Math]::Max(1, [Math]::Floor($FrameCount / 12))
    & $Ffmpeg -hide_banner -loglevel error -y -i (Join-Path $Frames '%05d.jpg') -vf "select='not(mod(n\,$Every))',scale=426:-1,tile=4x3" -frames:v 1 -fps_mode vfr (Join-Path $RunDir 'sheet.png')
    if (Test-Path (Join-Path $RunDir 'video.mp4')) { Remove-Item -Recurse -Force $Frames }
}
$Crops = New-ReviewCrops -RunDir $RunDir -Ffmpeg $Ffmpeg
Write-ReviewSheet -RunDir $RunDir -Kind $SheetKind -Title $Name -Result $Result -Reasons $Reasons -Evidence $Evidence

foreach ($L in (Select-String -Path $Log -Pattern 'VIEWCHECK (PASS|FAIL)|COCKPIT (PASS|FAIL)|PREFLIGHT (PASS|FAIL)|REVIEW (STEERING|GAUGES)' -ErrorAction SilentlyContinue)) { Write-Host "  $(($L.Line -replace '^\[.*?\]\[.*?\]\w+: Display: ', '').Substring(0, [Math]::Min(260, ($L.Line -replace '^\[.*?\]\[.*?\]\w+: Display: ', '').Length)))" }
$Shots = @(Get-ChildItem (Join-Path $RunDir 'review') -Filter '*.png' -ErrorAction SilentlyContinue).Count
Write-Host "Evidence: $Shots full-size frames, $Crops close-ups$(if ($Evidence.Crashed) { ', CRASH folder copied' })$(if ($Evidence.Hung) { ', HUNG and stopped' })"
Write-Host "REVIEW PENDING: look at $RunDir\review\ and review\crops\, write a verdict for every TODO in $RunDir\review.md, then:"
Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\review-check.ps1 `"$RunDir`""
if ($Result -ne 'PASS') { exit 1 }
exit 0

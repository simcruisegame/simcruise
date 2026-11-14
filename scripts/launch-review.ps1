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
    [switch]$NoCockpit
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Root = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Root 'SimCruise.uproject'
$Editor = Join-Path $EngineDir 'Engine\Binaries\Win64\UnrealEditor.exe'
if (-not (Test-Path $Editor)) { Write-Host "No Unreal Editor at $Editor (set -EngineDir or UE_ROOT)."; exit 2 }
$Ffmpeg = Find-Ffmpeg

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
$GameArgs = @("`"$Project`"", '-game', '-SimCruiseWorld', '-SimCruiseNoLive', '-SimCruiseNoNeural', "-SimCruiseGraphics=$Graphics", "-SimCruiseUtc=$Utc",
    "-SimCruiseCar=$Car", '-SimCruiseViewCheck', '-SimCruisePreflight', '-SimCruiseReview', "-SimCruisePreflightRoad=$Name", "-SimCruisePreflightSeconds=$Seconds",
    "`"-SimCruisePreflightOut=$RunDir`"", '-NoLiveCoding', '-windowed', '-ResX=1280', '-ResY=720', '-nosplash', '-unattended', '-log', "`"-abslog=$Log`"") + $PlaceArgs + $ExtraArgs + @(if (-not $NoCockpit) { '-SimCruiseCockpitTest' })
$CommandLine = "`"$Editor`" " + ($GameArgs -join ' ')

if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "launch review $Name on $Root")) { exit 1 }
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
Write-ReviewSheet -RunDir $RunDir -Kind 'launch' -Title $Name -Result $Result -Reasons $Reasons -Evidence $Evidence

foreach ($L in (Select-String -Path $Log -Pattern 'VIEWCHECK (PASS|FAIL)|COCKPIT (PASS|FAIL)|PREFLIGHT (PASS|FAIL)|REVIEW (STEERING|GAUGES)' -ErrorAction SilentlyContinue)) { Write-Host "  $(($L.Line -replace '^\[.*?\]\[.*?\]\w+: Display: ', '').Substring(0, [Math]::Min(260, ($L.Line -replace '^\[.*?\]\[.*?\]\w+: Display: ', '').Length)))" }
$Shots = @(Get-ChildItem (Join-Path $RunDir 'review') -Filter '*.png' -ErrorAction SilentlyContinue).Count
Write-Host "Evidence: $Shots full-size frames, $Crops close-ups$(if ($Evidence.Crashed) { ', CRASH folder copied' })$(if ($Evidence.Hung) { ', HUNG and stopped' })"
Write-Host "REVIEW PENDING: look at $RunDir\review\ and review\crops\, write a verdict for every TODO in $RunDir\review.md, then:"
Write-Host "  powershell -ExecutionPolicy Bypass -File scripts\review-check.ps1 `"$RunDir`""
if ($Result -ne 'PASS') { exit 1 }
exit 0

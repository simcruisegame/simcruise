<#
.SYNOPSIS
  The one full test of a release candidate: every place once, everything every team needs, in
  one run, off screen, at the game's top graphics preset and the screen's own resolution.

.DESCRIPTION
  The release train: while they build, the lanes run only scripts\check.ps1 -Tier fast. At the
  cut-off all lanes merge into one candidate, and this script tests that candidate once for all
  the teams (docs\team\release-test.md). One game per place (scripts\launch-review.ps1), one at a
  time on the PC (the GPU test lock), each collecting in the same launch:
    - the view check (what the world shows),
    - the cockpit steer test at rest (wheel axis, hub, needles with the wheel hidden, lock to lock),
    - the robot's drive (pre-flight judge: spin, run-off, slow frames, road steps and gaps, pop-in,
      stuck traffic, white sky), -Seconds long, with frames every 5 s and at each event, close-ups
      of the wheel, cluster, mirrors and windscreen, the 10 fps video, the log, crash data, and the
      real graphics settings read back from the engine.
  Places: the four hero roads, the 1999 Miata, the Mustang and the Corvette turning round them so
  each car drives each hero road once in three runs (the turn is set by -Seed), and -RandomCount
  ordinary US places from data\test-places.json (the same seeded draw as preflight.ps1 -Roads
  random), one car each. Then the controller menu test (scripts\pad-review.ps1) when it is there,
  else NOT RUN.

  Output: Saved\ReleaseTest\<stamp>-<commit>\summary.md (the verdict and a table per place), one
  report per team in teams\<team>.md (driving, cockpit, picture, world, ui, release: Verdict,
  Findings, Frames to look at, Numbers vs limits), and each launch's folder with its review.md for
  the frame review. A launch whose graphics were not the top preset at the screen's resolution is
  NOT VALID FOR SIGN-OFF.

.PARAMETER MaxPlaces
  Stop after this many places (a dry run of the script itself; 0: all).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1
  powershell -ExecutionPolicy Bypass -File scripts\release-test.ps1 -MaxPlaces 2
#>
param(
    [int]$Seed = -1,
    [int]$RandomCount = 4,
    [int]$Seconds = 60,
    [int]$MaxPlaces = 0,
    [string]$Utc = '2026-11-12T17:00:00Z',
    # Show the games' windows (default: off screen).
    [switch]$ShowWindow,
    [int]$TimeoutMinutes = 20,
    [int]$LockTimeoutMinutes = 180,
    # The scan eye's dev model (satlas_landcover), read in place; it is not in the repo.
    [string]$EyeModelDir = 'D:\sc\lane-detail\Saved\AI',
    # Run only these test cases (names from the case list), no places: a dry run of the hooks.
    [string[]]$OnlyCases = @(),
    # A re-test: only the places whose name contains one of these words (welch, stonington, ...).
    [string[]]$Places = @(),
    # A re-test: only these cases (case kinds or names: controller, fallback, eye, cooked-drive, ...).
    # With -Places or -Cases and no -Cases, no case runs; with -Cases and no -Places, no place runs.
    [string[]]$Cases = @(),
    # Run the cooked package (scripts\package.ps1 -Configuration Development: shipped shaders and
    # a log; the Shipping exe writes no log) in place of the editor, and the cooked-drive case.
    [switch]$Cooked,
    [string]$CookedExe = '',
    # Editor runs: one short untimed launch per place first, so the shader cache is warm before the
    # timed drive (a fresh worktree compiles shaders during its first drive). -NoWarm skips it.
    [switch]$NoWarm,
    # Words that must not show to the player (the owner asked that their name is not in the game).
    [string[]]$ForbiddenNames = @('Aaron')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
. (Join-Path $PSScriptRoot 'RandomPlaces.ps1')
. (Join-Path $PSScriptRoot 'ReleaseReport.ps1')
. (Join-Path $PSScriptRoot 'LoadWatch.ps1')
. (Join-Path $PSScriptRoot 'ViewerWatch.ps1')
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot
if ($Seed -lt 0) { $Seed = [int](Get-Date -Format 'yyyyMMdd') }
$Commit = Get-ReviewCommit $Root
$Stamp = Get-Date -Format 'yyyyMMdd-HHmm'
$RunDir = Join-Path $Root "Saved\ReleaseTest\$Stamp-$Commit"
New-Item -ItemType Directory -Force (Join-Path $RunDir 'teams') | Out-Null
$Started = Get-Date

# The places: the hero roads with the cars turning round them, then the seeded ordinary places.
$Cars = @('nb2001_aaron', 'mustang65', 'meridian_z')
$Heroes = @('ocean-drive', 'dragon', 'nebraska', 'big-sur')
$Plan = @()
for ($I = 0; $I -lt $Heroes.Count; $I++) {
    $Plan += [pscustomobject]@{ Name = $Heroes[$I]; Car = $Cars[($I + $Seed) % $Cars.Count]; Road = $Heroes[$I]; Start = ''; StartRoad = ''; Utc = $Utc; Kind = 'launch' }
}
$Draw = @(Get-TestPlaceDraw -Root $Root -Seed $Seed -Count $RandomCount -Utc $Utc)
for ($I = 0; $I -lt $Draw.Count; $I++) {
    $P = $Draw[$I]
    $Plan += [pscustomobject]@{ Name = $P.key; Car = $Cars[($I + $Seed + 1) % $Cars.Count]; Road = ''; Start = "$($P.lat),$($P.lon)"; StartRoad = ''; Utc = $P.Utc; Kind = 'random' }
}
if ($MaxPlaces -gt 0) { $Plan = @($Plan | Select-Object -First $MaxPlaces) }
# "powershell -File" hands "a,b" over as one string.
$OnlyCases = @($OnlyCases | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
if ($OnlyCases.Count) { $Plan = @() }
$Places = @($Places | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Cases = @($Cases | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$Retest = ($Places.Count + $Cases.Count) -gt 0
if ($Retest) { $Plan = @($Plan | Where-Object { $N = $_.Name; @($Places | Where-Object { $N -like "*$_*" }).Count }) }
if (-not $CookedExe) { $CookedExe = Join-Path $Root 'Saved\Package\Windows\SimCruise\Binaries\Win64\SimCruise.exe' }
if ($Cooked -and -not (Test-Path $CookedExe)) { Write-Host "No cooked game at ${CookedExe}: run scripts\package.ps1 -Configuration Development first."; exit 2 }
$GameExeArgs = if ($Cooked) { @('-GameExe', $CookedExe) } else { @() }
# A case runs when no selection was made, or when -Cases (or -OnlyCases) names its kind or its name.
function Test-CaseSelected($C) {
    if ($OnlyCases.Count) { return $OnlyCases -contains $C.Name }
    if ($Retest) { return [bool](@($Selection | Where-Object { $_ -eq $C.Case -or $_ -eq $C.Name -or ($_ -eq 'eye' -and $C.Case -eq 'eyeoff') }).Count) }
    return $true
}
# About 2 min to start and build the place, 1 min for the view and cockpit tests, the drive, and
# 1 min to stop and make the video: a time to plan by, printed before the run.
$Estimate = [int]($Plan.Count * (4.0 + $Seconds / 60.0) + 2 + $(if ($MaxPlaces -eq 0) { 3 * 4 + 1.5 + 3 + 3 } else { 0 }))
Write-Host "Release test of $Commit, seed ${Seed}$(if ($Cooked) { ', COOKED package' })$(if ($Retest) { ", re-test: places $($Places -join ',') cases $($Cases -join ',')" }): $($Plan.Count) places, about $Estimate min -> $RunDir"
foreach ($P in $Plan) { Write-Host "  $($P.Name) with $($P.Car)" }

# A quiet window: other lanes' games and GPU work add to the frame times (about 2 ms p95).
$Before = Get-LoadSample $RunDir
if ($Before.OtherProcesses -or $Before.OtherGpu -gt 5) { Write-Host "WARNING: the PC is not quiet: $($Before.OtherProcesses) (other GPU use $($Before.OtherGpu) %). Frame times will be NOT QUIET." -ForegroundColor Yellow }
Set-GpuBusy "release test of $Commit$(if ($Retest) { ' (re-test)' })" ($Estimate + 30)
try {
$LoadJob = Start-LoadWatch $RunDir
$ViewerJob = if ($ShowWindow) { Start-ViewerWatch $RunDir } else { $null }
$Rows = @()
# Warm the shader cache: a short untimed launch per place (editor runs only; a package has its shaders).
if (-not $Cooked -and -not $NoWarm -and $Plan.Count) {
    $WarmDir = Join-Path $RunDir 'warm'
    foreach ($P in $Plan) {
        $Args = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'launch-review.ps1'), '-Car', $P.Car, '-Seconds', 15, '-Utc', $P.Utc,
            '-Name', $P.Name, '-OutRoot', $WarmDir, '-NoCockpit', '-TimeoutMinutes', $TimeoutMinutes, '-LockTimeoutMinutes', $LockTimeoutMinutes)
        if ($P.Road) { $Args += @('-Road', $P.Road) } else { $Args += @('-Start', $P.Start) }
        $W0 = Get-Date
        & powershell @Args | Out-Null
        Write-Host ("  warm-up {0}: {1} min" -f $P.Name, [Math]::Round(((Get-Date) - $W0).TotalMinutes, 1))
    }
}
foreach ($P in $Plan) {
    $Args = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'launch-review.ps1'), '-Car', $P.Car, '-Seconds', $Seconds, '-Utc', $P.Utc,
        '-Name', $P.Name, '-OutRoot', $RunDir, '-SheetKind', $P.Kind, '-TimeoutMinutes', $TimeoutMinutes, '-LockTimeoutMinutes', $LockTimeoutMinutes) + $GameExeArgs
    if ($P.Road) { $Args += @('-Road', $P.Road) } else { $Args += @('-Start', $P.Start) }
    if ($ShowWindow) { $Args += '-ShowWindow' }
    $T0 = Get-Date
    & powershell @Args | Out-Null
    $Dir = Join-Path $RunDir $P.Name
    $Rows += [pscustomobject]@{ Plan = $P; Dir = $Dir; Minutes = [Math]::Round(((Get-Date) - $T0).TotalMinutes, 1); FromUtc = $T0.ToUniversalTime(); ToUtc = (Get-Date).ToUniversalTime() }
    Write-Host ("  {0} done in {1} min" -f $P.Name, $Rows[-1].Minutes)
}

# The test cases that need their own launch. A case whose game flag is not in this build is NOT RUN
# (with the reason); it does not stop the run.
function Test-GameFlag([string]$Flag) {
    return [bool](Get-ChildItem (Join-Path $Root 'Source') -Recurse -Include *.cpp | Select-String -Pattern "TEXT(`"$Flag" -SimpleMatch -List | Select-Object -First 1)
}
$CaseDefs = @(
    [pscustomobject]@{ Case = 'standstill'; Name = 'standstill-ocean-drive'; Road = 'ocean-drive'; Flag = 'SimCruiseStandstillTest'; Seconds = 90 },
    [pscustomobject]@{ Case = 'speed'; Name = 'speed-nebraska'; Road = 'nebraska'; Flag = 'SimCruiseSpeedTest'; Seconds = 180 },
    [pscustomobject]@{ Case = 'speed'; Name = 'speed-dragon'; Road = 'dragon'; Flag = 'SimCruiseSpeedTest'; Seconds = 180 },
    [pscustomobject]@{ Case = 'fallback'; Name = 'fallback-ocean-drive'; Road = 'ocean-drive'; Flag = 'SimCruiseFailTiles='; Seconds = 60 },
    [pscustomobject]@{ Case = 'eyeoff'; Name = 'eye-off-ocean-drive'; Road = 'ocean-drive'; Flag = 'SimCruiseScanEye='; Seconds = 60 },
    [pscustomobject]@{ Case = 'eye'; Name = 'eye-on-ocean-drive'; Road = 'ocean-drive'; Flag = 'SimCruiseScanEye='; Seconds = 60 },
    # The cooked package from the drive start: no frame or freeze over 0.5 s in the first 60 s, with an Insights trace.
    [pscustomobject]@{ Case = 'cooked-drive'; Name = 'cooked-dragon'; Road = 'dragon'; Flag = 'SimCruisePreflight'; Seconds = 60 },
    [pscustomobject]@{ Case = 'cooked-drive'; Name = 'cooked-ocean-drive'; Road = 'ocean-drive'; Flag = 'SimCruisePreflight'; Seconds = 60 }
)
$Selection = $Cases
$Cases = @()
$EyeModel = Join-Path $Root 'Saved\AI\scan-eye-satlas.onnx'
if (-not (Test-Path $EyeModel) -and (Test-Path (Join-Path $EyeModelDir 'scan-eye-satlas.onnx')) -and -not (Test-Path (Join-Path $Root 'Saved\AI'))) {
    New-Item -ItemType Junction -Path (Join-Path $Root 'Saved\AI') -Target $EyeModelDir | Out-Null
}
foreach ($C in $CaseDefs) {
    $Dir = Join-Path $RunDir $C.Name
    if (-not (Test-CaseSelected $C)) { $Cases += [pscustomobject]@{ Def = $C; Dir = $Dir; Ran = $false; Reason = 'not selected' }; continue }
    if ($C.Case -eq 'cooked-drive' -and -not $Cooked) { $Cases += [pscustomobject]@{ Def = $C; Dir = $Dir; Ran = $false; Reason = 'needs -Cooked (the packaged build)' }; continue }
    if ($MaxPlaces -gt 0 -and -not $OnlyCases.Count -and -not $Retest) { $Cases += [pscustomobject]@{ Def = $C; Dir = $Dir; Ran = $false; Reason = 'dry run (-MaxPlaces)' }; continue }
    if ($C.Case -in 'eye', 'eyeoff' -and -not (Test-Path $EyeModel)) { $Cases += [pscustomobject]@{ Def = $C; Dir = $Dir; Ran = $false; Reason = "no scan eye model at $EyeModel (-EyeModelDir)" }; continue }
    if (-not (Test-GameFlag $C.Flag)) { $Cases += [pscustomobject]@{ Def = $C; Dir = $Dir; Ran = $false; Reason = "the game flag -$($C.Flag) is not in this build" }; continue }
    $Args = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'launch-review.ps1'), '-Car', $Cars[0], '-Seconds', $C.Seconds, '-Utc', $Utc, '-Road', $C.Road,
        '-Name', $C.Name, '-OutRoot', $RunDir, '-NoCockpit', '-TimeoutMinutes', $TimeoutMinutes, '-LockTimeoutMinutes', $LockTimeoutMinutes) + $GameExeArgs
    if ($C.Case -eq 'cooked-drive') { $Args += '-Trace' } else { $Args += @('-Case', $C.Case) }
    if ($ShowWindow) { $Args += '-ShowWindow' }
    $T0 = Get-Date
    & powershell @Args | Out-Null
    if ($C.Case -eq 'standstill') {
        # Detail at 0, 5, 15 and 60 s after the stop; the reference photo beside the last frame.
        $Steps = @('t000', 't005', 't015', 't060' | ForEach-Object { Join-Path $Dir "standstill\$_.png" } | Where-Object { Test-Path $_ })
        if ($Steps.Count) { & python (Join-Path $PSScriptRoot 'detail-metrics.py') (Join-Path $Dir 'standstill\detail.json') @Steps | Out-Null }
        $Ref = @(Get-ChildItem (Join-Path $Root "data\reference\$($C.Road)") -Include *.jpg, *.png -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1)
        $Ff = Find-Ffmpeg
        if ($Ref.Count -and $Ff -and $Steps.Count) {
            & $Ff -y -loglevel error -i $Steps[-1] -i $Ref[0].FullName -filter_complex '[0]scale=-2:720[a];[1]scale=-2:720[b];[a][b]hstack' (Join-Path $Dir 'standstill\side-by-side.png')
        }
    }
    $Cases += [pscustomobject]@{ Def = $C; Dir = $Dir; Ran = $true; Reason = ''; Minutes = [Math]::Round(((Get-Date) - $T0).TotalMinutes, 1) }
    Write-Host ("  {0} done in {1} min" -f $C.Name, $Cases[-1].Minutes)
}

# The controller menu test: its own short launch when the script is there.
$Pad = Join-Path $PSScriptRoot 'pad-review.ps1'
$PadResult = 'NOT RUN'
$PadDir = Join-Path $RunDir 'pad-review'
if ((Test-Path $Pad) -and $MaxPlaces -eq 0 -and -not $OnlyCases.Count -and (-not $Retest -or $Selection -contains 'controller')) {
    & powershell -ExecutionPolicy Bypass -File $Pad -OutDir $PadDir $(if ($ShowWindow) { '-ShowWindow' }) | Out-Null
    $PadResult = if ($LASTEXITCODE -eq 0) { 'PASS' } else { 'FAIL' }
}

# The cooked package's own text files: the owner's name must not be in a user-visible string (a
# name, title, label or description); ids and file names are listed, allowed for now.
$Extra = @()
if ($Cooked) {
    $PackageRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $CookedExe)))
    $Hits = @(Get-ChildItem $PackageRoot -Recurse -File -Include *.json, *.txt, *.ini, *.csv, *.md, *.html, *.xml -ErrorAction SilentlyContinue |
        Select-String -Pattern ($ForbiddenNames -join '|') -AllMatches -ErrorAction SilentlyContinue)
    $Lines = @("# Owner name in the cooked package ($PackageRoot)", '', "Words: $($ForbiddenNames -join ', '). $($Hits.Count) lines.", '', '| Kind | File | Line | Text |', '|---|---|---|---|')
    foreach ($H in $Hits) {
        $Visible = $H.Line -match '"(name|display_name|title|label|description|nickname)"\s*:'
        $Lines += "| $(if ($Visible) { 'VISIBLE' } else { 'id or path' }) | $($H.Path.Substring($PackageRoot.Length)) | $($H.LineNumber) | $(($H.Line.Trim() -replace '\|', '/').Substring(0, [Math]::Min(120, $H.Line.Trim().Length))) |"
        if ($Visible) { $Extra += [pscustomobject]@{ Team = 'ui'; Sev = 'P1'; Place = 'package'; Car = '-'; Time = '-'; Evidence = 'package-names.md'; What = "the owner's name in a visible string: $($H.Path.Substring($PackageRoot.Length)):$($H.LineNumber)" } }
    }
    $Lines | Set-Content -Encoding utf8 (Join-Path $RunDir 'package-names.md')
}$Load = Stop-LoadWatch $LoadJob $RunDir
Stop-ViewerWatch $ViewerJob
Write-Host "Load: $(if ($Load.Quiet) { 'quiet' } else { "NOT QUIET: $($Load.OtherProcesses), other GPU mean $($Load.MeanOtherGpu) % (max $($Load.MaxOtherGpu) %)" })"
Write-ReleaseReports -RunDir $RunDir -Rows $Rows -Commit $Commit -Seed $Seed -PadResult $PadResult -PadDir $PadDir -Cases $Cases -Load $Load -ExtraFindings $Extra -Minutes ([int]((Get-Date) - $Started).TotalMinutes)
}
finally { Clear-GpuBusy }
Write-Host "Summary: $(Join-Path $RunDir 'summary.md')"
Write-Host "Team reports: $(Join-Path $RunDir 'teams')"
Write-Host "Run time: $([int]((Get-Date) - $Started).TotalMinutes) min (estimate $Estimate)"
Write-Host "Frame review: look at each place's review\ frames and fill its review.md, then scripts\review-check.ps1 `"$RunDir`""
exit 0

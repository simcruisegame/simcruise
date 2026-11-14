<#
.SYNOPSIS
  Writes summary.md and teams\*.md again for a finished release test folder.

.DESCRIPTION
  For a run whose reports must be made again (a report fault fixed, or reruns added). Each
  place's time window comes from its game.log (first and last log time, UTC), so viewer.csv and
  load.csv are judged per place. The test cases are the folders named like the case list; the
  controller result comes from pad-review\report.md.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\rebuild-release-report.ps1 D:\sc\lane-rc\Saved\ReleaseTest\20261114-0725-53b290c
#>
param([Parameter(Mandatory = $true)][string]$RunDir, [int]$Seed = 0, [int]$Minutes = 0, [string]$Note = '')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleaseReport.ps1')
. (Join-Path $PSScriptRoot 'LoadWatch.ps1')
$Name = Split-Path -Leaf $RunDir
$Commit = ($Name -split '-')[-1]
function Get-LogWindow([string]$Log) {
    $Times = @(Select-String -Path $Log -Pattern '^\[(\d{4})\.(\d\d)\.(\d\d)-(\d\d)\.(\d\d)\.(\d\d)' | ForEach-Object {
        $G = $_.Matches[0].Groups
        [datetime]::new([int]$G[1].Value, [int]$G[2].Value, [int]$G[3].Value, [int]$G[4].Value, [int]$G[5].Value, [int]$G[6].Value, [DateTimeKind]::Utc) })
    if (-not $Times.Count) { return $null }
    return @($Times[0], $Times[-1])
}
$CaseKinds = [ordered]@{ 'standstill-ocean-drive' = 'standstill'; 'speed-nebraska' = 'speed'; 'speed-dragon' = 'speed'; 'fallback-ocean-drive' = 'fallback'; 'eye-off-ocean-drive' = 'eyeoff'; 'eye-on-ocean-drive' = 'eye' }
$Rows = @(); $Cases = @()
foreach ($D in (Get-ChildItem $RunDir -Directory | Where-Object { $_.Name -notin 'teams', 'pad-review' -and $_.Name -notlike '*-rerun' } | Sort-Object CreationTime)) {
    $Log = Join-Path $D.FullName 'game.log'
    if ($CaseKinds.Contains($D.Name)) {
        $Cases += [pscustomobject]@{ Def = [pscustomobject]@{ Case = $CaseKinds[$D.Name]; Name = $D.Name }; Dir = $D.FullName; Ran = (Test-Path $Log); Reason = ''; Minutes = 0 }
        continue
    }
    if (-not (Test-Path $Log)) { continue }
    $W = Get-LogWindow $Log
    $Pre = if (Test-Path (Join-Path $D.FullName 'preflight.json')) { Get-Content (Join-Path $D.FullName 'preflight.json') -Raw | ConvertFrom-Json } else { $null }
    $Rows += [pscustomobject]@{ Plan = [pscustomobject]@{ Name = $D.Name; Car = $(if ($Pre) { $Pre.car } else { '-' }) }; Dir = $D.FullName
        Minutes = $(if ($W) { [Math]::Round(($W[1] - $W[0]).TotalMinutes, 1) } else { 0 }); FromUtc = $(if ($W) { $W[0] } else { $null }); ToUtc = $(if ($W) { $W[1] } else { $null }) }
}
foreach ($K in $CaseKinds.Keys) { if (-not ($Cases | Where-Object { $_.Def.Name -eq $K })) { $Cases += [pscustomobject]@{ Def = [pscustomobject]@{ Case = $CaseKinds[$K]; Name = $K }; Dir = (Join-Path $RunDir $K); Ran = $false; Reason = 'not run (no folder)' } } }
$PadReport = Join-Path $RunDir 'pad-review\report.md'
$PadResult = if (Test-Path $PadReport) { if ((Get-Content $PadReport -Raw) -match 'Verdict: \*\*PASS') { 'PASS' } else { 'FAIL' } } else { 'NOT RUN' }
$Load = if (Test-Path (Join-Path $RunDir 'load.csv')) { Stop-LoadWatch $null $RunDir } else { $null }
Write-ReleaseReports -RunDir $RunDir -Rows $Rows -Commit $Commit -Seed $Seed -PadResult $PadResult -PadDir (Join-Path $RunDir 'pad-review') -Cases $Cases -Load $Load -Minutes $Minutes
# Off-screen reruns of DISTURBED places (<place>-rerun folders): their frame times.
$Reruns = @(Get-ChildItem $RunDir -Directory -Filter '*-rerun' -ErrorAction SilentlyContinue | Sort-Object Name)
if ($Reruns.Count) {
    $Sec = @('', '## Rerun off screen (the DISTURBED places)', '', 'Each DISTURBED place ran again off screen, frame times only, with the same settings (Ultra 75 %, 1920x1080, dynamic resolution off, no cockpit test). Limit: frame p95 50 ms; a window costs about 1 ms of p95 against off screen.', '',
        '| Place | Car | Frame p95 in the run (ms) | Frame p95 rerun off screen (ms) | Frames over 50 ms / hitches | Drive judge (rerun) |', '|---|---|---|---|---|---|')
    foreach ($Rr in $Reruns) {
        $Place = $Rr.Name -replace '-rerun$', ''
        $O = if (Test-Path (Join-Path $RunDir "$Place\preflight.json")) { Get-Content (Join-Path $RunDir "$Place\preflight.json") -Raw | ConvertFrom-Json } else { $null }
        $N = if (Test-Path (Join-Path $Rr.FullName 'preflight.json')) { Get-Content (Join-Path $Rr.FullName 'preflight.json') -Raw | ConvertFrom-Json } else { $null }
        $Sec += "| $Place | $(if ($N) { $N.car } else { '-' }) | $(if ($O) { $O.frame_p95_ms } else { '-' }) (DISTURBED) | $(if ($N) { $N.frame_p95_ms } else { 'no result' }) | $(if ($N) { "$($N.frames_over_50ms) / $($N.hitches_over_100ms)" } else { '-' }) | $(if ($N) { "$($N.result) $($N.reasons)" } else { '-' }) |"
    }
    Add-Content -Encoding utf8 (Join-Path $RunDir 'summary.md') ($Sec -join "`r`n")
    Add-Content -Encoding utf8 (Join-Path $RunDir 'teams\picture.md') ($Sec -join "`r`n")
}
if ($Note) { Add-Content -Encoding utf8 (Join-Path $RunDir 'summary.md') ("`r`n## Notes`r`n`r`n" + $Note) }
Write-Host "Reports written: $(Join-Path $RunDir 'summary.md')"

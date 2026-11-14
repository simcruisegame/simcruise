<#
.SYNOPSIS
  Proves that a focus change does not change a visible test run's frame pacing.

.DESCRIPTION
  Two visible launches (scripts\launch-review.ps1 -ShowWindow: a borderless window at the
  screen's size) of Ocean Drive with the same car: a baseline, then the same drive with a focus
  change. In the second, at drive second 20 the script gives the focus to Notepad and at drive
  second 30 it gives it back to the game (the drive time comes from the review frames' names,
  every 5 s). The report compares the two runs' frame p50, p95, frames over 50 ms and hitches,
  and lists the hitch events with their drive time (review.json). viewer.csv logs the focus.
  PASS when p50 and p95 differ by no more than 1 ms and the focus run has no hitch between drive
  seconds 18 and 35 that the baseline did not have.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\focus-check.ps1
#>
param(
    [string]$OutRoot = '',
    [string]$Car = 'nb2001_aaron',
    [int]$Seconds = 60
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
Set-GpuBusy 'focus-check (timed game launches)' 40
try {
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
. (Join-Path $PSScriptRoot 'ViewerWatch.ps1')
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutRoot) { $OutRoot = (Get-ReviewRoot $Root) + '-focus-check' }
New-Item -ItemType Directory -Force $OutRoot | Out-Null
$Launch = Join-Path $PSScriptRoot 'launch-review.ps1'
$Shell = New-Object -ComObject WScript.Shell

function Get-DriveSecond([string]$Dir) {
    $F = Get-ChildItem (Join-Path $Dir 'review') -Filter '*s-*.png' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -Last 1
    if ($F -and $F.Name -match '^\d+-([\d.]+)s-') { return [double]$Matches[1] }
    return -1
}
function Read-Run([string]$Dir) {
    $Pre = if (Test-Path (Join-Path $Dir 'preflight.json')) { Get-Content (Join-Path $Dir 'preflight.json') -Raw | ConvertFrom-Json } else { $null }
    $Frames = if (Test-Path (Join-Path $Dir 'game.log')) { Select-String -Path (Join-Path $Dir 'game.log') -Pattern 'PREFLIGHT FRAMES .*p50=([\d.]+)ms' | Select-Object -Last 1 } else { $null }
    $Rev = if (Test-Path (Join-Path $Dir 'review.json')) { Get-Content (Join-Path $Dir 'review.json') -Raw | ConvertFrom-Json } else { $null }
    $Hitches = @(if ($Rev) { $Rev.events | Where-Object { $_ -match 'hitch' } })
    return [pscustomobject]@{ P50 = $(if ($Frames) { [double]$Frames.Matches[0].Groups[1].Value } else { -1 }); P95 = $(if ($Pre) { [double]$Pre.frame_p95_ms } else { -1 })
        Over50 = $(if ($Pre) { [int]$Pre.frames_over_50ms } else { -1 }); Hitches = $(if ($Pre) { [int]$Pre.hitches_over_100ms } else { -1 }); Events = $Hitches }
}

# 1. Baseline.
$BaseDir = Join-Path $OutRoot 'baseline'
& powershell -ExecutionPolicy Bypass -File $Launch -Road ocean-drive -Car $Car -Seconds $Seconds -Name 'baseline' -OutRoot $OutRoot -NoCockpit -ShowWindow | Out-Null

# 2. The same drive with the focus taken away and given back.
$FocusDir = Join-Path $OutRoot 'focus'
$Viewer = Start-ViewerWatch $OutRoot
$Job = Start-Job -ScriptBlock { param($L, $O, $C, $S) & powershell -ExecutionPolicy Bypass -File $L -Road ocean-drive -Car $C -Seconds $S -Name 'focus' -OutRoot $O -NoCockpit -ShowWindow | Out-Null } -ArgumentList $Launch, $OutRoot, $Car, $Seconds
$Away = $null; $Back = $false; $Notes = @(); $Deadline = (Get-Date).AddMinutes(20)
while ($Job.State -eq 'Running' -and (Get-Date) -lt $Deadline) {
    $T = Get-DriveSecond $FocusDir
    if ($T -ge 20 -and -not $Away) {
        $Away = Start-Process notepad -PassThru
        Start-Sleep -Milliseconds 800
        [void]$Shell.AppActivate($Away.Id)
        $Notes += "focus to Notepad at drive second ~$T"
    }
    if ($T -ge 30 -and $Away -and -not $Back) {
        $Game = Get-CimInstance Win32_Process | Where-Object { $_.Name -like 'UnrealEditor*' -and $_.CommandLine -and $_.CommandLine.Contains($FocusDir) } | Select-Object -First 1
        if ($Game) { [void]$Shell.AppActivate([int]$Game.ProcessId) }
        Stop-Process -Id $Away.Id -Force -ErrorAction SilentlyContinue
        $Back = $true
        $Notes += "focus back to the game at drive second ~$T"
    }
    Start-Sleep -Milliseconds 500
}
Wait-Job $Job -Timeout 600 | Out-Null
Remove-Job $Job -Force -ErrorAction SilentlyContinue
Stop-ViewerWatch $Viewer
if ($Away) { Stop-Process -Id $Away.Id -Force -ErrorAction SilentlyContinue }

$B = Read-Run $BaseDir; $F = Read-Run $FocusDir
$NewHitches = @($F.Events | Where-Object { $_ -match '^([\d.]+) s' -and [double]$Matches[1] -ge 18 -and [double]$Matches[1] -le 35 })
$Ok = $B.P95 -gt 0 -and $F.P95 -gt 0 -and [Math]::Abs($F.P50 - $B.P50) -le 1.0 -and [Math]::Abs($F.P95 - $B.P95) -le 1.0 -and $NewHitches.Count -le @($B.Events | Where-Object { $_ -match '^([\d.]+) s' -and [double]$Matches[1] -ge 18 -and [double]$Matches[1] -le 35 }).Count -and $Away -and $Back
$Result = if ($Ok) { 'PASS' } else { 'FAIL' }
$Md = @('# Focus check', '', "Verdict: **$Result**. Two visible runs of Ocean Drive ($Car, $Seconds s, borderless at the screen's size): a baseline, then a run with the focus taken away and given back.", '') +
    @($Notes | ForEach-Object { "- $_" }) + @('',
    '| Run | Frame p50 (ms) | Frame p95 (ms) | Frames over 50 ms | Hitches over 100 ms | Hitch events |', '|---|---|---|---|---|---|',
    "| Baseline | $($B.P50) | $($B.P95) | $($B.Over50) | $($B.Hitches) | $(($B.Events) -join '; ') |",
    "| Focus away 20-30 s | $($F.P50) | $($F.P95) | $($F.Over50) | $($F.Hitches) | $(($F.Events) -join '; ') |",
    '', 'Focus as Windows saw it: viewer.csv.')
$Md | Set-Content -Encoding utf8 (Join-Path $OutRoot 'focus-check.md')
Write-Host "FOCUS CHECK $Result baseline p50 $($B.P50) p95 $($B.P95) ms; focus run p50 $($F.P50) p95 $($F.P95) ms; $($Notes -join '; ') -> $(Join-Path $OutRoot 'focus-check.md')"
if ($Ok) { exit 0 } else { exit 1 }
}
finally { Clear-GpuBusy }

<#
.SYNOPSIS
  The controller menu test: can an Xbox controller reach every control of the menus?

.DESCRIPTION
  Runs the automation test SimCruise.UI.PadNavigation (scripts\check.ps1 -Filter, headless). The
  test sends pad keys (LB, RB, D-pad, A, B, the left stick) as the game gets them, walks every
  page of the pause menu and the list screens, and logs each control as PADNAV REACHED or
  PADNAV UNREACHABLE. This script writes report.md from those lines and FAILS (exit 1) on any
  UNREACHABLE control, also on one that the menu does not have yet (the tab bar focus from
  D-pad Up). Frames per screen need the game: that part is NOT RUN in this version.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\pad-review.ps1
#>
param(
    [string]$OutDir = '',
    [switch]$ShowWindow,
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
if (-not $OutDir) { $OutDir = Join-Path $Root "Saved\Review\$(Get-Date -Format 'yyyyMMdd-HHmm')-pad-review" }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$CheckArgs = @('-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'check.ps1'), '-Filter', 'SimCruise.UI.PadNavigation', '-NoViewCheck')
if ($NoBuild) { $CheckArgs += '-NoBuild' }
& powershell @CheckArgs | Out-File -Encoding utf8 (Join-Path $OutDir 'check.txt')
$TestExit = $LASTEXITCODE
$Log = Join-Path $Root 'Saved\Logs\SimCruiseChecks.log'
if (Test-Path $Log) { Copy-Item $Log (Join-Path $OutDir 'test.log') -Force }
$Lines = if (Test-Path $Log) { @(Select-String -Path $Log -Pattern 'PADNAV (REACHED|UNREACHABLE) screen="([^"]*)" control="([^"]*)" how="([^"]*)"' | Select-Object -Unique) } else { @() }
$Rows = @(); $Seen = @{}
foreach ($L in $Lines) {
    $G = $L.Matches[0].Groups
    $Key = "$($G[2].Value)|$($G[3].Value)"
    if ($Seen.ContainsKey($Key)) { continue }
    $Seen[$Key] = $true
    $Rows += [pscustomobject]@{ State = $G[1].Value; Screen = $G[2].Value; Control = $G[3].Value; How = $G[4].Value }
}
$Bad = @($Rows | Where-Object { $_.State -eq 'UNREACHABLE' })
$Result = if (-not $Rows.Count) { 'FAIL' } elseif ($Bad.Count) { 'FAIL' } else { 'PASS' }
$Md = @('# Controller menu test', '', "Verdict: **$Result** ($($Rows.Count) controls, $($Bad.Count) unreachable). Automation test exit code: $TestExit.", '')
if (-not $Rows.Count) { $Md += 'No PADNAV lines: the test did not run (see check.txt and test.log).' }
$Md += @('| State | Screen | Control | How |', '|---|---|---|---|')
foreach ($R in ($Rows | Sort-Object @{ Expression = { $_.State -eq 'REACHED' } }, Screen)) { $Md += "| $($R.State) | $($R.Screen) | $($R.Control) | $($R.How) |" }
$Md += @('', 'Frames per screen: NOT RUN (the walk is headless; frames need the in-game run).')
$Md | Set-Content -Encoding utf8 (Join-Path $OutDir 'report.md')
Write-Host "PAD REVIEW $Result controls=$($Rows.Count) unreachable=$($Bad.Count) -> $(Join-Path $OutDir 'report.md')"
foreach ($B in $Bad) { Write-Host "  UNREACHABLE $($B.Screen): $($B.Control) ($($B.How))" }
if ($Result -eq 'PASS') { exit 0 } else { exit 1 }

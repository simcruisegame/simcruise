<#
.SYNOPSIS
  Checks that every launch under a folder has been reviewed: each review.md has a verdict (PASS,
  FAIL or N/A) for every item and the reviewer's name.

.DESCRIPTION
  Prints, for each review.md under -Path, the items still TODO and the items that FAIL. Exits 0
  only when every sheet is complete and none has a FAIL. For a pre-flight folder (one with
  report.md and a pre-flight that passed its own checks) a complete review with no FAIL adds the
  PASS row to %LOCALAPPDATA%\SimCruise\preflight-index.csv that scripts\playtest.ps1 needs;
  a FAIL adds a FAIL row.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\review-check.ps1 Saved\Review\20261114-0130-abc1234
#>
param([Parameter(Mandatory = $true)][string]$Path)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewLib.ps1')
$Path = (Resolve-Path $Path).Path
$Sheets = @(Get-ChildItem $Path -Recurse -Filter 'review.md')
if (-not $Sheets.Count) { Write-Host "No review.md under $Path"; exit 1 }
$Incomplete = 0; $Failed = 0
foreach ($S in $Sheets) {
    $T = Test-ReviewSheet $S.FullName
    $Rel = $S.FullName.Substring($Path.Length).TrimStart('\')
    if (-not $T.Complete) { $Incomplete++; Write-Host "NOT REVIEWED $Rel : $($T.Todo -join '; ')" }
    elseif ($T.Failed.Count) { $Failed++; Write-Host "REVIEWED, FAIL $Rel : $($T.Failed -join '; ')" }
    else { Write-Host "REVIEWED, PASS $Rel" }
}

# A pre-flight run: record the reviewed result for scripts\playtest.ps1.
$Report = Join-Path $Path 'report.md'
$Marker = Join-Path $Path 'preflight-auto.txt'
if ((Test-Path $Report) -and (Test-Path $Marker) -and $Incomplete -eq 0) {
    $Auto = Get-Content $Marker
    $Commit = ($Auto | Where-Object { $_ -like 'commit *' }) -replace '^commit ', ''
    $AutoResult = ($Auto | Where-Object { $_ -like 'result *' }) -replace '^result ', ''
    $Verdict = if ($AutoResult -eq 'PASS' -and $Failed -eq 0) { 'PASS' } else { 'FAIL' }
    $Index = Join-Path $env:LOCALAPPDATA 'SimCruise\preflight-index.csv'
    if (Test-Path $Index) {
        $Why = if ($Verdict -eq 'PASS') { 'reviewed' } else { "reviewed: automatic $AutoResult, $Failed sheets with FAIL items" }
        "$(Get-Date -Format 'yyyyMMdd-HHmmss'),$Commit,$Verdict,$($Sheets.Count),,,$Report,`"$Why`"" | Add-Content -Encoding utf8 $Index
        Write-Host "Index: $Commit $Verdict ($Why)"
    }
}
Write-Host ("{0} sheets: {1} not reviewed, {2} with FAIL items" -f $Sheets.Count, $Incomplete, $Failed)
if ($Incomplete -or $Failed) { exit 1 }
Write-Host 'ALL REVIEWED, NO FAIL'
exit 0

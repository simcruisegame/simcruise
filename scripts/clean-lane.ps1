<#
.SYNOPSIS
  Removes old test outputs, and on request package outputs and build caches, from one checkout.

.DESCRIPTION
  The hygiene step at the end of each release (docs/team/dev-loop.md, "Hygiene";
  docs/release/RELEASE-CHECKLIST.md, step 8). Each lane runs it on its own checkout only.

  Report mode by default: it changes nothing and lists what it would remove, with the size.
  -Apply removes it.

  - Run folders (one sub-folder or file per run): Saved\Preflight, Saved\Review,
    Saved\ReleaseTest, Saved\ViewCheck, Saved\Screenshots\ViewCheck, Saved\Profiling,
    Saved\Crashes. The newest -Keep runs of each folder stay. Items named "latest" and index
    files (*.csv, *.md, *.json at the top of the folder) always stay.
  - Saved\Logs: files older than -LogDays are removed.
  - -Package also removes Saved\Package, Saved\StagedBuilds, Saved\Cooked and Saved\Shaders.
    scripts\package.ps1 makes them again.
  - -Build also removes Intermediate, Binaries and DerivedDataCache, in the checkout and in
    its Plugins. The next build is a full build (about 25 minutes). Use it when a lane is idle.

  It never touches tracked files, Content, data, Saved\Config, Saved\Realism (it holds the
  reference photos and the score history) or another checkout. It stops when an editor, a game,
  a build or a test of this checkout runs.

.PARAMETER Root
  The checkout to clean (default: the checkout of this script).
.PARAMETER Keep
  How many runs of each run folder stay (default 3).
.PARAMETER LogDays
  Log files older than this many days go (default 7).
.PARAMETER Package
  Also remove the package outputs.
.PARAMETER Build
  Also remove the build caches.
.PARAMETER Apply
  Remove the items. Without it the script only reports.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\clean-lane.ps1
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\clean-lane.ps1 -Package -Apply
.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\clean-lane.ps1 -Root D:\sc\lane-live -Package -Build -Apply
#>
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [ValidateRange(1, 100)][int]$Keep = 3,
    [ValidateRange(0, 3650)][int]$LogDays = 7,
    [switch]$Package,
    [switch]$Build,
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\')
if (-not (Test-Path -LiteralPath (Join-Path $Root 'SimCruise.uproject'))) { throw "$Root is not a SimCruise checkout (no SimCruise.uproject)" }

# Stop when something of this checkout runs: its files may be open, and a run folder may be half written.
$Pattern = [regex]::Escape($Root) + '([\\"/ ]|$)'
$Busy = @(Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -and $_.Name -match '^(UnrealEditor|SimCruise|UnrealBuildTool|dotnet|ShaderCompileWorker|CrashReportClient)' -and $_.CommandLine -match $Pattern })
if ($Busy.Count -gt 0) {
    $Busy | ForEach-Object { Write-Host ("  running: {0} (pid {1})" -f $_.Name, $_.ProcessId) }
    throw "Something of $Root runs now. Run the cleanup when it has stopped."
}

$Items = New-Object System.Collections.Generic.List[object]
function Add-Item([string]$Path, [string]$Why) {
    if (Test-Path -LiteralPath $Path) { $Items.Add([pscustomobject]@{ Path = $Path; Why = $Why }) }
}
function Get-Size([string]$Path) {
    $Item = Get-Item -LiteralPath $Path -Force
    if (-not $Item.PSIsContainer) { return $Item.Length }
    $Sum = (Get-ChildItem -LiteralPath $Path -Recurse -File -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum).Sum
    if ($Sum) { return [long]$Sum } else { return 0 }
}

$Saved = Join-Path $Root 'Saved'
# Folder = items per run: a view check saves its 3 pictures as files, the others make one item per run.
$RunFolders = [ordered]@{ 'Preflight' = 1; 'Review' = 1; 'ReleaseTest' = 1; 'ViewCheck' = 1; 'Screenshots\ViewCheck' = 3; 'Profiling' = 1; 'Crashes' = 1 }
foreach ($Folder in $RunFolders.Keys) {
    $Dir = Join-Path $Saved $Folder
    $KeepItems = $Keep * $RunFolders[$Folder]
    if (-not (Test-Path -LiteralPath $Dir)) { continue }
    $Runs = @(Get-ChildItem -LiteralPath $Dir -Force | Where-Object {
        $_.Name -ne 'latest' -and -not (-not $_.PSIsContainer -and $_.Extension -in '.csv', '.md', '.json') } |
        Sort-Object LastWriteTime -Descending)
    $Runs | Select-Object -Skip $KeepItems | ForEach-Object { Add-Item $_.FullName "older than the newest $Keep runs" }
}

$Logs = Join-Path $Saved 'Logs'
if (Test-Path -LiteralPath $Logs) {
    $Limit = (Get-Date).AddDays(-$LogDays)
    Get-ChildItem -LiteralPath $Logs -File -Force | Where-Object { $_.LastWriteTime -lt $Limit } |
        ForEach-Object { Add-Item $_.FullName "log older than $LogDays days" }
}

if ($Package) {
    foreach ($Name in 'Package', 'StagedBuilds', 'Cooked', 'Shaders') { Add-Item (Join-Path $Saved $Name) 'package output' }
}

if ($Build) {
    $Bases = @($Root) + @(Get-ChildItem -LiteralPath (Join-Path $Root 'Plugins') -Directory -ErrorAction SilentlyContinue | ForEach-Object FullName)
    foreach ($Base in $Bases) {
        foreach ($Name in 'Intermediate', 'Binaries', 'DerivedDataCache') { Add-Item (Join-Path $Base $Name) 'build cache' }
    }
}

# Never remove a tracked file (a safety net for the folder lists above).
$Tracked = @(& git -C $Root ls-files -- Saved Intermediate Binaries DerivedDataCache 2>$null)
if ($Tracked.Count -gt 0) { throw "Tracked files are under the cache folders of $Root; nothing was removed: $($Tracked[0])" }

$Total = [long]0
foreach ($Item in $Items) {
    $Item | Add-Member -NotePropertyName Bytes -NotePropertyValue (Get-Size $Item.Path)
    $Total += $Item.Bytes
    Write-Host ("{0,9:N1} MB  {1}  ({2})" -f ($Item.Bytes / 1MB), $Item.Path.Substring($Root.Length + 1), $Item.Why)
}
Write-Host ("{0} items, {1:N2} GB in {2}" -f $Items.Count, ($Total / 1GB), $Root)
if (-not $Apply) { Write-Host 'Report only. Add -Apply to remove these items.'; exit 0 }

$Failed = 0
foreach ($Item in $Items) {
    # The \\?\ form removes paths longer than 260 characters (Unreal's Intermediate has them).
    $Long = '\\?\' + $Item.Path
    if ((Get-Item -LiteralPath $Item.Path -Force).PSIsContainer) { & cmd.exe /d /c "rd /s /q `"$Long`"" 2>$null }
    else { & cmd.exe /d /c "del /f /q `"$Long`"" 2>$null }
    if (Test-Path -LiteralPath $Item.Path) { $Failed++; Write-Host "  not removed (in use?): $($Item.Path)" }
}
$Free = (Get-PSDrive ($Root.Substring(0, 1))).Free
Write-Host ("Removed {0} of {1} items. Free on {2}: {3:N1} GB" -f ($Items.Count - $Failed), $Items.Count, $Root.Substring(0, 2), ($Free / 1GB))
if ($Failed -gt 0) { exit 1 }
exit 0

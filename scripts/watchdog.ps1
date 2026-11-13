<#
.SYNOPSIS
  Lists Unreal editors, SimCruise games and crash reporters on this PC that look stale.

.DESCRIPTION
  Report mode by default: it changes nothing. It lists every UnrealEditor*, SimCruise*.exe and
  CrashReportClient*.exe process that is older than -MaxMinutes, or whose parent (the script
  or editor that started it) is gone, with its process id, age, why it is listed, the folder
  it works in (the checkout of its .uproject, or the folder of a packaged game) and its
  command line. A stale run can hold the editor lock or a test slot (scripts\EditorLock.ps1)
  and the files of its checkout.

  -Stop stops the listed processes, and only those. Look at the list first: an interactive
  editor that someone left open on purpose is also "older than -MaxMinutes".

.PARAMETER MaxMinutes
  List processes that have run longer than this (default 30).

.PARAMETER Stop
  Stop the processes in the list (each one by its process id, not its children).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\watchdog.ps1
  powershell -ExecutionPolicy Bypass -File scripts\watchdog.ps1 -MaxMinutes 60
#>
param(
    [int]$MaxMinutes = 30,
    [switch]$Stop
)

$ErrorActionPreference = 'Stop'
$Now = Get-Date
$All = @(Get-CimInstance Win32_Process)
$ById = @{}
foreach ($P in $All) { $ById[[int]$P.ProcessId] = $P }

function Get-WorkFolder($P) {
    $Cmd = [string]$P.CommandLine
    if ($Cmd -match '"?([A-Za-z]:[^"]*?\\)[^\\"]+\.uproject"?') { return $Matches[1].TrimEnd('\') }
    if ($P.ExecutablePath -and $P.Name -like 'SimCruise*') { return Split-Path $P.ExecutablePath }
    # A crash reporter works for the editor or game it monitors (or its parent).
    $Parent = $ById[[int]$P.ParentProcessId]
    if ($Cmd -match '-MONITOR=(\d+)') { $Parent = $ById[[int]$Matches[1]] }
    if ($Parent -and $Parent.Name -notlike 'CrashReportClient*' -and ($Parent.Name -like 'UnrealEditor*' -or $Parent.Name -like 'SimCruise*')) { return Get-WorkFolder $Parent }
    return ''
}

$Rows = @()
foreach ($P in $All) {
    if (-not ($P.Name -like 'UnrealEditor*' -or $P.Name -like 'SimCruise*.exe' -or $P.Name -like 'CrashReportClient*')) { continue }
    if (-not $P.CreationDate) { continue }
    $Age = ($Now - $P.CreationDate).TotalMinutes
    $Parent = $ById[[int]$P.ParentProcessId]
    # A crash reporter restarts itself detached from its editor (-RespawnedInstance); the
    # process it works for is the one in -MONITOR=<pid>.
    if ($P.Name -like 'CrashReportClient*' -and [string]$P.CommandLine -match '-MONITOR=(\d+)') { $Parent = $ById[[int]$Matches[1]] }
    # A process id can be reused: a "parent" that started after the child is not its parent.
    $Orphan = (-not $Parent) -or ($Parent.CreationDate -and $Parent.CreationDate -gt $P.CreationDate)
    $Why = @()
    if ($Age -gt $MaxMinutes) { $Why += "older than $MaxMinutes min" }
    if ($Orphan) { $Why += 'its starter is gone' }
    if (-not $Why) { continue }
    $Rows += [pscustomobject]@{
        Pid     = [int]$P.ProcessId
        Name    = $P.Name
        AgeMin  = [int]$Age
        Why     = $Why -join ', '
        Folder  = Get-WorkFolder $P
        Command = [string]$P.CommandLine
    }
}

if (-not $Rows) { Write-Host "No stale Unreal or SimCruise processes (limit $MaxMinutes min)."; exit 0 }
foreach ($R in ($Rows | Sort-Object AgeMin -Descending)) {
    Write-Host ("pid {0,-6} {1,-28} {2,5} min  {3}" -f $R.Pid, $R.Name, $R.AgeMin, $R.Why)
    Write-Host ("    folder:  {0}" -f $(if ($R.Folder) { $R.Folder } else { '(unknown)' }))
    Write-Host ("    command: {0}" -f $R.Command)
}
if (-not $Stop) {
    Write-Host "$($Rows.Count) listed. Nothing was stopped (report mode; -Stop stops exactly these)."
    exit 0
}
foreach ($R in $Rows) {
    try { Stop-Process -Id $R.Pid -Force; Write-Host "Stopped pid $($R.Pid) ($($R.Name))." }
    catch { Write-Host "Could not stop pid $($R.Pid): $_" }
}
exit 0

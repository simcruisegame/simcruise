<#
.SYNOPSIS
  One GPU Unreal Editor at a time on this PC, across every checkout and worktree,
  and a few headless test editors beside it.

.DESCRIPTION
  Dot-source this file, then wrap an editor run in Enter-EditorLock / Exit-EditorLock.
  The lock is a named Windows mutex, so Windows frees it when the process that holds
  it ends, even after a crash or a kill. Two editors at once fight over the GPU and
  over the MCP port (8000), so play tests, -game runs, captures and MCP sessions take it.
  Headless -nullrhi test runs (scripts\check.ps1) take a slot instead
  (Enter-HeadlessSlot / Exit-HeadlessSlot): they start without the MCP plugins, so they
  can run next to the GPU editor and next to each other.
#>

$script:EditorLockName = 'Global\SimCruiseEditor'
$script:EditorLock = $null

function Enter-EditorLock {
    param([int]$TimeoutMinutes = 60, [string]$Why = 'editor run')
    $script:EditorLock = New-Object System.Threading.Mutex($false, $script:EditorLockName)
    Write-Host "Waiting for the editor lock ($Why)..."
    try {
        if (-not $script:EditorLock.WaitOne([TimeSpan]::FromMinutes($TimeoutMinutes))) {
            Write-Host "Another SimCruise editor run held the lock for $TimeoutMinutes min. Gave up."
            return $false
        }
    }
    catch [System.Threading.AbandonedMutexException] {
        # The last holder ended without releasing it; the lock is now ours.
    }
    Write-Host 'Editor lock taken.'
    return $true
}

function Exit-EditorLock {
    if ($script:EditorLock) {
        try { $script:EditorLock.ReleaseMutex() } catch { }
        $script:EditorLock.Dispose()
        $script:EditorLock = $null
    }
}

# Headless (-nullrhi) test runs use neither the GPU nor MCP port 8000, so they do not take
# the one-editor lock. They take one of a few slots instead, so several worktrees can test at
# once without running the PC out of memory. Each slot is its own named mutex
# (Global\SimCruiseHeadless0, 1, ...), not one semaphore: Windows frees a mutex when its
# holder dies, while a semaphore count taken by a killed script would stay taken.
$script:HeadlessSlot = $null
$script:HeadlessSlotMax = 3

function Get-HeadlessSlotCount {
    # How many headless test editors may run at once: -Slots, else $env:SIMCRUISE_TEST_SLOTS,
    # else what the free memory allows (keeping ReserveGB free), from 1 to 3.
    # EditorPeakGB: a full -nullrhi test run peaked at 6.1 GB (working set, 2026-11-12).
    param([int]$Requested = 0, [double]$EditorPeakGB = 6.5, [double]$ReserveGB = 8)
    if ($Requested -le 0 -and $env:SIMCRUISE_TEST_SLOTS) { $Requested = [int]$env:SIMCRUISE_TEST_SLOTS }
    if ($Requested -gt 0) { return [int][math]::Min([math]::Max($Requested, 1), $script:HeadlessSlotMax) }
    try { $FreeGB = (Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory / 1MB } catch { return 1 }
    $N = [math]::Floor(($FreeGB - $ReserveGB) / $EditorPeakGB)
    return [int][math]::Min([math]::Max($N, 1), $script:HeadlessSlotMax)
}

function Enter-HeadlessSlot {
    param([int]$Slots = 1, [int]$TimeoutMinutes = 60, [string]$Why = 'headless test run')
    $Deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    Write-Host "Waiting for a headless test slot (one of $Slots; $Why)..."
    while ($true) {
        for ($I = 0; $I -lt $Slots; $I++) {
            $M = New-Object System.Threading.Mutex($false, "Global\SimCruiseHeadless$I")
            $Taken = $false
            try { $Taken = $M.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $Taken = $true }
            if ($Taken) {
                $script:HeadlessSlot = $M
                Write-Host "Headless test slot $I taken."
                return $true
            }
            $M.Dispose()
        }
        if ((Get-Date) -gt $Deadline) {
            Write-Host "All $Slots headless test slots stayed busy for $TimeoutMinutes min. Gave up."
            return $false
        }
        Start-Sleep -Seconds 2
    }
}

function Exit-HeadlessSlot {
    if ($script:HeadlessSlot) {
        try { $script:HeadlessSlot.ReleaseMutex() } catch { }
        $script:HeadlessSlot.Dispose()
        $script:HeadlessSlot = $null
    }
}

function Stop-ProcessTree {
    param([int]$ProcessId)
    & taskkill.exe /T /F /PID $ProcessId 2>&1 | Out-Null
}

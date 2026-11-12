<#
.SYNOPSIS
  One Unreal Editor at a time on this PC, across every checkout and worktree.

.DESCRIPTION
  Dot-source this file, then wrap an editor run in Enter-EditorLock / Exit-EditorLock.
  The lock is a named Windows mutex, so Windows frees it when the process that holds
  it ends, even after a crash or a kill. Two editors at once fight over the GPU and
  over the MCP port (8000), so headless tests, play tests and MCP sessions all take it.
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

function Stop-ProcessTree {
    param([int]$ProcessId)
    & taskkill.exe /T /F /PID $ProcessId 2>&1 | Out-Null
}

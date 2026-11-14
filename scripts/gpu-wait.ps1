<#
.SYNOPSIS
  Waits while another lane holds the GPU (C:\aiwork\GPU-BUSY). Call it before a build or a game launch.

.DESCRIPTION
  The file has one line: holder, start, end_estimate (UTC) and purpose. The script waits while the
  file is there, checking every 30 s, up to -TimeoutMinutes. A file whose end_estimate is more than
  30 min past is stale: it is ignored and logged. Exit 0 when the GPU is free (or the file is
  stale), 1 on the timeout.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\gpu-wait.ps1
  powershell -ExecutionPolicy Bypass -File scripts\gpu-wait.ps1 -TimeoutMinutes 90
#>
param([int]$TimeoutMinutes = 120)

. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Until = (Get-Date).AddMinutes($TimeoutMinutes)
while ($true) {
    $B = Read-GpuBusy
    if (-not $B) { Write-Host 'GPU free.'; exit 0 }
    if ($B.Stale) { Write-Host "GPU-BUSY is stale (end_estimate more than 30 min past), ignored: $($B.Text)"; exit 0 }
    if ((Get-Date) -gt $Until) { Write-Host "Timed out after $TimeoutMinutes min; still busy: $($B.Text)"; exit 1 }
    Write-Host "$((Get-Date).ToUniversalTime().ToString('HH:mm:ss'))Z waiting: $($B.Text)"
    Start-Sleep -Seconds 30
}

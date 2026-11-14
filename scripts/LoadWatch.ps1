<#
.SYNOPSIS
  Other load on the PC during a frame-time test (dot-source this file).

.DESCRIPTION
  Frame times are honest only in a quiet window: load from other lanes added about 2 ms to the
  p95. Start-LoadWatch samples every -EverySeconds, into load.csv: the other Unreal and SimCruise
  processes (any whose command line does not name this run's folder) and the GPU use of all
  processes other than this run's game (Windows "GPU Engine" counters, 3D engines). Stop-LoadWatch
  ends it and returns @{ Quiet; Samples; OtherProcesses; MaxOtherGpu; MeanOtherGpu }. A run is
  NOT QUIET when another Unreal or SimCruise process ran or other GPU use averaged over 5 %.
#>

function Get-LoadSample([string]$RunDir) {
    $Procs = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.Name -match '^(UnrealEditor|SimCruise|UnrealEditor-Cmd)' -and -not ($_.CommandLine -and $_.CommandLine.Contains($RunDir)) })
    $Ours = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object { $_.CommandLine -and $_.CommandLine.Contains($RunDir) } | ForEach-Object { $_.ProcessId })
    $Gpu = 0.0
    try {
        $C = (Get-Counter '\GPU Engine(*engtype_3D)\Utilization Percentage' -ErrorAction Stop).CounterSamples
        foreach ($S in $C) {
            if ($S.InstanceName -match 'pid_(\d+)_' -and $Ours -notcontains [int]$Matches[1]) { $Gpu += $S.CookedValue }
        }
    } catch { $Gpu = -1 }
    return [pscustomobject]@{
        Time = (Get-Date).ToUniversalTime().ToString('HH:mm:ss')
        OtherProcesses = (($Procs | ForEach-Object { "$($_.Name)#$($_.ProcessId)" }) -join ' ')
        OtherGpu = [Math]::Round($Gpu, 1)
    }
}

function Start-LoadWatch([string]$RunDir, [int]$EverySeconds = 30) {
    $Csv = Join-Path $RunDir 'load.csv'
    'time,other_processes,other_gpu_percent' | Set-Content -Encoding utf8 $Csv
    $Lib = $PSCommandPath
    return Start-Job -ScriptBlock {
        param($Lib, $RunDir, $Csv, $Every)
        . $Lib
        while ($true) {
            $S = Get-LoadSample $RunDir
            "$($S.Time),`"$($S.OtherProcesses)`",$($S.OtherGpu)" | Add-Content -Encoding utf8 $Csv
            Start-Sleep -Seconds $Every
        }
    } -ArgumentList $Lib, $RunDir, $Csv, $EverySeconds
}

function Stop-LoadWatch($Job, [string]$RunDir) {
    if ($Job) { Stop-Job $Job -ErrorAction SilentlyContinue; Remove-Job $Job -Force -ErrorAction SilentlyContinue }
    $Rows = @(Import-Csv (Join-Path $RunDir 'load.csv') -ErrorAction SilentlyContinue)
    $Others = @($Rows | Where-Object { $_.other_processes } | ForEach-Object { $_.other_processes -split ' ' } | Select-Object -Unique)
    $Gpus = @($Rows | Where-Object { [double]$_.other_gpu_percent -ge 0 } | ForEach-Object { [double]$_.other_gpu_percent })
    $Mean = if ($Gpus.Count) { [Math]::Round(($Gpus | Measure-Object -Average).Average, 1) } else { -1 }
    $Max = if ($Gpus.Count) { ($Gpus | Measure-Object -Maximum).Maximum } else { -1 }
    return [pscustomobject]@{ Quiet = ($Others.Count -eq 0 -and $Mean -le 5); Samples = $Rows.Count; OtherProcesses = ($Others -join ', '); MaxOtherGpu = $Max; MeanOtherGpu = $Mean }
}

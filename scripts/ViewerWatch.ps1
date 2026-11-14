<#
.SYNOPSIS
  A watched test run: focus changes and the viewer's input, logged from outside the game (dot-source this file).

.DESCRIPTION
  Start-ViewerWatch samples every 250 ms, into viewer.csv: the foreground window's process (a
  "focus" row each time it changes) and the time of the last keyboard, mouse or pad input on the
  PC (Windows GetLastInputInfo; an "input" row each time it changes, at most once a second). The
  robot's input goes to the game directly, not through Windows, so it is not counted.
  Get-ViewerEvents returns the rows between two times: release-test.ps1 marks a place's frame
  times DISTURBED when a focus change away from the game or the viewer's input happened while it
  ran. The input is logged, not blocked: the game itself does not block it in this build.
#>

function Start-ViewerWatch([string]$RunDir) {
    $Csv = Join-Path $RunDir 'viewer.csv'
    'utc,event,detail' | Set-Content -Encoding utf8 $Csv
    return Start-Job -ScriptBlock {
        param($Csv)
        Add-Type -Namespace SimCruise -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern System.IntPtr GetForegroundWindow();
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(System.IntPtr hWnd, out uint pid);
[StructLayout(LayoutKind.Sequential)] public struct LASTINPUTINFO { public uint cbSize; public uint dwTime; }
[DllImport("user32.dll")] public static extern bool GetLastInputInfo(ref LASTINPUTINFO info);
'@
        $LastPid = 0; $LastInput = 0; $LastInputNote = [datetime]::MinValue
        while ($true) {
            $Now = (Get-Date).ToUniversalTime()
            [uint32]$Fg = 0
            [void][SimCruise.Win]::GetWindowThreadProcessId([SimCruise.Win]::GetForegroundWindow(), [ref]$Fg)
            if ($Fg -ne $LastPid) {
                $Name = (Get-Process -Id $Fg -ErrorAction SilentlyContinue).ProcessName
                "$($Now.ToString('o')),focus,$Name#$Fg" | Add-Content -Encoding utf8 $Csv
                $LastPid = $Fg
            }
            $Info = New-Object SimCruise.Win+LASTINPUTINFO
            $Info.cbSize = 8
            if ([SimCruise.Win]::GetLastInputInfo([ref]$Info) -and $Info.dwTime -ne $LastInput) {
                if ($LastInput -ne 0 -and ($Now - $LastInputNote).TotalSeconds -ge 1) {
                    "$($Now.ToString('o')),input,user input" | Add-Content -Encoding utf8 $Csv
                    $LastInputNote = $Now
                }
                $LastInput = $Info.dwTime
            }
            Start-Sleep -Milliseconds 250
        }
    } -ArgumentList $Csv
}

function Stop-ViewerWatch($Job) {
    if ($Job) { Stop-Job $Job -ErrorAction SilentlyContinue; Remove-Job $Job -Force -ErrorAction SilentlyContinue }
}

<# The focus changes away from the game while one place ran. Input rows are information only: on this PC something simulates input about every 50 s with nobody there. #>
function Get-ViewerEvents([string]$RunDir, [datetime]$FromUtc, [datetime]$ToUtc) {
    $Rows = @(Import-Csv (Join-Path $RunDir 'viewer.csv') -ErrorAction SilentlyContinue)
    return @($Rows | Where-Object {
        $T = [datetime]::Parse($_.utc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::RoundtripKind).ToUniversalTime()
        $T -ge $FromUtc -and $T -le $ToUtc -and ($_.event -eq 'focus' -and $_.detail -notmatch '^UnrealEditor#')
    })
}

<#
.SYNOPSIS
  Makes a play-test build of the latest main: a packaged, smoke-tested zip with release notes.

.DESCRIPTION
  For the owner's play test after each wave of work. It never touches another checkout:
  it fetches origin, puts a dedicated worktree (default D:\sc\playtest) on origin/main
  (or -Ref), copies in tools\atlas.exe from this checkout when it is there, and runs
  scripts\package.ps1 -Zip in that worktree under the PC-wide editor lock. Then it starts the
  packaged game headless with -SimCruiseShellSelfTest (stopped after -SmokeMinutes) and copies
  the zip to -OutDir as SimCruise-<version>-<date>-<commit>.zip with a NOTES.txt: the commits
  since the last play-test build and the smoke-test result. Keeps the newest -Keep builds.
  Nothing is published; share the zip by hand.

.PARAMETER Ref
  What to build (default origin/main).

.PARAMETER Worktree
  The play-test worktree (default D:\sc\playtest; made on first use).

.PARAMETER OutDir
  Where the zips go (default D:\SimCruise-playtests).

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File scripts\playtest.ps1
#>
param(
    [string]$Ref = 'origin/main',
    [string]$Worktree = 'D:\sc\playtest',
    [string]$OutDir = 'D:\SimCruise-playtests',
    [int]$Keep = 5,
    [int]$SmokeMinutes = 10,
    [int]$LockTimeoutMinutes = 120,
    [string]$EngineDir = $(if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.8' })
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorLock.ps1')
$Root = Split-Path -Parent $PSScriptRoot

Write-Host 'Fetching origin'
& git -C $Root fetch --quiet origin
if ($LASTEXITCODE -ne 0) { throw 'git fetch failed' }
$Commit = (& git -C $Root rev-parse --short $Ref).Trim()
if (-not (Test-Path (Join-Path $Worktree '.git'))) {
    Write-Host "Making the play-test worktree $Worktree"
    & git -C $Root worktree add --detach $Worktree $Ref
    if ($LASTEXITCODE -ne 0) { throw "git worktree add $Worktree failed" }
} else {
    if (& git -C $Worktree status --porcelain --untracked-files=no) { throw "$Worktree has local changes; it is only for play-test builds" }
    & git -C $Worktree checkout --quiet --detach $Ref
    if ($LASTEXITCODE -ne 0) { throw "git checkout $Ref in $Worktree failed" }
}
$Atlas = Join-Path $Root 'tools\atlas.exe'
if (Test-Path $Atlas) {
    New-Item -ItemType Directory -Force (Join-Path $Worktree 'tools') | Out-Null
    Copy-Item $Atlas (Join-Path $Worktree 'tools') -Force
}

# package.ps1 first runs an editor commandlet, so the worktree's editor module must be built
# (a new worktree has none). Unreal Build Tool queues on its own mutex.
Write-Host "Building SimCruiseEditor in $Worktree"
& (Join-Path $EngineDir 'Engine\Build\BatchFiles\Build.bat') SimCruiseEditor Win64 Development "-Project=$(Join-Path $Worktree 'SimCruise.uproject')" -WaitMutex -NoHotReloadFromIDE | Out-Host
if ($LASTEXITCODE -ne 0) { Write-Host 'EDITOR BUILD FAILED'; exit 1 }

if (-not (Enter-EditorLock -TimeoutMinutes $LockTimeoutMinutes -Why "play-test build of $Commit")) { exit 1 }
try {
    Write-Host "Packaging $Commit in $Worktree"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Worktree 'scripts\package.ps1') -Zip -EngineDir $EngineDir | Out-Host
    if ($LASTEXITCODE -ne 0) { Write-Host 'PACKAGE FAILED'; exit 1 }

    $Exe = Join-Path $Worktree 'Saved\Package\Windows\SimCruise.exe'
    Write-Host 'Smoke test: the packaged game, headless, -SimCruiseShellSelfTest'
    $Smoke = 'not run'
    $Proc = Start-Process -FilePath $Exe -ArgumentList '-nullrhi -unattended -nopause -nosplash -SimCruiseShellSelfTest' -PassThru -WindowStyle Hidden
    if (-not $Proc.WaitForExit($SmokeMinutes * 60 * 1000)) {
        Stop-ProcessTree -ProcessId $Proc.Id
        $Smoke = "TIMED OUT after $SmokeMinutes min"
    } else {
        $Smoke = if ($Proc.ExitCode -eq 0) { 'PASSED' } else { "FAILED (exit $($Proc.ExitCode))" }
    }
}
finally { Exit-EditorLock }
Write-Host "Smoke test: $Smoke"

$Zip = Get-ChildItem (Join-Path $Worktree 'Saved\Package') -Filter 'SimCruise-*-windows-x64.zip' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $Zip) { Write-Host 'No zip was written.'; exit 1 }
New-Item -ItemType Directory -Force $OutDir | Out-Null
$Name = $Zip.BaseName -replace '-windows-x64$', ''
$Target = Join-Path $OutDir ("{0}-{1}-{2}.zip" -f $Name, (Get-Date -Format 'yyyyMMdd-HHmm'), $Commit)
Copy-Item $Zip.FullName $Target -Force

# Release notes: the commits since the last play-test build.
$LastFile = Join-Path $OutDir 'last-commit.txt'
$Since = if (Test-Path $LastFile) { (Get-Content $LastFile -Raw).Trim() } else { '' }
$Log = if ($Since) { & git -C $Root log --no-merges --format='- %s' "$Since..$Commit" } else { & git -C $Root log --no-merges --format='- %s' -n 30 $Commit }
$Notes = @("SimCruise play-test build $Name, commit $Commit, $(Get-Date -Format 'yyyy-MM-dd HH:mm')", '',
    "Smoke test (packaged game, -SimCruiseShellSelfTest): $Smoke", '',
    $(if ($Since) { "Changes since the last play-test build ($Since):" } else { 'Latest changes:' })) + @($Log)
Set-Content -Path ($Target -replace '\.zip$', '-NOTES.txt') -Value $Notes -Encoding utf8
Set-Content -Path $LastFile -Value $Commit

# Keep the newest builds only.
Get-ChildItem $OutDir -Filter 'SimCruise-*.zip' | Sort-Object LastWriteTime -Descending | Select-Object -Skip $Keep | ForEach-Object {
    Remove-Item $_.FullName -Force
    $N = $_.FullName -replace '\.zip$', '-NOTES.txt'
    if (Test-Path $N) { Remove-Item $N -Force }
}
Write-Host "PLAYTEST BUILD: $Target"
if ($Smoke -ne 'PASSED') { exit 1 }
exit 0

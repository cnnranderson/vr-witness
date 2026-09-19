<#
.SYNOPSIS
Control the persistent walking, cursor and snap-turn session.
.DESCRIPTION
Start accepts feature flags; TargetPid defaults to the sole running game. Use the loaded BuildName.
.PARAMETER AimSpeed
Initial motion speed (10..300 percent of view width/s); a valid config/input.ini value overrides it.
.PARAMETER AimSmoothingMs
Motion smoothing time in milliseconds; applies at session start.
.EXAMPLE
.\scripts\controller-session.ps1 start -BuildName input-settings -LegacyAxis0 -Aim -Snap
#>
[CmdletBinding()]
param(
    [ValidateSet('start','stop','enable','disable','status')][string]$Action = 'status',
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\The Witness',
    [int]$TargetPid = 0,
    [switch]$LegacyAxis0,
    [switch]$Aim,
    [switch]$Snap,
    [ValidateSet('22.5','45','90')][string]$SnapAngle = '22.5',
    [ValidateRange(10,300)][int]$AimSpeed = 60,
    [ValidateRange(0,250)][int]$AimSmoothingMs = 80,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$BuildName = 'input-settings'
)
$ErrorActionPreference = 'Stop'
if (($PSBoundParameters.ContainsKey('AimSpeed') -or $PSBoundParameters.ContainsKey('AimSmoothingMs')) -and (!$Aim -or $Action -ne 'start')) { throw 'Pointing settings require start -Aim.' }
if ($Snap -and $Action -ne 'start') { throw 'Snap is selected at session start only.' }
if ($PSBoundParameters.ContainsKey('SnapAngle') -and (!$Snap -or $Action -ne 'start')) { throw 'SnapAngle requires start -Snap.' }
if ($Aim -and $Action -ne 'start') { throw 'Aim is selected at session start only.' }
if ($LegacyAxis0 -and $Action -ne 'start') { throw 'LegacyAxis0 is selected at session start only.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$loader = Join-Path $projectRoot "out\$BuildName\bin\witness-probe-loader.exe"
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one 64-bit Witness process, or specify -TargetPid.' }
    $TargetPid = $games[0].Id
}
$extra = @()
if ($Action -eq 'start') {
    $log = Join-Path $projectRoot ("out\logs\input-session-{0}-{1}-{2}.jsonl" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $TargetPid, ([guid]::NewGuid().ToString('N').Substring(0,8)))
    $extra = @('--log', $log)
    if ($LegacyAxis0) { $extra += '--controller-legacy-axis0' }
    if ($Snap) { $extra += @('--controller-snap','--snap-angle',$SnapAngle) }
    if ($Aim) { $extra += @('--controller-aim','--aim-speed-percent',$AimSpeed,'--aim-smoothing-ms',$AimSmoothingMs) }
}
& $loader --controller-session $Action --pid $TargetPid --game-exe (Join-Path $GameDir 'witness64_d3d11.exe') @extra
if ($LASTEXITCODE -ne 0) { throw 'Input session command failed; inspect status and log. Changing loaded builds requires game restart.' }
if ($Action -eq 'start') {
    Write-Output "Controller session log: $log"
    if ($Aim) { Write-Output 'Right stick takes over the puzzle cursor; A (trigger released) recenters and returns to pointing. The input-settings build reads separate stick/motion speeds from config/input.ini; AimSmoothingMs affects pointing. Left B opens the pause menu; right B performs puzzle back. Legacy stick/pad cancellation is consumed in puzzle mode.' }
    if ($Snap) { Write-Output "Right stick snap turning: $SnapAngle degrees, walking only. Center between turns and after puzzles/menus." }
    Write-Output 'F7 toggles controller movement, configured pointing and turning; F9 stops this and an active cursor/pause session. Center the stick and release the trigger after enabling/resuming.'
}

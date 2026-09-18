<#
.SYNOPSIS
Capture controller input for at most 30 seconds.
.DESCRIPTION
Use no feature flags to observe an active session; set BuildName to its loaded build.
#>
[CmdletBinding()]
param(
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\The Witness',
    [int]$TargetPid = 0,
    [ValidateRange(1,30)][int]$Seconds = 5,
    [switch]$Move,
    [switch]$Aim,
    [switch]$AimTrace,
    [switch]$Snap,
    [switch]$SnapTrace,
    [ValidateSet('22.5','45','90')][string]$SnapAngle = '45',
    [switch]$LegacyAxis0,
    [ValidatePattern('^[a-zA-Z0-9_-]+$')][string]$BuildName = 'controller-experiment'
)
$ErrorActionPreference = 'Stop'
if ($Snap -and $SnapTrace) { throw 'Choose Snap or SnapTrace.' }
if ($PSBoundParameters.ContainsKey('SnapAngle') -and !$Snap) { throw 'SnapAngle requires Snap.' }
if ($Aim -and $AimTrace) { throw 'Choose Aim or AimTrace.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$loader = Join-Path $projectRoot "out\$BuildName\bin\witness-probe-loader.exe"
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one 64-bit Witness process, or specify -TargetPid.' }
    $TargetPid = $games[0].Id
}
$log = Join-Path $projectRoot ("out\logs\input-{0}-{1}-{2}.jsonl" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $TargetPid, ([guid]::NewGuid().ToString('N').Substring(0,8)))
$extra = @(); if ($Move) { $extra += '--controller-move' }; if ($LegacyAxis0) { $extra += '--controller-legacy-axis0' }
if ($Snap) { $extra += @('--controller-snap','--snap-angle',$SnapAngle) }; if ($SnapTrace) { $extra += '--controller-snap-trace' }
if ($Aim) { $extra += '--controller-aim' }; if ($AimTrace) { $extra += '--controller-aim-trace' }
Write-Output 'An unflagged capture observes an active session without changing it in the new build. Bounded controller test. Movement requires -Move, a detected joystick (or explicit legacy axis), game focus and neutral stick before deflection.'
if ($Aim) { Write-Output 'Pointing is enabled in puzzle mode. Release the right trigger to arm; rotate the right controller to aim.' }
if ($Snap) { Write-Output "Snap turning: $SnapAngle degrees, walking only; center the right stick between turns and after menus/puzzles." }
Write-Output 'F9 ends the test early; it also stops the cursor/pause session if that session is running.'
& $loader --controller-trace @extra --pid $TargetPid --game-exe (Join-Path $GameDir 'witness64_d3d11.exe') --log $log --duration-ms ($Seconds * 1000)
if ($LASTEXITCODE -ne 0) { throw "Controller test failed. See $log; restart the game before retrying." }
Get-Content -LiteralPath $log -Tail 1
Write-Output "Controller log: $log"

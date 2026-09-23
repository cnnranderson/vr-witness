<#
.SYNOPSIS
Control the persistent headset cursor and pause-menu repair.
.DESCRIPTION
TargetPid defaults to the sole running game; F8 toggles visuals and an active controller session.
#>
[CmdletBinding()]
param(
    [ValidateSet('start','stop','enable','disable','status')][string]$Action = 'status',
    [Parameter(Mandatory)][string]$GameDir,
    [int]$TargetPid = 0
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$loader = Join-Path $projectRoot 'out\build\bin\witness-probe-loader.exe'
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one 64-bit Witness process, or specify -TargetPid.' }
    $TargetPid = $games[0].Id
}
$extra = @()
if ($Action -eq 'start') {
    $log = Join-Path $projectRoot ("out\logs\cursor-session-{0}-{1}-{2}.jsonl" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $TargetPid, ([guid]::NewGuid().ToString('N').Substring(0,8)))
    $extra = @('--log', $log)
}
& $loader --cursor-session $Action --pid $TargetPid --game-exe (Join-Path $GameDir 'witness64_d3d11.exe') @extra
if ($LASTEXITCODE -ne 0) { throw 'Cursor/pause session command failed. Inspect status and the most recent session log.' }
if ($Action -eq 'start') {
    Write-Output "Cursor/pause session log: $log"
    Write-Output 'With the game focused: F8 toggles both active fix sessions. Use the stop action to detach. Close the game before rebuilding.'
}

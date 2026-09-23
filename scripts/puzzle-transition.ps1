<#
.SYNOPSIS
Capture puzzle-entry camera intermediates without changing the running game.
.DESCRIPTION
Requires 64-bit Python. Captures at most 30 seconds under out/logs. No DLL restart is needed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDir,
    [int]$TargetPid = 0,
    [ValidateRange(1,30)][int]$Seconds = 30
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one 64-bit Witness process, or specify -TargetPid.' }
    $TargetPid = $games[0].Id
}
& python (Join-Path $projectRoot 'tools/puzzle_transition.py') --pid $TargetPid --game-dir $GameDir --seconds $Seconds
if ($LASTEXITCODE -ne 0) { throw 'Puzzle-entry capture failed; inspect the message above.' }

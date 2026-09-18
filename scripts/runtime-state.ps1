[CmdletBinding()]
param(
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\The Witness',
    [int]$TargetPid = 0
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one 64-bit Witness process, or specify -TargetPid.' }
    $TargetPid = $games[0].Id
}
& python (Join-Path $projectRoot 'tools\runtime_state.py') --pid $TargetPid --game-dir $GameDir
if ($LASTEXITCODE -ne 0) { throw 'Read-only runtime snapshot failed. See the error above.' }

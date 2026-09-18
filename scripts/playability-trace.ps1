[CmdletBinding()]
param(
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\The Witness',
    [int]$TargetPid = 0,
    [ValidateRange(1, 30)][int]$Seconds = 5,
    [ValidateRange(1, 120)][int]$Hz = 30,
    [string]$Label = 'unspecified'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one 64-bit Witness process, or specify -TargetPid.' }
    $TargetPid = $games[0].Id
}
& python (Join-Path $projectRoot 'tools\playability_trace.py') --pid $TargetPid --game-dir $GameDir --seconds $Seconds --hz $Hz --label $Label
if ($LASTEXITCODE -ne 0) { throw 'Read-only playability capture failed. See the error above.' }

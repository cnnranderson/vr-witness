<#
.SYNOPSIS
Observe loaded game modules with a short-lived diagnostic DLL.
.DESCRIPTION
TargetPid defaults to the sole running game; reports stay under out/logs.
#>
[CmdletBinding()]
param(
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\The Witness',
    [int]$TargetPid = 0,
    [ValidateRange(0,30)][int]$Seconds = 5
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$loader = Join-Path $projectRoot 'out\build\bin\witness-probe-loader.exe'
if (-not (Test-Path -LiteralPath $loader)) { throw 'Run .\scripts\build.ps1 first.' }
$gameExe = (Resolve-Path -LiteralPath (Join-Path $GameDir 'witness64_d3d11.exe')).Path
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -eq 0) { throw 'Start the 64-bit Witness normally through Steam, then run this command again. No headset is needed.' }
    if ($games.Count -ne 1) { throw 'Multiple game processes found. Pass -TargetPid with the correct process ID.' }
    $TargetPid = $games[0].Id
}
$logRoot = Join-Path $projectRoot 'out\logs'
$log = Join-Path $logRoot ("probe-{0}-{1}-{2}.jsonl" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $TargetPid, ([guid]::NewGuid().ToString('N').Substring(0,8)))
& $loader --pid $TargetPid --game-exe $gameExe --log $log --duration-ms ($Seconds * 1000)
if ($LASTEXITCODE -ne 0) { throw 'Probe failed. See the error above; do not retry a timed-out probe until the game has been restarted.' }
Write-Output 'Graphics/VR module presence (presence alone does not prove active VR):'
Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json } | Where-Object event -eq 'runtime.snapshot' | Select-Object module,loaded

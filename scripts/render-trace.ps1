<#
.SYNOPSIS
Run a bounded render trace or one optional render experiment.
.DESCRIPTION
Stop the persistent render session before tracing; choose at most one behavior flag.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$GameDir,
    [int]$TargetPid = 0,
    [ValidateRange(1,30)][int]$Seconds = 5,
    [switch]$DeferCursor,
    [switch]$DeferMenu,
    [switch]$RedrawPause
)
$ErrorActionPreference = 'Stop'
if ([int]$DeferCursor.IsPresent + [int]$DeferMenu.IsPresent + [int]$RedrawPause.IsPresent -gt 1) { throw 'Choose one timed render experiment.' }
$projectRoot = Split-Path -Parent $PSScriptRoot
$loader = Join-Path $projectRoot 'out\build\bin\witness-probe-loader.exe'
if ($TargetPid -eq 0) {
    $games = @(Get-Process -Name 'witness64_d3d11' -ErrorAction SilentlyContinue)
    if ($games.Count -ne 1) { throw 'Start one 64-bit Witness process, or specify -TargetPid.' }
    $TargetPid = $games[0].Id
}
$log = Join-Path $projectRoot ("out\logs\render-{0}-{1}-{2}.jsonl" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $TargetPid, ([guid]::NewGuid().ToString('N').Substring(0,8)))
$experiment = @()
if ($DeferCursor) { $experiment += '--defer-cursor' }
if ($DeferMenu) { $experiment += '--defer-menu' }
if ($RedrawPause) { $experiment += '--redraw-pause' }
& $loader --render-trace @experiment --pid $TargetPid --game-exe (Join-Path $GameDir 'witness64_d3d11.exe') --log $log --duration-ms ($Seconds * 1000)
if ($LASTEXITCODE -ne 0) { throw "Render experiment failed. See $log; restart the game before retrying." }
Get-Content -LiteralPath $log | ForEach-Object { $_ | ConvertFrom-Json } | Group-Object event | Select-Object Name,Count

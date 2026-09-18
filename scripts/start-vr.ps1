<#
.SYNOPSIS
Start VR and enable both current mod sessions.
.DESCRIPTION
Reuse a running game, wait for native VR, then start or enable the tested sessions.
.PARAMETER WaitSeconds
Native VR readiness timeout in seconds (10..600); SteamVR/game process startup has separate limits.
#>
[CmdletBinding()]
param(
    [string]$GameDir = 'D:\SteamLibrary\steamapps\common\The Witness',
    [ValidateRange(10,600)][int]$WaitSeconds = 300
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildName = 'input-settings'
$mutex = New-Object System.Threading.Mutex($false, 'Local\WitnessVrLauncher')
$locked = $false
$transcribing = $false
$result = 1

function Get-WitnessProcess {
    $games = @(Get-Process -Name witness64_d3d11 -ErrorAction SilentlyContinue)
    if ($games.Count -gt 1) { throw 'More than one Witness game is running. Close the extra instance and try again.' }
    if ($games.Count -eq 1) { return $games[0] }
}

function Get-ModStatus([string]$Script, [hashtable]$Arguments) {
    $lines = @(& $Script status @Arguments)
    $json = @($lines | Where-Object { $_ -is [string] -and $_.StartsWith('{') })
    if ($json.Count -ne 1) { throw "No usable status from $Script." }
    return ($json[0] | ConvertFrom-Json)
}

function Assert-ModState($Status, [string]$Label) {
    if ($Status.fault -or $Status.state -notin @('running','stopped')) {
        throw "$Label reports $($Status.state) / fault=$($Status.fault). Close the game normally and try again; logs are in out\logs."
    }
}

function Enable-Mod([string]$Script, [hashtable]$Arguments, [hashtable]$StartArguments, $Status, [string]$Label) {
    if ($Status.state -eq 'stopped') {
        Write-Host "Starting $Label..."
        & $Script start @Arguments @StartArguments
    } elseif (!$Status.enabled) {
        Write-Host "Enabling $Label..."
        & $Script enable @Arguments
    } else {
        Write-Host "$Label is already running."
    }
}

try {
    try { $locked = $mutex.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another Witness VR launcher is already working. Use its window.' }
    $logDir = Join-Path $projectRoot 'out\logs'
    New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    $log = Join-Path $logDir ("launcher-{0}-{1}.txt" -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'), $PID)
    Start-Transcript -Path $log | Out-Null
    $transcribing = $true
    Write-Host 'Witness VR - turn on your headset and both controllers.'
    Write-Host "Startup log: $log"

    $gameLauncher = Join-Path $GameDir 'witness_d3d11.exe'
    $required = @($gameLauncher, (Join-Path $GameDir 'witness64_d3d11.exe'))
    foreach ($relative in @('out\build\bin\witness-probe-loader.exe', 'out\build\bin\witness-render-probe.dll',
        'out\input-settings\bin\witness-probe-loader.exe', 'out\input-settings\bin\witness-input-probe.dll',
        'scripts\cursor-session.ps1', 'scripts\controller-session.ps1', 'tools\runtime_state.py')) {
        $required += Join-Path $projectRoot $relative
    }
    foreach ($path in $required) {
        if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file is missing: $path" }
    }
    $python = (Get-Command python -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    & $python -c 'import sys; sys.exit(0 if sys.maxsize > 2**32 else 1)'
    if ($LASTEXITCODE -ne 0) { throw '64-bit Python is required for the VR readiness check.' }

    # Check ambiguity before starting anything. The game/loader checks its exact executable path as well.
    $game = Get-WitnessProcess
    if (!(Get-Process -Name vrserver -ErrorAction SilentlyContinue)) {
        Write-Host 'Starting SteamVR...'
        Start-Process 'steam://rungameid/250820'
        $vrDeadline = (Get-Date).AddSeconds(60)
        while (!(Get-Process -Name vrserver -ErrorAction SilentlyContinue)) {
            if ((Get-Date) -ge $vrDeadline) { throw 'SteamVR did not start. Open SteamVR manually, connect the headset, and try again.' }
            Start-Sleep -Seconds 1
        }
    }
    if (!$game) {
        # A first launcher may still be creating its 64-bit child; do not start another copy.
        if (!(Get-Process -Name witness_d3d11 -ErrorAction SilentlyContinue)) {
            Write-Host 'Starting The Witness in VR...'
            Start-Process -FilePath $gameLauncher -ArgumentList '-vr' -WorkingDirectory $GameDir
        }
        $gameDeadline = (Get-Date).AddSeconds(60)
        while (!($game = Get-WitnessProcess)) {
            if ((Get-Date) -ge $gameDeadline) { throw 'The Witness did not start. Check Steam/the game window, then try again.' }
            Start-Sleep -Seconds 1
        }
    } else {
        Write-Host "Using the running Witness game (PID $($game.Id))."
    }
    Write-Host "Waiting up to $WaitSeconds seconds for native VR. Keep the headset awake."
    & $python (Join-Path $projectRoot 'tools\runtime_state.py') --pid $game.Id --game-dir $GameDir --wait-seconds $WaitSeconds
    if ($LASTEXITCODE -ne 0) { throw 'VR readiness check failed. If this game was started without -vr, close it normally and run this launcher again. No fixes were started by this run.' }

    $renderScript = Join-Path $PSScriptRoot 'cursor-session.ps1'
    $inputScript = Join-Path $PSScriptRoot 'controller-session.ps1'
    $renderArgs = @{TargetPid=$game.Id; GameDir=$GameDir}
    $inputArgs = @{TargetPid=$game.Id; GameDir=$GameDir; BuildName=$buildName}
    # Validate both before changing either. Incompatible pinned input builds are refused by the loader.
    $render = Get-ModStatus $renderScript $renderArgs
    $inputStatus = Get-ModStatus $inputScript $inputArgs
    Assert-ModState $render 'Cursor/pause fixes'
    Assert-ModState $inputStatus 'Controller fixes'
    if ($inputStatus.state -eq 'running' -and (!$inputStatus.legacy_axis0 -or !$inputStatus.pointing -or $inputStatus.snap_angle -ne 45)) {
        throw 'The running input session uses different controls. Stop that session deliberately or close the game before using this launcher.'
    }
    Enable-Mod $renderScript $renderArgs @{} $render 'Cursor/pause fixes'
    Enable-Mod $inputScript $inputArgs @{LegacyAxis0=$true; Aim=$true; Snap=$true} $inputStatus 'Controller fixes'
    $render = Get-ModStatus $renderScript $renderArgs
    $inputStatus = Get-ModStatus $inputScript $inputArgs
    foreach ($status in @($render, $inputStatus)) {
        if ($status.state -ne 'running' -or !$status.enabled -or $status.fault) {
            throw 'A mod session did not become healthy. Inspect the startup/session logs; any already-running fixes were left in place.'
        }
    }
    Write-Host ''
    Write-Host 'Ready! Focus The Witness, center both sticks, and release the triggers.' -ForegroundColor Green
    Write-Host "Cursor speeds: stick $($inputStatus.stick_cursor_speed_percent), motion $($inputStatus.aim_speed_percent). Edit config\input.ini to adjust."
    Write-Host 'F7: controller toggle | F8: cursor/pause toggle | F9: stop both'
    Write-Host 'You can close this window. The fixes stay active until the game exits.'
    $result = 0
} catch {
    Write-Host "Startup failed: $($_.Exception.Message)" -ForegroundColor Red
} finally {
    if ($transcribing) { Stop-Transcript | Out-Null }
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
exit $result
